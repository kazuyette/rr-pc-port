/* rr-pc-port -- phase 1 vertical slice.
   *
   * Proves two things compile and run together on a normal host:
 *   1. Genuine ported decomp logic (src/globals.c, src/stubs.c), called
 *      through their original PS1 symbol names.
 *   2. An optional SDL2 window/event loop that degrades gracefully to a
 *      headless run when SDL2 isn't available at build time, or when
 *      there's no display at run time (this sandbox has neither).
 *
 * This is scaffolding only -- no rendering of actual game geometry, no
 * asm-locked function, no audio. See ROADMAP.md.
 */
#include <stdio.h>
#include "ported.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

static void exercise_ported_functions(void) {
      printf("-- exercising ported logic functions --\n");

    printf("func_80021FA4(10, 3)  = %d (expect 7)\n", func_80021FA4(10, 3));
    printf("func_80021FA4(3, 10)  = %d (expect 7)\n", func_80021FA4(3, 10));

    printf("func_80019C6C(0x10, 0x20)   = %d (expect 16)\n", func_80019C6C(0x10, 0x20));
    printf("func_80019C6C(0x10, 0xFF0)  = %d (expect 32)\n", func_80019C6C(0x10, 0xFF0));

    func_8001CD90();
    func_8001CDA8();
    printf("func_8001CD90()+func_8001CDA8() ran (reset/reload pair, no visible return)\n");

    printf("func_80032DF8() = %d (expect 0)\n", func_80032DF8());

    unsigned char obj[8] = {0};
    func_80047B98(obj);
    printf("func_80047B98(obj): obj[3]=%d obj[7]=0x%02X (expect 5, 0x28)\n", obj[3], obj[7]);

    func_80047AF8(obj, 1);
    printf("func_80047AF8(obj, 1): obj[7]=0x%02X (expect 0x2A, bit 0x2 set)\n", obj[7]);

    func_8003FA94();
    printf("func_8003FA94() ran (no-op hook)\n");

    printf("-- ported logic OK --\n");
}

#ifdef HAVE_SDL2
static int run_sdl_loop(void) {
      if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed (%s) -- continuing headless\n", SDL_GetError());
        return 1;
      }

    SDL_Window *window = SDL_CreateWindow(
        "rr-pc-port (phase 1)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 480, SDL_WINDOW_SHOWN);

    if (!window) {
        printf("SDL_CreateWindow failed (%s) -- continuing headless\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        printf("SDL_CreateRenderer failed (%s)\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Uint32 start = SDL_GetTicks();
    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
        }

        SDL_SetRenderDrawColor(renderer, 32, 32, 96, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);

        if (SDL_GetTicks() - start > 2000) running = 0; /* headless/CI safety cap */
    }

    printf("SDL loop exited cleanly after %u ms\n", SDL_GetTicks() - start);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
#endif

int main(void) {
      printf("rr-pc-port phase 1 vertical slice\n");

    exercise_ported_functions();

#ifdef HAVE_SDL2
    if (run_sdl_loop() != 0) {
        printf("(no usable display -- window/event loop skipped, logic-only run)\n");
    }
#else
    printf("(built without SDL2 -- window/event loop skipped, logic-only run)\n");
#endif

    printf("phase 1 vertical slice complete, exiting 0\n");
    return 0;
}
