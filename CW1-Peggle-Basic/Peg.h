#pragma once

struct Peg
{
    float x = 0.0f, y = 0.0f;
    bool  hit = false; // marked for deletion, set the first time the ball touches it
    bool  removed = false; // deleted, so it is no longer drawn or collided with
};