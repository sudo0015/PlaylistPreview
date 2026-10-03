#include "pch.h"
#include "ClassFactory.h"
#include "Diag.h"
#include "PreviewHandler.h"
#include "XamlEnvironment.h"

#include <atomic>
#include <new>

namespace
{
    std::atomic<long> g_objectCount{ 0 };
    std::atomic<long> g_lockCount{ 0 };

    class PreviewHandlerClassFactory : public winrt::implements<PreviewHandlerClassFactory, IClassFactory>
    {
    public:
        HRESULT __stdcall CreateInstance(IUnknown* outer, REFIID riid, void** instance) noexcept override
        {
            if (instance == nullptr)
            {
                return E_POINTER;
            }
            *instance = nullptr;

            if (outer != nullptr)
            {
                return CLASS_E_NOAGGREGATION;
            }

            try
            {
                auto handler = winrt::make_self<PlaylistPreview::Shell::PreviewHandler>();
                return handler->QueryInterface(riid, instance);
            }
            catch (std::bad_alloc const&)
            {
                return E_OUTOFMEMORY;
            }
            catch (winrt::hresult_error const& e)
            {
                return e.code();
            }
            catch (...)
            {
                return winrt::to_hresult();
            }
        }

        HRESULT __stdcall LockServer(BOOL lock) noexcept override
        {
            if (lock != FALSE)
            {
                PlaylistPreview::Shell::AddLockRef();
            }
            else
            {
                PlaylistPreview::Shell::ReleaseLockRef();
            }
            return S_OK;
        }
    };
}

namespace PlaylistPreview::Shell
{
    void AddObjectRef() noexcept
    {
        ++g_objectCount;
    }

    void ReleaseObjectRef() noexcept
    {
        --g_objectCount;
    }

    void AddLockRef() noexcept
    {
        ++g_lockCount;
    }

    void ReleaseLockRef() noexcept
    {
        --g_lockCount;
    }

    bool ModuleCanUnloadNow() noexcept
    {
        // Deliberately conservative: the process-wide XAML environment
        // (Application / DispatcherQueue / WindowsXamlManager) cannot be torn
        // down and rebuilt safely, so once it exists we stay mapped.
        if (XamlEnvironmentReady())
        {
            return false;
        }

        return (g_objectCount.load() == 0) && (g_lockCount.load() == 0);
    }

    winrt::com_ptr<IClassFactory> CreatePreviewHandlerClassFactory()
    {
        auto self = winrt::make_self<PreviewHandlerClassFactory>();

        winrt::com_ptr<IClassFactory> factory;
        winrt::check_hresult(self->QueryInterface(IID_IClassFactory, factory.put_void()));
        return factory;
    }
}
