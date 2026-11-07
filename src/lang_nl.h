#pragma once

// Dutch for the launcher's screens (lang_nl.cpp; the owner's wish, 2026-11-06). The screens choose
// between German and English with `german ? "..." : "..."`; the English half goes through NL(),
// which gives its Dutch translation while the launcher speaks Dutch (hub setting language =
// "dutch", or "auto" on a Dutch system) and the English text otherwise. A text without a Dutch
// translation stays English. Text built from pieces is translated piece by piece, so a piece is
// translated as it stands (with its spaces and punctuation).

#include <string>

namespace coreoflight {

void SetDutch(bool on);
bool Dutch();

const char* NL(const char* en);
std::string NL(const std::string& en);
const wchar_t* NLW(const wchar_t* en);

}  // namespace coreoflight
