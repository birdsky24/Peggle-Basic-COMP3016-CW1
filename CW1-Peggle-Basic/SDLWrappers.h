#pragma once

#include <SDL3/SDL.h>
#include <memory>

struct SDLWindowDeleter { void operator()(SDL_Window* w)   const { if (w) SDL_DestroyWindow(w); } };
struct SDLRendererDeleter { void operator()(SDL_Renderer* r) const { if (r) SDL_DestroyRenderer(r); } };
struct SDLTextureDeleter { void operator()(SDL_Texture* t)  const { if (t) SDL_DestroyTexture(t); } };
struct SDLSurfaceDeleter { void operator()(SDL_Surface* s)  const { if (s) SDL_DestroySurface(s); } };

using UniqueWindow = std::unique_ptr<SDL_Window, SDLWindowDeleter>;
using UniqueRenderer = std::unique_ptr<SDL_Renderer, SDLRendererDeleter>;
using UniqueTexture = std::unique_ptr<SDL_Texture, SDLTextureDeleter>;
using UniqueSurface = std::unique_ptr<SDL_Surface, SDLSurfaceDeleter>;