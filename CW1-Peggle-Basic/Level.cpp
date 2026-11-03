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

static void AddPolyline(std::vector<Peg>& pegs, const std::vector<Point>& pts, int count, bool closed = false)
{
    if (pts.size() < 2 || count <= 0) return;

    std::vector<Point> allPts = pts;
    if (closed) allPts.push_back(pts.front());

    std::vector<float> segLens(allPts.size() - 1);
    float totalLen = 0.0f;
    for (size_t i = 0; i < allPts.size() - 1; ++i)
    {
        const float dx = allPts[i + 1].x - allPts[i].x;
        const float dy = allPts[i + 1].y - allPts[i].y;
        segLens[i] = std::sqrt(dx * dx + dy * dy);
        totalLen += segLens[i];
    }

    if (totalLen <= 0.0001f) return;

    const int steps = closed ? count : (count - 1);
    for (int i = 0; i < count; ++i)
    {
        const float targetDist = (static_cast<float>(i) / static_cast<float>(steps)) * totalLen;
        float acc = 0.0f;

        for (size_t s = 0; s < segLens.size(); ++s)
        {
            if (targetDist <= acc + segLens[s] || s == segLens.size() - 1)
            {
                const float u = (segLens[s] > 0.0001f) ? (targetDist - acc) / segLens[s] : 0.0f;
                Peg p;
                p.x = allPts[s].x + std::max(0.0f, std::min(1.0f, u)) * (allPts[s + 1].x - allPts[s].x);
                p.y = allPts[s].y + std::max(0.0f, std::min(1.0f, u)) * (allPts[s + 1].y - allPts[s].y);
                pegs.push_back(p);
                break;
            }
            acc += segLens[s];
        }
    }
}

static void AddStar(std::vector<Peg>& pegs, float cx, float cy, float rOuter, float rInner, int count)
{
    std::vector<Point> pts;
    for (int i = 0; i < 12; ++i)
    {
        const float r = (i % 2 == 0) ? rOuter : rInner;
        const float angle = (static_cast<float>(i) / 12.0f) * (2.0f * SDL_PI_F) - (SDL_PI_F / 2.0f);
        pts.push_back({ cx + r * std::cos(angle), cy + r * std::sin(angle) });
    }
    AddPolyline(pegs, pts, count, true);
}

// Builds a heart curve with separated left/right halves to prevent overlapping pegs
static void AddHeart(std::vector<Peg>& pegs, float cx, float cy, float scale, int count, float topSep, float bottomSep)
{
    std::vector<Point> pts;
    constexpr int samples = 72;

    for (int i = 0; i < samples; ++i)
    {
        const float t = (static_cast<float>(i) / static_cast<float>(samples)) * (2.0f * SDL_PI_F);

        // Standard parametric heart formula
        const float hx = 16.0f * std::pow(std::sin(t), 3.0f);
        const float hy = -(13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t));

        // Separate left and right halves to prevent crevice and tip bunching
        float u = 0.0f;
        float sign = 1.0f;
        if (t <= SDL_PI_F)
        {
            u = t / SDL_PI_F; // 0.0 (top crevice) -> 1.0 (bottom tip)
            sign = 1.0f;      // Right half (+X)
        }
        else
        {
            u = (2.0f * SDL_PI_F - t) / SDL_PI_F; // 1.0 (bottom tip) -> 0.0 (top crevice)
            sign = -1.0f;     // Left half (-X)
        }

        const float currentSep = topSep + u * (bottomSep - topSep);
        pts.push_back({ cx + sign * currentSep + hx * scale, cy + hy * scale });
    }

    AddPolyline(pegs, pts, count, true);
}

static void AddLine(std::vector<Peg>& pegs, float x1, float y1, float x2, float y2, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const float u = (count > 1) ? (static_cast<float>(i) / static_cast<float>(count - 1)) : 0.0f;
        Peg p;
        p.x = x1 + u * (x2 - x1);
        p.y = y1 + u * (y2 - y1);
        pegs.push_back(p);
    }
}

