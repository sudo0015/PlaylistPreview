#include "pch.h"
#include "PreviewRoot.h"
#include "PlaylistParser.h"
#include "TextHighlighter.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Documents;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace PlaylistPreviewUI;

namespace
{
    // Phase 3.6 turns these into a real policy; for now they keep a pathological
    // file from freezing the preview pane.
    constexpr size_t kMaxRenderedChars = 200000;
    constexpr size_t kMaxHighlightedLines = 2000;
    constexpr size_t kMaxRenderedTracks = 2000;

    // Body size for the text view.  The host's font size is treated as a lower
    // bound rather than a mandate: Explorer hands out its *UI* size (9 pt), which
    // is unreadable for a text/code view.
    constexpr float kBodyFontSize = 14.0f;

    // Mode switch items are identified by name.  Comparing interface pointers
    // does not work here: NavigationView hands back the IInspectable identity
    // pointer, while winrt::get_abi() on the projected NavigationViewItem gives
    // its default interface pointer, and those two are never equal.
    constexpr wchar_t kPlaylistItemName[] = L"PlaylistPreview.Mode.Playlist";
    constexpr wchar_t kTextItemName[] = L"PlaylistPreview.Mode.Text";

    // Last mode the user picked, remembered per user.  A preview handler runs
    // inside prevhost.exe as the signed-in account, so HKCU is both writable
    // and correct -- a file next to the DLL would need write access to
    // Program Files.
    constexpr wchar_t kSettingsKeyPath[] = L"Software\\PlaylistPreview";
    constexpr wchar_t kPreviewModeValueName[] = L"PreviewMode";
    constexpr wchar_t kPreviewModePlaylist[] = L"playlist";
    constexpr wchar_t kPreviewModeText[] = L"text";

    bool LoadPreferredPlaylistMode()
    {
        wchar_t buffer[32]{};
        DWORD size = sizeof(buffer);
        const LSTATUS status = ::RegGetValueW(
            HKEY_CURRENT_USER,
            kSettingsKeyPath,
            kPreviewModeValueName,
            RRF_RT_REG_SZ,
            nullptr,
            buffer,
            &size);

        if (status != ERROR_SUCCESS)
        {
            return true;   // nothing stored yet: playlist view is the default
        }

        return ::CompareStringOrdinal(buffer, -1, kPreviewModeText, -1, TRUE) != CSTR_EQUAL;
    }

    void SavePreferredPlaylistMode(bool playlist) noexcept
    {
        HKEY key = nullptr;
        if (::RegCreateKeyExW(
                HKEY_CURRENT_USER,
                kSettingsKeyPath,
                0,
                nullptr,
                0,
                KEY_SET_VALUE,
                nullptr,
                &key,
                nullptr) != ERROR_SUCCESS)
        {
            return;
        }

        const wchar_t* const value = playlist ? kPreviewModePlaylist : kPreviewModeText;
        ::RegSetValueExW(
            key,
            kPreviewModeValueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(value),
            static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));

