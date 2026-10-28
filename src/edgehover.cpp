#include "input_compat.hpp"

#include "edgehover.hpp"

#include "geometry.hpp"
#include <src/plugins/PluginSystem.hpp>
#include <dlfcn.h>
#include <wayland-server-core.h>

namespace {
bool viewflowCaptureActive() {
    if (!g_pPluginSystem) return false;
    for (const auto* plugin : g_pPluginSystem->getAllPlugins()) {
        if (!plugin || plugin->m_name != "viewflow-hyprland" || !plugin->m_handle) continue;
        // Resolve from the currently loaded handle on every call. Retaining a
        // function pointer would be unsafe across plugin unload/reload.
        using Query = bool (*)();
        const auto query = reinterpret_cast<Query>(dlsym(plugin->m_handle, "viewflow_input_capture_active_v1"));
        return query && query();
    }
    return false;
}
}


#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <protocols/wlr-layer-shell-unstable-v1.hpp>
#include <src/Compositor.hpp>
#include <src/config/ConfigValue.hpp>
#include <src/config/ConfigManager.hpp>
#include <src/workspace/HLWorkspace.hpp>
#include <src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/state/ViewState.hpp>
#include <src/desktop/state/WindowState.hpp>
#include <src/desktop/state/ViewHitTester.hpp>
#include <src/desktop/view/LayerSurface.hpp>
#include <src/desktop/view/WLSurface.hpp>
#include <src/desktop/view/window/Window.hpp>
#include <src/output/Monitor.hpp>
#include <src/state/MonitorState.hpp>
#include <src/managers/fullscreen/FullscreenController.hpp>
#include <src/helpers/time/Time.hpp>
#include <src/layout/LayoutManager.hpp>
#include <src/managers/SeatManager.hpp>
#include <src/managers/SessionLockManager.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/protocols/XDGShell.hpp>
#include <src/protocols/InputCapture.hpp>

