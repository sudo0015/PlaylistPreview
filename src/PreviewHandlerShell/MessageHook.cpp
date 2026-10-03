#include "pch.h"
#include "MessageHook.h"
#include "Diag.h"
#include "ShellPaths.h"
#include "XamlEnvironment.h"

#include <atomic>

namespace PlaylistPreview::Shell
{
    namespace
    {
        HHOOK g_hook{ nullptr };
        std::atomic<long> g_messagesOffered{ 0 };
        std::atomic<long> g_messagesConsumed{ 0 };

        LRESULT CALLBACK GetMessageHook(int code, WPARAM wparam, LPARAM lparam)
        {
            if ((code >= 0) && (wparam == PM_REMOVE))
            {
                auto* message = reinterpret_cast<MSG*>(lparam);
                if (message != nullptr)
                {
                    ++g_messagesOffered;
                    if (PreTranslateMessage(*message))
                    {
                        // XAML consumed this message (typically a key press).
                        ++g_messagesConsumed;
                        message->message = WM_NULL;
                    }
                }
            }

            return ::CallNextHookEx(nullptr, code, wparam, lparam);
        }
    }

    bool InstallMessageHook()
    {
        if (g_hook != nullptr)
        {
            return true;
        }

        g_hook = ::SetWindowsHookExW(WH_GETMESSAGE, GetMessageHook, ShellModuleHandle(), ::GetCurrentThreadId());
        if (g_hook == nullptr)
        {
            DiagLogResult(L"SetWindowsHookEx(WH_GETMESSAGE)", HRESULT_FROM_WIN32(::GetLastError()));
            return false;
        }

        return true;
    }

    void RemoveMessageHook() noexcept
    {
        if (g_hook != nullptr)
        {
            ::UnhookWindowsHookEx(g_hook);
            g_hook = nullptr;
            DiagLog(L"message hook removed: offered=%ld, consumed by XAML=%ld",
                g_messagesOffered.load(), g_messagesConsumed.load());
            g_messagesOffered = 0;
            g_messagesConsumed = 0;
        }
    }

    bool MessageHookInstalled() noexcept
    {
        return g_hook != nullptr;
    }
}
