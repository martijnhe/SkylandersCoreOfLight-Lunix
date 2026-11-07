// music.h off Windows: the title music's WAV through an SDL3 audio stream, looped by queueing it
// again before it runs out, the fades as the stream's gain.

#include "music.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "hub_log.h"

namespace coreoflight {
namespace {

constexpr float kFadeIn = 1.2f, kFadeOut = 0.6f;  // seconds
constexpr float kLevel = 0.7f;                    // not at full loudness behind the menus

struct Track {
  SDL_AudioStream* stream = nullptr;
  Uint8* data = nullptr;
  Uint32 size = 0;
};
Track g_track;

}  // namespace

MusicPlayer::~MusicPlayer() { Close(); }

void MusicPlayer::Play(const std::filesystem::path& file) { wanted_ = file; }

void MusicPlayer::Stop() {
  wanted_.clear();
  Close();
}

void MusicPlayer::Open(const std::filesystem::path& file) {
  Close();
  if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    HubLog(std::string("music: no audio: ") + SDL_GetError());
    wanted_.clear();
    return;
  }
  SDL_AudioSpec spec{};
  if (!SDL_LoadWAV(reinterpret_cast<const char*>(file.u8string().c_str()), &spec, &g_track.data, &g_track.size)) {
    HubLog(std::string("music: cannot read the title music: ") + SDL_GetError());
    wanted_.clear();
    return;
  }
  g_track.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!g_track.stream) {
    HubLog(std::string("music: no audio device: ") + SDL_GetError());
    SDL_free(g_track.data);
    g_track = {};
    wanted_.clear();
    return;
  }
  open_ = true;
  playing_ = file;
  volume_ = 0.0f;
  SetVolume(0.0f);
  SDL_PutAudioStreamData(g_track.stream, g_track.data, int(g_track.size));
  SDL_ResumeAudioStreamDevice(g_track.stream);
}

void MusicPlayer::Close() {
  if (g_track.stream) SDL_DestroyAudioStream(g_track.stream);
  if (g_track.data) SDL_free(g_track.data);
  g_track = {};
  open_ = false;
  playing_.clear();
  volume_ = 0.0f;
}

void MusicPlayer::SetVolume(float v) {
  // The ear follows the square better than the line (as music.cpp does with MCI's volume).
  const float loud = v * loudness_;
  if (g_track.stream) SDL_SetAudioStreamGain(g_track.stream, loud * loud * kLevel);
}

void MusicPlayer::SetLoudness(float loudness) {
  loudness = std::clamp(loudness, 0.0f, 1.0f);
  if (loudness == loudness_) return;
  loudness_ = loudness;
  if (open_) SetVolume(volume_);
}

void MusicPlayer::Update(float dt) {
  if (open_ && playing_ != wanted_) {
    volume_ = std::max(0.0f, volume_ - dt / kFadeOut);
    SetVolume(volume_);
    if (volume_ <= 0.0f) Close();
    return;
  }
  if (!open_ && !wanted_.empty()) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(wanted_, ec)) Open(wanted_);
    else wanted_.clear();
    return;
  }
  if (open_ && volume_ < 1.0f) {
    volume_ = std::min(1.0f, volume_ + dt / kFadeIn);
    SetVolume(volume_);
  }
  // The loop: the next round queued while less than half of this one is left.
  since_check_ += dt;
  if (open_ && since_check_ >= 0.25f) {
    since_check_ = 0.0f;
    if (SDL_GetAudioStreamQueued(g_track.stream) < int(g_track.size / 2)) {
      SDL_PutAudioStreamData(g_track.stream, g_track.data, int(g_track.size));
    }
  }
}

}  // namespace coreoflight
