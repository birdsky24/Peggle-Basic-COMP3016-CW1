#include "Bucket.h"
#include "Config.h"
#include <algorithm>

Bucket::Bucket()
{
    Reset();
}

void Bucket::Reset()
{
    m_time = BUCKET_SWEEP_TIME * 0.5f;
    m_x = LAUNCHER_X;
}

float Bucket::CalculateProgress(float t)
{
    const float dwell = BUCKET_DWELL_TIME;
    const float ramp = BUCKET_RAMP_TIME;
    const float cruise = BUCKET_SWEEP_TIME - dwell - 2.0f * ramp;
    const float total = ramp + cruise;

    auto rampDistance = [](float u) { return u * u * u - u * u * u * u / 2.0f; };

    t -= dwell;
    if (t <= 0.0f) return 0.0f;
    if (t < ramp) return (ramp * rampDistance(t / ramp)) / total;
    if (t < ramp + cruise) return (ramp * 0.5f + (t - ramp)) / total;

    const float u = std::min(1.0f, (t - ramp - cruise) / ramp);
    return (ramp * 0.5f + cruise + ramp * (0.5f - rampDistance(1.0f - u))) / total;
}

void Bucket::Update(float dt)
{
    m_time += dt;
    if (m_time >= 2.0f * BUCKET_SWEEP_TIME)
        m_time -= 2.0f * BUCKET_SWEEP_TIME;

    const bool returning = m_time >= BUCKET_SWEEP_TIME;
    const float t = returning ? m_time - BUCKET_SWEEP_TIME : m_time;
    float p = CalculateProgress(t);
    if (returning) p = 1.0f - p;

    const float minX = PLAY_LEFT + BUCKET_WIDTH / 2.0f;
    const float maxX = PLAY_RIGHT - BUCKET_WIDTH / 2.0f;
    m_x = minX + p * (maxX - minX);
}

void Bucket::GetShape(SDL_FRect& rect, Triangle& left, Triangle& right) const
{
    rect = { m_x - BUCKET_WIDTH / 2.0f, PLAY_BOTTOM - BUCKET_HEIGHT, BUCKET_WIDTH, BUCKET_HEIGHT };
    const float l = rect.x;
    const float r = rect.x + rect.w;
    const float apex = PLAY_BOTTOM - BUCKET_TRI_HEIGHT;

    left = { { { l, apex }, { l - BUCKET_TRI_WIDTH, PLAY_BOTTOM }, { l, PLAY_BOTTOM } } };
    right = { { { r, apex }, { r, PLAY_BOTTOM }, { r + BUCKET_TRI_WIDTH, PLAY_BOTTOM } } };
}

void Bucket::Render(SDL_Renderer* renderer) const
{
    SDL_FRect bucketRect;
    Triangle leftTri, rightTri;
    GetShape(bucketRect, leftTri, rightTri);

    SDL_SetRenderDrawColor(renderer, 205, 215, 235, 255);
    SDL_RenderFillRect(renderer, &bucketRect);

    const SDL_FColor triColour = { 0.62f, 0.68f, 0.85f, 1.0f };
    for (const Triangle* tri : { &leftTri, &rightTri })
    {
        SDL_Vertex v[3] = {};
        for (int i = 0; i < 3; ++i)
        {
            v[i].position = tri->p[i];
            v[i].color = triColour;
        }
        SDL_RenderGeometry(renderer, nullptr, v, 3, nullptr, 0);
    }
}