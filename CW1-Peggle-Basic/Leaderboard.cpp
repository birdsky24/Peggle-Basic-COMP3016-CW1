#include "Leaderboard.h"
#include "Config.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <sstream>

// The leaderboard file is encrypted and signed, so it can't be read or edited in a text editor.
//
// File layout:  "PGB1" (4 bytes) | random nonce (16 bytes) | encrypted scores | signature (32 bytes)
//
// The scores are turned into text ("1 ABC 123456" per line), scrambled with a key stream made from SHA-256,
// then an HMAC-SHA256 signature covers everything before it. If even one byte of the file is changed
// (or the file is made by hand) the signature no longer matches and the whole file is ignored.
//
// This stops anyone changing a score in the file, but the secret key is inside the game's exe, so it can't stop
// someone who takes the exe apart. Only a server that checks scores could do that.

namespace
{
    // Change these bytes to your own random bytes. Anyone who has them could make a valid leaderboard file.
    const uint8_t SECRET[32] = {
        0x29, 0x32, 0x0e, 0x9f, 0x99, 0xbb, 0x06, 0x5a,
        0x1c, 0x1d, 0x4d, 0x96, 0x13, 0xa9, 0x6f, 0x0b,
        0x31, 0x31, 0xb5, 0x53, 0xda, 0x0b, 0x69, 0x8a,
        0x5e, 0x74, 0x73, 0x64, 0x0a, 0x1b, 0x88, 0x9b
    };

    const char     MAGIC[4] = { 'P', 'G', 'B', '1' };
    const size_t   NONCE_SIZE = 16;
    const size_t   TAG_SIZE = 32;
    const size_t   HEADER_SIZE = sizeof(MAGIC) + NONCE_SIZE;

    // ---- SHA-256 ----
    using Digest = std::array<uint8_t, 32>;