        ::RegCloseKey(key);
    }

    Windows::UI::Color Rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return Microsoft::UI::ColorHelper::FromArgb(255, r, g, b);
    }

    // COLORREF is 0x00BBGGRR.
    Windows::UI::Color FromColorRef(uint32_t value)
    {
        return Rgb(static_cast<uint8_t>(value & 0xFF),
                   static_cast<uint8_t>((value >> 8) & 0xFF),
                   static_cast<uint8_t>((value >> 16) & 0xFF));
    }

    struct Palette
    {
        Windows::UI::Color Background;
        Windows::UI::Color Card;
        Windows::UI::Color Separator;
        Windows::UI::Color PrimaryText;
        Windows::UI::Color SecondaryText;
        Windows::UI::Color Accent;
        Windows::UI::Color Body;
        Windows::UI::Color Keyword;
        Windows::UI::Color Comment;
        Windows::UI::Color String;
        Windows::UI::Color Number;
        Windows::UI::Color Directive;
        Windows::UI::Color Path;
    };

    bool HostPrefersDark(Windows::UI::Color background, Windows::UI::Color text)
    {
        return (static_cast<int>(text.R) + text.G + text.B) >
               (static_cast<int>(background.R) + background.G + background.B);
    }

    // Windows' own app-mode switch: 0 = dark, 1 = light.  The value is missing
    // until the user touches the setting, which is light mode.
    bool SystemPrefersDark() noexcept
    {
        DWORD light = 1;
        DWORD size = sizeof(light);
        const LSTATUS status = ::RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",
            RRF_RT_REG_DWORD,
            nullptr,
            &light,
            &size);

        return (status == ERROR_SUCCESS) && (light == 0);
    }

    // The one decision both the palette and the element theme come from.
    //
    // kTheme_Default (what the shell always sends) means "follow the Windows
    // app mode".  Handing WinUI one theme while painting the other is what made
    // the menu render white-on-light earlier, so both are derived here and
    // nowhere else.
    bool EffectiveDark(int32_t theme, bool systemDark)
    {
        if (theme == kTheme_Dark)
        {
            return true;
        }
        if (theme == kTheme_Light)
        {
            return false;
        }

        return systemDark;
    }

    // ...and whether the host's own colours may tint that surface.
    //
    // Explorer hands preview handlers the classic system colours
    // (COLOR_WINDOW = white, COLOR_WINDOWTEXT = black) even when Windows is in
    // dark mode, so honouring them unconditionally is what made the pane white
    // on a dark desktop.  They are used only when they agree with the decision
    // above; a host that really is dark-backed (or light-backed) still gets its
    // own background and text colours.
    bool UseHostPalette(bool useHostColors, uint32_t hostBackground, uint32_t hostText, bool dark)
    {
        return useHostColors &&
               (HostPrefersDark(FromColorRef(hostBackground), FromColorRef(hostText)) == dark);
    }

    ElementTheme RequestedThemeFor(bool dark)
    {
        return dark ? ElementTheme::Dark : ElementTheme::Light;
    }

    // Depth comes from painting two layers: the page, and a card one step away
    // from it (Windows 11 shows elevation with a colour step, not a shadow).
    //
    // The host hands us one colour.  In dark mode the card is the lighter layer
    // (#202020 -> #2C2C2C).  In light mode the card is lighter too -- unless the
    // page has no headroom left, which is exactly the case Explorer puts us in
    // with its white COLOR_WINDOW: then the step goes the other way, the page
    // drops and the card keeps the colour.  Both branches land on the same
    // #F3F3F3 page / #FFFFFF card pairing our own light palette uses.
    void SplitPageAndCard(Windows::UI::Color given, bool dark, Windows::UI::Color& page, Windows::UI::Color& card)
    {
        constexpr int kLayerStep = 12;
        const auto shift = [](uint8_t channel, int delta) {
            const int value = static_cast<int>(channel) + delta;
            return static_cast<uint8_t>((std::max)(0, (std::min)(255, value)));
        };

        const int headroom = 255 - (std::max)({ given.R, given.G, given.B });
        if (dark || (headroom >= kLayerStep))
        {
            page = given;
            card = Rgb(shift(given.R, kLayerStep), shift(given.G, kLayerStep), shift(given.B, kLayerStep));
            return;
        }

        page = Rgb(shift(given.R, -kLayerStep), shift(given.G, -kLayerStep), shift(given.B, -kLayerStep));
        card = given;
    }

    Palette PaletteFor(bool dark, bool hostPalette, uint32_t hostBackground, uint32_t hostText)
    {
        if (hostPalette)
        {
            const Windows::UI::Color text = FromColorRef(hostText);
            Windows::UI::Color page{};
            Windows::UI::Color card{};
            SplitPageAndCard(FromColorRef(hostBackground), dark, page, card);

            return dark
                ? Palette{ page, card, Rgb(70, 70, 70), text, text, Rgb(96, 205, 255), text,
                           Rgb(86, 156, 214), Rgb(87, 166, 74), Rgb(206, 145, 120), Rgb(181, 206, 168),
                           Rgb(197, 134, 192), Rgb(86, 156, 214) }
                : Palette{ page, card, Rgb(215, 215, 215), text, text, Rgb(0, 95, 184), text,
                           Rgb(0, 90, 158), Rgb(0, 128, 0), Rgb(163, 21, 21), Rgb(9, 134, 88),
                           Rgb(136, 0, 180), Rgb(0, 102, 204) };
        }

        if (dark)
        {
            return Palette{ Rgb(32, 32, 32), Rgb(45, 45, 45), Rgb(64, 64, 64), Rgb(255, 255, 255),
                            Rgb(175, 175, 175), Rgb(96, 205, 255), Rgb(230, 230, 230),
                            Rgb(86, 156, 214), Rgb(87, 166, 74), Rgb(206, 145, 120), Rgb(181, 206, 168),
                            Rgb(197, 134, 192), Rgb(86, 156, 214) };
        }

        return Palette{ Rgb(243, 243, 243), Rgb(255, 255, 255), Rgb(226, 226, 226), Rgb(28, 28, 28),
                        Rgb(95, 95, 95), Rgb(0, 95, 184), Rgb(32, 32, 32),
                        Rgb(0, 90, 158), Rgb(0, 128, 0), Rgb(163, 21, 21), Rgb(9, 134, 88),
                        Rgb(136, 0, 180), Rgb(0, 102, 204) };
    }

    // Phase 3 keeps exactly one live root per process, matching the shell's
    // single-island design.  Phase 5.3 (concurrent instances) revisits this.
    struct LiveRoot
    {
        Grid Root{ nullptr };
        NavigationView ModeNav{ nullptr };
        NavigationViewItem PlaylistItem{ nullptr };
        NavigationViewItem TextItem{ nullptr };
        TextBlock PlaylistLabel{ nullptr };
        TextBlock TextLabel{ nullptr };
        Border Card{ nullptr };
        Border StripCover{ nullptr };
        Grid AlbumInfo{ nullptr };
        TextBlock AlbumTitle{ nullptr };
        TextBlock AlbumMeta{ nullptr };
        ScrollViewer TextScroller{ nullptr };
        ScrollViewer ListScroller{ nullptr };
        TextBlock TextBody{ nullptr };
        StackPanel ListPanel{ nullptr };

        int32_t Theme{ kTheme_Default };
        bool UseHostColors{ false };
        uint32_t HostBackground{ 0 };
        uint32_t HostText{ 0 };
        std::wstring FontFamily;
        float FontSize{ 0.0f };
        bool SystemDark{ false };
        bool VisualsApplied{ false };

        std::wstring DisplayName;
        std::wstring Text;
        size_t ByteCount{ 0 };
        bool Binary{ false };
        PlaylistDocument Document;
        bool ShowPlaylist{ false };
        bool PreferPlaylist{ true };
    };

    // Phase 5: one state per content root.  Explorer reuses a single prevhost
    // process for every preview pane, so two panes previewing two playlists at
    // the same time give us two handlers and therefore two roots -- they must
    // not share state.  The key is the root element pointer the shell passes
    // back to us (it is exactly the pointer CreatePreviewRoot hands out).
    std::mutex g_rootsLock;
    std::map<IInspectable*, std::shared_ptr<LiveRoot>> g_roots;

    std::shared_ptr<LiveRoot> FindRoot(IInspectable* key)
    {
        if (key == nullptr)
        {
            return nullptr;
        }

        std::lock_guard guard{ g_rootsLock };
        const auto found = g_roots.find(key);
        return (found == g_roots.end()) ? nullptr : found->second;
    }

    std::wstring FormatCount(size_t value)
    {
        return std::to_wstring(value);
    }

    // UI font stack used when the preview host does not hand one down: Segoe UI
    // is the primary face, and the Microsoft YaHei families follow it as the
    // fallback for CJK titles and performers (Segoe UI carries no CJK glyphs).
    // The plain "Microsoft YaHei" entry covers systems where only that variant
    // is installed.
    constexpr wchar_t kDefaultUiFontStack[] =
        L"Segoe UI, Microsoft YaHei UI, Microsoft YaHei";

    // Monospace first, UI font second: playlist numbers and the source view line
    // up in Consolas, while anything Consolas cannot draw (CJK titles, performers,
    // symbols) falls through to the UI stack instead of turning into boxes.
    std::wstring MonospaceFontFamily(std::wstring const& fallback)
    {
        return std::wstring{ L"Consolas, " } +
               (fallback.empty() ? std::wstring{ kDefaultUiFontStack } : fallback);
    }

    // A playlist file is plain text.  If the bytes decode to something else
    // (an audio file that was renamed, an archive, ...) the preview should say so
    // rather than parse garbage or render pages of mojibake.
    bool LooksBinary(std::wstring const& text)
    {
        const size_t sample = (std::min)(text.size(), static_cast<size_t>(4096));
        if (sample == 0)
        {
            return false;
        }

        size_t suspicious = 0;
        for (size_t index = 0; index < sample; ++index)
        {
            const wchar_t c = text[index];
            if ((c == L'\r') || (c == L'\n') || (c == L'\t'))
            {
                continue;
            }
            if ((c < 0x20) || (c == 0xFFFD))
            {
                ++suspicious;
            }
        }

        return (suspicious * 20) > sample;   // more than ~5% junk
    }

    void Append(LiveRoot& root, SyntaxKind kind, std::wstring_view text, Palette const& palette)
    {
        if (text.empty())
        {
            return;
        }

        Run run;
        run.Text(hstring{ text });

        switch (kind)
        {
        case SyntaxKind::Keyword: run.Foreground(SolidColorBrush{ palette.Keyword }); break;
        case SyntaxKind::Comment: run.Foreground(SolidColorBrush{ palette.Comment }); break;
        case SyntaxKind::String: run.Foreground(SolidColorBrush{ palette.String }); break;
        case SyntaxKind::Number: run.Foreground(SolidColorBrush{ palette.Number }); break;
        case SyntaxKind::Directive: run.Foreground(SolidColorBrush{ palette.Directive }); break;
        case SyntaxKind::Path: run.Foreground(SolidColorBrush{ palette.Path }); break;
        default: run.Foreground(SolidColorBrush{ palette.Body }); break;
        }

        root.TextBody.Inlines().Append(run);
    }

    void BuildTextView(LiveRoot& root, Palette const& palette)
    {
        root.TextBody.Inlines().Clear();
        root.TextBody.FontFamily(FontFamily{ MonospaceFontFamily(root.FontFamily) });
        root.TextBody.FontSize((std::max)(root.FontSize, kBodyFontSize));
        root.TextBody.Foreground(SolidColorBrush{ palette.Body });

        if (root.Text.empty())
        {
            return;
        }

        // The header no longer carries the "non-text" hint, so say it here
        // instead of silently rendering mojibake.
        if (root.Binary)
        {
            Run warning;
            warning.Text(hstring{ L"This does not look like text. Showing the raw content." });
            warning.Foreground(SolidColorBrush{ palette.SecondaryText });
            root.TextBody.Inlines().Append(warning);
            root.TextBody.Inlines().Append(LineBreak{});
            root.TextBody.Inlines().Append(LineBreak{});
        }

        // Rendering budget: past this the remainder is shown as plain text so a
        // huge file cannot freeze the preview pane.
        const std::wstring_view view{ root.Text.data(),
                                      (std::min)(root.Text.size(), kMaxRenderedChars) };
        const bool truncated = root.Text.size() > view.size();
        size_t offset = 0;
        size_t lineIndex = 0;
        std::vector<PlaylistPreviewUI::SyntaxSpan> spans;

        while (offset <= view.size())
        {
            const size_t end = view.find_first_of(L"\r\n", offset);
            const std::wstring_view line =
                (end == std::wstring_view::npos) ? view.substr(offset) : view.substr(offset, end - offset);

            if (lineIndex >= kMaxHighlightedLines)
            {
                // Past the highlighting budget: show the remainder as one block.
                Run tail;
                tail.Text(hstring{ view.substr(offset) });
                tail.Foreground(SolidColorBrush{ palette.Body });
                root.TextBody.Inlines().Append(tail);
                break;
            }

            PlaylistPreviewUI::HighlightLine(root.Document.Kind, line, spans);
            for (auto const& span : spans)
            {
                Append(root, span.Kind, span.Text, palette);
            }

            root.TextBody.Inlines().Append(LineBreak{});
            ++lineIndex;

            if (end == std::wstring_view::npos)
            {
                break;
            }

            // Skip CRLF as a pair.
            offset = end + 1;
            if ((view[end] == L'\r') && (offset < view.size()) && (view[offset] == L'\n'))
            {
                ++offset;
            }
        }

        if (truncated)
        {
            Run note;
            note.Text(hstring{ L"… (content truncated; showing the first part only)" });
            note.Foreground(SolidColorBrush{ palette.SecondaryText });
            root.TextBody.Inlines().Append(note);
        }
    }

    // Track durations are always mm:ss, zero padded (02:34).  A playlist can run
    // past an hour, and then the minutes simply keep counting (65:30) rather than
    // growing a third field -- one shape for every row.
    std::wstring FormatTrackDuration(double seconds)
    {
        if (seconds < 0.0)
        {
            return {};
        }

        const int total = static_cast<int>(seconds + 0.5);
        wchar_t buffer[16]{};
        swprintf_s(buffer, L"%02d:%02d", total / 60, total % 60);
        return buffer;
    }

    // Second line, right part: the track's own performer, otherwise the album
    // artist (CUE's top-level PERFORMER / M3U8's #EXTART).  Nothing else is
    // invented -- a track with neither shows just its duration.
    std::wstring TrackPerformer(PlaylistDocument const& document, PlaylistTrack const& track)
    {
        if (!track.Performer.empty())
        {
            return track.Performer;
        }

        return document.Performer;
    }

    // A row trims long text, so hang the untrimmed value off the element that
    // does the trimming.
    void SetOverflowToolTip(FrameworkElement const& element, std::wstring const& text)
    {
        if (!text.empty())
        {
            ToolTipService::SetToolTip(element, box_value(hstring{ text }));
        }
    }

    // The top pane lays its menu out left-aligned: the template puts a zero-width
    // spacer before the items and all the slack into a star column after them.
    // Giving that spacer the same star width splits the leftover space evenly, so
    // the items end up centred.  Both widths are literals in the template (no
    // theme resource involved), so the change survives theme switches.
    void CentreNavigationViewItems(DependencyObject const& element)
    {
        std::vector<DependencyObject> pending{ element };
        while (!pending.empty())
        {
            const DependencyObject current = pending.back();
            pending.pop_back();

            if (auto grid = current.try_as<Grid>())
            {
                if (grid.Name() == L"TopNavGrid")
                {
                    auto columns = grid.ColumnDefinitions();
                    // 1: TopNavLeftPadding (the spacer), 5: the trailing star.
                    if (columns.Size() > 5)
                    {
                        columns.GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
                    }
                    return;
                }
            }

            const int32_t childCount = VisualTreeHelper::GetChildrenCount(current);
            for (int32_t index = 0; index < childCount; ++index)
            {
                pending.push_back(VisualTreeHelper::GetChild(current, index));
            }
        }
    }

    // Album strip, line 2: album artist and track count, dot separated.
    std::wstring AlbumMetaText(PlaylistDocument const& document)
    {
        const size_t count = document.Tracks.size();
        std::wstring meta = FormatCount(count) + ((count == 1) ? L" Track" : L" Tracks");
        if (!document.Performer.empty())
        {
            meta = document.Performer + L"  ·  " + meta;
        }
        return meta;
    }

    // Fills the album strip when a file is loaded.
    void ApplyAlbumInfo(LiveRoot& root)
    {
        const PlaylistDocument& document = root.Document;

        const std::wstring title = !document.Album.empty()
            ? document.Album
            : document.Title;
        root.AlbumTitle.Text(hstring{ title });
        root.AlbumTitle.Visibility(title.empty() ? Visibility::Collapsed : Visibility::Visible);
        SetOverflowToolTip(root.AlbumTitle, title);

        const std::wstring meta = AlbumMetaText(document);
        root.AlbumMeta.Text(hstring{ meta });
        SetOverflowToolTip(root.AlbumMeta, meta);
    }

    UIElement BuildTrackRow(LiveRoot const& root, PlaylistTrack const& track, size_t index, Palette const& palette)
    {
        Grid row;
        row.Padding(ThicknessHelper::FromLengths(4, 7, 4, 7));
        row.ColumnDefinitions().Append(ColumnDefinition{});
        row.ColumnDefinitions().Append(ColumnDefinition{});
        row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::Auto());
        row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));

        wchar_t numberText[16]{};
        swprintf_s(numberText, L"%02d", track.Number > 0 ? track.Number : static_cast<int>(index) + 1);

        // Left column: just the number, centred against the whole two-line block.
        TextBlock number;
        number.Text(numberText);
        number.FontSize(15);
        number.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        number.FontFamily(FontFamily{ MonospaceFontFamily(root.FontFamily) });
        number.MinWidth(34);
        number.TextAlignment(TextAlignment::Right);
        number.VerticalAlignment(VerticalAlignment::Center);
        number.Foreground(SolidColorBrush{ palette.SecondaryText });
        number.Margin(ThicknessHelper::FromLengths(0, 0, 12, 0));
        Grid::SetColumn(number, 0);

        StackPanel details;
        details.Spacing(2);
        details.VerticalAlignment(VerticalAlignment::Center);

        const std::wstring titleText =
            track.Title.empty() ? std::wstring{ L"(Untitled)" } : track.Title;

        TextBlock title;
        title.Text(hstring{ titleText });
        title.FontSize(15);
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title.TextTrimming(TextTrimming::CharacterEllipsis);
        title.Foreground(SolidColorBrush{ palette.PrimaryText });
        SetOverflowToolTip(title, titleText);
        details.Children().Append(title);

        const std::wstring duration = FormatTrackDuration(track.DurationSeconds);
        const std::wstring performer = TrackPerformer(root.Document, track);
        if (!duration.empty() || !performer.empty())
        {
            // Second line: duration, a dot, then the performer.  The performer
            // column takes the leftover width so it is the one that ellipsizes.
            Grid meta;
            meta.ColumnDefinitions().Append(ColumnDefinition{});
            meta.ColumnDefinitions().Append(ColumnDefinition{});
            meta.ColumnDefinitions().Append(ColumnDefinition{});
            meta.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::Auto());
            meta.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());
            meta.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));

            if (!duration.empty())
            {
                TextBlock durationBlock;
                durationBlock.Text(hstring{ duration });
                durationBlock.FontSize(12);
                durationBlock.Foreground(SolidColorBrush{ palette.SecondaryText });
                Grid::SetColumn(durationBlock, 0);
                meta.Children().Append(durationBlock);
            }

            if (!duration.empty() && !performer.empty())
            {
                TextBlock dot;
                dot.Text(L"·");
                dot.FontSize(12);
                dot.Margin(ThicknessHelper::FromLengths(6, 0, 6, 0));
                dot.Foreground(SolidColorBrush{ palette.SecondaryText });
                Grid::SetColumn(dot, 1);
                meta.Children().Append(dot);
            }

            if (!performer.empty())
            {
                TextBlock performerBlock;
                performerBlock.Text(hstring{ performer });
                performerBlock.FontSize(12);
                performerBlock.TextTrimming(TextTrimming::CharacterEllipsis);
                performerBlock.Foreground(SolidColorBrush{ palette.SecondaryText });
                SetOverflowToolTip(performerBlock, performer);
                Grid::SetColumn(performerBlock, 2);
                meta.Children().Append(performerBlock);
            }

            details.Children().Append(meta);
        }

        Grid::SetColumn(details, 1);
        row.Children().Append(number);
        row.Children().Append(details);

        Border separator;
        separator.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 1));
        separator.BorderBrush(SolidColorBrush{ palette.Separator });
        separator.Child(row);
        return separator;
    }

    void BuildPlaylistView(LiveRoot& root, Palette const& palette)
    {
        root.ListPanel.Children().Clear();

        PlaylistDocument const& document = root.Document;
        if (document.Tracks.empty())
        {
            TextBlock empty;
            empty.Text(L"No tracks found.");
            empty.Foreground(SolidColorBrush{ palette.SecondaryText });
            root.ListPanel.Children().Append(empty);
            return;
        }

        // The card carries only the list: album title, artist and track count all
        // live in the album strip above it.
        const size_t trackCount = document.Tracks.size();
        const size_t count = (std::min)(trackCount, kMaxRenderedTracks);
        for (size_t index = 0; index < count; ++index)
        {
            root.ListPanel.Children().Append(BuildTrackRow(root, document.Tracks[index], index, palette));
        }

        if (trackCount > count)
        {
            const size_t remaining = trackCount - count;
            TextBlock more;
            more.Text(hstring{ L"… " + FormatCount(remaining) +
                ((remaining == 1) ? L" more track not shown" : L" more tracks not shown") });
            more.FontSize(12);
            more.Margin(ThicknessHelper::FromLengths(4, 8, 4, 4));
            more.Foreground(SolidColorBrush{ palette.SecondaryText });
            root.ListPanel.Children().Append(more);
        }
    }

    void ApplyMode(LiveRoot& root, Palette const& palette)
    {
        const bool showPlaylist = root.ShowPlaylist && !root.Document.Tracks.empty();

        // Both views stay alive; switching only swaps what the card shows, so
        // parse results and scroll positions survive a mode change.
        root.ListScroller.Visibility(showPlaylist ? Visibility::Visible : Visibility::Collapsed);
        root.TextScroller.Visibility(showPlaylist ? Visibility::Collapsed : Visibility::Visible);
        root.AlbumInfo.Visibility(showPlaylist ? Visibility::Visible : Visibility::Collapsed);

        // The item labels carry their own brush: a NavigationViewItem resolves
        // its foreground from theme resources, which follow the *process*
        // theme, not the palette we just painted on the surface.  Explicit
        // TextBlocks keep the menu readable either way.
        root.PlaylistLabel.Foreground(SolidColorBrush{ showPlaylist ? palette.PrimaryText : palette.SecondaryText });
        root.TextLabel.Foreground(SolidColorBrush{ showPlaylist ? palette.SecondaryText : palette.PrimaryText });
    }

    void ApplyPalette(LiveRoot& root)
    {
        if (!root.Root)
        {
            return;
        }

        const bool dark = EffectiveDark(root.Theme, root.SystemDark);
        const bool hostPalette =
            UseHostPalette(root.UseHostColors, root.HostBackground, root.HostText, dark);
        const Palette palette = PaletteFor(dark, hostPalette, root.HostBackground, root.HostText);

        root.Root.RequestedTheme(RequestedThemeFor(dark));
        root.Root.Background(SolidColorBrush{ palette.Background });
        root.ModeNav.Background(SolidColorBrush{ palette.Background });
        root.StripCover.Background(SolidColorBrush{ palette.Background });
        root.Card.Background(SolidColorBrush{ palette.Card });
        root.AlbumTitle.Foreground(SolidColorBrush{ palette.PrimaryText });
        root.AlbumMeta.Foreground(SolidColorBrush{ palette.SecondaryText });

        // Runs bake their colour in, so the text view has to be rebuilt.
        BuildTextView(root, palette);
        BuildPlaylistView(root, palette);
        ApplyMode(root, palette);
    }

}

