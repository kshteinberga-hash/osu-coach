// The miniaudio + stb_vorbis implementation, compiled once into build\audio_impl.obj.
#include "audio_cfg.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
