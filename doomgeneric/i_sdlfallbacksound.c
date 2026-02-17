//
// SDL audio fallback backend for Atari/MiNT builds without SDL_mixer.
// Provides SFX playback; music hooks are present but intentionally no-op.
//

#include "config.h"

#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deh_str.h"
#include "doomtype.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#ifndef ATARI_FALLBACK_MIX_CHANNELS
#define ATARI_FALLBACK_MIX_CHANNELS 8
#endif

#ifndef ATARI_FALLBACK_AUDIO_RATE
#define ATARI_FALLBACK_AUDIO_RATE 11025
#endif

#ifndef ATARI_FALLBACK_AUDIO_DEVICE_CHANNELS
#define ATARI_FALLBACK_AUDIO_DEVICE_CHANNELS 1
#endif

#ifndef ATARI_FALLBACK_AUDIO_U8
#define ATARI_FALLBACK_AUDIO_U8 1
#endif

#define FALLBACK_NUM_CHANNELS ATARI_FALLBACK_MIX_CHANNELS

typedef struct fallback_cached_sound_s fallback_cached_sound_t;

struct fallback_cached_sound_s
{
    sfxinfo_t *sfxinfo;
    int16_t *samples;
    uint32_t sample_count;
    fallback_cached_sound_t *next;
};

typedef struct
{
    fallback_cached_sound_t *sound;
    uint32_t position;
    int left;
    int right;
} fallback_channel_t;

static boolean sound_initialized = false;
static SDL_AudioSpec obtained_spec;
static boolean use_sfx_prefix = false;

static fallback_cached_sound_t *cached_sounds = NULL;
static fallback_channel_t channels_playing[FALLBACK_NUM_CHANNELS];

// Config variables that are bound when FEATURE_SOUND is enabled.
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
char *timidity_cfg_path = "";
char *gus_patch_path = "";
unsigned int gus_ram_kb = 1024;

static int GetSliceSize(void)
{
    int limit;
    int n;

    limit = (snd_samplerate * snd_maxslicetime_ms) / 1000;
    if (limit < 64)
    {
        limit = 64;
    }

    for (n = 0; ; ++n)
    {
        if ((1 << (n + 1)) > limit)
        {
            return (1 << n);
        }
    }
}

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    if (sfx->link != NULL)
    {
        sfx = sfx->link;
    }

    if (use_sfx_prefix)
    {
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    }
    else
    {
        M_StringCopy(buf, DEH_String(sfx->name), buf_len);
    }
}

static fallback_cached_sound_t *CacheSfx(sfxinfo_t *sfxinfo)
{
    int lumpnum;
    unsigned int lumplen;
    int source_rate;
    unsigned int source_len;
    byte *data;
    const byte *source;
    uint32_t i;
    uint32_t out_len;
    fallback_cached_sound_t *cached;

    lumpnum = sfxinfo->lumpnum;
    data = W_CacheLumpNum(lumpnum, PU_STATIC);
    lumplen = W_LumpLength(lumpnum);

    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
    {
        W_ReleaseLumpNum(lumpnum);
        return NULL;
    }

    source_rate = (data[3] << 8) | data[2];
    source_len = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];

    if (source_rate <= 0 || source_len > lumplen - 8 || source_len <= 48)
    {
        W_ReleaseLumpNum(lumpnum);
        return NULL;
    }

    // Match classic DMX behavior.
    source_len -= 32;
    source = data + 16 + 8;

    out_len = (uint32_t)(((uint64_t)source_len * (uint64_t)obtained_spec.freq)
                         / (uint64_t)source_rate);
    if (out_len == 0)
    {
        out_len = 1;
    }

    cached = (fallback_cached_sound_t *)malloc(sizeof(*cached));
    if (cached == NULL)
    {
        W_ReleaseLumpNum(lumpnum);
        return NULL;
    }

    cached->samples = (int16_t *)malloc(out_len * sizeof(int16_t));
    if (cached->samples == NULL)
    {
        free(cached);
        W_ReleaseLumpNum(lumpnum);
        return NULL;
    }

    for (i = 0; i < out_len; ++i)
    {
        uint32_t src_index =
            (uint32_t)(((uint64_t)i * (uint64_t)source_rate) / (uint64_t)obtained_spec.freq);
        int sample;

        if (src_index >= source_len)
        {
            src_index = source_len - 1;
        }

        sample = (int)source[src_index] - 128;
        cached->samples[i] = (int16_t)(sample << 8);
    }

    W_ReleaseLumpNum(lumpnum);

    cached->sfxinfo = sfxinfo;
    cached->sample_count = out_len;
    cached->next = cached_sounds;
    cached_sounds = cached;

    sfxinfo->driver_data = cached;
    return cached;
}

static void UpdateChannelParams(int handle, int vol, int sep)
{
    int left;
    int right;

    if (handle < 0 || handle >= FALLBACK_NUM_CHANNELS)
    {
        return;
    }

    left = ((254 - sep) * vol) / 127;
    right = (sep * vol) / 127;

    if (left < 0) left = 0;
    if (right < 0) right = 0;
    if (left > 255) left = 255;
    if (right > 255) right = 255;

    channels_playing[handle].left = left;
    channels_playing[handle].right = right;
}

