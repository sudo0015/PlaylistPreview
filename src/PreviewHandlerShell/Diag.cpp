#include "pch.h"
#include "Diag.h"
#include "ShellPaths.h"

#include <cstdarg>
#include <cstdio>

namespace PlaylistPreview::Shell
{
    namespace
    {
        std::mutex g_logLock;
        bool g_initialized{ false };
        std::wstring g_logPath;
        std::wstring g_fallbackPath;

        // prevhost.exe runs at Low integrity, and a Low integrity process cannot
        // write to a folder created by a medium integrity one.  Fall back to the
        // only place it is always allowed to write.
        std::wstring LocalLowLogPath()
        {
            wchar_t buffer[MAX_PATH * 2]{};
            DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, ARRAYSIZE(buffer));
            if ((length > 0) && (length < ARRAYSIZE(buffer)))
            {
                return std::wstring{ buffer } + L"Low\\PlaylistPreview\\PreviewHandlerShell.log";
            }

            length = ::GetEnvironmentVariableW(L"USERPROFILE", buffer, ARRAYSIZE(buffer));
            if ((length > 0) && (length < ARRAYSIZE(buffer)))
            {
                return std::wstring{ buffer } + L"\\AppData\\LocalLow\\PlaylistPreview\\PreviewHandlerShell.log";
            }

            return {};
        }

        void Initialize()
        {
            if (g_initialized)
            {
                return;
            }
            g_initialized = true;

            wchar_t value[MAX_PATH * 2]{};
            const DWORD length = ::GetEnvironmentVariableW(L"PLAYLISTPREVIEW_SHELL_LOG", value, ARRAYSIZE(value));
            if ((length > 0) && (length < ARRAYSIZE(value)))
            {
                if (::wcscmp(value, L"1") == 0)
                {
                    g_logPath = ShellModuleSibling(L"PreviewHandlerShell.log");
                }
                else
                {
                    g_logPath = value;
                }
                return;
            }

            // prevhost.exe is launched by the COM SCM, so it inherits SYSTEM's
            // environment rather than the user's -- an env var cannot reach it.
            // A marker file next to the DLL is something we can always see.
            const std::wstring marker = ShellModuleSibling(kDiagnosticsMarkerFile);
            if (::GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                g_logPath = ShellModuleSibling(L"PreviewHandlerShell.log");
            }

            if (!g_logPath.empty())
            {
                g_fallbackPath = LocalLowLogPath();
            }
        }
    }

    bool DiagnosticsEnabled()
    {
        std::lock_guard guard{ g_logLock };
        Initialize();
        return !g_logPath.empty();
    }

    void DiagLog(const wchar_t* format, ...)
    {
        std::lock_guard guard{ g_logLock };
        Initialize();
        if (g_logPath.empty())
        {
            return;
        }

        wchar_t buffer[1024]{};
        va_list arguments;
        va_start(arguments, format);
        ::_vsnwprintf_s(buffer, _TRUNCATE, format, arguments);
        va_end(arguments);

        const std::wstring line = std::wstring{ buffer } + L"\r\n";
        const int size = ::WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return;
        }

        std::string utf8(static_cast<size_t>(size), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(), size, nullptr, nullptr);

        HANDLE file = ::CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if ((file == INVALID_HANDLE_VALUE) && (::GetLastError() == ERROR_ACCESS_DENIED) && !g_fallbackPath.empty())
        {
            // Low integrity: the folder next to the DLL is out of reach.
            const size_t separator = g_fallbackPath.find_last_of(L'\\');
            if (separator != std::wstring::npos)
            {
                ::CreateDirectoryW(g_fallbackPath.substr(0, separator).c_str(), nullptr);
            }

            file = ::CreateFileW(g_fallbackPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                g_logPath = g_fallbackPath;
            }
        }

        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }

        DWORD written = 0;
        ::WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        ::CloseHandle(file);
    }

    void DiagLogResult(const wchar_t* step, HRESULT result)
    {
        if (SUCCEEDED(result))
        {
            DiagLog(L"  %s: ok (0x%08X)", step, static_cast<unsigned int>(result));
        }
        else
        {
            DiagLog(L"  %s: FAILED (0x%08X)", step, static_cast<unsigned int>(result));
        }
    }
}