namespace hypr_edgehover {

namespace {

Rect boxToRect(const CBox& box) {
    return {
        .x = box.x,
        .y = box.y,
        .width = box.width,
        .height = box.height,
    };
}

CBox goalBox(PHLWINDOW window) {
    if (!window)
        return {0.0, 0.0, 0.0, 0.0};

    const auto position = window->position(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    const auto size     = window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    return {position.x, position.y, size.x, size.y};
}

Rect intersectRects(const Rect& lhs, const Rect& rhs) {
    const double left   = std::max(lhs.left(), rhs.left());
    const double right  = std::min(lhs.right(), rhs.right());
    const double top    = std::max(lhs.top(), rhs.top());
    const double bottom = std::min(lhs.bottom(), rhs.bottom());

    if (right <= left || bottom <= top)
        return {.x = left, .y = top, .width = 0.0, .height = 0.0};

    return {.x = left, .y = top, .width = right - left, .height = bottom - top};
}

VisibleThickness visibleThicknessInMonitor(const Rect& monitor, const Rect& window) {
    const Rect visible = intersectRects(monitor, window);
    return {
        .left = visible.width,
        .right = visible.width,
        .top = visible.height,
        .bottom = visible.height,
    };
}

double visibleThicknessForEdge(const VisibleThickness& thickness, Edge edge) {
    switch (edge) {
        case Edge::Left: return thickness.left;
        case Edge::Right: return thickness.right;
        case Edge::Top: return thickness.top;
        case Edge::Bottom: return thickness.bottom;
    }
    return 0.0;
}

Point clampPointToRectWithInset(const Point& coords, const Rect& box, double inset) {
    const double safeInset = std::max(0.0, inset);

    auto clampAxis = [](double value, double min, double max) {
        if (min > max)
            return (min + max) / 2.0;
        return std::clamp(value, min, max);
    };

    return {
        .x = clampAxis(coords.x, box.left() + safeInset, box.right() - safeInset),
        .y = clampAxis(coords.y, box.top() + safeInset, box.bottom() - safeInset),
    };
}

Point vectorToPoint(const Vector2D& vector) {
    return {.x = vector.x, .y = vector.y};
}

Vector2D pointToVector(const Point& point) {
    return {point.x, point.y};
}

PHLWORKSPACE selectedWorkspace(PHLMONITOR monitor) {
    if (!monitor)
        return nullptr;

    return monitor->m_activeSpecialWorkspace ? monitor->m_activeSpecialWorkspace : monitor->m_activeWorkspace;
}

bool currentWorkspaceHasFullscreen(PHLMONITOR monitor) {
    if (!monitor)
        return false;

    const auto workspace = selectedWorkspace(monitor);
    return Fullscreen::controller()->hasFullscreen(monitor) || (workspace && Fullscreen::controller()->hasFullscreen(workspace));
}

bool windowBelongsToActiveWorkspace(PHLWINDOW window, PHLMONITOR monitor) {
    if (!window || !monitor || !window->m_workspace)
        return false;

    static const auto PSPECIALFALLTHROUGH = CConfigValue<Config::INTEGER>("input:special_fallthrough");
    if (monitor->m_activeSpecialWorkspace && !*PSPECIALFALLTHROUGH)
        return window->m_workspace == monitor->m_activeSpecialWorkspace;

    return window->m_workspace == monitor->m_activeWorkspace || (monitor->m_activeSpecialWorkspace && window->m_workspace == monitor->m_activeSpecialWorkspace);
}

template <typename Window>
bool shadowedByModal(const Window& window) {
    static const auto PMODALPARENTBLOCKING = CConfigValue<Config::INTEGER>("general:modal_parent_blocking");
    if (!window || !*PMODALPARENTBLOCKING)
        return false;

    if constexpr (requires { window->backend(); })
        return !window->backend().isX11() && window->backend().traits().hasModalChild;
    else
        // Released 0.56 headers expose the same Wayland state via XDG.
        return !window->m_isX11 && window->m_xdgSurface && window->m_xdgSurface->m_toplevel && window->m_xdgSurface->m_toplevel->anyChildModal();
}

bool noFocus(PHLWINDOW window) {
    return window && window->m_ruleApplicator && window->m_ruleApplicator->noFocus().valueOrDefault();
}

bool noFollowMouse(PHLWINDOW window) {
    return window && window->m_ruleApplicator && window->m_ruleApplicator->noFollowMouse().valueOrDefault();
}

bool confinePointer(PHLWINDOW window) {
    return window && window->m_ruleApplicator && window->m_ruleApplicator->confinePointer().valueOrDefault();
}

std::string trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return std::string(value);
}

std::string lowerTrimmed(std::string_view value) {
    std::string lowered = trim(value);
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

std::vector<std::string> parseCommaList(std::string_view value, bool lowerCase) {
    std::vector<std::string> result;

    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const auto        part  = value.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        std::string       item  = lowerCase ? lowerTrimmed(part) : trim(part);
        if (!item.empty())
            result.push_back(std::move(item));

        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }

    return result;
}

PassSet parsePassSet(std::string_view value) {
    PassSet pass;
    for (const auto& item : parseCommaList(value, true)) {
        if (item == "hover")
            pass.hover = true;
        else if (item == "click")
            pass.click = true;
        else if (item == "scroll")
            pass.scroll = true;
        else if (item == "keyboard")
            pass.keyboard = true;
    }
    return pass;
}

bool rawConfigChanged(bool initialized, const std::string& cached, std::string_view value) {
    return !initialized || cached.size() != value.size() || !std::equal(cached.begin(), cached.end(), value.begin());
}

const PassSet& cachedPassSet(std::string_view value, PassSetCache& cache) {
    if (rawConfigChanged(cache.initialized, cache.raw, value)) {
        cache.raw         = std::string(value);
        cache.parsed      = parsePassSet(cache.raw);
        cache.initialized = true;
    }

    return cache.parsed;
}

const std::vector<std::string>& cachedStringList(std::string_view value, bool lowerCase, StringListCache& cache) {
    if (rawConfigChanged(cache.initialized, cache.raw, value)) {
        cache.raw         = std::string(value);
        cache.parsed      = parseCommaList(cache.raw, lowerCase);
        cache.initialized = true;
    }

    return cache.parsed;
}

const ZoneRanges& cachedZones(std::string_view value, ZoneRangesCache& cache) {
    if (rawConfigChanged(cache.initialized, cache.raw, value)) {
        cache.raw         = std::string(value);
        cache.parsed      = parseZones(cache.raw);
        cache.initialized = true;
    }

    return cache.parsed;
}

PassSet passForSource(const RuntimeConfig& config, SyntheticSource source) {
    switch (source) {
        case SyntheticSource::Gap: return config.gapPass;
        case SyntheticSource::Layer: return config.layerPass;
        case SyntheticSource::Overhang: return config.overhangPass;
    }
    return config.gapPass;
}

bool layerNamespaceMatches(const RuntimeConfig& config, PHLLS layerSurface) {
    if (!layerSurface)
        return false;
    if (config.layerNamespaces.empty())
        return true;

    return std::ranges::any_of(config.layerNamespaces, [&](const std::string& name) { return name == layerSurface->m_namespace; });
}

PHLWINDOW stockWindowHit(const Vector2D& coords) {
    constexpr uint16_t flags = Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING | Desktop::View::FOLLOW_MOUSE_CHECK;
    return Desktop::viewState() ? Desktop::viewState()->hitTest().windowAt(coords, flags) : nullptr;
}

PHLLS stockUpperLayerHit(const Vector2D& coords, PHLMONITOR monitor) {
    if (!g_pCompositor || !monitor)
        return nullptr;

    Vector2D layerLocal;
    PHLLS    layerSurface = nullptr;

    if (Desktop::viewState()->hitTest().layerPopupSurfaceAt(coords, monitor, &layerLocal, &layerSurface))
        return layerSurface;

    constexpr std::array layers = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    };

