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

// Level 2 - Pyramid (100 pegs: sum of 1 to 12 is 78, plus filler rows or layout)
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

// Level 3 - 5 Circular rings across the play area (5 x 20 = 100 pegs)
static std::vector<Peg> BuildLevel3()
{
    std::vector<Peg> pegs;
    constexpr float radius = 105.0f;
    constexpr int countPerCircle = 20;

    // 1. Center Circle (20 pegs)
    AddRing(pegs, 400.0f, 310.0f, radius, countPerCircle);

    // 2. Top-Left Circle (20 pegs)
    AddRing(pegs, 185.0f, 195.0f, radius, countPerCircle);

    // 3. Top-Right Circle (20 pegs)
    AddRing(pegs, 615.0f, 195.0f, radius, countPerCircle);

    // 4. Bottom-Left Circle (20 pegs)
    AddRing(pegs, 185.0f, 425.0f, radius, countPerCircle);

    // 5. Bottom-Right Circle (20 pegs)
    AddRing(pegs, 615.0f, 425.0f, radius, countPerCircle);

    return pegs;
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------
int LevelCount()
{
    return 3; // Updated to 3
}

std::vector<Peg> BuildLevel(int levelNumber)
{
    std::vector<Peg> pegs;
    switch (levelNumber)
    {
    case 1: pegs = BuildLevel1(); break;
    case 2: pegs = BuildLevel2(); break;
    case 3: pegs = BuildLevel3(); break; // Added Level 3
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