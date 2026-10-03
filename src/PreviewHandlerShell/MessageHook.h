#pragma once

namespace PlaylistPreview::Shell
{
    // A preview handler does not own the host's message loop, but WinUI 3 needs
    // every message offered to ContentPreTranslateMessage before it is
    // dispatched -- otherwise keyboard input never reaches the island (the
    // .NET Islands sample does the same thing with an IMessageFilter).
    //
    // We install a WH_GETMESSAGE hook on our own thread, so nothing is injected
    // into the host process beyond this thread's message queue.
    bool InstallMessageHook();

    void RemoveMessageHook() noexcept;

    bool MessageHookInstalled() noexcept;
}
