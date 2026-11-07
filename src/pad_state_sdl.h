#pragma once

// The pads for the launcher off Windows: every gamepad SDL3 knows (its own mappings cover Xbox,
// PlayStation, Nintendo and Steam Deck pads, and the community database the DirectInput layouts
// pad_state_win.h maps by hand), merged into Dear ImGui's gamepad keys; the first four connected
// ones fill the start screen's slots. The same interface as pad_state_win.h.

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "hints.h"
#include "hub_log.h"
#include "imgui.h"

namespace coreoflight {

constexpr int kPadSlots = 4;

// Which of the start screen's four slots have a pad (UpdatePads keeps them, every frame).
inline std::atomic<bool> g_pad_connected[kPadSlots] = {};

// Pads outside the four slots aren't shown apart off Windows; kept for launcher_ui.cpp.
struct GenericPad {
  bool connected = false, skip = true;
  unsigned long vendor = 0;
};
inline GenericPad g_generic_pads[16];

enum class UsbPortalKind { kNone, kPs3WiiU, kXbox360 };
inline std::atomic<UsbPortalKind> g_usb_portal{UsbPortalKind::kNone};

// The real Portal of Power on USB by its ids in sysfs (1430:0150 PS3 / Wii / Wii U, 1430:1F17 Xbox
// 360), only looked at, never opened: a game may be using it.
inline UsbPortalKind FindUsbPortal() {
  namespace fs = std::filesystem;
  std::error_code ec;
  UsbPortalKind found = UsbPortalKind::kNone;
  auto read = [](const fs::path& file) {
    std::ifstream in(file);
    std::string text;
    in >> text;
    return text;
  };
  for (auto it = fs::directory_iterator("/sys/bus/usb/devices", ec); !ec && it != fs::directory_iterator();
       it.increment(ec)) {
    if (read(it->path() / "idVendor") != "1430") continue;
    const std::string product = read(it->path() / "idProduct");
    if (product == "0150") return UsbPortalKind::kPs3WiiU;
    if (product == "1f17" || product == "1F17") found = UsbPortalKind::kXbox360;
  }
  return found;
}

// The USB portal every two seconds, on a thread of its own.
inline void StartPadWatch() {
  static std::once_flag once;
  std::call_once(once, [] {
    std::thread([] {
      for (;;) {
        const UsbPortalKind portal = FindUsbPortal();
        if (g_usb_portal.exchange(portal) != portal) {
          HubLog(portal == UsbPortalKind::kNone       ? "portal: USB portal gone"
                 : portal == UsbPortalKind::kXbox360 ? "portal: Xbox 360 USB portal plugged in"
                                                     : "portal: USB portal (PS3 / Wii / Wii U) plugged in");
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
    }).detach();
  });
}

// The open gamepads by slot; slots 4.. hold pads past the fourth (merged, not shown).
struct SdlPads {
  std::array<SDL_Gamepad*, 16> pads{};
  std::mutex mutex;  // PadBattery and PadBuzz come from the frame thread as well, but be safe
};
inline SdlPads& Pads() {
  static SdlPads pads;
  return pads;
}

// Opens new gamepads into the first free slot, closes gone ones. Each frame (SDL_GetGamepads is
// a list in memory, no device is asked).
inline void RefreshPads() {
  SdlPads& p = Pads();
  std::lock_guard lock(p.mutex);
  for (size_t i = 0; i < p.pads.size(); ++i) {
    if (p.pads[i] && !SDL_GamepadConnected(p.pads[i])) {
      HubLog("pads: slot " + std::to_string(i) + " gone");
      SDL_CloseGamepad(p.pads[i]);
      p.pads[i] = nullptr;
    }
  }
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  for (int k = 0; ids && k < count; ++k) {
    const bool open = std::any_of(p.pads.begin(), p.pads.end(),
                                  [&](SDL_Gamepad* g) { return g && SDL_GetGamepadID(g) == ids[k]; });
    if (open) continue;
    auto slot = std::find(p.pads.begin(), p.pads.end(), nullptr);
    if (slot == p.pads.end()) break;
    if ((*slot = SDL_OpenGamepad(ids[k]))) {
      const char* name = SDL_GetGamepadName(*slot);
      char line[200];
      std::snprintf(line, sizeof(line), "pads: slot %d connected, %s (vendor %04X, product %04X)",
                    int(slot - p.pads.begin()), name ? name : "unnamed", unsigned(SDL_GetGamepadVendor(*slot)),
                    unsigned(SDL_GetGamepadProduct(*slot)));
      HubLog(line);
    }
  }
  SDL_free(ids);
  for (int i = 0; i < kPadSlots; ++i) g_pad_connected[i] = p.pads[i] != nullptr;
}

// Closes every open gamepad (before SDL_Quit, main_sdl.cpp).
inline void ClosePads() {
  SdlPads& p = Pads();
  std::lock_guard lock(p.mutex);
  for (SDL_Gamepad*& g : p.pads) {
    if (g) SDL_CloseGamepad(g);
    g = nullptr;
  }
  for (auto& connected : g_pad_connected) connected = false;
}

// A connected pad's battery for the start screen: 0 empty .. 3 full, or -1 for a wired pad (or one
// that doesn't say).
inline int PadBattery(int pad) {
  SdlPads& p = Pads();
  std::lock_guard lock(p.mutex);
  if (pad < 0 || pad >= kPadSlots || !p.pads[pad]) return -1;
  int percent = -1;
  if (SDL_GetGamepadPowerInfo(p.pads[pad], &percent) != SDL_POWERSTATE_ON_BATTERY || percent < 0) return -1;
  return std::clamp(percent * 4 / 101, 0, 3);
}

// A quarter-second buzz at `percent` of full strength (the controller page, #35).
inline void PadBuzz(int pad, int percent) {
  SdlPads& p = Pads();
  std::lock_guard lock(p.mutex);
  if (pad < 0 || pad >= kPadSlots || !p.pads[pad]) return;
  const Uint16 speed = Uint16(65535u * unsigned(std::clamp(percent, 0, 100)) / 100u);
  SDL_RumbleGamepad(p.pads[pad], speed, speed, speed ? 250 : 0);
}

// Every gamepad merged into Dear ImGui's gamepad keys (the SDL3 backend's own gamepad reading is
// left out: ImGui_ImplSDL3_SetGamepadMode isn't called and this runs after its NewFrame). The hints
// follow the maker of the pad pressed (#29).
inline void UpdatePads(ImGuiIO& io) {
  StartPadWatch();
  RefreshPads();
  SdlPads& p = Pads();
  std::lock_guard lock(p.mutex);
  bool any = false;
  bool buttons[SDL_GAMEPAD_BUTTON_COUNT] = {};
  int16_t axes[SDL_GAMEPAD_AXIS_COUNT] = {};
  for (SDL_Gamepad* g : p.pads) {
    if (!g) continue;
    any = true;
    bool pressed = false;
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
      const bool down = SDL_GetGamepadButton(g, SDL_GamepadButton(b));
      buttons[b] = buttons[b] || down;
      pressed = pressed || down;
    }
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
      const int16_t v = SDL_GetGamepadAxis(g, SDL_GamepadAxis(a));
      if (std::abs(int(v)) > std::abs(int(axes[a]))) axes[a] = v;  // the stick pushed furthest
    }
    if (pressed) {
      switch (SDL_GetGamepadType(g)) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5:
          SetHintPad(HintPad::kPlayStation);
          break;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
          SetHintPad(HintPad::kNintendo);
          break;
        default:
          SetHintPad(HintPad::kXbox);
      }
    }
  }
  io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  if (!any) return;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  // As Dear ImGui's SDL3 backend maps them (imgui_impl_sdl3.cpp ImGui_ImplSDL3_UpdateGamepads): the
  // buttons by position, so the bottom one is A whatever is printed on it.
  auto button = [&](ImGuiKey key, SDL_GamepadButton b) { io.AddKeyEvent(key, buttons[b]); };
  auto analog = [&](ImGuiKey key, SDL_GamepadAxis a, float v0, float v1) {
    const float v = std::clamp((float(axes[a]) - v0) / (v1 - v0), 0.0f, 1.0f);
    io.AddKeyAnalogEvent(key, v > 0.10f, v);
  };
  button(ImGuiKey_GamepadStart, SDL_GAMEPAD_BUTTON_START);
  button(ImGuiKey_GamepadBack, SDL_GAMEPAD_BUTTON_BACK);
  button(ImGuiKey_GamepadFaceLeft, SDL_GAMEPAD_BUTTON_WEST);
  button(ImGuiKey_GamepadFaceRight, SDL_GAMEPAD_BUTTON_EAST);
  button(ImGuiKey_GamepadFaceUp, SDL_GAMEPAD_BUTTON_NORTH);
  button(ImGuiKey_GamepadFaceDown, SDL_GAMEPAD_BUTTON_SOUTH);
  button(ImGuiKey_GamepadDpadLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  button(ImGuiKey_GamepadDpadRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
  button(ImGuiKey_GamepadDpadUp, SDL_GAMEPAD_BUTTON_DPAD_UP);
  button(ImGuiKey_GamepadDpadDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  button(ImGuiKey_GamepadL1, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  button(ImGuiKey_GamepadR1, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  button(ImGuiKey_GamepadL3, SDL_GAMEPAD_BUTTON_LEFT_STICK);
  button(ImGuiKey_GamepadR3, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
  constexpr float kTrigger = 30.0f * 128.0f;  // XInput's trigger threshold on SDL's 0..32767
  constexpr float kDead = 7849.0f;            // XInput's left stick dead zone
  analog(ImGuiKey_GamepadL2, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, kTrigger, 32767);
  analog(ImGuiKey_GamepadR2, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, kTrigger, 32767);
  // SDL's y axes point down, XInput's up.
  analog(ImGuiKey_GamepadLStickLeft, SDL_GAMEPAD_AXIS_LEFTX, -kDead, -32768);
  analog(ImGuiKey_GamepadLStickRight, SDL_GAMEPAD_AXIS_LEFTX, +kDead, +32767);
  analog(ImGuiKey_GamepadLStickUp, SDL_GAMEPAD_AXIS_LEFTY, -kDead, -32768);
  analog(ImGuiKey_GamepadLStickDown, SDL_GAMEPAD_AXIS_LEFTY, +kDead, +32767);
  analog(ImGuiKey_GamepadRStickLeft, SDL_GAMEPAD_AXIS_RIGHTX, -kDead, -32768);
  analog(ImGuiKey_GamepadRStickRight, SDL_GAMEPAD_AXIS_RIGHTX, +kDead, +32767);
  analog(ImGuiKey_GamepadRStickUp, SDL_GAMEPAD_AXIS_RIGHTY, -kDead, -32768);
  analog(ImGuiKey_GamepadRStickDown, SDL_GAMEPAD_AXIS_RIGHTY, +kDead, +32767);
}

// A controller button held, or (with `keys`) Enter, Space or Esc. Reads the devices' state anew
// (SDL_UpdateGamepads): main_sdl.cpp calls it in loops without an event pump.
inline bool InputHeld(bool keys = true) {
  SDL_PumpEvents();
  SdlPads& p = Pads();
  {
    std::lock_guard lock(p.mutex);
    for (SDL_Gamepad* g : p.pads) {
      if (!g) continue;
      for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        if (SDL_GetGamepadButton(g, SDL_GamepadButton(b))) return true;
      }
      if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 30 * 128 ||
          SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 30 * 128) {
        return true;
      }
    }
  }
  if (keys) {
    const bool* state = SDL_GetKeyboardState(nullptr);
    if (state && (state[SDL_SCANCODE_RETURN] || state[SDL_SCANCODE_SPACE] || state[SDL_SCANCODE_ESCAPE])) return true;
  }
  return false;
}

}  // namespace coreoflight
