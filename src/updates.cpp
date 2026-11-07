#include "updates.h"

#include "games.h"  // VersionLess

#ifndef _WIN32
#include <curl/curl.h>
#include <strings.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <winhttp.h>
#endif
#include "hub_log.h"
#include "platform.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <vector>

namespace coreoflight {
namespace {

namespace fs = std::filesystem;

constexpr const char* kOwner = "tsapZ";

// --- A small JSON reader, for the GitHub API's answers -------------------------------------------

struct Json {
  enum class Kind { kNull, kBool, kNumber, kString, kArray, kObject };
  Kind kind = Kind::kNull;
  std::string text;  // a string's value, or a number as written
  bool flag = false;
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> fields;

  const Json* Get(std::string_view key) const {
    for (const auto& [k, v] : fields) {
      if (k == key) return &v;
    }
    return nullptr;
  }
  std::string String(std::string_view key) const {
    const Json* v = Get(key);
    return v && v->kind == Kind::kString ? v->text : std::string();
  }
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) : p_(text.data()), end_(text.data() + text.size()) {}

  bool Parse(Json& out) {
    out = Value(0);
    Space();
    return ok_ && p_ == end_;
  }

 private:
  void Space() {
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
  }
  bool Take(char c) {
    Space();
    if (p_ < end_ && *p_ == c) {
      ++p_;
      return true;
    }
    return false;
  }
  static void Utf8(std::string& s, uint32_t c) {
    if (c < 0x80) {
      s += char(c);
    } else if (c < 0x800) {
      s += char(0xC0 | (c >> 6));
      s += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      s += char(0xE0 | (c >> 12));
      s += char(0x80 | ((c >> 6) & 0x3F));
      s += char(0x80 | (c & 0x3F));
    } else {
      s += char(0xF0 | (c >> 18));
      s += char(0x80 | ((c >> 12) & 0x3F));
      s += char(0x80 | ((c >> 6) & 0x3F));
      s += char(0x80 | (c & 0x3F));
    }
  }
  bool Hex4(uint32_t& v) {
    if (end_ - p_ < 4) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = *p_++;
      v <<= 4;
      if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
      else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
      else return false;
    }
    return true;
  }
  std::string StringBody() {
    std::string s;
    while (p_ < end_ && *p_ != '"') {
      char c = *p_++;
      if (c != '\\') {
        s += c;
        continue;
      }
      if (p_ >= end_) break;
      c = *p_++;
      switch (c) {
        case 'n': s += '\n'; break;
        case 't': s += '\t'; break;
        case 'r': s += '\r'; break;
        case 'b': s += '\b'; break;
        case 'f': s += '\f'; break;
        case 'u': {
          uint32_t v = 0;
          if (!Hex4(v)) {
            ok_ = false;
            return s;
          }
          // A surrogate pair: the second half follows as its own \u.
          if (v >= 0xD800 && v < 0xDC00 && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
            p_ += 2;
            uint32_t low = 0;
            if (!Hex4(low)) {
              ok_ = false;
              return s;
            }
            v = 0x10000 + ((v - 0xD800) << 10) + (low - 0xDC00);
          }
          Utf8(s, v);
          break;
        }
        default: s += c; break;  // \" \\ \/
      }
    }
    if (p_ < end_) ++p_;  // the closing quote
    else ok_ = false;
    return s;
  }
  Json Value(int depth) {
    Json v;
    Space();
    if (p_ >= end_ || depth > 64) {
      ok_ = false;
      return v;
    }
    const char c = *p_;
    if (c == '{') {
      ++p_;
      v.kind = Json::Kind::kObject;
      if (Take('}')) return v;
      do {
        if (!Take('"')) {
          ok_ = false;
          return v;
        }
        std::string key = StringBody();
        if (!Take(':')) {
          ok_ = false;
          return v;
        }
        v.fields.emplace_back(std::move(key), Value(depth + 1));
      } while (ok_ && Take(','));
      if (!Take('}')) ok_ = false;
    } else if (c == '[') {
      ++p_;
      v.kind = Json::Kind::kArray;
      if (Take(']')) return v;
      do {
        v.items.push_back(Value(depth + 1));
      } while (ok_ && Take(','));
      if (!Take(']')) ok_ = false;
    } else if (c == '"') {
      ++p_;
      v.kind = Json::Kind::kString;
      v.text = StringBody();
    } else if (std::string_view(p_, size_t(end_ - p_)).starts_with("true")) {
      p_ += 4;
      v.kind = Json::Kind::kBool;
      v.flag = true;
    } else if (std::string_view(p_, size_t(end_ - p_)).starts_with("false")) {
      p_ += 5;
      v.kind = Json::Kind::kBool;
    } else if (std::string_view(p_, size_t(end_ - p_)).starts_with("null")) {
      p_ += 4;
    } else {
      v.kind = Json::Kind::kNumber;
      while (p_ < end_ && (std::isdigit(static_cast<unsigned char>(*p_)) || *p_ == '-' || *p_ == '+' || *p_ == '.' ||
                           *p_ == 'e' || *p_ == 'E')) {
        v.text += *p_++;
      }
      if (v.text.empty()) ok_ = false;
    }
    return v;
  }

