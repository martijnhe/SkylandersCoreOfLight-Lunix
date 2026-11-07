#pragma once

// The pads for the launcher (UpdatePads, InputHeld, the slots' batteries, a buzz, the USB portal):
// on Windows XInput and winmm (pad_state_win.h), elsewhere SDL3's gamepads (pad_state_sdl.h).

#ifdef _WIN32
#include "pad_state_win.h"
#else
#include "pad_state_sdl.h"
#endif
