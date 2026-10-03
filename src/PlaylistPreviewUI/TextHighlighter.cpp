#include "pch.h"
#include "TextHighlighter.h"

#include <cwchar>

namespace
{
    bool IsSpace(wchar_t c)
    {
        return (c == L' ') || (c == L'\t');
    }

    bool IsDigit(wchar_t c)
    {
        return (c >= L'0') && (c <= L'9');
    }

    void Push(std::vector<PlaylistPreviewUI::SyntaxSpan>& out,
              PlaylistPreviewUI::SyntaxKind kind,
              std::wstring_view text)
    {
        if (!text.empty())
        {
            out.push_back(PlaylistPreviewUI::SyntaxSpan{ kind, std::wstring{ text } });
        }
    }

    // Colours the value part of a CUE line: "..." strings and mm:ss:ff times.
    void HighlightCueValue(std::wstring_view text, std::vector<PlaylistPreviewUI::SyntaxSpan>& out)
    {
        using PlaylistPreviewUI::SyntaxKind;

        size_t index = 0;
        size_t plainStart = 0;

        const auto flushPlain = [&](size_t end) {
            if (end > plainStart)
            {
                Push(out, SyntaxKind::Plain, text.substr(plainStart, end - plainStart));
            }
        };

        while (index < text.size())
        {
            const wchar_t c = text[index];

            if (c == L'"')
            {
                flushPlain(index);
                size_t end = text.find(L'"', index + 1);
                end = (end == std::wstring_view::npos) ? text.size() : end + 1;
                Push(out, SyntaxKind::String, text.substr(index, end - index));
                index = end;
                plainStart = index;
                continue;
            }

            if (IsDigit(c))
            {
                size_t end = index;
                while ((end < text.size()) && (IsDigit(text[end]) || (text[end] == L':') || (text[end] == L'.')))
                {
                    ++end;
                }

                flushPlain(index);
                Push(out, SyntaxKind::Number, text.substr(index, end - index));
                index = end;
                plainStart = index;
                continue;
            }

            ++index;
        }

        flushPlain(text.size());
    }

    bool TagEquals(std::wstring_view tag, wchar_t const* expected)
    {
        const size_t length = ::wcslen(expected);
        return (tag.size() == length) && (::_wcsnicmp(tag.data(), expected, length) == 0);
    }
}

namespace PlaylistPreviewUI
{
    void HighlightLine(PlaylistKind kind, std::wstring_view line, std::vector<SyntaxSpan>& out)
    {
        out.clear();

        if (line.empty())
        {
            return;
        }

        if (kind == PlaylistKind::M3u8)
        {
            if (line.front() != L'#')
            {
                // A plain entry: path or URL.
                Push(out, SyntaxKind::Path, line);
                return;
            }

            const size_t colon = line.find(L':');
            if (colon == std::wstring_view::npos)
            {
                Push(out, SyntaxKind::Directive, line);
                return;
            }

            const std::wstring_view tag = line.substr(0, colon);
            Push(out, SyntaxKind::Directive, line.substr(0, colon + 1));

            const std::wstring_view rest = line.substr(colon + 1);
            if (TagEquals(tag, L"#EXTINF"))
            {
                // #EXTINF:<seconds>,<title>
                const size_t comma = rest.find(L',');
                if (comma == std::wstring_view::npos)
                {
                    Push(out, SyntaxKind::Number, rest);
                }
                else
                {
                    Push(out, SyntaxKind::Number, rest.substr(0, comma));
                    Push(out, SyntaxKind::Plain, L",");
                    Push(out, SyntaxKind::String, rest.substr(comma + 1));
                }
            }
            else
            {
                Push(out, SyntaxKind::String, rest);
            }
            return;
        }

        if (kind == PlaylistKind::Cue)
        {
            size_t end = 0;
            while ((end < line.size()) && !IsSpace(line[end]))
            {
                ++end;
            }

            const std::wstring_view command = line.substr(0, end);
            const bool isRemark = TagEquals(command, L"REM");
            Push(out, isRemark ? SyntaxKind::Comment : SyntaxKind::Keyword, command);

            HighlightCueValue(line.substr(end), out);
            return;
        }

        Push(out, SyntaxKind::Plain, line);
    }
}
