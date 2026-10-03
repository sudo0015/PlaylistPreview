#include "pch.h"
#include "XamlEnvironment.h"
#include "Diag.h"
#include "PreviewApplication.h"

#include <Microsoft.UI.Dispatching.Interop.h>

namespace PlaylistPreview::Shell
{
    namespace
    {
        winrt::Microsoft::UI::Dispatching::DispatcherQueueController g_dispatcherQueueController{ nullptr };
        winrt::Microsoft::UI::Xaml::Application g_application{ nullptr };
        winrt::Microsoft::UI::Xaml::Hosting::WindowsXamlManager g_xamlManager{ nullptr };
        bool g_environmentReady{ false };
        PFN_ContentPreTranslateMessage g_preTranslateMessage{ nullptr };

        bool g_controlsResourcesMerged{ false };

        // ContentPreTranslateMessage lives in Microsoft.UI.Windowing.Core.dll,
        // which only becomes resolvable once the Windows App Runtime has been
        // bootstrapped.  Importing it statically would fail our DLL load.
        PFN_ContentPreTranslateMessage ResolveContentPreTranslateMessage()
        {
            if (g_preTranslateMessage != nullptr)
            {
                return g_preTranslateMessage;
            }

            HMODULE module = ::GetModuleHandleW(L"Microsoft.UI.Windowing.Core.dll");
            if (module == nullptr)
            {
                module = ::LoadLibraryExW(L"Microsoft.UI.Windowing.Core.dll", nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            }

            if (module == nullptr)
            {
                // Fall back to the framework package directory, located through
                // a Windows App SDK module that is already loaded by now.
                if (HMODULE xaml = ::GetModuleHandleW(L"Microsoft.ui.xaml.dll"))
                {
                    std::wstring path(MAX_PATH, L'\0');
                    const DWORD length = ::GetModuleFileNameW(xaml, path.data(), static_cast<DWORD>(path.size()));
                    if ((length > 0) && (length < path.size()))
                    {
                        path.resize(length);
                        const size_t separator = path.find_last_of(L"\\/");
                        if (separator != std::wstring::npos)
                        {
                            path = path.substr(0, separator + 1) + L"Microsoft.UI.Windowing.Core.dll";
                            module = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
                        }
                    }
                }
            }

            if (module != nullptr)
            {
                g_preTranslateMessage = reinterpret_cast<PFN_ContentPreTranslateMessage>(
                    ::GetProcAddress(module, "ContentPreTranslateMessage"));
            }

            return g_preTranslateMessage;
        }
    }

    HRESULT EnsureXamlEnvironment()
    {
        if (g_environmentReady)
        {
            return S_OK;
        }

        DiagLog(L"EnsureXamlEnvironment: begin");

        // The island must live on an STA thread with a message pump.  If the
        // host already initialized this thread as MTA this fails loudly rather
        // than producing a half-working island.
        try
        {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
        }
        catch (winrt::hresult_error const& e)
        {
            DiagLogResult(L"init_apartment(STA)", e.code());
            return e.code();
        }
        DiagLog(L"  init_apartment(STA): ok");

        try
        {
            g_dispatcherQueueController = winrt::Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();
            DiagLog(L"  DispatcherQueueController: ok");

            g_application = CreatePreviewApplication();
            DiagLog(L"  Application: ok");

            g_xamlManager = winrt::Microsoft::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
            DiagLog(L"  WindowsXamlManager: ok");

            // Without XamlControlsResources the WinUI control templates and
            // theme brushes are missing and content renders blank or throws.
            //
            // This must happen *after* WindowsXamlManager initialized the XAML
            // core on this thread: touching Application.Resources earlier
            // fails with RPC_E_WRONG_THREAD.
        }
        catch (winrt::hresult_error const& e)
        {
            DiagLogResult(L"xaml environment", e.code());
            DiagLog(L"    message: %s", e.message().c_str());
            ShutdownXamlEnvironment();
            return e.code();
        }

        g_environmentReady = true;
        return S_OK;
    }

    // Merging XamlControlsResources is what gives WinUI controls their default
    // templates and theme brushes.  The official Islands sample does this from
    // Application.OnLaunched, i.e. only once the framework is fully up, so we
    // do it after the island source exists rather than during construction.
    // A failure here is logged but must not sink the island.
    void EnsureControlsResources()
    {
        if (g_controlsResourcesMerged)
        {
            return;
        }

        DiagLog(L"EnsureControlsResources: begin");
        try
        {
            auto resources = g_application.Resources();
            DiagLog(L"  Application.Resources: ok");

            auto merged = resources.MergedDictionaries();
            DiagLog(L"  MergedDictionaries: ok");

            winrt::Microsoft::UI::Xaml::Controls::XamlControlsResources controlsResources{};
            DiagLog(L"  XamlControlsResources ctor: ok");

            merged.Append(controlsResources);
            DiagLog(L"  XamlControlsResources merged: ok");
            g_controlsResourcesMerged = true;
        }
        catch (winrt::hresult_error const& e)
        {
            DiagLogResult(L"XamlControlsResources", e.code());
            DiagLog(L"    message: %s", e.message().c_str());
        }
    }

    bool PreTranslateMessage(const MSG& message)
    {
        if (!g_environmentReady)
        {
            return false;
        }

        const PFN_ContentPreTranslateMessage preTranslate = ResolveContentPreTranslateMessage();
        if (preTranslate == nullptr)
        {
            return false;
        }

        return preTranslate(&message) != FALSE;
    }

    void ShutdownXamlEnvironment() noexcept
    {
        g_xamlManager = nullptr;
        g_application = nullptr;
        g_dispatcherQueueController = nullptr;
        g_environmentReady = false;
    }

    bool XamlEnvironmentReady() noexcept
    {
        return g_environmentReady;
    }
}
