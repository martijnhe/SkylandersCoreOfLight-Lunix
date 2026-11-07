#include "platform.h"

#include <chrono>
#include <cstdio>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <openssl/evp.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <ctime>

extern char** environ;
#endif

#include "hub_log.h"

namespace coreoflight::platform {

namespace fs = std::filesystem;

uint64_t TickMs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

void SleepMs(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

namespace {

// A SHA-256 being computed, fed piece by piece.
class Sha256 {
 public:
  Sha256() {
#ifdef _WIN32
    if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) alg_ = nullptr;
    if (alg_ && BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0) != 0) hash_ = nullptr;
    ok_ = hash_ != nullptr;
#else
    ctx_ = EVP_MD_CTX_new();
    ok_ = ctx_ && EVP_DigestInit_ex(ctx_, EVP_sha256(), nullptr) == 1;
#endif
  }
  ~Sha256() {
#ifdef _WIN32
    if (hash_) BCryptDestroyHash(hash_);
    if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
#else
    EVP_MD_CTX_free(ctx_);
#endif
  }
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;

  void Update(const void* data, size_t size) {
    if (!ok_ || !size) return;
#ifdef _WIN32
    ok_ = BCryptHashData(hash_, static_cast<PUCHAR>(const_cast<void*>(data)), ULONG(size), 0) == 0;
#else
    ok_ = EVP_DigestUpdate(ctx_, data, size) == 1;
#endif
  }

  std::string Hex() {
    unsigned char digest[32];
#ifdef _WIN32
    if (!ok_ || BCryptFinishHash(hash_, digest, sizeof(digest), 0) != 0) return {};
#else
    unsigned int n = 0;
    if (!ok_ || EVP_DigestFinal_ex(ctx_, digest, &n) != 1 || n != 32) return {};
#endif
    char hex[65];
    for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    return std::string(hex, 64);
  }

 private:
  bool ok_ = false;
#ifdef _WIN32
  BCRYPT_ALG_HANDLE alg_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
#else
  EVP_MD_CTX* ctx_ = nullptr;
#endif
};

}  // namespace

std::string Sha256Hex(const void* data, size_t size) {
  Sha256 sha;
  sha.Update(data, size);
  return sha.Hex();
}

std::string FileSha256Hex(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return {};
  Sha256 sha;
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), std::streamsize(buffer.size()));
    if (const auto n = in.gcount(); n > 0) sha.Update(buffer.data(), size_t(n));
  }
  return sha.Hex();
}

LocalTime LocalNow() {
  LocalTime t;
  ToLocalTime(NowFileTime(), t);
  return t;
}

#ifdef _WIN32

namespace {

std::wstring Wide(const std::string& text) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(size_t(n > 0 ? n : 0), L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
  return out;
}

// One argument quoted the way CommandLineToArgvW reads it back.
std::wstring Quote(const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
  std::wstring out = L"\"";
  size_t slashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++slashes;
      continue;
    }
    out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    out += c;
  }
  out.append(slashes * 2, L'\\');
  return out + L"\"";
}

}  // namespace

fs::path ExePath() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (n == 0) return {};
    if (n < path.size()) return fs::path(path.substr(0, n));
    path.resize(path.size() * 2);
  }
}

std::string Env(const char* name) {
  const wchar_t* value = _wgetenv(Wide(name).c_str());
  if (!value) return {};
  const std::u8string u8 = fs::path(value).u8string();
  return std::string(u8.begin(), u8.end());
}

fs::path LocalDataDir() {
  const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
  return local ? fs::path(local) / "CoreOfLight" : fs::path();
}

fs::path TempDir() {
  std::error_code ec;
  return fs::temp_directory_path(ec) / "CoreOfLight";
}

fs::path GetUserFolder(UserFolder which) {
  const KNOWNFOLDERID& id = which == UserFolder::kDocuments   ? FOLDERID_Documents
                            : which == UserFolder::kDownloads ? FOLDERID_Downloads
                            : which == UserFolder::kDesktop   ? FOLDERID_Desktop
                                                              : FOLDERID_Profile;
  PWSTR path = nullptr;
  fs::path out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path))) out = path;
  CoTaskMemFree(path);
  return out;
}

