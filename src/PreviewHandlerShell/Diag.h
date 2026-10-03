#pragma once

namespace PlaylistPreview::Shell
{
    // Create this file next to PreviewHandlerShell.dll to turn logging on when
    // an environment variable cannot reach the host process (prevhost.exe is
    // started by the COM SCM and inherits SYSTEM's environment).
    inline constexpr wchar_t kDiagnosticsMarkerFile[] = L"PreviewHandlerShell.enable-log";

    // Diagnostic logging for the shell.
    //
    // Off by default: a preview handler must not write files into a host
    // process's folder behind the user's back.  Set the environment variable
    // PLAYLISTPREVIEW_SHELL_LOG to a file path (or to "1" to use
    // PreviewHandlerShell.log next to the DLL) to turn it on.  tools/dev and
    // tools/IslandTestHost use this for Phase 1/2 bring-up.
    bool DiagnosticsEnabled();

    void DiagLog(const wchar_t* format, ...);

    void DiagLogResult(const wchar_t* step, HRESULT result);
}
