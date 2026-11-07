// Skylanders: Core of Light off Windows: the launcher window with SDL3 + OpenGL 3 + Dear ImGui (as
// Dear ImGui's own SDL3 / OpenGL 3 example sets them up). The same steps as main.cpp, the Windows
// one; the screen itself is launcher_ui.cpp.
//
// What differs: a game's window can't be looked for in another process (Wayland gives no list of
// windows), so the launcher hides once the game has run for a moment or took the focus, and comes
// back when the game's process ends. On a Steam Deck (and under gamescope anywhere) the launcher
// starts in fullscreen: there is one window on the screen at a time.

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "gpu_tier.h"
#include "hints.h"
#include "hub_log.h"
#include "hub_settings.h"
#include "launcher_ui.h"
#include "pad_state.h"
#include "platform.h"
#include "portable.h"
#include "portal/data_folder.h"
#include "textures.h"

namespace {

namespace fs = std::filesystem;
using coreoflight::platform::TickMs;

coreoflight::LauncherUi* g_ui = nullptr;

// While a game runs and for 30 s after it: the window's focus goes into the log (main.cpp).
bool g_game_running = false;
uint64_t g_focus_log_until = 0;

fs::path LauncherDir() { return coreoflight::platform::ExePath().parent_path(); }

// The launcher's own fonts (Noto Sans, SIL OFL) next to it: there is no Segoe UI off Windows.
std::string g_fonts;

ImFont* LoadFont(ImGuiIO& io, const char* own_file) {
  for (const fs::path& path : {LauncherDir() / "fonts" / own_file, fs::path("/usr/share/fonts/noto") / own_file}) {
    std::error_code ec;
    if (!fs::exists(path, ec)) continue;
    if (ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), 32.0f)) {
      g_fonts += (g_fonts.empty() ? "" : " ") + path.filename().string();
      return font;
    }
  }
  g_fonts += g_fonts.empty() ? "none (Dear ImGui's own)" : " none";
  return nullptr;
}

void LogTerminate() {
  std::string what = "unknown";
  if (const std::exception_ptr e = std::current_exception()) {
    try {
      std::rethrow_exception(e);
    } catch (const std::exception& ex) {
      what = ex.what();
    } catch (...) {
    }
  }
  coreoflight::HubError("launcher: ended by an uncaught exception: " + what);
  std::abort();
}

// A fault says so in the log (its signal), then ends the launcher as it would have.
void LogFault(int sig) {
  coreoflight::HubError("launcher: crashed, signal " + std::to_string(sig));
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}

void ApplyFullscreen(SDL_Window* window, bool on) {
  const bool is_full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
  if (on != is_full) SDL_SetWindowFullscreen(window, on);  // borderless on the window's display
}

}  // namespace

