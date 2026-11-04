#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "Config.h"
#include "Audio.h"
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

// Popping animation particles & shockwave
struct PegPopParticle
{
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float radius = 3.0f;
};

struct PegPopEffect
{
    float x = 0.0f, y = 0.0f;
    float timer = 0.0f;
    float duration = 0.35f;
    SDL_Color colour = { 255, 255, 255, 255 };
    std::vector<PegPopParticle> particles;
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

// The +10,000 shown next to a ball in the left stack as it is cashed in during extreme fever
struct BallBonusPopup
{
    int index = 0; // which ball in the stack (0 = bottom)
    float timer = 0.0f;
};

// The final shot's breakdown: fireworks while the pegs are cleared, then FEVER SCORE counting up
struct Finale
{
    bool fireworksActive = false; // the shot has landed in a bucket, the fireworks are playing and the pegs are being cleared
    float fireworksTimer = 0.0f;
    bool scoreActive = false;     // FEVER SCORE is on screen
    bool ballsStarted = false;    // the balls left have started to be cashed in
    long long target = 0;         // what the number counts up to (grows by 10,000 for each ball cashed in)
    long long shown = 0;          // the number on screen, this is also added to the total score as it climbs
    double shownF = 0.0;
    float timer = 0.0f;           // time since FEVER SCORE appeared
    float holdTimer = 0.0f;       // time the finished number has been on screen
};

struct DroppingBall
{
    float y = 0.0f;
    float vy = 0.0f;
    int targetIndex = 0; // Which slot in the stack it is falling into
    int bouncesLeft = 1; // 1 small bounce upon landing
    bool settled = false;
};

// ---------------------------------------------------------------------------
// Run State (10-Level Challenge Run)
// ---------------------------------------------------------------------------
struct RunState
{
    bool active = false;
    int currentLevel = 1;
    bool noFail = true;
    int completedCount = 0; // Number of levels cleared and moved past with NEXT LEVEL
    long long levelScores[10] = { 0 };

    long long CompletedTotal() const
    {
        long long sum = 0;
        for (int i = 0; i < completedCount && i < 10; ++i)
            sum += levelScores[i];
        return sum;
    }
};

static void SaveRunToFile(const RunState& run)
{
    if (!run.active)
    {
        std::remove(RUN_SAVE_FILE);
        return;
    }
    std::ofstream f(RUN_SAVE_FILE, std::ios::binary | std::ios::trunc);
    if (f)
    {
        f.write(reinterpret_cast<const char*>(&run), sizeof(RunState));
    }
}

static RunState LoadRunFromFile()
{
    RunState run;
    std::ifstream f(RUN_SAVE_FILE, std::ios::binary);
    if (f)
    {
        f.read(reinterpret_cast<char*>(&run), sizeof(RunState));
    }
    return run;
}

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
    bool approachingLastOrange = false; // the ball is close to the last orange peg and heading for it
    bool slowmoTriggered = false; // the slow motion sound has played this shot
    float timeScale = 1.0f; // how fast time is running, 1 is normal
    int bucketStreak = 0; // bucket shots in a row, for Mad Skillz
    Roulette roulette;
    float bucketTime = BUCKET_SWEEP_TIME * 0.5f; // time through the bucket's back and forth movement
    float bucketX = LAUNCHER_X; // centre of the bucket
    ShotStats shot;
    std::vector<FloatingText> floaters; // small pop-ups next to pegs
    std::vector<PegPopEffect> pegPops;

    // After the last orange peg's shot, the balls left are cashed in one by one from the top of the stack
    bool ballBonusActive = false;
    int ballBonusShown = 0;      // balls still drawn in the stack while it is cashing in
    int ballBonusLayoutCount = 0; // how many balls the stack had at the start, so the spacing doesn't change
    float ballBonusTimer = 0.0f;
    std::vector<BallBonusPopup> ballBonusPopups;
    std::vector<DroppingBall> droppingBalls;

    // Anti-Stuck Tracking
    float stuckTimer = 0.0f;
    float stuckCheckX = 0.0f;
    float stuckCheckY = 0.0f;

    // Ultra Fever Ribbon Popup
    bool ribbonActive = false;
    float ribbonTimer = 0.0f;
    bool ribbonDone = false;

    // Final shot
    Finale finale;

