#pragma once

// Settings shared between main.cpp and the level files

// Window size
constexpr int   WINDOW_W = 800;
constexpr int   WINDOW_H = 600;

// Black borders to leave room for ball count, score, multiplier later
constexpr float BORDER_LEFT = 50.0f;
constexpr float BORDER_RIGHT = 50.0f;
constexpr float BORDER_TOP = 50.0f;
constexpr float BORDER_BOTTOM = 20.0f;

// Defining the play area
constexpr float PLAY_LEFT = BORDER_LEFT;
constexpr float PLAY_RIGHT = WINDOW_W - BORDER_RIGHT;
constexpr float PLAY_TOP = BORDER_TOP;
constexpr float PLAY_BOTTOM = WINDOW_H - BORDER_BOTTOM;

// Ball and pegs
constexpr float BALL_RADIUS = 5.0f;
constexpr float PEG_RADIUS = 10.0f;
constexpr float PEG_BOUNCE = 0.85f;   // how bouncy the pegs are, 1.0 = no energy lost
constexpr int   ORANGE_PEG_COUNT = 25; // orange pegs per level, every level also gets 1 purple peg and the rest are blue
constexpr float PEG_REMOVE_MAX_DELAY = 0.2f; // longest gap between two deletions, so a few pegs don't take ages
constexpr float PEG_REMOVE_TOTAL_TIME = 1.5f; // seconds to delete every hit peg after the shot is gone, however many there are

// Physics
constexpr float GRAVITY = 900.0f; // speed is pixels / second^2
constexpr float SHOT_SPEED = 650.0f; // initial velocity
constexpr float WALL_BOUNCE = 0.95f; // speed lost after a bounce
constexpr float MAX_FRAME_DT = 0.05f;    // clamp huge frame times (e.g. when dragging the window)
constexpr float PHYSICS_STEP = 1.0f / 240.0f; // fixed physics timestep

// Launcher position
constexpr float LAUNCHER_X = (PLAY_LEFT + PLAY_RIGHT) / 2.0f;
constexpr float LAUNCHER_Y = 70.0f;

// The max angle you can shoot at (negative = slightly above horizontal)
constexpr float MIN_AIM_ANGLE_DEG = -10.0f;

// Shot guide
constexpr float GUIDE_LENGTH = 0.4f * (PLAY_BOTTOM - PLAY_TOP); // fixed length of the guide, 4/10 of the play area height
constexpr float GUIDE_DOT_SPACING = 20.0f;  // distance between dots along the path
constexpr float GUIDE_START_OFFSET = 28.0f;  // gap between launcher and first dot
constexpr float GUIDE_DOT_RADIUS = 4.0f;

// Score for each peg type before the fever multiplier
constexpr int   SCORE_BLUE = 10;
constexpr int   SCORE_ORANGE = 100;
constexpr int   SCORE_PURPLE = 500;

// Style shots, distances are measured as a fraction of the play area height
constexpr float LONG_SHOT_DISTANCE = (PLAY_BOTTOM - PLAY_TOP) / 3.0f;
constexpr float SUPER_LONG_SHOT_DISTANCE = (PLAY_BOTTOM - PLAY_TOP) * 2.0f / 3.0f;
constexpr float OFF_THE_WALL_DISTANCE = (PLAY_BOTTOM - PLAY_TOP) / 5.0f; // minimum distance travelled after the wall bounce
constexpr int   LONG_SHOT_POINTS = 5000;
constexpr int   SUPER_LONG_SHOT_POINTS = 25000;
constexpr int   OFF_THE_WALL_POINTS = 25000;
constexpr int   DOUBLE_LONG_SHOT_POINTS = 25000;
constexpr int   ORANGE_HAT_TRICK_POINTS = 100000;

// Orange Attack, fill a whole score multiplier stage in one shot. Each number is how many orange pegs
// must be hit for a stage to be full (x1, x2, x3, x5, x10). These match FeverMultiplier() in main.cpp.
constexpr int   ORANGE_STAGE_COUNT = 5;
constexpr int   ORANGE_STAGE_ENDS[ORANGE_STAGE_COUNT] = { 10, 15, 19, 22, ORANGE_PEG_COUNT };
constexpr int   ORANGE_ATTACK_POINTS = 50000;

