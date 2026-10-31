#include "Level.h"
#include "Config.h"

#include <algorithm>
#include <random>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// One random number generator shared by everything in this file
static std::mt19937& Rng()
{
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}

// Adds a row of pegs centred on the launcher
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

// Picks random pegs to be orange and one to be purple, the rest stay blue
static void AssignPegTypes(std::vector<Peg>& pegs)
{
    if (pegs.empty()) return;

    // Shuffle the peg numbers, then the first ones become orange and the next one purple
    std::vector<int> order(pegs.size());
    for (int i = 0; i < static_cast<int>(order.size()); ++i) order[i] = i;
    std::shuffle(order.begin(), order.end(), Rng());

    // A level with very few pegs still keeps room for the purple one
    const int orangeCount = std::min(ORANGE_PEG_COUNT, static_cast<int>(pegs.size()) - 1);

    for (int i = 0; i < orangeCount; ++i)
        pegs[order[i]].type = PegType::Orange;
    pegs[order[orangeCount]].type = PegType::Purple;
}

// ---------------------------------------------------------------------------
// Levels
// ---------------------------------------------------------------------------

// Level 1 - rows of alternating pegs
static std::vector<Peg> BuildLevel1()
{
    constexpr int   rows = 8;
    constexpr int   colsLong = 13; // rows 1, 3, 5, 7
    constexpr int   colsShort = 12; // rows 2, 4, 6, 8. This is done to make sure there are 100 pegs.
    constexpr float spacing = 40.0f; // distance between pegs
    constexpr float gridTop = 210.0f; // y of the first row

    std::vector<Peg> pegs;
    for (int row = 0; row < rows; ++row)
    {
        const int cols = (row % 2 == 0) ? colsLong : colsShort;
        AddCentredRow(pegs, gridTop + row * spacing, cols, spacing);
    }
    return pegs;
}

// Level 2 - basic example may change later. Add a menu later, after beating level 1 to go to level 2. Add a main menu later to select levels.
static std::vector<Peg> BuildLevel2()
{
    constexpr int   rows = 12;
    constexpr float spacing = 40.0f;
    constexpr float gridTop = 180.0f;

    std::vector<Peg> pegs;
    for (int row = 0; row < rows; ++row)
        AddCentredRow(pegs, gridTop + row * spacing, row + 1, spacing);
    return pegs;
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------
int LevelCount()
{
    return 2;
}

std::vector<Peg> BuildLevel(int levelNumber)
{
    std::vector<Peg> pegs;
    switch (levelNumber)
    {
    case 1: pegs = BuildLevel1(); break;
    case 2: pegs = BuildLevel2(); break;
    default: break;
    }

    AssignPegTypes(pegs);
    return pegs;
}

void MovePurplePeg(std::vector<Peg>& pegs)
{
    // Find the current purple peg (if it was deleted it doesn't count) and every blue peg left
    int oldPurple = -1;
    std::vector<int> blues;
    for (int i = 0; i < static_cast<int>(pegs.size()); ++i)
    {
        if (pegs[i].removed) continue;
        if (pegs[i].type == PegType::Purple) oldPurple = i;
        else if (pegs[i].type == PegType::Blue) blues.push_back(i);
    }

    // No blue pegs left to pick, so the purple peg stays where it is
    if (blues.empty()) return;

    if (oldPurple != -1)
        pegs[oldPurple].type = PegType::Blue;

    std::uniform_int_distribution<int> pick(0, static_cast<int>(blues.size()) - 1);
    pegs[blues[pick(Rng())]].type = PegType::Purple;
}