    for (const auto layer : layers) {
        if (Desktop::viewState()->hitTest().layerSurfaceAt(coords, &monitor->m_layerSurfaceLayers[layer], &layerLocal, &layerSurface))
            return layerSurface;
    }

    return nullptr;
}

PHLLS stockLowerLayerHit(const Vector2D& coords, PHLMONITOR monitor) {
    if (!g_pCompositor || !monitor)
        return nullptr;

    Vector2D layerLocal;
    PHLLS    layerSurface = nullptr;

    constexpr std::array layers = {
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    };

    for (const auto layer : layers) {
        if (Desktop::viewState()->hitTest().layerSurfaceAt(coords, &monitor->m_layerSurfaceLayers[layer], &layerLocal, &layerSurface))
            return layerSurface;
    }

    return nullptr;
}

bool forcedFocusActive() {
    PHLWINDOW forcedFocus;

    if (g_pInputManager)
        forcedFocus = g_pInputManager->m_forcedFocus.lock();
    if (!forcedFocus && Desktop::viewState())
        forcedFocus = Desktop::viewState()->query().forceFocus().runWindow();

    return static_cast<bool>(forcedFocus);
}

bool focusedWindowConfinesPointer() {
    if (!Desktop::focusState())
        return false;

    PHLWINDOW window;

    const auto focusedSurface = Desktop::focusState()->surface();
    if (focusedSurface) {
        const auto hlSurface = Desktop::View::CWLSurface::fromResource(focusedSurface);
        if (hlSurface)
            window = Desktop::View::CWindow::fromView(hlSurface->view());
    }

    if (!window)
        window = Desktop::focusState()->window();

    return confinePointer(window);
}

bool shouldFocusKeyboard(const RuntimeConfig& config, PHLWINDOW window) {
    if (!window || noFollowMouse(window))
        return false;

    if (Desktop::focusState() && Desktop::focusState()->window() == window)
        return false;

    if (config.keyboardFocus == 0)
        return false;
    if (config.keyboardFocus > 0)
        return true;

    return config.followMouse == 1;
}

} // namespace

EdgeHover::EdgeHover(HANDLE handle) : m_handle(handle) {}

EdgeHover::~EdgeHover() {
    if (m_buttonBypassIdle)
        wl_event_source_remove(m_buttonBypassIdle);
}

