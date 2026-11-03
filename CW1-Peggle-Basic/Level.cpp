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

// Adds a circular ring of pegs evenly spaced around (cx, cy)
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

static void AssignPegTypes(std::vector<Peg>& pegs)
{
    if (pegs.empty()) return;

    // Shuffle the peg order: first 25 are Orange, 26th is Purple, remaining 74 are Blue
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

// Level 1 - Alternating grid (100 pegs)
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

// Level 2 - Pyramid (100 pegs)
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

// Level 3 - 5 Circular rings (5 x 20 = 100 pegs)
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

// Level 4 - Center vertical line with two large side circles (20 + 40 + 40 = 100 pegs)
static std::vector<Peg> BuildLevel4()
{
    std::vector<Peg> pegs;

    // 1. Center vertical line of 20 pegs
    constexpr int   lineCount = 20;
    constexpr float lineSpacing = 17.0f;
    constexpr float lineStartY = 165.0f;
    for (int i = 0; i < lineCount; ++i)
    {
        Peg p;
        p.x = LAUNCHER_X;
        p.y = lineStartY + i * lineSpacing;
        pegs.push_back(p);
    }

    // 2. Left and Right large circles (40 pegs each)
    constexpr int   circleCount = 40;
    constexpr float circleRadius = 125.0f;
    constexpr float circleY = 325.0f;
    constexpr float leftCircleX = 225.0f;
    constexpr float rightCircleX = 575.0f;

    AddRing(pegs, leftCircleX, circleY, circleRadius, circleCount);
    AddRing(pegs, rightCircleX, circleY, circleRadius, circleCount);

    return pegs;
}

// Add above LevelCount():

// Level 5 - 7 vertical columns of pegs equally spaced across the play area (100 pegs)
static std::vector<Peg> BuildLevel5()
{
    std::vector<Peg> pegs;

    constexpr int   columnCount = 7;
    constexpr float startY = 180.0f;
    constexpr float endY = 480.0f;
    constexpr float columnHeight = endY - startY;

    // 8 equal gaps between walls and the 7 columns (87.5px each)
    constexpr float gapX = (PLAY_RIGHT - PLAY_LEFT) / (columnCount + 1.0f);

    for (int col = 0; col < columnCount; ++col)
    {
        const float x = PLAY_LEFT + (col + 1) * gapX;

        // 6 columns with 14 pegs + 1 center column with 16 pegs = 100 pegs
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

// Update LevelCount():
int LevelCount()
{
    return 5; // Updated to 5 levels
}

// Update BuildLevel():
std::vector<Peg> BuildLevel(int levelNumber)
{
    std::vector<Peg> pegs;
    switch (levelNumber)
    {
    case 1: pegs = BuildLevel1(); break;
    case 2: pegs = BuildLevel2(); break;
    case 3: pegs = BuildLevel3(); break;
    case 4: pegs = BuildLevel4(); break;
    case 5: pegs = BuildLevel5(); break; // Added Level 5
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