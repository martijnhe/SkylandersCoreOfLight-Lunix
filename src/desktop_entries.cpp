#include "desktop_entries.h"

#ifndef _WIN32

#include <fstream>
#include <iterator>
#include <sstream>

#include "hub_log.h"
#include "platform.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

// Every entry the launcher writes is named so, and only those are ever changed or removed.
constexpr char kPrefix[] = "coreoflight";

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(u8.begin(), u8.end());
}

fs::path ApplicationsDir() {
  const std::string data = platform::Env("XDG_DATA_HOME");
  const fs::path base = data.empty() ? platform::GetUserFolder(platform::UserFolder::kHome) / ".local" / "share"
                                     : fs::path(data);
  return base / "applications";
}

// A value as the format wants it: no line breaks, a backslash doubled.
std::string Value(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (c == '\n' || c == '\r') continue;
    if (c == '\\') out += '\\';
    out += c;
  }
  return out;
}

// A program for Exec=: quoted, ", `, $ and \ escaped inside the quotes, % doubled (field codes).
std::string ExecArg(const fs::path& program) {
  std::string out = "\"";
  for (const char c : Utf8(program)) {
    if (c == '"' || c == '`' || c == '$' || c == '\\') out += '\\';
    if (c == '%') out += '%';
    out += c;
  }
  return out + "\"";
}

std::string Entry(const std::string& name, const std::string& comment, const fs::path& program, const fs::path& dir,
                  const fs::path& icon) {
  std::ostringstream out;
  out << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Name=" << Value(name) << "\n"
      << "Comment=" << Value(comment) << "\n"
      // Exec is unquoted once more by the reader (the value's own backslash rule): doubled again.
      << "Exec=" << Value(ExecArg(program)) << "\n"
      << "Path=" << Value(Utf8(dir)) << "\n";
  if (!icon.empty()) out << "Icon=" << Value(Utf8(icon)) << "\n";
  out << "Terminal=false\n"
      << "Categories=Game;\n";
  return out.str();
}

// Writes `file` when its text differs (a menu that watches the folder reloads on every change).
void Put(const fs::path& file, const std::string& text) {
  std::ifstream in(file, std::ios::binary);
  const std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (old == text) return;
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  fs::path temp = file;
  temp += ".new";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) return;
  }
  if (platform::ReplaceFile(temp, file)) HubLog("desktop entry: " + Utf8(file.filename()) + (old.empty() ? " added" : " updated"));
}

void Remove(const fs::path& file) {
  std::error_code ec;
  if (fs::remove(file, ec)) HubLog("desktop entry: " + Utf8(file.filename()) + " removed");
}

}  // namespace

void UpdateDesktopEntries(const std::vector<Found>& found, const std::vector<std::string>& installed_versions,
                          const fs::path& launcher_dir, bool packaged) {
  if (DocumentsMoved()) return;
  const fs::path apps = ApplicationsDir();
  std::error_code ec;
  // The launcher's icon (package_linux.sh puts it next to the program).
  fs::path launcher_icon = launcher_dir / "coreoflight.png";
  if (!fs::is_regular_file(launcher_icon, ec)) launcher_icon.clear();
  if (packaged) {
    Put(apps / (std::string(kPrefix) + ".desktop"),
        Entry("Skylanders: Core of Light", "The launcher for the recompiled Skylanders games", platform::ExePath(),
              launcher_dir, launcher_icon));
  }
  const auto& games = AllGames();
  for (size_t i = 0; i < games.size(); ++i) {
    const fs::path file = apps / (std::string(kPrefix) + "-" + games[i].key + ".desktop");
    const bool installed = i < found.size() && !found[i].exe.empty() && i < installed_versions.size() &&
                           !installed_versions[i].empty();
    if (!installed) {
      Remove(file);
      continue;
    }
    // The game's hero as the game left it (its picture export), else the launcher's icon.
    fs::path icon = FigurePicture(games[i].hero_id, games[i].hero_variant);
    if (icon.empty()) icon = launcher_icon;
    Put(file, Entry(std::string("Skylanders: ") + games[i].name_en, games[i].app_name, found[i].exe, found[i].workdir,
                    icon));
  }
}

}  // namespace coreoflight

#else

namespace coreoflight {
// Windows: the games' setups make their own Start menu entries.
void UpdateDesktopEntries(const std::vector<Found>&, const std::vector<std::string>&, const std::filesystem::path&,
                          bool) {}
}  // namespace coreoflight

#endif
