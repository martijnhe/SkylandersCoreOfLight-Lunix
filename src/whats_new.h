#pragma once

// What's new in this version of the launcher, a few short lines in English and German, shown at
// the bottom right of the launcher settings (LB / Q, #44). Rewritten for each release: only what a
// player notices, newest version only.

namespace coreoflight {

struct WhatsNewLine {
  const char* en;
  const char* de;
};

inline constexpr WhatsNewLine kWhatsNew[] = {
    {"Runs on Linux and the Steam Deck, also as one AppImage file.",
     "L\xC3\xA4uft unter Linux und auf dem Steam Deck, auch als eine AppImage-Datei."},
    {"Dutch: the launcher, and Nederlands in every game's language setting.",
     "Niederl\xC3\xA4ndisch: der Launcher und Nederlands in der Spracheinstellung jedes Spiels."},
};

}  // namespace coreoflight
