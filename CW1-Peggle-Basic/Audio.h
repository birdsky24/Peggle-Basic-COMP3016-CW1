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
    FeverBucket,    // landing in a 10,000 or 50,000 extreme fever bucket
    FeverBucketTop, // landing in a 100,000 bucket
    FreeBallScore1, // the 3 free balls for a high scoring shot, one sound each
    FreeBallScore2,
    FreeBallScore3,
    SlowMotion,
    BucketBounce,   // the shot bounces off the edge of the moving bucket
    FeverBucketBounce, // the shot bounces off one of the big circles around the extreme fever buckets
    Count
};

enum class Music
{
    None,
    Menu,  // main menu and the pause, fail and clear screens
    Level, // while a level is being played
    Fever  // extreme fever
};

// Call once after SDL_Init(SDL_INIT_AUDIO). Returns false if there is no audio, the game still runs.
bool AudioInit();
void AudioShutdown();

// Plays a sound once. pitchSemitones raises (or lowers) the pitch, 12 is one octave up.
void PlaySfx(Sfx sfx, float pitchSemitones = 0.0f);

// Switches to a song. The one that was playing is paused (not stopped), so when it is switched back to
// it carries on from the same place. All songs loop. Does nothing if it is already playing.
void SetMusic(Music music);

// Starts the level and fever songs from the beginning next time they play, call it when a level (re)starts.
// The menu song is left alone so it still carries on from where it was.
void ResetLevelMusic();

// The sound that loops while the 50/50 is spinning
void SetRouletteSpin(bool on);
