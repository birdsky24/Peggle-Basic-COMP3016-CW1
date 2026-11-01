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
    MIX_Audio* g_rouletteSpin = nullptr;

    // Each song has its own track, so pausing one keeps its place
    struct Song
    {
        MIX_Audio* audio = nullptr;
        MIX_Track* track = nullptr;
    };
    Song g_menuSong;
    Song g_levelSong;
    Song g_feverSong;

    MIX_Track* g_sfxTracks[SFX_TRACK_COUNT] = {};
    int        g_nextSfxTrack = 0;
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
        case Sfx::FeverBucket:  return SOUND_FEVER_BUCKET;
        case Sfx::FeverBucketTop: return SOUND_FEVER_BUCKET_TOP;
        case Sfx::FreeBallScore1: return SOUND_FREE_BALL_1;
        case Sfx::FreeBallScore2: return SOUND_FREE_BALL_2;
        case Sfx::FreeBallScore3: return SOUND_FREE_BALL_3;
        case Sfx::SlowMotion:   return SOUND_SLOW_MOTION;
        case Sfx::BucketBounce: return SOUND_BUCKET_BOUNCE;
        case Sfx::FeverBucketBounce: return SOUND_FEVER_BUCKET_BOUNCE;
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

    Song* SongFor(Music music)
    {
        Song* song = nullptr;
        if (music == Music::Menu) song = &g_menuSong;
        else if (music == Music::Level) song = &g_levelSong;
        else if (music == Music::Fever) song = &g_feverSong;
        return (song && song->audio && song->track) ? song : nullptr;
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
    g_menuSong.audio = Load(MUSIC_MENU, false);
    g_levelSong.audio = Load(MUSIC_LEVEL, false);
    g_feverSong.audio = Load(MUSIC_FEVER, false);

    for (MIX_Track*& track : g_sfxTracks)
        track = MIX_CreateTrack(g_mixer);
    g_spinTrack = MIX_CreateTrack(g_mixer);

    for (Song* song : { &g_menuSong, &g_levelSong, &g_feverSong })
    {
        song->track = MIX_CreateTrack(g_mixer);
        if (song->track && song->audio)
        {
            MIX_SetTrackAudio(song->track, song->audio);
            MIX_SetTrackGain(song->track, MUSIC_VOLUME);
        }
    }
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
    for (MIX_Audio** audio : { &g_rouletteSpin, &g_menuSong.audio, &g_levelSong.audio, &g_feverSong.audio })
    {
        if (*audio) MIX_DestroyAudio(*audio);
        *audio = nullptr;
    }
    g_menuSong.track = g_levelSong.track = g_feverSong.track = nullptr;
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

    // The song that was playing is paused, not stopped, so it carries on from the same place later
    if (Song* old = SongFor(g_music))
        MIX_PauseTrack(old->track);
    g_music = music;

    Song* song = SongFor(music);
    if (!song) return;

    if (MIX_TrackPaused(song->track))
        MIX_ResumeTrack(song->track);
    else if (!MIX_TrackPlaying(song->track))
        PlayLooping(song->track, 300); // it was stopped (or never played), so it starts from the beginning
}

void ResetLevelMusic()
{
    if (!g_mixer) return;

    for (Song* song : { &g_levelSong, &g_feverSong })
        if (song->track) MIX_StopTrack(song->track, 0);

    // Make sure the next SetMusic starts the song again
    if (g_music == Music::Level || g_music == Music::Fever)
        g_music = Music::None;
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
