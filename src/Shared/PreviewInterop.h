#pragma once
//
// Interop contract shared by:
//   * src/PreviewHandlerShell  (consumer of PlaylistPreviewUI.dll, COM preview handler)
//   * src/PlaylistPreviewUI    (WinUI 3 content library, producer)
//   * tools/IslandTestHost     (dev-only harness that drives the shell)
//
// The boundary is deliberately a small plain-C ABI instead of WinRT activation:
// a preview handler DLL is loaded by a host process we do not own (prevhost.exe)
// and cannot modify, so we must not depend on registry/manifest based WinRT
// activation or on ms-appx:/// resource resolution for our own components.
//

#include <windows.h>
#include <unknwn.h>
#include <inspectable.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// PlaylistPreviewUI.dll
// ---------------------------------------------------------------------------

// Creates the WinUI 3 content root (a Microsoft.UI.Xaml.UIElement) for the
// preview surface.  Returns the element as IInspectable with a +1 reference.
//
// Preconditions, all of which must already be satisfied on the calling thread:
//   * Windows App SDK runtime bootstrapped
//   * thread is STA and has a Microsoft.UI.Dispatching.DispatcherQueue
//   * Microsoft.UI.Xaml.Application exists (implementing IXamlMetadataProvider)
//   * WindowsXamlManager.InitializeForCurrentThread() has been called
//   * XamlControlsResources merged into Application.Resources
using PFN_PPUI_CreatePreviewRoot = HRESULT(__stdcall*)(IInspectable** content);

// Hands the file to the content library.
//   displayName : file name shown in the header (may be null/empty)
//   utf8Text    : the file contents, already transcoded to UTF-8
//   byteLength  : number of bytes in utf8Text
using PFN_PPUI_SetText = HRESULT(__stdcall*)(
    IInspectable* root,
    const wchar_t* displayName,
    const char* utf8Text,
    uint32_t byteLength);

// Applies colours and font.
//   theme          : kTheme_* (used when useHostColors == 0)
//   useHostColors  : 1 = use backgroundRef/textRef from the preview host
//   backgroundRef  : COLORREF (0x00BBGGRR) from IPreviewHandlerVisuals
//   textRef        : COLORREF (0x00BBGGRR) from IPreviewHandlerVisuals
//   fontFamily     : LOGFONT face name, may be null/empty
//   fontSize       : point size, <= 0 means "content library default"
using PFN_PPUI_SetVisuals = HRESULT(__stdcall*)(
    IInspectable* root,
    int32_t theme,
    int32_t useHostColors,
    uint32_t backgroundRef,
    uint32_t textRef,
    const wchar_t* fontFamily,
    float fontSize);

// Drops any state the library kept for that root.  Must be called by the shell
// when the island is torn down so the WinUI object graph can be released.
using PFN_PPUI_ReleasePreviewRoot = void(__stdcall*)(IInspectable* root);

inline constexpr char kPPUI_Export_CreatePreviewRoot[] = "PlaylistPreviewUI_CreatePreviewRoot";
inline constexpr char kPPUI_Export_SetText[] = "PlaylistPreviewUI_SetText";
inline constexpr char kPPUI_Export_SetVisuals[] = "PlaylistPreviewUI_SetVisuals";
inline constexpr char kPPUI_Export_ReleasePreviewRoot[] = "PlaylistPreviewUI_ReleasePreviewRoot";
inline constexpr wchar_t kPPUI_DllName[] = L"PlaylistPreviewUI.dll";

// ---------------------------------------------------------------------------
// PreviewHandlerShell.dll
// ---------------------------------------------------------------------------
// Phase 2: the shell is a plain in-process COM server, so the only exports are
// the standard ones.  Development harnesses load the DLL, grab DllGetClassObject
// and drive the real IPreviewHandler interfaces (no side doors).
inline constexpr wchar_t kShell_DllName[] = L"PreviewHandlerShell.dll";
inline constexpr char kShell_Export_DllGetClassObject[] = "DllGetClassObject";

// The preview handler's CLSID.  Single definition shared by the COM server, the
// registration code and development harnesses.
// {8F3A2C64-6D2E-4E7B-9C5A-1B2C3D4E5F60}
inline constexpr GUID kShell_HandlerClsid{
    0x8f3a2c64, 0x6d2e, 0x4e7b, { 0x9c, 0x5a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60 } };

// ---------------------------------------------------------------------------
// Themes
// ---------------------------------------------------------------------------
inline constexpr int32_t kTheme_Default = 0;
inline constexpr int32_t kTheme_Light = 1;
inline constexpr int32_t kTheme_Dark = 2;
