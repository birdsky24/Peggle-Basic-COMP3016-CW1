#pragma once

enum class PegType
{
    Blue,
    Orange,
    Purple
};

struct Peg
{
    float x = 0.0f, y = 0.0f;
    PegType type = PegType::Blue;
    bool  hit = false; // marked for deletion, set the first time the ball touches it
    bool  removed = false; // deletes the peg
};
