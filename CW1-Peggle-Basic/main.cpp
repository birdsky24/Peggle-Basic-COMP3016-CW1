#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Config.h"
#include "Level.h"
#include "Leaderboard.h"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
struct Ball
{
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    bool  active = false;
};

struct StyleAward
{
    std::string name;
    int points;
};

// Everything tracked for the shot that is currently in play
struct ShotStats
{
    int pegScoreSum = 0; // base score of every peg hit (with the fever multiplier)
    int pegsHit = 0;
    int stylePoints = 0;
    std::vector<StyleAward> styles;
    int orangeAtStart = 0; // orange pegs already hit before this shot
    float distSinceContact = 0.0f; // how far the ball has travelled since it last touched a peg
    int lastContactPeg = -1;
    bool lastContactNonBlue = false;
    bool bouncedOffWall = false; // bounced off a side wall since the last peg
    float distSinceWall = 0.0f; // how far the ball has travelled since that bounce
    int consecutiveLong = 0; // long shots in a row
};

struct GameState
{
    long long score = 0;
    int ballsLeft = STARTING_BALLS;
    int orangeHit = 0;
    int totalOrange = 0;
    bool shotInProgress = false; // true from firing until the hit pegs are deleted
    bool feverActive = false; // the last orange peg has been hit
    bool ultraFever = false;
    int bucketPoints = 0;
    bool levelComplete = false;
    bool freeBall = false; // this shot was caught by the bucket
    float bucketTime = BUCKET_SWEEP_TIME * 0.5f; // time through the bucket's back and forth movement
    float bucketX = LAUNCHER_X; // centre of the bucket
    ShotStats shot;
};

// The box in the middle of the screen at the end of each shot
struct Popup
{
    std::vector<std::string> lines;
    float timer = 0.0f;
};

enum class Screen
{
    MainMenu,
    Playing,
    Paused,
    Fail,
    LevelClear
};

enum class Action
{
    None, // a greyed out button
    Play,
    Resume,
    Quit,
    OpenLevelSelect,
    CloseLevelSelect,
    GoToLevel,
    Restart,
    MainMenu,
    NextLevel,
    SaveScore
};