    inline uint32_t RotR(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    class Sha256
    {
    public:
        Sha256()
        {
            const uint32_t start[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
            std::copy(start, start + 8, m_state);
        }

        void Update(const uint8_t* data, size_t len)
        {
            m_totalBytes += len;
            while (len > 0)
            {
                const size_t take = std::min(len, sizeof(m_buffer) - m_bufferLen);
                std::memcpy(m_buffer + m_bufferLen, data, take);
                m_bufferLen += take;
                data += take;
                len -= take;
                if (m_bufferLen == sizeof(m_buffer))
                {
                    Block(m_buffer);
                    m_bufferLen = 0;
                }
            }
        }

        Digest Finish()
        {
            const uint64_t bits = m_totalBytes * 8;
            const uint8_t one = 0x80;
            const uint8_t zero = 0;
            Update(&one, 1);
            while (m_bufferLen != 56) Update(&zero, 1);

            uint8_t length[8];
            for (int i = 0; i < 8; ++i)
                length[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
            Update(length, 8);

            Digest out;
            for (int i = 0; i < 8; ++i)
            {
                out[i * 4 + 0] = static_cast<uint8_t>(m_state[i] >> 24);
                out[i * 4 + 1] = static_cast<uint8_t>(m_state[i] >> 16);
                out[i * 4 + 2] = static_cast<uint8_t>(m_state[i] >> 8);
                out[i * 4 + 3] = static_cast<uint8_t>(m_state[i]);
            }
            return out;
        }

    private:
        void Block(const uint8_t* b)
        {
            static const uint32_t K[64] = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = (uint32_t(b[i * 4]) << 24) | (uint32_t(b[i * 4 + 1]) << 16) |
                       (uint32_t(b[i * 4 + 2]) << 8) | uint32_t(b[i * 4 + 3]);
            for (int i = 16; i < 64; ++i)
            {
                const uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }

            uint32_t a = m_state[0], bb = m_state[1], c = m_state[2], d = m_state[3];
            uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
            for (int i = 0; i < 64; ++i)
            {
                const uint32_t S1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
                const uint32_t ch = (e & f) ^ (~e & g);
                const uint32_t t1 = h + S1 + ch + K[i] + w[i];
                const uint32_t S0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
                const uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
                const uint32_t t2 = S0 + maj;
                h = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
            }
            m_state[0] += a; m_state[1] += bb; m_state[2] += c; m_state[3] += d;
            m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
        }

        uint32_t m_state[8];
        uint8_t  m_buffer[64] = {};
        size_t   m_bufferLen = 0;
        uint64_t m_totalBytes = 0;
    };

    Digest HmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen)
    {
        uint8_t block[64] = {};
        if (keyLen > sizeof(block))
        {
            Sha256 k;
            k.Update(key, keyLen);
            const Digest kd = k.Finish();
            std::memcpy(block, kd.data(), kd.size());
        }
        else
        {
            std::memcpy(block, key, keyLen);
        }

        uint8_t ipad[64], opad[64];
        for (int i = 0; i < 64; ++i)
        {
            ipad[i] = block[i] ^ 0x36;
            opad[i] = block[i] ^ 0x5c;
        }

        Sha256 inner;
        inner.Update(ipad, sizeof(ipad));
        inner.Update(msg, msgLen);
        const Digest innerDigest = inner.Finish();

        Sha256 outer;
        outer.Update(opad, sizeof(opad));
        outer.Update(innerDigest.data(), innerDigest.size());
        return outer.Finish();
    }

    // One key for scrambling and a different one for signing, both made from the secret
    Digest DeriveKey(const char* label)
    {
        Sha256 h;
        h.Update(reinterpret_cast<const uint8_t*>(label), std::strlen(label));
        h.Update(SECRET, sizeof(SECRET));
        return h.Finish();
    }

    // Scrambles (or unscrambles, it is the same thing) the data with a key stream made from SHA-256
    void ApplyKeyStream(const Digest& key, const uint8_t* nonce, std::vector<uint8_t>& data)
    {
        for (size_t block = 0; block * 32 < data.size(); ++block)
        {
            const uint32_t counter = static_cast<uint32_t>(block);
            const uint8_t counterBytes[4] = { uint8_t(counter), uint8_t(counter >> 8), uint8_t(counter >> 16), uint8_t(counter >> 24) };

            Sha256 h;
            h.Update(key.data(), key.size());
            h.Update(nonce, NONCE_SIZE);
            h.Update(counterBytes, sizeof(counterBytes));
            const Digest stream = h.Finish();

            for (size_t j = 0; j < stream.size() && block * 32 + j < data.size(); ++j)
                data[block * 32 + j] ^= stream[j];
        }
    }

    // Compares without stopping at the first difference
    bool SameBytes(const uint8_t* a, const uint8_t* b, size_t len)
    {
        uint8_t diff = 0;
        for (size_t i = 0; i < len; ++i) diff |= a[i] ^ b[i];
        return diff == 0;
    }
}

// Each line of the (decrypted) file is: level name score, for example "1 ABC 123456"

std::vector<ScoreEntry> LoadScores()
{
    std::vector<ScoreEntry> scores;

    std::ifstream file(LEADERBOARD_FILE, std::ios::binary);
    if (!file) return scores; // no file yet
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Anything that is the wrong size, or doesn't start with the right marker, isn't ours
    if (bytes.size() < HEADER_SIZE + TAG_SIZE) return scores;
    if (std::memcmp(bytes.data(), MAGIC, sizeof(MAGIC)) != 0) return scores;

    // The signature has to match everything before it, otherwise the file was changed
    const size_t signedSize = bytes.size() - TAG_SIZE;
    const Digest macKey = DeriveKey("leaderboard-sign");
    const Digest expected = HmacSha256(macKey.data(), macKey.size(), bytes.data(), signedSize);
    if (!SameBytes(expected.data(), bytes.data() + signedSize, TAG_SIZE)) return scores;

    // Unscramble the scores
    std::vector<uint8_t> text(bytes.begin() + HEADER_SIZE, bytes.begin() + signedSize);
    ApplyKeyStream(DeriveKey("leaderboard-scramble"), bytes.data() + sizeof(MAGIC), text);

    std::istringstream in(std::string(text.begin(), text.end()));
    ScoreEntry entry;
    while (in >> entry.level >> entry.name >> entry.score)
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
    // Add the new score, then keep only the best few for each level.
    // If the file was edited, LoadScores gives nothing, so it starts again with just this score.
    std::vector<ScoreEntry> scores = LoadScores();
    scores.push_back(entry);
    std::stable_sort(scores.begin(), scores.end(),
        [](const ScoreEntry& a, const ScoreEntry& b) { return a.score > b.score; });

    std::map<int, int> kept; // how many scores of each level are kept so far
    std::ostringstream text;
    for (const ScoreEntry& s : scores)
    {
        if (kept[s.level] >= LEADERBOARD_SCORES_PER_LEVEL) continue;
        kept[s.level]++;
        text << s.level << ' ' << s.name << ' ' << s.score << '\n';
    }
    const std::string plain = text.str();

    // A new random nonce every save, so the same scores never look the same twice
    uint8_t nonce[NONCE_SIZE];
    std::random_device rd;
    for (size_t i = 0; i < NONCE_SIZE; ++i)
        nonce[i] = static_cast<uint8_t>(rd());

    // Layout: marker, nonce, scrambled scores, then the signature of all of that
    std::vector<uint8_t> scrambled(plain.begin(), plain.end());
    ApplyKeyStream(DeriveKey("leaderboard-scramble"), nonce, scrambled);

    std::vector<uint8_t> out(MAGIC, MAGIC + sizeof(MAGIC));
    out.insert(out.end(), nonce, nonce + NONCE_SIZE);
    out.insert(out.end(), scrambled.begin(), scrambled.end());

    const Digest macKey = DeriveKey("leaderboard-sign");
    const Digest tag = HmacSha256(macKey.data(), macKey.size(), out.data(), out.size());
    out.insert(out.end(), tag.begin(), tag.end());

    std::ofstream file(LEADERBOARD_FILE, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(file);
}
