#pragma once

#include <winrt/Microsoft.UI.Xaml.h>

#include <cstdint>
#include <string>

#include "..\Shared\PreviewInterop.h"

namespace PlaylistPreviewUI
{
    // Builds the preview surface: header + a scrollable text view.
    //
    // The visual tree is authored in C++/WinRT rather than as compiled XAML
    // markup on purpose; see docs/registration.md for the resources.pri
    // constraint that drives this decision.
    winrt::Microsoft::UI::Xaml::UIElement CreatePreviewRoot();

    // All of these address a root by the pointer CreatePreviewRoot handed out.
    // Several roots can be alive at once (one per preview pane), so there is no
    // process-wide "current" root.
    void SetText(IInspectable* root, std::wstring const& displayName, std::string const& utf8Text);

    // Applies theme / host supplied colours and font to the live root.
    void SetVisuals(
        IInspectable* root,
        int32_t theme,
        bool useHostColors,
        uint32_t backgroundRef,
        uint32_t textRef,
        std::wstring const& fontFamily,
        float fontSize);

    // Releases the state kept for that root.
    void ReleasePreviewRoot(IInspectable* root) noexcept;
}
