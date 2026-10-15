
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
}
