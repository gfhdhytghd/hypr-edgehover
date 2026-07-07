#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "geometry.hpp"

namespace {

using hypr_edgehover::Edge;
using hypr_edgehover::EdgeZones;
using hypr_edgehover::GeometryConfig;
using hypr_edgehover::Point;
using hypr_edgehover::Rect;
using hypr_edgehover::WindowCandidate;
using hypr_edgehover::edgeZoneMatches;
using hypr_edgehover::pickTarget;
using hypr_edgehover::parseZones;
using hypr_edgehover::zoneContains;

bool expect(bool condition, const char* message) {
    if (condition)
        return true;

    std::cerr << "FAIL: " << message << '\n';
    return false;
}

bool closeEnough(double actual, double expected, double epsilon = 1e-6) {
    return std::abs(actual - expected) <= epsilon;
}

bool expectPoint(const Point& actual, const Point& expected, const char* message) {
    return expect(closeEnough(actual.x, expected.x) && closeEnough(actual.y, expected.y), message);
}

} // namespace

int main() {
    bool ok = true;

    const Rect                       monitor{0, 0, 1000, 800};
    const std::vector<WindowCandidate> singleWindow = {
        {.index = 0, .box = {20, 30, 960, 740}},
    };

    {
        const GeometryConfig config{.edges = "lrtb", .inset = 1, .maxDistance = 0};

        const auto left = pickTarget(monitor, singleWindow, {0, 400}, config);
        ok &= expect(left.has_value(), "left edge should trigger");
        ok &= expect(left && left->edge == Edge::Left, "left edge should report left");
        ok &= left ? expectPoint(left->targetPoint, {21, 400}, "left edge should clamp x inside the window") : false;

        const auto right = pickTarget(monitor, singleWindow, {999, 400}, config);
        ok &= expect(right.has_value(), "right edge should trigger");
        ok &= expect(right && right->edge == Edge::Right, "right edge should report right");
        ok &= right ? expectPoint(right->targetPoint, {979, 400}, "right edge should clamp x inside the window") : false;

        const auto top = pickTarget(monitor, singleWindow, {500, 0}, config);
        ok &= expect(top.has_value(), "top edge should trigger");
        ok &= expect(top && top->edge == Edge::Top, "top edge should report top");
        ok &= top ? expectPoint(top->targetPoint, {500, 31}, "top edge should clamp y inside the window") : false;

        const auto bottom = pickTarget(monitor, singleWindow, {500, 799}, config);
        ok &= expect(bottom.has_value(), "bottom edge should trigger");
        ok &= expect(bottom && bottom->edge == Edge::Bottom, "bottom edge should report bottom");
        ok &= bottom ? expectPoint(bottom->targetPoint, {500, 769}, "bottom edge should clamp y inside the window") : false;
    }

    {
        const GeometryConfig config{.edges = "lrtb", .inset = 1, .maxDistance = 0};

        const auto topLeft = pickTarget(monitor, singleWindow, {0, 0}, config);
        ok &= expect(topLeft.has_value(), "top-left corner should trigger");
        ok &= topLeft ? expectPoint(topLeft->targetPoint, {21, 31}, "top-left corner should clamp both axes") : false;

        const auto topRight = pickTarget(monitor, singleWindow, {999, 0}, config);
        ok &= expect(topRight.has_value(), "top-right corner should trigger");
        ok &= topRight ? expectPoint(topRight->targetPoint, {979, 31}, "top-right corner should clamp both axes") : false;

        const auto bottomLeft = pickTarget(monitor, singleWindow, {0, 799}, config);
        ok &= expect(bottomLeft.has_value(), "bottom-left corner should trigger");
        ok &= bottomLeft ? expectPoint(bottomLeft->targetPoint, {21, 769}, "bottom-left corner should clamp both axes") : false;

        const auto bottomRight = pickTarget(monitor, singleWindow, {999, 799}, config);
        ok &= expect(bottomRight.has_value(), "bottom-right corner should trigger");
        ok &= bottomRight ? expectPoint(bottomRight->targetPoint, {979, 769}, "bottom-right corner should clamp both axes") : false;
    }

    {
        const auto result = pickTarget(monitor, singleWindow, {10, 400}, {.edges = "lrtb", .inset = 1, .maxDistance = 0});
        ok &= expect(result.has_value(), "mid-gap-band point should trigger");
        ok &= result ? expectPoint(result->targetPoint, {21, 400}, "mid-gap-band point should clamp to the inside edge") : false;
    }

    {
        const auto result = pickTarget(monitor, singleWindow, {10, 400}, {.edges = "lrtb", .inset = 1, .maxDistance = 5});
        ok &= expect(!result.has_value(), "point beyond max_distance should not trigger");
    }

    {
        const auto result = pickTarget(monitor, singleWindow, {0, 400}, {.edges = "rtb", .inset = 1, .maxDistance = 0});
        ok &= expect(!result.has_value(), "disabled left edge should not trigger");
    }

    {
        ok &= expect(!pickTarget(monitor, {{.index = 0, .box = {-20, 30, 960, 740}}}, {0, 400}, {.edges = "l", .inset = 1, .maxDistance = 0}).has_value(),
                     "left edge should reject windows extending past the monitor edge");
        ok &= expect(!pickTarget(monitor, {{.index = 0, .box = {20, 30, 1000, 740}}}, {999, 400}, {.edges = "r", .inset = 1, .maxDistance = 0}).has_value(),
                     "right edge should reject windows extending past the monitor edge");
        ok &= expect(!pickTarget(monitor, {{.index = 0, .box = {20, -30, 960, 740}}}, {500, 0}, {.edges = "t", .inset = 1, .maxDistance = 0}).has_value(),
                     "top edge should reject windows extending past the monitor edge");
        ok &= expect(!pickTarget(monitor, {{.index = 0, .box = {20, 30, 960, 800}}}, {500, 799}, {.edges = "b", .inset = 1, .maxDistance = 0}).has_value(),
                     "bottom edge should reject windows extending past the monitor edge");
    }

    {
        const std::vector<WindowCandidate> windows = {
            {.index = 0, .box = {-10, 100, 400, 200}},
            {.index = 1, .box = {20, 300, 400, 200}},
        };

        const auto result = pickTarget(monitor, windows, {0, 350}, {.edges = "l", .inset = 1, .maxDistance = 0});
        ok &= expect(result.has_value(), "wrong-side left candidate should not block valid candidates");
        ok &= expect(result && result->windowIndex == 1, "left edge should pick the valid candidate after rejecting the wrong-side one");
        ok &= result ? expectPoint(result->targetPoint, {21, 350}, "valid candidate target should clamp normally") : false;
    }

    {
        const std::vector<WindowCandidate> windows = {
            {.index = 0, .box = {20, 100, 400, 200}},
            {.index = 1, .box = {20, 500, 400, 200}},
        };

        const auto result = pickTarget(monitor, windows, {0, 450}, {.edges = "l", .inset = 1, .maxDistance = 0});
        ok &= expect(result.has_value(), "gap between two windows should still pick a target");
        ok &= expect(result && result->windowIndex == 1, "gap between two windows should pick the parallel-nearest window");
        ok &= result ? expectPoint(result->targetPoint, {21, 501}, "parallel-nearest target should clamp to that window") : false;
    }

    {
        const std::vector<WindowCandidate> tinyWindow = {
            {.index = 0, .box = {20, 30, 10, 10}},
        };

        const auto result = pickTarget(monitor, tinyWindow, {0, 35}, {.edges = "l", .inset = 7, .maxDistance = 0});
        ok &= expect(result.has_value(), "tiny window should still trigger");
        ok &= result ? expectPoint(result->targetPoint, {25, 35}, "oversized inset should clamp to the window center on that axis") : false;
    }

    {
        const auto splitZones = parseZones("0-40,60-100");
        ok &= expect(splitZones.size() == 2, "split zones should parse two valid ranges");
        ok &= expect(zoneContains(splitZones, 0) && zoneContains(splitZones, 40), "split zones should include the first range endpoints");
        ok &= expect(zoneContains(splitZones, 60) && zoneContains(splitZones, 100), "split zones should include the second range endpoints");
        ok &= expect(!zoneContains(splitZones, 50), "split zones should reject the middle gap");

        const auto outOfOrder = parseZones("80-100,0-20");
        ok &= expect(zoneContains(outOfOrder, 10) && zoneContains(outOfOrder, 90), "out-of-order zones should match independently of list order");
        ok &= expect(!zoneContains(outOfOrder, 50), "out-of-order zones should still reject missing ranges");

        const auto invalidMixed = parseZones("0-20,120-130,-5-5,bad,40-30,80-100");
        ok &= expect(invalidMixed.size() == 2, "invalid and out-of-range zone fragments should be ignored");
        ok &= expect(zoneContains(invalidMixed, 10) && zoneContains(invalidMixed, 90), "valid fragments should survive invalid zone fragments");

        const auto emptyZones = parseZones("");
        ok &= expect(emptyZones.empty(), "empty zone config should parse as no ranges");
        ok &= expect(!zoneContains(emptyZones, 0), "empty zones should match nothing");
    }

    {
        EdgeZones zones;
        zones.left  = parseZones("0-50");
        zones.right = parseZones("");
        zones.top   = parseZones("0-40,60-100");

        ok &= expect(edgeZoneMatches(monitor, {350, 0}, Edge::Top, zones), "top zone should match the first enabled percent range");
        ok &= expect(!edgeZoneMatches(monitor, {500, 0}, Edge::Top, zones), "top zone should reject the disabled middle percent range");
        ok &= expect(edgeZoneMatches(monitor, {900, 0}, Edge::Top, zones), "top zone should match the second enabled percent range");
        ok &= expect(edgeZoneMatches(monitor, {0, 200}, Edge::Left, zones), "left zone should match vertical edge percent");
        ok &= expect(!edgeZoneMatches(monitor, {0, 600}, Edge::Left, zones), "left zone should reject vertical edge percent outside its range");
        ok &= expect(!edgeZoneMatches(monitor, {999, 200}, Edge::Right, zones), "empty right zone should match nothing");
    }

    {
        EdgeZones zones;
        zones.top = parseZones("0-40,60-100");

        const GeometryConfig config{.edges = "t", .inset = 1, .maxDistance = 0, .overhangThreshold = 8, .zones = zones};
        ok &= expect(pickTarget(monitor, singleWindow, {350, 0}, config).has_value(), "pickTarget should allow points inside edge zones");
        ok &= expect(!pickTarget(monitor, singleWindow, {500, 0}, config).has_value(), "pickTarget should reject points outside edge zones");

        zones.left = parseZones("");
        const GeometryConfig fallbackConfig{.edges = "lt", .inset = 1, .maxDistance = 0, .overhangThreshold = 8, .zones = zones};
        const auto           fallback = pickTarget(monitor, singleWindow, {0, 30}, fallbackConfig);
        ok &= expect(fallback.has_value(), "zone rejection on the nearest enabled edge should fall back to a farther matching edge");
        ok &= expect(fallback && fallback->edge == Edge::Top, "fallback edge should be the farther top edge");
    }

    {
        EdgeZones zones;
        zones.left = parseZones("");
        zones.top  = parseZones("0-100");

        const GeometryConfig cornerFallbackConfig{.edges = "lt", .inset = 1, .maxDistance = 0, .overhangThreshold = 8, .zones = zones};
        const auto           cornerFallback = pickTarget(monitor, singleWindow, {0, 0}, cornerFallbackConfig);
        ok &= expect(cornerFallback.has_value(), "corner point should fall back from a rejected tie edge to a matching edge");
        ok &= expect(cornerFallback && cornerFallback->edge == Edge::Top, "corner fallback should keep l-r-t-b tie order after rejected edges");
    }

    {
        const std::vector<WindowCandidate> windows = {
            {.index = 0, .box = {0, 100, 4, 300}, .visibleThickness = {.left = 4, .right = 4, .top = 300, .bottom = 300}},
            {.index = 1, .box = {20, 100, 500, 300}, .visibleThickness = {.left = 500, .right = 500, .top = 300, .bottom = 300}},
        };

        const auto result = pickTarget(monitor, windows, {1, 200}, {.edges = "l", .inset = 1, .maxDistance = 0, .overhangThreshold = 8});
        ok &= expect(result.has_value(), "thin edge windows should not block a non-thin target behind them");
        ok &= expect(result && result->windowIndex == 1, "thin edge windows should not be selected as targets");
        ok &= result ? expectPoint(result->targetPoint, {21, 200}, "non-thin target behind a thin window should clamp normally") : false;

        ok &= expect(!pickTarget(monitor, {windows.front()}, {1, 200}, {.edges = "l", .inset = 1, .maxDistance = 0, .overhangThreshold = 8}).has_value(),
                     "all-thin candidates should produce no target");
    }

    {
        const std::vector<WindowCandidate> windows = {
            {.index = 0, .box = {0, 100, 8, 300}, .visibleThickness = {.left = 8, .right = 8, .top = 300, .bottom = 300}},
            {.index = 1, .box = {20, 100, 500, 300}, .visibleThickness = {.left = 500, .right = 500, .top = 300, .bottom = 300}},
        };

        const auto result = pickTarget(monitor, windows, {6, 200}, {.edges = "l", .inset = 1, .maxDistance = 0, .overhangThreshold = 8});
        ok &= expect(result.has_value(), "OVERHANG with uncapped maxDistance should trigger from the middle of the exposed strip");
        ok &= expect(result && result->windowIndex == 1, "OVERHANG middle-strip point should pick the non-thin target");
        ok &= result ? expectPoint(result->targetPoint, {21, 200}, "OVERHANG middle-strip target should clamp normally") : false;
    }

    {
        const std::vector<WindowCandidate> gapThresholdTarget = {
            {.index = 0, .box = {20, 100, 9, 300}, .visibleThickness = {.left = 9, .right = 9, .top = 300, .bottom = 300}},
        };

        const auto result = pickTarget(monitor, gapThresholdTarget, {0, 200}, {.edges = "l", .inset = 1, .maxDistance = 0, .overhangThreshold = 8});
        ok &= expect(result.has_value(), "GAP target just above overhang_threshold should remain selectable");
        ok &= expect(result && result->windowIndex == 0, "GAP threshold-boundary target should be selected");
        ok &= result ? expectPoint(result->targetPoint, {21, 200}, "GAP threshold-boundary target should clamp normally") : false;
    }

    {
        const std::vector<WindowCandidate> reservedAreaTarget = {
            {.index = 0, .box = {30, 50, 900, 700}},
        };
        const GeometryConfig stealConfig{.edges = "l", .inset = 1, .maxDistance = 2, .overhangThreshold = 8};

        const auto allowed = pickTarget(monitor, reservedAreaTarget, {2, 400}, stealConfig);
        ok &= expect(allowed.has_value(), "steal_edge_width should allow points inside the configured physical-edge strip");
        ok &= allowed ? expectPoint(allowed->targetPoint, {31, 400}, "bar-edge target should be reachable across the reserved area") : false;

        const auto rejected = pickTarget(monitor, reservedAreaTarget, {3, 400}, stealConfig);
        ok &= expect(!rejected.has_value(), "steal_edge_width should reject points outside the configured physical-edge strip");
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
