#include "Level.h"
#include "Config.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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
    switch (levelNumber)
    {
    case 1: return BuildLevel1();
    case 2: return BuildLevel2();
    default: return {};
    }
}
