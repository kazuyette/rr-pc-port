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
#include <stdlib.h>
#include <string.h>
#include "ported.h"
#include "ported_logic.h"

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

static void exercise_ported_logic_round1(void) {
    printf("-- exercising phase 2 round 1 ported logic (src/ported_logic.c) --\n");

    printf("func_80055800() = %d (expect 3)\n", func_80055800());
    printf("func_80055808() = %d (expect 1)\n", func_80055808());

    printf("func_80059040() = %d (expect 1, global starts at 0)\n", func_80059040());

    func_8004985C(0x1234);
    printf("func_8004985C(0x1234) ran, no direct getter (paired global set)\n");

    unsigned char obj[16] = {0};
    func_80047B48(obj);
    printf("func_80047B48(obj): obj[3]=%d obj[7]=0x%02X (expect 4, 0x20)\n", obj[3], obj[7]);

    func_80047B20(obj, 1);
    printf("func_80047B20(obj,1): obj[7]=0x%02X (expect 0x21, bit0 set)\n", obj[7]);
    func_80047B20(obj, 0);
    printf("func_80047B20(obj,0): obj[7]=0x%02X (expect 0x20, bit0 cleared)\n", obj[7]);

    printf("func_80045738(42) = %d (expect 0, old value)\n", func_80045738(42));
    printf("func_80045738(7)  = %d (expect 42, previous value)\n", func_80045738(7));

    printf("func_80047920(0x30, 0x2) = 0x%X (expect 0x83)\n", func_80047920(0x30, 2));

    printf("func_800554EC(0x10) = %d (expect 0, in range)\n", func_800554EC(0x10));
    printf("func_800554EC(0x30) = %d (expect 1, out of range)\n", func_800554EC(0x30));

    unsigned int word = 0;
    unsigned char cnt[8] = {0, 0, 0, 5, 0, 0, 0, 0};
    int rc = func_80047D24(cnt, &word);
    printf("func_80047D24: rc=%d cnt[3]=%d word=0x%X (expect 0, 6, 0)\n", rc, cnt[3], word);

    printf("-- phase 2 round 1 ported logic OK --\n");
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

    /* CI/headless runs force SDL_VIDEODRIVER=dummy (no real window ever
     * appears, nothing for a human to look at) -- keep the old 2s safety
     * cap there so the workflow can't hang. On a real display, stay open
     * until the user closes the window (or Escape), same as any normal
     * app -- flashing open/closed after 2s was confusing on a real
     * desktop, it looked like a crash rather than "working as intended". */
    const char *video_driver = getenv("SDL_VIDEODRIVER");
    int is_headless = video_driver && strcmp(video_driver, "dummy") == 0;
    if (!is_headless) {
        printf("window open -- close it (or press Escape) to exit\n");
    }

    Uint32 start = SDL_GetTicks();
    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
        }

        SDL_SetRenderDrawColor(renderer, 32, 32, 96, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);

        if (is_headless && SDL_GetTicks() - start > 2000) running = 0; /* headless/CI safety cap */
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
    exercise_ported_logic_round1();

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
