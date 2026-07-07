#include "geometry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>

namespace hypr_edgehover {

double Rect::left() const {
    return x;
}

double Rect::right() const {
    return x + width;
}

double Rect::top() const {
    return y;
}

double Rect::bottom() const {
    return y + height;
}

double Rect::centerX() const {
    return x + width / 2.0;
}

double Rect::centerY() const {
    return y + height / 2.0;
}

bool Rect::contains(const Point& point) const {
    return point.x >= left() && point.x <= right() && point.y >= top() && point.y <= bottom();
}

namespace {

struct EdgeOption {
    Edge   edge;
    double distance;
    bool   enabled;
};

bool edgeEnabled(const std::string& edges, Edge edge) {
    const char needle = [edge] {
        switch (edge) {
            case Edge::Left: return 'l';
            case Edge::Right: return 'r';
            case Edge::Top: return 't';
            case Edge::Bottom: return 'b';
        }
        return ' ';
    }();

    return std::ranges::any_of(edges, [needle](unsigned char c) { return static_cast<char>(std::tolower(c)) == needle; });
}

double nearEdgeDistance(const Rect& monitor, const Rect& window, Edge edge) {
    switch (edge) {
        case Edge::Left: return window.left() - monitor.left();
        case Edge::Right: return monitor.right() - window.right();
        case Edge::Top: return window.top() - monitor.top();
        case Edge::Bottom: return monitor.bottom() - window.bottom();
    }
    return std::numeric_limits<double>::infinity();
}

bool parallelOverlaps(const Rect& window, const Point& coords, Edge edge) {
    switch (edge) {
        case Edge::Left:
        case Edge::Right: return coords.y >= window.top() && coords.y <= window.bottom();
        case Edge::Top:
        case Edge::Bottom: return coords.x >= window.left() && coords.x <= window.right();
    }
    return false;
}

double parallelDistance(const Rect& window, const Point& coords, Edge edge) {
    switch (edge) {
        case Edge::Left:
        case Edge::Right:
            if (coords.y < window.top())
                return window.top() - coords.y;
            if (coords.y > window.bottom())
                return coords.y - window.bottom();
            return 0.0;
        case Edge::Top:
        case Edge::Bottom:
            if (coords.x < window.left())
                return window.left() - coords.x;
            if (coords.x > window.right())
                return coords.x - window.right();
            return 0.0;
    }
    return std::numeric_limits<double>::infinity();
}

Point clampWithInset(const Point& coords, const Rect& box, double inset) {
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

std::optional<EdgeOption> nearestEnabledEdge(const Rect& monitor, const Point& coords, const GeometryConfig& config) {
    const std::array<EdgeOption, 4> options = {{
        {.edge = Edge::Left, .distance = coords.x - monitor.left(), .enabled = edgeEnabled(config.edges, Edge::Left)},
        {.edge = Edge::Right, .distance = monitor.right() - coords.x, .enabled = edgeEnabled(config.edges, Edge::Right)},
        {.edge = Edge::Top, .distance = coords.y - monitor.top(), .enabled = edgeEnabled(config.edges, Edge::Top)},
        {.edge = Edge::Bottom, .distance = monitor.bottom() - coords.y, .enabled = edgeEnabled(config.edges, Edge::Bottom)},
    }};

    std::optional<EdgeOption> best;
    for (const auto& option : options) {
        if (!option.enabled || option.distance < 0.0)
            continue;
        if (!best || option.distance < best->distance)
            best = option;
    }

    return best;
}

std::optional<WindowCandidate> chooseWindow(const Rect& monitor, const std::vector<WindowCandidate>& windows, const Point& coords, Edge edge) {
    struct ScoredWindow {
        WindowCandidate window;
        bool            overlapsParallel = false;
        double          parallelDistance = 0.0;
        double          normalDistance   = 0.0;
    };

    std::vector<ScoredWindow> scored;
    scored.reserve(windows.size());

    for (const auto& window : windows) {
        if (window.box.width <= 0.0 || window.box.height <= 0.0)
            continue;

        const double normalDistance = nearEdgeDistance(monitor, window.box, edge);
        if (normalDistance < 0.0)
            continue;

        scored.push_back({
            .window = window,
            .overlapsParallel = parallelOverlaps(window.box, coords, edge),
            .parallelDistance = parallelDistance(window.box, coords, edge),
            .normalDistance = normalDistance,
        });
    }

    if (scored.empty())
        return std::nullopt;

    const bool hasParallelOverlap = std::ranges::any_of(scored, [](const ScoredWindow& item) { return item.overlapsParallel; });

    const auto better = [hasParallelOverlap](const ScoredWindow& lhs, const ScoredWindow& rhs) {
        if (hasParallelOverlap) {
            if (lhs.overlapsParallel != rhs.overlapsParallel)
                return lhs.overlapsParallel;
            if (lhs.normalDistance != rhs.normalDistance)
                return lhs.normalDistance < rhs.normalDistance;
        } else {
            if (lhs.parallelDistance != rhs.parallelDistance)
                return lhs.parallelDistance < rhs.parallelDistance;
            if (lhs.normalDistance != rhs.normalDistance)
                return lhs.normalDistance < rhs.normalDistance;
        }
        return lhs.window.index < rhs.window.index;
    };

    return std::ranges::min_element(scored, better)->window;
}

} // namespace

std::optional<PickResult> pickTarget(const Rect& monitor, const std::vector<WindowCandidate>& windows, const Point& coords, const GeometryConfig& config) {
    if (monitor.width <= 0.0 || monitor.height <= 0.0 || windows.empty() || !monitor.contains(coords))
        return std::nullopt;

    const auto selectedEdge = nearestEnabledEdge(monitor, coords, config);
    if (!selectedEdge)
        return std::nullopt;

    if (config.maxDistance > 0.0 && selectedEdge->distance > config.maxDistance)
        return std::nullopt;

    const auto selectedWindow = chooseWindow(monitor, windows, coords, selectedEdge->edge);
    if (!selectedWindow)
        return std::nullopt;

    const double triggerWidth = nearEdgeDistance(monitor, selectedWindow->box, selectedEdge->edge);
    if (triggerWidth < 0.0 || selectedEdge->distance > triggerWidth)
        return std::nullopt;

    return PickResult{
        .windowIndex = selectedWindow->index,
        .edge = selectedEdge->edge,
        .targetPoint = clampWithInset(coords, selectedWindow->box, config.inset),
        .distanceToEdge = selectedEdge->distance,
    };
}

} // namespace hypr_edgehover
