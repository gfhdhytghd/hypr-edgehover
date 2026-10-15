
using namespace hypr_edgehover;
static void reset(EdgeHover& e) {
 input=FakeInput{};seat=FakeSeat{};remote=false;full=false;lock.locked=false;
 fallthrough=0;modalBlocking=1;cfg=RuntimeConfig{};cfg.followMouse=1;
 cfg.zones={parseZones("0-100"),parseZones("0-100"),parseZones("0-100"),parseZones("0-100")};
 mon->m_activeSpecialWorkspace.reset();Desktop::focusState()->current.reset();
 Desktop::viewState()->data.clear();e.clearStickAndSynthetic();
}
static PHLWINDOW addWindow(double x, PHLWORKSPACE ws) {
 auto w=std::make_shared<FakeWindow>();w->pos.x=x;w->m_monitor=mon;w->m_workspace=ws;
 Desktop::viewState()->data.push_back(w);return w;
}
static void move(EdgeHover& e) {Event::SCallbackInfo info;e.handleMouseMove({1,400},info);}
static EdgeHover* owner;
static PHLWINDOW native;
static int stockButtons=0, nestedMoves=0;
static bool allowClick=false;
static void stockMove() {
 Event::SCallbackInfo info;owner->handleMouseMove({1,400},info);++nestedMoves;
 if (!info.cancelled) seat.pointer=native->surf->data;
}
static void stockButton(IPointer::SButtonEvent event) {
 Event::SCallbackInfo info;owner->handleMouseButton(event,info);assert(!info.cancelled);
 // A slow click leaves keyboard focus synthetic after simulateMouseMovement.
 // Stock may refocus multiple times before finally delivering this button.
 if(event.state==WL_POINTER_BUTTON_STATE_PRESSED) {
 stockMove();stockMove();
 assert(seat.pointer==(allowClick?owner->m_synthetic.target.lock():native)->surf->data);
 }
 ++stockButtons;
}
static void buttonCases(EdgeHover& e) {
 reset(e);owner=&e;native=std::make_shared<FakeWindow>();
 auto target=addWindow(100,mon->m_activeWorkspace);
 e.m_synthetic={true,SyntheticSource::Overhang,target};seat.pointer=target->surf->data;
 cfg.overhangPass.click=false;simulation=stockMove;
 stockButton({272,1});
 assert(stockButtons==1&&nestedMoves==3&&e.m_buttonBypass&&!e.m_synthetic.active);
 assert(e.m_buttonBypassIdle);
 // Even extra synchronous motions cannot consume the button barrier.
 stockMove();assert(seat.pointer==native->surf->data&&e.m_buttonBypass);
 wl_event_loop_dispatch_idle(wl_display_get_event_loop(compositor.m_wlDisplay));
 assert(!e.m_buttonBypass&&!e.m_buttonBypassIdle);
 // The next physical motion can synthesize again: bypass cannot leak.
 move(e);assert(e.m_synthetic.active&&seat.pointer==target->surf->data);
 e.m_synthetic.source=SyntheticSource::Overhang;cfg.overhangPass.click=true;allowClick=true;
 input.held=true;stockButton({272,1});
 assert(e.m_stickButtons&&stockButtons==2&&!e.m_buttonBypass);
 stockButton({272,0});input.held=false;
 assert(!e.m_stickButtons&&stockButtons==3);
 // Multiple presses before idle share one source. Destroying the owner
 // cancels it; dispatching the loop afterwards must not touch freed state.
 auto temporary=std::make_unique<EdgeHover>(nullptr);owner=temporary.get();
 temporary->m_synthetic={true,SyntheticSource::Overhang,target};cfg.overhangPass.click=false;allowClick=false;
 Event::SCallbackInfo info;temporary->handleMouseButton({272,1},info);
 auto* idle=temporary->m_buttonBypassIdle;assert(idle);
 temporary->m_synthetic={true,SyntheticSource::Overhang,target};
 temporary->handleMouseButton({273,1},info);assert(temporary->m_buttonBypassIdle==idle);
 temporary.reset();owner=nullptr;simulation={};
 wl_event_loop_dispatch_idle(wl_display_get_event_loop(compositor.m_wlDisplay));
 // If no idle cleanup can be scheduled, the press must not reach stale focus.
 auto* display=compositor.m_wlDisplay;compositor.m_wlDisplay=nullptr;
 e.m_synthetic={true,SyntheticSource::Overhang,target};
 Event::SCallbackInfo failed;e.handleMouseButton({272,1},failed);
 assert(failed.cancelled&&!e.m_buttonBypass&&!e.m_buttonBypassIdle);
 compositor.m_wlDisplay=display;
 std::cout<<"button: repeated refocus, idle release, click enabled and destruction cancellation PASS\n";
}
int main() {
 EdgeHover e{nullptr};reset(e);
 auto normal=addWindow(100,mon->m_activeWorkspace);
 mon->m_activeSpecialWorkspace=std::make_shared<FakeWorkspace>();
 auto special=addWindow(400,mon->m_activeSpecialWorkspace);
 move(e);assert(e.m_synthetic.target.lock()==special);
 assert(Desktop::focusState()->current==special);
 assert(Desktop::focusState()->fullCalls>0&&Desktop::focusState()->rawCalls==0);
 fallthrough=1;move(e);assert(e.m_synthetic.target.lock()==normal);
 mon->m_activeSpecialWorkspace.reset();fallthrough=0;move(e);
 assert(e.m_synthetic.target.lock()==normal);
 std::cout<<"special workspace: blocking, fallthrough, and ordinary workspace PASS\n";
 reset(e);normal=addWindow(100,mon->m_activeWorkspace);
 auto child=addWindow(400,mon->m_activeWorkspace);
 normal->m_xdgSurface->m_toplevel->modal=true;
 move(e);assert(e.m_synthetic.target.lock()==child);
 modalBlocking=0;move(e);assert(e.m_synthetic.target.lock()==normal);
 modalBlocking=1;normal->m_isX11=true;move(e);
 assert(e.m_synthetic.target.lock()==normal);
 std::cout<<"modal parent: Wayland blocked, disabled setting and X11 allowed PASS\n";
 // Pinned targets must obey changes made after the press too.
 reset(e);normal=addWindow(100,mon->m_activeWorkspace);move(e);
 Event::SCallbackInfo info;e.handleMouseButton({272,1},info);input.held=true;
 assert(e.m_stickButtons);
 mon->m_activeSpecialWorkspace=std::make_shared<FakeWorkspace>();move(e);
 assert(!e.m_synthetic.active&&!e.m_stickButtons);
 reset(e);normal=addWindow(100,mon->m_activeWorkspace);move(e);
 e.handleMouseButton({272,1},info);input.held=true;
 normal->m_xdgSurface->m_toplevel->modal=true;move(e);
 assert(!e.m_synthetic.active&&!e.m_stickButtons);
 std::cout<<"sticky targets: recheck special/modal input blocking PASS\n";
 buttonCases(e);
 wl_display_destroy(compositor.m_wlDisplay);compositor.m_wlDisplay=nullptr;
}
