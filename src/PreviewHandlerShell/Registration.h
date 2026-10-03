#pragma once

#include <guiddef.h>
#include <string>

#include "..\Shared\PreviewInterop.h"

namespace PlaylistPreview::Shell::Registration
{
    inline constexpr GUID kHandlerClsid = kShell_HandlerClsid;

    // System "Preview Handler Surrogate Host" AppID; its DllSurrogate points at
    // prevhost.exe, so this is what puts the handler in the shared proxy.
    inline constexpr wchar_t kSurrogateAppId[] = L"{6d2b5079-2f0b-48dd-ab7f-97cec514d30b}";

    // {8895B1C6-B41F-4C1C-A562-0D564250836F} - IPreviewHandler's IID.
    inline constexpr wchar_t kPreviewHandlerInterface[] = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";

    inline constexpr wchar_t kFriendlyName[] = L"Playlist Preview (CUE / M3U8)";

    // File types this handler claims.  Windows resolves preview handlers purely
    // by extension, so every supported format needs its own entry here.
    inline constexpr const wchar_t* kExtensions[] = { L".cue", L".m3u8" };

    // Writes the CLSID key, InprocServer32, the surrogate AppID and the
    // extension associations.  Prefers the machine-wide hive and falls back to
    // the current user when not elevated.
    HRESULT RegisterServer() noexcept;

    HRESULT UnregisterServer() noexcept;

    std::wstring HandlerClsidString();
}