  const char* p_;
  const char* end_;
  bool ok_ = true;
};

#ifdef _WIN32
// --- HTTPS through WinHTTP -------------------------------------------------------------------

std::wstring Wide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

// A GET of `url` (redirects followed, as GitHub's downloads need), the body handed to `sink` in
// pieces (false from it stops); `total` gets the length when the server says it. True if the
// status was 200 and the whole body went to the sink.
bool HttpGet(const std::string& url, const std::string& header_lines,
             const std::function<bool(const char*, size_t)>& sink, uint64_t* total) {
  const std::wstring header_text = Wide(header_lines);
  const wchar_t* headers = header_text.empty() ? nullptr : header_text.c_str();
  const std::wstring wurl = Wide(url);
  URL_COMPONENTS parts{sizeof(parts)};
  wchar_t host[256] = L"", path[2048] = L"";
  parts.lpszHostName = host;
  parts.dwHostNameLength = DWORD(std::size(host));
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = DWORD(std::size(path));
  parts.dwSchemeLength = 1;
  parts.dwExtraInfoLength = 1;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) return false;
  std::wstring target = path;
  if (parts.lpszExtraInfo && parts.dwExtraInfoLength) target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

  bool ok = false;
  const std::wstring agent = L"CoreOfLight/" + Wide(COREOFLIGHT_VERSION);
  HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  HINTERNET connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
  HINTERNET request =
      connection ? WinHttpOpenRequest(connection, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                 : nullptr;
  if (request) {
    WinHttpSetTimeouts(request, 10000, 10000, 15000, 30000);
    if (WinHttpSendRequest(request, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? DWORD(-1L) : 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
      DWORD status = 0, size = sizeof(status);
      WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                          &status, &size, WINHTTP_NO_HEADER_INDEX);
      if (status == 200) {
        if (total) {
          wchar_t length[32] = L"";
          DWORD length_size = sizeof(length);
          *total = WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length,
                                       &length_size, WINHTTP_NO_HEADER_INDEX)
                       ? _wcstoui64(length, nullptr, 10)
                       : 0;
        }
        ok = true;
        std::vector<char> buffer(size_t(64) << 10);
        for (;;) {
          DWORD read = 0;
          if (!WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &read)) {
            ok = false;
            break;
          }
          if (read == 0) break;
          if (!sink(buffer.data(), read)) {
            ok = false;
            break;
          }
        }
      }
    }
  }
  if (request) WinHttpCloseHandle(request);
  if (connection) WinHttpCloseHandle(connection);
  if (session) WinHttpCloseHandle(session);
  return ok;
}
#else
// --- HTTPS through libcurl -------------------------------------------------------------------

