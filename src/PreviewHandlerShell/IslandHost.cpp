#include "pch.h"
#include "IslandHost.h"
#include "AppSdkBootstrap.h"
#include "Diag.h"
#include "MessageHook.h"
#include "ShellPaths.h"
#include "XamlEnvironment.h"
#include "XamlThread.h"

#include <winrt/Microsoft.UI.Interop.h>

#include "..\Shared\PreviewInterop.h"

using namespace winrt;

namespace PlaylistPreview::Shell
{
    namespace
    {
        constexpr wchar_t kIslandWindowClass[] = L"PlaylistPreview.IslandHost";

        // InvokeOnXamlThread reports S_FALSE when it had to release the calling
        // thread before the XAML work finished (see XamlThread.cpp: focus
        // navigation and content teardown can need that thread to keep pumping).
        // The work is queued and will complete, so there is nothing to report;
        // only a real transport failure or the task's own HRESULT matters.
        HRESULT TransportResult(HRESULT transport, HRESULT result) noexcept
        {
            if (transport == S_FALSE)
            {
                return S_OK;
            }
            return FAILED(transport) ? transport : result;
        }

        struct UiLibrary
        {
            HMODULE module{ nullptr };
            PFN_PPUI_CreatePreviewRoot createRoot{ nullptr };
            PFN_PPUI_SetText setText{ nullptr };
            PFN_PPUI_SetVisuals setVisuals{ nullptr };
            PFN_PPUI_ReleasePreviewRoot releaseRoot{ nullptr };
        };

        struct IslandState
        {
            // Each preview handler instance owns one of these; several can be
            // alive at once because Explorer reuses a single prevhost process
            // for every preview pane.
            std::mutex lock;

            bool active{ false };
            HWND parent{ nullptr };
            HWND child{ nullptr };
            RECT bounds{};
            winrt::Microsoft::UI::Xaml::Hosting::DesktopWindowXamlSource xamlSource{ nullptr };
            winrt::Microsoft::UI::Xaml::UIElement content{ nullptr };

            // Explorer calls DoPreview before it shows a freshly reopened preview
            // pane.  Building the island into a hidden window leaves WinUI's
            // composition unconnected (the island window and its site bridge end
            // up visible and correctly sized, but nothing is ever painted), so the
            // creation is deferred until the host window actually shows.
            struct Pending
            {
                bool active{ false };
                RECT bounds{};
                bool hasText{ false };
                std::wstring displayName;
                std::string text;
            } pending;

            // Visuals are remembered so they can be (re)applied whenever the
            // island or the content appears.
            int32_t theme{ kTheme_Light };
            bool useHostColors{ false };
            uint32_t hostBackground{ 0 };
            uint32_t hostText{ 0 };
            std::wstring fontFamily;
            float fontSize{ 0.0f };
        };

        UiLibrary g_ui;

        // Registry of live islands, used both for deferred creations and for the
        // teardown ordering at process exit.  Weak references, so a handler that
        // goes away cannot leave a dangling entry behind.
        std::mutex g_islandsLock;
        std::vector<std::weak_ptr<IslandState>> g_islands;

        void TrackIsland(std::shared_ptr<IslandState> const& island)
        {
            std::lock_guard guard{ g_islandsLock };
            g_islands.push_back(island);
        }

        std::vector<std::shared_ptr<IslandState>> SnapshotIslands()
        {
            std::lock_guard guard{ g_islandsLock };
            std::vector<std::shared_ptr<IslandState>> result;
            g_islands.erase(
                std::remove_if(g_islands.begin(), g_islands.end(),
                    [](std::weak_ptr<IslandState> const& entry) { return entry.expired(); }),
                g_islands.end());

            for (auto const& entry : g_islands)
            {
                if (auto island = entry.lock())
                {
                    result.push_back(island);
                }
            }
            return result;
        }

        bool HostAcceptsIsland(HWND host)
        {
            return (host != nullptr) &&
                   (::IsWindow(host) != FALSE) &&
                   (::IsWindowVisible(host) != FALSE) &&
                   (::IsIconic(host) == FALSE);
        }

