#include "pch.h"
#include "ShellPaths.h"

namespace PlaylistPreview::Shell
{
    namespace
    {
        HMODULE g_shellModule{ nullptr };
    }

    void SetShellModuleHandle(HMODULE module) noexcept
    {
        g_shellModule = module;
    }

    HMODULE ShellModuleHandle() noexcept
    {
        return g_shellModule;
    }

    std::wstring ShellModuleDirectory()
    {
        std::wstring path(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = ::GetModuleFileNameW(g_shellModule, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < path.size())
            {
                path.resize(length);
                break;
            }
            path.resize(path.size() * 2);
        }

        const size_t separator = path.find_last_of(L"\\/");
        return (separator == std::wstring::npos) ? std::wstring{} : path.substr(0, separator);
    }

    std::wstring ShellModuleSibling(const wchar_t* fileName)
    {
        const std::wstring directory = ShellModuleDirectory();
        if (directory.empty())
        {
            return {};
        }
        return directory + L"\\" + fileName;
    }

    std::wstring ShellModulePath()
    {
        std::wstring path(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = ::GetModuleFileNameW(g_shellModule, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < path.size())
            {
                path.resize(length);
                return path;
            }
            path.resize(path.size() * 2);
        }
    }
}
