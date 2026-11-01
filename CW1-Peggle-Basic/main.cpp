#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <random>
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

// A short lived piece of text drawn in the play area next to a peg
struct FloatingText
{
    std::string text;  // peg points, or the style shot name
    std::string text2; // second line (the style points), empty for peg points
    float x = 0.0f, y = 0.0f; // the peg that was hit
    float timer = 0.0f;
    SDL_Color colour = { 255, 255, 255, 255 };
    bool isStyle = false;
    int stack = 0; // style shots from the same hit are stacked above each other
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
    int freeBallsEarned = 0; // free balls given so far this shot for reaching the score thresholds
    float freeBallFlash = 0.0f; // counts down after a threshold is reached so the full bar can be seen
    bool bucketEdgeHit = false; // the ball has bounced off the edge of the bucket this shot
    bool pegAfterEdge = false; // and has touched a peg since the last time it hit the edge
    bool edgeSincePeg = false; // the last thing the ball bounced off was the bucket edge
};

// The 50/50 pop-up shown after a shot that hit nothing and missed the bucket
struct Roulette
{
    bool active = false;
    bool returned = false; // the result, decided when the shot ends
    bool revealed = false; // the shot has been given back (if it was going to be)
    float timer = 0.0f;
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
    int bucketStreak = 0; // bucket shots in a row, for Mad Skillz
    Roulette roulette;
    float bucketTime = BUCKET_SWEEP_TIME * 0.5f; // time through the bucket's back and forth movement
    float bucketX = LAUNCHER_X; // centre of the bucket
    ShotStats shot;
    std::vector<FloatingText> floaters; // small pop-ups next to pegs
};

// The box in the middle of the screen at the end of each shot
struct PopupLine
{
    std::string text;
    float scale;
    SDL_Color colour;
};

struct Popup
{
    std::vector<PopupLine> lines;
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

static const SDL_Color COL_WHITE  = { 255, 255, 255, 255 };
static const SDL_Color COL_YELLOW = { 255, 225, 40, 255 };
static const SDL_Color COL_ORANGE = { 255, 150, 30, 255 };
static const SDL_Color COL_GREEN  = { 90, 240, 90, 255 };
static const SDL_Color COL_CYAN   = { 120, 230, 235, 255 };
static const SDL_Color COL_RED    = { 240, 70, 60, 255 };

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

// Text with a black outline so it can be read over the pegs. alpha fades it out.
static void DrawOutlinedText(SDL_Renderer* r, const std::string& text, float x, float y, float scale,
                             SDL_Color colour, Uint8 alpha = 255)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    const float o = std::max(1.0f, std::floor(scale / 2.0f)); // outline thickness
    SDL_SetRenderDrawColor(r, 0, 0, 0, alpha);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx != 0 || dy != 0)
                DrawText(r, text, x + dx * o, y + dy * o, scale);

    SDL_SetRenderDrawColor(r, colour.r, colour.g, colour.b, alpha);
    DrawText(r, text, x, y, scale);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
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

// The score of the shot so far, worked out the same way as the pop-up at the end of the shot
static long long LiveShotScore(const ShotStats& shot)
{
    return static_cast<long long>(shot.pegScoreSum) * shot.pegsHit + shot.stylePoints;
}

// Gives a free ball each time the shot's score passes the next threshold
static void CheckFreeBalls(GameState& game)
{
    ShotStats& shot = game.shot;
    while (shot.freeBallsEarned < FREE_BALL_THRESHOLD_COUNT &&
           LiveShotScore(shot) >= FREE_BALL_THRESHOLDS[shot.freeBallsEarned])
    {
        shot.freeBallsEarned++;
        game.ballsLeft++;
        shot.freeBallFlash = FREE_BALL_FLASH_TIME;
    }
}