        HRESULT LoadUiLibrary()
        {
            if (g_ui.module != nullptr)
            {
                return S_OK;
            }

            // Load from our own directory explicitly: when this DLL is loaded
            // by prevhost.exe the process search path does not include us.
            const std::wstring fullPath = ShellModuleSibling(kPPUI_DllName);
            if (fullPath.empty())
            {
                return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
            }

            HMODULE module = ::LoadLibraryExW(fullPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (module == nullptr)
            {
                return HRESULT_FROM_WIN32(::GetLastError());
            }

            auto createRoot = reinterpret_cast<PFN_PPUI_CreatePreviewRoot>(::GetProcAddress(module, kPPUI_Export_CreatePreviewRoot));
            auto setText = reinterpret_cast<PFN_PPUI_SetText>(::GetProcAddress(module, kPPUI_Export_SetText));
            auto setVisuals = reinterpret_cast<PFN_PPUI_SetVisuals>(::GetProcAddress(module, kPPUI_Export_SetVisuals));
            auto releaseRoot = reinterpret_cast<PFN_PPUI_ReleasePreviewRoot>(::GetProcAddress(module, kPPUI_Export_ReleasePreviewRoot));
            if ((createRoot == nullptr) || (setText == nullptr) || (setVisuals == nullptr) || (releaseRoot == nullptr))
            {
                ::FreeLibrary(module);
                return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
            }

            // The module stays loaded for the lifetime of the process; content
            // objects handed to XAML outlive individual islands.
            g_ui = UiLibrary{ module, createRoot, setText, setVisuals, releaseRoot };
            return S_OK;
        }

        // Posted to the island window when its DPI (or its parent's) changes.
        // Doing the work from a posted message keeps it off the code paths that
        // hold state.lock (window creation and SetParent both run under it).
        constexpr UINT kApplyBoundsMessage = WM_APP + 0x100;

        void ApplyBoundsForCurrentDpi(IslandState& state);

        // The island host window keeps the XAML child site inside the bounds we
        // were given and gives us an HWND for focus and DPI work.
        LRESULT CALLBACK IslandWndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            switch (message)
            {
            case WM_DPICHANGED:
#ifdef WM_DPICHANGED_AFTERPARENT
            case WM_DPICHANGED_AFTERPARENT:
#endif
                // The pane moved to a monitor with a different scale factor; the
                // site bridge has to be told the size again so the XAML content
                // re-rasterises at the new DPI.
                ::PostMessageW(window, kApplyBoundsMessage, 0, 0);
                break;

            case kApplyBoundsMessage:
            {
                auto* state = reinterpret_cast<IslandState*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
                if (state != nullptr)
                {
                    ApplyBoundsForCurrentDpi(*state);
                }
                return 0;
            }

            default:
                break;
            }

            return ::DefWindowProcW(window, message, wparam, lparam);
        }

        HRESULT EnsureIslandWindowClass()
        {
            static bool registered = false;
            if (registered)
            {
                return S_OK;
            }

            WNDCLASSEXW description{};
            description.cbSize = sizeof(description);
            description.style = CS_HREDRAW | CS_VREDRAW;
            description.lpfnWndProc = IslandWndProc;
            description.hInstance = ShellModuleHandle();
            description.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
            description.lpszClassName = kIslandWindowClass;

            if (::RegisterClassExW(&description) == 0)
            {
                const DWORD error = ::GetLastError();
                if (error != ERROR_CLASS_ALREADY_EXISTS)
                {
                    return HRESULT_FROM_WIN32(error);
                }
            }

            registered = true;
            return S_OK;
        }

