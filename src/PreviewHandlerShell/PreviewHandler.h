#pragma once

#include <ShObjIdl.h>
#include <propsys.h>

#include <mutex>
#include <string>
#include <vector>

#include "IslandHost.h"

namespace PlaylistPreview::Shell
{
    // The preview handler itself.
    //
    // Threading model is Apartment: every call arrives on the thread that owns
    // the island, which is also the thread the XAML environment lives on.
    // Not `final` and with a public destructor: C++/WinRT requires that for
    // winrt::make_self, which is how instances are created.
    class PreviewHandler : public winrt::implements<
        PreviewHandler,
        IInitializeWithStream,
        IInitializeWithFile,
        IPreviewHandler,
        IObjectWithSite,
        IOleWindow,
        IPreviewHandlerVisuals>
    {
    public:
        PreviewHandler();
        ~PreviewHandler();

        // IInitializeWithStream
        HRESULT __stdcall Initialize(IStream* stream, DWORD mode) noexcept override;
        // IInitializeWithFile
        HRESULT __stdcall Initialize(LPCWSTR filePath, DWORD mode) noexcept override;

        // IPreviewHandler
        HRESULT __stdcall SetWindow(HWND window, const RECT* rect) noexcept override;
        HRESULT __stdcall SetRect(const RECT* rect) noexcept override;
        HRESULT __stdcall DoPreview() noexcept override;
        HRESULT __stdcall Unload() noexcept override;
        HRESULT __stdcall SetFocus() noexcept override;
        HRESULT __stdcall QueryFocus(HWND* window) noexcept override;
        HRESULT __stdcall TranslateAccelerator(MSG* message) noexcept override;

        // IObjectWithSite
        HRESULT __stdcall SetSite(IUnknown* site) noexcept override;
        HRESULT __stdcall GetSite(REFIID riid, void** site) noexcept override;

        // IOleWindow
        HRESULT __stdcall GetWindow(HWND* window) noexcept override;
        HRESULT __stdcall ContextSensitiveHelp(BOOL enterMode) noexcept override;

        // IPreviewHandlerVisuals
        HRESULT __stdcall SetBackgroundColor(COLORREF color) noexcept override;
        HRESULT __stdcall SetTextColor(COLORREF color) noexcept override;
        HRESULT __stdcall SetFont(const LOGFONTW* font) noexcept override;

    private:
        HRESULT LoadFromBytes(std::vector<uint8_t>&& bytes) noexcept;
        void PushVisuals() noexcept;
        RECT EffectiveBounds() const;

        std::mutex m_lock;
        // One island per handler instance: prevhost.exe is shared by every
        // preview pane, so several of these can be alive at the same time.
        Island m_island;

        HWND m_hostWindow{ nullptr };
        IUnknown* m_site{ nullptr };
        RECT m_bounds{};
        bool m_hasBounds{ false };
        bool m_loaded{ false };
        bool m_previewing{ false };

        std::vector<uint8_t> m_bytes;
        std::wstring m_displayName;

        bool m_useHostColors{ false };
        uint32_t m_background{ 0 };
        uint32_t m_text{ 0 };
        std::wstring m_fontFamily;
        float m_fontSize{ 0.0f };
    };
}
