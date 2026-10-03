#pragma once

namespace PlaylistPreview::Shell
{
    // Bootstraps the Windows App SDK runtime for this process.
    //
    // Safe to call repeatedly; the first successful call performs
    // MddBootstrapInitialize2 and every call must be paired with
    // ReleaseAppSdkRuntime().  Returns the bootstrap HRESULT so callers can
    // surface "runtime missing" to the preview host instead of fail-fasting
    // inside prevhost.exe.
    HRESULT EnsureAppSdkRuntime() noexcept;

    void ReleaseAppSdkRuntime() noexcept;
}