        HRESULT ApplyBoundsLocked(IslandState& state, const RECT& bounds)
        {
        if (!state.active)
        {
            return S_FALSE;
        }

        const int32_t width = bounds.right - bounds.left;
        const int32_t height = bounds.bottom - bounds.top;
        if ((width <= 0) || (height <= 0))
        {
            // Explorer sometimes reports an empty rect before the pane has been
            // laid out.  Keep the island alive; SetRect will follow.
            DiagLog(L"ApplyBoundsLocked: ignoring empty rect (%d, %d, %d, %d)",
                bounds.left, bounds.top, bounds.right, bounds.bottom);
            return S_FALSE;
        }

            ::SetWindowPos(
                state.child,
                nullptr,
                bounds.left,
                bounds.top,
                width,
                height,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

            // XAML content is hosted in its own child window inside ours; the
            // site bridge is what actually sizes it.
            state.xamlSource.SiteBridge().MoveAndResize(winrt::Windows::Graphics::RectInt32{ 0, 0, width, height });
            state.bounds = bounds;
            return S_OK;
        }

        void ApplyBoundsForCurrentDpi(IslandState& state)
        {
            std::lock_guard guard{ state.lock };
            if (!state.active)
            {
                return;
            }

            const UINT dpi = (state.child != nullptr) ? ::GetDpiForWindow(state.child) : 0;
            DiagLog(L"  DPI changed: re-applying island bounds (dpi=%u, %dx%d)",
                dpi,
                state.bounds.right - state.bounds.left,
                state.bounds.bottom - state.bounds.top);

            ApplyBoundsLocked(state, state.bounds);
        }

        HRESULT ApplyVisualsLocked(IslandState& state)
        {
            if (!state.active || (g_ui.setVisuals == nullptr))
            {
                return S_FALSE;
            }

            return g_ui.setVisuals(
                static_cast<IInspectable*>(winrt::get_abi(state.content)),
                state.theme,
                state.useHostColors ? 1 : 0,
                state.hostBackground,
                state.hostText,
                state.fontFamily.c_str(),
                state.fontSize);
        }

        // Caller must hold state.lock.
        void DestroyIslandLocked(IslandState& state) noexcept
        {
            // A deferred creation counts as work to cancel even though no island
            // exists yet: Unload must not let an island appear afterwards.
            if (!state.active && (state.child == nullptr) && !state.pending.active)
            {
                return;
            }

            RemoveMessageHook();

            IInspectable* contentKey = nullptr;
            try
            {
                if (state.xamlSource != nullptr)
                {
                    state.xamlSource.Content(nullptr);
                    state.xamlSource = nullptr;
                }
                if (state.content != nullptr)
                {
                    contentKey = static_cast<IInspectable*>(winrt::get_abi(state.content));
                }
                state.content = nullptr;
            }
            catch (...)
            {
            }

            if (g_ui.releaseRoot != nullptr)
            {
                g_ui.releaseRoot(contentKey);
            }

            if (state.child != nullptr)
            {
                ::DestroyWindow(state.child);
            }

            const int32_t theme = state.theme;
            const bool useHostColors = state.useHostColors;
            const uint32_t background = state.hostBackground;
            const uint32_t text = state.hostText;
            const std::wstring fontFamily = state.fontFamily;
            const float fontSize = state.fontSize;

            // Clear the transient state but keep the mutex (it is not assignable)
            // and the remembered visuals.  Clearing `pending` also cancels a
            // deferred creation: Unload must not let an island appear afterwards.
            state.active = false;
            state.parent = nullptr;
            state.child = nullptr;
            state.bounds = RECT{};
            state.xamlSource = nullptr;
            state.content = nullptr;
            state.pending = IslandState::Pending{};

            state.theme = theme;
            state.useHostColors = useHostColors;
            state.hostBackground = background;
            state.hostText = text;
            state.fontFamily = fontFamily;
            state.fontSize = fontSize;
        }
    }

    // --- implementations: all of these must run on the XAML thread ---

    // Defined below; declared here so the deferred-creation path can use it.
    static HRESULT SetIslandTextLocked(IslandState& state, std::wstring const& displayName, std::string const& utf8Text);

    // Caller must hold state.lock and run on the XAML thread.
    static HRESULT CreateIslandLocked(IslandState& state, HWND parent, const RECT& bounds)
    {
        if (parent == nullptr)
        {
            return E_INVALIDARG;
        }

        if (state.active)
        {
            return S_FALSE;
        }

        // The XAML thread owns the App SDK bootstrap and the XAML environment;
        // if either had failed we would never be scheduled here.
        if (!XamlEnvironmentReady())
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_READY);
        }

        HRESULT hr = S_OK;

        hr = LoadUiLibrary();
        DiagLogResult(L"CreateIsland/LoadUiLibrary", hr);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = EnsureIslandWindowClass();
        DiagLogResult(L"CreateIsland/EnsureIslandWindowClass", hr);
        if (FAILED(hr))
        {
            return hr;
        }

        // Explorer can call DoPreview before the preview pane has been laid out,
        // so an empty rect must not abort the island; fall back to the host's
        // client area (and then to a sane default) and let SetRect fix it later.
        RECT effective = bounds;
        if ((effective.right - effective.left <= 0) || (effective.bottom - effective.top <= 0))
        {
            RECT client{};
            if (::GetClientRect(parent, &client) &&
                ((client.right - client.left > 0) && (client.bottom - client.top > 0)))
            {
                effective = client;
            }
            else
            {
                effective = RECT{ 0, 0, 800, 600 };
            }

            DiagLog(L"  empty preview rect; using (%d, %d, %d, %d) instead",
                effective.left, effective.top, effective.right, effective.bottom);
        }

