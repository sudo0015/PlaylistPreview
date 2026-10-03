#include "pch.h"
#include "XamlThread.h"
#include "AppSdkBootstrap.h"
#include "Diag.h"
#include "IslandHost.h"
#include "MessageHook.h"
#include "XamlEnvironment.h"

#include <deque>
#include <memory>
#include <thread>
#include <atomic>

namespace PlaylistPreview::Shell
{
    namespace
    {
        // How long a COM entry point is willing to block the caller before it
        // hands control back.  See InvokeOnXamlThread for why this is bounded.
        constexpr DWORD kTaskWaitMilliseconds = 1000;

        // How long the process exit path waits for the XAML thread to stop
        // before the loader starts unloading the framework.  See
        // StopXamlThreadBestEffort.
        constexpr DWORD kDetachGraceMilliseconds = 2000;

        // How often the islands are handed their visuals again so that a Windows
        // light/dark switch reaches panes that are already open.
        constexpr ULONGLONG kVisualRefreshMilliseconds = 1000;

        struct Task
        {
            std::function<void()> work;
            HANDLE done{ nullptr };

            ~Task()
            {
                if (done != nullptr)
                {
                    ::CloseHandle(done);
                }
            }
        };

        std::mutex g_lock;
        std::thread g_thread;
        DWORD g_threadId{ 0 };
        HANDLE g_readyEvent{ nullptr };
        HANDLE g_workEvent{ nullptr };
        HANDLE g_threadHandle{ nullptr };
        HRESULT g_startResult{ E_PENDING };
        std::deque<std::shared_ptr<Task>> g_tasks;
        std::atomic<bool> g_stopRequested{ false };
        std::atomic<bool> g_processDetach{ false };
        bool g_started{ false };
        ULONGLONG g_nextVisualRefresh{ 0 };

        void RunTask(std::shared_ptr<Task> const& task)
        {
            try
            {
                task->work();
            }
            catch (winrt::hresult_error const& e)
            {
                DiagLogResult(L"XAML thread task", e.code());
            }
            catch (...)
            {
                DiagLog(L"XAML thread task threw an unknown exception");
            }

            if (task->done != nullptr)
            {
                ::SetEvent(task->done);
            }
        }

        void XamlThreadMain()
        {
            g_threadId = ::GetCurrentThreadId();
            DiagLog(L"XAML thread started (id=%lu)", g_threadId);

            HRESULT hr = S_OK;
            try
            {
                // Bootstrap first: this thread is about to activate Windows App
                // SDK WinRT classes, which fails until the runtime is available.
                hr = EnsureAppSdkRuntime();
                DiagLogResult(L"App SDK bootstrap", hr);

                if (SUCCEEDED(hr))
                {
                    hr = EnsureXamlEnvironment();
                }
            }
            catch (winrt::hresult_error const& e)
            {
                hr = e.code();
            }
            catch (...)
            {
                hr = winrt::to_hresult();
            }

            if (SUCCEEDED(hr) && !InstallMessageHook())
            {
                DiagLog(L"WARNING: message hook failed; keyboard input will not reach the island");
            }

            g_startResult = hr;
            ::SetEvent(g_readyEvent);

            if (FAILED(hr))
            {
                return;
            }

            while (!g_stopRequested.load())
            {
                // Whatever the COM threads queued up for us.
                for (;;)
                {
                    std::shared_ptr<Task> task;
                    {
                        std::lock_guard guard{ g_lock };
                        if (g_tasks.empty())
                        {
                            break;
                        }
                        task = g_tasks.front();
                        g_tasks.pop_front();
                    }
                    RunTask(task);
                }

                // Wait for either new work or a window message.  The timeout is
                // a safety net so we never sleep through a stop request.
                const HANDLE handles[1] = { g_workEvent };
                ::MsgWaitForMultipleObjectsEx(1, handles, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

                // Create the island if the host window has finally been shown
                // (Explorer calls DoPreview before showing a reopened pane).
                ProcessDeferredIslandCreation();

                // Windows can switch between light and dark while a pane is open;
                // the content library resolves the mode, so all we do is ask it
                // to look again.
                const ULONGLONG now = ::GetTickCount64();
                if (now >= g_nextVisualRefresh)
                {
                    g_nextVisualRefresh = now + kVisualRefreshMilliseconds;
                    RefreshIslandVisuals();
                }

                MSG message{};
                while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT)
                    {
                        g_stopRequested = true;
                        break;
                    }

                    // The island is hosted here, so this thread owns the message
                    // hook that feeds ContentPreTranslateMessage.  Do not call
                    // TranslateMessage/DispatchMessage ourselves for messages the
                    // hook already consumed.
                    if (message.message != WM_NULL)
                    {
                        ::TranslateMessage(&message);
                        ::DispatchMessageW(&message);
                    }
                }
            }

