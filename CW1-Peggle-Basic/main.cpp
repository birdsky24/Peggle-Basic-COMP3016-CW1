#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
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

static void UpdateBall(Ball& ball, const std::vector<Peg>& pegs, float dt)
{
    if (!ball.active) return;

    // Gravity then move (semi-implicit Euler)
    ball.vy += GRAVITY * dt;
    ball.x += ball.vx * dt;
    ball.y += ball.vy * dt;

    // Left wall
    if (ball.x - BALL_RADIUS < PLAY_LEFT)
    {
        ball.x = PLAY_LEFT + BALL_RADIUS;
        ball.vx = std::fabs(ball.vx) * WALL_BOUNCE;
    }
    // Right wall
    else if (ball.x + BALL_RADIUS > PLAY_RIGHT)
    {
        ball.x = PLAY_RIGHT - BALL_RADIUS;
        ball.vx = -std::fabs(ball.vx) * WALL_BOUNCE;
    }

    // Ceiling
    if (ball.y - BALL_RADIUS < PLAY_TOP)
    {
        ball.y = PLAY_TOP + BALL_RADIUS;
        ball.vy = std::fabs(ball.vy) * WALL_BOUNCE;
    }

    // If the ball overlaps with a peg it is pushed back
    for (const Peg& peg : pegs)
    {
        const float dx = ball.x - peg.x;
        const float dy = ball.y - peg.y;
        const float minDist = BALL_RADIUS + PEG_RADIUS;
        const float distSq = dx * dx + dy * dy;
        if (distSq >= minDist * minDist) continue;

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

    // shot is deleted if off the screen
    if (ball.y - BALL_RADIUS > PLAY_BOTTOM)
    {
        ball.active = false;
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
    std::vector<Peg> pegs = BuildLevel(1);
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
                if (e.button.button == SDL_BUTTON_LEFT && !ball.active)
                {
                    mouseX = e.button.x;
                    mouseY = e.button.y;
                    float dirX, dirY;
                    GetAimDirection(mouseX, mouseY, dirX, dirY);
                    FireBall(ball, dirX, dirY);
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
        accumulator += frameDt;
        while (accumulator >= PHYSICS_STEP)
        {
            UpdateBall(ball, pegs, PHYSICS_STEP);
            accumulator -= PHYSICS_STEP;
        }

        // ---- Render ----
        SDL_SetRenderDrawColor(renderer, 20, 24, 40, 255);
        SDL_RenderClear(renderer);

        // Pegs (all blue for now, colours come later)
        SDL_SetRenderDrawColor(renderer, 50, 110, 220, 255);
        for (const Peg& peg : pegs)
            DrawFilledCircle(renderer, peg.x, peg.y, PEG_RADIUS);

        // Launcher
        SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
        DrawFilledCircle(renderer, LAUNCHER_X, LAUNCHER_Y, 14.0f);

        // Aim guide only shows when no ball is in play
        if (!ball.active)
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

        // Black borders (will add stuff there later)
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        const SDL_FRect borders[4] = {
            { 0.0f,        0.0f,        static_cast<float>(WINDOW_W), BORDER_TOP }, // top
            { 0.0f,        PLAY_BOTTOM, static_cast<float>(WINDOW_W), BORDER_BOTTOM }, // bottom
            { 0.0f,        PLAY_TOP,    BORDER_LEFT,                  PLAY_BOTTOM - PLAY_TOP }, // left
            { PLAY_RIGHT,  PLAY_TOP,    BORDER_RIGHT,                 PLAY_BOTTOM - PLAY_TOP }, // right
        };
        SDL_RenderFillRects(renderer, borders, 4);

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