// Bucket style shots
constexpr int   FREE_BALL_SKILLS_POINTS = 5000;   // into the bucket after hitting exactly one peg
constexpr int   LUCKY_BOUNCE_POINTS = 25000;      // bucket edge, then a peg, then into the bucket
constexpr int   KICK_THE_BUCKET_POINTS = 25000;   // bucket edge, then straight into the last orange peg
constexpr int   MAD_SKILLZ_STREAK = 5;            // bucket shots in a row
constexpr int   MAD_SKILLZ_POINTS = 25000;
constexpr int   CRAZY_MAD_SKILLZ_STREAK = 10;
constexpr int   CRAZY_MAD_SKILLZ_POINTS = 100000;

// Shots
constexpr int   STARTING_BALLS = 10;

// Extreme fever, 6 big circles along the bottom make 5 buckets between them
constexpr int   FEVER_BUCKET_COUNT = 5;
constexpr int   FEVER_BUCKET_POINTS[FEVER_BUCKET_COUNT] = { 10000, 50000, 100000, 50000, 10000 };
constexpr int   ULTRA_BUCKET_POINTS = 100000; // every bucket is worth this if every peg is cleared
constexpr int   UNUSED_BALL_POINTS = 10000;
constexpr float FEVER_CIRCLE_RADIUS = 30.0f;
constexpr float FEVER_CIRCLE_Y = PLAY_BOTTOM - 20.0f;
constexpr float FEVER_SLOWMO = 0.05f; // speed of time while it is slowed, 1.0 = normal speed
constexpr float SLOWMO_TRIGGER_DISTANCE = 90.0f; // time slows when the ball is this close to the last orange peg and heading for it

// Popup shown at the end of each shot
constexpr float POPUP_TIME = 1.5f; // seconds
constexpr float POPUP_FADE_TIME = 0.3f; // it fades out over the last bit of POPUP_TIME

// 50/50 for a shot that hits nothing, a missed shot gets one more go or is lost
constexpr float ROULETTE_TIME = 2.0f; // seconds the whole pop-up is on screen
constexpr float ROULETTE_SPIN_TIME = 1.5f; // seconds the numbers swing before the result is shown

// Small pop-ups that appear next to a peg in the play area
constexpr float PEG_POPUP_TIME = 0.1f; // seconds the points for a peg stay on screen
constexpr float PEG_POPUP_SCALE = 2.0f;
constexpr float STYLE_POPUP_TIME = 1.0f; // seconds a style shot pop-up stays on screen
constexpr float STYLE_POPUP_SCALE = 2.0f;

// Free balls for a high scoring shot. Each score is the shot's own score, so it starts again from 0 every shot.
constexpr int   FREE_BALL_THRESHOLD_COUNT = 3;
constexpr int   FREE_BALL_THRESHOLDS[FREE_BALL_THRESHOLD_COUNT] = { 25000, 75000, 125000 };
constexpr float FREE_BALL_FLASH_TIME = 0.6f; // how long a full bar and lit circle stay up before the next bar starts

// Ball stack and free ball bars in the left border
constexpr float STACK_BALL_RADIUS = 8.0f;
constexpr float STACK_BALL_PITCH = 20.0f; // gap between the balls, squashes together if there are too many
constexpr float FREE_BALL_BAR_WIDTH = 8.0f;
constexpr float FREE_BALL_CIRCLE_RADIUS = 21.0f;

// Fever meter in the right border
constexpr int   METER_SEGMENT_HEIGHT = 16;
constexpr int   METER_SEGMENT_GAP = 3;

// Leaderboard
constexpr const char* LEADERBOARD_FILE = "leaderboard.dat"; // encrypted and signed, saved next to where the game is run from
constexpr int   LEADERBOARD_SCORES_PER_LEVEL = 10; // best scores kept for each level, lower ones are deleted from the file

