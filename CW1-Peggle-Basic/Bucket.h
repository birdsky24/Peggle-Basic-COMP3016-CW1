#pragma once

#include <SDL3/SDL.h>

struct Triangle
{
    SDL_FPoint p[3];
};

class Bucket
{
public:
    Bucket();

    void Reset();
    void Update(float dt);
    void Render(SDL_Renderer* renderer) const;

    float GetX() const { return m_x; }
    void GetShape(SDL_FRect& rect, Triangle& left, Triangle& right) const;

private:
    float m_time = 0.0f;
    float m_x = 0.0f;

    static float CalculateProgress(float t);
};