int main(int argc, char** argv) {
  std::set_terminate(LogTerminate);
  for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) std::signal(sig, LogFault);
  // A game started by the launcher that ends doesn't take the launcher with it.
  std::signal(SIGPIPE, SIG_IGN);

  // The command line as main.cpp reads it.
  fs::path iso;
  fs::path portable_update;
  bool fullscreen_arg = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.starts_with("--documents=")) {
      coreoflight::SetDocumentsDir(arg.substr(12));
    } else if (arg.starts_with("--install_dir=")) {
      coreoflight::SetInstallDir(arg.substr(14));
    } else if (arg == "--no_game_art") {
      coreoflight::SetNoGameArt(true);
      coreoflight::KeepControllerHints();
    } else if (arg == "--own_font") {
      // The launcher's own font is the only one here.
    } else if (arg == "--offline") {
      coreoflight::SetOffline(true);
    } else if (arg == "--hint_pad=playstation" || arg == "--hint_pad=nintendo") {
      coreoflight::ForceHintPad(arg.ends_with("playstation") ? coreoflight::HintPad::kPlayStation
                                                             : coreoflight::HintPad::kNintendo);
    } else if (arg.starts_with("--portable_update=")) {
      portable_update = arg.substr(18);
    } else if (arg == "--fullscreen") {
      fullscreen_arg = true;
    } else if (iso.empty()) {
      iso = arg;
    }
  }
  if (!portable_update.empty()) {
    const fs::path exe = coreoflight::platform::ExePath();
    std::string why = "not a portable launcher (no portable.txt next to it)";
    const bool done = coreoflight::IsPortable(exe.parent_path()) &&
                      coreoflight::ApplyPortableUpdate(portable_update, exe.parent_path(), exe.filename(), false, why) &&
                      coreoflight::StartLauncher(exe);
    coreoflight::HubLog(done ? "portable update test: done, the new launcher started" : "portable update test: " + why);
    return done ? 0 : 1;
  }

  // The pads are read in the background too: the press that brings the launcher back after a game
  // comes while its window isn't the focused one.
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_APP_NAME, "Skylanders: Core of Light");
  SDL_SetHint(SDL_HINT_APP_ID, "coreoflight");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    coreoflight::HubError(std::string("launcher: SDL_Init failed: ") + SDL_GetError());
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

  const float dpi = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  const float scale = dpi > 0 ? dpi : 1.0f;
  // A Steam Deck or a gamescope session: fullscreen whatever the setting says (one window on the
  // screen at a time there, and the Deck's game mode has no window frame to make it bigger).
  const bool console_like = coreoflight::platform::OnSteamDeck() || coreoflight::platform::UnderGamescope();
  const bool start_fullscreen = fullscreen_arg || console_like || coreoflight::LoadHubSettings().fullscreen;
  SDL_Window* window = SDL_CreateWindow("Skylanders: Core of Light", int(1280 * scale), int(720 * scale),
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                            SDL_WINDOW_HIDDEN | (start_fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
  if (!window) {
    coreoflight::HubError(std::string("launcher: no window: ") + SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) {
    coreoflight::HubError(std::string("launcher: no OpenGL 3.2 context: ") + SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_GL_MakeCurrent(window, gl);
  SDL_GL_SetSwapInterval(1);
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
  ImGui_ImplSDL3_InitForOpenGL(window, gl);
  // The pads are read by pad_state_sdl.h, all of them merged: the backend's own reading stays off.
  ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
  ImGui_ImplOpenGL3_Init("#version 150");
  ImFont* regular = LoadFont(io, "NotoSans-Regular.ttf");
  ImFont* bold = LoadFont(io, "NotoSans-Bold.ttf");
  if (!regular) regular = io.Fonts->AddFontDefault();
  if (!bold) bold = regular;

  {
    char offset[16] = "";
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::strftime(offset, sizeof(offset), "UTC%z", &local);
    coreoflight::HubLog("launcher: start, " COREOFLIGHT_VERSION ", " + coreoflight::platform::SystemName() +
                        (coreoflight::platform::OnSteamDeck() ? ", Steam Deck" : "") +
                        (coreoflight::platform::UnderGamescope() ? ", gamescope" : "") + ", video " +
                        SDL_GetCurrentVideoDriver() + (start_fullscreen ? ", fullscreen" : ", window") +
                        ", time zone " + offset + ", fonts " + g_fonts);
    coreoflight::DetectGpu();
    std::error_code ec;
    coreoflight::HubLog(std::string("data folder: ") +
                        (!skylanders::PortableFolder().empty() ? "portable, next to the launcher"
                         : fs::equivalent(skylanders::DataFolder(), skylanders::DefaultDataFolder(), ec) ||
                                   skylanders::DataFolder() == skylanders::DefaultDataFolder()
                               ? "standard"
                               : "moved"));
  }
  // On the heap, so it goes before SDL does (below): its music and textures need SDL and GL alive.
  auto ui_owner = std::make_unique<coreoflight::LauncherUi>();
  coreoflight::LauncherUi& ui = *ui_owner;
  ui.Init(LauncherDir(), regular, bold);
  if (fullscreen_arg || console_like) ui.StartFullscreen();
  g_ui = &ui;
  if (!iso.empty()) ui.AddDisc(iso);

  bool running = true;
  void* game = nullptr;
  bool game_shown = false;        // the game is up and the launcher hidden
  uint64_t game_since = 0;        // when the game started
  bool focus_lost = false;        // the launcher lost the focus since the game started (its window came)
  uint64_t took_back_until = 0;   // after a game: until then a pad press brings the launcher to the front
  bool input_logged = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if ((event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) &&
          (g_game_running || TickMs() < g_focus_log_until)) {
        coreoflight::HubLog(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ? "window: focus" : "window: focus lost");
      }
      if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && game) focus_lost = true;
      ImGui_ImplSDL3_ProcessEvent(&event);
      switch (event.type) {
        case SDL_EVENT_QUIT:
          running = false;
          break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
          if (event.window.windowID == SDL_GetWindowID(window)) running = false;
          break;
        case SDL_EVENT_KEY_DOWN:
          // F11 and Alt+Enter: fullscreen on / off, as in main.cpp.
          if (!event.key.repeat && (event.key.key == SDLK_F11 ||
                                    (event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT)))) {
            ui.ToggleFullscreen();
          }
          break;
        case SDL_EVENT_DROP_FILE:
          // An ISO dropped on the window: set up like one picked with X.
          if (event.drop.data) ui.AddDisc(fs::path(event.drop.data));
          break;
        default:
          break;
      }
    }
    if (!running) break;
    if (game && !game_shown) {
      // The launch screen until the game is up: it took the focus, ran two seconds, or ended at
      // once (or 30 s passed).
      const bool ended = coreoflight::platform::WaitProcess(game, 0);
      const uint64_t ran = TickMs() - game_since;
      if (focus_lost || ran > 2000 || ended) {
        game_shown = true;
        coreoflight::HubLog(std::string("game: ") + (ended ? "ended at once" : focus_lost ? "took the focus" : "running") +
                            ", launcher hidden");
        ui.GameWindowShown();
        SDL_HideWindow(window);
      }
    }
    if (game && game_shown) {
      // The launcher waits hidden and comes back when the game's process ends.
      if (!coreoflight::platform::WaitProcess(game, 250)) {
        ui.GameRunning();
        continue;
      }
      const uint32_t code = coreoflight::platform::ProcessExitCode(game);
      coreoflight::HubLog("game: ended, exit code " + std::to_string(code));
      coreoflight::platform::CloseProcess(game);
      game = nullptr;
      game_shown = false;
      g_game_running = false;
      g_focus_log_until = TickMs() + 30000;
      ui.GameEnded();
      ui.GameExitCode(code);
      ui.Refresh();
      SDL_ShowWindow(window);
      SDL_RestoreWindow(window);
      SDL_RaiseWindow(window);
      input_logged = false;
      took_back_until = TickMs() + 15000;
      // A button still held from the game must not count as a press here (main.cpp).
      for (int i = 0; i < 120 && coreoflight::InputHeld(); ++i) coreoflight::platform::SleepMs(25);
      ImGui::GetIO().ClearEventsQueue();
      ImGui::GetIO().ClearInputKeys();
    }
    const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (took_back_until && TickMs() < took_back_until && !focused && coreoflight::InputHeld(/*keys=*/false)) {
      SDL_RaiseWindow(window);
    }
    if (!input_logged && took_back_until && coreoflight::InputHeld()) {
      input_logged = true;
      coreoflight::HubLog(std::string("launcher: first input after the game, in front ") + (focused ? "yes" : "no"));
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(50);
      continue;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    coreoflight::UpdatePads(io);
    coreoflight::KeepClickedButton(io);
    ApplyFullscreen(window, ui.fullscreen());
    ImGui::NewFrame();
    coreoflight::UpdateHintInput();
    if (!ui.Draw(io.DisplaySize.x, io.DisplaySize.y)) running = false;
    ImGui::Render();
    glViewport(0, 0, int(io.DisplaySize.x * io.DisplayFramebufferScale.x),
               int(io.DisplaySize.y * io.DisplayFramebufferScale.y));
    glClearColor(0.01f, 0.03f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
    if (void* started = ui.TakeStartedProcess()) {
      game = started;
      game_shown = false;
      game_since = TickMs();
      focus_lost = false;
      g_game_running = true;
      coreoflight::HubLog("game: started, process " + std::to_string(coreoflight::platform::ProcessId(game)));
    }
  }

  // The launcher first (its title music is an SDL audio stream), then the pads and the textures,
  // then SDL: torn down the other way round, closing the music after SDL_Quit hung the process
  // with its window already gone (the owner's PC, 2026-11-06: a launcher left running unseen).
  g_ui = nullptr;
  ui_owner.reset();
  coreoflight::ClosePads();
  coreoflight::ReleaseTextures();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DestroyContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
