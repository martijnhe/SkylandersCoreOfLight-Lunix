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
    {"SuperChargers: the French / Dutch disc can be set up, with Dutch.",
     "SuperChargers: die franz\xC3\xB6sisch-niederl\xC3\xA4ndische Disc l\xC3\xA4sst sich einrichten, mit Niederl\xC3\xA4ndisch."},
    {"Runs on Linux and the Steam Deck, also as one AppImage file.",
     "L\xC3\xA4uft unter Linux und auf dem Steam Deck, auch als eine AppImage-Datei."},
};

}  // namespace coreoflight
