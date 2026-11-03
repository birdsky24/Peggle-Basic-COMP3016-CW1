#include "Level.h"
#include "Config.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <random>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::mt19937& Rng()
{
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}

static void AddCentredRow(std::vector<Peg>& pegs, float y, int count, float spacing)
{
    const float startX = LAUNCHER_X - (count - 1) * spacing / 2.0f;
    for (int i = 0; i < count; ++i)
    {
        Peg p;
        p.x = startX + i * spacing;
        p.y = y;
        pegs.push_back(p);
    }
}

static void AddRing(std::vector<Peg>& pegs, float cx, float cy, float radius, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const float angle = (static_cast<float>(i) / static_cast<float>(count)) * (2.0f * SDL_PI_F);
        Peg p;
        p.x = cx + radius * std::cos(angle);
        p.y = cy + radius * std::sin(angle);
        pegs.push_back(p);
    }
}

// Adds an oval (ellipse) of pegs with separate horizontal and vertical radii
static void AddOval(std::vector<Peg>& pegs, float cx, float cy, float radiusX, float radiusY, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const float angle = (static_cast<float>(i) / static_cast<float>(count)) * (2.0f * SDL_PI_F);
        Peg p;
        p.x = cx + radiusX * std::cos(angle);
        p.y = cy + radiusY * std::sin(angle);
        pegs.push_back(p);
    }
}

struct Point { float x, y; };

// Distributes 'count' pegs evenly along a multi-segment line/zigzag
static void AddPolyline(std::vector<Peg>& pegs, const std::vector<Point>& pts, int count)
{
    if (pts.size() < 2 || count <= 0) return;

    std::vector<float> segLens(pts.size() - 1);
    float totalLen = 0.0f;
    for (size_t i = 0; i < pts.size() - 1; ++i)
    {
        const float dx = pts[i + 1].x - pts[i].x;
        const float dy = pts[i + 1].y - pts[i].y;
        segLens[i] = std::sqrt(dx * dx + dy * dy);
        totalLen += segLens[i];
    }

    if (totalLen <= 0.0001f) return;

    for (int i = 0; i < count; ++i)
    {
        const float targetDist = (static_cast<float>(i) / static_cast<float>(count - 1)) * totalLen;
        float acc = 0.0f;

        for (size_t s = 0; s < segLens.size(); ++s)
        {
            if (targetDist <= acc + segLens[s] || s == segLens.size() - 1)
            {
                const float u = (segLens[s] > 0.0001f) ? (targetDist - acc) / segLens[s] : 0.0f;
                Peg p;
                p.x = pts[s].x + std::max(0.0f, std::min(1.0f, u)) * (pts[s + 1].x - pts[s].x);
                p.y = pts[s].y + std::max(0.0f, std::min(1.0f, u)) * (pts[s + 1].y - pts[s].y);
                pegs.push_back(p);
                break;
            }
            acc += segLens[s];
        }
    }
}

static void AssignPegTypes(std::vector<Peg>& pegs)
{
    if (pegs.empty()) return;

    std::vector<int> order(pegs.size());
    for (int i = 0; i < static_cast<int>(order.size()); ++i) order[i] = i;
    std::shuffle(order.begin(), order.end(), Rng());

    const int orangeCount = std::min(ORANGE_PEG_COUNT, static_cast<int>(pegs.size()) - 1);

    for (int i = 0; i < orangeCount; ++i)
        pegs[order[i]].type = PegType::Orange;
    pegs[order[orangeCount]].type = PegType::Purple;
}

// ---------------------------------------------------------------------------
// Levels
// ---------------------------------------------------------------------------

static std::vector<Peg> BuildLevel1()
{
    constexpr int   rows = 8;
    constexpr int   colsLong = 13;
    constexpr int   colsShort = 12;
    constexpr float spacing = 40.0f;
    constexpr float gridTop = 210.0f;

    std::vector<Peg> pegs;
    for (int row = 0; row < rows; ++row)
    {
        const int cols = (row % 2 == 0) ? colsLong : colsShort;
        AddCentredRow(pegs, gridTop + row * spacing, cols, spacing);
    }
    return pegs;
}

static std::vector<Peg> BuildLevel2()
{
    constexpr int   rows = 12;
    constexpr float spacing = 40.0f;
    constexpr float gridTop = 100.0f;

    std::vector<Peg> pegs;
    for (int row = 0; row < rows; ++row)
        AddCentredRow(pegs, gridTop + row * spacing, row + 1, spacing);
    return pegs;
}

static std::vector<Peg> BuildLevel3()
{
    std::vector<Peg> pegs;
    constexpr float radius = 105.0f;
    constexpr int countPerCircle = 20;

    AddRing(pegs, 400.0f, 310.0f, radius, countPerCircle);
    AddRing(pegs, 185.0f, 195.0f, radius, countPerCircle);
    AddRing(pegs, 615.0f, 195.0f, radius, countPerCircle);
    AddRing(pegs, 185.0f, 425.0f, radius, countPerCircle);
    AddRing(pegs, 615.0f, 425.0f, radius, countPerCircle);

    return pegs;
}