namespace PlaylistPreviewUI
{
    UIElement CreatePreviewRoot()
    {
        LiveRoot state;

        // The mode switch *is* the top of the surface: a NavigationView in Top
        // mode whose MenuItems are the two modes.  WinUI draws the selection
        // indicator (and the hover / pressed states) itself, so there is no
        // hand-rolled title row, no accent rule and no file metadata strip.
        NavigationView nav;
        nav.PaneDisplayMode(NavigationViewPaneDisplayMode::Top);
        nav.IsSettingsVisible(false);
        nav.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        nav.IsPaneToggleButtonVisible(false);
        nav.IsTitleBarAutoPaddingEnabled(false);
        nav.AlwaysShowHeader(false);

        NavigationViewItem playlistItem;
        playlistItem.Name(kPlaylistItemName);
        TextBlock playlistLabel;
        playlistLabel.Text(L"Playlist");
        playlistLabel.FontSize(14);
        playlistItem.Content(playlistLabel);

        NavigationViewItem textItem;
        textItem.Name(kTextItemName);
        TextBlock textLabel;
        textLabel.Text(L"Text");
        textLabel.FontSize(14);
        textItem.Content(textLabel);

        // Nothing parsed yet: the surface starts on the text view and the
        // playlist item only appears once a file has something to list.
        playlistItem.Visibility(Visibility::Collapsed);

        nav.MenuItems().Append(playlistItem);
        nav.MenuItems().Append(textItem);

        // Album strip: album title on the first line, album artist and track count
        // on the second.  It sits on the page (no card), between the menu and the
        // list card, and only exists in playlist mode.
        TextBlock albumTitle;
        albumTitle.FontSize(16);
        albumTitle.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        albumTitle.TextTrimming(TextTrimming::CharacterEllipsis);

        TextBlock albumMeta;
        albumMeta.FontSize(13);
        albumMeta.TextTrimming(TextTrimming::CharacterEllipsis);
        albumMeta.Margin(ThicknessHelper::FromLengths(0, 2, 0, 0));

        StackPanel albumText;
        albumText.VerticalAlignment(VerticalAlignment::Center);
        albumText.Children().Append(albumTitle);
        albumText.Children().Append(albumMeta);

        Grid albumInfo;
        albumInfo.Children().Append(albumText);
        albumInfo.Margin(ThicknessHelper::FromLengths(12, 6, 12, 10));
        albumInfo.Visibility(Visibility::Collapsed);

        Border card;
        card.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        card.Padding(ThicknessHelper::FromLengths(12, 8, 12, 8));
        card.Margin(ThicknessHelper::FromLengths(12, 0, 12, 12));

        ScrollViewer textScroller;
        textScroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
        textScroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        TextBlock textBody;
        textBody.IsTextSelectionEnabled(true);
        textBody.TextWrapping(TextWrapping::NoWrap);
        textScroller.Content(textBody);

        ScrollViewer listScroller;
        listScroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        listScroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        StackPanel listPanel;
        listScroller.Content(listPanel);

        Grid cardLayout;
        cardLayout.RowDefinitions().Append(RowDefinition{});
        cardLayout.RowDefinitions().GetAt(0).Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        cardLayout.Children().Append(textScroller);
        cardLayout.Children().Append(listScroller);
        card.Child(cardLayout);

        Grid root;
        root.RowDefinitions().Append(RowDefinition{});
        root.RowDefinitions().Append(RowDefinition{});
        root.RowDefinitions().Append(RowDefinition{});
        root.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
        root.RowDefinitions().GetAt(1).Height(GridLengthHelper::Auto());
        root.RowDefinitions().GetAt(2).Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));

        // The menu is a sibling of the content, not its host.  Nested in the
        // NavigationView (nav.Content(card)) the card still laid out and
        // reported the right Visibility, but this host never re-rendered the
        // scroller swap performed from inside the menu's SelectionChanged --
        // the mode change looked like a no-op.  As siblings the content is
        // outside the element that is processing the event.
        Grid::SetRow(nav, 0);
        nav.HorizontalAlignment(HorizontalAlignment::Stretch);
        nav.VerticalAlignment(VerticalAlignment::Top);
        Grid::SetRow(albumInfo, 1);
        Grid::SetRow(card, 2);

        // The NavigationView template wraps its content in a SplitView that draws
        // a 1 px divider (NavigationViewBorderThickness) along the bottom of the
        // menu strip.  We hand the NavigationView no content -- the card is a
        // sibling -- so that strip is the only thing left of it.  Its border
        // cannot be reached from here: a Resources entry is ignored ({ThemeResource}
        // only consults theme dictionaries) and a theme-dictionary override takes
        // the control's own theme resources down with it.  Painting over the band
        // is the one approach that always holds, and since the strip and the page
        // share a colour it is invisible.
        Border stripCover;
        stripCover.Height(2);
        stripCover.VerticalAlignment(VerticalAlignment::Bottom);
        stripCover.IsHitTestVisible(false);
        Grid::SetRow(stripCover, 0);

        root.Children().Append(nav);
        root.Children().Append(stripCover);
        root.Children().Append(albumInfo);
        root.Children().Append(card);

        state.Root = root;
        state.ModeNav = nav;
        state.PlaylistItem = playlistItem;
        state.TextItem = textItem;
        state.PlaylistLabel = playlistLabel;
        state.TextLabel = textLabel;
        state.Card = card;
        state.StripCover = stripCover;
        state.AlbumInfo = albumInfo;
        state.AlbumTitle = albumTitle;
        state.AlbumMeta = albumMeta;
        state.TextScroller = textScroller;
        state.ListScroller = listScroller;
        state.TextBody = textBody;
        state.ListPanel = listPanel;
        state.Theme = kTheme_Default;
        state.PreferPlaylist = LoadPreferredPlaylistMode();
        state.SystemDark = SystemPrefersDark();

        auto shared = std::make_shared<LiveRoot>(state);

        // Key the state by the exact pointer the caller gets back.  Note this has
        // to be the UIElement interface pointer, not the Grid one: C++/WinRT
        // hands out a different pointer per interface, and the shell echoes back
        // whatever CreatePreviewRoot returned.
        winrt::Microsoft::UI::Xaml::UIElement element = root;
        {
            std::lock_guard guard{ g_rootsLock };
            g_roots[static_cast<IInspectable*>(winrt::get_abi(element))] = shared;
        }

        // The element that raises the event keeps the tree (and therefore this
        // state) alive, so a weak reference is enough and avoids a cycle.
        std::weak_ptr<LiveRoot> weak = shared;

        nav.SelectionChanged([weak](NavigationView const&, NavigationViewSelectionChangedEventArgs const& args) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }

            const auto item = args.SelectedItem().try_as<NavigationViewItem>();
            if (!item)
            {
                return;   // deselected, or the (hidden) settings item
            }

            const hstring name = item.Name();
            const bool isPlaylist = (name == kPlaylistItemName);
            if (!isPlaylist && (name != kTextItemName))
            {
                return;
            }

            // Programmatic selections (loading a file) always set ShowPlaylist
            // first, so only a real user choice can differ here -- which is
            // exactly what should be remembered for the next preview.
            if (self->ShowPlaylist != isPlaylist)
            {
                self->ShowPlaylist = isPlaylist;
                self->PreferPlaylist = isPlaylist;
                SavePreferredPlaylistMode(isPlaylist);
                ApplyPalette(*self);
            }
        });

        nav.SelectedItem(textItem);

        // Loaded fires once the template exists, which is when the pane grid we
        // need to rebalance is finally in the tree.
        nav.Loaded([weak](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.lock())
            {
                CentreNavigationViewItems(self->ModeNav);
            }
        });

        ApplyPalette(*shared);

        return element;
    }

    void SetText(IInspectable* key, std::wstring const& displayName, std::string const& utf8Text)
    {
        auto self = FindRoot(key);
        if (!self)
        {
            throw hresult_error(E_INVALIDARG, L"unknown preview root");
        }
        LiveRoot& root = *self;

        root.DisplayName = displayName;
        root.ByteCount = utf8Text.size();
        root.Text = std::wstring{ winrt::to_hstring(utf8Text) };
        root.Binary = LooksBinary(root.Text);
        root.Document = root.Binary ? PlaylistDocument{} : ParsePlaylist(displayName, root.Text);

        ApplyAlbumInfo(root);

        const bool hasPlaylist = (root.Document.Kind != PlaylistKind::Unknown) && !root.Document.Tracks.empty();
        // A file with nothing to list always opens as text; otherwise the mode
        // the user last chose wins (see LoadPreferredPlaylistMode).
        const bool showPlaylist = hasPlaylist && root.PreferPlaylist;
        root.ShowPlaylist = showPlaylist;

        // Collapse rather than disable the item: a NavigationViewItem that has
        // nothing to show must not stay in the menu as a dead entry.  (Disabling
        // is not an option either -- a disabled item cannot be styled back to
        // visibility in this host.)
        if (hasPlaylist)
        {
            root.PlaylistItem.Visibility(Visibility::Visible);
        }
        // ShowPlaylist is already in place, so the selection change this raises
        // is a no-op in the handler and does not overwrite the preference.
        root.ModeNav.SelectedItem(showPlaylist ? root.PlaylistItem : root.TextItem);
        if (!hasPlaylist)
        {
            root.PlaylistItem.Visibility(Visibility::Collapsed);
        }

        ApplyPalette(root);
    }

    void SetVisuals(
        IInspectable* key,
        int32_t theme,
        bool useHostColors,
        uint32_t backgroundRef,
        uint32_t textRef,
        std::wstring const& fontFamily,
        float fontSize)
    {
        auto self = FindRoot(key);
        if (!self)
        {
            throw hresult_error(E_INVALIDARG, L"unknown preview root");
        }
        LiveRoot& root = *self;

        // The shell re-pushes the same visuals once a second so that a Windows
        // light/dark switch reaches a pane that is already open; the OS theme is
        // therefore the only input that can change behind an otherwise identical
        // call.  Compare everything (including it) and do nothing when nothing
        // moved -- that also keeps the periodic nudge from rebuilding the text
        // view over and over.
        const bool systemDark = SystemPrefersDark();
        if (root.VisualsApplied &&
            (root.Theme == theme) &&
            (root.UseHostColors == useHostColors) &&
            (root.HostBackground == backgroundRef) &&
            (root.HostText == textRef) &&
            (root.FontFamily == fontFamily) &&
            (root.FontSize == fontSize) &&
            (root.SystemDark == systemDark))
        {
            return;
        }

        root.Theme = theme;
        root.UseHostColors = useHostColors;
        root.HostBackground = backgroundRef;
        root.HostText = textRef;
        root.FontFamily = fontFamily;
        root.FontSize = fontSize;
        root.SystemDark = systemDark;
        root.VisualsApplied = true;

        ApplyPalette(root);
    }

    void ReleasePreviewRoot(IInspectable* key) noexcept
    {
        std::lock_guard guard{ g_rootsLock };
        g_roots.erase(key);
    }
}
