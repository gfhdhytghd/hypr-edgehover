// Offline host fixture; models only synchronous policy and pointer delivery.

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <functional>
#include <wayland-server-core.h>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "geometry.hpp"
template<class T> using SP=std::shared_ptr<T>;
struct Vector2D { double x=0,y=0; Vector2D operator-(Vector2D p)const{return{x-p.x,y-p.y};} Vector2D operator*(double k)const{return{x*k,y*k};} };
struct CBox { double x=0,y=0,width=0,height=0; };
namespace Desktop::View { enum{GEOMETRIC_GOAL,GEOMETRIC_CURRENT}; struct IGeometric {enum{GEOMETRIC_GOAL,GEOMETRIC_CURRENT};}; }
struct FakeWorkspace{};
using PHLWORKSPACE=SP<FakeWorkspace>;
struct FakeMonitor {Vector2D m_position{0,0},m_size{1000,800}; PHLWORKSPACE m_activeWorkspace=std::make_shared<FakeWorkspace>(),m_activeSpecialWorkspace;};
using PHLMONITOR=SP<FakeMonitor>;
struct CWLSurfaceResource {int identity=1;};
struct FakeWLSurface {SP<CWLSurfaceResource> data=std::make_shared<CWLSurfaceResource>(); auto resource(){return data;}};
struct MonitorRef : SP<FakeMonitor> {
 using SP<FakeMonitor>::shared_ptr;
 MonitorRef& operator=(SP<FakeMonitor> m){SP<FakeMonitor>::operator=(m);return *this;}
 auto lock() const {return SP<FakeMonitor>(*this);}
};
struct FakeTop {bool modal=false;bool anyChildModal(){return modal;}};
struct FakeXdg {SP<FakeTop> m_toplevel=std::make_shared<FakeTop>();};
struct FakeWindow { bool m_isMapped=true,hidden=false,m_isX11=false,m_X11ShouldntFocus=false,input=true;
 double m_X11SurfaceScaledBy=1; PHLWORKSPACE m_workspace; MonitorRef m_monitor; SP<FakeXdg> m_xdgSurface=std::make_shared<FakeXdg>();
 Vector2D pos{100,100},sz{800,600}; SP<FakeWLSurface> surf=std::make_shared<FakeWLSurface>();
 auto position(int) {return pos;} auto size(int){return sz;} CBox getWindowMainSurfaceBox(){return{pos.x,pos.y,sz.x,sz.y};}

#ifdef MODERN_BACKEND
 struct Backend {FakeWindow* w;bool isX11()const{return w->m_isX11;} struct Traits{bool hasModalChild;}; Traits traits()const{return {w->m_xdgSurface->m_toplevel->modal};}};
 Backend backend(){return {this};}
#endif
 bool isHidden(){return hidden;} bool acceptsInput(){return input;} auto wlSurface(){return surf;}
};
using PHLWINDOW=SP<FakeWindow>; using PHLWINDOWREF=std::weak_ptr<FakeWindow>;
struct FakeLayer{}; using PHLLS=SP<FakeLayer>;
using HANDLE=void*; struct CHyprSignalListener{};
namespace Event {struct SCallbackInfo {bool cancelled=false;};}
constexpr int WL_POINTER_BUTTON_STATE_RELEASED=0,WL_POINTER_BUTTON_STATE_PRESSED=1;
struct IPointer {struct SButtonEvent{uint32_t button=272;int state=0;};struct SAxisEvent{};};
static std::function<void()> simulation;
static bool remote=false,full=false;
static bool viewflowCaptureActive(){return remote;}
namespace Fullscreen {struct Controller {template<class T> bool hasFullscreen(T){return full;}}; static Controller* controller(){static Controller c;return &c;}}
static PHLMONITOR mon=std::make_shared<FakeMonitor>();
namespace State {struct Query{Query& vec(Vector2D){return *this;} PHLMONITOR run(){return mon;}};struct Tracker{Query query(){return{};}};static Tracker* monitorState(){static Tracker t;return &t;}}
namespace Desktop {struct Focus {PHLWINDOW current; int fullCalls=0,rawCalls=0; PHLWINDOW window(){return current;} void rawWindowFocus(PHLWINDOW w,int,SP<CWLSurfaceResource>){++rawCalls;current=w;} void fullWindowFocus(PHLWINDOW w,int,SP<CWLSurfaceResource>){++fullCalls;current=w;}};
static Focus* focusState(){static Focus f;return &f;} constexpr int FOCUS_REASON_FFM=1;
struct HitTester{SP<CWLSurfaceResource> windowSurfaceAt(Vector2D p,PHLWINDOW w,Vector2D& local){if(!w->m_isMapped)return{};local=p-w->pos;return w->wlSurface()->resource();}};
struct ViewTracker{std::vector<PHLWINDOW> data;auto& windows(){return data;} HitTester hitTest(){return{};}};
static ViewTracker* viewState(){static ViewTracker v;return &v;}}
struct FakeInput {bool held=false,constrained=false;int simulations=0;std::vector<int> m_exclusiveLSes; bool m_emptyFocusCursorSet=false,m_lastFocusOnLS=false;
 struct Relay{bool popup=false;bool popupFromCoords(Vector2D){return popup;}}m_relay;
 bool hasHeldButtons(){return held;} bool isConstrained(){return constrained;} void simulateMouseMovement(){++simulations;if(simulation)simulation();}
};
static FakeInput input; static auto* g_pInputManager=&input;
struct FakeSeat{bool m_seatGrab=false;int motions=0;SP<CWLSurfaceResource> pointer;
 void setPointerFocus(SP<CWLSurfaceResource> s,Vector2D){pointer=s;} void sendPointerMotion(int,Vector2D){++motions;}};
static FakeSeat seat;static auto* g_pSeatManager=&seat;
struct Lock{bool locked=false;bool isSessionLocked(){return locked;}};static Lock lock;static auto* g_pSessionLockManager=&lock;
struct Layout {struct Drag{bool value=false;bool target(){return value;}} drag;auto* dragController(){return &drag;}};
static Layout layout;static auto* g_layoutManager=&layout;
struct FakeCompositor {wl_display* m_wlDisplay=wl_display_create();};
static FakeCompositor compositor;static auto* g_pCompositor=&compositor;
namespace Time {static int steadyNow(){return 0;}static int millis(int x){return x;}}

namespace Config {using INTEGER=long;}
static long fallthrough=0, modalBlocking=1;
template<class T> struct CConfigValue {std::string name;CConfigValue(const char* n):name(n){} T operator*()const{return name=="input:special_fallthrough"?fallthrough:modalBlocking;}};