// Level 4 - 4 Ovals and an extended center line (4 x 20 + 20 = 100 pegs)
static std::vector<Peg> BuildLevel4()
{
    std::vector<Peg> pegs;

    // 1. Center vertical line of 20 pegs (spanning Y=95 to Y=551, ~456px long)
    constexpr int   lineCount = 20;
    constexpr float lineSpacing = 24.0f; // Wider gaps between vertical pegs
    constexpr float lineStartY = 95.0f;

    for (int i = 0; i < lineCount; ++i)
    {
        Peg p;
        p.x = LAUNCHER_X;
        p.y = lineStartY + i * lineSpacing;
        pegs.push_back(p);
    }

    // 2. 4 Ovals (2 on the left, 2 on the right, 20 pegs each)
    constexpr int   pegsPerOval = 20;
    constexpr float ovalRadiusX = 95.0f;  // Width radius
    constexpr float ovalRadiusY = 110.0f; // Height radius (stretched to increase perimeter & gaps)
    constexpr float topOvalY = 190.0f;
    constexpr float bottomOvalY = 440.0f;
    constexpr float leftOvalX = 225.0f;
    constexpr float rightOvalX = 575.0f;

    // Left side ovals
    AddOval(pegs, leftOvalX, topOvalY, ovalRadiusX, ovalRadiusY, pegsPerOval);
    AddOval(pegs, leftOvalX, bottomOvalY, ovalRadiusX, ovalRadiusY, pegsPerOval);

    // Right side ovals
    AddOval(pegs, rightOvalX, topOvalY, ovalRadiusX, ovalRadiusY, pegsPerOval);
    AddOval(pegs, rightOvalX, bottomOvalY, ovalRadiusX, ovalRadiusY, pegsPerOval);

    return pegs;
}

static std::vector<Peg> BuildLevel5()
{
    std::vector<Peg> pegs;

    constexpr int   columnCount = 7;
    constexpr float startY = 100.0f;
    constexpr float endY = 500.0f;
    constexpr float columnHeight = endY - startY;
    constexpr float gapX = (PLAY_RIGHT - PLAY_LEFT) / (columnCount + 1.0f);

    for (int col = 0; col < columnCount; ++col)
    {
        const float x = PLAY_LEFT + (col + 1) * gapX;
        const int count = (col == 3) ? 16 : 14;
        const float spacing = columnHeight / static_cast<float>(count - 1);

        for (int i = 0; i < count; ++i)
        {
            Peg p;
            p.x = x;
            p.y = startY + i * spacing;
            pegs.push_back(p);
        }
    }

    return pegs;
}

// Level 6 - 5 Lightning Bolts (5 x 20 = 100 pegs)
static std::vector<Peg> BuildLevel6()
{
    std::vector<Peg> pegs;

    constexpr int boltCount = 5;
    constexpr int pegsPerBolt = 20;

    for (int k = 0; k < boltCount; ++k)
    {
        // 5 bolts centered across the board (Bolt 2 is directly under the launcher at 400px)
        const float baseX = 185.0f + k * 107.5f;

        const std::vector<Point> pts = {
            { baseX,          120.0f }, // Top point (starts much higher up)
            { baseX - 125.0f, 250.0f }, // Left knee
            { baseX + 120.0f, 380.0f }, // Right knee (wider sweep)
            { baseX - 60.0f,  515.0f }  // Bottom point (stretches deep towards the bucket)
        };

        AddPolyline(pegs, pts, pegsPerBolt);
    }

    return pegs;
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------
int LevelCount()
{
    return 6; // Updated to 6 levels
}

std::vector<Peg> BuildLevel(int levelNumber)
{
    std::vector<Peg> pegs;
    switch (levelNumber)
    {
    case 1: pegs = BuildLevel1(); break;
    case 2: pegs = BuildLevel2(); break;
    case 3: pegs = BuildLevel3(); break;
    case 4: pegs = BuildLevel4(); break;
    case 5: pegs = BuildLevel5(); break;
    case 6: pegs = BuildLevel6(); break; // Added Level 6
    default: break;
    }

    AssignPegTypes(pegs);
    return pegs;
}

void MovePurplePeg(std::vector<Peg>& pegs)
{
    int oldPurple = -1;
    std::vector<int> blues;
    for (int i = 0; i < static_cast<int>(pegs.size()); ++i)
    {
        if (pegs[i].removed) continue;
        if (pegs[i].type == PegType::Purple) oldPurple = i;
        else if (pegs[i].type == PegType::Blue) blues.push_back(i);
    }

    if (blues.empty()) return;

    if (oldPurple != -1)
        pegs[oldPurple].type = PegType::Blue;

    std::uniform_int_distribution<int> pick(0, static_cast<int>(blues.size()) - 1);
    pegs[blues[pick(Rng())]].type = PegType::Purple;
}