        const int32_t width = effective.right - effective.left;
        const int32_t height = effective.bottom - effective.top;

        DWORD hostProcessId = 0;
        const DWORD hostThreadId = ::GetWindowThreadProcessId(parent, &hostProcessId);
        const bool crossProcess = hostProcessId != ::GetCurrentProcessId();

        DiagLog(L"  host window: %p isWindow=%d thread=%u process=%u ours=%d",
            parent, ::IsWindow(parent) ? 1 : 0, hostThreadId, hostProcessId, crossProcess ? 0 : 1);

        // Always start as a standalone top-level window: the island hosting API
        // requires the window (and its ancestors) to live on this thread, and
        // creating it as a child of the host up front deadlocks whenever the
        // host window belongs to another thread -- window creation sends
        // messages to the parent's thread, which is blocked inside DoPreview
        // waiting for us.  It becomes a child below, after the island exists.
        HWND child = ::CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kIslandWindowClass,
            L"",
            WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0,
            0,
            width,
            height,
            nullptr,
            nullptr,
            ShellModuleHandle(),
            nullptr);

        if (child == nullptr)
        {
            return HRESULT_FROM_WIN32(::GetLastError());
        }

        try
        {
            // Island infrastructure first, then control styles, then content:
            // XamlControlsResources only resolves once the framework is fully up.
            state.xamlSource = winrt::Microsoft::UI::Xaml::Hosting::DesktopWindowXamlSource{};
            DiagLog(L"  DesktopWindowXamlSource created");

            try
            {
                state.xamlSource = winrt::Microsoft::UI::Xaml::Hosting::DesktopWindowXamlSource{};
                state.xamlSource.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(child));
                DiagLog(L"  Initialize(wrapper window): ok");
            }
            catch (winrt::hresult_error const& e)
            {
                DiagLogResult(L"Initialize(wrapper window)", e.code());
                DiagLog(L"    message: %s", e.message().c_str());
                DestroyIslandLocked(state);
                return e.code();
            }

                DiagLog(L"  before SetParent: parent=%p style=0x%08X",
                    ::GetParent(child),
                    static_cast<unsigned int>(::GetWindowLongW(child, GWL_STYLE)));

                // Order matters, and this is the recipe PowerToys' preview
                // handlers use: switch the window to WS_CHILD *first*, then
                // re-parent.  Setting the style afterwards would only attach the
                // window to the desktop and leave it floating.
                LONG_PTR style = ::GetWindowLongPtrW(child, GWL_STYLE);
                style &= ~static_cast<LONG_PTR>(WS_POPUP);
                style |= WS_CHILD;
                ::SetWindowLongPtrW(child, GWL_STYLE, style);

                ::SetLastError(ERROR_SUCCESS);
                HWND previousParent = ::SetParent(child, parent);
                const DWORD setParentError = ::GetLastError();

                const BOOL isChild = ::IsChild(parent, child);
                DiagLog(L"  after SetParent: previous=%p error=%u parent=%p isChild=%d",
                    previousParent, setParentError, ::GetParent(child), isChild);
                if (!isChild)
                {
                    DiagLogResult(L"SetParent(wrapper, host) -- continuing anyway",
                        HRESULT_FROM_WIN32(setParentError == ERROR_SUCCESS ? E_FAIL : setParentError));
                }

            EnsureControlsResources();

            IInspectable* rawContent = nullptr;
            hr = g_ui.createRoot(&rawContent);
            DiagLogResult(L"CreateIsland/createRoot", hr);
            if (FAILED(hr))
            {
                DestroyIslandLocked(state);
                return hr;
            }

            state.content = winrt::Microsoft::UI::Xaml::UIElement{ rawContent, winrt::take_ownership_from_abi };

            state.xamlSource.Content(state.content);
            DiagLog(L"  DesktopWindowXamlSource.Content: ok");

            state.active = true;
            state.parent = parent;
            state.child = child;

            hr = ApplyBoundsLocked(state, effective);
            if (FAILED(hr))
            {
                DestroyIslandLocked(state);
                return hr;
            }

            ApplyVisualsLocked(state);

            if (!InstallMessageHook())
            {
                DiagLog(L"  message hook failed; keyboard input will not reach the island");
            }
        }
        catch (winrt::hresult_error const& e)
        {
            hr = e.code();
            DestroyIslandLocked(state);
            return hr;
        }

        return S_OK;
    }

    static HRESULT CreateIslandOnXamlThread(std::shared_ptr<IslandState> const& island, HWND parent, const RECT& bounds)
    {
        IslandState& state = *island;

        if (parent == nullptr)
        {
            return E_INVALIDARG;
        }

        TrackIsland(island);

        std::lock_guard guard{ state.lock };
        if (state.active)
        {
            return S_FALSE;
        }

        if (!HostAcceptsIsland(parent))
        {
            state.parent = parent;
            state.pending.active = true;
            state.pending.bounds = bounds;
            DiagLog(L"  host window is not visible yet; deferring island creation");
            return S_OK;
        }

        return CreateIslandLocked(state, parent, bounds);
    }

    // Caller must hold state.lock and run on the XAML thread.
    static void CompleteDeferredCreationLocked(std::shared_ptr<IslandState> const& island)
    {
        IslandState& state = *island;
        if (!state.pending.active)
        {
            return;
        }

        if (state.active)
        {
            state.pending = IslandState::Pending{};
            return;
        }

        if (!HostAcceptsIsland(state.parent))
        {
            return;
        }

        const IslandState::Pending pending = state.pending;
        state.pending = IslandState::Pending{};

        DiagLog(L"  host window is visible now; creating the deferred island");
        const HRESULT created = CreateIslandLocked(state, state.parent, pending.bounds);
        DiagLogResult(L"deferred CreateIsland", created);

        if (SUCCEEDED(created) && pending.hasText)
        {
            DiagLogResult(L"deferred SetText", SetIslandTextLocked(state, pending.displayName, pending.text));
        }
    }

    void ProcessDeferredIslandCreation()
    {
        for (auto const& island : SnapshotIslands())
        {
            std::lock_guard guard{ island->lock };
            CompleteDeferredCreationLocked(island);
        }
    }

    void RefreshIslandVisuals()
    {
        // Cheap when nothing changed: the content library compares the visuals
        // it was given (and the Windows theme it resolves itself) against what it
        // already painted.
        for (auto const& island : SnapshotIslands())
        {
            std::lock_guard guard{ island->lock };
            ApplyVisualsLocked(*island);
        }
    }

    void DestroyAllIslands() noexcept
    {
        // Same ordering the SDK documents: release every island before the XAML
        // manager and the dispatcher queue go away.  Runs on the XAML thread.
        for (auto const& island : SnapshotIslands())
        {
            std::lock_guard guard{ island->lock };
            DestroyIslandLocked(*island);
        }
    }

    static HRESULT ResizeIslandOnXamlThread(std::shared_ptr<IslandState> const& island, const RECT& bounds)
    {
        IslandState& state = *island;
        std::lock_guard guard{ state.lock };
        return ApplyBoundsLocked(state, bounds);
    }

    // Caller must hold state.lock and run on the XAML thread.
    static HRESULT SetIslandTextLocked(IslandState& state, std::wstring const& displayName, std::string const& utf8Text)
    {
        if (state.active && (g_ui.setText != nullptr))
        {
            return g_ui.setText(
                static_cast<IInspectable*>(winrt::get_abi(state.content)),
                displayName.c_str(),
                utf8Text.data(),
                static_cast<uint32_t>(utf8Text.size()));
        }

        if (state.pending.active)
        {
            // The island has not been created yet because the host window was
            // still hidden; keep the content for ProcessDeferredIslandCreation.
            state.pending.hasText = true;
            state.pending.displayName = displayName;
            state.pending.text = utf8Text;
            return S_OK;
        }

        return E_NOT_VALID_STATE;
    }

    static HRESULT SetIslandTextOnXamlThread(std::shared_ptr<IslandState> const& island, std::wstring const& displayName, std::string const& utf8Text)
    {
        IslandState& state = *island;
        std::lock_guard guard{ state.lock };
        return SetIslandTextLocked(state, displayName, utf8Text);
    }

    static HRESULT SetIslandVisualsOnXamlThread(std::shared_ptr<IslandState> const& island,
        int32_t theme,
        bool useHostColors,
        uint32_t backgroundRef,
        uint32_t textRef,
        std::wstring const& fontFamily,
        float fontSize)
    {
        IslandState& state = *island;
        std::lock_guard guard{ state.lock };

        state.theme = theme;
        state.useHostColors = useHostColors;
        state.hostBackground = backgroundRef;
        state.hostText = textRef;
        state.fontFamily = fontFamily;
        state.fontSize = fontSize;

        return ApplyVisualsLocked(state);
    }

    static HRESULT FocusIslandOnXamlThread(std::shared_ptr<IslandState> const& island) noexcept
    {
        IslandState& state = *island;
        std::lock_guard guard{ state.lock };
        if (!state.active || (state.xamlSource == nullptr))
        {
            return S_FALSE;
        }

        try
        {
            using namespace winrt::Microsoft::UI::Xaml::Hosting;

            // Focusing the wrapper HWND is not enough: keyboard input only
            // reaches the island once XAML owns focus, which is what
            // NavigateFocus does.
            const XamlSourceFocusNavigationRequest request{ XamlSourceFocusNavigationReason::Programmatic };
            const XamlSourceFocusNavigationResult result = state.xamlSource.NavigateFocus(request);
            DiagLog(L"FocusIsland: moved=%d", result.WasFocusMoved() ? 1 : 0);
            return result.WasFocusMoved() ? S_OK : S_FALSE;
        }
        catch (winrt::hresult_error const& e)
        {
            DiagLogResult(L"FocusIsland/NavigateFocus", e.code());
            return e.code();
        }
    }

    bool IslandPreTranslateMessage(const MSG& message)
    {
        return PreTranslateMessage(message);
    }

    static void DestroyIslandOnXamlThread(std::shared_ptr<IslandState> const& island) noexcept
    {
        IslandState& state = *island;
        std::lock_guard guard{ state.lock };
        DestroyIslandLocked(state);
    }

    // --- Island ----------------------------------------------------------

    struct Island::Impl : IslandState
    {
    };

    Island::Island() :
        m_impl(std::make_shared<Impl>())
    {
    }

    Island::~Island()
    {
        Destroy();
    }

    HRESULT Island::Create(HWND parent, const RECT& bounds)
    {
        auto island = m_impl;
        HRESULT result = E_FAIL;
        const HRESULT transport = InvokeOnXamlThread([&] { result = CreateIslandOnXamlThread(island, parent, bounds); });
        return TransportResult(transport, result);
    }

    HRESULT Island::Resize(const RECT& bounds)
    {
        auto island = m_impl;
        HRESULT result = E_FAIL;
        const HRESULT transport = InvokeOnXamlThread([&] { result = ResizeIslandOnXamlThread(island, bounds); });
        return TransportResult(transport, result);
    }

    HRESULT Island::SetText(std::wstring const& displayName, std::string const& utf8Text)
    {
        auto island = m_impl;
        HRESULT result = E_FAIL;
        const HRESULT transport = InvokeOnXamlThread([&] { result = SetIslandTextOnXamlThread(island, displayName, utf8Text); });
        return TransportResult(transport, result);
    }

    HRESULT Island::SetVisuals(
        int32_t theme,
        bool useHostColors,
        uint32_t backgroundRef,
        uint32_t textRef,
        std::wstring const& fontFamily,
        float fontSize)
    {
        auto island = m_impl;
        HRESULT result = E_FAIL;
        const HRESULT transport = InvokeOnXamlThread([&] {
            result = SetIslandVisualsOnXamlThread(island, theme, useHostColors, backgroundRef, textRef, fontFamily, fontSize);
        });
        return TransportResult(transport, result);
    }

    HRESULT Island::Focus() noexcept
    {
        auto island = m_impl;
        HRESULT result = E_FAIL;
        const HRESULT transport = InvokeOnXamlThread([&] { result = FocusIslandOnXamlThread(island); });
        return TransportResult(transport, result);
    }

    HWND Island::Window() const noexcept
    {
        std::lock_guard guard{ m_impl->lock };
        return m_impl->child;
    }

    bool Island::Active() const noexcept
    {
        std::lock_guard guard{ m_impl->lock };
        return m_impl->active;
    }

    void Island::Destroy() noexcept
    {
        auto island = m_impl;
        const HRESULT hr = InvokeOnXamlThread([&] { DestroyIslandOnXamlThread(island); });
        if (FAILED(hr))
        {
            DiagLog(L"DestroyIsland: XAML thread unavailable (0x%08X)", static_cast<unsigned int>(hr));
        }
    }
}