std::vector<Drive> Drives() {
  std::vector<Drive> out;
  const DWORD drives = GetLogicalDrives();
  for (int d = 0; d < 26; ++d) {
    if (!(drives & (1u << d))) continue;
    const std::wstring root = std::wstring(1, wchar_t(L'A' + d)) + L":\\";
    wchar_t label[MAX_PATH] = L"";
    GetVolumeInformationW(root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
    const std::u8string a = fs::path(root).u8string(), b = fs::path(label).u8string();
    std::string name(a.begin(), a.end());
    if (label[0]) name += "  " + std::string(b.begin(), b.end());
    out.push_back({fs::path(root), name});
  }
  return out;
}

void* StartProcess(const fs::path& exe, const std::vector<std::string>& args, const fs::path& workdir, bool hidden,
                   std::string* error) {
  std::wstring command = Quote(exe.wstring());
  for (const std::string& a : args) command += L" " + Quote(Wide(a));
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION info = {};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, hidden ? CREATE_NO_WINDOW : 0, nullptr,
                      workdir.empty() ? nullptr : workdir.c_str(), &startup, &info)) {
    if (error) *error = "Windows error " + std::to_string(GetLastError());
    return nullptr;
  }
  CloseHandle(info.hThread);
  return info.hProcess;
}

bool WaitProcess(void* process, uint32_t ms) { return WaitForSingleObject(HANDLE(process), ms) == WAIT_OBJECT_0; }

uint32_t ProcessExitCode(void* process) {
  DWORD code = 0;
  GetExitCodeProcess(HANDLE(process), &code);
  return code;
}

uint32_t ProcessId(void* process) { return GetProcessId(HANDLE(process)); }
void KillProcess(void* process, uint32_t code) { TerminateProcess(HANDLE(process), code); }
void CloseProcess(void* process) {
  if (process) CloseHandle(HANDLE(process));
}

