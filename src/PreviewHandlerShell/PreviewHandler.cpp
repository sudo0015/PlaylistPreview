#include "pch.h"
#include "PreviewHandler.h"
#include "ClassFactory.h"
#include "Diag.h"
#include "IslandHost.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

#include "..\Shared\PreviewInterop.h"

namespace
{
    // Phase 2 reads the whole file into memory.  The cap keeps a pathological
    // file from exhausting the preview host; Phase 3.6 adds the real policy.
    constexpr size_t kMaxFileBytes = 8 * 1024 * 1024;

    HRESULT SeekToStart(IStream* stream)
    {
        LARGE_INTEGER origin{};
        ULARGE_INTEGER ignored{};
        return stream->Seek(origin, STREAM_SEEK_SET, &ignored);
    }

    HRESULT ReadAllBytes(IStream* stream, std::vector<uint8_t>& bytes)
    {
        SeekToStart(stream);

        bytes.clear();
        bytes.reserve(64 * 1024);

        uint8_t buffer[64 * 1024];
        for (;;)
        {
            ULONG read = 0;
            const HRESULT hr = stream->Read(buffer, static_cast<ULONG>(sizeof(buffer)), &read);
            if (FAILED(hr))
            {
                return hr;
            }

            // S_FALSE from IStream::Read only means "fewer bytes than asked
            // for", which is the normal case for the final read of a file.
            if (read == 0)
            {
                return S_OK;
            }

            const size_t remaining = (bytes.size() < kMaxFileBytes) ? (kMaxFileBytes - bytes.size()) : 0;
            const size_t take = (std::min)(remaining, static_cast<size_t>(read));
            bytes.insert(bytes.end(), buffer, buffer + take);

            if (take < read)
            {
                PlaylistPreview::Shell::DiagLog(L"file truncated at %zu bytes", kMaxFileBytes);
                return S_OK;
            }
        }
    }

    bool IsValidUtf8(std::string_view text)
    {
        if (text.empty())
        {
            return true;
        }

        const int wide = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), nullptr, 0);
        return wide > 0;
    }

    std::string WideToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return {};
        }

        std::string result(static_cast<size_t>(size), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring BytesToWide(const uint8_t* data, size_t size, UINT codePage)
    {
        if (size == 0)
        {
            return {};
        }

        const int characters = ::MultiByteToWideChar(codePage, 0,
            reinterpret_cast<const char*>(data), static_cast<int>(size), nullptr, 0);
        if (characters <= 0)
        {
            return {};
        }

        std::wstring result(static_cast<size_t>(characters), L'\0');
        ::MultiByteToWideChar(codePage, 0, reinterpret_cast<const char*>(data), static_cast<int>(size),
            result.data(), characters);
        return result;
    }

    // Phase 3.6 replaces this with proper encoding detection; for now: honour a
    // BOM, otherwise use UTF-8 when it is valid and fall back to the ANSI code
    // page (which is what most legacy GBK cue sheets on a zh-CN machine need).
    std::string TranscodeToUtf8(const std::vector<uint8_t>& bytes)
    {
        const size_t size = bytes.size();
        const uint8_t* data = bytes.data();

        if (size == 0)
        {
            return {};
        }

        if ((size >= 3) && (data[0] == 0xEF) && (data[1] == 0xBB) && (data[2] == 0xBF))
        {
            return std::string{ reinterpret_cast<const char*>(data + 3), size - 3 };
        }

        if ((size >= 2) && (data[0] == 0xFF) && (data[1] == 0xFE))
        {
            return WideToUtf8(std::wstring_view{ reinterpret_cast<const wchar_t*>(data + 2), (size - 2) / sizeof(wchar_t) });
        }

        if ((size >= 2) && (data[0] == 0xFE) && (data[1] == 0xFF))
        {
            std::wstring swapped;
            swapped.resize((size - 2) / 2);
            for (size_t index = 0; index < swapped.size(); ++index)
            {
                swapped[index] = static_cast<wchar_t>((static_cast<uint16_t>(data[2 + index * 2]) << 8) |
                                                      static_cast<uint16_t>(data[3 + index * 2]));
            }
            return WideToUtf8(swapped);
        }

        const std::string_view text{ reinterpret_cast<const char*>(data), size };
        if (IsValidUtf8(text))
        {
            return std::string{ text };
        }

        PlaylistPreview::Shell::DiagLog(L"file is not valid UTF-8; decoding with the ANSI code page");
        return WideToUtf8(BytesToWide(data, size, CP_ACP));
    }

    std::wstring FileNameFromPath(const wchar_t* path)
    {
        if (path == nullptr)
        {
            return {};
        }

        const std::wstring_view view{ path };
        const size_t separator = view.find_last_of(L"\\/");
        return std::wstring{ (separator == std::wstring_view::npos) ? view : view.substr(separator + 1) };
    }
}

