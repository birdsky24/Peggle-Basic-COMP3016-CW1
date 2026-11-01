#include "Audio.h"
#include "Config.h"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int SFX_COUNT = static_cast<int>(Sfx::Count);
    constexpr int SFX_TRACK_COUNT = 24; // sounds that can play at the same time

    MIX_Mixer* g_mixer = nullptr;
    MIX_Audio* g_sfx[SFX_COUNT] = {};
    MIX_Audio* g_background = nullptr;
    MIX_Audio* g_fever = nullptr;
    MIX_Audio* g_rouletteSpin = nullptr;

    MIX_Track* g_sfxTracks[SFX_TRACK_COUNT] = {};
    int        g_nextSfxTrack = 0;
    MIX_Track* g_musicTrack = nullptr;
    MIX_Track* g_spinTrack = nullptr;

    Music g_music = Music::None;
    bool  g_spinning = false;

    const char* SfxPath(Sfx sfx)
    {
        switch (sfx)
        {
        case Sfx::ButtonClick:  return SOUND_BUTTON_CLICK;
        case Sfx::PegHit:       return SOUND_PEG_HIT;
        case Sfx::PegRemove:    return SOUND_PEG_REMOVE;
        case Sfx::FreeBall:     return SOUND_FREE_BALL;
        case Sfx::RouletteLose: return SOUND_ROULETTE_LOSE;
        case Sfx::RouletteWin:  return SOUND_ROULETTE_WIN;
        case Sfx::Shoot:        return SOUND_SHOOT;
        case Sfx::Applause:     return SOUND_APPLAUSE;
        case Sfx::StyleShot:    return SOUND_STYLE_SHOT;
        case Sfx::PurplePeg:    return SOUND_PURPLE_PEG;
        case Sfx::SlowMotion:   return SOUND_SLOW_MOTION;
        default:                return "";
        }
    }

    // A missing file isn't an error, it just stays silent
    MIX_Audio* Load(const char* path, bool predecode)
    {
        MIX_Audio* audio = MIX_LoadAudio(g_mixer, path, predecode);
        if (!audio)
            SDL_Log("Sound not loaded (%s): %s", path, SDL_GetError());
        return audio;
    }

    // Starts a track from the beginning and loops it forever
    void PlayLooping(MIX_Track* track, int fadeInMs)
    {
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
        if (fadeInMs > 0)
            SDL_SetNumberProperty(props, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, fadeInMs);
        MIX_PlayTrack(track, props);
        SDL_DestroyProperties(props);
    }
}

bool AudioInit()
{
    if (!MIX_Init())
    {
        SDL_Log("MIX_Init failed: %s", SDL_GetError());
        return false;
    }

    g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!g_mixer)
    {
        SDL_Log("No audio device, the game will be silent: %s", SDL_GetError());
        MIX_Quit();
        return false;
    }

    // Short sounds are decoded up front, the songs are streamed from the file as they play
    for (int i = 0; i < SFX_COUNT; ++i)
        g_sfx[i] = Load(SfxPath(static_cast<Sfx>(i)), true);
    g_rouletteSpin = Load(SOUND_ROULETTE_SPIN, true);
    g_background = Load(MUSIC_BACKGROUND, false);
    g_fever = Load(MUSIC_FEVER, false);

    for (MIX_Track*& track : g_sfxTracks)
        track = MIX_CreateTrack(g_mixer);
    g_musicTrack = MIX_CreateTrack(g_mixer);
    g_spinTrack = MIX_CreateTrack(g_mixer);

    MIX_SetTrackGain(g_musicTrack, MUSIC_VOLUME);
    MIX_SetTrackGain(g_spinTrack, SFX_VOLUME);
    return true;
}

void AudioShutdown()
{
    if (!g_mixer) return;

    // Destroying the mixer stops and frees its tracks
    MIX_DestroyMixer(g_mixer);
    g_mixer = nullptr;

    for (MIX_Audio*& audio : g_sfx)
    {
        if (audio) MIX_DestroyAudio(audio);
        audio = nullptr;
    }
    for (MIX_Audio** audio : { &g_rouletteSpin, &g_background, &g_fever })
    {
        if (*audio) MIX_DestroyAudio(*audio);
        *audio = nullptr;
    }
    MIX_Quit();
}

void PlaySfx(Sfx sfx, float pitchSemitones)
{
    if (!g_mixer) return;
    MIX_Audio* audio = g_sfx[static_cast<int>(sfx)];
    if (!audio) return;

    // Tracks are used in turn, so if too many are playing the oldest one is cut off
    MIX_Track* track = g_sfxTracks[g_nextSfxTrack];
    g_nextSfxTrack = (g_nextSfxTrack + 1) % SFX_TRACK_COUNT;
    if (!track) return;

    MIX_SetTrackAudio(track, audio);
    MIX_SetTrackGain(track, SFX_VOLUME);
    MIX_SetTrackFrequencyRatio(track, std::pow(2.0f, pitchSemitones / 12.0f)); // changes the pitch
    MIX_PlayTrack(track, 0);
}

void SetMusic(Music music)
{
    if (!g_mixer || music == g_music) return;
    g_music = music;

    MIX_StopTrack(g_musicTrack, 0);

    MIX_Audio* audio = nullptr;
    if (music == Music::Background) audio = g_background;
    else if (music == Music::Fever) audio = g_fever;
    if (!audio) return;

    MIX_SetTrackAudio(g_musicTrack, audio);
    PlayLooping(g_musicTrack, 300);
}

void SetRouletteSpin(bool on)
{
    if (!g_mixer || on == g_spinning) return;
    g_spinning = on;

    if (!on)
    {
        MIX_StopTrack(g_spinTrack, 0);
        return;
    }
    if (!g_rouletteSpin) return;

    MIX_SetTrackAudio(g_spinTrack, g_rouletteSpin);
    PlayLooping(g_spinTrack, 0);
}