bool EdgeHover::initialize() {
    m_mouseMoveListener = Event::bus()->m_events.input.mouse.move.listen([this](const Vector2D& coords, Event::SCallbackInfo& info) {
        handleMouseMove(coords, info);
    });
    m_mouseButtonListener = Event::bus()->m_events.input.mouse.button.listen([this](const IPointer::SButtonEvent& event, Event::SCallbackInfo& info) {
        handleMouseButton(event, info);
    });
    m_mouseAxisListener = Event::bus()->m_events.input.mouse.axis.listen([this](const IPointer::SAxisEvent& event, Event::SCallbackInfo& info) {
        handleMouseAxis(event, info);
    });

    return static_cast<bool>(m_mouseMoveListener) && static_cast<bool>(m_mouseButtonListener) && static_cast<bool>(m_mouseAxisListener);
}

RuntimeConfig EdgeHover::readConfig() const {
    (void)m_handle;

    static const auto PENABLED       = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:enabled");
    static const auto PEDGES         = CConfigValue<std::string>("plugin:hypr_edgehover:edges");
    static const auto PINSET         = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:inset");
    static const auto PMAXDISTANCE   = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:max_distance");
    static const auto PKEYBOARDFOCUS = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:keyboard_focus");
    static const auto PGAPPASS       = CConfigValue<std::string>("plugin:hypr_edgehover:gap_pass");
    static const auto PLAYERPASS     = CConfigValue<std::string>("plugin:hypr_edgehover:layer_pass");
    static const auto PLAYERNAMES    = CConfigValue<std::string>("plugin:hypr_edgehover:layer_namespaces");
    static const auto POVERHANGPASS  = CConfigValue<std::string>("plugin:hypr_edgehover:overhang_pass");
    static const auto POVERHANGTHRESHOLD = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:overhang_threshold");
    static const auto POVERHANGEDGEWIDTH = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:overhang_edge_width");
    static const auto PSTEALEDGEWIDTH    = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:steal_edge_width");
    static const auto PZONESTOP          = CConfigValue<std::string>("plugin:hypr_edgehover:zones_top");
    static const auto PZONESBOTTOM       = CConfigValue<std::string>("plugin:hypr_edgehover:zones_bottom");
    static const auto PZONESLEFT         = CConfigValue<std::string>("plugin:hypr_edgehover:zones_left");
    static const auto PZONESRIGHT        = CConfigValue<std::string>("plugin:hypr_edgehover:zones_right");
    static const auto PFOLLOWMOUSE   = CConfigValue<Config::INTEGER>("input:follow_mouse");

    return {
        .enabled = *PENABLED != 0,
        .edges = *PEDGES,
        .inset = static_cast<double>(std::max<Config::INTEGER>(0, *PINSET)),
        .maxDistance = static_cast<double>(std::max<Config::INTEGER>(0, *PMAXDISTANCE)),
        .keyboardFocus = *PKEYBOARDFOCUS,
        .followMouse = *PFOLLOWMOUSE,
        .gapPass = cachedPassSet(*PGAPPASS, m_gapPassCache),
        .layerPass = cachedPassSet(*PLAYERPASS, m_layerPassCache),
        .layerNamespaces = cachedStringList(*PLAYERNAMES, false, m_layerNamespacesCache),
        .overhangPass = cachedPassSet(*POVERHANGPASS, m_overhangPassCache),
        .overhangThreshold = static_cast<double>(std::max<Config::INTEGER>(0, *POVERHANGTHRESHOLD)),
        .overhangEdgeWidth = static_cast<double>(std::max<Config::INTEGER>(0, *POVERHANGEDGEWIDTH)),
        .stealEdgeWidth = static_cast<double>(std::max<Config::INTEGER>(0, *PSTEALEDGEWIDTH)),
        .zones = {
            .left = cachedZones(*PZONESLEFT, m_zonesLeftCache),
            .right = cachedZones(*PZONESRIGHT, m_zonesRightCache),
            .top = cachedZones(*PZONESTOP, m_zonesTopCache),
            .bottom = cachedZones(*PZONESBOTTOM, m_zonesBottomCache),
        },
    };
}

void EdgeHover::clearSynthetic() {
    m_synthetic.active = false;
    m_synthetic.target = {};
}

void EdgeHover::clearStickButtons() {
    m_stickButtons = false;
    m_stickyButtonCodes.clear();
}

void EdgeHover::clearStickAndSynthetic() {
    clearStickButtons();
    clearSynthetic();
}

