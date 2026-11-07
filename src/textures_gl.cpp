// textures.h off Windows: stb_image decodes, OpenGL holds the pictures (the SDL3 / OpenGL 3 window,
// main_sdl.cpp, whose context is current on the UI thread).

#include "textures.h"

#include <GL/gl.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

// stb_image's PNG decoder, every function static, as language_import.cpp has its own copy.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace coreoflight {
namespace {

struct Entry {
  GLuint texture = 0;
  std::chrono::steady_clock::time_point tried;
};
std::unordered_map<std::string, Entry> g_textures;

GLuint Decode(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  if (bytes.empty()) return 0;
  int w = 0, h = 0, channels = 0;
  stbi_uc* rgba = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 4);
  if (!rgba) return 0;
  GLuint texture = 0;
  if (w > 0 && h > 0 && w <= 4096 && h <= 4096) {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  }
  stbi_image_free(rgba);
  return texture;
}

}  // namespace

bool g_no_game_art = false;

void SetNoGameArt(bool on) { g_no_game_art = on; }

ImTextureID LoadTexture(const std::filesystem::path& path) {
  if (g_no_game_art) return ImTextureID{};
  const auto now = std::chrono::steady_clock::now();
  auto [it, added] = g_textures.try_emplace(path.string());
  Entry& e = it->second;
  if (!e.texture && (added || now - e.tried > std::chrono::seconds(10))) {
    e.tried = now;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) e.texture = Decode(path);
  }
  return e.texture ? ImTextureID(e.texture) : ImTextureID{};
}

void ReleaseTextures() {
  for (auto& [path, e] : g_textures) {
    if (e.texture) glDeleteTextures(1, &e.texture);
  }
  g_textures.clear();
}

}  // namespace coreoflight