static int BytesPerSample(Uint16 format)
{
    int bits = (int)(format & 0x00ff);

    if (bits <= 0)
    {
        return 0;
    }

    return bits / 8;
}

static void WriteSample(Uint8 *dst, Uint16 format, int sample)
{
    int bits;
    int signed_sample;

    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;

    bits = (int)(format & 0x00ff);
    signed_sample = (format & 0x8000) != 0;

    if (bits == 8)
    {
        if (signed_sample)
        {
            dst[0] = (Uint8)((Sint8)(sample >> 8));
        }
        else
        {
            dst[0] = (Uint8)((sample + 32768) >> 8);
        }
        return;
    }

    if (bits == 16)
    {
        Uint16 out16;

        if (signed_sample)
        {
            out16 = (Uint16)((Sint16)sample);
        }
        else
        {
            out16 = (Uint16)(sample + 32768);
        }

        if ((format & 0x1000) != 0)
        {
            dst[0] = (Uint8)(out16 >> 8);
            dst[1] = (Uint8)(out16 & 0xff);
        }
        else
        {
            dst[0] = (Uint8)(out16 & 0xff);
            dst[1] = (Uint8)(out16 >> 8);
        }

        return;
    }

    // Unsupported format, silence.
    dst[0] = 0;
    if (bits >= 16)
    {
        dst[1] = 0;
    }
}

static void FallbackAudioCallback(void *userdata, Uint8 *stream, int len)
{
    int frames;
    int sample_bytes;
    int frame_bytes;
    int out_channels;
    int i;
    int c;

    (void)userdata;

    memset(stream, 0, (size_t)len);

    sample_bytes = BytesPerSample(obtained_spec.format);
    out_channels = obtained_spec.channels;

    if (sample_bytes <= 0 || out_channels <= 0)
    {
        return;
    }

    frame_bytes = sample_bytes * out_channels;
    if (frame_bytes <= 0)
    {
        return;
    }

    frames = len / frame_bytes;

    for (i = 0; i < frames; ++i)
    {
        Uint8 *frame_ptr = stream + (i * frame_bytes);
        int mix_l = 0;
        int mix_r = 0;

        for (c = 0; c < FALLBACK_NUM_CHANNELS; ++c)
        {
            fallback_channel_t *ch = &channels_playing[c];
            fallback_cached_sound_t *snd = ch->sound;
            int sample;

            if (snd == NULL)
            {
                continue;
            }

            if (ch->position >= snd->sample_count)
            {
                ch->sound = NULL;
                ch->position = 0;
                continue;
            }

            sample = snd->samples[ch->position++];
            mix_l += (sample * ch->left) / 255;
            mix_r += (sample * ch->right) / 255;

            if (ch->position >= snd->sample_count)
            {
                ch->sound = NULL;
                ch->position = 0;
            }
        }

        if (mix_l > 32767) mix_l = 32767;
        if (mix_l < -32768) mix_l = -32768;
        if (mix_r > 32767) mix_r = 32767;
        if (mix_r < -32768) mix_r = -32768;

        if (out_channels == 1)
        {
            int mono = (mix_l + mix_r) / 2;
            WriteSample(frame_ptr, obtained_spec.format, mono);
        }
        else
        {
            int out_ch;

            for (out_ch = 0; out_ch < out_channels; ++out_ch)
            {
                int sample = (out_ch == 0) ? mix_l : (out_ch == 1 ? mix_r : ((mix_l + mix_r) / 2));
                WriteSample(frame_ptr + (out_ch * sample_bytes), obtained_spec.format, sample);
            }
        }
    }
}

static boolean I_SDLFallback_InitSound(boolean _use_sfx_prefix)
{
    SDL_AudioSpec desired;
    int requested_rate;
    int i;

    use_sfx_prefix = _use_sfx_prefix;

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
    {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        {
            fprintf(stderr, "Unable to initialize SDL audio: %s\n", SDL_GetError());
            return false;
        }
    }

    memset(&desired, 0, sizeof(desired));
    requested_rate = snd_samplerate;
    if (requested_rate <= 0)
    {
        requested_rate = ATARI_FALLBACK_AUDIO_RATE;
    }

    // Keep fallback audio light enough for Atari CPUs.
    if (requested_rate > ATARI_FALLBACK_AUDIO_RATE)
    {
        requested_rate = ATARI_FALLBACK_AUDIO_RATE;
    }

    desired.freq = requested_rate;
#if ATARI_FALLBACK_AUDIO_U8
    desired.format = AUDIO_U8;
#else
    desired.format = AUDIO_S16SYS;
#endif
    desired.channels = ATARI_FALLBACK_AUDIO_DEVICE_CHANNELS;
    if (desired.channels <= 0)
    {
        desired.channels = 1;
    }
    desired.samples = (Uint16)GetSliceSize();
    desired.callback = FallbackAudioCallback;

    if (SDL_OpenAudio(&desired, &obtained_spec) < 0)
    {
        fprintf(stderr, "Unable to open SDL audio: %s\n", SDL_GetError());
        return false;
    }

    for (i = 0; i < FALLBACK_NUM_CHANNELS; ++i)
    {
        channels_playing[i].sound = NULL;
        channels_playing[i].position = 0;
        channels_playing[i].left = 255;
        channels_playing[i].right = 255;
    }

    SDL_PauseAudio(0);
    sound_initialized = true;
    return true;
}

