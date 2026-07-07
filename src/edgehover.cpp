#include "edgehover.hpp"

#include "geometry.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <hyprland/protocols/wlr-layer-shell-unstable-v1.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

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
    return monitor->inFullscreenMode() || (workspace && workspace->getFullscreenWindow());
}

bool windowBelongsToActiveWorkspace(PHLWINDOW window, PHLMONITOR monitor) {
    if (!window || !monitor || !window->m_workspace)
        return false;

    return window->m_workspace == monitor->m_activeWorkspace || (monitor->m_activeSpecialWorkspace && window->m_workspace == monitor->m_activeSpecialWorkspace);
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

bool stockWindowHit(const Vector2D& coords) {
    constexpr uint16_t flags = Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING | Desktop::View::FOLLOW_MOUSE_CHECK;
    return g_pCompositor && g_pCompositor->vectorToWindowUnified(coords, flags);
}

bool stockLayerHit(const Vector2D& coords, PHLMONITOR monitor) {
    if (!g_pCompositor || !monitor)
        return false;

    Vector2D layerLocal;
    PHLLS    layerSurface = nullptr;

    if (g_pCompositor->vectorToLayerPopupSurface(coords, monitor, &layerLocal, &layerSurface))
        return true;

    constexpr std::array layers = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    };

    for (const auto layer : layers) {
        if (g_pCompositor->vectorToLayerSurface(coords, &monitor->m_layerSurfaceLayers[layer], &layerLocal, &layerSurface))
            return true;
    }

    return false;
}

bool forcedFocusActive() {
    PHLWINDOW forcedFocus;

    if (g_pInputManager)
        forcedFocus = g_pInputManager->m_forcedFocus.lock();
    if (!forcedFocus && g_pCompositor)
        forcedFocus = g_pCompositor->getForceFocus();

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

bool EdgeHover::initialize() {
    m_mouseMoveListener = Event::bus()->m_events.input.mouse.move.listen([this](const Vector2D& coords, Event::SCallbackInfo& info) {
        handleMouseMove(coords, info);
    });

    return static_cast<bool>(m_mouseMoveListener);
}

RuntimeConfig EdgeHover::readConfig() const {
    (void)m_handle;

    static const auto PENABLED       = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:enabled");
    static const auto PEDGES         = CConfigValue<std::string>("plugin:hypr_edgehover:edges");
    static const auto PINSET         = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:inset");
    static const auto PMAXDISTANCE   = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:max_distance");
    static const auto PKEYBOARDFOCUS = CConfigValue<Config::INTEGER>("plugin:hypr_edgehover:keyboard_focus");
    static const auto PFOLLOWMOUSE   = CConfigValue<Config::INTEGER>("input:follow_mouse");

    return {
        .enabled = *PENABLED != 0,
        .edges = *PEDGES,
        .inset = static_cast<double>(std::max<Config::INTEGER>(0, *PINSET)),
        .maxDistance = static_cast<double>(std::max<Config::INTEGER>(0, *PMAXDISTANCE)),
        .keyboardFocus = *PKEYBOARDFOCUS,
        .followMouse = *PFOLLOWMOUSE,
    };
}

void EdgeHover::handleMouseMove(const Vector2D& coords, Event::SCallbackInfo& info) {
    const RuntimeConfig config = readConfig();
    if (!config.enabled || info.cancelled)
        return;

    if (!g_pCompositor || !g_pSeatManager || !g_pInputManager || !g_pSessionLockManager || !g_layoutManager)
        return;

    const auto monitor = g_pCompositor->getMonitorFromVector(coords);
    if (!monitor)
        return;

    if (g_pSessionLockManager->isSessionLocked() || g_pInputManager->isConstrained() || g_pSeatManager->m_seatGrab || g_layoutManager->dragController()->target())
        return;

    if (!g_pInputManager->m_exclusiveLSes.empty() || g_pInputManager->hasHeldButtons() || focusedWindowConfinesPointer() || forcedFocusActive())
        return;

    if (g_pInputManager->m_relay.popupFromCoords(coords))
        return;

    if (currentWorkspaceHasFullscreen(monitor))
        return;

    if (stockWindowHit(coords) || stockLayerHit(coords, monitor))
        return;

    std::vector<PHLWINDOW>         windows;
    std::vector<WindowCandidate>   candidates;
    const Rect                     monitorRect = {.x = monitor->m_position.x, .y = monitor->m_position.y, .width = monitor->m_size.x, .height = monitor->m_size.y};
    const GeometryConfig           geometryConfig = {.edges = config.edges, .inset = config.inset, .maxDistance = config.maxDistance};

    for (const auto& window : g_pCompositor->m_windows) {
        if (!window || !window->m_isMapped || window->isHidden() || window->m_monitor != monitor || !windowBelongsToActiveWorkspace(window, monitor) || !window->acceptsInput() ||
            window->m_X11ShouldntFocus || noFocus(window))
            continue;

        const auto box = window->getWindowMainSurfaceBox();
        if (box.width <= 0.0 || box.height <= 0.0)
            continue;

        const std::size_t index = windows.size();
        windows.push_back(window);
        candidates.push_back({.index = index, .box = boxToRect(box)});
    }

    const auto picked = pickTarget(monitorRect, candidates, vectorToPoint(coords), geometryConfig);
    if (!picked || picked->windowIndex >= windows.size())
        return;

    const auto window = windows[picked->windowIndex];
    if (!window)
        return;

    if (config.followMouse == 0 && config.keyboardFocus != 1 && Desktop::focusState() && window != Desktop::focusState()->window())
        return;

    const Vector2D targetPoint = pointToVector(picked->targetPoint);
    Vector2D       surfaceLocal;
    SP<CWLSurfaceResource> surface;

    if (window->m_isX11) {
        const auto windowSurface = window->wlSurface();
        if (!windowSurface)
            return;

        surface      = windowSurface->resource();
        surfaceLocal = (targetPoint - window->m_realPosition->value()) * window->m_X11SurfaceScaledBy;
    } else {
        surface = g_pCompositor->vectorWindowToSurface(targetPoint, window, surfaceLocal);

        if (!surface) {
            const auto windowSurface = window->wlSurface();
            if (!windowSurface)
                return;

            surface      = windowSurface->resource();
            surfaceLocal = targetPoint - window->m_realPosition->value();
        }
    }

    if (!surface)
        return;

    g_pInputManager->m_emptyFocusCursorSet = false;
    g_pSeatManager->setPointerFocus(surface, surfaceLocal);
    g_pSeatManager->sendPointerMotion(Time::millis(Time::steadyNow()), surfaceLocal);

    if (shouldFocusKeyboard(config, window) && Desktop::focusState())
        Desktop::focusState()->rawWindowFocus(window, Desktop::FOCUS_REASON_FFM, surface);

    // The stock path would clear pointer focus in gaps; cancellation keeps this synthetic delivery authoritative for this motion.
    info.cancelled = true;
}

} // namespace hypr_edgehover
