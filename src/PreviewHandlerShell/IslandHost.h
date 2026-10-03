#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace PlaylistPreview::Shell
{
    // One preview surface.
    //
    // Every preview handler instance owns one of these: Explorer reuses a single
    // prevhost.exe process for all preview panes, so two panes showing two
    // playlists at the same time give us two handlers and therefore two islands.
    // The process-wide pieces (App SDK bootstrap, DispatcherQueue, Application,
    // WindowsXamlManager, message hook) stay shared and live on the XAML thread.
    class Island
    {
    public:
        Island();
        ~Island();

        Island(Island const&) = delete;
        Island& operator=(Island const&) = delete;

        // Creates the island as a child of `parent`, covering `bounds` (parent
        // client coordinates).  If the host window is not visible yet -- Explorer
        // calls DoPreview before showing a reopened preview pane -- the creation
        // is deferred and completed by ProcessDeferredIslandCreation().
        HRESULT Create(HWND parent, const RECT& bounds);

        HRESULT Resize(const RECT& bounds);

        // Hand the (already UTF-8 transcoded) file contents to the content library.
        HRESULT SetText(std::wstring const& displayName, std::string const& utf8Text);

        // Colours / font, normally originating from IPreviewHandlerVisuals.
        HRESULT SetVisuals(
            int32_t theme,
            bool useHostColors,
            uint32_t backgroundRef,
            uint32_t textRef,
            std::wstring const& fontFamily,
            float fontSize);

        // Moves keyboard focus into the XAML content (IPreviewHandler::SetFocus).
        HRESULT Focus() noexcept;

        // Window the island renders into, or nullptr when there is no island.
        HWND Window() const noexcept;

        bool Active() const noexcept;

        void Destroy() noexcept;

    private:
        struct Impl;
        std::shared_ptr<Impl> m_impl;
    };

    // Only used by IPreviewHandler::TranslateAccelerator; the normal path is the
    // WH_GETMESSAGE hook installed while an island exists.
    bool IslandPreTranslateMessage(const MSG& message);

    // Runs on the XAML thread; completes island creations that were deferred
    // because their host window was still hidden.
    void ProcessDeferredIslandCreation();

    // Runs on the XAML thread; hands every live island its visuals again.  The
    // content library decides whether anything actually changed, which is how a
    // Windows light/dark switch reaches a preview pane that is already open.
    void RefreshIslandVisuals();

    // Runs on the XAML thread; releases every island before the XAML environment
    // is torn down.
    void DestroyAllIslands() noexcept;
}