static void I_SDLFallback_ShutdownSound(void)
{
    fallback_cached_sound_t *cached;

    if (!sound_initialized)
    {
        return;
    }

    SDL_LockAudio();
    memset(channels_playing, 0, sizeof(channels_playing));
    SDL_UnlockAudio();

    SDL_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    cached = cached_sounds;
    while (cached != NULL)
    {
        fallback_cached_sound_t *next = cached->next;

        if (cached->sfxinfo != NULL)
        {
            cached->sfxinfo->driver_data = NULL;
        }

        free(cached->samples);
        free(cached);
        cached = next;
    }

    cached_sounds = NULL;
    sound_initialized = false;
}

static int I_SDLFallback_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];

    GetSfxLumpName(sfx, namebuf, sizeof(namebuf));
    return W_GetNumForName(namebuf);
}

static void I_SDLFallback_UpdateSound(void)
{
    // State is advanced from the audio callback.
}

static void I_SDLFallback_UpdateSoundParams(int channel, int vol, int sep)
{
    if (!sound_initialized || channel < 0 || channel >= FALLBACK_NUM_CHANNELS)
    {
        return;
    }

    if (channels_playing[channel].sound == NULL)
    {
        return;
    }

    SDL_LockAudio();
    UpdateChannelParams(channel, vol, sep);
    SDL_UnlockAudio();
}

static int I_SDLFallback_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    fallback_cached_sound_t *cached;

    if (!sound_initialized || channel < 0 || channel >= FALLBACK_NUM_CHANNELS)
    {
        return -1;
    }

    cached = (fallback_cached_sound_t *)sfxinfo->driver_data;
    if (cached == NULL)
    {
        cached = CacheSfx(sfxinfo);
        if (cached == NULL)
        {
            return -1;
        }
    }

    SDL_LockAudio();
    channels_playing[channel].sound = cached;
    channels_playing[channel].position = 0;
    UpdateChannelParams(channel, vol, sep);
    SDL_UnlockAudio();

    return channel;
}

static void I_SDLFallback_StopSound(int channel)
{
    if (!sound_initialized || channel < 0 || channel >= FALLBACK_NUM_CHANNELS)
    {
        return;
    }

    SDL_LockAudio();
    channels_playing[channel].sound = NULL;
    channels_playing[channel].position = 0;
    SDL_UnlockAudio();
}

static boolean I_SDLFallback_SoundIsPlaying(int channel)
{
    if (!sound_initialized || channel < 0 || channel >= FALLBACK_NUM_CHANNELS)
    {
        return false;
    }

    return channels_playing[channel].sound != NULL;
}

static void I_SDLFallback_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    (void)sounds;
    (void)num_sounds;
}

static snddevice_t sound_fallback_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module =
{
    sound_fallback_devices,
    arrlen(sound_fallback_devices),
    I_SDLFallback_InitSound,
    I_SDLFallback_ShutdownSound,
    I_SDLFallback_GetSfxLumpNum,
    I_SDLFallback_UpdateSound,
    I_SDLFallback_UpdateSoundParams,
    I_SDLFallback_StartSound,
    I_SDLFallback_StopSound,
    I_SDLFallback_SoundIsPlaying,
    I_SDLFallback_PrecacheSounds,
};

// Music is intentionally not implemented in the fallback backend.

static snddevice_t music_fallback_devices[] = { SNDDEVICE_NONE };

static boolean I_FallbackMusic_Init(void) { return false; }
static void I_FallbackMusic_Shutdown(void) {}
static void I_FallbackMusic_SetVolume(int volume) { (void)volume; }
static void I_FallbackMusic_Pause(void) {}
static void I_FallbackMusic_Resume(void) {}
static void *I_FallbackMusic_Register(void *data, int len)
{
    (void)data;
    (void)len;
    return NULL;
}
static void I_FallbackMusic_Unregister(void *handle) { (void)handle; }
static void I_FallbackMusic_Play(void *handle, boolean looping)
{
    (void)handle;
    (void)looping;
}
static void I_FallbackMusic_Stop(void) {}
static boolean I_FallbackMusic_IsPlaying(void) { return false; }
static void I_FallbackMusic_Poll(void) {}

music_module_t DG_music_module =
{
    music_fallback_devices,
    arrlen(music_fallback_devices),
    I_FallbackMusic_Init,
    I_FallbackMusic_Shutdown,
    I_FallbackMusic_SetVolume,
    I_FallbackMusic_Pause,
    I_FallbackMusic_Resume,
    I_FallbackMusic_Register,
    I_FallbackMusic_Unregister,
    I_FallbackMusic_Play,
    I_FallbackMusic_Stop,
    I_FallbackMusic_IsPlaying,
    I_FallbackMusic_Poll,
};

void I_InitTimidityConfig(void)
{
}