    // Camera. camZoom is 0 for the normal view and 1 when fully zoomed in on the ball (SLOWMO_ZOOM times bigger).
    float camZoom = 0.0f;
    float camFocusX = LAUNCHER_X;
    float camFocusY = (PLAY_TOP + PLAY_BOTTOM) / 2.0f;
    bool  feverZoomOut = false;   // EXTREME FEVER text is up and the view is zooming back out
    float feverZoomTimer = 0.0f;
    float feverZoomStart = 0.0f;  // how zoomed in the view was when the last orange peg was hit
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
    SaveScore,
    SaveChallengeScore,
    OpenSettings,
    CloseSettings,
    OpenStyleShots,
    CloseStyleShots,
    ContinueRun,
    NewRun,
    CancelRunPopup
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

static const SDL_Color COL_WHITE = { 255, 255, 255, 255 };
static const SDL_Color COL_YELLOW = { 255, 225, 40, 255 };
static const SDL_Color COL_ORANGE = { 255, 150, 30, 255 };
static const SDL_Color COL_GREEN = { 90, 240, 90, 255 };
static const SDL_Color COL_CYAN = { 120, 230, 235, 255 };
static const SDL_Color COL_RED = { 240, 70, 60, 255 };

static void DrawFilledCircle(SDL_Renderer* r, float cx, float cy, float radius)
{
    const int ir = static_cast<int>(std::ceil(radius));
    for (int dy = -ir; dy <= ir; ++dy)
    {
        const float h = std::sqrt(std::max(0.0f, radius * radius - static_cast<float>(dy * dy)));
        SDL_RenderLine(r, cx - h, cy + dy, cx + h, cy + dy);
    }
}

static void DrawCircleOutline(SDL_Renderer* r, float cx, float cy, float radius, float thickness = 2.0f)
{
    constexpr int SEGMENTS = 24;
    for (int t = 0; t < static_cast<int>(thickness); ++t)
    {
        const float rad = radius - static_cast<float>(t);
        if (rad <= 0.0f) break;
        for (int i = 0; i < SEGMENTS; ++i)
        {
            const float a1 = (static_cast<float>(i) / SEGMENTS) * SDL_PI_F * 2.0f;
            const float a2 = (static_cast<float>(i + 1) / SEGMENTS) * SDL_PI_F * 2.0f;
            SDL_RenderLine(r, cx + std::cos(a1) * rad, cy + std::sin(a1) * rad,
                cx + std::cos(a2) * rad, cy + std::sin(a2) * rad);
        }
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

// ---------------------------------------------------------------------------
// Settings Popup
// ---------------------------------------------------------------------------
static SDL_FRect SettingsPanel()
{
    return { LAUNCHER_X - 180.0f, 170.0f, 360.0f, 240.0f };
}

static SDL_FRect VolumeSliderBar()
{
    const SDL_FRect panel = SettingsPanel();
    return { panel.x + 40.0f, panel.y + 110.0f, 280.0f, 16.0f };
}

static std::vector<Button> GetSettingsButtons()
{
    const SDL_FRect panel = SettingsPanel();
    return {
        { { LAUNCHER_X - 75.0f, panel.y + 170.0f, 150.0f, 40.0f }, "CLOSE", Action::CloseSettings, 0 }
    };
}

static void DrawSettings(SDL_Renderer* r, float mouseX, float mouseY)
{
    DrawDim(r, 190);
    const SDL_FRect panel = SettingsPanel();
    DrawPanel(r, panel);

    // Title
    const std::string title = "SETTINGS";
    SDL_SetRenderDrawColor(r, 255, 220, 80, 255);
    DrawText(r, title, LAUNCHER_X - TextWidth(title, 3.0f) / 2.0f, panel.y + 20.0f, 3.0f);

    // Volume percentage label (0% - 200%)
    const int percent = static_cast<int>(std::round(GetAudioVolume() * 100.0f));
    const std::string volText = "AUDIO VOLUME: " + std::to_string(percent) + "%";
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    DrawText(r, volText, LAUNCHER_X - TextWidth(volText, 2.0f) / 2.0f, panel.y + 75.0f, 2.0f);

    // Slider bar background
    const SDL_FRect bar = VolumeSliderBar();
    SDL_SetRenderDrawColor(r, 35, 38, 50, 255);
    SDL_RenderFillRect(r, &bar);

    // Progress fraction (0.0 at 0%, 0.5 at 100%, 1.0 at 200%)
    const float progress = std::max(0.0f, std::min(1.0f, GetAudioVolume() / 2.0f));

    // Filled portion
    const SDL_FRect filled = { bar.x, bar.y, bar.w * progress, bar.h };
    SDL_SetRenderDrawColor(r, 80, 180, 240, 255);
    SDL_RenderFillRect(r, &filled);

    // Slider outline
    SDL_SetRenderDrawColor(r, 180, 190, 210, 255);
    SDL_RenderRect(r, &bar);

    // 100% tick notch in the exact center of the bar
    const float midX = bar.x + bar.w * 0.5f;
    SDL_SetRenderDrawColor(r, 200, 200, 220, 255);
    SDL_RenderLine(r, midX, bar.y - 2.0f, midX, bar.y + bar.h + 2.0f);

    // Slider knob
    const float knobX = bar.x + bar.w * progress;
    const float knobY = bar.y + bar.h / 2.0f;
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    DrawFilledCircle(r, knobX, knobY, 12.0f);
    SDL_SetRenderDrawColor(r, 45, 60, 100, 255);
    DrawFilledCircle(r, knobX, knobY, 7.0f);

    // Close button
    DrawButtons(r, GetSettingsButtons(), mouseX, mouseY);
}

// ---------------------------------------------------------------------------
// Style Shots Popup
// ---------------------------------------------------------------------------
static SDL_FRect StyleShotsPanel()
{
    return { LAUNCHER_X - 290.0f, 35.0f, 580.0f, 530.0f };
}

static std::vector<Button> GetStyleShotsButtons()
{
    const SDL_FRect panel = StyleShotsPanel();
    return {
        { { LAUNCHER_X - 75.0f, panel.y + panel.h - 46.0f, 150.0f, 34.0f }, "CLOSE", Action::CloseStyleShots, 0 }
    };
}

static void DrawStyleShots(SDL_Renderer* r, float mouseX, float mouseY)
{
    DrawDim(r, 205);
    const SDL_FRect panel = StyleShotsPanel();
    DrawPanel(r, panel);

    // Title
    const std::string title = "STYLE SHOTS";
    SDL_SetRenderDrawColor(r, 255, 220, 80, 255);
    DrawText(r, title, LAUNCHER_X - TextWidth(title, 3.0f) / 2.0f, panel.y + 16.0f, 3.0f);

    struct StyleInfo {
        std::string name;
        std::string pts;
        std::string desc;
    };

    static const StyleInfo list[] = {
        { "LONG SHOT",         "+5,000",   "Hit a non-blue peg from 1/3 screen distance away." },
        { "SUPER LONG SHOT",   "+25,000",  "Hit a non-blue peg from 2/3 screen distance away." },
        { "DOUBLE LONG SHOT",  "+25,000",  "Make two long shots in a row in a single turn." },
        { "OFF THE WALL",      "+25,000",  "Bounce off a wall and travel 1/5 screen height into a peg." },
        { "FREE BALL SKILLS",  "+5,000",   "Catch the ball in the bucket after hitting only 1 peg." },
        { "LUCKY BOUNCE",      "+25,000",  "Bounce off bucket rim, hit a peg, then land in bucket." },
        { "KICK THE BUCKET",   "+25,000",  "Bounce off bucket rim directly into the last orange peg." },
        { "ORANGE ATTACK!",    "+50,000",  "Fill an entire multiplier stage in a single shot." },
        { "ORANGE HAT TRICK",  "+100,000", "Hit the final 3 orange pegs all in the same shot." },
        { "MAD SKILLZ",        "+25,000",  "Catch the ball in the moving bucket 5 shots in a row." },
        { "CRAZY MAD SKILLZ",  "+100,000", "Catch the ball in the moving bucket 10 shots in a row." }
    };

    float y = panel.y + 55.0f;
    for (const auto& item : list)
    {
        // Name (Yellow)
        SDL_SetRenderDrawColor(r, 255, 220, 80, 255);
        DrawText(r, item.name, panel.x + 20.0f, y, 1.0f);

        // Points (Green)
        SDL_SetRenderDrawColor(r, 90, 240, 90, 255);
        DrawText(r, item.pts, panel.x + panel.w - 20.0f - TextWidth(item.pts, 1.0f), y, 1.0f);

        // Description (Light Grey)
        SDL_SetRenderDrawColor(r, 210, 215, 225, 255);
        DrawText(r, item.desc, panel.x + 20.0f, y + 12.0f, 1.0f);

        // Divider line
        SDL_SetRenderDrawColor(r, 50, 55, 75, 255);
        SDL_RenderLine(r, panel.x + 20.0f, y + 27.0f, panel.x + panel.w - 20.0f, y + 27.0f);

        y += 35.0f;
    }

    DrawButtons(r, GetStyleShotsButtons(), mouseX, mouseY);
}

// ---------------------------------------------------------------------------
// Run Prompt Popup (Continue / New Run)
// ---------------------------------------------------------------------------
static SDL_FRect RunPromptPanel()
{
    return { LAUNCHER_X - 170.0f, 180.0f, 340.0f, 240.0f };
}

static std::vector<Button> GetRunPromptButtons()
{
    const SDL_FRect p = RunPromptPanel();
    const float bx = LAUNCHER_X - 120.0f;
    return {
        { { bx, p.y + 75.0f,  240.0f, 38.0f }, "CONTINUE RUN", Action::ContinueRun, 0 },
        { { bx, p.y + 122.0f, 240.0f, 38.0f }, "NEW 10-RUN",    Action::NewRun,      0 },
        { { bx, p.y + 169.0f, 240.0f, 38.0f }, "CANCEL",       Action::CancelRunPopup, 0 }
    };
}

static void DrawRunPrompt(SDL_Renderer* r, const RunState& run, float mouseX, float mouseY)
{
    DrawDim(r, 200);
    const SDL_FRect p = RunPromptPanel();
    DrawPanel(r, p);

    const std::string title = "ACTIVE 10-RUN";
    SDL_SetRenderDrawColor(r, 255, 220, 80, 255);
    DrawText(r, title, LAUNCHER_X - TextWidth(title, 2.5f) / 2.0f, p.y + 16.0f, 2.5f);

    const std::string info = "Level " + std::to_string(run.currentLevel) + " | " +
        (run.noFail ? "NO FAIL" : "RETRY USED") + " | " + FormatNumber(run.CompletedTotal()) + " pts";
    SDL_SetRenderDrawColor(r, 200, 210, 225, 255);
    DrawText(r, info, LAUNCHER_X - TextWidth(info, 1.0f) / 2.0f, p.y + 48.0f, 1.0f);

    DrawButtons(r, GetRunPromptButtons(), mouseX, mouseY);
}

// The buttons for each screen, used both to draw them and to click them
static std::vector<Button> GetScreenButtons(Screen screen, int currentLevel, int levelCount, bool canSave, bool scoreSaved)
{
    std::vector<Button> buttons;

    if (screen == Screen::MainMenu)
    {
        buttons.push_back({ { 60.0f, 106.0f, 240.0f, 44.0f }, "PLAY", Action::Play, 0 });
        buttons.push_back({ { 60.0f, 160.0f, 240.0f, 44.0f }, "LEVEL SELECT", Action::OpenLevelSelect, 0 });
        buttons.push_back({ { 60.0f, 214.0f, 240.0f, 44.0f }, "SETTINGS", Action::OpenSettings, 0 });
        buttons.push_back({ { 60.0f, 268.0f, 240.0f, 44.0f }, "STYLE SHOTS", Action::OpenStyleShots, 0 });
        buttons.push_back({ { 60.0f, 322.0f, 240.0f, 44.0f }, "QUIT", Action::Quit, 0 });
    }
    else if (screen == Screen::Paused)
    {
        const float x = LAUNCHER_X - 150.0f;
        float y = 206.0f;
        const float btnH = 36.0f;
        const float btnStep = 44.0f;
        buttons.push_back({ { x, y, 300.0f, btnH }, "RESUME", Action::Resume, 0 }); y += btnStep;
        buttons.push_back({ { x, y, 300.0f, btnH }, "RESTART", Action::Restart, 0 }); y += btnStep;
        buttons.push_back({ { x, y, 300.0f, btnH }, "STYLE SHOTS", Action::OpenStyleShots, 0 }); y += btnStep;
        buttons.push_back({ { x, y, 300.0f, btnH }, "SETTINGS", Action::OpenSettings, 0 }); y += btnStep;
        buttons.push_back({ { x, y, 300.0f, btnH }, "MAIN MENU", Action::MainMenu, 0 }); y += btnStep;
        buttons.push_back({ { x, y, 300.0f, btnH }, "LEVEL SELECT", Action::OpenLevelSelect, 0 });
    }
    else if (screen == Screen::Fail)
    {
        const float x = LAUNCHER_X - 150.0f;
        float y = 230.0f;
        buttons.push_back({ { x, y, 300.0f, 40.0f }, "RESTART", Action::Restart, 0 }); y += 48.0f;
        buttons.push_back({ { x, y, 300.0f, 40.0f }, "SETTINGS", Action::OpenSettings, 0 }); y += 48.0f;
        buttons.push_back({ { x, y, 300.0f, 40.0f }, "MAIN MENU", Action::MainMenu, 0 }); y += 48.0f;
        buttons.push_back({ { x, y, 300.0f, 40.0f }, "LEVEL SELECT", Action::OpenLevelSelect, 0 });
    }
    else if (screen == Screen::LevelClear)
    {
        const float x = LAUNCHER_X - 150.0f;
        const bool isLevel10 = (currentLevel == levelCount);

        float y = isLevel10 ? 245.0f : 238.0f;
        const float btnH = isLevel10 ? 34.0f : 38.0f;
        const float btnStep = isLevel10 ? 39.0f : 44.0f;

        // Button 1: Level 10 score
        buttons.push_back({ { x, y, 300.0f, btnH }, scoreSaved ? "LVL 10 SAVED" : "SAVE LEVEL SCORE", (canSave && !scoreSaved) ? Action::SaveScore : Action::None, 0 });
        y += btnStep;

        // Button 2: If on Level 10, separate button for the 10-Run challenge score
        if (isLevel10)
        {
            buttons.push_back({ { x, y, 300.0f, btnH }, "SAVE 10-RUN SCORE", Action::SaveChallengeScore, 0 });
            y += btnStep;
        }

        if (currentLevel < levelCount)
        {
            buttons.push_back({ { x, y, 300.0f, btnH }, "NEXT LEVEL", Action::NextLevel, 0 });
            y += btnStep;
        }

        buttons.push_back({ { x, y, 300.0f, btnH }, "SETTINGS", Action::OpenSettings, 0 });
        y += btnStep;

        buttons.push_back({ { x, y, 300.0f, btnH }, "MAIN MENU", Action::MainMenu, 0 });
        y += btnStep;

        buttons.push_back({ { x, y, 300.0f, btnH }, "LEVEL SELECT", Action::OpenLevelSelect, 0 });
        y += btnStep;

        buttons.push_back({ { x, y, 300.0f, btnH }, "RETRY", Action::Restart, 0 });
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

// Calculates the resting Y position for a ball in the left stack
static float GetStackBallY(int index, int totalBalls)
{
    const float circleR = FREE_BALL_CIRCLE_RADIUS;
    const float cy = PLAY_BOTTOM - circleR - 12.0f;
    const float barTop = PLAY_TOP + 10.0f;
    const float firstY = cy - circleR - 4.0f - STACK_BALL_RADIUS;
    const float topY = barTop + 6.0f + STACK_BALL_RADIUS;

    float pitch = STACK_BALL_PITCH;
    if (totalBalls > 1)
        pitch = std::min(pitch, (firstY - topY) / static_cast<float>(totalBalls - 1));

    return firstY - index * pitch;
}

// Gives a free ball and starts its drop animation down the chute
static void AwardFreeBall(GameState& game)
{
    const int targetIdx = game.ballsLeft; // The new top of the stack
    game.ballsLeft++;

    DroppingBall drop;
    drop.y = PLAY_TOP - 15.0f; // Starts above the top of the chute
    drop.vy = 80.0f;           // Initial downward velocity
    drop.targetIndex = targetIdx;
    game.droppingBalls.push_back(drop);
}

// Gives a free ball each time the shot's score passes the next threshold
static void CheckFreeBalls(GameState& game)
{
    ShotStats& shot = game.shot;
    while (shot.freeBallsEarned < FREE_BALL_THRESHOLD_COUNT &&
        LiveShotScore(shot) >= FREE_BALL_THRESHOLDS[shot.freeBallsEarned])
    {
        PlaySfx(static_cast<Sfx>(static_cast<int>(Sfx::FreeBallScore1) + shot.freeBallsEarned));
        shot.freeBallsEarned++;
        AwardFreeBall(game);
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

    PlaySfx(Sfx::StyleShot);
    CheckFreeBalls(game);
}

static bool AllPegsCleared(const std::vector<Peg>& pegs)
{
    for (const Peg& peg : pegs)
        if (!peg.hit && !peg.removed) return false;
    return true;
}

static float FeverCircleX(int k)
{
    return PLAY_LEFT + k * (PLAY_RIGHT - PLAY_LEFT) / static_cast<float>(FEVER_BUCKET_COUNT);
}

static void GetAimDirection(float mouseX, float mouseY, float& dirX, float& dirY)
{
    const float dx = mouseX - LAUNCHER_X;
    const float dy = mouseY - LAUNCHER_Y;

    const float minAngle = MIN_AIM_ANGLE_DEG * SDL_PI_F / 180.0f;

    const float side = (dx >= 0.0f) ? 1.0f : -1.0f;
    const float adx = std::fabs(dx);

    float angle;
    if (adx < 1.0f)
    {
        angle = SDL_PI_F / 2.0f;
    }
    else
    {
        const float a = GRAVITY * adx * adx / (2.0f * SHOT_SPEED * SHOT_SPEED);
        const float disc = adx * adx - 4.0f * a * (a - dy);

        if (disc >= 0.0f)
            angle = std::atan((-adx + std::sqrt(disc)) / (2.0f * a));
        else
            angle = std::atan2(dy, adx);
    }

    angle = std::max(angle, minAngle);

    dirX = side * std::cos(angle);
    dirY = std::sin(angle);
}

static void DrawAimGuide(SDL_Renderer* r, const std::vector<Peg>& pegs, float dirX, float dirY)
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

        bool touchingPeg = false;
        for (const Peg& peg : pegs)
        {
            if (peg.removed) continue;
            const float pdx = x - peg.x;
            const float pdy = y - peg.y;
            const float reach = BALL_RADIUS + PEG_RADIUS;
            if (pdx * pdx + pdy * pdy < reach * reach)
            {
                touchingPeg = true;
                break;
            }
        }
        if (touchingPeg) break;

        if (travelled >= nextDot)
        {
            const float t = travelled / GUIDE_LENGTH;
            const Uint8 alpha = static_cast<Uint8>(255.0f * (1.0f - 0.6f * t));
            SDL_SetRenderDrawColor(r, 120, 230, 235, alpha);
            DrawFilledCircle(r, x, y, GUIDE_DOT_RADIUS);
            nextDot += GUIDE_DOT_SPACING;
        }
    }

    if (!leftScreen)
    {
        const float len = std::sqrt(vx * vx + vy * vy);
        const float tx = vx / len, ty = vy / len;
        const float nx = -ty, ny = tx;
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
        if (-vn >= BUCKET_BOUNCE_SOUND_MIN_SPEED)
            PlaySfx(Sfx::FeverBucketBounce);

        ball.vx -= (1.0f + PEG_BOUNCE) * vn * nx;
        ball.vy -= (1.0f + PEG_BOUNCE) * vn * ny;
    }
}

struct Triangle
{
    SDL_FPoint p[3];
};

static float BucketProgress(float t)
{
    const float dwell = BUCKET_DWELL_TIME;
    const float ramp = BUCKET_RAMP_TIME;
    const float cruise = BUCKET_SWEEP_TIME - dwell - 2.0f * ramp;
    const float total = ramp + cruise;

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
    game.bucketTime += dt;
    if (game.bucketTime >= 2.0f * BUCKET_SWEEP_TIME)
        game.bucketTime -= 2.0f * BUCKET_SWEEP_TIME;

    const bool returning = game.bucketTime >= BUCKET_SWEEP_TIME;
    const float t = returning ? game.bucketTime - BUCKET_SWEEP_TIME : game.bucketTime;
    float p = BucketProgress(t);
    if (returning) p = 1.0f - p;

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

    left = { { { l, apex }, { l - BUCKET_TRI_WIDTH, PLAY_BOTTOM }, { l, PLAY_BOTTOM } } };
    right = { { { r, apex }, { r, PLAY_BOTTOM }, { r + BUCKET_TRI_WIDTH, PLAY_BOTTOM } } };
}

static bool BounceOffTriangle(Ball& ball, const Triangle& tri)
{
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
        nx = (inside ? bestX - ball.x : ball.x - bestX) / dist;
        ny = (inside ? bestY - ball.y : ball.y - bestY) / dist;
    }

    ball.x = bestX + nx * BALL_RADIUS;
    ball.y = bestY + ny * BALL_RADIUS;

    const float vn = ball.vx * nx + ball.vy * ny;
    if (vn < 0.0f)
    {
        if (-vn >= BUCKET_BOUNCE_SOUND_MIN_SPEED)
            PlaySfx(Sfx::BucketBounce);

        ball.vx -= (1.0f + PEG_BOUNCE) * vn * nx;
        ball.vy -= (1.0f + PEG_BOUNCE) * vn * ny;
    }

    return ny < -0.3f;
}

static std::mt19937& GameRng()
{
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}

static void SpawnPegPop(GameState& game, const Peg& peg)
{
    PegPopEffect fx;
    fx.x = peg.x;
    fx.y = peg.y;
    fx.timer = 0.0f;
    fx.duration = 0.35f;

    if (peg.type == PegType::Orange) fx.colour = { 255, 160, 40, 255 };
    else if (peg.type == PegType::Purple) fx.colour = { 220, 80, 240, 255 };
    else fx.colour = { 80, 160, 255, 255 };

    constexpr int NUM_PARTICLES = 10;
    for (int i = 0; i < NUM_PARTICLES; ++i)
    {
        const float angle = (static_cast<float>(i) / NUM_PARTICLES) * SDL_PI_F * 2.0f +
            (std::uniform_real_distribution<float>(-0.2f, 0.2f)(GameRng()));
        const float speed = std::uniform_real_distribution<float>(70.0f, 160.0f)(GameRng());
        PegPopParticle p;
        p.x = peg.x;
        p.y = peg.y;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.radius = std::uniform_real_distribution<float>(2.0f, 4.0f)(GameRng());
        fx.particles.push_back(p);
    }
    game.pegPops.push_back(fx);
}

static void DrawPegPops(SDL_Renderer* r, const std::vector<PegPopEffect>& pops)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (const auto& fx : pops)
    {
        const float progress = fx.timer / fx.duration;
        const float alphaNorm = std::max(0.0f, 1.0f - progress);
        const Uint8 alpha = static_cast<Uint8>(255.0f * alphaNorm);

        // Expanding shockwave ring
        const float ringRadius = PEG_RADIUS + progress * 22.0f;
        SDL_SetRenderDrawColor(r, fx.colour.r, fx.colour.g, fx.colour.b, alpha);
        DrawCircleOutline(r, fx.x, fx.y, ringRadius, 2.0f);

        // Quick bright core flash
        if (progress < 0.3f)
        {
            const float flashP = 1.0f - (progress / 0.3f);
            SDL_SetRenderDrawColor(r, 255, 255, 255, static_cast<Uint8>(200.0f * flashP));
            DrawFilledCircle(r, fx.x, fx.y, PEG_RADIUS * (1.0f + 0.3f * flashP));
        }

        // Flying bursting sparks
        for (const auto& p : fx.particles)
        {
            const float rad = std::max(0.5f, p.radius * alphaNorm);
            SDL_SetRenderDrawColor(r, fx.colour.r, fx.colour.g, fx.colour.b, alpha);
            DrawFilledCircle(r, p.x, p.y, rad);
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void QueueRemainingPegs(std::vector<Peg>& pegs, std::vector<int>& hitOrder)
{
    std::vector<int> rest;
    for (int i = 0; i < static_cast<int>(pegs.size()); ++i)
        if (!pegs[i].removed && !pegs[i].hit) rest.push_back(i);
    std::shuffle(rest.begin(), rest.end(), GameRng());
    hitOrder.insert(hitOrder.end(), rest.begin(), rest.end());
}

static void UpdateBall(Ball& ball, std::vector<Peg>& pegs, std::vector<int>& hitOrder, GameState& game, float dt)
{
    if (!ball.active) return;
    ShotStats& shot = game.shot;

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

    // Time slows down just before hitting the last orange peg
    game.approachingLastOrange = false;
    if (!game.feverActive && game.totalOrange > 0 && game.orangeHit == game.totalOrange - 1)
    {
        const float speed = std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
        for (const Peg& peg : pegs)
        {
            if (peg.removed || peg.hit || peg.type != PegType::Orange) continue;
            const float toX = peg.x - ball.x;
            const float toY = peg.y - ball.y;
            const float dist = std::sqrt(toX * toX + toY * toY);
            if (dist < 0.001f || speed < 1.0f || dist > SLOWMO_TRIGGER_DISTANCE) continue;

            if ((ball.vx * toX + ball.vy * toY) / (speed * dist) > 0.8f)
                game.approachingLastOrange = true;
        }
    }
    if (game.approachingLastOrange && !game.slowmoTriggered)
    {
        game.slowmoTriggered = true;
        PlaySfx(Sfx::SlowMotion);
    }

    for (int i = 0; i < static_cast<int>(pegs.size()); ++i)
    {
        Peg& peg = pegs[i];
        if (peg.removed) continue;

        const float dx = ball.x - peg.x;
        const float dy = ball.y - peg.y;
        const float minDist = BALL_RADIUS + PEG_RADIUS;
        const float distSq = dx * dx + dy * dy;
        if (distSq >= minDist * minDist) continue;

        if (i != shot.lastContactPeg)
        {
            const bool nonBlue = (peg.type != PegType::Blue);
            const bool freshNonBlue = nonBlue && !peg.hit;

            bool longShot = false;
            if (freshNonBlue && shot.lastContactNonBlue)
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

            if (shot.bouncedOffWall && freshNonBlue && shot.distSinceWall >= OFF_THE_WALL_DISTANCE)
                AddStyle(game, "OFF THE WALL", OFF_THE_WALL_POINTS, peg.x, peg.y);
            shot.bouncedOffWall = false;

            shot.lastContactPeg = i;
            shot.lastContactNonBlue = nonBlue;
        }
        shot.distSinceContact = 0.0f;

        if (!peg.hit)
        {
            peg.hit = true;
            hitOrder.push_back(i);

            if (peg.type == PegType::Orange)
                game.orangeHit++;

            const int pegPoints = BasePegScore(peg.type) * FeverMultiplier(game.orangeHit);
            shot.pegScoreSum += pegPoints;
            shot.pegsHit++;
            AddPegPopup(game, peg, pegPoints);
            PlaySfx(Sfx::PegHit, std::min((shot.pegsHit - 1) * PEG_HIT_PITCH_STEP, PEG_HIT_PITCH_MAX));
            if (peg.type == PegType::Purple)
                PlaySfx(Sfx::PurplePeg);
            CheckFreeBalls(game);

            if (peg.type == PegType::Orange)
            {
                for (int s = 0; s < ORANGE_STAGE_COUNT; ++s)
                {
                    const int stageStart = (s == 0) ? 0 : ORANGE_STAGE_ENDS[s - 1];
                    if (game.orangeHit == ORANGE_STAGE_ENDS[s] && shot.orangeAtStart <= stageStart)
                        AddStyle(game, "ORANGE ATTACK!", ORANGE_ATTACK_POINTS, peg.x, peg.y);
                }
            }

            if (peg.type == PegType::Orange && game.orangeHit == game.totalOrange)
            {
                game.feverActive = true;
                PlaySfx(Sfx::Applause);

                // Check if every single peg on screen has been hit!
                game.ultraFever = AllPegsCleared(pegs);

                // EXTREME FEVER text appears and the view slowly zooms back out
                game.feverZoomOut = true;
                game.feverZoomTimer = 0.0f;
                game.feverZoomStart = game.camZoom;

                // Kick the bucket, the ball came straight off the bucket's edge into the last orange peg
                if (shot.edgeSincePeg)
                    AddStyle(game, "KICK THE BUCKET", KICK_THE_BUCKET_POINTS, peg.x, peg.y);

                // Hat trick, the last 3 orange pegs were all hit in this shot
                if (game.totalOrange >= 3 && shot.orangeAtStart <= game.totalOrange - 3)
                    AddStyle(game, "ORANGE HAT TRICK", ORANGE_HAT_TRICK_POINTS, peg.x, peg.y);
            }
        }

        if (shot.bucketEdgeHit) shot.pegAfterEdge = true;
        shot.edgeSincePeg = false;

        const float dist = std::sqrt(distSq);
        float nx = 0.0f, ny = -1.0f;
        if (dist > 0.0001f)
        {
            nx = dx / dist;
            ny = dy / dist;
        }

        ball.x = peg.x + nx * minDist;
        ball.y = peg.y + ny * minDist;

        const float vn = ball.vx * nx + ball.vy * ny;
        if (vn < 0.0f)
        {
            ball.vx -= (1.0f + PEG_BOUNCE) * vn * nx;
            ball.vy -= (1.0f + PEG_BOUNCE) * vn * ny;
        }
    }

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
            shot.pegAfterEdge = false;
            shot.edgeSincePeg = true;
        }

        if (ball.x >= bucketRect.x && ball.x <= bucketRect.x + bucketRect.w && ball.y + BALL_RADIUS >= bucketRect.y)
        {
            ball.active = false;

            if (shot.pegsHit == 1 && !shot.bucketEdgeHit)
                AddStyle(game, "FREE BALL SKILLS!", FREE_BALL_SKILLS_POINTS, ball.x, ball.y);

            if (shot.bucketEdgeHit && shot.pegAfterEdge)
                AddStyle(game, "LUCKY BOUNCE", LUCKY_BOUNCE_POINTS, ball.x, ball.y);

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

            AwardFreeBall(game);
            PlaySfx(Sfx::FreeBall);
            game.freeBall = true;
            return;
        }
    }

    if (game.feverActive)
    {
        for (int k = 0; k <= FEVER_BUCKET_COUNT; ++k)
            BounceOffCircle(ball, FeverCircleX(k), FEVER_CIRCLE_Y, FEVER_CIRCLE_RADIUS);
    }

    if (ball.y - BALL_RADIUS > PLAY_BOTTOM)
    {
        ball.active = false;

        if (game.feverActive)
        {
            const float bucketWidth = (PLAY_RIGHT - PLAY_LEFT) / static_cast<float>(FEVER_BUCKET_COUNT);
            int bucket = static_cast<int>((ball.x - PLAY_LEFT) / bucketWidth);
            bucket = std::max(0, std::min(bucket, FEVER_BUCKET_COUNT - 1));

            game.ultraFever = AllPegsCleared(pegs);
            game.bucketPoints = game.ultraFever ? ULTRA_BUCKET_POINTS : FEVER_BUCKET_POINTS[bucket];
            PlaySfx(game.bucketPoints >= ULTRA_BUCKET_POINTS ? Sfx::FeverBucketTop : Sfx::FeverBucket);

            game.finale.fireworksActive = true;
            game.finale.fireworksTimer = 0.0f;
            QueueRemainingPegs(pegs, hitOrder);
        }
    }

    // -----------------------------------------------------------------------
    // Anti-Stuck / Rest Detection:
    // If the ball stays within a 12px radius for 1.0 second, remove touching pegs
    // -----------------------------------------------------------------------
    const float sDx = ball.x - game.stuckCheckX;
    const float sDy = ball.y - game.stuckCheckY;
    constexpr float STUCK_RADIUS = 12.0f;

    if (sDx * sDx + sDy * sDy < STUCK_RADIUS * STUCK_RADIUS)
    {
        game.stuckTimer += dt;
    }
    else
    {
        game.stuckTimer = 0.0f;
        game.stuckCheckX = ball.x;
        game.stuckCheckY = ball.y;
    }

    if (game.stuckTimer >= 1.0f) // Stuck for 1 second
    {
        bool freedAny = false;
        const float touchDist = BALL_RADIUS + PEG_RADIUS + 4.0f;

        for (int i = 0; i < static_cast<int>(pegs.size()); ++i)
        {
            Peg& peg = pegs[i];
            if (peg.removed) continue;

            const float pdx = ball.x - peg.x;
            const float pdy = ball.y - peg.y;

            if (pdx * pdx + pdy * pdy <= touchDist * touchDist)
            {
                // If touching a peg that wasn't scored yet, score it now
                if (!peg.hit)
                {
                    peg.hit = true;
                    if (peg.type == PegType::Orange)
                        game.orangeHit++;

                    const int pegPoints = BasePegScore(peg.type) * FeverMultiplier(game.orangeHit);
                    shot.pegScoreSum += pegPoints;
                    shot.pegsHit++;
                    AddPegPopup(game, peg, pegPoints);
                    PlaySfx(Sfx::PegHit, std::min((shot.pegsHit - 1) * PEG_HIT_PITCH_STEP, PEG_HIT_PITCH_MAX));
                    if (peg.type == PegType::Purple)
                        PlaySfx(Sfx::PurplePeg);
                    CheckFreeBalls(game);

                    if (peg.type == PegType::Orange && game.orangeHit == game.totalOrange)
                    {
                        game.feverActive = true;
                        PlaySfx(Sfx::Applause);
                        game.ultraFever = AllPegsCleared(pegs);
                        game.feverZoomOut = true;
                        game.feverZoomTimer = 0.0f;
                        game.feverZoomStart = game.camZoom;
                    }
                }

                // Delete the peg immediately so the ball can drop through
                SpawnPegPop(game, peg);
                peg.removed = true;
                PlaySfx(Sfx::PegRemove);

                // Erase from hitOrder so it isn't popped a second time at the end
                hitOrder.erase(std::remove(hitOrder.begin(), hitOrder.end(), i), hitOrder.end());
                freedAny = true;
            }
        }

        if (freedAny)
        {
            game.stuckTimer = 0.0f;
            // Downward nudge so gravity takes over
            ball.vy = std::max(ball.vy, 100.0f);
        }
    }
}

static void FinishShot(GameState& game, std::vector<Peg>& pegs, Popup& popup)
{
    const ShotStats& shot = game.shot;
    const long long pegTotal = static_cast<long long>(shot.pegScoreSum) * shot.pegsHit;
    const long long shotScore = pegTotal + shot.stylePoints;

    if (game.feverActive)
    {
        Finale& fin = game.finale;
        fin.scoreActive = true;
        fin.ballsStarted = false;
        fin.target = shotScore + game.bucketPoints;
        fin.shown = 0;
        fin.shownF = 0.0;
        fin.timer = 0.0f;
        fin.holdTimer = 0.0f;

        game.levelComplete = true;
        game.shotInProgress = false;
        popup = Popup();
        return;
    }

    popup.lines.clear();

    popup.lines.push_back({ FormatNumber(shot.pegScoreSum) + " X " + std::to_string(shot.pegsHit) + " PEGS", 3.0f, COL_YELLOW });
    popup.lines.push_back({ FormatNumber(pegTotal), 5.0f, COL_WHITE });
    if (shot.stylePoints > 0)
    {
        popup.lines.push_back({ "+" + FormatNumber(shot.stylePoints) + " STYLE POINTS", 3.0f, COL_GREEN });
        popup.lines.push_back({ "SHOT SCORE " + FormatNumber(shotScore), 2.0f, COL_WHITE });
    }

    const int freeBalls = shot.freeBallsEarned + (game.freeBall ? 1 : 0);
    if (freeBalls == 1)
        popup.lines.push_back({ "FREE BALL!", 3.0f, COL_CYAN });
    else if (freeBalls > 1)
        popup.lines.push_back({ std::to_string(freeBalls) + " FREE BALLS!", 3.0f, COL_CYAN });

    game.score += shotScore;

    MovePurplePeg(pegs);

    if (!game.freeBall) game.bucketStreak = 0;

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

static void DrawFloatingTexts(SDL_Renderer* r, const std::vector<FloatingText>& floaters)
{
    for (const FloatingText& f : floaters)
    {
        if (!f.isStyle)
        {
            const float scale = PEG_POPUP_SCALE;
            const float w = TextWidth(f.text, scale);
            float x = f.x + PEG_RADIUS + 3.0f;
            if (x + w > PLAY_RIGHT - 2.0f) x = f.x - PEG_RADIUS - 3.0f - w;
            DrawOutlinedText(r, f.text, x, f.y - 4.0f * scale, scale, f.colour);
        }
        else
        {
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

static void DrawBallMeter(SDL_Renderer* r, const GameState& game, bool live)
{
    const ShotStats& shot = game.shot;
    const SDL_Color stageColours[FREE_BALL_THRESHOLD_COUNT] = {
        { 60, 200, 80, 255 }, { 175, 70, 215, 255 }, { 250, 210, 50, 255 }
    };

    int stage = 0;
    float fill = 0.0f;
    bool lit = false;
    if (live)
    {
        stage = shot.freeBallsEarned;
        if (shot.freeBallFlash > 0.0f && stage > 0)
        {
            stage--;
            fill = 1.0f;
            lit = true;
        }
        else if (stage >= FREE_BALL_THRESHOLD_COUNT)
        {
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
    const float barH = cy - barTop;
    const float barX[2] = { 3.0f, BORDER_LEFT - 3.0f - FREE_BALL_BAR_WIDTH };

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

    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    DrawFilledCircle(r, cx, cy, circleR);
    if (lit) SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    else SDL_SetRenderDrawColor(r, 30, 33, 46, 255);
    DrawFilledCircle(r, cx, cy, circleR - 3.0f);

    if (lit) SDL_SetRenderDrawColor(r, 15, 15, 20, 255);
    else SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    DrawText(r, "FREE", cx - TextWidth("FREE", 1.0f) / 2.0f, cy - 9.0f, 1.0f);
    DrawText(r, "BALL", cx - TextWidth("BALL", 1.0f) / 2.0f, cy + 1.0f, 1.0f);

    const int n = std::max(game.ballBonusActive ? game.ballBonusShown : game.ballsLeft, 0);
    const int layoutN = game.ballBonusActive ? game.ballBonusLayoutCount : n;
    const float firstY = cy - circleR - 4.0f - STACK_BALL_RADIUS;
    const float topY = barTop + 6.0f + STACK_BALL_RADIUS;
    float pitch = STACK_BALL_PITCH;
    if (layoutN > 1) pitch = std::min(pitch, (firstY - topY) / static_cast<float>(layoutN - 1));
    SDL_SetRenderDrawColor(r, 230, 230, 240, 255);

    // 1. Draw static balls (skipping any ball slot that is actively dropping)
    for (int i = 0; i < n; ++i)
    {
        bool isDropping = false;
        for (const auto& drop : game.droppingBalls)
        {
            if (drop.targetIndex == i)
            {
                isDropping = true;
                break;
            }
        }
        if (!isDropping)
            DrawFilledCircle(r, cx, firstY - i * pitch, STACK_BALL_RADIUS);
    }

    // 2. Draw the dropping balls at their current falling position
    for (const auto& drop : game.droppingBalls)
    {
        DrawFilledCircle(r, cx, drop.y, STACK_BALL_RADIUS);
    }

    const std::string bonusText = "+" + FormatNumber(UNUSED_BALL_POINTS);
    for (const BallBonusPopup& b : game.ballBonusPopups)
    {
        const float scale = PEG_POPUP_SCALE;
        DrawOutlinedText(r, bonusText, PLAY_LEFT + 6.0f, firstY - b.index * pitch - 4.0f * scale, scale, COL_YELLOW);
    }
}

static int RouletteReturnChance(float t, bool returned)
{
    const float p = std::min(1.0f, t / ROULETTE_SPIN_TIME);
    const float omega = (returned ? 2.5f : 3.5f) * SDL_PI_F;
    const float swing = p * std::sin(omega * p);
    const int chance = static_cast<int>(std::lround(50.0f + 50.0f * swing));
    return std::max(0, std::min(chance, 100));
}

static void DrawRoulette(SDL_Renderer* r, const Roulette& roulette)
{
    const float t = roulette.timer;
    const int chance = RouletteReturnChance(t, roulette.returned);
    const bool done = (t >= ROULETTE_SPIN_TIME);

    const float fade = std::max(0.0f, std::min(1.0f, (ROULETTE_TIME - t) / POPUP_FADE_TIME));
    const Uint8 full = static_cast<Uint8>(255.0f * fade);
    const Uint8 dim = static_cast<Uint8>(110.0f * fade);
    const Uint8 returnAlpha = (done && !roulette.returned) ? dim : full;
    const Uint8 loseAlpha = (done && roulette.returned) ? dim : full;

    const float scale = 4.0f;
    const float cw = 8.0f * scale;
    const float leftExtent = cw / 2.0f + 3.0f * cw + cw / 2.0f + 6.0f * cw;
    const float rightExtent = cw / 2.0f + 3.0f * cw + cw / 2.0f + 4.0f * cw;
    const float x0 = LAUNCHER_X - (leftExtent + rightExtent) / 2.0f;
    const float slashX = x0 + leftExtent;
    const float y = (PLAY_TOP + PLAY_BOTTOM) / 2.0f - 50.0f;

    const std::string leftNum = std::to_string(chance);
    const std::string rightNum = std::to_string(100 - chance);

    DrawOutlinedText(r, "RETURN", x0, y, scale, COL_GREEN, returnAlpha);
    DrawOutlinedText(r, leftNum, slashX - cw / 2.0f - TextWidth(leftNum, scale), y, scale, COL_WHITE, returnAlpha);
    DrawOutlinedText(r, "/", slashX - cw / 2.0f, y, scale, COL_WHITE, full);
    DrawOutlinedText(r, rightNum, slashX + cw / 2.0f, y, scale, COL_WHITE, loseAlpha);
    DrawOutlinedText(r, "LOSE", slashX + cw / 2.0f + 3.0f * cw + cw / 2.0f, y, scale, COL_RED, loseAlpha);

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

    if (done)
    {
        const std::string result = roulette.returned ? "SHOT RETURNED!" : "SHOT LOST";
        const SDL_Color col = roulette.returned ? COL_CYAN : SDL_Color{ 200, 200, 210, 255 };
        DrawOutlinedText(r, result, LAUNCHER_X - TextWidth(result, 3.0f) / 2.0f, barY + barH + 22.0f, 3.0f, col, full);
    }
}

static SDL_FRect CameraSource(const GameState& game)
{
    const float z = game.camZoom;
    const float factor = 1.0f + (SLOWMO_ZOOM - 1.0f) * z;
    const float centreX = (PLAY_LEFT + PLAY_RIGHT) / 2.0f;
    const float centreY = (PLAY_TOP + PLAY_BOTTOM) / 2.0f;

    const float fx = centreX + (game.camFocusX - centreX) * z;
    const float fy = centreY + (game.camFocusY - centreY) * z;

    const float w = WINDOW_W / factor;
    const float h = WINDOW_H / factor;

    const float s = std::min(1.0f, z / 0.1f);
    const float loX = PLAY_LEFT * s;
    const float hiX = std::max(loX, WINDOW_W - BORDER_RIGHT * s - w);
    const float loY = PLAY_TOP * s;
    const float hiY = std::max(loY, WINDOW_H - BORDER_BOTTOM * s - h);

    return { std::max(loX, std::min(fx - w / 2.0f, hiX)), std::max(loY, std::min(fy - h / 2.0f, hiY)), w, h };
}

static void DrawExtremeFeverText(SDL_Renderer* r, const GameState& game)
{
    const float u = std::max(0.0f, std::min(1.0f, game.feverZoomTimer / EXTREME_FEVER_ZOOM_OUT_TIME));
    const float ease = u * u * (3.0f - 2.0f * u);

    // If ultra fever, start slightly smaller so "ULTRA EXTREME FEVER!" fits on screen
    const float startScale = game.ultraFever ? (EXTREME_FEVER_TEXT_SCALE * 0.75f) : EXTREME_FEVER_TEXT_SCALE;
    const float endScale = game.ultraFever ? (EXTREME_FEVER_TEXT_END_SCALE * 0.75f) : EXTREME_FEVER_TEXT_END_SCALE;
    const float scale = startScale + (endScale - startScale) * ease;
    const float fade = std::max(0.0f, std::min(1.0f, (1.0f - u) / 0.5f));

    // Choose text based on whether it is Ultra Extreme Fever
    const std::string text = game.ultraFever ? "ULTRA EXTREME FEVER!" : "EXTREME FEVER!";
    const float y = (PLAY_TOP + PLAY_BOTTOM) / 2.0f - 4.0f * scale;
    DrawOutlinedText(r, text, LAUNCHER_X - TextWidth(text, scale) / 2.0f, y, scale, COL_ORANGE, static_cast<Uint8>(255.0f * fade));
}

static void DrawFeverScore(SDL_Renderer* r, const GameState& game)
{
    const Finale& fin = game.finale;
    const Uint8 alpha = static_cast<Uint8>(255.0f * std::max(0.0f, std::min(1.0f, fin.timer / 0.2f)));

    const std::string title = "FEVER SCORE";
    const std::string number = FormatNumber(fin.shown);
    const float titleScale = FEVER_SCORE_TITLE_SCALE;
    const float numberScale = std::min(FEVER_SCORE_NUMBER_SCALE, 640.0f / (8.0f * static_cast<float>(std::max<size_t>(number.size(), 1))));

    const float top = (PLAY_TOP + PLAY_BOTTOM) / 2.0f - 70.0f;
    if (game.ultraFever)
    {
        const std::string ultra = "ULTRA EXTREME FEVER!";
        DrawOutlinedText(r, ultra, LAUNCHER_X - TextWidth(ultra, 3.0f) / 2.0f, top - 36.0f, 3.0f, COL_ORANGE, alpha);
    }
    DrawOutlinedText(r, title, LAUNCHER_X - TextWidth(title, titleScale) / 2.0f, top, titleScale, { 210, 230, 255, 255 }, alpha);
    DrawOutlinedText(r, number, LAUNCHER_X - TextWidth(number, numberScale) / 2.0f, top + 8.0f * titleScale + 16.0f, numberScale, { 255, 170, 30, 255 }, alpha);
}

static void DrawHudBox(SDL_Renderer* r, const SDL_FRect& box, const std::string& text)
{
    SDL_SetRenderDrawColor(r, 45, 60, 100, 255);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 200, 200, 210, 255);
    SDL_RenderRect(r, &box);

    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    DrawText(r, text, box.x + (box.w - TextWidth(text, 2.0f)) / 2.0f, box.y + (box.h - 16.0f) / 2.0f, 2.0f);
}

static void DrawHudBoxScaled(SDL_Renderer* r, const SDL_FRect& box, const std::string& text, float scale)
{
    SDL_SetRenderDrawColor(r, 45, 60, 100, 255);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 200, 200, 210, 255);
    SDL_RenderRect(r, &box);

    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    DrawText(r, text, box.x + (box.w - TextWidth(text, scale)) / 2.0f, box.y + (box.h - 8.0f * scale) / 2.0f, scale);
}

static void DrawTopHud(SDL_Renderer* r, int level, long long currentScore, const RunState& run)
{
    const float margin = 8.0f;
    const float boxY = 8.0f;
    const float boxH = 34.0f;
    const float padding = 12.0f;

    const std::string levelText = "LEVEL " + std::to_string(level);
    const std::string scoreText = "SCORE " + FormatNumber(currentScore);

    const float levelW = std::max(TextWidth("LEVEL 99", 2.0f), TextWidth(levelText, 2.0f)) + 2.0f * padding;
    const float scoreW = std::max(TextWidth("SCORE 0,000,000", 2.0f), TextWidth(scoreText, 2.0f)) + 2.0f * padding;

    DrawHudBox(r, { margin, boxY, levelW, boxH }, levelText);
    DrawHudBox(r, { WINDOW_W - margin - scoreW, boxY, scoreW, boxH }, scoreText);

    // Centered in the open gap between LEVEL and SCORE using scale 1.5 so it never overlaps
    if (run.active && run.completedCount > 0)
    {
        const long long total = run.CompletedTotal() + currentScore;
        const std::string totalText = "TOTAL SCORE " + FormatNumber(total);
        const float totalW = TextWidth(totalText, 1.5f) + 20.0f;

        const float gapLeft = margin + levelW + 8.0f;
        const float gapRight = WINDOW_W - margin - scoreW - 8.0f;
        const float totalX = gapLeft + ((gapRight - gapLeft) - totalW) / 2.0f;

        DrawHudBoxScaled(r, { totalX, boxY, totalW, boxH }, totalText, 1.5f);
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Peggle Basic", WINDOW_W, WINDOW_H, 0, &window, &renderer))
    {
        SDL_Log("Window/renderer creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    AudioInit();

    SDL_Texture* worldTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, WINDOW_W, WINDOW_H);
    if (worldTex) SDL_SetTextureScaleMode(worldTex, SDL_SCALEMODE_LINEAR);
    else SDL_Log("Zoom texture failed: %s", SDL_GetError());

    // Load Ribbon image (BMP)
    SDL_Surface* ribbonSurf = SDL_LoadBMP("ribbon.bmp");
    SDL_Texture* ribbonTex = ribbonSurf ? SDL_CreateTextureFromSurface(renderer, ribbonSurf) : nullptr;
    if (ribbonSurf) SDL_DestroySurface(ribbonSurf);

    Ball  ball;
    std::vector<Peg> pegs;
    std::vector<int> hitOrder;
    float removeTimer = 0.0f;
    int   removeCount = 0;
    GameState game;
    Popup popup;

    bool running = true;
    bool fastForward = false;
    Screen screen = Screen::MainMenu;
    int  currentLevel = 1;
    bool levelSelectOpen = false;
    bool settingsOpen = false;
    bool styleShotsOpen = false;
    RunState activeRun = LoadRunFromFile();
    bool runPromptOpen = false;
    bool draggingSlider = false;
    std::string nameInput;
    bool scoreSaved = false;
    std::string chalNameInput;
    bool chalScoreSaved = false;
    int activeNameTarget = 0; // 0 = Level Name, 1 = Run Name
    std::vector<ScoreEntry> scores = LoadScores();

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
            ResetLevelMusic();
            removeTimer = 0.0f;
            removeCount = 0;
            nameInput.clear();
            scoreSaved = false;
            chalNameInput.clear();      // <--- ADD THIS
            chalScoreSaved = false;     // <--- ADD THIS
            activeNameTarget = 0;
            levelSelectOpen = false;
            settingsOpen = false;
            styleShotsOpen = false;
            runPromptOpen = false;
            draggingSlider = false;
            screen = Screen::Playing;
        };

    auto goToMenu = [&]()
        {
            screen = Screen::MainMenu;
            levelSelectOpen = false;
            settingsOpen = false;
            styleShotsOpen = false;
            runPromptOpen = false;
            draggingSlider = false;
            scores = LoadScores();
        };

    auto saveScore = [&]()
        {
            if (nameInput.size() != 3 || scoreSaved) return;
            ScoreEntry entry;
            entry.level = currentLevel;
            entry.name = nameInput;
            entry.score = game.score;
            entry.isUltra = game.ultraFever;
            if (SaveScore(entry))
            {
                scoreSaved = true;
                scores = LoadScores();
            }
        };

    auto saveChallengeScore = [&]()
        {
            if (chalNameInput.size() != 3 || chalScoreSaved) return;
            if (!activeRun.active || currentLevel != LevelCount()) return;

            activeRun.levelScores[currentLevel - 1] = game.score;
            activeRun.completedCount = LevelCount();
            const long long runGrandTotal = activeRun.CompletedTotal();

            ScoreEntry chalEntry;
            chalEntry.level = activeRun.noFail ? CHALLENGE_10_NO_FAIL : CHALLENGE_10_REGULAR;
            chalEntry.name = chalNameInput;
            chalEntry.score = runGrandTotal;
            chalEntry.isUltra = false;
            if (SaveScore(chalEntry))
            {
                chalScoreSaved = true;
                scores = LoadScores();
            }
        };

    auto doAction = [&](const Button& b)
        {
            if (b.action != Action::None) PlaySfx(Sfx::ButtonClick);
            switch (b.action)
            {
            case Action::Play:
                if (activeRun.active && activeRun.currentLevel > 1)
                {
                    runPromptOpen = true;
                }
                else
                {
                    activeRun = RunState();
                    activeRun.active = true;
                    activeRun.currentLevel = 1;
                    activeRun.noFail = true;
                    activeRun.completedCount = 0;
                    SaveRunToFile(activeRun);
                    startLevel(1);
                }
                break;

            case Action::ContinueRun:
                runPromptOpen = false;
                // Exiting mid-level counts as resetting the level -> lose No Fail
                activeRun.noFail = false;
                SaveRunToFile(activeRun);
                startLevel(activeRun.currentLevel);
                break;

            case Action::NewRun:
                runPromptOpen = false;
                activeRun = RunState();
                activeRun.active = true;
                activeRun.currentLevel = 1;
                activeRun.noFail = true;
                activeRun.completedCount = 0;
                SaveRunToFile(activeRun);
                startLevel(1);
                break;

            case Action::CancelRunPopup:
                runPromptOpen = false;
                break;

            case Action::Resume: screen = Screen::Playing; break;
            case Action::Quit: running = false; break;
            case Action::OpenLevelSelect: levelSelectOpen = true; break;
            case Action::CloseLevelSelect: levelSelectOpen = false; break;
            case Action::OpenSettings: settingsOpen = true; break;
            case Action::CloseSettings: settingsOpen = false; break;
            case Action::OpenStyleShots: styleShotsOpen = true; break;
            case Action::CloseStyleShots: styleShotsOpen = false; break;

            case Action::GoToLevel:
                if (b.level == 1)
                {
                    // Selecting Level 1 starts a fresh 10-run
                    activeRun = RunState();
                    activeRun.active = true;
                    activeRun.currentLevel = 1;
                    activeRun.noFail = true;
                    activeRun.completedCount = 0;
                    SaveRunToFile(activeRun);
                }
                else
                {
                    // Jumping to other levels abandons any active 10-run
                    activeRun.active = false;
                    SaveRunToFile(activeRun);
                }
                startLevel(b.level);
                break;

            case Action::Restart:
                if (activeRun.active)
                {
                    activeRun.noFail = false; // Using restart/retry forfeits No Fail
                    SaveRunToFile(activeRun);
                }
                startLevel(currentLevel);
                break;

            case Action::MainMenu:
                if (activeRun.active)
                {
                    activeRun.noFail = false; // Leaving mid-level forfeits No Fail
                    SaveRunToFile(activeRun);
                }
                goToMenu();
                break;

            case Action::NextLevel:
                if (activeRun.active)
                {
                    activeRun.levelScores[currentLevel - 1] = game.score;
                    activeRun.completedCount = std::max(activeRun.completedCount, currentLevel);
                    activeRun.currentLevel = currentLevel + 1;
                    SaveRunToFile(activeRun);
                }
                startLevel(currentLevel + 1);
                break;

            case Action::SaveScore: saveScore(); break;
            case Action::SaveChallengeScore: saveChallengeScore(); break;
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
                    if (runPromptOpen) runPromptOpen = false;
                    else if (styleShotsOpen) styleShotsOpen = false;
                    else if (settingsOpen) settingsOpen = false;
                    else if (levelSelectOpen) levelSelectOpen = false;
                    else if (screen == Screen::Playing) screen = Screen::Paused;
                    else if (screen == Screen::Paused) screen = Screen::Playing;
                }
                else if (screen == Screen::LevelClear && !levelSelectOpen && !settingsOpen && !styleShotsOpen)
                {
                    const bool isLevel10Run = (activeRun.active && currentLevel == LevelCount());

                    // Tab switches between typing Level name and Run name
                    if (e.key.key == SDLK_TAB && isLevel10Run)
                    {
                        activeNameTarget = (activeNameTarget == 0) ? 1 : 0;
                    }
                    else
                    {
                        std::string& targetStr = (activeNameTarget == 0 || !isLevel10Run) ? nameInput : chalNameInput;
                        const bool isSaved = (activeNameTarget == 0 || !isLevel10Run) ? scoreSaved : chalScoreSaved;

                        if (!isSaved)
                        {
                            if (e.key.key == SDLK_BACKSPACE && !targetStr.empty())
                                targetStr.pop_back();
                            else if (e.key.key >= SDLK_A && e.key.key <= SDLK_Z && targetStr.size() < 3 && !e.key.repeat)
                                targetStr += static_cast<char>('A' + (e.key.key - SDLK_A));
                            else if (e.key.key == SDLK_RETURN)
                            {
                                if (activeNameTarget == 0 || !isLevel10Run) saveScore();
                                else saveChallengeScore();
                            }
                        }
                    }
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                mouseX = e.motion.x;
                mouseY = e.motion.y;
                if (settingsOpen && draggingSlider)
                {
                    const SDL_FRect bar = VolumeSliderBar();
                    const float fraction = (mouseX - bar.x) / bar.w;
                    SetAudioVolume(fraction * 2.0f); // 0.0f -> 2.0f (200%)
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_RIGHT)
                    fastForward = false;

                if (e.button.button == SDL_BUTTON_LEFT)
                {
                    if (draggingSlider)
                    {
                        draggingSlider = false;
                        SaveAudioSettings();
                    }
                }
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

                if (runPromptOpen)
                {
                    for (const Button& b : GetRunPromptButtons())
                    {
                        if (PointInRect(mouseX, mouseY, b.rect))
                        {
                            doAction(b);
                            break;
                        }
                    }
                    break;
                }

                if (settingsOpen)
                {
                    const SDL_FRect bar = VolumeSliderBar();
                    const SDL_FRect sliderHit = { bar.x - 10.0f, bar.y - 12.0f, bar.w + 20.0f, bar.h + 24.0f };
                    if (PointInRect(mouseX, mouseY, sliderHit))
                    {
                        draggingSlider = true;
                        const float fraction = (mouseX - bar.x) / bar.w;
                        SetAudioVolume(fraction * 2.0f); // 0.0f -> 2.0f (200%)
                    }
                    else
                    {
                        for (const Button& b : GetSettingsButtons())
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

                if (styleShotsOpen)
                {
                    for (const Button& b : GetStyleShotsButtons())
                    {
                        if (PointInRect(mouseX, mouseY, b.rect))
                        {
                            doAction(b);
                            break;
                        }
                    }
                    break;
                }

                if (levelSelectOpen)
                {
                    for (const Button& b : GetLevelSelectButtons(LevelCount()))
                    {
                        if (PointInRect(mouseX, mouseY, b.rect))
                        {
                            doAction(b);
                            break;
                        }
                    }
                    break;
                }

                if (screen == Screen::Playing)
                {
                    if (!ball.active && hitOrder.empty() && game.ballsLeft > 0 && !game.levelComplete && !game.feverActive && !game.roulette.active)
                    {
                        float dirX, dirY;
                        GetAimDirection(mouseX, mouseY, dirX, dirY);
                        FireBall(ball, dirX, dirY);
                        PlaySfx(Sfx::Shoot);
                        game.shotInProgress = true;
                        game.ballsLeft--;
                        game.shot = ShotStats();
                        game.shot.orangeAtStart = game.orangeHit;
                        game.freeBall = false;
                        game.slowmoTriggered = false;

                        // Reset anti-stuck detector
                        game.stuckTimer = 0.0f;
                        game.stuckCheckX = ball.x;
                        game.stuckCheckY = ball.y;
                    }
                }
                if (screen == Screen::LevelClear && activeRun.active && currentLevel == LevelCount())
                {
                    const SDL_FRect row0 = { LAUNCHER_X - 150.0f, 185.0f, 300.0f, 20.0f };
                    const SDL_FRect row1 = { LAUNCHER_X - 150.0f, 205.0f, 300.0f, 20.0f };
                    if (PointInRect(mouseX, mouseY, row0)) activeNameTarget = 0;
                    if (PointInRect(mouseX, mouseY, row1)) activeNameTarget = 1;
                }
                else
                {
                    const std::vector<Button> buttons = GetScreenButtons(screen, currentLevel, LevelCount(), nameInput.size() == 3, scoreSaved);
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
        if (screen == Screen::Paused) frameDt = 0.0f;
        else if (fastForward && screen == Screen::Playing && !game.shotInProgress && !ball.active) frameDt *= GAME_SPEED_FAST;

        // ---- Music ----
        Music music = Music::Menu;
        if (screen == Screen::Playing)
            music = game.feverActive ? Music::Fever : Music::Level;
        else if (screen == Screen::LevelClear)
            music = Music::Fever;
        SetMusic(music);
        SetRouletteSpin(screen == Screen::Playing && game.roulette.active && !game.roulette.revealed);

        // ---- Main menu ----
        if (screen == Screen::MainMenu)
        {
            SDL_SetRenderDrawColor(renderer, 20, 24, 40, 255);
            SDL_RenderClear(renderer);

            const std::string title = "PEGGLE BASIC";
            SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
            DrawText(renderer, title, LAUNCHER_X - TextWidth(title, 4.0f) / 2.0f, 40.0f, 4.0f);

            const float hoverX = (levelSelectOpen || settingsOpen || styleShotsOpen || runPromptOpen) ? -1.0f : mouseX;
            const float hoverY = (levelSelectOpen || settingsOpen || styleShotsOpen || runPromptOpen) ? -1.0f : mouseY;
            DrawButtons(renderer, GetScreenButtons(screen, currentLevel, LevelCount(), false, false), hoverX, hoverY);

            // Helper text below the main menu buttons
            SDL_SetRenderDrawColor(renderer, COL_WHITE.r, COL_WHITE.g, COL_WHITE.b, 255);
            const float hintScale = 1.8f;

            auto DrawBoldText = [&](const std::string& text, float x, float y) {
                DrawText(renderer, text, x, y, hintScale);
                DrawText(renderer, text, x + 1.0f, y, hintScale);
                };

            DrawBoldText("Press Left Mouse", 65.0f, 390.0f);
            DrawBoldText("To Shoot", 65.0f, 418.0f);
            DrawBoldText("Hold Right Mouse", 65.0f, 457.0f);
            DrawBoldText("To Speed Up Bucket", 65.0f, 485.0f);
            DrawBoldText("Press ESC", 65.0f, 524.0f);
            DrawBoldText("To Pause", 65.0f, 552.0f);

            // Leaderboard Header
            const float lbCenterX = 395.0f + 375.0f / 2.0f;
            SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
            DrawText(renderer, "LEADERBOARD", lbCenterX - TextWidth("LEADERBOARD", 2.0f) / 2.0f, 120.0f, 2.0f);

            struct LeaderboardRow {
                int id;
                std::string label;
                bool isChallenge;
            };

            std::vector<LeaderboardRow> lbRows = {
                { CHALLENGE_10_NO_FAIL, "10 LVL NO FAIL", true },
                { CHALLENGE_10_REGULAR, "10 LEVELS",       true },
                { CHALLENGE_3,          "CHALLENGE 3",     true }
            };
            for (int lvl = 1; lvl <= LevelCount(); ++lvl)
                lbRows.push_back({ lvl, "LEVEL " + std::to_string(lvl), false });

            int hoveredLevel = 0;
            std::string hoveredTitle = "";

            const float startY = 150.0f; // Shifted down to match the new header position
            const float rowHeight = 22.0f;
            const float challengeGap = 10.0f;

            for (size_t i = 0; i < lbRows.size(); ++i)
            {
                const auto& item = lbRows[i];
                float y = startY + i * rowHeight;
                if (!item.isChallenge)
                    y += challengeGap; // Gap separating challenges from regular levels

                const SDL_FRect rowRect = { 395.0f, y - 2.0f, 375.0f, 20.0f };
                const std::vector<ScoreEntry> levelScores = ScoresForLevel(scores, item.id);

                if (!levelSelectOpen && !settingsOpen && !styleShotsOpen && !runPromptOpen && PointInRect(mouseX, mouseY, rowRect))
                {
                    hoveredLevel = item.id;
                    hoveredTitle = item.label;
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(renderer, 80, 110, 170, 120);
                    SDL_RenderFillRect(renderer, &rowRect);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                }

                if (item.isChallenge) SDL_SetRenderDrawColor(renderer, 255, 200, 70, 255);
                else                  SDL_SetRenderDrawColor(renderer, 160, 160, 170, 255);
                DrawText(renderer, item.label, 400.0f, y, 1.5f);

                if (item.id == CHALLENGE_3)
                {
                    DrawText(renderer, "LOCKED", 760.0f - TextWidth("LOCKED", 1.5f), y, 1.5f);
                }
                else if (levelScores.empty())
                {
                    DrawText(renderer, "NO SCORES", 760.0f - TextWidth("NO SCORES", 1.5f), y, 1.5f);
                }
                else
                {
                    const std::string scoreText = FormatNumber(levelScores[0].score);
                    const float scoreX = 760.0f - TextWidth(scoreText, 1.5f);

                    if (levelScores[0].isUltra && ribbonTex)
                    {
                        const SDL_FRect miniRibbon = { scoreX - 16.0f, y - 1.0f, 12.0f, 18.0f };
                        SDL_RenderTexture(renderer, ribbonTex, nullptr, &miniRibbon);
                    }
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                    DrawText(renderer, levelScores[0].name, 585.0f, y, 1.5f);
                    DrawText(renderer, scoreText, scoreX, y, 1.5f);
                }
            }

            if (hoveredLevel > 0 && hoveredLevel != CHALLENGE_3)
            {
                const std::vector<ScoreEntry> levelScores = ScoresForLevel(scores, hoveredLevel);
                if (!levelScores.empty())
                {
                    const float h = 44.0f + levelScores.size() * 22.0f;
                    const SDL_FRect panel = { 395.0f, std::max(60.0f, std::min(mouseY - 20.0f, WINDOW_H - 10.0f - h)), 375.0f, h };
                    DrawPanel(renderer, panel);

                    SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
                    DrawText(renderer, hoveredTitle + " BEST SCORES", panel.x + 10.0f, panel.y + 10.0f, 1.8f);
                    for (size_t i = 0; i < levelScores.size(); ++i)
                    {
                        const float y = panel.y + 36.0f + i * 22.0f;
                        const std::string scoreText = FormatNumber(levelScores[i].score);
                        const float scoreX = panel.x + panel.w - 10.0f - TextWidth(scoreText, 1.5f);

                        if (levelScores[i].isUltra && ribbonTex)
                        {
                            const SDL_FRect miniRibbon = { scoreX - 16.0f, y - 1.0f, 12.0f, 18.0f };
                            SDL_RenderTexture(renderer, ribbonTex, nullptr, &miniRibbon);
                        }

                        SDL_SetRenderDrawColor(renderer, 160, 160, 170, 255);
                        DrawText(renderer, std::to_string(i + 1) + ".", panel.x + 10.0f, y, 1.5f);
                        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                        DrawText(renderer, levelScores[i].name, panel.x + 60.0f, y, 1.5f);
                        DrawText(renderer, scoreText, scoreX, y, 1.5f);
                    }
                }
            }

            if (runPromptOpen)
                DrawRunPrompt(renderer, activeRun, mouseX, mouseY);
            else if (levelSelectOpen)
                DrawLevelSelect(renderer, LevelCount(), mouseX, mouseY);
            else if (settingsOpen)
                DrawSettings(renderer, mouseX, mouseY);
            else if (styleShotsOpen)
                DrawStyleShots(renderer, mouseX, mouseY);

            SDL_RenderPresent(renderer);
            continue;
        }

        // ---- Pop-up timers ----
        if (game.shot.freeBallFlash > 0.0f)
            game.shot.freeBallFlash -= frameDt;
        for (FloatingText& f : game.floaters)
            f.timer -= frameDt;
        game.floaters.erase(std::remove_if(game.floaters.begin(), game.floaters.end(),
            [](const FloatingText& f) { return f.timer <= 0.0f; }),
            game.floaters.end());

        for (BallBonusPopup& b : game.ballBonusPopups)
            b.timer -= frameDt;
        game.ballBonusPopups.erase(std::remove_if(game.ballBonusPopups.begin(), game.ballBonusPopups.end(),
            [](const BallBonusPopup& b) { return b.timer <= 0.0f; }),
            game.ballBonusPopups.end());

        // Update peg pop particles
        for (auto& fx : game.pegPops)
        {
            fx.timer += frameDt;
            for (auto& p : fx.particles)
            {
                p.x += p.vx * frameDt;
                p.y += p.vy * frameDt;
                p.vy += 300.0f * frameDt;
            }
        }
        game.pegPops.erase(std::remove_if(game.pegPops.begin(), game.pegPops.end(),
            [](const PegPopEffect& fx) { return fx.timer >= fx.duration; }),
            game.pegPops.end());

        // Animate dropping free balls down the chute
        for (auto& drop : game.droppingBalls)
        {
            const float targetY = GetStackBallY(drop.targetIndex, game.ballsLeft);
            drop.vy += 2400.0f * frameDt;
            drop.y += drop.vy * frameDt;

            if (drop.y >= targetY)
            {
                drop.y = targetY;
                if (drop.bouncesLeft > 0)
                {
                    drop.vy = -drop.vy * 0.25f;
                    drop.bouncesLeft--;
                }
                else
                {
                    drop.settled = true;
                }
            }
        }
        game.droppingBalls.erase(std::remove_if(game.droppingBalls.begin(), game.droppingBalls.end(),
            [](const DroppingBall& d) { return d.settled; }),
            game.droppingBalls.end());

        if (game.finale.scoreActive && !game.finale.ballsStarted && game.finale.shown >= game.finale.target)
        {
            game.finale.ballsStarted = true;
            if (game.ballsLeft > 0)
            {
                game.ballBonusActive = true;
                game.ballBonusShown = game.ballsLeft;
                game.ballBonusLayoutCount = game.ballsLeft;
                game.ballBonusTimer = 0.0f;
            }
        }
        if (game.ballBonusActive)
        {
            const float interval = std::min(BALL_BONUS_MAX_DELAY, BALL_BONUS_TOTAL_TIME / static_cast<float>(game.ballBonusLayoutCount));
            game.ballBonusTimer += frameDt;
            while (game.ballBonusTimer >= interval && game.ballBonusShown > 0)
            {
                game.ballBonusShown--;
                game.ballBonusPopups.push_back({ game.ballBonusShown, BALL_BONUS_POPUP_TIME });
                PlaySfx(Sfx::PegRemove);
                game.finale.target += UNUSED_BALL_POINTS;
                game.ballBonusTimer -= interval;
            }
            if (game.ballBonusShown <= 0)
            {
                game.ballBonusActive = false;
                game.ballsLeft = 0;
            }
        }

        if (game.finale.scoreActive)
        {
            Finale& fin = game.finale;
            fin.timer += frameDt;

            const double speed = std::max(static_cast<double>(fin.target) / FEVER_SCORE_COUNT_TIME, 100000.0);
            fin.shownF = std::min(static_cast<double>(fin.target), fin.shownF + speed * frameDt);
            const long long newShown = static_cast<long long>(fin.shownF);
            game.score += newShown - fin.shown;
            fin.shown = newShown;

            if (fin.ballsStarted && !game.ballBonusActive && fin.shown >= fin.target)
            {
                fin.holdTimer += frameDt;
                if (fin.holdTimer >= FEVER_SCORE_HOLD_TIME) fin.scoreActive = false;
            }
        }

        if (game.roulette.active)
        {
            Roulette& rl = game.roulette;
            rl.timer += frameDt;
            if (!rl.revealed && rl.timer >= ROULETTE_SPIN_TIME)
            {
                rl.revealed = true;
                SetRouletteSpin(false);
                if (rl.returned)
                {
                    AwardFreeBall(game);
                    PlaySfx(Sfx::RouletteWin);
                    PlaySfx(Sfx::FreeBall);
                }
                else
                {
                    PlaySfx(Sfx::RouletteLose);
                }
            }
            if (rl.timer >= ROULETTE_TIME) rl.active = false;
        }

        // ---- Camera ----
        if (ball.active)
        {
            game.camFocusX = ball.x;
            game.camFocusY = ball.y;
        }
        if (game.feverZoomOut)
        {
            game.feverZoomTimer += frameDt;
            const float u = std::min(1.0f, game.feverZoomTimer / EXTREME_FEVER_ZOOM_OUT_TIME);
            game.camZoom = game.feverZoomStart * (1.0f - u * u * (3.0f - 2.0f * u));
            if (u >= 1.0f)
            {
                game.feverZoomOut = false;
                game.camZoom = 0.0f;
            }
        }
        else if (ball.active && !game.feverActive && game.approachingLastOrange)
            game.camZoom += (1.0f - game.camZoom) * std::min(1.0f, frameDt * CAMERA_ZOOM_SPEED);
        else
            game.camZoom = std::max(0.0f, game.camZoom - frameDt * CAMERA_ZOOM_SPEED * 0.5f);

        if (game.finale.fireworksActive)
            game.finale.fireworksTimer += frameDt;

        // ---- Update (fixed timestep) ----
        if (ball.active)
        {
            game.timeScale = (!game.feverActive && game.approachingLastOrange) ? FEVER_SLOWMO : 1.0f;
        }
        else
        {
            game.timeScale = 1.0f;
            game.approachingLastOrange = false;
        }
        accumulator += frameDt * game.timeScale;
        while (accumulator >= PHYSICS_STEP)
        {
            UpdateBucket(game, PHYSICS_STEP);
            UpdateBall(ball, pegs, hitOrder, game, PHYSICS_STEP);
            accumulator -= PHYSICS_STEP;
        }

        if (!ball.active && !hitOrder.empty())
        {
            if (removeCount == 0) removeCount = static_cast<int>(hitOrder.size());
            const float removeTotal = game.finale.fireworksActive ? FEVER_FIREWORKS_TIME : PEG_REMOVE_TOTAL_TIME;
            const float removeInterval = std::min(PEG_REMOVE_MAX_DELAY, removeTotal / removeCount);

            removeTimer += frameDt;
            while (removeTimer >= removeInterval && !hitOrder.empty())
            {
                SpawnPegPop(game, pegs[hitOrder.front()]);
                pegs[hitOrder.front()].removed = true;
                PlaySfx(Sfx::PegRemove);
                hitOrder.erase(hitOrder.begin());
                removeTimer -= removeInterval;
            }
        }
        else
        {
            removeTimer = 0.0f;
            removeCount = 0;
        }

        if (game.shotInProgress && !ball.active && hitOrder.empty()
            && (!game.feverActive || game.finale.fireworksTimer >= FEVER_FIREWORKS_TIME))
            FinishShot(game, pegs, popup);

        if (popup.timer > 0.0f)
            popup.timer -= frameDt;

        if (screen == Screen::Playing && !game.shotInProgress && !ball.active && hitOrder.empty() && popup.timer <= 0.0f && !game.roulette.active
            && !game.ballBonusActive && game.ballBonusPopups.empty() && !game.finale.scoreActive)
        {
            if (game.levelComplete)
            {
                // If 100% pegs were cleared, show the 3-second ribbon celebration first!
                if (game.ultraFever && !game.ribbonDone)
                {
                    if (!game.ribbonActive)
                    {
                        game.ribbonActive = true;
                        game.ribbonTimer = 3.0f;
                        PlaySfx(Sfx::Applause);
                    }
                }
                else
                {
                    screen = Screen::LevelClear;
                }
            }
            else if (game.ballsLeft <= 0)
            {
                screen = Screen::Fail;
            }
        }

        // Count down the ribbon timer
        if (game.ribbonActive)
        {
            game.ribbonTimer -= frameDt;
            if (game.ribbonTimer <= 0.0f)
            {
                game.ribbonActive = false;
                game.ribbonDone = true;
                screen = Screen::LevelClear;
            }
        }

        // ---- Render ----
        const bool zoomed = (worldTex != nullptr) && game.camZoom > 0.001f;
        if (zoomed) SDL_SetRenderTarget(renderer, worldTex);
        SDL_SetRenderDrawColor(renderer, 20, 24, 40, 255);
        SDL_RenderClear(renderer);

        for (const Peg& peg : pegs)
        {
            if (peg.removed) continue;

            Uint8 r = 50, g = 110, b = 220;
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

        if (game.feverActive)
        {
            SDL_SetRenderDrawColor(renderer, 230, 190, 60, 255);
            for (int k = 0; k <= FEVER_BUCKET_COUNT; ++k)
                DrawFilledCircle(renderer, FeverCircleX(k), FEVER_CIRCLE_Y, FEVER_CIRCLE_RADIUS);

            const bool ultra = game.finale.fireworksActive ? game.ultraFever : AllPegsCleared(pegs);
            for (int k = 0; k < FEVER_BUCKET_COUNT; ++k)
            {
                const std::string label = FormatNumber(ultra ? ULTRA_BUCKET_POINTS : FEVER_BUCKET_POINTS[k]);
                const float midX = (FeverCircleX(k) + FeverCircleX(k + 1)) / 2.0f;
                DrawText(renderer, label, midX - TextWidth(label, 1.0f) / 2.0f, PLAY_BOTTOM - 40.0f, 1.0f);
            }
        }

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

        SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
        DrawFilledCircle(renderer, LAUNCHER_X, LAUNCHER_Y, 14.0f);

        if (!ball.active && hitOrder.empty() && game.ballsLeft > 0 && !game.levelComplete && !game.feverActive && !game.roulette.active)
        {
            float dirX, dirY;
            GetAimDirection(mouseX, mouseY, dirX, dirY);
            DrawAimGuide(renderer, pegs, dirX, dirY);
        }

        if (ball.active)
        {
            SDL_SetRenderDrawColor(renderer, 230, 230, 240, 255);
            DrawFilledCircle(renderer, ball.x, ball.y, BALL_RADIUS);
        }

        DrawPegPops(renderer, game.pegPops);
        DrawFloatingTexts(renderer, game.floaters);

        if (zoomed)
        {
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            const SDL_FRect view = CameraSource(game);
            SDL_RenderTexture(renderer, worldTex, &view, nullptr);
        }

        // Draw 3-Second Ultra Extreme Fever Ribbon Celebration
        if (game.ribbonActive)
        {
            DrawDim(renderer, 190);

            const float rw = 190.0f;
            const float rh = 300.0f;
            const SDL_FRect ribbonRect = { LAUNCHER_X - rw / 2.0f, (WINDOW_H - rh) / 2.0f - 20.0f, rw, rh };

            if (ribbonTex)
            {
                SDL_RenderTexture(renderer, ribbonTex, nullptr, &ribbonRect);
            }
            else
            {
                const std::string badge = "[ 100% ALL PEGS CLEARED! ]";
                SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
                DrawText(renderer, badge, LAUNCHER_X - TextWidth(badge, 2.5f) / 2.0f, ribbonRect.y + 120.0f, 2.5f);
            }

            const std::string banner = "ULTRA EXTREME FEVER!";
            SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
            DrawText(renderer, banner, LAUNCHER_X - TextWidth(banner, 2.0f) / 2.0f, ribbonRect.y + rh + 15.0f, 2.0f);
        }

        // Borders
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        const SDL_FRect borders[4] = {
            { 0.0f,        0.0f,        static_cast<float>(WINDOW_W), BORDER_TOP },
            { 0.0f,        PLAY_BOTTOM, static_cast<float>(WINDOW_W), BORDER_BOTTOM },
            { 0.0f,        PLAY_TOP,    BORDER_LEFT,                  PLAY_BOTTOM - PLAY_TOP },
            { PLAY_RIGHT,  PLAY_TOP,    BORDER_RIGHT,                 PLAY_BOTTOM - PLAY_TOP },
        };
        SDL_RenderFillRects(renderer, borders, 4);

        DrawTopHud(renderer, currentLevel, game.score, activeRun);
        DrawBallMeter(renderer, game, game.shotInProgress || popup.timer > 0.0f || game.finale.scoreActive);

        // Fever meter
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

        const struct { int hits; const char* label; } multipliers[4] = { { 10, "x2" }, { 15, "x3" }, { 19, "x5" }, { 22, "x10" } };
        for (const auto& m : multipliers)
        {
            const float segY = meterBottom - m.hits * segPitch;
            if (game.orangeHit >= m.hits) SDL_SetRenderDrawColor(renderer, 255, 220, 120, 255);
            else SDL_SetRenderDrawColor(renderer, 150, 150, 160, 255);
            DrawText(renderer, m.label, meterX + 19.0f, segY + 4.0f, 1.0f);
        }

        if (popup.timer > 0.0f && !popup.lines.empty())
            DrawShotPopup(renderer, popup);

        if (game.feverZoomOut)
            DrawExtremeFeverText(renderer, game);
        if (game.finale.scoreActive)
            DrawFeverScore(renderer, game);

        if (game.roulette.active)
            DrawRoulette(renderer, game.roulette);

        // Overlays (Paused, Fail, LevelClear)
        if (screen == Screen::Paused || screen == Screen::Fail || screen == Screen::LevelClear)
        {
            const bool hoverOn = !levelSelectOpen && !settingsOpen && !styleShotsOpen && !runPromptOpen;
            DrawDim(renderer, 170);
            DrawPanel(renderer, { LAUNCHER_X - 210.0f, 80.0f, 420.0f, 435.0f });

            const std::string scoreLine = "SCORE " + FormatNumber(game.score);
            if (screen == Screen::Paused)
            {
                const std::string title = "PAUSED";
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                DrawText(renderer, title, LAUNCHER_X - TextWidth(title, 5.0f) / 2.0f, 115.0f, 5.0f);
                DrawText(renderer, scoreLine, LAUNCHER_X - TextWidth(scoreLine, 2.0f) / 2.0f, 175.0f, 2.0f);
            }
            else if (screen == Screen::Fail)
            {
                const std::string title = "FAIL";
                SDL_SetRenderDrawColor(renderer, 230, 70, 60, 255);
                DrawText(renderer, title, LAUNCHER_X - TextWidth(title, 5.0f) / 2.0f, 115.0f, 5.0f);
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                DrawText(renderer, scoreLine, LAUNCHER_X - TextWidth(scoreLine, 2.0f) / 2.0f, 175.0f, 2.0f);
                const std::string note = "OUT OF SHOTS";
                SDL_SetRenderDrawColor(renderer, 160, 160, 170, 255);
                DrawText(renderer, note, LAUNCHER_X - TextWidth(note, 2.0f) / 2.0f, 200.0f, 2.0f);
            }
            else
            {
                const std::string title = "LEVEL CLEARED";
                SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
                DrawText(renderer, title, LAUNCHER_X - TextWidth(title, 3.0f) / 2.0f, 96.0f, 3.0f);

                // Level score
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                DrawText(renderer, scoreLine, LAUNCHER_X - TextWidth(scoreLine, 2.0f) / 2.0f, 130.0f, 2.0f);

                const std::vector<ScoreEntry> levelScores = ScoresForLevel(scores, currentLevel);
                const long long prevBest = levelScores.empty() ? 0 : levelScores[0].score;
                const bool isNewHigh = (game.score > prevBest);

                if (isNewHigh)
                {
                    const std::string highText = "NEW HIGHSCORE!";
                    SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
                    DrawText(renderer, highText, LAUNCHER_X - TextWidth(highText, 2.0f) / 2.0f, 154.0f, 2.0f);
                }
                else
                {
                    const std::string bestLine = "BEST: " + FormatNumber(prevBest);
                    SDL_SetRenderDrawColor(renderer, 170, 180, 200, 255);
                    DrawText(renderer, bestLine, LAUNCHER_X - TextWidth(bestLine, 2.0f) / 2.0f, 154.0f, 2.0f);
                }

                const bool isLevel10Run = (activeRun.active && currentLevel == LevelCount());

                if (isLevel10Run)
                {
                    const long long runGrandTotal = activeRun.CompletedTotal() + game.score;
                    const std::string totalLine = "10-RUN TOTAL: " + FormatNumber(runGrandTotal);
                    SDL_SetRenderDrawColor(renderer, 255, 200, 70, 255);
                    DrawText(renderer, totalLine, LAUNCHER_X - TextWidth(totalLine, 1.8f) / 2.0f, 174.0f, 1.8f);

                    // Name line for Level 10
                    std::string line0 = scoreSaved ? ("LVL NAME: " + nameInput + " (SAVED)") :
                        ("LVL NAME: " + nameInput + (nameInput.size() < 3 && activeNameTarget == 0 ? "_" : ""));
                    SDL_SetRenderDrawColor(renderer, activeNameTarget == 0 ? 255 : 180, activeNameTarget == 0 ? 220 : 180, activeNameTarget == 0 ? 80 : 190, 255);
                    DrawText(renderer, line0, LAUNCHER_X - TextWidth(line0, 1.5f) / 2.0f, 196.0f, 1.5f);

                    // Name line for 10-Run
                    std::string line1 = chalScoreSaved ? ("RUN NAME: " + chalNameInput + " (SAVED)") :
                        ("RUN NAME: " + chalNameInput + (chalNameInput.size() < 3 && activeNameTarget == 1 ? "_" : ""));
                    SDL_SetRenderDrawColor(renderer, activeNameTarget == 1 ? 255 : 180, activeNameTarget == 1 ? 220 : 180, activeNameTarget == 1 ? 80 : 190, 255);
                    DrawText(renderer, line1, LAUNCHER_X - TextWidth(line1, 1.5f) / 2.0f, 214.0f, 1.5f);

                    const std::string hint = "CLICK NAME OR PRESS TAB TO SWITCH";
                    SDL_SetRenderDrawColor(renderer, 150, 150, 160, 255);
                    DrawText(renderer, hint, LAUNCHER_X - TextWidth(hint, 1.0f) / 2.0f, 232.0f, 1.0f);
                }
                else
                {
                    std::string nameLine = scoreSaved ? ("SAVED AS " + nameInput) : ("NAME " + nameInput + (nameInput.size() < 3 ? "_" : ""));
                    SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);
                    DrawText(renderer, nameLine, LAUNCHER_X - TextWidth(nameLine, 2.0f) / 2.0f, 185.0f, 2.0f);

                    if (!scoreSaved)
                    {
                        const std::string hint = "TYPE 3 LETTERS THEN SAVE";
                        SDL_SetRenderDrawColor(renderer, 160, 160, 170, 255);
                        DrawText(renderer, hint, LAUNCHER_X - TextWidth(hint, 1.0f) / 2.0f, 210.0f, 1.0f);
                    }
                }
            }

            DrawButtons(renderer,
                GetScreenButtons(screen, currentLevel, LevelCount(), nameInput.size() == 3, scoreSaved),
                hoverOn ? mouseX : -1.0f, hoverOn ? mouseY : -1.0f);

            if (levelSelectOpen)
                DrawLevelSelect(renderer, LevelCount(), mouseX, mouseY);
            else if (settingsOpen)
                DrawSettings(renderer, mouseX, mouseY);
            else if (styleShotsOpen)
                DrawStyleShots(renderer, mouseX, mouseY);
        }

        SDL_RenderPresent(renderer);
    }

    AudioShutdown();
    if (worldTex) SDL_DestroyTexture(worldTex);
    if (ribbonTex) SDL_DestroyTexture(ribbonTex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}