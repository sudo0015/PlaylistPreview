#include "pch.h"
#include "AppSdkBootstrap.h"
#include "ShellPaths.h"

#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>

namespace PlaylistPreview::Shell
{
    namespace
    {
        // Imported dynamically from Microsoft.WindowsAppRuntime.Bootstrap.dll,
        // which we load by full path from our own directory.  A static import
        // would fail: prevhost.exe resolves our imports against System32 and
        // PATH, neither of which contains our install folder.
        using PfnMddBootstrapInitialize2 = HRESULT(STDAPICALLTYPE*)(UINT32, PCWSTR, PACKAGE_VERSION, MddBootstrapInitializeOptions);
        using PfnMddBootstrapShutdown = void(STDAPICALLTYPE*)(void);

        std::mutex g_bootstrapLock;
        int32_t g_bootstrapRefCount{ 0 };
        HRESULT g_bootstrapResult{ S_OK };
        HMODULE g_bootstrapModule{ nullptr };
        PfnMddBootstrapInitialize2 g_initialize{ nullptr };
        PfnMddBootstrapShutdown g_shutdown{ nullptr };

        HRESULT LoadBootstrapLibrary()
        {
            if (g_initialize != nullptr)
            {
                return S_OK;
            }

            const std::wstring path = ShellModuleSibling(L"Microsoft.WindowsAppRuntime.Bootstrap.dll");
            if (path.empty())
            {
                return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
            }

            HMODULE module = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (module == nullptr)
            {
                return HRESULT_FROM_WIN32(::GetLastError());
            }

            auto initialize = reinterpret_cast<PfnMddBootstrapInitialize2>(::GetProcAddress(module, "MddBootstrapInitialize2"));
            auto shutdown = reinterpret_cast<PfnMddBootstrapShutdown>(::GetProcAddress(module, "MddBootstrapShutdown"));
            if ((initialize == nullptr) || (shutdown == nullptr))
            {
                ::FreeLibrary(module);
                return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
            }

            g_bootstrapModule = module;
            g_initialize = initialize;
            g_shutdown = shutdown;
            return S_OK;
        }
    }

    HRESULT EnsureAppSdkRuntime() noexcept
    {
        std::lock_guard guard{ g_bootstrapLock };

        if (g_bootstrapRefCount > 0)
        {
            ++g_bootstrapRefCount;
            return g_bootstrapResult;
        }

        HRESULT hr = LoadBootstrapLibrary();
        if (FAILED(hr))
        {
            g_bootstrapResult = hr;
            return hr;
        }

        // Same arguments the SDK's own auto-initializer uses, minus the
        // "show UI / fail fast" options: a preview handler must never pop a
        // dialog out of prevhost.exe.
        const UINT32 majorMinorVersion{ WINDOWSAPPSDK_RELEASE_MAJORMINOR };
        PCWSTR versionTag{ WINDOWSAPPSDK_RELEASE_VERSION_TAG_W };
        const PACKAGE_VERSION minVersion{ WINDOWSAPPSDK_RUNTIME_VERSION_UINT64 };

        hr = g_initialize(
            majorMinorVersion,
            versionTag,
            minVersion,
            MddBootstrapInitializeOptions_None);

        g_bootstrapResult = hr;
        if (SUCCEEDED(hr))
        {
            ++g_bootstrapRefCount;
        }
        return hr;
    }

    void ReleaseAppSdkRuntime() noexcept
    {
        std::lock_guard guard{ g_bootstrapLock };

        if ((g_bootstrapRefCount > 0) && (--g_bootstrapRefCount == 0))
        {
            g_shutdown();
            g_bootstrapResult = S_OK;
        }
    }
}