// Builds a 6-pointed star with 24 perimeter pegs and 5 interior cross pegs (29 pegs total)
static void AddStarPattern(std::vector<Peg>& pegs, float cx, float cy, float rOuter, float rInner)
{
    // 12 vertices of the 6-pointed star (alternating tip and valley)
    Point v[12];
    for (int i = 0; i < 12; ++i)
    {
        const float r = (i % 2 == 0) ? rOuter : rInner;
        const float angle = (static_cast<float>(i) * 30.0f - 90.0f) * (SDL_PI_F / 180.0f);
        v[i] = { cx + r * std::cos(angle), cy + r * std::sin(angle) };
    }

    // 24 perimeter pegs: 1 at each vertex (12) + 1 at each edge midpoint (12)
    for (int i = 0; i < 12; ++i)
    {
        // Vertex peg
        Peg p1;
        p1.x = v[i].x;
        p1.y = v[i].y;
        pegs.push_back(p1);

        // Midpoint peg
        const Point next = v[(i + 1) % 12];
        Peg p2;
        p2.x = (v[i].x + next.x) * 0.5f;
        p2.y = (v[i].y + next.y) * 0.5f;
        pegs.push_back(p2);
    }

    // 5 pegs inside the star (center + 4-way cross)
    constexpr float innerOffset = 32.0f;
    Peg center; center.x = cx; center.y = cy; pegs.push_back(center);
    Peg top;    top.x = cx;    top.y = cy - innerOffset; pegs.push_back(top);
    Peg bottom; bottom.x = cx; bottom.y = cy + innerOffset; pegs.push_back(bottom);
    Peg left;   left.x = cx - innerOffset; left.y = cy; pegs.push_back(left);
    Peg right;  right.x = cx + innerOffset; right.y = cy; pegs.push_back(right);
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

// Level 3 - 5 Rings (100 pegs)
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

// Level 4 - 4 Ovals and center line (100 pegs)
static std::vector<Peg> BuildLevel4()
{
    std::vector<Peg> pegs;

    constexpr int   lineCount = 20;
    constexpr float lineSpacing = 24.0f;
    constexpr float lineStartY = 95.0f;
    for (int i = 0; i < lineCount; ++i)
    {
        Peg p;
        p.x = LAUNCHER_X;
        p.y = lineStartY + i * lineSpacing;
        pegs.push_back(p);
    }

    constexpr int   pegsPerOval = 20;
    constexpr float ovalRadiusX = 95.0f;
    constexpr float ovalRadiusY = 110.0f;

    AddOval(pegs, 225.0f, 190.0f, ovalRadiusX, ovalRadiusY, pegsPerOval);
    AddOval(pegs, 225.0f, 440.0f, ovalRadiusX, ovalRadiusY, pegsPerOval);
    AddOval(pegs, 575.0f, 190.0f, ovalRadiusX, ovalRadiusY, pegsPerOval);
    AddOval(pegs, 575.0f, 440.0f, ovalRadiusX, ovalRadiusY, pegsPerOval);
    return pegs;
}

// Level 5 - 7 Columns (100 pegs)
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

// Level 6 - 5 Lightning Bolts (100 pegs)
static std::vector<Peg> BuildLevel6()
{
    std::vector<Peg> pegs;
    constexpr int boltCount = 5;
    constexpr int pegsPerBolt = 20;

    for (int k = 0; k < boltCount; ++k)
    {
        const float baseX = 185.0f + k * 107.5f;
        const std::vector<Point> pts = {
            { baseX,          120.0f },
            { baseX - 125.0f, 250.0f },
            { baseX + 120.0f, 380.0f },
            { baseX - 60.0f,  515.0f }
        };
        AddPolyline(pegs, pts, pegsPerBolt);
    }
    return pegs;
}

// Level 7 - 3 Stars (29 pegs each) + 3 surrounding clusters (5 + 4 + 4 = 13 pegs) = 100 pegs
static std::vector<Peg> BuildLevel7()
{
    std::vector<Peg> pegs;

    constexpr float rOuter = 110.0f;
    constexpr float rInner = 62.0f;

    // 1. Three 6-Pointed Stars (29 pegs each = 87 pegs)
    AddStarPattern(pegs, 225.0f, 220.0f, rOuter, rInner); // Top-Left
    AddStarPattern(pegs, 575.0f, 220.0f, rOuter, rInner); // Top-Right
    AddStarPattern(pegs, 400.0f, 410.0f, rOuter, rInner); // Bottom-Center

    // 2. Top-Center Cluster (5 pegs below the launcher)
    constexpr float topClusterX = 400.0f;
    pegs.push_back({ topClusterX, 135.0f });
    pegs.push_back({ topClusterX, 165.0f });
    pegs.push_back({ topClusterX, 195.0f });
    pegs.push_back({ topClusterX - 30.0f, 165.0f });
    pegs.push_back({ topClusterX + 30.0f, 165.0f });

    // 3. Bottom-Left Outside Cluster (4-peg diamond, 1 removed for 100-peg balance)
    constexpr float blX = 160.0f, blY = 460.0f, cOff = 28.0f;
    pegs.push_back({ blX, blY - cOff });
    pegs.push_back({ blX, blY + cOff });
    pegs.push_back({ blX - cOff, blY });
    pegs.push_back({ blX + cOff, blY });

    // 4. Bottom-Right Outside Cluster (4-peg diamond, 1 removed for 100-peg balance)
    constexpr float brX = 640.0f, brY = 460.0f;
    pegs.push_back({ brX, brY - cOff });
    pegs.push_back({ brX, brY + cOff });
    pegs.push_back({ brX - cOff, brY });
    pegs.push_back({ brX + cOff, brY });

    return pegs;
}

// Level 8 - Concentric Hearts with Wider Spacing & Outside Circles (45 + 35 + 10 + 10 = 100 pegs)
static std::vector<Peg> BuildLevel8()
{
    std::vector<Peg> pegs;

    constexpr float heartCenterY = 282.0f;

    // 1. Outer Heart (Enlarged to scale 15.2f, 45 pegs)
    AddHeart(pegs, LAUNCHER_X, heartCenterY, 15.2f, 45, 20.0f, 12.0f);

    // 2. Inner Heart (scale 8.5f, 35 pegs) - creates a wide ~95px channel between hearts
    AddHeart(pegs, LAUNCHER_X, heartCenterY + 14.0f, 8.5f, 35, 16.0f, 11.0f);

    // 3. Two Outside Circles on Bottom-Left and Bottom-Right (10 pegs each)
    constexpr float circleRadius = 50.0f;
    constexpr int   pegsPerCircle = 10;
    constexpr float circleY = 465.0f;

    AddRing(pegs, 160.0f, circleY, circleRadius, pegsPerCircle); // Bottom-Left
    AddRing(pegs, 640.0f, circleY, circleRadius, pegsPerCircle); // Bottom-Right

    return pegs;
}

// Level 9 - 4 Pachinko Ramps on each side + Center Divider (8 x 10 + 20 = 100 pegs)
static std::vector<Peg> BuildLevel9()
{
    std::vector<Peg> pegs;

    // 1. Center vertical dividing line (20 pegs, Y=140 to Y=540)
    AddLine(pegs, LAUNCHER_X, 140.0f, LAUNCHER_X, 540.0f, 20);

    // 2. 4 Slanted Ramps on Left & Right with generous ~30px peg spacing
    constexpr float leftStartX = 55.0f;
    constexpr float leftEndX = 340.0f;
    constexpr float rightStartX = 745.0f;
    constexpr float rightEndX = 460.0f;

    constexpr float rampDropY = 85.0f;
    constexpr int   pegsPerRamp = 10;

    for (int r = 0; r < 4; ++r)
    {
        const float startY = 120.0f + r * 105.0f;
        const float endY = startY + rampDropY;

        // Left ramp (funnels down-right)
        AddLine(pegs, leftStartX, startY, leftEndX, endY, pegsPerRamp);

        // Right ramp (funnels down-left)
        AddLine(pegs, rightStartX, startY, rightEndX, endY, pegsPerRamp);
    }

    return pegs;
}

// Level 10 - Grand Clockwork Wheel with Maximized Spacing (36 + 8 + 24 + 32 = 100 pegs)
static std::vector<Peg> BuildLevel10()
{
    std::vector<Peg> pegs;

    constexpr float wheelX = LAUNCHER_X;
    constexpr float wheelY = 320.0f;

    // 1. Outer Wheel Rim (Enlarged to R = 215px, 36 pegs -> ~38px spacing)
    constexpr float rimRadius = 215.0f;
    AddRing(pegs, wheelX, wheelY, rimRadius, 36);

    // 2. Open Center Hub Ring (Enlarged to R = 60px, 8 pegs -> ~47px spacing)
    constexpr float hubRadius = 60.0f;
    AddRing(pegs, wheelX, wheelY, hubRadius, 8);

    // 3. 8 Radial Spokes with wide 40px spacing (3 pegs each = 24 pegs)
    constexpr int spokeCount = 8;
    constexpr int pegsPerSpoke = 3;
    constexpr float spokeStartR = 100.0f;
    constexpr float spokeEndR = 175.0f;
    constexpr float spokeStepR = (spokeEndR - spokeStartR) / static_cast<float>(pegsPerSpoke - 1);

    for (int s = 0; s < spokeCount; ++s)
    {
        const float angle = static_cast<float>(s) * (2.0f * SDL_PI_F / static_cast<float>(spokeCount));
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        for (int p = 0; p < pegsPerSpoke; ++p)
        {
            const float r = spokeStartR + p * spokeStepR;
            pegs.push_back({ wheelX + r * cosA, wheelY + r * sinA });
        }
    }

    // 4. 4 Corner Satellite Rings with wide spacing (8 pegs each = 32 pegs)
    constexpr float cornerR = 45.0f;
    const Point corners[4] = {
        { 115.0f, 150.0f }, // Top-Left
        { 685.0f, 150.0f }, // Top-Right
        { 115.0f, 490.0f }, // Bottom-Left
        { 685.0f, 490.0f }  // Bottom-Right
    };

    for (const Point& c : corners)
    {
        AddRing(pegs, c.x, c.y, cornerR, 8);
    }

    return pegs;
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------
int LevelCount()
{
    return 10; // Updated to 10 levels
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
    case 6: pegs = BuildLevel6(); break;
    case 7: pegs = BuildLevel7(); break;
    case 8: pegs = BuildLevel8(); break;
    case 9: pegs = BuildLevel9(); break;
    case 10: pegs = BuildLevel10(); break; // Added Level 10
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