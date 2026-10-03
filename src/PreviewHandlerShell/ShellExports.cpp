#include "pch.h"
#include "ClassFactory.h"
#include "Diag.h"
#include "IslandHost.h"
#include "MessageHook.h"
#include "Registration.h"
#include "ShellPaths.h"
#include "XamlEnvironment.h"
#include "XamlThread.h"

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** instance)
{
    if (instance == nullptr)
    {
        return E_POINTER;
    }
    *instance = nullptr;

    if (!::IsEqualCLSID(clsid, PlaylistPreview::Shell::Registration::kHandlerClsid))
    {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    try
    {
        auto factory = PlaylistPreview::Shell::CreatePreviewHandlerClassFactory();
        if (!factory)
        {
            return E_OUTOFMEMORY;
        }
        return factory->QueryInterface(riid, instance);
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

STDAPI DllCanUnloadNow()
{
    return PlaylistPreview::Shell::ModuleCanUnloadNow() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    return PlaylistPreview::Shell::Registration::RegisterServer();
}

STDAPI DllUnregisterServer()
{
    return PlaylistPreview::Shell::Registration::UnregisterServer();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        PlaylistPreview::Shell::SetShellModuleHandle(module);
        ::DisableThreadLibraryCalls(module);
    }

    if (reason == DLL_PROCESS_DETACH)
    {
        // The XAML environment lives on our own STA thread, so teardown has to
        // happen there.  Never wait for it from DllMain: that would risk a
        // loader-lock deadlock.  The XAML thread does the ordering itself
        // (island -> hook -> environment) when it notices the stop request.
        PlaylistPreview::Shell::StopXamlThreadBestEffort();
    }

    return TRUE;
}