namespace PlaylistPreview::Shell
{
    PreviewHandler::PreviewHandler()
    {
        AddObjectRef();
        DiagLog(L"PreviewHandler created");
    }

    PreviewHandler::~PreviewHandler()
    {
        if (m_site != nullptr)
        {
            m_site->Release();
            m_site = nullptr;
        }

        ReleaseObjectRef();
        DiagLog(L"PreviewHandler destroyed");
    }

    HRESULT __stdcall PreviewHandler::Initialize(IStream* stream, DWORD mode) noexcept
    {
        if (stream == nullptr)
        {
            return E_POINTER;
        }

        try
        {
            std::lock_guard guard{ m_lock };

            STATSTG stats{};
            std::vector<uint8_t> bytes;
            const HRESULT hr = ReadAllBytes(stream, bytes);
            if (FAILED(hr))
            {
                return hr;
            }

            // An IStream over a file carries its name, which saves the host from
            // having to tell us what it is previewing.
            if (SUCCEEDED(stream->Stat(&stats, STATFLAG_DEFAULT)) && (stats.pwcsName != nullptr))
            {
                m_displayName = FileNameFromPath(stats.pwcsName);
                ::CoTaskMemFree(stats.pwcsName);
            }

            m_loaded = true;
            DiagLog(L"IInitializeWithStream: mode=0x%08X, %zu bytes, name='%s'",
                static_cast<unsigned int>(mode), bytes.size(), m_displayName.c_str());

            return LoadFromBytes(std::move(bytes));
        }
        catch (winrt::hresult_error const& e)
        {
            return e.code();
        }
        catch (...)
        {
            return winrt::to_hresult();
        }
    }

    HRESULT __stdcall PreviewHandler::Initialize(LPCWSTR filePath, DWORD mode) noexcept
    {
        if (filePath == nullptr)
        {
            return E_POINTER;
        }

        try
        {
            std::lock_guard guard{ m_lock };

            const HANDLE file = ::CreateFileW(filePath, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return HRESULT_FROM_WIN32(::GetLastError());
            }

            std::vector<uint8_t> bytes;
            bytes.reserve(64 * 1024);

            uint8_t buffer[64 * 1024];
            HRESULT hr = S_OK;
            for (;;)
            {
                DWORD read = 0;
                if (!::ReadFile(file, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr))
                {
                    hr = HRESULT_FROM_WIN32(::GetLastError());
                    break;
                }
                if (read == 0)
                {
                    break;
                }

                const size_t remaining = (bytes.size() < kMaxFileBytes) ? (kMaxFileBytes - bytes.size()) : 0;
                const size_t take = (std::min)(remaining, static_cast<size_t>(read));
                bytes.insert(bytes.end(), buffer, buffer + take);
                if (take < read)
                {
                    break;
                }
            }

            ::CloseHandle(file);
            if (FAILED(hr))
            {
                return hr;
            }

            m_displayName = FileNameFromPath(filePath);
            m_loaded = true;
            DiagLog(L"IInitializeWithFile: mode=0x%08X, %zu bytes, name='%s', path='%s'",
                static_cast<unsigned int>(mode), bytes.size(), m_displayName.c_str(), filePath);

            return LoadFromBytes(std::move(bytes));
        }
        catch (winrt::hresult_error const& e)
        {
            return e.code();
        }
        catch (...)
        {
            return winrt::to_hresult();
        }
    }

