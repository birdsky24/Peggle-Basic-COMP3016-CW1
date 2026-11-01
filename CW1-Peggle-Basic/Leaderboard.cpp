#include "Leaderboard.h"
#include "Config.h"

#include <algorithm>
#include <fstream>
#include <map>

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

std::vector<ScoreEntry> ScoresForLevel(const std::vector<ScoreEntry>& scores, int level)
{
    std::vector<ScoreEntry> result;
    for (const ScoreEntry& entry : scores)
    {
        if (entry.level != level) continue;
        result.push_back(entry);
    }

    std::stable_sort(result.begin(), result.end(),
        [](const ScoreEntry& a, const ScoreEntry& b) { return a.score > b.score; });
    if (static_cast<int>(result.size()) > LEADERBOARD_SCORES_PER_LEVEL)
        result.resize(LEADERBOARD_SCORES_PER_LEVEL);
    return result;
}

bool SaveScore(const ScoreEntry& entry)
{
    // Add the new score, then keep only the best few for each level
    std::vector<ScoreEntry> scores = LoadScores();
    scores.push_back(entry);
    std::stable_sort(scores.begin(), scores.end(),
        [](const ScoreEntry& a, const ScoreEntry& b) { return a.score > b.score; });

    std::map<int, int> kept; // how many scores of each level are kept so far
    std::vector<ScoreEntry> toWrite;
    for (const ScoreEntry& s : scores)
    {
        if (kept[s.level] >= LEADERBOARD_SCORES_PER_LEVEL) continue;
        kept[s.level]++;
        toWrite.push_back(s);
    }

    // The file is written again from scratch
    std::ofstream file(LEADERBOARD_FILE, std::ios::trunc);
    if (!file) return false;
    for (const ScoreEntry& s : toWrite)
        file << s.level << ' ' << s.name << ' ' << s.score << '\n';
    return true;
}
