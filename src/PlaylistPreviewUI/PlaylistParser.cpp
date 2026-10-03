#include "pch.h"
#include "PlaylistParser.h"

#include <algorithm>
#include <cwctype>
#include <string_view>
#include <vector>

using namespace winrt;
using namespace PlaylistPreviewUI;

namespace
{
    bool IsSpace(wchar_t c)
    {
        return (c == L' ') || (c == L'\t') || (c == L'\r') || (c == L'\n');
    }

    std::wstring_view Trim(std::wstring_view text)
    {
        while (!text.empty() && IsSpace(text.front()))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && IsSpace(text.back()))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    std::wstring ToUpper(std::wstring_view text)
    {
        std::wstring result{ text };
        std::transform(result.begin(), result.end(), result.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(::towupper(c)); });
        return result;
    }

    // Splits on any of CR, LF, CRLF.
    std::vector<std::wstring_view> SplitLines(std::wstring const& text)
    {
        std::vector<std::wstring_view> lines;
        std::wstring_view view{ text };
        size_t start = 0;

        while (start <= view.size())
        {
            size_t end = view.find_first_of(L"\r\n", start);
            if (end == std::wstring_view::npos)
            {
                lines.push_back(view.substr(start));
                break;
            }

            lines.push_back(view.substr(start, end - start));
            if ((view[end] == L'\r') && (end + 1 < view.size()) && (view[end + 1] == L'\n'))
            {
                start = end + 2;
            }
            else
            {
                start = end + 1;
            }
        }

        if (!lines.empty() && lines.back().empty())
        {
            lines.pop_back();
        }
        return lines;
    }

    // First word of a CUE command line, upper-cased.
    std::wstring_view FirstWord(std::wstring_view line, std::wstring_view& rest)
    {
        line = Trim(line);
        size_t end = 0;
        while ((end < line.size()) && !IsSpace(line[end]))
        {
            ++end;
        }

        rest = Trim(line.substr(end));
        return line.substr(0, end);
    }

    // Parses the leading "..." of a CUE value; falls back to the whole token.
    std::wstring ParseQuoted(std::wstring_view text)
    {
        text = Trim(text);
        if (text.empty())
        {
            return {};
        }

        if (text.front() != L'"')
        {
            const size_t space = text.find_first_of(L" \t");
            return std::wstring{ text.substr(0, space == std::wstring_view::npos ? text.size() : space) };
        }

        text.remove_prefix(1);
        std::wstring value;
        while (!text.empty())
        {
            const wchar_t c = text.front();
            text.remove_prefix(1);
            if (c == L'"')
            {
                break;
            }
            value.push_back(c);
        }
        return value;
    }

    // "mm:ss:ff" (75 frames per second) -> seconds.  -1 when malformed.
    double ParseTimecode(std::wstring_view text)
    {
        text = Trim(text);
        int parts[3]{ 0, 0, 0 };
        int index = 0;
        int value = 0;
        bool any = false;

        for (const wchar_t c : text)
        {
            if ((c >= L'0') && (c <= L'9'))
            {
                value = value * 10 + (c - L'0');
                any = true;
            }
            else if (c == L':')
            {
                if (index >= 2)
                {
                    return -1.0;
                }
                parts[index++] = value;
                value = 0;
            }
            else if (IsSpace(c))
            {
                break;
            }
            else
            {
                return -1.0;
            }
        }

        if (!any)
        {
            return -1.0;
        }
        parts[index] = value;

        if (index == 0)
        {
            return static_cast<double>(parts[0]);
        }
        if (index == 1)
        {
            return parts[0] * 60.0 + parts[1];
        }
        return parts[0] * 60.0 + parts[1] + parts[2] / 75.0;
    }

    double ParseDouble(std::wstring_view text)
    {
        text = Trim(text);
        if (text.empty())
        {
            return -1.0;
        }

        std::wstring buffer{ text };
        wchar_t* end = nullptr;
        const double value = ::wcstod(buffer.c_str(), &end);
        if ((end == buffer.c_str()) || (value < 0.0))
        {
            return -1.0;
        }
        return value;
    }

    std::wstring FileNameOf(std::wstring_view path)
    {
        const size_t separator = path.find_last_of(L"\\/");
        return std::wstring{ (separator == std::wstring_view::npos) ? path : path.substr(separator + 1) };
    }

    bool EndsWith(std::wstring_view text, std::wstring_view suffix)
    {
        return (text.size() >= suffix.size()) &&
               (::_wcsnicmp(text.data() + (text.size() - suffix.size()), suffix.data(), suffix.size()) == 0);
    }

    PlaylistDocument ParseCue(std::vector<std::wstring_view> const& lines)
    {
        PlaylistDocument document;
        document.Kind = PlaylistKind::Cue;

        PlaylistTrack* current = nullptr;
        bool trackHasIndex01 = false;

        for (const std::wstring_view rawLine : lines)
        {
            std::wstring_view rest;
            const std::wstring_view command = FirstWord(rawLine, rest);
            if (command.empty())
            {
                continue;
            }

            const std::wstring keyword = ToUpper(command);

            if (keyword == L"REM")
            {
                // REM GENRE "..." / REM DATE ... - album level metadata.
                std::wstring_view value;
                const std::wstring_view key = FirstWord(rest, value);
                const std::wstring upperKey = ToUpper(key);
                if (upperKey == L"GENRE")
                {
                    document.Genre = ParseQuoted(value);
                }
                else if (upperKey == L"DATE")
                {
                    document.Date = ParseQuoted(value);
                }
                continue;
            }

            if (keyword == L"TITLE")
            {
                if (current != nullptr)
                {
                    current->Title = ParseQuoted(rest);
                }
                else
                {
                    document.Title = ParseQuoted(rest);
                }
                continue;
            }

            if (keyword == L"PERFORMER")
            {
                const std::wstring value = ParseQuoted(rest);
                if (current != nullptr)
                {
                    current->Performer = value;
                }
                else
                {
                    document.Performer = value;
                }
                continue;
            }

            if (keyword == L"FILE")
            {
                document.Source = ParseQuoted(rest);
                continue;
            }

            if (keyword == L"TRACK")
            {
                // "01 AUDIO" -> number "01", remainder "AUDIO".
                std::wstring_view remainder;
                const std::wstring_view number = FirstWord(rest, remainder);

                PlaylistTrack track;
                track.Number = static_cast<int32_t>(::wcstol(std::wstring{ number }.c_str(), nullptr, 10));
                if (track.Number == 0)
                {
                    track.Number = static_cast<int32_t>(document.Tracks.size()) + 1;
                }

                document.Tracks.push_back(track);
                current = &document.Tracks.back();
                trackHasIndex01 = false;
                continue;
            }

            if ((keyword == L"INDEX") && (current != nullptr))
            {
                // "01 00:02:34" -> index "01", timecode "00:02:34".
                std::wstring_view timecode;
                const std::wstring_view indexValue = FirstWord(rest, timecode);
                const int indexNumber = static_cast<int>(::wcstol(std::wstring{ indexValue }.c_str(), nullptr, 10));
                const double seconds = ParseTimecode(timecode);
                if (seconds < 0.0)
                {
                    continue;
                }

                if ((indexNumber == 1) || (!trackHasIndex01 && (indexNumber == 0) && (current->StartSeconds < 0.0)))
                {
                    current->StartSeconds = seconds;
                    trackHasIndex01 = (indexNumber == 1);
                }
                continue;
            }
        }

        // Derive per-track durations from the following track's start.
        for (size_t index = 0; index + 1 < document.Tracks.size(); ++index)
        {
            const double start = document.Tracks[index].StartSeconds;
            const double next = document.Tracks[index + 1].StartSeconds;
            if ((start >= 0.0) && (next >= start))
            {
                document.Tracks[index].DurationSeconds = next - start;
            }
        }

        for (auto& track : document.Tracks)
        {
            if (track.Location.empty())
            {
                track.Location = document.Source;
            }
        }

        return document;
    }

    PlaylistDocument ParseM3u8(std::vector<std::wstring_view> const& lines)
    {
        PlaylistDocument document;
        document.Kind = PlaylistKind::M3u8;

        double pendingDuration = -1.0;
        std::wstring pendingTitle;
        std::wstring pendingGroup;
        double runningStart = 0.0;
        bool firstLine = true;

        for (const std::wstring_view rawLine : lines)
        {
            const std::wstring_view line = Trim(rawLine);
            if (line.empty())
            {
                continue;
            }

            if (firstLine)
            {
                firstLine = false;
                if ((line.size() >= 7) && (::_wcsnicmp(line.data(), L"#EXTM3U", 7) == 0))
                {
                    continue;
                }
            }

            if (line.front() == L'#')
            {
                if (line.size() > 8 && ::_wcsnicmp(line.data(), L"#EXTINF:", 8) == 0)
                {
                    std::wstring_view value = line.substr(8);
                    const size_t comma = value.find(L',');
                    pendingDuration = ParseDouble((comma == std::wstring_view::npos) ? value : value.substr(0, comma));
                    pendingTitle = (comma == std::wstring_view::npos) ? std::wstring{} : std::wstring{ Trim(value.substr(comma + 1)) };
                }
                else if (line.size() > 9 && ::_wcsnicmp(line.data(), L"#PLAYLIST:", 10) == 0)
                {
                    document.Title = std::wstring{ Trim(line.substr(10)) };
                }
                else if (line.size() > 8 && ::_wcsnicmp(line.data(), L"#EXTALB:", 8) == 0)
                {
                    document.Album = std::wstring{ Trim(line.substr(8)) };
                }
                else if (line.size() > 8 && ::_wcsnicmp(line.data(), L"#EXTART:", 8) == 0)
                {
                    document.Performer = std::wstring{ Trim(line.substr(8)) };
                }
                else if (line.size() > 8 && ::_wcsnicmp(line.data(), L"#EXTGRP:", 8) == 0)
                {
                    pendingGroup = std::wstring{ Trim(line.substr(8)) };
                }
                continue;
            }

            PlaylistTrack track;
            track.Number = static_cast<int32_t>(document.Tracks.size()) + 1;
            track.Location = std::wstring{ line };
            track.Title = pendingTitle.empty() ? FileNameOf(line) : pendingTitle;
            if (!pendingGroup.empty())
            {
                track.Performer = pendingGroup;
            }
            track.StartSeconds = runningStart;
            track.DurationSeconds = pendingDuration;

            if (pendingDuration > 0.0)
            {
                runningStart += pendingDuration;
            }

            document.Tracks.push_back(track);
            pendingDuration = -1.0;
            pendingTitle.clear();
        }

        return document;
    }
}

