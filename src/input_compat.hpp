#pragma once

// The pinned fork has no public accessors for the current dispatch source or
// border cursor cache. Preinclude InputManager's dependencies so the access
// override is confined to that class. PLUGIN_INIT checks the exact fork ABI.
#include <src/defines.hpp>
#include <list>
#include <any>
#include <src/helpers/WLClasses.hpp>
#include <src/helpers/time/Timer.hpp>
#include <src/managers/input/InputMethodRelay.hpp>
#include <src/managers/input/PointerConstraintController.hpp>
#include <src/helpers/signal/Signal.hpp>
#include <src/desktop/view/WLSurface.hpp>
#include <src/devices/IPointer.hpp>
#include <src/devices/ITouch.hpp>
#include <src/devices/IKeyboard.hpp>
#include <src/devices/Tablet.hpp>
#include <src/managers/SessionLockManager.hpp>
#include <src/managers/SeatManager.hpp>

#pragma push_macro("private")
#undef private
#define private public
#include <src/managers/input/InputManager.hpp>
#pragma pop_macro("private")