    HRESULT PreviewHandler::LoadFromBytes(std::vector<uint8_t>&& bytes) noexcept
    {
        m_bytes = std::move(bytes);
        return S_OK;
    }

    HRESULT __stdcall PreviewHandler::SetWindow(HWND window, const RECT* rect) noexcept
    {
        std::lock_guard guard{ m_lock };
        m_hostWindow = window;
        if (rect != nullptr)
        {
            m_bounds = *rect;
            m_hasBounds = true;
        }

        if (rect != nullptr)
        {
            DiagLog(L"SetWindow(0x%p, rect=%d,%d,%d,%d)",
                window, rect->left, rect->top, rect->right, rect->bottom);
        }
        else
        {
            DiagLog(L"SetWindow(0x%p, rect=null)", window);
        }
        return S_OK;
    }

    HRESULT __stdcall PreviewHandler::SetRect(const RECT* rect) noexcept
    {
        if (rect == nullptr)
        {
            return E_POINTER;
        }

        RECT bounds{};
        bool previewing = false;
        {
            std::lock_guard guard{ m_lock };
            m_bounds = *rect;
            m_hasBounds = true;
            bounds = *rect;
            previewing = m_previewing;
        }

        DiagLog(L"SetRect(%d,%d,%d,%d) previewing=%d",
            rect->left, rect->top, rect->right, rect->bottom, previewing ? 1 : 0);

        if (previewing)
        {
            return m_island.Resize(bounds);
        }
        return S_OK;
    }

    HRESULT __stdcall PreviewHandler::DoPreview() noexcept
    {
        try
        {
            std::wstring displayName;
            std::string utf8;
            HWND host = nullptr;
            RECT bounds{};

            {
                std::lock_guard guard{ m_lock };
                if (!m_loaded)
                {
                    return E_UNEXPECTED;
                }
                if (m_hostWindow == nullptr)
                {
                    return E_UNEXPECTED;
                }

                host = m_hostWindow;
                bounds = EffectiveBounds();
                displayName = m_displayName;
                utf8 = TranscodeToUtf8(m_bytes);
            }

            HRESULT hr = m_island.Create(host, bounds);
            DiagLogResult(L"DoPreview/CreateIsland", hr);
            if (FAILED(hr))
            {
                return hr;
            }

            hr = m_island.SetText(displayName, utf8);
            DiagLogResult(L"DoPreview/SetText", hr);

            PushVisuals();

            {
                std::lock_guard guard{ m_lock };
                m_previewing = true;
            }
            return S_OK;
        }
        catch (winrt::hresult_error const& e)
        {
            return e.code();
        }
        catch (...)
        {
            return winrt::to_hresult();
        }
    }

    HRESULT __stdcall PreviewHandler::Unload() noexcept
    {
        DiagLog(L"Unload");
        m_island.Destroy();

        std::lock_guard guard{ m_lock };
        m_previewing = false;
        return S_OK;
    }

    HRESULT __stdcall PreviewHandler::SetFocus() noexcept
    {
        if (m_island.Window() == nullptr)
        {
            return S_FALSE;
        }

        return m_island.Focus();
    }

    HRESULT __stdcall PreviewHandler::QueryFocus(HWND* window) noexcept
    {
        if (window == nullptr)
        {
            return E_POINTER;
        }

        *window = ::GetFocus();
        return S_OK;
    }

    HRESULT __stdcall PreviewHandler::TranslateAccelerator(MSG* message) noexcept
    {
        if (message == nullptr)
        {
            return E_POINTER;
        }

        return IslandPreTranslateMessage(*message) ? S_OK : S_FALSE;
    }

    HRESULT __stdcall PreviewHandler::SetSite(IUnknown* site) noexcept
    {
        std::lock_guard guard{ m_lock };

        if (m_site != nullptr)
        {
            m_site->Release();
        }
        m_site = site;
        if (m_site != nullptr)
        {
            m_site->AddRef();
        }
        return S_OK;
    }