namespace PlaylistPreviewUI
{
    wchar_t const* KindLabel(PlaylistKind kind)
    {
        switch (kind)
        {
        case PlaylistKind::Cue: return L"CUE";
        case PlaylistKind::M3u8: return L"M3U8";
        default: return L"";
        }
    }

    std::wstring FormatDuration(double seconds)
    {
        if (seconds < 0.0)
        {
            return {};
        }

        const int total = static_cast<int>(seconds + 0.5);
        const int hours = total / 3600;
        const int minutes = (total % 3600) / 60;
        const int remainder = total % 60;

        wchar_t buffer[32]{};
        if (hours > 0)
        {
            swprintf_s(buffer, L"%d:%02d:%02d", hours, minutes, remainder);
        }
        else
        {
            swprintf_s(buffer, L"%d:%02d", minutes, remainder);
        }
        return buffer;
    }

    PlaylistDocument ParsePlaylist(std::wstring const& displayName, std::wstring const& text)
    {
        const std::vector<std::wstring_view> lines = SplitLines(text);

        // Content wins over the extension: plenty of .m3u files are really M3U8.
        for (const std::wstring_view line : lines)
        {
            const std::wstring_view trimmed = Trim(line);
            if (trimmed.empty())
            {
                continue;
            }

            if ((trimmed.size() >= 7) && (::_wcsnicmp(trimmed.data(), L"#EXTM3U", 7) == 0))
            {
                return ParseM3u8(lines);
            }
            break;
        }

        bool looksLikeCue = false;
        for (const std::wstring_view line : lines)
        {
            const std::wstring_view trimmed = Trim(line);
            if ((trimmed.size() >= 5) && (::_wcsnicmp(trimmed.data(), L"FILE ", 5) == 0))
            {
                looksLikeCue = true;
                break;
            }
            if ((trimmed.size() >= 6) && (::_wcsnicmp(trimmed.data(), L"TRACK ", 6) == 0))
            {
                looksLikeCue = true;
                break;
            }
        }

        if (looksLikeCue || EndsWith(displayName, L".cue"))
        {
            return ParseCue(lines);
        }

        if (EndsWith(displayName, L".m3u8") || EndsWith(displayName, L".m3u"))
        {
            return ParseM3u8(lines);
        }

        return PlaylistDocument{};
    }

    PlaylistDocument ParsePlaylist(std::wstring const& displayName, std::string const& utf8Text)
    {
        const std::wstring text{ winrt::to_hstring(utf8Text) };
        return ParsePlaylist(displayName, text);
    }
}
