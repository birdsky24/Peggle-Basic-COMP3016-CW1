#pragma once

#include <vector>
#include "Peg.h"

// Number of levels that exist, levels are numbered 1 to LevelCount()
int LevelCount();

// Builds the pegs for a level, 25 random orange pegs, 1 random purple peg and the rest blue. An unknown number gives an empty level.
std::vector<Peg> BuildLevel(int levelNumber);

// Call this after a shot is finished and the hit pegs are deleted. Turns the purple peg back to blue (if it still exists) and makes a different random blue peg purple.
void MovePurplePeg(std::vector<Peg>& pegs);
