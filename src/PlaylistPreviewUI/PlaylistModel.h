#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PlaylistPreviewUI
{
    enum class PlaylistKind
    {
        Unknown,
        Cue,
        M3u8,
    };

    struct PlaylistTrack
    {
        // 1-based position shown in the list.
        int32_t Number{ 0 };
        std::wstring Title;
        std::wstring Performer;
        // CUE: the FILE the track lives in.  M3U8: the URI/path of the entry.
        std::wstring Location;
        // CUE: INDEX 01.  M3U8: running total of the preceding durations.
        double StartSeconds{ -1.0 };
        // Negative means "not known" (e.g. the last CUE track without the audio
        // file, or an M3U8 entry without #EXTINF).
        double DurationSeconds{ -1.0 };
    };

    struct PlaylistDocument
    {
        PlaylistKind Kind{ PlaylistKind::Unknown };
        std::wstring Title;
        std::wstring Performer;
        std::wstring Album;
        std::wstring Genre;
        std::wstring Date;
        // CUE: the first FILE statement.  M3U8: #PLAYLIST name.
        std::wstring Source;
        std::vector<PlaylistTrack> Tracks;
    };

    // "CUE" / "M3U8" / "" - used in the header and for the status line.
    wchar_t const* KindLabel(PlaylistKind kind);

    // "3:42" or "1:02:03"; empty when the value is unknown.
    std::wstring FormatDuration(double seconds);
}
