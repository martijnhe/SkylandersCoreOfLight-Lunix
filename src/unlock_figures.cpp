#include "unlock_figures.h"

#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "hub_log.h"
#include "platform.h"
#include "portal/figure_catalog.h"
#include "portal/figure_file.h"

namespace coreoflight {
namespace {

namespace fs = std::filesystem;
namespace sp = skylanders::portal;

constexpr char kList[] = ".coreoflight-unlocked.txt";

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(u8.begin(), u8.end());
}

fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool IsFigureFile(const fs::path& p) {
  const fs::path ext = p.extension();
  return ext == ".dump" || ext == ".sky" || ext == ".bin";
}

// A file name without the characters a file system refuses.
std::string SafeName(std::string_view name) {
  std::string out;
  for (const char c : name) out += std::string_view("<>:\"/\\|?*").find(c) == std::string_view::npos ? c : '_';
  while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
  return out.empty() ? std::string("figure") : out;
}

}  // namespace

bool FiguresUnlocked(const fs::path& figures_dir) {
  std::error_code ec;
  return fs::is_regular_file(figures_dir / kList, ec);
}

int UnlockAllFigures(const fs::path& figures_dir) {
  std::error_code ec;
  fs::create_directories(figures_dir, ec);
  // What is there already, by id and variant (the owner's own files and an earlier unlock's).
  std::set<std::pair<uint16_t, uint16_t>> have;
  for (fs::recursive_directory_iterator it(figures_dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code type_ec;
    if (!it->is_regular_file(type_ec) || !IsFigureFile(it->path())) continue;
    if (const auto data = sp::LoadFigureFile(it->path())) have.emplace(sp::ReadFigureId(*data), sp::ReadFigureVariant(*data));
  }
  std::ofstream list(figures_dir / kList, std::ios::binary | std::ios::app);
  if (!list) return -1;
  int made = 0;
  for (const sp::SkylanderInfo& info : sp::AllSkylanders()) {
    if (info.game == "Imaginators" || info.category == "Unreleased") continue;
    if (!have.emplace(info.id, info.variant).second) continue;
    const fs::path dir = figures_dir / FromUtf8(std::string(info.game));
    fs::create_directories(dir, ec);
    const fs::path file = sp::UniqueFigurePath(dir, SafeName(info.name));
    const sp::FigureData data = sp::CreateBlankFigure(info.id, info.variant);
    if (!sp::SaveFigureFileAtomic(file, data)) continue;
    list << platform::Sha256Hex(data.data(), data.size()) << '\t' << Utf8(fs::relative(file, figures_dir, ec)) << '\n';
    ++made;
  }
  list.flush();
  HubLog("figures: unlocked all, " + std::to_string(made) + " new blank figures in " + Utf8(figures_dir));
  return made;
}

int RelockFigures(const fs::path& figures_dir) {
  std::ifstream in(figures_dir / kList, std::ios::binary);
  int removed = 0, kept = 0;
  std::error_code ec;
  for (std::string line; std::getline(in, line);) {
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    const fs::path file = figures_dir / FromUtf8(line.substr(tab + 1));
    if (!fs::is_regular_file(file, ec)) continue;
    // Only a file still exactly as made: one a game wrote progress onto stays.
    if (platform::FileSha256Hex(file) != line.substr(0, tab)) {
      ++kept;
      continue;
    }
    if (fs::remove(file, ec)) ++removed;
  }
  in.close();
  fs::remove(figures_dir / kList, ec);
  // The game folders the unlock made, if nothing else is in them.
  for (fs::directory_iterator it(figures_dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code dir_ec;
    if (it->is_directory(dir_ec) && fs::is_empty(it->path(), dir_ec)) fs::remove(it->path(), dir_ec);
  }
  HubLog("figures: locked again, " + std::to_string(removed) + " blank figures removed, " + std::to_string(kept) +
         " played ones kept");
  return removed;
}

}  // namespace coreoflight
