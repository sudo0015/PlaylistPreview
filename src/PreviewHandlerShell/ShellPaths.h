#pragma once

#include <string>

namespace PlaylistPreview::Shell
{
    // Remembered from DllMain.  Every module-relative lookup must use the
    // shell's own directory: prevhost.exe lives in System32 and its process
    // search path contains none of our dependencies.
    void SetShellModuleHandle(HMODULE module) noexcept;

    // The module handle remembered from DllMain.
    HMODULE ShellModuleHandle() noexcept;

    // Directory containing PreviewHandlerShell.dll, without a trailing slash.
    std::wstring ShellModuleDirectory();

    // Full path of PreviewHandlerShell.dll itself.
    std::wstring ShellModulePath();

    // Full path of a file that sits next to PreviewHandlerShell.dll.
    std::wstring ShellModuleSibling(const wchar_t* fileName);
}
