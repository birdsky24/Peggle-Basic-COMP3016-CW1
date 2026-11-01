#pragma once

#include <string>
#include <vector>

struct ScoreEntry
{
    int level = 0;
    std::string name; // 3 letters
    long long score = 0;
};

// Reads every saved score from the leaderboard file, highest score first.
// A missing file just gives an empty list.
std::vector<ScoreEntry> LoadScores();

// Adds one score to the end of the leaderboard file. Returns false if the file couldn't be written.
bool SaveScore(const ScoreEntry& entry);
