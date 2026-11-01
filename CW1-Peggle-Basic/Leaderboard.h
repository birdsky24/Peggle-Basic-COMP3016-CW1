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

// The scores for one level out of a list from LoadScores, highest first (at most LEADERBOARD_SCORES_PER_LEVEL of them).
std::vector<ScoreEntry> ScoresForLevel(const std::vector<ScoreEntry>& scores, int level);

// Adds a score to the leaderboard file. Each level only keeps its best LEADERBOARD_SCORES_PER_LEVEL scores,
// so lower ones are deleted from the file. Returns false if the file couldn't be written.
bool SaveScore(const ScoreEntry& entry);
