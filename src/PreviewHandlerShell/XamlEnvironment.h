#pragma once

namespace PlaylistPreview::Shell
{
    using PFN_ContentPreTranslateMessage = BOOL(__stdcall*)(const MSG*);

    // Brings up WinUI 3 for the calling thread:
    //   STA init -> DispatcherQueue -> Application -> XamlControlsResources
    //   -> WindowsXamlManager.InitializeForCurrentThread()
    //
    // Must run on the thread that will own the island; WinUI objects have
    // thread affinity.  Idempotent per thread.
    HRESULT EnsureXamlEnvironment();

    bool XamlEnvironmentReady() noexcept;

    // Merges XamlControlsResources into the application, giving WinUI controls
    // their default templates and theme brushes.  Must be called once the
    // island infrastructure exists; see the implementation for why.
    void EnsureControlsResources();

    // Forwards a message to the Windows App SDK input stack.  The host message
    // loop must call this before TranslateMessage/DispatchMessage for as long
    // as an island is alive, otherwise keyboard input never reaches XAML.
    bool PreTranslateMessage(const MSG& message);

    // Tears the XAML environment down.  Call only after every island has been
    // released (see docs/registration.md for the required ordering).
    void ShutdownXamlEnvironment() noexcept;
}
