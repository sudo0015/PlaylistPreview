#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Localized UI text for the content library.
//
// The usual WinRT route (.resw -> resources.pri, resolved through ms-appx:///)
// is not open to us: PlaylistPreviewUI.dll is loaded by full path into a host
// process we do not own, so the host's PRI is not ours to extend and there is no
// package graph entry for this DLL.  See docs/registration.md.  A small
// in-binary table keeps the deployment at three files and adds no dependency on
// ms-appx resolution.
//
// Language selection follows the user's Windows display-language list
// (GetUserPreferredUILanguages): the first listed language we ship a table for
// wins.  Anything else -- a language we do not ship, or a missing entry -- falls
// back to English.
//
// For development there is a per-user override that does not exist in normal
// use.  It lives next to the playlist/text preference that PreviewRoot.cpp
// already keeps:
//
//     HKCU\Software\PlaylistPreview\UiLanguage = "en" | "zh-Hans"
//
// Setting it exercises either table without changing Windows' display language.
namespace PlaylistPreviewUI
{
    // Every piece of text the preview surface renders lives here.  Adding a
    // language means adding one function to Strings.cpp -- the call sites below
    // never change.
    enum class StringId
    {
        PlaylistLabel,     // mode switch: the parsed playlist view
        TextLabel,         // mode switch: the raw source view
        NoTracksFound,     // playlist view with nothing to list
        UntitledTrack,     // a track the file gave no title
        NotTextWarning,    // the bytes do not decode to text
        TextTruncated,     // the source view hit its rendering budget
        TrackCountOne,     // "{0} Track"  -- {0} is the count
        TrackCountMany,    // "{0} Tracks"
        MoreTracksOne,     // "… {0} more track not shown"
        MoreTracksMany,    // "… {0} more tracks not shown"
    };

    // The string for the current UI language.  Never throws; an entry a
    // language does not define falls back to English.
    std::wstring_view GetString(StringId id);

    // GetString with the "{0}" placeholder replaced by value.
    std::wstring FormatString(StringId id, size_t value);

    // Counted text.  English distinguishes one from many; a language without
    // plural forms points both ids at the same entry.
    std::wstring FormatTrackCount(size_t count);
    std::wstring FormatMoreTracks(size_t count);
}