bool EdgeHover::bypassForCapture(Event::SCallbackInfo& info) {
    const bool nativeCapture = PROTO::inputCapture && PROTO::inputCapture->isCaptured();
    const bool remoteCapture = viewflowCaptureActive();
    if (!nativeCapture && !remoteCapture)
        return false;

    clearStickAndSynthetic();
    m_bypassNext = false;
    m_buttonBypass = false;
    if (m_buttonBypassIdle) {
        wl_event_source_remove(m_buttonBypassIdle);
        m_buttonBypassIdle = nullptr;
    }
    // Native capture consumes button/axis events after the event bus returns.
    // Viewflow owns its events in its own listener instead.
    if (!nativeCapture)
        info.cancelled = true;
    return true;
}

bool EdgeHover::deliverSyntheticMotion(PHLWINDOW window, const Vector2D& targetPoint, const RuntimeConfig& config, SyntheticSource source) {
    if (!window || !g_pCompositor || !g_pSeatManager || !g_pInputManager)
        return false;

    // Recheck pinned drag targets too: special/modal state can change after selection.
    if (!windowBelongsToActiveWorkspace(window, window->m_monitor.lock()) || shadowedByModal(window) || confinePointer(window))
        return false;

    const Rect valueBox = boxToRect(window->getWindowMainSurfaceBox());
    if (valueBox.width <= 0.0 || valueBox.height <= 0.0)
        return false;

    const Vector2D clampedTargetPoint = pointToVector(clampPointToRectWithInset(vectorToPoint(targetPoint), valueBox, config.inset));

    Vector2D                 surfaceLocal;
    SP<CWLSurfaceResource>   surface;

    if (window->backend().isX11()) {
        const auto windowSurface = window->wlSurface();
        if (!windowSurface)
            return false;

        surface      = windowSurface->resource();
        surfaceLocal = window->backend().surfaceLocalToBuffer(clampedTargetPoint - window->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT));
    } else {
        surface = Desktop::viewState()->hitTest().windowSurfaceAt(clampedTargetPoint, window, surfaceLocal);

        if (!surface) {
            const auto windowSurface = window->wlSurface();
            if (!windowSurface)
                return false;

            surface      = windowSurface->resource();
            surfaceLocal = clampedTargetPoint - window->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        }
    }

    if (!surface)
        return false;

    g_pInputManager->m_emptyFocusCursorSet = false;
    g_pInputManager->m_lastFocusOnLS       = false;
    g_pSeatManager->setPointerFocus(surface, surfaceLocal);
    g_pSeatManager->sendPointerMotion(Time::millis(Time::steadyNow()), surfaceLocal);

    if (passForSource(config, source).keyboard && shouldFocusKeyboard(config, window) && Desktop::focusState())
        Desktop::focusState()->rawWindowFocus(window, Desktop::FOCUS_REASON_FFM, surface);

    m_synthetic.active = true;
    m_synthetic.source = source;
    m_synthetic.target = window;

    return true;
}