// The same GET as WinHTTP's above: redirects followed, the body to `sink`, 200 only. The system's
// CA certificates (on SteamOS too); the same timeouts (10 s to connect, no 30 s without data).
bool HttpGet(const std::string& url, const std::string& header_lines,
             const std::function<bool(const char*, size_t)>& sink, uint64_t* total) {
  static std::once_flag init;
  std::call_once(init, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
  CURL* curl = curl_easy_init();
  if (!curl) return false;
  struct State {
    const std::function<bool(const char*, size_t)>* sink;
    bool stopped = false;
  } state{&sink};
  curl_slist* headers = nullptr;
  for (size_t start = 0; start < header_lines.size();) {
    size_t end = header_lines.find("\r\n", start);
    if (end == std::string::npos) end = header_lines.size();
    if (end > start) headers = curl_slist_append(headers, header_lines.substr(start, end - start).c_str());
    start = end + 2;
  }
  const std::string agent = "CoreOfLight/" COREOFLIGHT_VERSION;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // called from background threads
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);  // no body for anything but 2xx
  if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* data, size_t size, size_t n, void* user) -> size_t {
    auto* st = static_cast<State*>(user);
    if (!(*st->sink)(data, size * n)) {
      st->stopped = true;
      return 0;  // stops the transfer
    }
    return size * n;
  });
  if (total) {
    *total = 0;
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, total);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, +[](char* data, size_t size, size_t n, void* user) -> size_t {
      // The last response's Content-Length (a redirect's comes first and is replaced).
      const std::string line(data, size * n);
      if (line.size() > 15 && strncasecmp(line.c_str(), "content-length:", 15) == 0) {
        *static_cast<uint64_t*>(user) = std::strtoull(line.c_str() + 15, nullptr, 10);
      }
      return size * n;
    });
  }
  const CURLcode result = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (result != CURLE_OK && !state.stopped) HubWarn(std::string("http: ") + curl_easy_strerror(result));
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return result == CURLE_OK && status == 200 && !state.stopped;
}
#endif

fs::path LocalDataDir() { return platform::LocalDataDir(); }

fs::path KnownReleasesFile() { return LocalDataDir() / "releases.txt"; }

uint64_t Now() { return platform::NowFileTime(); }

}  // namespace

std::optional<Release> FetchLatestRelease(const std::string& repo, const std::string& asset_prefix,
                                          const std::string& extension, bool* answered) {
  // /releases rather than /releases/latest: that one leaves out pre-releases, and ours are betas.
  std::string body;
  const std::string url = "https://api.github.com/repos/" + std::string(kOwner) + "/" + repo + "/releases?per_page=10";
  if (!HttpGet(url, "Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28",
               [&](const char* data, size_t n) {
                 body.append(data, n);
                 return body.size() < (size_t(8) << 20);
               },
               nullptr)) {
    return std::nullopt;
  }
  Json releases;
  if (!JsonParser(body).Parse(releases) || releases.kind != Json::Kind::kArray) return std::nullopt;
  if (answered) *answered = true;
  // The highest version with its setup, not the first in the list: GitHub sorts the list by the
  // tag as text there, so v0.1.0-beta.10 came after beta.7 and the launcher (beta.9) never saw it.
  std::optional<Release> best;
  for (const Json& r : releases.items) {
    if (const Json* draft = r.Get("draft"); draft && draft->flag) continue;
    std::string version = r.String("tag_name");
    if (version.starts_with("v")) version.erase(0, 1);
    if (version.empty() || (best && !VersionLess(best->version, version))) continue;
    const std::string wanted = asset_prefix + "-" + version + extension;
    const Json* assets = r.Get("assets");
    for (size_t i = 0; assets && i < assets->items.size(); ++i) {
      const Json& a = assets->items[i];
      if (a.String("name") != wanted) continue;
      Release release{repo, version, wanted, a.String("browser_download_url"), 0, {}};
      if (std::string digest = a.String("digest"); digest.starts_with("sha256:") && digest.size() == 7 + 64) {
        release.sha256 = digest.substr(7);
        for (char& c : release.sha256) c = char(std::tolower(static_cast<unsigned char>(c)));
      }
      if (const Json* size = a.Get("size"); size && size->kind == Json::Kind::kNumber) {
        release.size = std::strtoull(size->text.c_str(), nullptr, 10);
      }
      if (!release.url.empty()) best = release;
    }
  }
  return best;
}