    HRESULT __stdcall PreviewHandler::GetSite(REFIID riid, void** site) noexcept
    {
        if (site == nullptr)
        {
            return E_POINTER;
        }
        *site = nullptr;

        std::lock_guard guard{ m_lock };
        if (m_site == nullptr)
        {
            return E_FAIL;
        }
        return m_site->QueryInterface(riid, site);
    }

    HRESULT __stdcall PreviewHandler::GetWindow(HWND* window) noexcept
    {
        if (window == nullptr)
        {
            return E_POINTER;
        }

        HWND island = m_island.Window();
        if (island == nullptr)
        {
            std::lock_guard guard{ m_lock };
            island = m_hostWindow;
        }

        *window = island;
        return S_OK;
    }

    HRESULT __stdcall PreviewHandler::ContextSensitiveHelp(BOOL) noexcept
    {
        return S_OK;
    }

    HRESULT __stdcall PreviewHandler::SetBackgroundColor(COLORREF color) noexcept
    {
        {
            std::lock_guard guard{ m_lock };
            m_background = static_cast<uint32_t>(color);
            m_useHostColors = true;
        }

        DiagLog(L"IPreviewHandlerVisuals::SetBackgroundColor(0x%06X)", static_cast<unsigned int>(color));
        PushVisuals();
        return S_OK;
    }

    HRESULT __stdcall PreviewHandler::SetTextColor(COLORREF color) noexcept
    {
        {
            std::lock_guard guard{ m_lock };
            m_text = static_cast<uint32_t>(color);
            m_useHostColors = true;
        }

        DiagLog(L"IPreviewHandlerVisuals::SetTextColor(0x%06X)", static_cast<unsigned int>(color));
        PushVisuals();
        return S_OK;
    }

    HRESULT __stdcall PreviewHandler::SetFont(const LOGFONTW* font) noexcept
    {
        if (font == nullptr)
        {
            return E_POINTER;
        }

        try
        {
            {
                std::lock_guard guard{ m_lock };
                m_fontFamily = font->lfFaceName;

                // lfHeight is negative for a character height in logical units;
                // convert to points so the content library can use it directly.
                if (font->lfHeight != 0)
                {
                    // lfHeight is in logical units; convert to points.
                    const UINT dpi = (m_hostWindow != nullptr) ? ::GetDpiForWindow(m_hostWindow) : 96;
                    m_fontSize = static_cast<float>(std::abs(font->lfHeight) * 72.0 / (dpi > 0 ? dpi : 96));
                }
            }

            PushVisuals();
            return S_OK;
        }
        catch (winrt::hresult_error const& e)
        {
            return e.code();
        }
        catch (...)
        {
            return winrt::to_hresult();
        }
    }

    void PreviewHandler::PushVisuals() noexcept
    {
        int32_t theme = kTheme_Default;
        bool useHostColors = false;
        uint32_t background = 0;
        uint32_t text = 0;
        std::wstring fontFamily;
        float fontSize = 0.0f;

        {
            std::lock_guard guard{ m_lock };
            useHostColors = m_useHostColors;
            background = m_background;
            text = m_text;
            fontFamily = m_fontFamily;
            fontSize = m_fontSize;
        }

        DiagLog(L"PushVisuals: theme=%d useHostColors=%d background=0x%06X text=0x%06X font='%s' size=%.1f",
            theme, useHostColors ? 1 : 0, background, text, fontFamily.c_str(), fontSize);

        {
            DWORD light = 1;
            DWORD size = sizeof(light);
            const LSTATUS status = ::RegGetValueW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
            DiagLog(L"  AppsUseLightTheme: status=%ld value=%lu", status, light);
        }

        const HRESULT hr = m_island.SetVisuals(theme, useHostColors, background, text, fontFamily, fontSize);
        if (FAILED(hr) && (hr != S_FALSE) && (hr != E_NOT_VALID_STATE))
        {
            DiagLogResult(L"SetIslandVisuals", hr);
        }
    }

    RECT PreviewHandler::EffectiveBounds() const
    {
        if (m_hasBounds)
        {
            return m_bounds;
        }

        RECT client{};
        if (m_hostWindow != nullptr)
        {
            ::GetClientRect(m_hostWindow, &client);
        }
        return client;
    }
}
