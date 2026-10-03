#pragma once

#include <string>

#include "PlaylistModel.h"

namespace PlaylistPreviewUI
{
    // Detects the format (from the content first, the file name second) and
    // parses it.  Never throws; an unrecognised file comes back as Unknown with
    // no tracks.
    PlaylistDocument ParsePlaylist(std::wstring const& displayName, std::string const& utf8Text);

    // Same, on text that has already been decoded.
    PlaylistDocument ParsePlaylist(std::wstring const& displayName, std::wstring const& text);
}
