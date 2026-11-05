#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <random>

#include "Config.h"
#include "Peg.h"
#include "Bucket.h"
#include "Leaderboard.h"
#include "SDLWrappers.h"

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

struct FloatingText
{
    std::string text;
    std::string text2;
    float x = 0.0f, y = 0.0f;
    float timer = 0.0f;
    SDL_Color colour = { 255, 255, 255, 255 };
    bool isStyle = false;
    int stack = 0;
};

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

struct ShotStats
{
    int pegScoreSum = 0;
    int pegsHit = 0;
    int stylePoints = 0;
    std::vector<StyleAward> styles;
    int orangeAtStart = 0;
    float distSinceContact = 0.0f;
    int lastContactPeg = -1;
    bool lastContactNonBlue = false;
    bool bouncedOffWall = false;
    float distSinceWall = 0.0f;
    int consecutiveLong = 0;
    int freeBallsEarned = 0;
    float freeBallFlash = 0.0f;
    bool bucketEdgeHit = false;
    bool pegAfterEdge = false;
    bool edgeSincePeg = false;
};

struct Roulette
{
    bool active = false;
    bool returned = false;
    bool revealed = false;
    float timer = 0.0f;
};

struct BallBonusPopup
{
    int index = 0;
    float timer = 0.0f;
};

struct Finale
{
    bool fireworksActive = false;
    float fireworksTimer = 0.0f;
    bool scoreActive = false;
    bool ballsStarted = false;
    long long target = 0;
    long long shown = 0;
    double shownF = 0.0;
    float timer = 0.0f;
    float holdTimer = 0.0f;
};

struct DroppingBall
{
    float y = 0.0f;
    float vy = 0.0f;
    int targetIndex = 0;
    int bouncesLeft = 1;
    bool settled = false;
};

struct BallLoadAnim
{
    bool active = false;
    float startX = 0.0f, startY = 0.0f;
    float apexY = 0.0f;
    float timer = 0.0f;
    float duration = 0.45f;
    float curX = 0.0f, curY = 0.0f;
};

struct RunState
{
    bool active = false;
    int currentLevel = 1;
    bool noFail = true;
    bool noReload = false;
    int savedBalls = 10;
    int completedCount = 0;
    long long levelScores[10] = { 0 };

    long long CompletedTotal() const
    {
        long long sum = 0;
        for (int i = 0; i < completedCount && i < 10; ++i)
            sum += levelScores[i];
        return sum;
    }
};

struct GameState
{
    long long score = 0;
    int ballsLeft = STARTING_BALLS;
    int orangeHit = 0;
    int totalOrange = 0;
    bool shotInProgress = false;
    bool feverActive = false;
    bool ultraFever = false;
    int bucketPoints = 0;
    bool levelComplete = false;
    bool freeBall = false;
    bool approachingLastOrange = false;
    bool slowmoTriggered = false;
    float timeScale = 1.0f;
    int bucketStreak = 0;
    Roulette roulette;
    ShotStats shot;
    std::vector<FloatingText> floaters;
    std::vector<PegPopEffect> pegPops;

    bool ballBonusActive = false;
    int ballBonusShown = 0;
    int ballBonusLayoutCount = 0;
    float ballBonusTimer = 0.0f;
    std::vector<BallBonusPopup> ballBonusPopups;
    std::vector<DroppingBall> droppingBalls;

    BallLoadAnim ballLoadAnim;
    bool launcherLoaded = true;
    bool convertingToNoFail = false;

    float stuckTimer = 0.0f;
    float stuckCheckX = 0.0f;
    float stuckCheckY = 0.0f;

    bool ribbonActive = false;
    float ribbonTimer = 0.0f;
    bool ribbonDone = false;

    Finale finale;

    float camZoom = 0.0f;
    float camFocusX = LAUNCHER_X;
    float camFocusY = (PLAY_TOP + PLAY_BOTTOM) / 2.0f;
    bool  feverZoomOut = false;
    float feverZoomTimer = 0.0f;
    float feverZoomStart = 0.0f;
};

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
    None,
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
    SwitchToNoFail,
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
    std::string label2 = "";
    Action action;
    int level;
};

class Game
{
public:
    Game();
    ~Game();

    bool Initialize();
    void Run();

private:
    void HandleEvents();
    void Update(float frameDt);
    void Render();

    void DoAction(const Button& b);
    void StartLevel(int level);
    void RestoreLevel();
    void SaveLevelState();
    void GoToMenu();
    void SaveCurrentScore();
    void SaveChallengeScore();

    void AwardFreeBall();
    void CheckFreeBalls();
    void AddPegPopup(const Peg& peg, int points);
    void AddStyle(const char* name, int points, float x, float y);
    void SpawnPegPop(const Peg& peg);
    void SpawnBallPop(float bx, float by);
    void StartBallLoadAnim(float delay = 0.0f);
    void FinishShot();

    void UpdateBall(float dt);
    bool BounceOffTriangle(const Triangle& tri);
    void BounceOffCircle(float cx, float cy, float radius);

    void RenderWorld();
    void RenderHUD();
    void RenderOverlays();
    void RenderActivePopups();

    UniqueWindow   m_window;
    UniqueRenderer m_renderer;
    UniqueTexture  m_worldTex;
    UniqueTexture  m_ribbonTex;

    bool m_running = true;
    bool m_fastForward = false;
    Screen m_screen = Screen::MainMenu;
    int m_currentLevel = 1;

    bool m_levelSelectOpen = false;
    bool m_settingsOpen = false;
    bool m_styleShotsOpen = false;
    bool m_runPromptOpen = false;
    bool m_draggingSlider = false;

    std::string m_nameInput;
    bool m_scoreSaved = false;
    std::string m_chalNameInput;
    bool m_chalScoreSaved = false;
    int m_activeNameTarget = 0;

    RunState m_activeRun;
    std::vector<ScoreEntry> m_scores;

    Ball m_ball;
    Bucket m_bucket;
    std::vector<Peg> m_pegs;
    std::vector<int> m_hitOrder;
    float m_removeTimer = 0.0f;
    int m_removeCount = 0;

    GameState m_game;
    Popup m_popup;

    float m_mouseX = LAUNCHER_X;
    float m_mouseY = WINDOW_H;
    float m_accumulator = 0.0f;
    Uint64 m_lastTicks = 0;
};