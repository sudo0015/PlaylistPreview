# PlaylistPreview

A Windows Explorer preview handler for playlist files. Select a `.cue` or `.m3u8`
file in Explorer, press `Alt+P`, and the preview pane shows its contents — rendered
with WinUI 3 and matching the Windows 11 look and light/dark theme.

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

| Format | Parsed from | Shown per track |
|---|---|---|
| CUE (`.cue`) | `TITLE`, `PERFORMER`, `REM GENRE/DATE`, `FILE`, `TRACK`, `INDEX 01` | Index, title, artist, duration |
| M3U8 (`.m3u8`) | `#EXTM3U`, `#EXTINF`, `#PLAYLIST`, `#EXTALB`, `#EXTART`, `#EXTGRP` | Index, title, duration, path/URL |

CUE track durations are derived from consecutive `INDEX 01` entries. M3U8 entries
without `#EXTINF` fall back to the file name and a running start time.

## Requirements

- Windows 10 version 1809 (build 17763) or later; Windows 11 recommended.
- [Windows App Runtime 2.5.1 or later](https://aka.ms/windowsappsdk/2.5/latest/windowsappruntimeinstall-x64.exe).
  The handler is an in-process COM DLL, so the runtime cannot be bundled with it.

## Install

Registration is machine-wide and requires administrator rights; the script asks for
them automatically.

**Install**
```powershell
scripts\install.cmd
```

**Uninstall**
```powershell
scripts\uninstall.cmd
```

Add `-DryRun` to preview what the installer will do, or `-RestartExplorer` to restart
Explorer after installing. Run `scripts\install.ps1 -?` for all options.

## Usage

1. Install and let Explorer restart.
2. Select a `.cue` or `.m3u8` file and press `Alt+P` to open the preview pane.
3. Use the toolbar to switch between Playlist and Text view.

## License

[MIT License](LICENSE)

## Credits

### Built with

- **[Windows App SDK](https://github.com/microsoft/WindowsAppSDK) 2.5.1** — WinUI 3 controls,
  XAML Islands hosting (`DesktopWindowXamlSource`) and the Windows App Runtime.
- **[C++/WinRT](https://github.com/microsoft/cppwinrt)** — the WinRT projection behind both
  `PreviewHandlerShell.dll` and `PlaylistPreviewUI.dll`.
- **Windows SDK** — Win32/COM headers, libraries and the XAML compiler.
- **Windows Shell preview-handler interfaces** — `IPreviewHandler`, `IInitializeWithStream`, `IObjectWithSite` and `IPreviewHandlerVisuals`, hosted in `prevhost.exe`.

### Inspired by

- Microsoft's **`CppShellExtPreviewHandler`** sample in
  [Windows-classic-samples](https://github.com/microsoft/Windows-classic-samples) — the preview-handler skeleton this project grew out of.
- **[Microsoft PowerToys](https://github.com/microsoft/PowerToys)** (MIT) — the window-embedding recipe from `FormHandlerControl.UpdateWindowBounds`: create the window on the STA thread, switch to `WS_CHILD` before calling `SetParent`, and fall back to `GetClientRect` when the host passes an empty rectangle.
- **[Windows App SDK samples](https://github.com/microsoft/WindowsAppSDK-Samples)** — XAML Islands and app SDK bootstrap references.

### Format references

- CUE sheet keywords (`TITLE`, `PERFORMER`, `REM`, `FILE`, `TRACK`, `INDEX`) as used by CDRWIN and Exact Audio Copy.
- Extended M3U / M3U8 (`#EXTM3U`, `#EXTINF`, `#EXTALB`, `#EXTART`, `#EXTGRP`) as used by Winamp-compatible players.

### Third-party components

NuGet dependencies are listed in each project's `packages.config`: the Windows App SDK, C++/WinRT and Windows SDK Build Tools this project builds on, plus packages the WinUI template's import graph pulls in. All remain under their own licenses — MIT for the Microsoft packages.

Thanks to the authors of those samples and tools; getting WinUI 3 islands to run inside `prevhost.exe` would have been far harder without them.