void EdgeHover::handleMouseMove(const Vector2D& coords, Event::SCallbackInfo& info) {
    if (bypassForCapture(info))
        return;

    // mouseMoveUnified sets m_lastInputMouse from this invocation's mouse
    // argument before emitting move. Physical mouse input must still work when
    // m_lastInputTouch is stale; touch refocus must retain stock hit testing.
    if (g_pInputManager && g_pInputManager->m_lastInputTouch && !g_pInputManager->m_lastInputMouse) {
        clearStickAndSynthetic();
        return;
    }

    if (m_buttonBypass) {
        clearSynthetic();
        return;
    }

    if (m_bypassNext) {
        m_bypassNext = false;
        clearSynthetic();
        return;
    }

    const RuntimeConfig config = readConfig();
    if (!config.enabled || info.cancelled) {
        clearSynthetic();
        return;
    }

    if (!g_pCompositor || !g_pSeatManager || !g_pInputManager || !g_pSessionLockManager || !g_layoutManager) {
        clearSynthetic();
        return;
    }

    const auto monitor = State::monitorState()->query().vec(coords).run();
    if (!monitor) {
        clearSynthetic();
        return;
    }

    const Rect   monitorRect = {.x = monitor->m_position.x, .y = monitor->m_position.y, .width = monitor->m_size.x, .height = monitor->m_size.y};
    const Point  cursorPoint = vectorToPoint(coords);
    GeometryConfig geometryConfig = {
        .edges = config.edges,
        .inset = config.inset,
        .maxDistance = config.maxDistance,
        .overhangThreshold = config.overhangThreshold,
        .zones = config.zones,
    };

    const bool hasHeldButtons = g_pInputManager->hasHeldButtons();
    if (!hasHeldButtons)
        clearStickButtons();

    if (g_pSessionLockManager->isSessionLocked() || g_pInputManager->isConstrained() || g_pSeatManager->m_seatGrab || g_layoutManager->dragController()->target()) {
        clearSynthetic();
        return;
    }

    if (!g_pInputManager->m_exclusiveKeyboardLSes.empty() || (hasHeldButtons && !m_stickButtons) || focusedWindowConfinesPointer() || forcedFocusActive()) {
        clearSynthetic();
        return;
    }

    if (g_pInputManager->m_relay.popupFromCoords(coords)) {
        clearSynthetic();
        return;
    }

    if (currentWorkspaceHasFullscreen(monitor)) {
        clearSynthetic();
        return;
    }

    if (hasHeldButtons && m_stickButtons) {
        const auto pinnedWindow = m_synthetic.target.lock();
        if (!pinnedWindow) {
            clearStickAndSynthetic();
            return;
        }

        const Rect pinnedBox = boxToRect(pinnedWindow->getWindowMainSurfaceBox());
        if (pinnedBox.width <= 0.0 || pinnedBox.height <= 0.0) {
            clearStickAndSynthetic();
            return;
        }

        if (!deliverSyntheticMotion(pinnedWindow, pointToVector(clampPointToRectWithInset(cursorPoint, pinnedBox, config.inset)), config, m_synthetic.source)) {
            clearStickAndSynthetic();
            return;
        }

        // The stock path holds pointer focus while buttons are held; cancellation keeps the pinned target authoritative for this motion.
        info.cancelled = true;
        return;
    }

    const auto hitUpperLayer = stockUpperLayerHit(coords, monitor);
    PHLWINDOW  hitWindow     = nullptr;
    PHLLS      hitLowerLayer = nullptr;

    if (!hitUpperLayer) {
        hitWindow = stockWindowHit(coords);
        if (!hitWindow)
            hitLowerLayer = stockLowerLayerHit(coords, monitor);
    }

    const auto hitLayer = hitUpperLayer ? hitUpperLayer : hitLowerLayer;

    SyntheticSource source = SyntheticSource::Gap;

    if (hitLayer) {
        const PassSet layerPass = passForSource(config, SyntheticSource::Layer);
        const auto    edgeHit   = nearestEnabledEdge(monitorRect, cursorPoint, geometryConfig);
        if (!layerPass.hover || !edgeHit || edgeHit->distance > config.stealEdgeWidth || !layerNamespaceMatches(config, hitLayer)) {
            clearSynthetic();
            return;
        }

        source                     = SyntheticSource::Layer;
        geometryConfig.maxDistance = config.stealEdgeWidth;
    } else if (hitWindow) {
        const PassSet overhangPass = passForSource(config, SyntheticSource::Overhang);
        const auto    edgeHit      = nearestEnabledEdge(monitorRect, cursorPoint, geometryConfig);
        if (!overhangPass.hover || !edgeHit || (config.overhangEdgeWidth > 0.0 && edgeHit->distance > config.overhangEdgeWidth)) {
            clearSynthetic();
            return;
        }

        const Rect hitBox = boxToRect(goalBox(hitWindow));
        if (visibleThicknessForEdge(visibleThicknessInMonitor(monitorRect, hitBox), edgeHit->edge) > config.overhangThreshold) {
            clearSynthetic();
            return;
        }

        source                     = SyntheticSource::Overhang;
        geometryConfig.maxDistance = config.overhangEdgeWidth > 0.0 ? config.overhangEdgeWidth : 0.0;
    } else if (!passForSource(config, SyntheticSource::Gap).hover) {
        clearSynthetic();
        return;
    }

    std::vector<PHLWINDOW>         windows;
    std::vector<WindowCandidate>   candidates;

    for (const auto& window : Desktop::viewState()->windows()) {
        if (!window || !window->mapped() || window->isHidden() || window->m_monitor != monitor || !windowBelongsToActiveWorkspace(window, monitor) || !window->acceptsInput() ||
            window->shouldntFocus() || noFocus(window) || shadowedByModal(window) || confinePointer(window))
            continue;
        if (source == SyntheticSource::Overhang && window == hitWindow)
            continue;

        const auto box = goalBox(window);
        if (box.width <= 0.0 || box.height <= 0.0)
            continue;

        const std::size_t index = windows.size();
        const Rect        rect  = boxToRect(box);
        windows.push_back(window);
        candidates.push_back({.index = index, .box = rect, .visibleThickness = visibleThicknessInMonitor(monitorRect, rect)});
    }

    const auto picked = pickTarget(monitorRect, candidates, cursorPoint, geometryConfig);
    if (!picked || picked->windowIndex >= windows.size()) {
        clearSynthetic();
        return;
    }

    const auto window = windows[picked->windowIndex];
    if (!window) {
        clearSynthetic();
        return;
    }

    if (config.followMouse == 0 && config.keyboardFocus != 1 && Desktop::focusState() && window != Desktop::focusState()->window()) {
        clearSynthetic();
        return;
    }

    const Vector2D targetPoint = pointToVector(picked->targetPoint);
    if (!deliverSyntheticMotion(window, targetPoint, config, source)) {
        clearSynthetic();
        return;
    }

    // The stock path would clear pointer focus in gaps; cancellation keeps this synthetic delivery authoritative for this motion.
    info.cancelled = true;
}

