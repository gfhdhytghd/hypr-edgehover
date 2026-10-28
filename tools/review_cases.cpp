
static void confinementCase(EdgeHover& e) {
 reset(e);auto target=addWindow(100,mon->m_activeWorkspace);
 auto fallback=addWindow(300,mon->m_activeWorkspace);
 target->confined=true;move(e);
 assert(e.m_synthetic.target.lock()==fallback);
 // Delivery must recheck a rule that changed after selection/press.
 fallback->confined=true;
 int motions=seat.motions;
 assert(!e.deliverSyntheticMotion(fallback,{301,400},cfg,SyntheticSource::Gap));
 assert(seat.motions==motions);
 target->confined=false;move(e);assert(e.m_synthetic.target.lock()==target);
 std::cout<<"review confinement: target selection, delivery recheck and ordinary control PASS\n";
}
static void touchCase(EdgeHover& e) {
 reset(e);auto target=addWindow(100,mon->m_activeWorkspace);
 input.m_lastInputTouch=true;input.m_lastInputMouse=false;
 Event::SCallbackInfo touch;e.handleMouseMove({1,400},touch);
 assert(!touch.cancelled && !e.m_synthetic.active && seat.motions==0);
 // The touch flag is reset only AFTER a physical mouse dispatch in the fork.
 input.m_lastInputMouse=true;
 Event::SCallbackInfo mouse;e.handleMouseMove({1,400},mouse);
 assert(mouse.cancelled && e.m_synthetic.target.lock()==target);
 std::cout<<"review touch: refocus preserved and first physical mouse with stale touch flag PASS\n";
}
static void captureCase(EdgeHover& e) {
 for(int event=0;event<3;++event) {
  reset(e);auto target=addWindow(100,mon->m_activeWorkspace);move(e);
  e.m_synthetic.source=SyntheticSource::Overhang;
  e.m_bypassNext=true;e.m_buttonBypass=true;
  e.m_buttonBypassIdle=wl_event_loop_add_idle(wl_display_get_event_loop(compositor.m_wlDisplay),[](void*){assert(false);},nullptr);
  assert(e.m_buttonBypassIdle);
  PROTO::inputCapture->active=true;
  Event::SCallbackInfo info;
  if(event==0)e.handleMouseMove({1,400},info);
  if(event==1)e.handleMouseButton({272,1},info);
  if(event==2)e.handleMouseAxis({},info);
  assert(!info.cancelled && !e.m_synthetic.active && !e.m_stickButtons);
  assert(!e.m_bypassNext && !e.m_buttonBypass && !e.m_buttonBypassIdle && input.simulations==0);
  wl_event_loop_dispatch_idle(wl_display_get_event_loop(compositor.m_wlDisplay));
  PROTO::inputCapture->active=false;move(e);assert(e.m_synthetic.target.lock()==target);
 }
 reset(e);remote=true;
 Event::SCallbackInfo remoteInfo;e.handleMouseButton({272,1},remoteInfo);assert(remoteInfo.cancelled);
 std::cout<<"review capture: three native routes preserve events, clear idle/bypass, resume local; Viewflow cancellation PASS\n";
}
static void stickySurfaceCase(EdgeHover& e) {
 reset(e);cfg.keyboardFocus=0;auto target=addWindow(100,mon->m_activeWorkspace);
 target->splitSurface=true;target->child->offset={0,350};
 move(e);auto rootSurface=seat.pointer;
 Event::SCallbackInfo press;e.handleMouseButton({272,1},press);input.held=true;seat.pressed=true;
 Event::SCallbackInfo motion;e.handleMouseMove({1,500},motion);
 assert(motion.cancelled && seat.pointer==rootSurface && seat.autoReleases==0 && e.m_stickButtons);
 assert(seat.lastMotion.y==400);
 // Unmapping the fixed surface must relinquish the synthetic grab.
 rootSurface->m_mapped=false;move(e);assert(!e.m_stickButtons && !e.m_synthetic.active);
 rootSurface->m_mapped=true;input.held=false;seat.pressed=false;
 Event::SCallbackInfo hover;e.handleMouseMove({1,500},hover);
 assert(hover.cancelled && seat.pointer==target->child);
 Event::SCallbackInfo childPress;e.handleMouseButton({272,1},childPress);input.held=true;seat.pressed=true;
 Event::SCallbackInfo childMotion;e.handleMouseMove({1,400},childMotion);
 assert(childMotion.cancelled && seat.pointer==target->child && seat.autoReleases==0);
 assert(seat.lastMotion.y==-50);
 target->m_isMapped=false;move(e);assert(!e.m_synthetic.active && !e.m_stickButtons);
 std::cout<<"review sticky: root and child grabs retain pressed surface and local coordinates; unmap clears PASS\n";
}
static void reviewCases(EdgeHover& e,const std::string& name) {
 if(name=="all" || name=="confinement")confinementCase(e);
 if(name=="all" || name=="touch")touchCase(e);
 if(name=="all" || name=="capture")captureCase(e);
 if(name=="all" || name=="sticky")stickySurfaceCase(e);
}
