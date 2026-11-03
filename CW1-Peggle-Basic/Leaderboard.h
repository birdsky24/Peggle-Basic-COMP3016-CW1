// Leaderboard.h
#pragma once

#include <string>
#include <vector>
#include "Config.h"

struct ScoreEntry
{
    int level = 0;
    std::string name; // 3 letters
    long long score = 0;
};

// Reads scores from any encrypted leaderboard file (defaults to LEADERBOARD_FILE)
std::vector<ScoreEntry> LoadScores(const char* filename = LEADERBOARD_FILE);

std::vector<ScoreEntry> ScoresForLevel(const std::vector<ScoreEntry>& scores, int level);
bool SaveScore(const ScoreEntry& entry);

// Merges another file's scores with current scores and saves the result
bool MergeScoresFromFile(const char* otherFilePath);