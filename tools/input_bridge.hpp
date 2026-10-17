
namespace hypr_edgehover {
static RuntimeConfig cfg;
namespace {
bool noFocus(PHLWINDOW){return false;}
bool noFollowMouse(PHLWINDOW){return false;}
bool forcedFocusActive(){return false;}
bool focusedWindowConfinesPointer(){return false;}
PHLLS stockUpperLayerHit(const Vector2D&,PHLMONITOR){return nullptr;}
PHLLS stockLowerLayerHit(const Vector2D&,PHLMONITOR){return nullptr;}
PHLWINDOW stockWindowHit(const Vector2D&){return nullptr;}
bool layerNamespaceMatches(const RuntimeConfig&,PHLLS){return true;}
PassSet passForSource(const RuntimeConfig& c,SyntheticSource s){switch(s){case SyntheticSource::Gap:return c.gapPass;case SyntheticSource::Layer:return c.layerPass;default:return c.overhangPass;}}
bool shouldFocusKeyboard(const RuntimeConfig& c,PHLWINDOW w){return c.keyboardFocus!=0 && Desktop::focusState()->window()!=w;}
}
EdgeHover::EdgeHover(HANDLE h):m_handle(h){}
RuntimeConfig EdgeHover::readConfig() const{return cfg;}