struct Button
{
    SDL_FRect rect;
    std::string label;
    Action action;
    int level; // only used by GoToLevel
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void DrawFilledCircle(SDL_Renderer* r, float cx, float cy, float radius)
{
    // SDL3 has no circle primitive, so draw one horizontal line per row.
    const int ir = static_cast<int>(std::ceil(radius));
    for (int dy = -ir; dy <= ir; ++dy)
    {
        const float h = std::sqrt(std::max(0.0f, radius * radius - static_cast<float>(dy * dy)));
        SDL_RenderLine(r, cx - h, cy + dy, cx + h, cy + dy);
    }
}

// Turns 1234567 into "1,234,567"
static std::string FormatNumber(long long n)
{
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(i, ",");
    return s;
}

// SDL's built in debug font is 8x8 pixels per letter, scale makes it bigger
static float TextWidth(const std::string& text, float scale)
{
    return static_cast<float>(text.size()) * 8.0f * scale;
}

static void DrawText(SDL_Renderer* r, const std::string& text, float x, float y, float scale)
{
    SDL_SetRenderScale(r, scale, scale);
    SDL_RenderDebugText(r, x / scale, y / scale, text.c_str());
    SDL_SetRenderScale(r, 1.0f, 1.0f);
}

static bool PointInRect(float x, float y, const SDL_FRect& r)
{
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

// Darkens everything drawn so far, used behind pop-ups
static void DrawDim(SDL_Renderer* r, Uint8 alpha)
{
    const SDL_FRect all = { 0.0f, 0.0f, static_cast<float>(WINDOW_W), static_cast<float>(WINDOW_H) };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, alpha);
    SDL_RenderFillRect(r, &all);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void DrawPanel(SDL_Renderer* r, const SDL_FRect& panel)
{
    SDL_SetRenderDrawColor(r, 20, 24, 40, 255);
    SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawColor(r, 200, 200, 210, 255);
    SDL_RenderRect(r, &panel);
}

static void DrawButtons(SDL_Renderer* r, const std::vector<Button>& buttons, float mouseX, float mouseY)
{
    for (const Button& b : buttons)
    {
        const bool enabled = (b.action != Action::None);
        const bool hover = enabled && PointInRect(mouseX, mouseY, b.rect);

        if (!enabled) SDL_SetRenderDrawColor(r, 35, 38, 50, 255);
        else if (hover) SDL_SetRenderDrawColor(r, 80, 110, 170, 255);
        else SDL_SetRenderDrawColor(r, 45, 60, 100, 255);
        SDL_RenderFillRect(r, &b.rect);

        if (enabled) SDL_SetRenderDrawColor(r, 200, 200, 210, 255);
        else SDL_SetRenderDrawColor(r, 80, 80, 90, 255);
        SDL_RenderRect(r, &b.rect);

        // Text is centred, the small square level buttons get bigger text
        const float scale = (b.rect.w < 100.0f) ? 3.0f : 2.0f;
        if (enabled) SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        else SDL_SetRenderDrawColor(r, 110, 110, 120, 255);
        DrawText(r, b.label,
                 b.rect.x + (b.rect.w - TextWidth(b.label, scale)) / 2.0f,
                 b.rect.y + (b.rect.h - 8.0f * scale) / 2.0f, scale);
    }
}

// The level selector pop-up, a grid of numbers (5 per row) and a close button
constexpr int LEVELS_PER_ROW = 5;

static SDL_FRect LevelSelectPanel(int levelCount)
{
    const int rows = (levelCount + LEVELS_PER_ROW - 1) / LEVELS_PER_ROW;
    return { LAUNCHER_X - 220.0f, 120.0f, 440.0f, 140.0f + rows * 72.0f };
}

static std::vector<Button> GetLevelSelectButtons(int levelCount)
{
    std::vector<Button> buttons;
    const SDL_FRect panel = LevelSelectPanel(levelCount);
    const float size = 60.0f;
    const float step = 72.0f;
    const int rows = (levelCount + LEVELS_PER_ROW - 1) / LEVELS_PER_ROW;

    for (int i = 0; i < levelCount; ++i)
    {
        const int row = i / LEVELS_PER_ROW;
        const int col = i % LEVELS_PER_ROW;
        const int colsInRow = std::min(LEVELS_PER_ROW, levelCount - row * LEVELS_PER_ROW);
        const float x = LAUNCHER_X - (colsInRow * step - (step - size)) / 2.0f + col * step;
        const float y = panel.y + 70.0f + row * step;
        buttons.push_back({ { x, y, size, size }, std::to_string(i + 1), Action::GoToLevel, i + 1 });
    }

    buttons.push_back({ { LAUNCHER_X - 80.0f, panel.y + 70.0f + rows * step + 10.0f, 160.0f, 40.0f }, "CLOSE", Action::CloseLevelSelect, 0 });
    return buttons;
}

static void DrawLevelSelect(SDL_Renderer* r, int levelCount, float mouseX, float mouseY)
{
    DrawDim(r, 190);
    DrawPanel(r, LevelSelectPanel(levelCount));

    const std::string title = "SELECT LEVEL";
    SDL_SetRenderDrawColor(r, 255, 220, 80, 255);
    DrawText(r, title, LAUNCHER_X - TextWidth(title, 3.0f) / 2.0f, LevelSelectPanel(levelCount).y + 22.0f, 3.0f);
    DrawButtons(r, GetLevelSelectButtons(levelCount), mouseX, mouseY);
}

// The buttons for each screen, used both to draw them and to click them
static std::vector<Button> GetScreenButtons(Screen screen, int currentLevel, int levelCount, bool canSave, bool scoreSaved)
{
    std::vector<Button> buttons;

    if (screen == Screen::MainMenu)
    {
        buttons.push_back({ { 60.0f, 170.0f, 240.0f, 50.0f }, "PLAY", Action::Play, 0 });
        buttons.push_back({ { 60.0f, 240.0f, 240.0f, 50.0f }, "LEVEL SELECT", Action::OpenLevelSelect, 0 });
        buttons.push_back({ { 60.0f, 310.0f, 240.0f, 50.0f }, "QUIT", Action::Quit, 0 });
    }
    else if (screen == Screen::Paused)
    {
        const float x = LAUNCHER_X - 150.0f;
        buttons.push_back({ { x, 250.0f, 300.0f, 44.0f }, "RESUME", Action::Resume, 0 });
        buttons.push_back({ { x, 304.0f, 300.0f, 44.0f }, "RESTART", Action::Restart, 0 });
        buttons.push_back({ { x, 358.0f, 300.0f, 44.0f }, "MAIN MENU", Action::MainMenu, 0 });
        buttons.push_back({ { x, 412.0f, 300.0f, 44.0f }, "LEVEL SELECT", Action::OpenLevelSelect, 0 });
    }
    else if (screen == Screen::Fail)
    {
        const float x = LAUNCHER_X - 150.0f;
        buttons.push_back({ { x, 250.0f, 300.0f, 44.0f }, "RESTART", Action::Restart, 0 });
        buttons.push_back({ { x, 304.0f, 300.0f, 44.0f }, "MAIN MENU", Action::MainMenu, 0 });
        buttons.push_back({ { x, 358.0f, 300.0f, 44.0f }, "LEVEL SELECT", Action::OpenLevelSelect, 0 });
    }
    else if (screen == Screen::LevelClear)
    {
        const float x = LAUNCHER_X - 150.0f;
        float y = 250.0f;

        // Greyed out until 3 letters are typed, and again once it is saved
        buttons.push_back({ { x, y, 300.0f, 44.0f }, scoreSaved ? "SAVED" : "SAVE SCORE", (canSave && !scoreSaved) ? Action::SaveScore : Action::None, 0 });
        y += 54.0f;
        if (currentLevel < levelCount)
        {
            buttons.push_back({ { x, y, 300.0f, 44.0f }, "NEXT LEVEL", Action::NextLevel, 0 });
            y += 54.0f;
        }
        buttons.push_back({ { x, y, 300.0f, 44.0f }, "MAIN MENU", Action::MainMenu, 0 });
        y += 54.0f;
        buttons.push_back({ { x, y, 300.0f, 44.0f }, "LEVEL SELECT", Action::OpenLevelSelect, 0 });
    }
    return buttons;
}

static int BasePegScore(PegType type)
{
    if (type == PegType::Orange) return SCORE_ORANGE;
    if (type == PegType::Purple) return SCORE_PURPLE;
    return SCORE_BLUE;
}

// Fever meter multiplier from how many orange pegs have been hit (25 orange pegs)
static int FeverMultiplier(int orangeHit)
{
    if (orangeHit >= 22) return 10; // 3 left
    if (orangeHit >= 19) return 5;  // 6 left
    if (orangeHit >= 15) return 3;  // 10 left
    if (orangeHit >= 10) return 2;  // 15 left
    return 1;
}

static void AddStyle(ShotStats& shot, const char* name, int points)
{
    shot.styles.push_back({ name, points });
    shot.stylePoints += points;
}

static bool AllPegsCleared(const std::vector<Peg>& pegs)
{
    for (const Peg& peg : pegs)
        if (!peg.hit && !peg.removed) return false;
    return true;
}

// x position of the big circles used for extreme fever, 0 is half inside the left wall
static float FeverCircleX(int k)
{
    return PLAY_LEFT + k * (PLAY_RIGHT - PLAY_LEFT) / static_cast<float>(FEVER_BUCKET_COUNT);
}

// Works out where to fire the ball
static void GetAimDirection(float mouseX, float mouseY, float& dirX, float& dirY)
{
    const float dx = mouseX - LAUNCHER_X;
    const float dy = mouseY - LAUNCHER_Y;   // +y is down on screen

    const float minAngle = MIN_AIM_ANGLE_DEG * SDL_PI_F / 180.0f;

    // Solve for the right hand side only, then flip x if the mouse is on the left
    const float side = (dx >= 0.0f) ? 1.0f : -1.0f;
    const float adx = std::fabs(dx);

    float angle;
    if (adx < 1.0f)
    {
        angle = SDL_PI_F / 2.0f; // mouse is directly below, shoot straight down
    }
    else
    {
        // Path is: dy = adx*tan(a) + g*adx^2 / (2*v^2*cos^2(a)), a quadratic in tan(a)
        const float a = GRAVITY * adx * adx / (2.0f * SHOT_SPEED * SHOT_SPEED);
        const float disc = adx * adx - 4.0f * a * (a - dy);

        if (disc >= 0.0f)
            angle = std::atan((-adx + std::sqrt(disc)) / (2.0f * a));
        else
            angle = std::atan2(dy, adx); // out of range, just aim at the mouse
    }

    // Don't let the player aim past the limit
    angle = std::max(angle, minAngle);

    dirX = side * std::cos(angle);
    dirY = std::sin(angle);
}

// Creates a dotted guide for the shot
static void DrawAimGuide(SDL_Renderer* r, float dirX, float dirY)
{
    float x = LAUNCHER_X;
    float y = LAUNCHER_Y;
    float vx = dirX * SHOT_SPEED;
    float vy = dirY * SHOT_SPEED;

    const float dt = 1.0f / 240.0f;
    float travelled = 0.0f;
    float nextDot = GUIDE_START_OFFSET;
    bool  leftScreen = false;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    while (travelled < GUIDE_LENGTH)
    {
        vy += GRAVITY * dt;
        const float nx = x + vx * dt;
        const float ny = y + vy * dt;
        travelled += std::sqrt((nx - x) * (nx - x) + (ny - y) * (ny - y));
        x = nx;
        y = ny;

        if (x < PLAY_LEFT || x > PLAY_RIGHT || y > PLAY_BOTTOM)
        {
            leftScreen = true;
            break;
        }

        if (travelled >= nextDot)
        {
            // Dots fade out towards the end of the guide
            const float t = travelled / GUIDE_LENGTH;
            const Uint8 alpha = static_cast<Uint8>(255.0f * (1.0f - 0.6f * t));
            SDL_SetRenderDrawColor(r, 120, 230, 235, alpha);
            DrawFilledCircle(r, x, y, GUIDE_DOT_RADIUS);
            nextDot += GUIDE_DOT_SPACING;
        }
    }

    // Arrow at the end of the guide
    if (!leftScreen)
    {
        const float len = std::sqrt(vx * vx + vy * vy);
        const float tx = vx / len, ty = vy / len;   // tangent
        const float nx = -ty, ny = tx;         // normal
        const float size = 11.0f;
        const SDL_FColor col = { 0.47f, 0.9f, 0.92f, 0.5f };

        SDL_Vertex v[3] = {};
        v[0].position = { x + tx * size,           y + ty * size };
        v[1].position = { x - tx * size + nx * size * 0.7f, y - ty * size + ny * size * 0.7f };
        v[2].position = { x - tx * size - nx * size * 0.7f, y - ty * size - ny * size * 0.7f };
        for (auto& vert : v) vert.color = col;
        SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void FireBall(Ball& ball, float dirX, float dirY)
{
    ball.x = LAUNCHER_X;
    ball.y = LAUNCHER_Y;
    ball.vx = dirX * SHOT_SPEED;
    ball.vy = dirY * SHOT_SPEED;
    ball.active = true;
}

// Pushes the ball out of a circle that doesn't score and bounces it off
static void BounceOffCircle(Ball& ball, float cx, float cy, float radius)
{
    const float dx = ball.x - cx;
    const float dy = ball.y - cy;
    const float minDist = BALL_RADIUS + radius;
    const float distSq = dx * dx + dy * dy;
    if (distSq >= minDist * minDist) return;

    const float dist = std::sqrt(distSq);
    float nx = 0.0f, ny = -1.0f;
    if (dist > 0.0001f)
    {
        nx = dx / dist;
        ny = dy / dist;
    }

    ball.x = cx + nx * minDist;
    ball.y = cy + ny * minDist;

    const float vn = ball.vx * nx + ball.vy * ny;
    if (vn < 0.0f)
    {
        ball.vx -= (1.0f + PEG_BOUNCE) * vn * nx;
        ball.vy -= (1.0f + PEG_BOUNCE) * vn * ny;
    }
}

struct Triangle
{
    SDL_FPoint p[3];
};

// How far along the sweep the bucket is (0 to 1) after t seconds. It waits a moment at the wall,
// smoothly speeds up, cruises, then smoothly slows to a stop at the other wall.
static float BucketProgress(float t)
{
    const float dwell = BUCKET_DWELL_TIME;
    const float ramp = BUCKET_RAMP_TIME;
    const float cruise = BUCKET_SWEEP_TIME - dwell - 2.0f * ramp;
    const float total = ramp + cruise; // distance covered, in units of the cruise speed

    // Distance covered while speeding up for u seconds out of ramp (smooth speed curve)
    auto rampDistance = [](float u) { return u * u * u - u * u * u * u / 2.0f; };

    float d;
    t -= dwell;
    if (t <= 0.0f)
    {
        d = 0.0f;
    }
    else if (t < ramp)
    {
        d = ramp * rampDistance(t / ramp);
    }
    else if (t < ramp + cruise)
    {
        d = ramp * 0.5f + (t - ramp);
    }
    else
    {
        const float u = std::min(1.0f, (t - ramp - cruise) / ramp);
        d = ramp * 0.5f + cruise + ramp * (0.5f - rampDistance(1.0f - u));
    }
    return d / total;
}

static void UpdateBucket(GameState& game, float dt)
{
    // One full back and forth is two sweeps
    game.bucketTime += dt;
    if (game.bucketTime >= 2.0f * BUCKET_SWEEP_TIME)
        game.bucketTime -= 2.0f * BUCKET_SWEEP_TIME;

    const bool returning = game.bucketTime >= BUCKET_SWEEP_TIME;
    const float t = returning ? game.bucketTime - BUCKET_SWEEP_TIME : game.bucketTime;
    float p = BucketProgress(t);
    if (returning) p = 1.0f - p;

    // The edge of the rectangle touches the wall at each end
    const float minX = PLAY_LEFT + BUCKET_WIDTH / 2.0f;
    const float maxX = PLAY_RIGHT - BUCKET_WIDTH / 2.0f;
    game.bucketX = minX + p * (maxX - minX);
}

static void GetBucketShape(float centreX, SDL_FRect& rect, Triangle& left, Triangle& right)
{
    rect = { centreX - BUCKET_WIDTH / 2.0f, PLAY_BOTTOM - BUCKET_HEIGHT, BUCKET_WIDTH, BUCKET_HEIGHT };
    const float l = rect.x;
    const float r = rect.x + rect.w;
    const float apex = PLAY_BOTTOM - BUCKET_TRI_HEIGHT;

    // Right angle against the side of the rectangle, sloping down away from it
    left = { { { l, apex }, { l - BUCKET_TRI_WIDTH, PLAY_BOTTOM }, { l, PLAY_BOTTOM } } };
    right = { { { r, apex }, { r, PLAY_BOTTOM }, { r + BUCKET_TRI_WIDTH, PLAY_BOTTOM } } };
}

// Pushes the ball out of a triangle and bounces it off the nearest edge
static void BounceOffTriangle(Ball& ball, const Triangle& tri)
{
    // Closest point on the triangle's edges to the ball's centre
    float bestX = 0.0f, bestY = 0.0f;
    float bestDistSq = 1.0e30f;
    for (int i = 0; i < 3; ++i)
    {
        const SDL_FPoint a = tri.p[i];
        const SDL_FPoint b = tri.p[(i + 1) % 3];
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        float t = ((ball.x - a.x) * abx + (ball.y - a.y) * aby) / (abx * abx + aby * aby);
        t = std::max(0.0f, std::min(t, 1.0f));
        const float qx = a.x + t * abx;
        const float qy = a.y + t * aby;
        const float distSq = (ball.x - qx) * (ball.x - qx) + (ball.y - qy) * (ball.y - qy);
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestX = qx;
            bestY = qy;
        }
    }

    // Is the ball's centre inside the triangle
    float side[3];
    for (int i = 0; i < 3; ++i)
    {
        const SDL_FPoint a = tri.p[i];
        const SDL_FPoint b = tri.p[(i + 1) % 3];
        side[i] = (b.x - a.x) * (ball.y - a.y) - (b.y - a.y) * (ball.x - a.x);
    }
    const bool inside = (side[0] >= 0.0f && side[1] >= 0.0f && side[2] >= 0.0f) ||
                        (side[0] <= 0.0f && side[1] <= 0.0f && side[2] <= 0.0f);

    const float dist = std::sqrt(bestDistSq);
    if (!inside && dist >= BALL_RADIUS) return;

    float nx = 0.0f, ny = -1.0f;
    if (dist > 0.0001f)
    {
        // Normal points away from the triangle, or towards the nearest edge if the centre is inside
        nx = (inside ? bestX - ball.x : ball.x - bestX) / dist;
        ny = (inside ? bestY - ball.y : ball.y - bestY) / dist;
    }

    ball.x = bestX + nx * BALL_RADIUS;
    ball.y = bestY + ny * BALL_RADIUS;

    const float vn = ball.vx * nx + ball.vy * ny;
    if (vn < 0.0f)
    {
        ball.vx -= (1.0f + PEG_BOUNCE) * vn * nx;
        ball.vy -= (1.0f + PEG_BOUNCE) * vn * ny;
    }
}

// hitOrder holds the index of every peg hit by this shot, in the order they were hit
static void UpdateBall(Ball& ball, std::vector<Peg>& pegs, std::vector<int>& hitOrder, GameState& game, float dt)
{
    if (!ball.active) return;
    ShotStats& shot = game.shot;

    // Gravity then move (semi-implicit Euler)
    ball.vy += GRAVITY * dt;
    const float moveX = ball.vx * dt;
    const float moveY = ball.vy * dt;
    ball.x += moveX;
    ball.y += moveY;
    const float moved = std::sqrt(moveX * moveX + moveY * moveY);
    shot.distSinceContact += moved;
    shot.distSinceWall += moved;

    // Left wall
    if (ball.x - BALL_RADIUS < PLAY_LEFT)
    {
        ball.x = PLAY_LEFT + BALL_RADIUS;
        ball.vx = std::fabs(ball.vx) * WALL_BOUNCE;
        shot.bouncedOffWall = true;
        shot.distSinceWall = 0.0f;
    }
    // Right wall
    else if (ball.x + BALL_RADIUS > PLAY_RIGHT)
    {
        ball.x = PLAY_RIGHT - BALL_RADIUS;
        ball.vx = -std::fabs(ball.vx) * WALL_BOUNCE;
        shot.bouncedOffWall = true;
        shot.distSinceWall = 0.0f;
    }

    // Ceiling
    if (ball.y - BALL_RADIUS < PLAY_TOP)
    {
        ball.y = PLAY_TOP + BALL_RADIUS;
        ball.vy = std::fabs(ball.vy) * WALL_BOUNCE;
    }

    // If the ball overlaps with a peg it is pushed back
    for (int i = 0; i < static_cast<int>(pegs.size()); ++i)
    {
        Peg& peg = pegs[i];
        if (peg.removed) continue;

        const float dx = ball.x - peg.x;
        const float dy = ball.y - peg.y;
        const float minDist = BALL_RADIUS + PEG_RADIUS;
        const float distSq = dx * dx + dy * dy;
        if (distSq >= minDist * minDist) continue;

        // Style shots are checked the first time the ball touches a peg that isn't the last one it touched
        if (i != shot.lastContactPeg)
        {
            const bool nonBlue = (peg.type != PegType::Blue);

            // Long shots, travel a long way from a non-blue peg to another non-blue peg
            bool longShot = false;
            if (nonBlue && shot.lastContactNonBlue)
            {
                if (shot.distSinceContact >= SUPER_LONG_SHOT_DISTANCE)
                {
                    AddStyle(shot, "SUPER LONG SHOT", SUPER_LONG_SHOT_POINTS);
                    longShot = true;
                }
                else if (shot.distSinceContact >= LONG_SHOT_DISTANCE)
                {
                    AddStyle(shot, "LONG SHOT", LONG_SHOT_POINTS);
                    longShot = true;
                }
            }

            // Two long shots in a row
            if (longShot)
            {
                shot.consecutiveLong++;
                if (shot.consecutiveLong >= 2)
                {
                    AddStyle(shot, "DOUBLE LONG SHOT", DOUBLE_LONG_SHOT_POINTS);
                    shot.consecutiveLong = 0;
                }
            }
            else
            {
                shot.consecutiveLong = 0;
            }

            // Bounced off a side wall, travelled far enough and then hit a non-blue peg
            if (shot.bouncedOffWall && nonBlue && shot.distSinceWall >= OFF_THE_WALL_DISTANCE)
                AddStyle(shot, "OFF THE WALL", OFF_THE_WALL_POINTS);
            shot.bouncedOffWall = false;

            shot.lastContactPeg = i;
            shot.lastContactNonBlue = nonBlue;
        }
        shot.distSinceContact = 0.0f;

        // Mark the peg for deletion, only the first hit counts so it can't be added twice
        if (!peg.hit)
        {
            peg.hit = true;
            hitOrder.push_back(i);

            // Orange pegs light up the fever meter before the peg is scored
            if (peg.type == PegType::Orange)
                game.orangeHit++;

            shot.pegScoreSum += BasePegScore(peg.type) * FeverMultiplier(game.orangeHit);
            shot.pegsHit++;

            // The last orange peg starts extreme fever
            if (peg.type == PegType::Orange && game.orangeHit == game.totalOrange)
            {
                game.feverActive = true;

                // Hat trick, the last 3 orange pegs were all hit in this shot
                if (game.totalOrange >= 3 && shot.orangeAtStart <= game.totalOrange - 3)
                    AddStyle(shot, "ORANGE HAT TRICK", ORANGE_HAT_TRICK_POINTS);
            }
        }

        // Normal points from the peg centre to the ball centre
        const float dist = std::sqrt(distSq);
        float nx = 0.0f, ny = -1.0f;
        if (dist > 0.0001f)
        {
            nx = dx / dist;
            ny = dy / dist;
        }

        ball.x = peg.x + nx * minDist;
        ball.y = peg.y + ny * minDist;

        // Only bounce if the ball is moving into the peg
        const float vn = ball.vx * nx + ball.vy * ny;
        if (vn < 0.0f)
        {
            ball.vx -= (1.0f + PEG_BOUNCE) * vn * nx;
            ball.vy -= (1.0f + PEG_BOUNCE) * vn * ny;
        }
    }

    // The bucket, it goes away during extreme fever
    if (!game.feverActive)
    {
        SDL_FRect bucketRect;
        Triangle leftTri, rightTri;
        GetBucketShape(game.bucketX, bucketRect, leftTri, rightTri);
        BounceOffTriangle(ball, leftTri);
        BounceOffTriangle(ball, rightTri);

        // Dropping into the rectangle deletes the shot and gives the shot back
        if (ball.x >= bucketRect.x && ball.x <= bucketRect.x + bucketRect.w && ball.y + BALL_RADIUS >= bucketRect.y)
        {
            ball.active = false;
            game.ballsLeft++;
            game.freeBall = true;
            return;
        }
    }

    // Extreme fever, the big circles at the bottom are solid
    if (game.feverActive)
    {
        for (int k = 0; k <= FEVER_BUCKET_COUNT; ++k)
            BounceOffCircle(ball, FeverCircleX(k), FEVER_CIRCLE_Y, FEVER_CIRCLE_RADIUS);
    }

    // shot is deleted if off the screen
    if (ball.y - BALL_RADIUS > PLAY_BOTTOM)
    {
        ball.active = false;

        // In extreme fever the gap the ball fell through decides the bucket points
        if (game.feverActive)
        {
            const float bucketWidth = (PLAY_RIGHT - PLAY_LEFT) / static_cast<float>(FEVER_BUCKET_COUNT);
            int bucket = static_cast<int>((ball.x - PLAY_LEFT) / bucketWidth);
            bucket = std::max(0, std::min(bucket, FEVER_BUCKET_COUNT - 1));

            game.ultraFever = AllPegsCleared(pegs);
            game.bucketPoints = game.ultraFever ? ULTRA_BUCKET_POINTS : FEVER_BUCKET_POINTS[bucket];
        }
    }
}

// Works out the score for the shot that just finished and fills in the popup
static void FinishShot(GameState& game, std::vector<Peg>& pegs, Popup& popup)
{
    const ShotStats& shot = game.shot;
    const long long pegTotal = static_cast<long long>(shot.pegScoreSum) * shot.pegsHit;
    const long long shotScore = pegTotal + shot.stylePoints;

    popup.lines.clear();
    if (game.feverActive)
        popup.lines.push_back(game.ultraFever ? "ULTRA EXTREME FEVER!" : "EXTREME FEVER!");

    popup.lines.push_back("PEGS " + FormatNumber(shot.pegScoreSum) + " X " + std::to_string(shot.pegsHit) + " = " + FormatNumber(pegTotal));
    for (const StyleAward& style : shot.styles)
        popup.lines.push_back(style.name + " +" + FormatNumber(style.points));

    if (game.feverActive)
    {
        const long long ballBonus = static_cast<long long>(game.ballsLeft) * UNUSED_BALL_POINTS;
        const long long feverScore = shotScore + game.bucketPoints + ballBonus;

        popup.lines.push_back("SHOT SCORE " + FormatNumber(shotScore));
        popup.lines.push_back("BUCKET +" + FormatNumber(game.bucketPoints));
        popup.lines.push_back("BALLS LEFT " + std::to_string(game.ballsLeft) + " X " + FormatNumber(UNUSED_BALL_POINTS) + " +" + FormatNumber(ballBonus));
        popup.lines.push_back("FEVER SCORE +" + FormatNumber(feverScore));

        game.score += feverScore;
        game.levelComplete = true;
    }
    else
    {
        popup.lines.push_back("SHOT SCORE +" + FormatNumber(shotScore));
        if (game.freeBall)
            popup.lines.push_back("FREE BALL!");
        game.score += shotScore;

        // The purple peg moves for the next shot
        MovePurplePeg(pegs);
    }

    popup.timer = POPUP_TIME;
    game.shotInProgress = false;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Peggle Clone", WINDOW_W, WINDOW_H, 0, &window, &renderer))
    {
        SDL_Log("Window/renderer creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    Ball  ball;
    std::vector<Peg> pegs;
    std::vector<int> hitOrder; // pegs hit by the current shot, waiting to be deleted in order
    float removeTimer = 0.0f;
    GameState game;
    Popup popup;

    bool running = true;
    bool fastForward = false; // right mouse button held
    Screen screen = Screen::MainMenu;
    int  currentLevel = 1;
    bool levelSelectOpen = false;
    std::string nameInput; // the 3 letters typed after clearing a level
    bool scoreSaved = false;
    std::vector<ScoreEntry> scores = LoadScores();

    // Starts (or restarts) a level from scratch
    auto startLevel = [&](int level)
    {
        currentLevel = level;
        pegs = BuildLevel(level);
        hitOrder.clear();
        game = GameState();
        for (const Peg& peg : pegs)
            if (peg.type == PegType::Orange) game.totalOrange++;
        popup = Popup();
        ball = Ball();
        removeTimer = 0.0f;
        nameInput.clear();
        scoreSaved = false;
        levelSelectOpen = false;
        screen = Screen::Playing;
    };

    auto goToMenu = [&]()
    {
        screen = Screen::MainMenu;
        levelSelectOpen = false;
        scores = LoadScores(); // read the file again so the leaderboard is up to date
    };

    auto saveScore = [&]()
    {
        if (nameInput.size() != 3 || scoreSaved) return;
        ScoreEntry entry;
        entry.level = currentLevel;
        entry.name = nameInput;
        entry.score = game.score;
        if (SaveScore(entry)) scoreSaved = true;
    };

    auto doAction = [&](const Button& b)
    {
        switch (b.action)
        {
        case Action::Play: startLevel(1); break;
        case Action::Resume: screen = Screen::Playing; break;
        case Action::Quit: running = false; break;
        case Action::OpenLevelSelect: levelSelectOpen = true; break;
        case Action::CloseLevelSelect: levelSelectOpen = false; break;
        case Action::GoToLevel: startLevel(b.level); break;
        case Action::Restart: startLevel(currentLevel); break;
        case Action::MainMenu: goToMenu(); break;
        case Action::NextLevel: startLevel(currentLevel + 1); break;
        case Action::SaveScore: saveScore(); break;
        default: break;
        }
    };
    float mouseX = LAUNCHER_X;
    float mouseY = WINDOW_H;

    float    accumulator = 0.0f;
    Uint64   lastTicks = SDL_GetTicksNS();

    while (running)
    {
        // ---- Events ----
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_ESCAPE)
                {
                    // Esc closes the level selector, or pauses and unpauses a level
                    if (levelSelectOpen) levelSelectOpen = false;
                    else if (screen == Screen::Playing) screen = Screen::Paused;
                    else if (screen == Screen::Paused) screen = Screen::Playing;
                }
                else if (screen == Screen::LevelClear && !scoreSaved && !levelSelectOpen)
                {
                    // Typing the 3 letter name
                    if (e.key.key == SDLK_BACKSPACE && !nameInput.empty())
                        nameInput.pop_back();
                    else if (e.key.key >= SDLK_A && e.key.key <= SDLK_Z && nameInput.size() < 3 && !e.key.repeat)
                        nameInput += static_cast<char>('A' + (e.key.key - SDLK_A));
                    else if (e.key.key == SDLK_RETURN)
                        saveScore();
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                mouseX = e.motion.x;
                mouseY = e.motion.y;
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_RIGHT) fastForward = false;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                if (e.button.button == SDL_BUTTON_RIGHT)
                {
                    fastForward = true;
                    break;
                }
                if (e.button.button != SDL_BUTTON_LEFT) break;
                mouseX = e.button.x;
                mouseY = e.button.y;

                if (screen == Screen::Playing)
                {
                    if (!ball.active && hitOrder.empty() && game.ballsLeft > 0 && !game.levelComplete)
                    {
                        float dirX, dirY;
                        GetAimDirection(mouseX, mouseY, dirX, dirY);
                        FireBall(ball, dirX, dirY);
                        game.shotInProgress = true;
                        game.ballsLeft--;
                        game.shot = ShotStats();
                        game.shot.orangeAtStart = game.orangeHit;
                        game.freeBall = false;
                    }
                }
                else
                {
                    // Menus, the level selector pop-up blocks the buttons underneath it
                    const std::vector<Button> buttons = levelSelectOpen
                        ? GetLevelSelectButtons(LevelCount())
                        : GetScreenButtons(screen, currentLevel, LevelCount(), nameInput.size() == 3, scoreSaved);
                    for (const Button& b : buttons)
                    {
                        if (PointInRect(mouseX, mouseY, b.rect))
                        {
                            doAction(b);
                            break;
                        }
                    }
                }
                break;
            }
            }
        }

        // ---- Timing ----
        const Uint64 now = SDL_GetTicksNS();
        float frameDt = static_cast<float>(now - lastTicks) / 1.0e9f;
        lastTicks = now;
        frameDt = std::min(frameDt, MAX_FRAME_DT);
        if (screen == Screen::Paused) frameDt = 0.0f; // nothing moves while paused
        else if (fastForward && screen == Screen::Playing) frameDt *= GAME_SPEED_FAST;

        // ---- Main menu ----
        if (screen == Screen::MainMenu)
        {
            SDL_SetRenderDrawColor(renderer, 20, 24, 40, 255);
            SDL_RenderClear(renderer);

            const std::string title = "PEGGLE CLONE";
            SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
            DrawText(renderer, title, LAUNCHER_X - TextWidth(title, 4.0f) / 2.0f, 40.0f, 4.0f);

            // Hover highlight is switched off while the level selector is open on top
            const float hoverX = levelSelectOpen ? -1.0f : mouseX;
            const float hoverY = levelSelectOpen ? -1.0f : mouseY;
            DrawButtons(renderer, GetScreenButtons(screen, currentLevel, LevelCount(), false, false), hoverX, hoverY);

            // Leaderboard, read from the file
            SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
            DrawText(renderer, "LEADERBOARD", 400.0f, 120.0f, 2.0f);
            if (scores.empty())
            {
                SDL_SetRenderDrawColor(renderer, 160, 160, 170, 255);
                DrawText(renderer, "NO SCORES YET", 400.0f, 160.0f, 2.0f);
            }
            for (int i = 0; i < static_cast<int>(scores.size()) && i < LEADERBOARD_ROWS; ++i)
            {
                const float y = 160.0f + i * 26.0f;
                const std::string scoreText = FormatNumber(scores[i].score);
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                DrawText(renderer, std::to_string(i + 1) + ".", 400.0f, y, 2.0f);
                DrawText(renderer, scores[i].name, 440.0f, y, 2.0f);
                SDL_SetRenderDrawColor(renderer, 160, 160, 170, 255);
                DrawText(renderer, "L" + std::to_string(scores[i].level), 520.0f, y, 2.0f);
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                DrawText(renderer, scoreText, 760.0f - TextWidth(scoreText, 2.0f), y, 2.0f);
            }

            if (levelSelectOpen)
                DrawLevelSelect(renderer, LevelCount(), mouseX, mouseY);

            SDL_RenderPresent(renderer);
            continue;
        }

        // ---- Update (fixed timestep) ----
        // Time slows down while the shot is in extreme fever
        const float timeScale = (game.feverActive && ball.active) ? FEVER_SLOWMO : 1.0f;
        accumulator += frameDt * timeScale;
        while (accumulator >= PHYSICS_STEP)
        {
            UpdateBucket(game, PHYSICS_STEP);
            UpdateBall(ball, pegs, hitOrder, game, PHYSICS_STEP);
            accumulator -= PHYSICS_STEP;
        }

        // Once the shot is gone, delete the hit pegs one by one in the order they were hit
        if (!ball.active && !hitOrder.empty())
        {
            removeTimer += frameDt;
            while (removeTimer >= PEG_REMOVE_DELAY && !hitOrder.empty())
            {
                pegs[hitOrder.front()].removed = true;
                hitOrder.erase(hitOrder.begin());
                removeTimer -= PEG_REMOVE_DELAY;
            }
        }
        else
        {
            removeTimer = 0.0f;
        }

        // Shot is finished and the hit pegs are gone, so the shot is scored
        if (game.shotInProgress && !ball.active && hitOrder.empty())
            FinishShot(game, pegs, popup);

        if (popup.timer > 0.0f)
            popup.timer -= frameDt;

        // Once the shot popup is gone, check if the level is cleared or out of shots
        if (screen == Screen::Playing && !game.shotInProgress && !ball.active && hitOrder.empty() && popup.timer <= 0.0f)
        {
            if (game.levelComplete) screen = Screen::LevelClear;
            else if (game.ballsLeft <= 0) screen = Screen::Fail;
        }

        // ---- Render ----
        SDL_SetRenderDrawColor(renderer, 20, 24, 40, 255);
        SDL_RenderClear(renderer);

        // Pegs, hit pegs are drawn lighter
        for (const Peg& peg : pegs)
        {
            if (peg.removed) continue;

            Uint8 r = 50, g = 110, b = 220; // blue
            if (peg.type == PegType::Orange) { r = 240; g = 130; b = 40; }
            else if (peg.type == PegType::Purple) { r = 190; g = 60; b = 200; }

            if (peg.hit)
            {
                r = static_cast<Uint8>((r + 255) / 2);
                g = static_cast<Uint8>((g + 255) / 2);
                b = static_cast<Uint8>((b + 255) / 2);
            }
            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
            DrawFilledCircle(renderer, peg.x, peg.y, PEG_RADIUS);
        }

        // Extreme fever, the big circles and the points for the gap between each one
        if (game.feverActive)
        {
            SDL_SetRenderDrawColor(renderer, 230, 190, 60, 255);
            for (int k = 0; k <= FEVER_BUCKET_COUNT; ++k)
                DrawFilledCircle(renderer, FeverCircleX(k), FEVER_CIRCLE_Y, FEVER_CIRCLE_RADIUS);

            const bool ultra = AllPegsCleared(pegs);
            for (int k = 0; k < FEVER_BUCKET_COUNT; ++k)
            {
                const std::string label = FormatNumber(ultra ? ULTRA_BUCKET_POINTS : FEVER_BUCKET_POINTS[k]);
                const float midX = (FeverCircleX(k) + FeverCircleX(k + 1)) / 2.0f;
                DrawText(renderer, label, midX - TextWidth(label, 1.0f) / 2.0f, PLAY_BOTTOM - 40.0f, 1.0f);
            }
        }

        // Bucket, hidden during extreme fever
        if (game.feverActive == false)
        {
            SDL_FRect bucketRect;
            Triangle leftTri, rightTri;
            GetBucketShape(game.bucketX, bucketRect, leftTri, rightTri);

            SDL_SetRenderDrawColor(renderer, 190, 200, 225, 255);
            SDL_RenderFillRect(renderer, &bucketRect);

            const SDL_FColor triColour = { 0.45f, 0.5f, 0.65f, 1.0f };
            for (const Triangle* tri : { &leftTri, &rightTri })
            {
                SDL_Vertex v[3] = {};
                for (int i = 0; i < 3; ++i)
                {
                    v[i].position = tri->p[i];
                    v[i].color = triColour;
                }
                SDL_RenderGeometry(renderer, nullptr, v, 3, nullptr, 0);
            }
        }

        // Launcher
        SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
        DrawFilledCircle(renderer, LAUNCHER_X, LAUNCHER_Y, 14.0f);

        // Aim guide only shows when no ball is in play and the hit pegs are deleted
        if (!ball.active && hitOrder.empty() && game.ballsLeft > 0 && !game.levelComplete)
        {
            float dirX, dirY;
            GetAimDirection(mouseX, mouseY, dirX, dirY);
            DrawAimGuide(renderer, dirX, dirY);
        }

        // Ball
        if (ball.active)
        {
            SDL_SetRenderDrawColor(renderer, 230, 230, 240, 255);
            DrawFilledCircle(renderer, ball.x, ball.y, BALL_RADIUS);
        }

        // Black borders
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        const SDL_FRect borders[4] = {
            { 0.0f,        0.0f,        static_cast<float>(WINDOW_W), BORDER_TOP }, // top
            { 0.0f,        PLAY_BOTTOM, static_cast<float>(WINDOW_W), BORDER_BOTTOM }, // bottom
            { 0.0f,        PLAY_TOP,    BORDER_LEFT,                  PLAY_BOTTOM - PLAY_TOP }, // left
            { PLAY_RIGHT,  PLAY_TOP,    BORDER_RIGHT,                 PLAY_BOTTOM - PLAY_TOP }, // right
        };
        SDL_RenderFillRects(renderer, borders, 4);

        // Total score, top right
        const std::string scoreText = FormatNumber(game.score);
        SDL_SetRenderDrawColor(renderer, 160, 160, 170, 255);
        DrawText(renderer, "SCORE", WINDOW_W - 10.0f - TextWidth("SCORE", 1.0f), 8.0f, 1.0f);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        DrawText(renderer, scoreText, WINDOW_W - 10.0f - TextWidth(scoreText, 2.0f), 22.0f, 2.0f);

        // Balls left, left border
        const std::string ballsText = std::to_string(game.ballsLeft);
        SDL_SetRenderDrawColor(renderer, 160, 160, 170, 255);
        DrawText(renderer, "BALLS", (BORDER_LEFT - TextWidth("BALLS", 1.0f)) / 2.0f, PLAY_TOP + 10.0f, 1.0f);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        DrawText(renderer, ballsText, (BORDER_LEFT - TextWidth(ballsText, 2.0f)) / 2.0f, PLAY_TOP + 24.0f, 2.0f);

        // Fever meter, right border. One segment per orange peg hit, filling from the bottom.
        const int   segPitch = METER_SEGMENT_HEIGHT + METER_SEGMENT_GAP;
        const float meterBottom = PLAY_BOTTOM - 10.0f;
        const float meterX = PLAY_RIGHT + 4.0f;
        for (int k = 1; k <= ORANGE_PEG_COUNT; ++k)
        {
            const float segY = meterBottom - k * segPitch;
            if (k <= game.orangeHit) SDL_SetRenderDrawColor(renderer, 255, 150, 30, 255);
            else SDL_SetRenderDrawColor(renderer, 85, 42, 14, 255);
            const SDL_FRect seg = { meterX, segY, 16.0f, static_cast<float>(METER_SEGMENT_HEIGHT) };
            SDL_RenderFillRect(renderer, &seg);
        }

        // Multiplier labels, next to the segment where each multiplier starts
        const struct { int hits; const char* label; } multipliers[4] = { { 10, "x2" }, { 15, "x3" }, { 19, "x5" }, { 22, "x10" } };
        for (const auto& m : multipliers)
        {
            const float segY = meterBottom - m.hits * segPitch;
            if (game.orangeHit >= m.hits) SDL_SetRenderDrawColor(renderer, 255, 220, 120, 255);
            else SDL_SetRenderDrawColor(renderer, 150, 150, 160, 255);
            DrawText(renderer, m.label, meterX + 19.0f, segY + 4.0f, 1.0f);
        }

        // Popup with the points for the shot
        if (popup.timer > 0.0f && !popup.lines.empty())
        {
            const float scale = 2.0f;
            const float lineH = 22.0f;
            float maxW = 0.0f;
            for (const std::string& line : popup.lines)
                maxW = std::max(maxW, TextWidth(line, scale));

            const float boxW = maxW + 30.0f;
            const float boxH = popup.lines.size() * lineH + 20.0f;
            const SDL_FRect box = { LAUNCHER_X - boxW / 2.0f, (PLAY_TOP + PLAY_BOTTOM) / 2.0f - boxH / 2.0f, boxW, boxH };

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
            for (size_t i = 0; i < popup.lines.size(); ++i)
                DrawText(renderer, popup.lines[i], LAUNCHER_X - TextWidth(popup.lines[i], scale) / 2.0f, box.y + 10.0f + i * lineH, scale);
        }

        // Pause, fail and level clear pop-ups, drawn over the level
        if (screen == Screen::Paused || screen == Screen::Fail || screen == Screen::LevelClear)
        {
            const bool hoverOn = !levelSelectOpen;
            DrawDim(renderer, 170);
            DrawPanel(renderer, { LAUNCHER_X - 210.0f, 100.0f, 420.0f, 380.0f });

            const std::string scoreLine = "SCORE " + FormatNumber(game.score);
            if (screen == Screen::Paused)
            {
                const std::string title = "PAUSED";
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                DrawText(renderer, title, LAUNCHER_X - TextWidth(title, 5.0f) / 2.0f, 125.0f, 5.0f);
                DrawText(renderer, scoreLine, LAUNCHER_X - TextWidth(scoreLine, 2.0f) / 2.0f, 190.0f, 2.0f);
            }
            else if (screen == Screen::Fail)
            {
                const std::string title = "FAIL";
                SDL_SetRenderDrawColor(renderer, 230, 70, 60, 255);
                DrawText(renderer, title, LAUNCHER_X - TextWidth(title, 5.0f) / 2.0f, 125.0f, 5.0f);
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                DrawText(renderer, scoreLine, LAUNCHER_X - TextWidth(scoreLine, 2.0f) / 2.0f, 190.0f, 2.0f);
                const std::string note = "OUT OF SHOTS";
                SDL_SetRenderDrawColor(renderer, 160, 160, 170, 255);
                DrawText(renderer, note, LAUNCHER_X - TextWidth(note, 2.0f) / 2.0f, 215.0f, 2.0f);
            }
            else
            {
                const std::string title = "LEVEL CLEARED";
                SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
                DrawText(renderer, title, LAUNCHER_X - TextWidth(title, 3.0f) / 2.0f, 118.0f, 3.0f);
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                DrawText(renderer, scoreLine, LAUNCHER_X - TextWidth(scoreLine, 2.0f) / 2.0f, 165.0f, 2.0f);

                // Name entry
                std::string nameLine;
                if (scoreSaved) nameLine = "SAVED AS " + nameInput;
                else nameLine = "NAME " + nameInput + (nameInput.size() < 3 ? "_" : "");
                SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
                DrawText(renderer, nameLine, LAUNCHER_X - TextWidth(nameLine, 2.0f) / 2.0f, 198.0f, 2.0f);
                if (!scoreSaved)
                {
                    const std::string hint = "TYPE 3 LETTERS THEN SAVE";
                    SDL_SetRenderDrawColor(renderer, 160, 160, 170, 255);
                    DrawText(renderer, hint, LAUNCHER_X - TextWidth(hint, 1.0f) / 2.0f, 224.0f, 1.0f);
                }
            }

            DrawButtons(renderer,
                        GetScreenButtons(screen, currentLevel, LevelCount(), nameInput.size() == 3, scoreSaved),
                        hoverOn ? mouseX : -1.0f, hoverOn ? mouseY : -1.0f);

            if (levelSelectOpen)
                DrawLevelSelect(renderer, LevelCount(), mouseX, mouseY);
        }

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
