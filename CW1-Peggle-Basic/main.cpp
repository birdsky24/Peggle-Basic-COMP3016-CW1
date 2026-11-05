#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "Game.h"

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    Game game;
    if (!game.Initialize())
    {
        return 1;
    }

    game.Run();
    return 0;
}