std::vector<Release> LoadKnownReleases(uint64_t* checked) {
  std::vector<Release> releases;
  if (checked) *checked = 0;
  std::ifstream in(KnownReleasesFile());
  std::string line;
  while (std::getline(in, line)) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    for (std::string part; std::getline(ss, part, '\t');) f.push_back(part);
    if (f.size() == 2 && f[0] == "checked") {
      if (checked) *checked = std::strtoull(f[1].c_str(), nullptr, 10);
    } else if (f.size() == 5 || f.size() == 6) {  // the sixth, the SHA-256, since 0.3.0-beta.8
      releases.push_back({f[0], f[1], f[2], f[3], std::strtoull(f[4].c_str(), nullptr, 10),
                          f.size() == 6 ? f[5] : std::string()});
    }
  }
  return releases;
}

void SaveKnownReleases(const std::vector<Release>& releases) {
  std::error_code ec;
  fs::create_directories(LocalDataDir(), ec);
  std::ofstream out(KnownReleasesFile(), std::ios::trunc);
  out << "checked\t" << Now() << "\n";
  for (const Release& r : releases) {
    out << r.repo << '\t' << r.version << '\t' << r.asset << '\t' << r.url << '\t' << r.size << '\t' << r.sha256
        << "\n";
  }
}

bool UpdateCheckDue() {
  uint64_t checked = 0;
  LoadKnownReleases(&checked);
  // Six hours (a day before: a game released in the evening wasn't seen until the next day, and a
  // player whose disc it was made for couldn't start it, 2026-10-31). Seven requests a check,
  // GitHub allows 60 an hour without an account.
  constexpr uint64_t kInterval = 6ull * 60 * 60 * 10000000;  // in 100 ns steps
  return checked == 0 || Now() - checked > kInterval;
}

std::string FileSha256(const fs::path& file) { return platform::FileSha256Hex(file); }

bool DownloadFile(const std::string& url, const fs::path& file, std::atomic<float>* progress,
                  const std::atomic<bool>* cancel, const std::string& sha256) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  fs::path part = file;
  part += ".part";
  uint64_t total = 0, done = 0;
  bool ok;
  {
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    ok = HttpGet(url, {},
                 [&](const char* data, size_t n) {
                   out.write(data, std::streamsize(n));
                   done += n;
                   if (progress && total) progress->store(float(double(done) / double(total)));
                   return bool(out) && !(cancel && cancel->load());
                 },
                 &total) &&
         bool(out);
    // `total` is known only after the headers; the sink reads it from the first piece on.
  }
  if (ok && total && done != total) ok = false;
  // The release's own SHA-256 (GitHub's digest of the asset): a damaged or swapped download goes.
  if (ok && !sha256.empty() && FileSha256(part) != sha256) ok = false;
  if (!ok) {
    fs::remove(part, ec);
    return false;
  }
  fs::remove(file, ec);
  fs::rename(part, file, ec);
  if (progress) progress->store(1.0f);
  return !ec;
}

fs::path DownloadsDir() {
#ifdef _WIN32
  return platform::TempDir() / "downloads";
#else
  // Not /tmp: a RAM disk on many systems, the Steam Deck's too.
  const std::string cache = platform::Env("XDG_CACHE_HOME");
  const fs::path base = cache.empty() ? platform::GetUserFolder(platform::UserFolder::kHome) / ".cache" : fs::path(cache);
  return base / "CoreOfLight" / "downloads";
#endif
}

namespace {
bool g_offline = false;
}  // namespace

void SetOffline(bool offline) { g_offline = offline; }
bool Offline() { return g_offline; }

}  // namespace coreoflight
