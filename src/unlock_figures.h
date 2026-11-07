#pragma once

// "Unlock all figures" (hub setting unlock_all_figures, off by default; the owner's wish,
// 2026-11-06): a new, blank figure file for every Skylander, variant, trap, magic item, sidekick and
// vehicle of the first five games in the active profile's figure folder, made the way the games'
// portal menu makes one when a figure is placed (skylanders-portal CreateBlankFigure), so every
// game and the figure library have them all. Imaginators' toys are left out: they are signed and
// only work as files of the real toys (the portal menu can't make them either); so are the
// catalog's unreleased prototypes.
//
// The files go where the portal menu puts new figures, <figures>/<game>/<name>.dump, and a list of
// them with each one's SHA-256 as made into <figures>/.coreoflight-unlocked.txt. Switching the
// setting off removes the listed files that are still exactly as made; a figure played with since
// (its progress written onto it) stays. A figure already in the folder (any file with its id and
// variant) gets no second one.

#include <filesystem>

namespace coreoflight {

// Makes the missing ones; how many it made (-1 if the folder can't be written).
int UnlockAllFigures(const std::filesystem::path& figures_dir);
// Removes the unchanged ones it made; how many it removed. Played ones are kept.
int RelockFigures(const std::filesystem::path& figures_dir);
// Whether figures were unlocked in this folder (its list is there).
bool FiguresUnlocked(const std::filesystem::path& figures_dir);

}  // namespace coreoflight