void OpenPath(const fs::path& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void OpenUrl(const std::string& url) {
  ShellExecuteW(nullptr, L"open", Wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
void ShowInFolder(const fs::path& file) {
  const std::wstring select = L"/select,\"" + file.wstring() + L"\"";
  ShellExecuteW(nullptr, L"open", L"explorer.exe", select.c_str(), nullptr, SW_SHOWNORMAL);
}

uint64_t FileWriteTime(const fs::path& path) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
  return uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime;
}

uint64_t NowFileTime() {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  return uint64_t(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
}

bool ToLocalTime(uint64_t file_time, LocalTime& out) {
  FILETIME ft{DWORD(file_time & 0xFFFFFFFF), DWORD(file_time >> 32)};
  SYSTEMTIME utc{}, local{};
  if (!file_time || !FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
    return false;
  }
  out = {local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond, local.wMilliseconds};
  return true;
}

bool MoveToTrash(const fs::path& path) {
  // SHFileOperation wants the paths ended by two null characters.
  std::wstring from = path.wstring();
  from.push_back(L'\0');
  from.push_back(L'\0');
  SHFILEOPSTRUCTW op = {};
  op.wFunc = FO_DELETE;
  op.pFrom = from.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

bool ReplaceFile(const fs::path& from, const fs::path& to) {
  return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool OnSteamDeck() { return false; }  // a Deck with Windows counts as a handheld (handheld.cpp)
bool UnderGamescope() { return false; }
std::string SystemName() { return "Windows " + WindowsVersion(); }

#else  // Linux

namespace {

struct Child {
  pid_t pid = -1;
  bool done = false;
  uint32_t code = 0;
};

std::string ReadLine(const fs::path& file) {
  std::ifstream in(file);
  std::string line;
  std::getline(in, line);
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) line.pop_back();
  return line;
}

fs::path Home() {
  const char* home = std::getenv("HOME");
  return home && *home ? fs::path(home) : fs::path(".");
}

// A helper program started and left alone (xdg-open): reaped on a thread of its own.
void Detached(const std::vector<std::string>& argv) {
  if (void* p = StartProcess(argv[0], {argv.begin() + 1, argv.end()}, {}, true)) {
    std::thread([p] {
      WaitProcess(p, UINT32_MAX);
      CloseProcess(p);
    }).detach();
  }
}

}  // namespace

fs::path ExePath() {
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec);
}

fs::path GetUserFolder(UserFolder which) {
  const fs::path home = Home();
  if (which == UserFolder::kHome) return home;
  const char* key = which == UserFolder::kDocuments   ? "XDG_DOCUMENTS_DIR"
                    : which == UserFolder::kDownloads ? "XDG_DOWNLOAD_DIR"
                                                      : "XDG_DESKTOP_DIR";
  const char* fallback = which == UserFolder::kDocuments ? "Documents" : which == UserFolder::kDownloads ? "Downloads" : "Desktop";
  // user-dirs.dirs: XDG_DOWNLOAD_DIR="$HOME/Downloads"
  const char* config = std::getenv("XDG_CONFIG_HOME");
  std::ifstream in((config && *config ? fs::path(config) : home / ".config") / "user-dirs.dirs");
  for (std::string line; std::getline(in, line);) {
    if (line.rfind(key, 0) != 0 || line.size() <= std::strlen(key) + 1 || line[std::strlen(key)] != '=') continue;
    std::string value = line.substr(std::strlen(key) + 1);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
    if (value.rfind("$HOME", 0) == 0) value = home.string() + value.substr(5);
    std::error_code ec;
    if (!value.empty() && value != home.string() && fs::is_directory(value, ec)) return value;
  }
  std::error_code ec;
  return fs::is_directory(home / fallback, ec) ? home / fallback : fs::path();
}

std::vector<Drive> Drives() {
  std::vector<Drive> out = {{"/", "/"}};
  const std::string user = Env("USER");
  std::vector<fs::path> bases = {"/run/media", "/media", "/mnt"};
  if (!user.empty()) {
    bases.push_back(fs::path("/run/media") / user);
    bases.push_back(fs::path("/media") / user);
  }
  for (const fs::path& base : bases) {
    std::error_code ec;
    for (auto it = fs::directory_iterator(base, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      std::error_code type_ec;
      if (!it->is_directory(type_ec) || (!user.empty() && it->path().filename() == user)) continue;
      out.push_back({it->path(), it->path().string()});
    }
  }
  return out;
}

std::string Env(const char* name) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
}

fs::path LocalDataDir() {
  const char* data = std::getenv("XDG_DATA_HOME");
  return (data && *data ? fs::path(data) : Home() / ".local" / "share") / "CoreOfLight";
}

fs::path TempDir() {
  std::error_code ec;
  return fs::temp_directory_path(ec) / "CoreOfLight";
}

void* StartProcess(const fs::path& exe, const std::vector<std::string>& args, const fs::path& workdir, bool hidden,
                   std::string* error) {
  std::vector<std::string> argv_text;
  argv_text.push_back(exe.string());
  argv_text.insert(argv_text.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (std::string& a : argv_text) argv.push_back(a.data());
  argv.push_back(nullptr);
  // fork + chdir + exec: posix_spawn's chdir action is too new for older glibc (SteamOS 3's).
  int pipe_fds[2];
  if (pipe2(pipe_fds, O_CLOEXEC) != 0) {
    if (error) *error = std::strerror(errno);
    return nullptr;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    if (error) *error = std::strerror(errno);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return nullptr;
  }
  if (pid == 0) {
    setsid();  // its own session: the launcher's terminal signals don't reach it
    if (hidden) {
      const int null = open("/dev/null", O_RDWR);
      if (null >= 0) {
        dup2(null, 0);
        dup2(null, 1);
        dup2(null, 2);
      }
    }
    if (!workdir.empty() && chdir(workdir.c_str()) != 0) {
      const int e = errno;
      (void)!write(pipe_fds[1], &e, sizeof(e));
      _exit(127);
    }
    execv(argv[0], argv.data());
    const int e = errno;
    (void)!write(pipe_fds[1], &e, sizeof(e));
    _exit(127);
  }
  close(pipe_fds[1]);
  int child_errno = 0;
  const ssize_t n = read(pipe_fds[0], &child_errno, sizeof(child_errno));  // closed by exec: 0 bytes
  close(pipe_fds[0]);
  if (n == sizeof(child_errno)) {
    waitpid(pid, nullptr, 0);
    if (error) *error = std::strerror(child_errno);
    return nullptr;
  }
  return new Child{pid};
}

bool WaitProcess(void* process, uint32_t ms) {
  Child* c = static_cast<Child*>(process);
  if (!c) return true;
  const uint64_t until = ms == UINT32_MAX ? UINT64_MAX : TickMs() + ms;
  while (!c->done) {
    int status = 0;
    const pid_t r = waitpid(c->pid, &status, WNOHANG);
    if (r == c->pid) {
      c->done = true;
      c->code = WIFEXITED(status) ? uint32_t(WEXITSTATUS(status)) : WIFSIGNALED(status) ? 128u + WTERMSIG(status) : 0u;
      break;
    }
    if (r < 0) {  // not our child any more
      c->done = true;
      break;
    }
    if (TickMs() >= until) return false;
    SleepMs(std::min<uint64_t>(10, until - TickMs()));
  }
  return true;
}

uint32_t ProcessExitCode(void* process) {
  Child* c = static_cast<Child*>(process);
  return c && c->done ? c->code : 0;
}

uint32_t ProcessId(void* process) { return process ? uint32_t(static_cast<Child*>(process)->pid) : 0; }

void KillProcess(void* process, uint32_t code) {
  Child* c = static_cast<Child*>(process);
  if (!c || c->done) return;
  kill(c->pid, SIGKILL);
  WaitProcess(c, 2000);
  c->code = code;
}

void CloseProcess(void* process) {
  Child* c = static_cast<Child*>(process);
  if (!c) return;
  if (!c->done) {
    // Still running: reaped later on a thread, so it leaves no zombie behind.
    const pid_t pid = c->pid;
    std::thread([pid] { waitpid(pid, nullptr, 0); }).detach();
  }
  delete c;
}

void OpenPath(const fs::path& path) { Detached({"/usr/bin/xdg-open", path.string()}); }
void OpenUrl(const std::string& url) { Detached({"/usr/bin/xdg-open", url}); }
void ShowInFolder(const fs::path& file) {
  // The freedesktop file manager interface chooses the file; without it the folder opens.
  std::error_code ec;
  if (fs::exists("/usr/bin/dbus-send", ec)) {
    const std::u8string u8 = fs::absolute(file, ec).u8string();
    std::string uri = "file://";
    for (const char8_t ch : u8) {
      const unsigned char c = static_cast<unsigned char>(ch);
      if (std::isalnum(c) || std::strchr("/-_.~", c)) {
        uri += char(c);
      } else {
        char hex[4];
        std::snprintf(hex, sizeof(hex), "%%%02X", c);
        uri += hex;
      }
    }
    Detached({"/usr/bin/dbus-send", "--session", "--dest=org.freedesktop.FileManager1", "--type=method_call",
              "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1.ShowItems", "array:string:" + uri,
              "string:"});
    return;
  }
  OpenPath(file.parent_path());
}

// 100 ns steps between 1601-01-01 and 1970-01-01.
constexpr uint64_t kUnixEpoch = 116444736000000000ULL;

uint64_t FileWriteTime(const fs::path& path) {
  struct stat st{};
  if (stat(path.c_str(), &st) != 0) return 0;
  return kUnixEpoch + uint64_t(st.st_mtim.tv_sec) * 10000000ULL + uint64_t(st.st_mtim.tv_nsec) / 100;
}

uint64_t NowFileTime() {
  timespec now{};
  clock_gettime(CLOCK_REALTIME, &now);
  return kUnixEpoch + uint64_t(now.tv_sec) * 10000000ULL + uint64_t(now.tv_nsec) / 100;
}

bool ToLocalTime(uint64_t file_time, LocalTime& out) {
  if (file_time < kUnixEpoch) return false;
  const time_t seconds = time_t((file_time - kUnixEpoch) / 10000000ULL);
  tm t{};
  if (!localtime_r(&seconds, &t)) return false;
  out = {t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
         int((file_time - kUnixEpoch) / 10000ULL % 1000)};
  return true;
}

// The freedesktop.org trash: the file into $XDG_DATA_HOME/Trash/files, where it came from into
// Trash/info/<name>.trashinfo, so the file manager can put it back. Only on the home folder's file
// system (a rename); elsewhere false and the caller deletes or keeps it.
bool MoveToTrash(const fs::path& path) {
  std::error_code ec;
  const char* data = std::getenv("XDG_DATA_HOME");
  const fs::path trash = (data && *data ? fs::path(data) : Home() / ".local" / "share") / "Trash";
  fs::create_directories(trash / "files", ec);
  fs::create_directories(trash / "info", ec);
  const std::string base = path.filename().string();
  std::string name = base;
  for (int n = 2; fs::exists(trash / "files" / name, ec) || fs::exists(trash / "info" / (name + ".trashinfo"), ec); ++n) {
    name = base + "." + std::to_string(n);
  }
  const fs::path info = trash / "info" / (name + ".trashinfo");
  {
    std::ofstream out(info);
    LocalTime t;
    ToLocalTime(NowFileTime(), t);
    char date[32];
    std::snprintf(date, sizeof(date), "%04d-%02d-%02dT%02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute,
                  t.second);
    std::string encoded;
    for (const unsigned char c : fs::absolute(path, ec).string()) {
      if (std::isalnum(c) || std::strchr("/-_.~", c)) {
        encoded += char(c);
      } else {
        char hex[4];
        std::snprintf(hex, sizeof(hex), "%%%02X", c);
        encoded += hex;
      }
    }
    out << "[Trash Info]\nPath=" << encoded << "\nDeletionDate=" << date << "\n";
    if (!out) return false;
  }
  fs::rename(path, trash / "files" / name, ec);
  if (ec) {
    fs::remove(info, ec);
    return false;
  }
  return true;
}

bool ReplaceFile(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::rename(from, to, ec);
  return !ec;
}

bool PickWithDialog(bool folder, const std::string& title, const std::string& pattern, fs::path& out) {
  if (UnderGamescope()) return false;
  std::error_code ec;
  std::vector<std::string> args;
  fs::path program;
  if (fs::exists("/usr/bin/zenity", ec)) {
    program = "/usr/bin/zenity";
    args = {"--file-selection", "--title=" + title};
    if (folder) args.push_back("--directory");
    if (!pattern.empty()) args.push_back("--file-filter=" + pattern + " | " + pattern);
  } else if (fs::exists("/usr/bin/kdialog", ec)) {
    program = "/usr/bin/kdialog";
    args = {"--title", title, folder ? "--getexistingdirectory" : "--getopenfilename",
            GetUserFolder(UserFolder::kHome).string()};
    if (!folder && !pattern.empty()) args.push_back(pattern);
  } else {
    return false;
  }
  // Its answer (the path, one line) through a pipe on its stdout.
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) return false;
  std::vector<std::string> argv_text = {program.string()};
  argv_text.insert(argv_text.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (std::string& a : argv_text) argv.push_back(a.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
  pid_t pid = -1;
  const int spawned = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(fds[1]);
  std::string text;
  if (spawned == 0) {
    char buffer[4096];
    for (ssize_t n; (n = read(fds[0], buffer, sizeof(buffer))) > 0;) text.append(buffer, size_t(n));
  }
  close(fds[0]);
  int status = 0;
  if (spawned == 0) waitpid(pid, &status, 0);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  if (spawned != 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || text.empty()) return false;
  out = fs::path(text);
  return true;
}

bool OnSteamDeck() {
  static const bool deck = [] {
    if (Env("SteamDeck") == "1") return true;
    const std::string board = ReadLine("/sys/class/dmi/id/board_name");
    return board == "Jupiter" || board == "Galileo";
  }();
  return deck;
}

bool UnderGamescope() {
  return !Env("GAMESCOPE_WAYLAND_DISPLAY").empty() || Env("XDG_CURRENT_DESKTOP") == "gamescope";
}

std::string SystemName() {
  std::string name = "Linux";
  std::ifstream os("/etc/os-release");
  for (std::string line; std::getline(os, line);) {
    if (line.rfind("PRETTY_NAME=", 0) == 0) {
      name = line.substr(12);
      if (name.size() >= 2 && name.front() == '"' && name.back() == '"') name = name.substr(1, name.size() - 2);
      break;
    }
  }
  utsname u{};
  if (uname(&u) == 0) name += std::string(", kernel ") + u.release;
  return name;
}

#endif

}  // namespace coreoflight::platform
