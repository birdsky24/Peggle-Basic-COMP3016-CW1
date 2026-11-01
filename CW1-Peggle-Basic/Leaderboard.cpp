#include "Leaderboard.h"
#include "Config.h"

#include <algorithm>
#include <fstream>

// Each line of the file is: level name score, for example "1 ABC 123456"

std::vector<ScoreEntry> LoadScores()
{
    std::vector<ScoreEntry> scores;

    std::ifstream file(LEADERBOARD_FILE);
    ScoreEntry entry;
    while (file >> entry.level >> entry.name >> entry.score)
        scores.push_back(entry);

    std::stable_sort(scores.begin(), scores.end(),
        [](const ScoreEntry& a, const ScoreEntry& b) { return a.score > b.score; });
    return scores;
}

bool SaveScore(const ScoreEntry& entry)
{
    std::ofstream file(LEADERBOARD_FILE, std::ios::app);
    if (!file) return false;

    file << entry.level << ' ' << entry.name << ' ' << entry.score << '\n';
    return true;
}
