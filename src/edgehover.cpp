#include "edgehover.hpp"

#include "geometry.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <typeinfo>
#include <vector>

#include <hyprland/protocols/wlr-layer-shell-unstable-v1.hpp>
#include <hyprland/src/Compositor.hpp>
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

long getConfigInt(const char* name, long fallback) {
    if (!Config::mgr())
        return fallback;

    const auto value = Config::mgr()->getConfigValue(name);
    if (!value.dataptr || !value.type)
        return fallback;

    if (*value.type == typeid(bool))
        return **reinterpret_cast<bool* const*>(value.dataptr) ? 1L : 0L;

    if (*value.type == typeid(Config::INTEGER))
        return static_cast<long>(**reinterpret_cast<Config::INTEGER* const*>(value.dataptr));

    return fallback;
}

std::string getConfigString(const char* name, const std::string& fallback) {
    if (!Config::mgr())
        return fallback;

    const auto value = Config::mgr()->getConfigValue(name);
    if (!value.dataptr || !value.type)
        return fallback;

    if (*value.type == typeid(Config::STRING))
        return **reinterpret_cast<Config::STRING* const*>(value.dataptr);

    if (*value.type == typeid(Hyprlang::STRING)) {
        const auto* data = reinterpret_cast<Hyprlang::STRING const*>(value.dataptr);
        return data && *data ? std::string(*data) : fallback;
    }

    return fallback;
}

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

bool workspaceHasFullscreen(PHLWORKSPACE workspace) {
    return workspace && workspace->getFullscreenWindow();
}

bool currentWorkspaceHasFullscreen(PHLMONITOR monitor) {
    if (!monitor)
        return false;

    return workspaceHasFullscreen(monitor->m_activeWorkspace) || workspaceHasFullscreen(monitor->m_activeSpecialWorkspace);
}

bool windowBelongsToActiveWorkspace(PHLWINDOW window, PHLMONITOR monitor) {
    if (!window || !monitor || !window->m_workspace)
        return false;

    return window->m_workspace == monitor->m_activeWorkspace || (monitor->m_activeSpecialWorkspace && window->m_workspace == monitor->m_activeSpecialWorkspace);
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

bool noFollowMouse(PHLWINDOW window) {
    return window && window->m_ruleApplicator && window->m_ruleApplicator->noFollowMouse().valueOrDefault();
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

    return getConfigInt("input:follow_mouse", 0) == 1;
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

    return {
        .enabled = getConfigInt("plugin:hypr_edgehover:enabled", 1) != 0,
        .edges = getConfigString("plugin:hypr_edgehover:edges", "lrtb"),
        .inset = static_cast<double>(std::max(0L, getConfigInt("plugin:hypr_edgehover:inset", 1))),
        .maxDistance = static_cast<double>(std::max(0L, getConfigInt("plugin:hypr_edgehover:max_distance", 0))),
        .keyboardFocus = getConfigInt("plugin:hypr_edgehover:keyboard_focus", -1),
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

    if (currentWorkspaceHasFullscreen(monitor))
        return;

    if (stockWindowHit(coords) || stockLayerHit(coords, monitor))
        return;

    std::vector<PHLWINDOW>         windows;
    std::vector<WindowCandidate>   candidates;
    const Rect                     monitorRect = {.x = monitor->m_position.x, .y = monitor->m_position.y, .width = monitor->m_size.x, .height = monitor->m_size.y};
    const GeometryConfig           geometryConfig = {.edges = config.edges, .inset = config.inset, .maxDistance = config.maxDistance};

    for (const auto& window : g_pCompositor->m_windows) {
        if (!window || !window->m_isMapped || window->isHidden() || !windowBelongsToActiveWorkspace(window, monitor))
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

    const Vector2D targetPoint = pointToVector(picked->targetPoint);
    Vector2D       surfaceLocal;
    auto           surface = g_pCompositor->vectorWindowToSurface(targetPoint, window, surfaceLocal);

    if (!surface) {
        const auto windowSurface = window->wlSurface();
        if (!windowSurface)
            return;

        surface      = windowSurface->resource();
        surfaceLocal = targetPoint - window->m_realPosition->value();
    }

    if (!surface)
        return;

    if (window->m_isX11)
        surfaceLocal = surfaceLocal * window->m_X11SurfaceScaledBy;

    g_pSeatManager->setPointerFocus(surface, surfaceLocal);
    g_pSeatManager->sendPointerMotion(Time::millis(Time::steadyNow()), surfaceLocal);

    if (shouldFocusKeyboard(config, window) && Desktop::focusState())
        Desktop::focusState()->rawWindowFocus(window, Desktop::FOCUS_REASON_FFM, surface);

    // The stock path would clear pointer focus in gaps; cancellation keeps this synthetic delivery authoritative for this motion.
    info.cancelled = true;
}

} // namespace hypr_edgehover
