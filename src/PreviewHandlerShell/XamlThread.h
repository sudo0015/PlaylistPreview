#pragma once

#include <functional>

namespace PlaylistPreview::Shell
{
    // WinUI 3 requires an STA thread with a message pump, but prevhost.exe calls
    // the handler on a thread that is already initialised as MTA, so we cannot
    // just call init_apartment on the caller's thread (it fails with
    // RPC_E_CHANGED_MODE).
    //
    // The shell therefore owns one dedicated STA thread: it initialises the XAML
    // environment, installs the ContentPreTranslateMessage hook and runs the
    // island.  Every COM entry point marshals onto it.

    // Starts the thread on first use.  Idempotent.
    HRESULT EnsureXamlThread();

    // Runs `work` on the XAML thread and waits (bounded) for completion.
    // Returns S_OK when it completed, S_FALSE when the caller had to be released
    // before the work finished (the work stays queued; see XamlThread.cpp), or
    // the transport HRESULT when the thread could not be used at all.  Capture
    // your own result inside `work`.
    HRESULT InvokeOnXamlThread(std::function<void()> const& work) noexcept;

    DWORD XamlThreadId() noexcept;

    // Best effort and non-blocking, so it is safe to call from DllMain.
    void StopXamlThreadBestEffort() noexcept;
}
