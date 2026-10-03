#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>

namespace PlaylistPreview::Shell
{
    // Creates the single WinUI Application for this process.
    //
    // A bare Microsoft.UI.Xaml.Application is not enough for islands: the XAML
    // framework QIs the Application for IXamlMetadataProvider, and without a
    // provider that can resolve the WinUI control metadata it cannot resolve
    // control resources either.  XamlControlsResources then fails with
    // "Cannot find a resource with the given key:
    // AcrylicBackgroundFillColorDefaultBrush" and every templated control
    // renders as nothing.  The generated AppT in a WinUI app template wires
    // this up, so we mirror it here.
    winrt::Microsoft::UI::Xaml::Application CreatePreviewApplication();
}