// Moving bucket at the bottom, the rectangle catches the shot and the triangles either side bounce it
constexpr float BUCKET_WIDTH = (PLAY_RIGHT - PLAY_LEFT) / 5.0f; // the rectangle is 1/5 of the play area
constexpr float BUCKET_HEIGHT = 24.0f;
constexpr float BUCKET_TRI_WIDTH = 24.0f;
constexpr float BUCKET_TRI_HEIGHT = 32.0f; // a bit taller than the rectangle so it has a rim
constexpr float BUCKET_SWEEP_TIME = 3.0f; // seconds from one wall to the other, including slowing down at the walls
constexpr float BUCKET_DWELL_TIME = 0.15f; // how long it is stopped at each wall
constexpr float BUCKET_RAMP_TIME = 0.6f; // how long it takes to slow down and speed back up at each wall
constexpr float BUCKET_BOUNCE_SOUND_MIN_SPEED = 150.0f; // the ball must hit a bucket (or an extreme fever circle) at least this fast to make a sound, so resting or sliding on it stays quiet

// Hold the right mouse button to speed the game up
constexpr float GAME_SPEED_FAST = 3.0f;

// Sounds. Every file is looked for in the folder the game is run from. These are placeholder names, change them to match
// your mp3 files. A file that is missing is skipped (a message is written to the log), so they can be added one at a time.
constexpr const char* SOUND_BUTTON_CLICK = "sounds/button2.ogg"; // clicking a button in a menu
constexpr const char* SOUND_PEG_HIT = "sounds/peg_hit.ogg";           // the ball hits a peg, higher each peg in the shot
constexpr const char* SOUND_PEG_REMOVE = "sounds/pegpop.ogg";     // a hit peg is deleted
constexpr const char* SOUND_FREE_BALL = "sounds/free_ball.ogg";       // catching the shot in the bucket or winning the 50/50
constexpr const char* SOUND_FREE_BALL_1 = "sounds/extraball.ogg";   // the shot's score reaches the 1st free ball (25,000)
constexpr const char* SOUND_FREE_BALL_2 = "sounds/extraball2.ogg";   // the 2nd free ball (75,000)
constexpr const char* SOUND_FREE_BALL_3 = "sounds/extraball3.ogg";   // the 3rd free ball (125,000)
constexpr const char* SOUND_FEVER_BUCKET = "sounds/fireworks1.ogg"; // the shot lands in one of the 10,000 or 50,000 extreme fever buckets
constexpr const char* SOUND_FEVER_BUCKET_TOP = "sounds/fireworks2.ogg"; // the shot lands in a 100,000 bucket
constexpr const char* SOUND_ROULETTE_SPIN = "sounds/roulette_spin.ogg"; // loops while the 50/50 numbers swing
constexpr const char* SOUND_ROULETTE_LOSE = "sounds/roulette_lose.ogg";
constexpr const char* SOUND_ROULETTE_WIN = "sounds/free_ball.ogg";
constexpr const char* SOUND_SHOOT = "sounds/cannonshot.ogg";               // the ball is fired
constexpr const char* SOUND_APPLAUSE = "sounds/applause.ogg";         // the last orange peg is hit
constexpr const char* SOUND_STYLE_SHOT = "sounds/gapbonus1.ogg";     // any style shot
constexpr const char* SOUND_PURPLE_PEG = "sounds/peghit_plus_mega9.ogg";     // the ball hits the purple peg
constexpr const char* SOUND_SLOW_MOTION = "sounds/timpaniroll.ogg";   // time slows down just before the last orange peg is hit
constexpr const char* SOUND_BUCKET_BOUNCE = "sounds/buckethit.ogg"; // the shot bounces off the edge of the moving bucket
constexpr const char* SOUND_FEVER_BUCKET_BOUNCE = "sounds/cymbal.ogg"; // the shot bounces off a big circle between the extreme fever buckets
constexpr const char* MUSIC_MENU = "sounds/Peer Gynt Suite No. 1, Op. 46 - Morning Mood.mp3";         // loops in the main menu and the pause, fail and clear screens
constexpr const char* MUSIC_LEVEL = "sounds/Peggle Beat.mp3";  // loops while a level is being played
constexpr const char* MUSIC_FEVER = "sounds/Ode to Joy.mp3";       // loops during extreme fever

constexpr float SFX_VOLUME = 0.8f;   // 0.0 to 1.0
constexpr float MUSIC_VOLUME = 0.5f;

// Each different peg hit in a shot is this many semitones higher than the one before (12 = one octave), up to the max.
// It starts again from normal pitch every shot.
constexpr float PEG_HIT_PITCH_STEP = 1.0f;
constexpr float PEG_HIT_PITCH_MAX = 24.0f;
