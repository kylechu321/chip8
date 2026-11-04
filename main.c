#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdbool.h>

#include "cpu.c"

// How large each Chip-8 pixel is drawn as, in real screen pixels.
#define PIXEL_SCALE 12

// How many Chip-8 instructions to execute per second. This is decoupled
// from the 60Hz timer/display refresh rate; most ROMs run well somewhere
// between 500-1000Hz.
#define INSTRUCTIONS_PER_SECOND 700

// Maps SDL scancodes to the Chip-8's 16-key hex keypad, using the common
// layout that mirrors the original keypad's physical position:

//   Chip-8 keypad      Maps to keyboard
//   1 2 3 C            1 2 3 4
//   4 5 6 D      ->    Q W E R
//   7 8 9 E            A S D F
//   A 0 B F            Z X C V

static const SDL_Scancode KEYMAP[CHIP8_NUM_KEYS] = {
    SDL_SCANCODE_X, /* 0 */
    SDL_SCANCODE_1, /* 1 */
    SDL_SCANCODE_2, /* 2 */
    SDL_SCANCODE_3, /* 3 */
    SDL_SCANCODE_Q, /* 4 */
    SDL_SCANCODE_W, /* 5 */
    SDL_SCANCODE_E, /* 6 */
    SDL_SCANCODE_A, /* 7 */
    SDL_SCANCODE_S, /* 8 */
    SDL_SCANCODE_D, /* 9 */
    SDL_SCANCODE_Z, /* A */
    SDL_SCANCODE_C, /* B */
    SDL_SCANCODE_4, /* C */
    SDL_SCANCODE_R, /* D */
    SDL_SCANCODE_F, /* E */
    SDL_SCANCODE_V, /* F */
};

// Reads the current keyboard state into chip8->keypad, and if a key was pressed while Fx0A was blocking,
// resolves that instruction.
static void update_keypad(Chip8 *chip8)
{
    const uint8_t *keyboard_state = SDL_GetKeyboardState(NULL);

    for (int key = 0; key < CHIP8_NUM_KEYS; key++) {
        chip8->keypad[key] = keyboard_state[KEYMAP[key]] ? 1 : 0;
    }

    if (chip8->awaiting_key) {
        for (int key = 0; key < CHIP8_NUM_KEYS; key++) {
            if (chip8->keypad[key]) {
                chip8->v[chip8->awaiting_key_register] = (uint8_t)key;
                chip8->awaiting_key = 0;
                break;
            }
        }
    }
}

// Redraws the entire display texture from chip8->display. Each Chip-8 pixel becomes a PIXEL_SCALE x PIXEL_SCALE filled rectangle.
static void render_display(SDL_Renderer *renderer, const Chip8 *chip8)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    for (int y = 0; y < CHIP8_DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < CHIP8_DISPLAY_WIDTH; x++) {
            if (chip8->display[y * CHIP8_DISPLAY_WIDTH + x]) {
                SDL_Rect pixel_rect = {
                    .x = x * PIXEL_SCALE,
                    .y = y * PIXEL_SCALE,
                    .w = PIXEL_SCALE,
                    .h = PIXEL_SCALE,
                };
                SDL_RenderFillRect(renderer, &pixel_rect);
            }
        }
    }

    SDL_RenderPresent(renderer);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path-to-rom>\n", argv[0]);
        return 1;
    }

    Chip8 chip8;
    chip8_init(&chip8);

    if (!chip8_load_rom(&chip8, argv[1])) {
        fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow(
       "Chip-8 Interpreter",
       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
       CHIP8_DISPLAY_WIDTH * PIXEL_SCALE,
       CHIP8_DISPLAY_HEIGHT * PIXEL_SCALE,
       SDL_WINDOW_SHOWN
   );
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    uint64_t previous_ticks = SDL_GetPerformanceCounter();
    double instruction_accumulator = 0.0;
    double timer_accumulator = 0.0;
    const double performance_frequency = (double)SDL_GetPerformanceFrequency();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }
        }
        uint64_t current_ticks = SDL_GetPerformanceCounter();
        double elapsed_seconds = (double)(current_ticks - previous_ticks) / performance_frequency;
        previous_ticks = current_ticks;

        update_keypad(&chip8);

        // Run CHIP-8 instructions at a fixed rate, independent of the host's frame rate,
        // using an accumulator so timing stays consistent even if a frame takes longer than expected.
        instruction_accumulator += elapsed_seconds;
        double seconds_per_instruction = 1.0 / INSTRUCTIONS_PER_SECOND;
        while (instruction_accumulator >= seconds_per_instruction) {
            chip8_emulate_cycle(&chip8);
            instruction_accumulator -= seconds_per_instruction;
        }

        // Delay and sound timers always tick at 60Hz regardless of instruction speed.
        timer_accumulator += elapsed_seconds;
        const double seconds_per_timer_tick = 1.0 / 60.0;
        while (timer_accumulator >= seconds_per_timer_tick) {
            chip8_tick_timers(&chip8);
            timer_accumulator -= seconds_per_timer_tick;
        }

        if (chip8.sound_timer > 0) {
            // Placeholder for audio:
        }

        if (chip8.draw_flag) {
            render_display(renderer, &chip8);
            chip8.draw_flag = 0;
        }

        // Yield briefly so we don't busy-spin the CPU.
        SDL_Delay(1);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
