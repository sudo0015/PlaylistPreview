#include "pch.h"
#include "Strings.h"

namespace
{
    enum class UiLanguage
    {
        English,
        SimplifiedChinese,
    };

    // Same key the playlist/text preference lives in (see PreviewRoot.cpp).
    constexpr wchar_t kSettingsKeyPath[] = L"Software\\PlaylistPreview";
    constexpr wchar_t kUiLanguageValueName[] = L"UiLanguage";

    // The placeholder a table entry may carry; see FormatString.
    constexpr std::wstring_view kCountToken = L"{0}";

    bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right)
    {
        return (left.size() == right.size()) &&
               (::CompareStringOrdinal(
                    left.data(), static_cast<int>(left.size()),
                    right.data(), static_cast<int>(right.size()),
                    TRUE) == CSTR_EQUAL);
    }

    // Whether "zh-Hans-CN" carries the given BCP-47 subtag, compared as a whole
    // subtag so "TW" cannot match inside a longer name.
    bool HasSubtag(std::wstring_view name, std::wstring_view wanted)
    {
        size_t start = 0;
        for (;;)
        {
            const size_t dash = name.find(L'-', start);
            const std::wstring_view part = (dash == std::wstring_view::npos)
                ? name.substr(start)
                : name.substr(start, dash - start);

            if (!part.empty() && EqualsIgnoreCase(part, wanted))
            {
                return true;
            }
            if (dash == std::wstring_view::npos)
            {
                return false;
            }
            start = dash + 1;
        }
    }

    // Maps one BCP-47 name onto a table.  Returns false when we ship nothing for
    // it, so the caller keeps walking the user's language list.
    bool TryMatchLanguage(std::wstring_view name, UiLanguage& out)
    {
        const size_t dash = name.find(L'-');
        const std::wstring_view primary = (dash == std::wstring_view::npos)
            ? name
            : name.substr(0, dash);

        if (EqualsIgnoreCase(primary, L"en"))
        {
            out = UiLanguage::English;
            return true;
        }

        if (EqualsIgnoreCase(primary, L"zh"))
        {
            // Only Simplified is shipped.  Traditional (zh-Hant, zh-TW, zh-HK,
            // zh-MO) gets no table and falls through to English; adding one is a
            // table next to SimplifiedChineseString below.
            if (HasSubtag(name, L"Hant") || HasSubtag(name, L"TW") ||
                HasSubtag(name, L"HK") || HasSubtag(name, L"MO"))
            {
                return false;
            }

            out = UiLanguage::SimplifiedChinese;
            return true;
        }

        return false;
    }

    bool TryLanguageFromOverride(UiLanguage& out)
    {
        wchar_t buffer[64]{};
        DWORD size = sizeof(buffer);
        if (::RegGetValueW(
                HKEY_CURRENT_USER,
                kSettingsKeyPath,
                kUiLanguageValueName,
                RRF_RT_REG_SZ,
                nullptr,
                buffer,
                &size) != ERROR_SUCCESS)
        {
            return false;
        }

        return TryMatchLanguage(buffer, out);
    }

    UiLanguage ResolveLanguage()
    {
        UiLanguage override{};
        if (TryLanguageFromOverride(override))
        {
            return override;
        }

        // The user's Windows display languages, most preferred first.  A null
        // buffer asks for the size.  The returned list is double-null terminated.
        ULONG count = 0;
        ULONG length = 0;
        if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &length) &&
            (length > 0))
        {
            std::wstring buffer(length, L'\0');
            if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &length))
            {
                for (const wchar_t* entry = buffer.c_str(); *entry != L'\0';
                     entry += wcslen(entry) + 1)
                {
                    UiLanguage matched{};
                    if (TryMatchLanguage(entry, matched))
                    {
                        return matched;
                    }
                }
            }
        }

        return UiLanguage::English;
    }

    UiLanguage CurrentLanguage()
    {
        // Resolved once: the display language does not change under a running
        // preview pane, and the probe touches the registry.
        static const UiLanguage language = ResolveLanguage();
        return language;
    }

    // No default label: a new StringId makes MSVC's C4062 (/W4) point at every
    // table that has not been updated.
    std::wstring_view EnglishString(PlaylistPreviewUI::StringId id)
    {
        using PlaylistPreviewUI::StringId;
        switch (id)
        {
        case StringId::PlaylistLabel:  return L"Playlist";
        case StringId::TextLabel:      return L"Text";
        case StringId::NoTracksFound:  return L"No tracks found.";
        case StringId::UntitledTrack:  return L"(Untitled)";
        case StringId::NotTextWarning: return L"This does not look like text. Showing the raw content.";
        case StringId::TextTruncated:  return L"… (content truncated; showing the first part only)";
        case StringId::TrackCountOne:  return L"{0} Track";
        case StringId::TrackCountMany: return L"{0} Tracks";
        case StringId::MoreTracksOne:  return L"… {0} more track not shown";
        case StringId::MoreTracksMany: return L"… {0} more tracks not shown";
        }
        return {};
    }

    std::wstring_view SimplifiedChineseString(PlaylistPreviewUI::StringId id)
    {
        using PlaylistPreviewUI::StringId;
        switch (id)
        {
        case StringId::PlaylistLabel:  return L"播放列表";
        case StringId::TextLabel:      return L"文本";
        case StringId::NoTracksFound:  return L"未找到曲目。";
        case StringId::UntitledTrack:  return L"（无标题）";
        case StringId::NotTextWarning: return L"这似乎不是文本内容，已显示原始内容。";
        case StringId::TextTruncated:  return L"…（内容已截断，仅显示开头部分）";
        // 首 is the counter for songs and does not change with the number.
        case StringId::TrackCountOne:  return L"{0} 首";
        case StringId::TrackCountMany: return L"{0} 首";
        case StringId::MoreTracksOne:  return L"… 还有 {0} 首未显示";
        case StringId::MoreTracksMany: return L"… 还有 {0} 首未显示";
        }
        return {};
    }
}

namespace PlaylistPreviewUI
{
    std::wstring_view GetString(StringId id)
    {
        switch (CurrentLanguage())
        {
        case UiLanguage::SimplifiedChinese: return SimplifiedChineseString(id);
        case UiLanguage::English: break;
        }
        return EnglishString(id);
    }

    std::wstring FormatString(StringId id, size_t value)
    {
        std::wstring text{ GetString(id) };
        const size_t token = text.find(kCountToken);
        if (token != std::wstring::npos)
        {
            text.replace(token, kCountToken.size(), std::to_wstring(value));
        }
        return text;
    }

    std::wstring FormatTrackCount(size_t count)
    {
        return FormatString(count == 1 ? StringId::TrackCountOne : StringId::TrackCountMany, count);
    }

    std::wstring FormatMoreTracks(size_t count)
    {
        return FormatString(count == 1 ? StringId::MoreTracksOne : StringId::MoreTracksMany, count);
    }
}
