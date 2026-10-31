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
constexpr float PEG_REMOVE_DELAY = 0.1f; // seconds between each hit peg being deleted after the shot is gone

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