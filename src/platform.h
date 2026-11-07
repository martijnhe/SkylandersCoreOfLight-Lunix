#pragma once

// What differs between Windows and Linux under the launcher's screens (platform.cpp): processes,
// the clock, the folders the system keeps for a program, opening a file or a link, the machine.
// A process is a void* as everywhere in the launcher: a HANDLE on Windows, an own record on Linux.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace coreoflight::platform {

// Milliseconds since some fixed point (GetTickCount64 on Windows).
uint64_t TickMs();
void SleepMs(uint32_t ms);

// The launcher's own program file.
std::filesystem::path ExePath();
// The folder for the launcher's own files that aren't the player's (the log, the releases it
// knows): %LOCALAPPDATA%\CoreOfLight, on Linux $XDG_DATA_HOME/CoreOfLight (~/.local/share).
std::filesystem::path LocalDataDir();
// The temp folder's CoreOfLight folder (setups' logs, downloads, the exports' user data).
std::filesystem::path TempDir();
// An environment variable as UTF-8, empty if not set.
std::string Env(const char* name);

// The user's own folders: Windows' known folders, on Linux the desktop's (XDG user dirs, as
// ~/.config/user-dirs.dirs names them, else ~/Documents and so on). Empty if there is none.
enum class UserFolder { kDocuments, kDownloads, kDesktop, kHome };
std::filesystem::path GetUserFolder(UserFolder which);
// The places a file picker offers besides those: Windows' drive letters; on Linux /, the mounted
// drives under /run/media and /media (a Steam Deck's microSD card). Each with a name to show.
struct Drive {
  std::filesystem::path root;
  std::string name;
};
std::vector<Drive> Drives();

// Starts `exe` with `args` in `workdir` (empty: the current one). `hidden`: no console window on
// Windows; on Linux its output goes nowhere. Null and `error` when it couldn't start.
void* StartProcess(const std::filesystem::path& exe, const std::vector<std::string>& args,
                   const std::filesystem::path& workdir, bool hidden, std::string* error = nullptr);
// Waits up to `ms` (0: just looks): true once the process has ended.
bool WaitProcess(void* process, uint32_t ms);
// Its exit code once it ended (a signal on Linux as 128 + its number).
uint32_t ProcessExitCode(void* process);
uint32_t ProcessId(void* process);
void KillProcess(void* process, uint32_t code);
// Forgets the process (it keeps running if it still does).
void CloseProcess(void* process);

// Times as the launcher keeps them (in its files too): Windows file times, 100 ns steps since
// 1601-01-01 UTC, on Linux as well. A file's last write (0 if it can't be read), now, and a time
// in the local time zone.
uint64_t FileWriteTime(const std::filesystem::path& path);
uint64_t NowFileTime();
struct LocalTime {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, ms = 0;
};
bool ToLocalTime(uint64_t file_time, LocalTime& out);
LocalTime LocalNow();

// Moves a file or folder to the system's recycle bin / trash; false if it couldn't.
bool MoveToTrash(const std::filesystem::path& path);
// Puts `from` in the place of `to` in one step (a file written beside it first).
bool ReplaceFile(const std::filesystem::path& from, const std::filesystem::path& to);

// SHA-256 as lower-case hex (Windows' CNG, OpenSSL on Linux); empty if the file can't be read.
std::string Sha256Hex(const void* data, size_t size);
std::string FileSha256Hex(const std::filesystem::path& file);

#ifndef _WIN32
// The desktop's own file dialog (zenity, else kdialog): a file matching `pattern` ("*.iso"), or a
// folder; false when there is no such program, under gamescope (no desktop), or nothing was chosen.
bool PickWithDialog(bool folder, const std::string& title, const std::string& pattern, std::filesystem::path& out);
#endif

// Opens a folder in the file manager, a file with its program, or a link in the browser.
void OpenPath(const std::filesystem::path& path);
void OpenUrl(const std::string& url);
// The file manager on the folder of `file` (with it chosen where the system can).
void ShowInFolder(const std::filesystem::path& file);

// Whether this is a Steam Deck (SteamOS sets SteamDeck=1 for programs Steam starts; the board's name
// in DMI says it otherwise: Jupiter the LCD model, Galileo the OLED one).
bool OnSteamDeck();
// Whether Steam's gamescope session draws the screen (the Deck's game mode, Bazzite's): one
// fullscreen window at a time, no window manager.
bool UnderGamescope();
// The system for the log's start line: "Windows 10.0.26200" or the Linux distribution and kernel.
std::string SystemName();

}  // namespace coreoflight::platform
