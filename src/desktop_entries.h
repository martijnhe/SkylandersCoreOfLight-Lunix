#pragma once

// The launcher and the games it installed in the desktop's application menu (Linux only; the
// owner's wish, 2026-11-06, for the Caelestia shell's launcher): freedesktop.org desktop entries in
// ~/.local/share/applications, which Caelestia, KDE (the Steam Deck's desktop mode), GNOME and the
// others all read. Each game's entry starts the game's own program in its folder, as the launcher
// does, so no game needs the launcher to start.
//
// Written only for what the player installed: the packaged launcher (portable.txt next to it, not a
// developer's build), and the games installed from the launcher (games.h InstalledVersion). An
// entry of a game that is gone is removed again. Never with --documents (a test on a copy). The
// pictures are the player's own: the launcher's icon next to it, a game's hero as its game left it.

#include <filesystem>
#include <string>
#include <vector>

#include "games.h"

namespace coreoflight {

// Brings the entries in line with what is installed now (from LauncherUi::Refresh: at start, after
// an install or update, after a game). `installed_versions` as games.h InstalledVersion gives them
// per game, empty for a game not installed by the launcher.
void UpdateDesktopEntries(const std::vector<Found>& found, const std::vector<std::string>& installed_versions,
                          const std::filesystem::path& launcher_dir, bool packaged);

}  // namespace coreoflight
