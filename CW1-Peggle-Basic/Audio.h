#pragma once

// All the game's sounds. The files are named in Config.h (SOUND_* and MUSIC_*), so they can be renamed there.
// If a file is missing it is skipped (a message is logged), and if there is no audio device the game just runs silent.

enum class Sfx
{
    ButtonClick,
    PegHit,
    PegRemove,
    FreeBall,
    RouletteLose,
    RouletteWin,
    Shoot,
    Applause,
    StyleShot,
    PurplePeg,
    SlowMotion,
    Count
};

enum class Music
{
    None,
    Background,
    Fever
};

// Call once after SDL_Init(SDL_INIT_AUDIO). Returns false if there is no audio, the game still runs.
bool AudioInit();
void AudioShutdown();

// Plays a sound once. pitchSemitones raises (or lowers) the pitch, 12 is one octave up.
void PlaySfx(Sfx sfx, float pitchSemitones = 0.0f);

// Plays the song, swapping out the one that was playing. Does nothing if it is already playing.
void SetMusic(Music music);

// The sound that loops while the 50/50 is spinning
void SetRouletteSpin(bool on);
