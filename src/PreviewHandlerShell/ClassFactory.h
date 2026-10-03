#pragma once

namespace PlaylistPreview::Shell
{
    // COM object / lock accounting for DllCanUnloadNow.
    void AddObjectRef() noexcept;
    void ReleaseObjectRef() noexcept;
    void AddLockRef() noexcept;
    void ReleaseLockRef() noexcept;
    bool ModuleCanUnloadNow() noexcept;

    winrt::com_ptr<IClassFactory> CreatePreviewHandlerClassFactory();
}