void EdgeHover::handleMouseButton(const IPointer::SButtonEvent& event, Event::SCallbackInfo& info) {
    if (bypassForCapture(info))
        return;

    if (info.cancelled)
        return;

    if (event.state == WL_POINTER_BUTTON_STATE_RELEASED) {
        m_stickyButtonCodes.erase(event.button);
        if (m_stickyButtonCodes.empty())
            clearStickButtons();
        return;
    }

    if (event.state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;

    if (m_stickButtons) {
        m_stickyButtonCodes.insert(event.button);
        return;
    }

    if (!m_synthetic.active)
        return;

    const RuntimeConfig config = readConfig();
    if (config.enabled && passForSource(config, m_synthetic.source).click) {
        const auto pinnedWindow = m_synthetic.target.lock();
        if (!pinnedWindow) {
            clearSynthetic();
            return;
        }

        m_stickButtons = true;
        m_synthetic.target = pinnedWindow;
        m_stickyButtonCodes.insert(event.button);
        return;
    }

    clearSynthetic();
    if (g_pInputManager) {
        // Stock may refocus repeatedly after this listener returns. An idle
        // cannot run until synchronous button processing has finished.
        if (!m_buttonBypassIdle && g_pCompositor && g_pCompositor->m_wlDisplay) {
            m_buttonBypassIdle = wl_event_loop_add_idle(
                wl_display_get_event_loop(g_pCompositor->m_wlDisplay),
                [](void* data) {
                    auto& owner = *static_cast<EdgeHover*>(data);
                    owner.m_buttonBypassIdle = nullptr;
                    owner.m_buttonBypass = false;
                },
                this);
        }
        if (!m_buttonBypassIdle) {
            // Without a cleanup source, do not leave stale synthetic focus
            // eligible to receive this press.
            info.cancelled = true;
            return;
        }
        m_buttonBypass = true;
        g_pInputManager->simulateMouseMovement();
    }
}

void EdgeHover::handleMouseAxis(const IPointer::SAxisEvent& event, Event::SCallbackInfo& info) {
    if (bypassForCapture(info))
        return;

    (void)event;

    if (info.cancelled || !m_synthetic.active)
        return;

    const RuntimeConfig config = readConfig();
    if (config.enabled && passForSource(config, m_synthetic.source).scroll)
        return;

    clearSynthetic();
    if (g_pInputManager) {
        m_bypassNext = true;
        g_pInputManager->simulateMouseMovement();
    }
}

} // namespace hypr_edgehover
