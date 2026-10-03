#include "pch.h"
#include "PreviewRoot.h"

#include "..\Shared\PreviewInterop.h"

extern "C" HRESULT __stdcall PlaylistPreviewUI_CreatePreviewRoot(IInspectable** content) noexcept
{
    if (content == nullptr)
    {
        return E_POINTER;
    }
    *content = nullptr;

    try
    {
        auto root = PlaylistPreviewUI::CreatePreviewRoot();
        *content = static_cast<IInspectable*>(winrt::detach_abi(root));
        return S_OK;
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

extern "C" HRESULT __stdcall PlaylistPreviewUI_SetText(
    IInspectable* root,
    const wchar_t* displayName,
    const char* utf8Text,
    uint32_t byteLength) noexcept
{
    if ((utf8Text == nullptr) && (byteLength != 0))
    {
        return E_POINTER;
    }

    try
    {
        const std::string text{ utf8Text, utf8Text + byteLength };
        PlaylistPreviewUI::SetText(root, displayName == nullptr ? std::wstring{} : std::wstring{ displayName }, text);
        return S_OK;
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

extern "C" HRESULT __stdcall PlaylistPreviewUI_SetVisuals(
    IInspectable* root,
    int32_t theme,
    int32_t useHostColors,
    uint32_t backgroundRef,
    uint32_t textRef,
    const wchar_t* fontFamily,
    float fontSize) noexcept
{
    try
    {
        PlaylistPreviewUI::SetVisuals(
            root,
            theme,
            useHostColors != 0,
            backgroundRef,
            textRef,
            fontFamily == nullptr ? std::wstring{} : std::wstring{ fontFamily },
            fontSize);
        return S_OK;
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

extern "C" void __stdcall PlaylistPreviewUI_ReleasePreviewRoot(IInspectable* root) noexcept
{
    PlaylistPreviewUI::ReleasePreviewRoot(root);
}
