#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "geometry.hpp"

namespace hypr_edgehover {

enum class SyntheticSource {
    Gap,
    Layer,
    Overhang,
};

struct PassSet {
    bool hover    = false;
    bool click    = false;
    bool scroll   = false;
    bool keyboard = false;
};

struct RuntimeConfig {
    bool                     enabled       = true;
    std::string              edges         = "lrtb";
    double                   inset         = 1.0;
    double                   maxDistance   = 0.0;
    long                     keyboardFocus = -1;
    long                     followMouse   = 0;
    PassSet                  gapPass       = {.hover = true, .click = true, .scroll = true, .keyboard = true};
    PassSet                  layerPass     = {.hover = true, .keyboard = true};
    std::vector<std::string> layerNamespaces;
    PassSet                  overhangPass      = {.hover = true, .keyboard = true};
    double                   overhangThreshold = 8.0;
    double                   stealEdgeWidth    = 2.0;
    EdgeZones                zones;
};

struct SyntheticState {
    bool            active = false;
    SyntheticSource source = SyntheticSource::Gap;
    PHLWINDOWREF    target;
};

struct PassSetCache {
    bool        initialized = false;
    std::string raw;
    PassSet     parsed;
};

struct StringListCache {
    bool                     initialized = false;
    std::string              raw;
    std::vector<std::string> parsed;
};

struct ZoneRangesCache {
    bool        initialized = false;
    std::string raw;
    ZoneRanges parsed;
};

class EdgeHover {
  public:
    explicit EdgeHover(HANDLE handle);
    ~EdgeHover() = default;

    bool initialize();

  private:
    RuntimeConfig readConfig() const;
    void          handleMouseMove(const Vector2D& coords, Event::SCallbackInfo& info);
    void          handleMouseButton(const IPointer::SButtonEvent& event, Event::SCallbackInfo& info);
    void          handleMouseAxis(const IPointer::SAxisEvent& event, Event::SCallbackInfo& info);
    bool          deliverSyntheticMotion(PHLWINDOW window, const Vector2D& targetPoint, const RuntimeConfig& config, SyntheticSource source);
    void          clearSynthetic();
    void          clearStickButtons();
    void          clearStickAndSynthetic();

    HANDLE                  m_handle = nullptr;
    CHyprSignalListener     m_mouseMoveListener;
    CHyprSignalListener     m_mouseButtonListener;
    CHyprSignalListener     m_mouseAxisListener;
    SyntheticState          m_synthetic;
    bool                    m_bypassNext   = false;
    bool                    m_stickButtons = false;
    std::set<uint32_t>      m_stickyButtonCodes;
    mutable PassSetCache    m_gapPassCache;
    mutable PassSetCache    m_layerPassCache;
    mutable PassSetCache    m_overhangPassCache;
    mutable StringListCache m_layerNamespacesCache;
    mutable ZoneRangesCache m_zonesTopCache;
    mutable ZoneRangesCache m_zonesBottomCache;
    mutable ZoneRangesCache m_zonesLeftCache;
    mutable ZoneRangesCache m_zonesRightCache;
};

} // namespace hypr_edgehover
