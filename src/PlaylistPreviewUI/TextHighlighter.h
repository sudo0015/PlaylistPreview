#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "PlaylistModel.h"

namespace PlaylistPreviewUI
{
    enum class SyntaxKind
    {
        Plain,
        Comment,
        Keyword,
        String,
        Number,
        Directive,
        Path,
    };

    struct SyntaxSpan
    {
        SyntaxKind Kind{ SyntaxKind::Plain };
        std::wstring Text;
    };

    // Tokenizes one line (no line breaks) into coloured spans.
    void HighlightLine(PlaylistKind kind, std::wstring_view line, std::vector<SyntaxSpan>& out);
}
