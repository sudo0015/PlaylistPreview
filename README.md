# PlaylistPreview

A Windows Explorer preview handler for playlist files — rendered with WinUI 3 and matching the light/dark theme.

## Features

- **Preview playlists in Explorer** — `.cue` and `.m3u8` files, no extra app to open.
- **Two views, one switch** — toggle from the toolbar at any time:
  - **Playlist view** parses the file and lays out a structured track list: index,
    title, duration and artist.
  - **Text view** shows the raw file with syntax highlighting for CUE and M3U8
    keywords, timestamps, quoted strings and URIs.
- **Remembers your last view** — reopen a file and it lands in the mode you used before.
- **Follows Windows** — light and dark themes adapt automatically, and open preview
  panes update within about a second.
- **Graceful fallback** — files with no tracks, or that are not playlists at all,
  drop straight into text view.

## Supported formats

- **CUE (*.cue)**
- **M3U8 (*.m3u8)**

## Requirements

- Windows 10 version 1809 (build 17763) or later; Windows 11 recommended.
- [Windows App Runtime 2.5.1 or later](https://aka.ms/windowsappsdk/2.5/latest/windowsappruntimeinstall-x64.exe).

## Install

Download the installer for your machine from the
[Releases page](https://github.com/sudo0015/PlaylistPreview/releases):

> [!IMPORTANT]
> The installers are not code-signed yet, so SmartScreen may show its "Windows protected
your PC" warning — choose **More info → Run anyway**.

## Usage

1. Install and let Explorer restart.
2. Select a `.cue` or `.m3u8` file and press `Alt+P` to open the preview pane.
3. Use the toolbar to switch between Playlist and Text view.

## Language

The preview UI follows the Windows display language. Supported languages:

- English
- Simplified Chinese

> [!NOTE]
> Want more languages? Feel free to [open an issue](https://github.com/sudo0015/PlaylistPreview/issues) and ask.

## License

[MIT License](LICENSE)

## Credits

### Built with

- **[Windows App SDK](https://github.com/microsoft/WindowsAppSDK) 2.5.1** — WinUI 3 controls, XAML Islands hosting (`DesktopWindowXamlSource`) and the Windows App Runtime.
- **[C++/WinRT](https://github.com/microsoft/cppwinrt)** — the WinRT projection behind both `PreviewHandlerShell.dll` and `PlaylistPreviewUI.dll`.
- **Windows SDK** — Win32/COM headers, libraries and the XAML compiler.
- **Windows Shell preview-handler interfaces** — `IPreviewHandler`, `IInitializeWithStream`, `IObjectWithSite` and `IPreviewHandlerVisuals`, hosted in `prevhost.exe`.

### Inspired by

- Microsoft's **`CppShellExtPreviewHandler`** sample in [Windows-classic-samples](https://github.com/microsoft/Windows-classic-samples) — the preview-handler skeleton this project grew out of.
- **[Microsoft PowerToys](https://github.com/microsoft/PowerToys)** (MIT) — the window-embedding recipe from `FormHandlerControl.UpdateWindowBounds`: create the window on the STA thread, switch to `WS_CHILD` before calling `SetParent`, and fall back to `GetClientRect` when the host passes an empty rectangle.
- **[Windows App SDK samples](https://github.com/microsoft/WindowsAppSDK-Samples)** — XAML Islands and app SDK bootstrap references.

### Format references

- CUE sheet keywords (`TITLE`, `PERFORMER`, `REM`, `FILE`, `TRACK`, `INDEX`) as used by CDRWIN and Exact Audio Copy.
- Extended M3U / M3U8 (`#EXTM3U`, `#EXTINF`, `#EXTALB`, `#EXTART`, `#EXTGRP`) as used by Winamp-compatible players.

### Third-party components

NuGet dependencies are listed in each project's `packages.config`: the Windows App SDK, C++/WinRT and Windows SDK Build Tools this project builds on, plus packages the WinUI template's import graph pulls in. All remain under their own licenses — MIT for the Microsoft packages.

Thanks to the authors of those samples and tools; getting WinUI 3 islands to run inside `prevhost.exe` would have been far harder without them.
