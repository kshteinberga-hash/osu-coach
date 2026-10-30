#include "audio.h"
#include "audio_cfg.h"
#include <algorithm>
#include "miniaudio.h"

struct Audio::Impl {
    ma_engine engine;
    ma_sound sound;
    bool engineOk = false, soundOk = false;
    ~Impl() {
        if (soundOk) ma_sound_uninit(&sound);
        if (engineOk) ma_engine_uninit(&engine);
    }
};

Audio::Audio() : p(std::make_unique<Impl>()) {}
Audio::~Audio() = default;

bool Audio::load(const std::wstring& path) {
    p = std::make_unique<Impl>();
    if (ma_engine_init(nullptr, &p->engine) != MA_SUCCESS) return false;
    p->engineOk = true;
    p->soundOk = ma_sound_init_from_file_w(&p->engine, path.c_str(), 0, nullptr, nullptr, &p->sound) == MA_SUCCESS;
    return p->soundOk;
}
bool Audio::loaded() const { return p->soundOk; }
void Audio::play() { if (p->soundOk) ma_sound_start(&p->sound); }
void Audio::pause() { if (p->soundOk) ma_sound_stop(&p->sound); }
bool Audio::playing() const { return p->soundOk && ma_sound_is_playing(&p->sound); }
void Audio::setSpeed(float s) { if (p->soundOk) ma_sound_set_pitch(&p->sound, s); }
void Audio::setVolume(float v) { if (p->soundOk) ma_sound_set_volume(&p->sound, std::clamp(v, 0.f, 1.f)); }
bool Audio::ended() const { return p->soundOk && ma_sound_at_end(&p->sound); }

void Audio::seekMs(double ms) {
    if (!p->soundOk) return;
    ma_uint32 rate = 0;
    ma_sound_get_data_format(&p->sound, nullptr, nullptr, &rate, nullptr, 0);
    ma_sound_seek_to_pcm_frame(&p->sound, (ma_uint64)(std::max(0.0, ms) / 1000 * rate));
}

double Audio::positionMs() const {
    if (!p->soundOk) return 0;
    ma_uint64 frame = 0;
    ma_uint32 rate = 0;
    ma_sound_get_cursor_in_pcm_frames(&p->sound, &frame);
    ma_sound_get_data_format(&p->sound, nullptr, nullptr, &rate, nullptr, 0);
    return rate ? frame * 1000.0 / rate : 0;
}
