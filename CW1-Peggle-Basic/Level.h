#pragma once

#include <vector>
#include "Peg.h"

// Number of levels that exist, levels are numbered 1 to LevelCount()
int LevelCount();

// Builds and returns the pegs for a level. An unknown number gives an empty level.
std::vector<Peg> BuildLevel(int levelNumber);