            DiagLog(L"XAML thread stopping");
            if (g_processDetach.load())
            {
                // The process is on its way out: every framework DLL (including
                // Microsoft.UI.Xaml) is about to be unloaded, so running XAML
                // teardown here would execute code in modules that are already
                // going away -- which is exactly what produced access violations
                // at exit.  The OS reclaims everything anyway.
                DiagLog(L"  process exiting: skipping XAML teardown");
            }
            else
            {
                DestroyAllIslands();
                RemoveMessageHook();
                ShutdownXamlEnvironment();
                ReleaseAppSdkRuntime();
            }
        }
    }

    DWORD XamlThreadId() noexcept
    {
        return g_threadId;
    }

    HRESULT EnsureXamlThread()
    {
        std::lock_guard guard{ g_lock };
        if (g_started)
        {
            return g_startResult;
        }

        g_readyEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_workEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if ((g_readyEvent == nullptr) || (g_workEvent == nullptr))
        {
            return HRESULT_FROM_WIN32(::GetLastError());
        }

        g_thread = std::thread(&XamlThreadMain);

        // Detach immediately.  We never join: the thread is asked to stop with a
        // WM_QUIT plus a flag.  Leaving it joinable would make the std::thread
        // destructor call std::terminate when the DLL is unloaded inside the
        // host process (for example when prevhost.exe exits).
        g_thread.detach();

        // Pin this module for the remaining process lifetime.  The XAML thread
        // executes code in this DLL, so the module must never be unloaded while
        // that thread is alive.  Pinning also means DllMain(PROCESS_DETACH) can
        // only run at process termination, where no further COM call can arrive.
        HMODULE pinned = nullptr;
        ::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&XamlThreadMain),
            &pinned);

        g_started = true;

        // g_threadId is published by XamlThreadMain before it signals readiness,
        // and no COM entry point runs before we return here, so
        // InvokeOnXamlThread can never mistake the caller for the XAML thread.
        ::WaitForSingleObject(g_readyEvent, INFINITE);
        DiagLogResult(L"XAML thread environment", g_startResult);

        // A waitable handle for the exit path (the std::thread is detached, so
        // this is the only way to tell when the thread has actually stopped).
        g_threadHandle = ::OpenThread(SYNCHRONIZE, FALSE, g_threadId);

        return g_startResult;
    }

    HRESULT InvokeOnXamlThread(std::function<void()> const& work) noexcept
    {
        if ((g_threadId != 0) && (::GetCurrentThreadId() == g_threadId))
        {
            work();
            return S_OK;
        }

        const HRESULT ready = EnsureXamlThread();
        if (FAILED(ready))
        {
            return ready;
        }

        auto task = std::make_shared<Task>();
        task->work = work;
        task->done = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (task->done == nullptr)
        {
            return HRESULT_FROM_WIN32(::GetLastError());
        }

        {
            std::lock_guard guard{ g_lock };
            g_tasks.push_back(task);
        }
        ::SetEvent(g_workEvent);

        // Wait for the task, but keep servicing this thread's messages while we
        // do.
        //
        // Some XAML work cannot finish unless the thread that hosts the island
        // keeps pumping: navigating focus into the tree and tearing a content
        // tree down both do a synchronous round trip to the island's parent
        // window.  A plain blocking wait therefore deadlocks both sides -- and
        // that thread is Explorer's UI thread, so the symptom is a frozen
        // preview pane (or a frozen Explorer).  Pumping unblocks those cases;
        // the timeout is the safety net for a host that is itself blocked and
        // can never deliver the message (Explorer waiting on this very call).
        //
        // Returning early is safe: the task stays queued in order, keeps its own
        // reference, and finishes as soon as this thread pumps again.
        const ULONGLONG deadline = ::GetTickCount64() + kTaskWaitMilliseconds;
        for (;;)
        {
            const ULONGLONG now = ::GetTickCount64();
            if (now >= deadline)
            {
                break;
            }

            const HANDLE handles[1] = { task->done };
            const DWORD index = ::MsgWaitForMultipleObjectsEx(
                1, handles, static_cast<DWORD>(deadline - now), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (index == WAIT_OBJECT_0)
            {
                return S_OK;
            }
            if (index != (WAIT_OBJECT_0 + 1))
            {
                break;   // timeout or failure
            }

            MSG message{};
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    ::PostQuitMessage(static_cast<int>(message.wParam));
                    continue;
                }
                if (message.message != WM_NULL)   // already consumed by XAML
                {
                    ::TranslateMessage(&message);
                    ::DispatchMessageW(&message);
                }
            }
        }

        DiagLog(L"InvokeOnXamlThread: task still running after %lu ms; releasing the caller",
            static_cast<unsigned long>(kTaskWaitMilliseconds));
        return S_FALSE;
    }

    void StopXamlThreadBestEffort() noexcept
    {
        // Runs from DllMain (PROCESS_DETACH), so: no locks, no allocation, no
        // XAML teardown.
        //
        // g_processDetach tells the XAML thread to skip its own teardown, and
        // the bounded wait below keeps the process from tearing the framework
        // down while that thread is still executing inside it -- that race is
        // what made the host die with an access violation on the way out.  The
        // thread polls the stop flag every 100 ms, so this normally returns
        // almost immediately.
        g_processDetach = true;
        g_stopRequested = true;

        if (g_threadHandle != nullptr)
        {
            ::WaitForSingleObject(g_threadHandle, kDetachGraceMilliseconds);
        }
    }
}