// The small number next to a peg when it is hit
static void AddPegPopup(GameState& game, const Peg& peg, int points)
{
    FloatingText f;
    f.text = FormatNumber(points);
    f.x = peg.x;
    f.y = peg.y;
    f.timer = PEG_POPUP_TIME;
    if (peg.type == PegType::Orange) f.colour = { 255, 170, 60, 255 };
    else if (peg.type == PegType::Purple) f.colour = { 235, 130, 255, 255 };
    else f.colour = { 130, 190, 255, 255 };
    game.floaters.push_back(f);
}

// Adds a style shot to the shot, shows it next to the peg that was hit and checks for free balls.
// To add a new style shot, call this with its name, points and the peg that was hit.
static void AddStyle(GameState& game, const char* name, int points, float x, float y)
{
    ShotStats& shot = game.shot;
    shot.styles.push_back({ name, points });
    shot.stylePoints += points;

    FloatingText f;
    f.text = name;
    f.text2 = "+" + FormatNumber(points);
    f.x = x;
    f.y = y;
    f.timer = STYLE_POPUP_TIME;
    f.colour = COL_GREEN;
    f.isStyle = true;
    for (const FloatingText& other : game.floaters)
        if (other.isStyle && other.x == x && other.y == y) f.stack++;
    game.floaters.push_back(f);

    CheckFreeBalls(game);
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

// Pushes the ball out of a triangle and bounces it off the nearest edge.
// Returns true if the ball hit the top or sloped edge of the bucket. Scraping the inside wall of the bucket doesn't count.
static bool BounceOffTriangle(Ball& ball, const Triangle& tri)
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
    if (!inside && dist >= BALL_RADIUS) return false;

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

    return ny < -0.3f; // the surface faces upwards, so it is the rim and not the inside wall
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
                    AddStyle(game, "SUPER LONG SHOT", SUPER_LONG_SHOT_POINTS, peg.x, peg.y);
                    longShot = true;
                }
                else if (shot.distSinceContact >= LONG_SHOT_DISTANCE)
                {
                    AddStyle(game, "LONG SHOT", LONG_SHOT_POINTS, peg.x, peg.y);
                    longShot = true;
                }
            }

            // Two long shots in a row
            if (longShot)
            {
                shot.consecutiveLong++;
                if (shot.consecutiveLong >= 2)
                {
                    AddStyle(game, "DOUBLE LONG SHOT", DOUBLE_LONG_SHOT_POINTS, peg.x, peg.y);
                    shot.consecutiveLong = 0;
                }
            }
            else
            {
                shot.consecutiveLong = 0;
            }

            // Bounced off a side wall, travelled far enough and then hit a non-blue peg
            if (shot.bouncedOffWall && nonBlue && shot.distSinceWall >= OFF_THE_WALL_DISTANCE)
                AddStyle(game, "OFF THE WALL", OFF_THE_WALL_POINTS, peg.x, peg.y);
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

            const int pegPoints = BasePegScore(peg.type) * FeverMultiplier(game.orangeHit);
            shot.pegScoreSum += pegPoints;
            shot.pegsHit++;
            AddPegPopup(game, peg, pegPoints);
            CheckFreeBalls(game);

            // Orange Attack, this orange peg filled a multiplier stage and the whole stage was hit in this shot
            if (peg.type == PegType::Orange)
            {
                for (int s = 0; s < ORANGE_STAGE_COUNT; ++s)
                {
                    const int stageStart = (s == 0) ? 0 : ORANGE_STAGE_ENDS[s - 1];
                    if (game.orangeHit == ORANGE_STAGE_ENDS[s] && shot.orangeAtStart <= stageStart)
                        AddStyle(game, "ORANGE ATTACK!", ORANGE_ATTACK_POINTS, peg.x, peg.y);
                }
            }

            // The last orange peg starts extreme fever
            if (peg.type == PegType::Orange && game.orangeHit == game.totalOrange)
            {
                game.feverActive = true;

                // Kick the bucket, the ball came straight off the bucket's edge into the last orange peg
                if (shot.edgeSincePeg)
                    AddStyle(game, "KICK THE BUCKET", KICK_THE_BUCKET_POINTS, peg.x, peg.y);

                // Hat trick, the last 3 orange pegs were all hit in this shot
                if (game.totalOrange >= 3 && shot.orangeAtStart <= game.totalOrange - 3)
                    AddStyle(game, "ORANGE HAT TRICK", ORANGE_HAT_TRICK_POINTS, peg.x, peg.y);
            }
        }

        // Any peg touched after the bucket edge counts as a bounce for Lucky Bounce
        if (shot.bucketEdgeHit) shot.pegAfterEdge = true;
        shot.edgeSincePeg = false;

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
        const bool hitLeft = BounceOffTriangle(ball, leftTri);
        const bool hitRight = BounceOffTriangle(ball, rightTri);
        if (hitLeft || hitRight)
        {
            shot.bucketEdgeHit = true;
            shot.pegAfterEdge = false; // a peg has to be hit after the most recent edge bounce
            shot.edgeSincePeg = true;
        }

        // Dropping into the rectangle deletes the shot and gives the shot back
        if (ball.x >= bucketRect.x && ball.x <= bucketRect.x + bucketRect.w && ball.y + BALL_RADIUS >= bucketRect.y)
        {
            ball.active = false;

            // Free Ball Skills, straight into the bucket after hitting exactly one peg
            if (shot.pegsHit == 1 && !shot.bucketEdgeHit)
                AddStyle(game, "FREE BALL SKILLS!", FREE_BALL_SKILLS_POINTS, ball.x, ball.y);

            // Lucky Bounce, off the edge, off a peg and back into the bucket
            if (shot.bucketEdgeHit && shot.pegAfterEdge)
                AddStyle(game, "LUCKY BOUNCE", LUCKY_BOUNCE_POINTS, ball.x, ball.y);

            // Mad Skillz, bucket shots in a row. A shot that goes straight in without touching a peg doesn't count and breaks the streak.
            if (shot.pegsHit == 0)
            {
                game.bucketStreak = 0;
            }
            else
            {
                game.bucketStreak++;
                if (game.bucketStreak == CRAZY_MAD_SKILLZ_STREAK)
                {
                    AddStyle(game, "CRAZY MAD SKILLZ", CRAZY_MAD_SKILLZ_POINTS, ball.x, ball.y);
                    game.bucketStreak = 0;
                }
                else if (game.bucketStreak == MAD_SKILLZ_STREAK)
                {
                    AddStyle(game, "MAD SKILLZ", MAD_SKILLZ_POINTS, ball.x, ball.y);
                }
            }

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

// Random numbers for the 50/50
static std::mt19937& GameRng()
{
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}

// Works out the score for the shot that just finished and fills in the popup
static void FinishShot(GameState& game, std::vector<Peg>& pegs, Popup& popup)
{
    const ShotStats& shot = game.shot;
    const long long pegTotal = static_cast<long long>(shot.pegScoreSum) * shot.pegsHit;
    const long long shotScore = pegTotal + shot.stylePoints;

    popup.lines.clear();
    if (game.feverActive)
        popup.lines.push_back({ game.ultraFever ? "ULTRA EXTREME FEVER!" : "EXTREME FEVER!", 3.0f, COL_ORANGE });

    // Same layout as Peggle: peg points x pegs hit, the total, then the style points
    popup.lines.push_back({ FormatNumber(shot.pegScoreSum) + " X " + std::to_string(shot.pegsHit) + " PEGS", 3.0f, COL_YELLOW });
    popup.lines.push_back({ FormatNumber(pegTotal), 5.0f, COL_WHITE });
    if (shot.stylePoints > 0)
    {
        popup.lines.push_back({ "+" + FormatNumber(shot.stylePoints) + " STYLE POINTS", 3.0f, COL_GREEN });
        popup.lines.push_back({ "SHOT SCORE " + FormatNumber(shotScore), 2.0f, COL_WHITE });
    }

    // Free balls from the bucket and from the score thresholds
    const int freeBalls = shot.freeBallsEarned + (game.freeBall ? 1 : 0);
    if (freeBalls == 1)
        popup.lines.push_back({ "FREE BALL!", 3.0f, COL_CYAN });
    else if (freeBalls > 1)
        popup.lines.push_back({ std::to_string(freeBalls) + " FREE BALLS!", 3.0f, COL_CYAN });

    if (game.feverActive)
    {
        const long long ballBonus = static_cast<long long>(game.ballsLeft) * UNUSED_BALL_POINTS;
        const long long feverScore = shotScore + game.bucketPoints + ballBonus;

        popup.lines.push_back({ "BUCKET +" + FormatNumber(game.bucketPoints), 2.0f, COL_YELLOW });
        popup.lines.push_back({ "BALLS LEFT " + std::to_string(game.ballsLeft) + " X " + FormatNumber(UNUSED_BALL_POINTS) + " +" + FormatNumber(ballBonus), 2.0f, COL_YELLOW });
        popup.lines.push_back({ "FEVER SCORE +" + FormatNumber(feverScore), 3.0f, COL_WHITE });

        game.score += feverScore;
        game.levelComplete = true;
    }
    else
    {
        game.score += shotScore;

        // The purple peg moves for the next shot
        MovePurplePeg(pegs);
    }

    // Any shot that didn't land in the bucket ends the Mad Skillz streak
    if (!game.freeBall) game.bucketStreak = 0;

    // A shot that hit nothing and missed the bucket gets a 50/50 to be given back, instead of the normal pop-up
    const bool fullMiss = (shot.pegsHit == 0 && !game.freeBall && !game.feverActive);
    if (fullMiss)
    {
        game.roulette = Roulette();
        game.roulette.active = true;
        game.roulette.returned = std::uniform_int_distribution<int>(0, 1)(GameRng()) == 1;
        popup.lines.clear();
        popup.timer = 0.0f;
    }
    else
    {
        popup.timer = POPUP_TIME;
    }
    game.shotInProgress = false;
}

// The text in the middle of the screen at the end of a shot
static void DrawShotPopup(SDL_Renderer* r, const Popup& popup)
{
    const float gap = 10.0f;
    float totalH = -gap;
    for (const PopupLine& line : popup.lines)
        totalH += 8.0f * line.scale + gap;

    const float fade = std::max(0.0f, std::min(1.0f, popup.timer / POPUP_FADE_TIME));
    const Uint8 alpha = static_cast<Uint8>(255.0f * fade);

    float y = (PLAY_TOP + PLAY_BOTTOM) / 2.0f - totalH / 2.0f;
    for (const PopupLine& line : popup.lines)
    {
        DrawOutlinedText(r, line.text, LAUNCHER_X - TextWidth(line.text, line.scale) / 2.0f, y, line.scale, line.colour, alpha);
        y += 8.0f * line.scale + gap;
    }
}

// The small points next to pegs and the style shot names
static void DrawFloatingTexts(SDL_Renderer* r, const std::vector<FloatingText>& floaters)
{
    for (const FloatingText& f : floaters)
    {
        if (!f.isStyle)
        {
            // Points sit to the right of the peg, or the left if there is no room
            const float scale = PEG_POPUP_SCALE;
            const float w = TextWidth(f.text, scale);
            float x = f.x + PEG_RADIUS + 3.0f;
            if (x + w > PLAY_RIGHT - 2.0f) x = f.x - PEG_RADIUS - 3.0f - w;
            DrawOutlinedText(r, f.text, x, f.y - 4.0f * scale, scale, f.colour);
        }
        else
        {
            // Name with the points under it, above the peg and kept inside the play area
            const float scale = STYLE_POPUP_SCALE;
            const float lineH = 8.0f * scale + 4.0f;
            const float blockH = 2.0f * lineH;
            const float w = std::max(TextWidth(f.text, scale), TextWidth(f.text2, scale));

            const float fade = std::max(0.0f, std::min(1.0f, f.timer / 0.3f));
            const Uint8 alpha = static_cast<Uint8>(255.0f * fade);

            const float cx = std::max(PLAY_LEFT + 2.0f + w / 2.0f, std::min(f.x, PLAY_RIGHT - 2.0f - w / 2.0f));
            const float y = std::max(PLAY_TOP + 2.0f, f.y - PEG_RADIUS - 10.0f - blockH - f.stack * (blockH + 6.0f));

            DrawOutlinedText(r, f.text, cx - TextWidth(f.text, scale) / 2.0f, y, scale, f.colour, alpha);
            DrawOutlinedText(r, f.text2, cx - TextWidth(f.text2, scale) / 2.0f, y + lineH, scale, f.colour, alpha);
        }
    }
}

// Left border: the balls left stacked up between two bars that fill as the shot score climbs.
// The bars are green for the first free ball, purple for the second and yellow for the third.
static void DrawBallMeter(SDL_Renderer* r, const GameState& game, bool live)
{
    const ShotStats& shot = game.shot;
    const SDL_Color stageColours[FREE_BALL_THRESHOLD_COUNT] = {
        { 60, 200, 80, 255 }, { 175, 70, 215, 255 }, { 250, 210, 50, 255 }
    };

    // Which bar is showing, how full it is and whether the free ball circle is lit
    int stage = 0;
    float fill = 0.0f;
    bool lit = false;
    if (live)
    {
        stage = shot.freeBallsEarned;
        if (shot.freeBallFlash > 0.0f && stage > 0)
        {
            // A threshold was just reached, hold the full bar for a moment
            stage--;
            fill = 1.0f;
            lit = true;
        }
        else if (stage >= FREE_BALL_THRESHOLD_COUNT)
        {
            // All three earned, it stays full and yellow
            fill = 1.0f;
            lit = true;
        }
        else
        {
            fill = static_cast<float>(LiveShotScore(shot)) / static_cast<float>(FREE_BALL_THRESHOLDS[stage]);
            fill = std::max(0.0f, std::min(fill, 1.0f));
        }
    }
    const SDL_Color col = stageColours[std::min(stage, FREE_BALL_THRESHOLD_COUNT - 1)];

    const float cx = BORDER_LEFT / 2.0f;
    const float circleR = FREE_BALL_CIRCLE_RADIUS;
    const float cy = PLAY_BOTTOM - circleR - 12.0f;
    const float barTop = PLAY_TOP + 10.0f;
    const float barH = cy - barTop; // the bars run down into the circle
    const float barX[2] = { 3.0f, BORDER_LEFT - 3.0f - FREE_BALL_BAR_WIDTH };

    // Two bars, filling from the bottom
    for (float x : barX)
    {
        const SDL_FRect back = { x, barTop, FREE_BALL_BAR_WIDTH, barH };
        SDL_SetRenderDrawColor(r, 45, 48, 62, 255);
        SDL_RenderFillRect(r, &back);

        const float h = barH * fill;
        const SDL_FRect front = { x, cy - h, FREE_BALL_BAR_WIDTH, h };
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
        SDL_RenderFillRect(r, &front);

        SDL_SetRenderDrawColor(r, 120, 120, 135, 255);
        SDL_RenderRect(r, &back);
    }

    // The free ball circle joining the bars, filled with colour once the threshold is hit
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    DrawFilledCircle(r, cx, cy, circleR);
    if (lit) SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    else SDL_SetRenderDrawColor(r, 30, 33, 46, 255);
    DrawFilledCircle(r, cx, cy, circleR - 3.0f);

    if (lit) SDL_SetRenderDrawColor(r, 15, 15, 20, 255);
    else SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    DrawText(r, "FREE", cx - TextWidth("FREE", 1.0f) / 2.0f, cy - 9.0f, 1.0f);
    DrawText(r, "BALL", cx - TextWidth("BALL", 1.0f) / 2.0f, cy + 1.0f, 1.0f);

    // Balls left, stacked up from just above the circle
    const int n = std::max(game.ballsLeft, 0);
    const float firstY = cy - circleR - 4.0f - STACK_BALL_RADIUS;
    const float topY = barTop + 6.0f + STACK_BALL_RADIUS;
    float pitch = STACK_BALL_PITCH;
    if (n > 1) pitch = std::min(pitch, (firstY - topY) / static_cast<float>(n - 1));
    SDL_SetRenderDrawColor(r, 230, 230, 240, 255);
    for (int i = 0; i < n; ++i)
        DrawFilledCircle(r, cx, firstY - i * pitch, STACK_BALL_RADIUS);
}

// The chance (0 to 100) shown for RETURN. It starts at 50 and swings further each time, ending on 100 (returned) or 0 (lost).
static int RouletteReturnChance(float t, bool returned)
{
    const float p = std::min(1.0f, t / ROULETTE_SPIN_TIME);
    const float omega = (returned ? 2.5f : 3.5f) * SDL_PI_F;
    const float swing = p * std::sin(omega * p); // grows from 0 to +1 or -1
    const int chance = static_cast<int>(std::lround(50.0f + 50.0f * swing));
    return std::max(0, std::min(chance, 100));
}

// "RETURN 50/50 LOSE" in the middle of the screen
static void DrawRoulette(SDL_Renderer* r, const Roulette& roulette)
{
    const float t = roulette.timer;
    const int chance = RouletteReturnChance(t, roulette.returned);
    const bool done = (t >= ROULETTE_SPIN_TIME);

    // Fades out at the end, and the side that lost is dimmed once the result is in
    const float fade = std::max(0.0f, std::min(1.0f, (ROULETTE_TIME - t) / POPUP_FADE_TIME));
    const Uint8 full = static_cast<Uint8>(255.0f * fade);
    const Uint8 dim = static_cast<Uint8>(110.0f * fade);
    const Uint8 returnAlpha = (done && !roulette.returned) ? dim : full;
    const Uint8 loseAlpha = (done && roulette.returned) ? dim : full;

    // Layout: RETURN [number] / [number] LOSE, with fixed width gaps so the words don't move as the numbers change
    const float scale = 4.0f;
    const float cw = 8.0f * scale;
    const float leftExtent = cw / 2.0f + 3.0f * cw + cw / 2.0f + 6.0f * cw;  // slash, number, gap, RETURN
    const float rightExtent = cw / 2.0f + 3.0f * cw + cw / 2.0f + 4.0f * cw; // slash, number, gap, LOSE
    const float x0 = LAUNCHER_X - (leftExtent + rightExtent) / 2.0f;
    const float slashX = x0 + leftExtent; // centre of the slash
    const float y = (PLAY_TOP + PLAY_BOTTOM) / 2.0f - 50.0f;

    const std::string leftNum = std::to_string(chance);
    const std::string rightNum = std::to_string(100 - chance);

    DrawOutlinedText(r, "RETURN", x0, y, scale, COL_GREEN, returnAlpha);
    DrawOutlinedText(r, leftNum, slashX - cw / 2.0f - TextWidth(leftNum, scale), y, scale, COL_WHITE, returnAlpha);
    DrawOutlinedText(r, "/", slashX - cw / 2.0f, y, scale, COL_WHITE, full);
    DrawOutlinedText(r, rightNum, slashX + cw / 2.0f, y, scale, COL_WHITE, loseAlpha);
    DrawOutlinedText(r, "LOSE", slashX + cw / 2.0f + 3.0f * cw + cw / 2.0f, y, scale, COL_RED, loseAlpha);

    // A bar underneath showing the same split
    const float barW = 400.0f;
    const float barH = 14.0f;
    const float barX = LAUNCHER_X - barW / 2.0f;
    const float barY = y + cw + 14.0f;
    const float greenW = barW * static_cast<float>(chance) / 100.0f;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const SDL_FRect outline = { barX - 2.0f, barY - 2.0f, barW + 4.0f, barH + 4.0f };
    SDL_SetRenderDrawColor(r, 0, 0, 0, full);
    SDL_RenderFillRect(r, &outline);
    const SDL_FRect green = { barX, barY, greenW, barH };
    SDL_SetRenderDrawColor(r, COL_GREEN.r, COL_GREEN.g, COL_GREEN.b, returnAlpha);
    SDL_RenderFillRect(r, &green);
    const SDL_FRect red = { barX + greenW, barY, barW - greenW, barH };
    SDL_SetRenderDrawColor(r, COL_RED.r, COL_RED.g, COL_RED.b, loseAlpha);
    SDL_RenderFillRect(r, &red);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // The result, once the numbers have settled
    if (done)
    {
        const std::string result = roulette.returned ? "SHOT RETURNED!" : "SHOT LOST";
        const SDL_Color col = roulette.returned ? COL_CYAN : SDL_Color{ 200, 200, 210, 255 };
        DrawOutlinedText(r, result, LAUNCHER_X - TextWidth(result, 3.0f) / 2.0f, barY + barH + 22.0f, 3.0f, col, full);
    }
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
    int   removeCount = 0; // how many pegs were waiting when the deleting started
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
        removeCount = 0;
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
                    if (!ball.active && hitOrder.empty() && game.ballsLeft > 0 && !game.levelComplete && !game.roulette.active)
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

        // ---- Pop-up timers ----
        // Done before the physics so a pop-up made this frame is always drawn at least once
        if (game.shot.freeBallFlash > 0.0f)
            game.shot.freeBallFlash -= frameDt;
        for (FloatingText& f : game.floaters)
            f.timer -= frameDt;
        game.floaters.erase(std::remove_if(game.floaters.begin(), game.floaters.end(),
                                           [](const FloatingText& f) { return f.timer <= 0.0f; }),
                            game.floaters.end());

        // The 50/50 swings for a while, then the shot is given back (or not), then the pop-up goes away
        if (game.roulette.active)
        {
            Roulette& rl = game.roulette;
            rl.timer += frameDt;
            if (!rl.revealed && rl.timer >= ROULETTE_SPIN_TIME)
            {
                rl.revealed = true;
                if (rl.returned) game.ballsLeft++;
            }
            if (rl.timer >= ROULETTE_TIME) rl.active = false;
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
            // The gap between deletions shrinks the more pegs there are, so it takes the same total time (up to a maximum gap)
            if (removeCount == 0) removeCount = static_cast<int>(hitOrder.size());
            const float removeInterval = std::min(PEG_REMOVE_MAX_DELAY, PEG_REMOVE_TOTAL_TIME / removeCount);

            removeTimer += frameDt;
            while (removeTimer >= removeInterval && !hitOrder.empty())
            {
                pegs[hitOrder.front()].removed = true;
                hitOrder.erase(hitOrder.begin());
                removeTimer -= removeInterval;
            }
        }
        else
        {
            removeTimer = 0.0f;
            removeCount = 0;
        }

        // Shot is finished and the hit pegs are gone, so the shot is scored
        if (game.shotInProgress && !ball.active && hitOrder.empty())
            FinishShot(game, pegs, popup);

        if (popup.timer > 0.0f)
            popup.timer -= frameDt;

        // Once the shot popup is gone, check if the level is cleared or out of shots
        if (screen == Screen::Playing && !game.shotInProgress && !ball.active && hitOrder.empty() && popup.timer <= 0.0f && !game.roulette.active)
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

            SDL_SetRenderDrawColor(renderer, 205, 215, 235, 255);
            SDL_RenderFillRect(renderer, &bucketRect);

            const SDL_FColor triColour = { 0.62f, 0.68f, 0.85f, 1.0f };
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
        if (!ball.active && hitOrder.empty() && game.ballsLeft > 0 && !game.levelComplete && !game.roulette.active)
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

        // Points next to pegs and style shot names
        DrawFloatingTexts(renderer, game.floaters);

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

        // Balls left stacked up in the left border with the free ball bars either side
        DrawBallMeter(renderer, game, game.shotInProgress || popup.timer > 0.0f);

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
            DrawShotPopup(renderer, popup);

        // 50/50 for a shot that hit nothing
        if (game.roulette.active)
            DrawRoulette(renderer, game.roulette);

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
