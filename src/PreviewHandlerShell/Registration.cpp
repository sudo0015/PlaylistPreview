#include "pch.h"
#include "Registration.h"
#include "Diag.h"
#include "ShellPaths.h"

namespace PlaylistPreview::Shell::Registration
{
    namespace
    {
        std::wstring GuidToString(REFGUID guid)
        {
            wchar_t buffer[40]{};
            if (::StringFromGUID2(guid, buffer, ARRAYSIZE(buffer)) == 0)
            {
                return {};
            }
            return buffer;
        }

        HRESULT SetStringValue(HKEY root, const std::wstring& subKey, const wchar_t* valueName, const std::wstring& value)
        {
            HKEY key = nullptr;
            const LSTATUS created = ::RegCreateKeyExW(root, subKey.c_str(), 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
            if (created != ERROR_SUCCESS)
            {
                return HRESULT_FROM_WIN32(created);
            }

            const LSTATUS written = ::RegSetValueExW(key, valueName, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(value.c_str()),
                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
            ::RegCloseKey(key);
            return HRESULT_FROM_WIN32(written);
        }

        void DeleteKeyTree(HKEY root, const std::wstring& subKey)
        {
            if (!subKey.empty())
            {
                ::RegDeleteTreeW(root, subKey.c_str());
            }
        }

        HRESULT SetDwordValue(HKEY root, const std::wstring& subKey, const wchar_t* valueName, DWORD value)
        {
            HKEY key = nullptr;
            const LSTATUS created = ::RegCreateKeyExW(root, subKey.c_str(), 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
            if (created != ERROR_SUCCESS)
            {
                return HRESULT_FROM_WIN32(created);
            }

            const LSTATUS written = ::RegSetValueExW(key, valueName, 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&value), sizeof(value));
            ::RegCloseKey(key);
            return HRESULT_FROM_WIN32(written);
        }

        HRESULT RegisterUnder(HKEY root, const std::wstring& prefix, bool machineScope)
        {
            const std::wstring clsid = HandlerClsidString();
            if (clsid.empty())
            {
                return E_UNEXPECTED;
            }

            const std::wstring dllPath = ShellModulePath();
            if (dllPath.empty())
            {
                return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
            }

            const std::wstring clsidKey = prefix + L"CLSID\\" + clsid;
            const std::wstring inprocKey = clsidKey + L"\\InprocServer32";

            HRESULT hr = SetStringValue(root, clsidKey, nullptr, kFriendlyName);
            if (FAILED(hr)) { return hr; }

            hr = SetStringValue(root, inprocKey, nullptr, dllPath);
            if (FAILED(hr)) { return hr; }

            hr = SetStringValue(root, inprocKey, L"ThreadingModel", L"Apartment");
            if (FAILED(hr)) { return hr; }

            hr = SetStringValue(root, clsidKey, L"AppID", kSurrogateAppId);
            if (FAILED(hr)) { return hr; }

            // Explorer launches prevhost.exe at Low integrity, and a Low
            // integrity process is not allowed to re-parent its window into
            // Explorer's window (SetParent returns ERROR_ACCESS_DENIED), which is
            // what hosting a WinUI 3 island in the preview pane requires.
            // This is the documented (though discouraged) opt-out; without it the
            // handler can only create a floating window, not an embedded one.
            hr = SetDwordValue(root, clsidKey, L"DisableLowILProcessIsolation", 1);
            if (FAILED(hr)) { return hr; }

            for (const wchar_t* extension : kExtensions)
            {
                const std::wstring shellex = prefix + extension + L"\\shellex\\" + kPreviewHandlerInterface;
                hr = SetStringValue(root, shellex, nullptr, clsid);
                if (FAILED(hr)) { return hr; }
            }

            // Cosmetic only, and machine-wide by definition; ignore failures.
            if (machineScope)
            {
                SetStringValue(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers",
                    clsid.c_str(), kFriendlyName);
            }

            return S_OK;
        }

        void UnregisterUnder(HKEY root, const std::wstring& prefix, bool machineScope)
        {
            const std::wstring clsid = HandlerClsidString();

            for (const wchar_t* extension : kExtensions)
            {
                DeleteKeyTree(root, prefix + extension + L"\\shellex\\" + kPreviewHandlerInterface);
            }

            DeleteKeyTree(root, prefix + L"CLSID\\" + clsid);

            if (machineScope)
            {
                HKEY key = nullptr;
                if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers",
                        0, KEY_WRITE, &key) == ERROR_SUCCESS)
                {
                    ::RegDeleteValueW(key, clsid.c_str());
                    ::RegCloseKey(key);
                }
            }
        }
    }

    std::wstring HandlerClsidString()
    {
        return GuidToString(kHandlerClsid);
    }

    HRESULT RegisterServer() noexcept
    {
        HRESULT hr = RegisterUnder(HKEY_CLASSES_ROOT, L"", true);
        if (SUCCEEDED(hr))
        {
            DiagLog(L"register: machine scope ok");
            return S_OK;
        }
        DiagLogResult(L"register machine scope", hr);

        // Not elevated: registering for the current user still makes Explorer
        // pick the handler up.
        hr = RegisterUnder(HKEY_CURRENT_USER, L"Software\\Classes\\", false);
        if (SUCCEEDED(hr))
        {
            DiagLog(L"register: per-user scope ok");
            return S_OK;
        }
        DiagLogResult(L"register per-user scope", hr);
        return hr;
    }

    HRESULT UnregisterServer() noexcept
    {
        UnregisterUnder(HKEY_CLASSES_ROOT, L"", true);
        UnregisterUnder(HKEY_CURRENT_USER, L"Software\\Classes\\", false);
        DiagLog(L"unregister: done");
        return S_OK;
    }
}
