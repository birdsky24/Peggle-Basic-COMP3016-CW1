#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Config.h"
#include "Level.h"

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
    ShotStats shot;
};

// The box in the middle of the screen at the end of each shot
struct Popup
{
    std::vector<std::string> lines;
    float timer = 0.0f;
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
    std::vector<Peg> pegs = BuildLevel(1);
    std::vector<int> hitOrder; // pegs hit by the current shot, waiting to be deleted in order
    float removeTimer = 0.0f;
    GameState game;
    Popup popup;
    for (const Peg& peg : pegs)
        if (peg.type == PegType::Orange) game.totalOrange++;
    float mouseX = LAUNCHER_X;
    float mouseY = WINDOW_H;

    bool     running = true;
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
                if (e.key.key == SDLK_ESCAPE) running = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                mouseX = e.motion.x;
                mouseY = e.motion.y;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT && !ball.active && hitOrder.empty() && game.ballsLeft > 0 && !game.levelComplete)
                {
                    mouseX = e.button.x;
                    mouseY = e.button.y;
                    float dirX, dirY;
                    GetAimDirection(mouseX, mouseY, dirX, dirY);
                    FireBall(ball, dirX, dirY);
                    game.shotInProgress = true;
                    game.ballsLeft--;
                    game.shot = ShotStats();
                    game.shot.orangeAtStart = game.orangeHit;
                }
                break;
            }
        }

        // ---- Timing ----
        const Uint64 now = SDL_GetTicksNS();
        float frameDt = static_cast<float>(now - lastTicks) / 1.0e9f;
        lastTicks = now;
        frameDt = std::min(frameDt, MAX_FRAME_DT);

        // ---- Update (fixed timestep) ----
        // Time slows down while the shot is in extreme fever
        const float timeScale = (game.feverActive && ball.active) ? FEVER_SLOWMO : 1.0f;
        accumulator += frameDt * timeScale;
        while (accumulator >= PHYSICS_STEP)
        {
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
        else if (game.levelComplete)
        {
            const std::string msg = "LEVEL CLEARED";
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            DrawText(renderer, msg, LAUNCHER_X - TextWidth(msg, 3.0f) / 2.0f, (PLAY_TOP + PLAY_BOTTOM) / 2.0f - 12.0f, 3.0f);
        }

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
