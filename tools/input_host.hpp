// Offline host fixture; models only synchronous policy and pointer delivery.

#include <algorithm>
#include <array>
#include <cassert>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <functional>
#include <wayland-server-core.h>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <vector>
#include "geometry.hpp"
template<class T> using SP=std::shared_ptr<T>;
template<class T> using WP=std::weak_ptr<T>;
struct Vector2D { double x=0,y=0; Vector2D operator-(Vector2D p)const{return{x-p.x,y-p.y};} Vector2D operator*(double k)const{return{x*k,y*k};} };
struct CBox { double x=0,y=0,width=0,height=0; };
namespace Desktop::View { enum{GEOMETRIC_GOAL,GEOMETRIC_CURRENT}; struct IGeometric {enum{GEOMETRIC_GOAL,GEOMETRIC_CURRENT};}; }
struct FakeWorkspace{};
using PHLWORKSPACE=SP<FakeWorkspace>;
struct FakeMonitor {Vector2D m_position{0,0},m_size{1000,800}; PHLWORKSPACE m_activeWorkspace=std::make_shared<FakeWorkspace>(),m_activeSpecialWorkspace;};
using PHLMONITOR=SP<FakeMonitor>;
struct CWLSurfaceResource {int identity=1;bool m_mapped=true;Vector2D offset;};
struct FakeWLSurface {SP<CWLSurfaceResource> data=std::make_shared<CWLSurfaceResource>(); auto resource(){return data;}};
struct MonitorRef : SP<FakeMonitor> {
 using SP<FakeMonitor>::shared_ptr;
 MonitorRef& operator=(SP<FakeMonitor> m){SP<FakeMonitor>::operator=(m);return *this;}
 auto lock() const {return SP<FakeMonitor>(*this);}
};
struct FakeTop {bool modal=false;bool anyChildModal(){return modal;}};
struct FakeXdg {SP<FakeTop> m_toplevel=std::make_shared<FakeTop>();};
struct FakeWindow { bool m_isMapped=true,hidden=false,m_isX11=false,m_X11ShouldntFocus=false,input=true,confined=false;
 double m_X11SurfaceScaledBy=1; PHLWORKSPACE m_workspace; MonitorRef m_monitor; SP<FakeXdg> m_xdgSurface=std::make_shared<FakeXdg>();
 bool splitSurface=false;SP<CWLSurfaceResource> child=std::make_shared<CWLSurfaceResource>();
 Vector2D pos{100,100},sz{800,600}; SP<FakeWLSurface> surf=std::make_shared<FakeWLSurface>();
 auto position(int) {return pos;} auto size(int){return sz;} CBox getWindowMainSurfaceBox(){return{pos.x,pos.y,sz.x,sz.y};}

 struct Backend {FakeWindow* w;bool isX11()const{return w->m_isX11;} Vector2D surfaceLocalToBuffer(Vector2D p)const{return p*w->m_X11SurfaceScaledBy;} struct Traits{bool hasModalChild;}; Traits traits()const{return {w->m_xdgSurface->m_toplevel->modal};}};
 Backend backend(){return {this};}
 bool mapped(){return m_isMapped;} bool shouldntFocus(){return m_X11ShouldntFocus;}
 bool isHidden(){return hidden;} bool acceptsInput(){return input;} auto wlSurface(){return surf;}
};
using PHLWINDOW=SP<FakeWindow>; using PHLWINDOWREF=std::weak_ptr<FakeWindow>;
struct FakeLayer{}; using PHLLS=SP<FakeLayer>;
using HANDLE=void*; struct CHyprSignalListener{};
namespace Event {struct SCallbackInfo {bool cancelled=false;};}
constexpr int WL_POINTER_BUTTON_STATE_RELEASED=0,WL_POINTER_BUTTON_STATE_PRESSED=1;
struct IPointer {struct SButtonEvent{uint32_t button=272;int state=0;};struct SAxisEvent{};};
static std::function<void()> simulation;
namespace PROTO {struct Capture {bool active=false;bool isCaptured(){return active;}};static auto inputCapture=std::make_shared<Capture>();}
static bool remote=false,full=false;
static bool viewflowCaptureActive(){return remote;}
namespace Fullscreen {struct Controller {template<class T> bool hasFullscreen(T){return full;}}; static Controller* controller(){static Controller c;return &c;}}
static PHLMONITOR mon=std::make_shared<FakeMonitor>();
namespace State {struct Query{Query& vec(Vector2D){return *this;} PHLMONITOR run(){return mon;}};struct Tracker{Query query(){return{};}};static Tracker* monitorState(){static Tracker t;return &t;}}
namespace Desktop {struct Focus {PHLWINDOW current; int fullCalls=0,rawCalls=0; PHLWINDOW window(){return current;} void rawWindowFocus(PHLWINDOW w,int,SP<CWLSurfaceResource>){++rawCalls;current=w;} void fullWindowFocus(PHLWINDOW w,int,SP<CWLSurfaceResource>){++fullCalls;current=w;}};
static Focus* focusState(){static Focus f;return &f;} constexpr int FOCUS_REASON_FFM=1;
struct HitTester{SP<CWLSurfaceResource> windowSurfaceAt(Vector2D p,PHLWINDOW w,Vector2D& local){if(!w->m_isMapped)return{};auto s=w->splitSurface && p.y>=450?w->child:w->wlSurface()->resource();local=p-w->pos-s->offset;return s;}
 Vector2D surfaceLocalAt(Vector2D p,PHLWINDOW w,SP<CWLSurfaceResource> s){return p-w->pos-s->offset;}};
struct ViewTracker{std::vector<PHLWINDOW> data;auto& windows(){return data;} HitTester hitTest(){return{};}};
static ViewTracker* viewState(){static ViewTracker v;return &v;}}
constexpr int BORDERICON_NONE=0;
namespace Pointer::Cursor {constexpr int CURSOR_OVERRIDE_WINDOW_EDGE=1;struct Override {bool edge=false,other=false;void unsetOverride(int){edge=false;}};static auto overrideController=std::make_shared<Override>();}
struct FakeInput {int m_borderIconDirection=BORDERICON_NONE;bool held=false,constrained=false;int simulations=0;std::vector<int> m_exclusiveKeyboardLSes; bool m_emptyFocusCursorSet=false,m_lastFocusOnLS=false,m_lastInputTouch=false,m_lastInputMouse=true;
 struct Relay{bool popup=false;bool popupFromCoords(Vector2D){return popup;}}m_relay;
 bool hasHeldButtons(){return held;} bool isConstrained(){return constrained;} void simulateMouseMovement(){++simulations;if(simulation)simulation();}
};
static FakeInput input; static auto* g_pInputManager=&input;
struct FakeSeat{bool m_seatGrab=false;int motions=0;Vector2D lastMotion;SP<CWLSurfaceResource> pointer;
 struct State{WP<CWLSurfaceResource> pointerFocus;}m_state;
 bool pressed=false;int autoReleases=0;
 void setPointerFocus(SP<CWLSurfaceResource> s,Vector2D){if(pointer!=s && pressed){++autoReleases;pressed=false;}pointer=s;m_state.pointerFocus=s;} void sendPointerMotion(int,Vector2D p){++motions;lastMotion=p;}};
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
