#ifndef INC_8_BIT_INTERPRETOR_CPU_H
#define INC_8_BIT_INTERPRETOR_CPU_H

#include <stdint.h>

#define CHIP8_MEMORY_SIZE   4096
#define CHIP8_NUM_REGISTERS 16
#define CHIP8_STACK_SIZE    16
#define CHIP8_NUM_KEYS      16
#define CHIP8_DISPLAY_WIDTH  64
#define CHIP8_DISPLAY_HEIGHT 32
#define CHIP8_PROGRAM_START 0x200
#define CHIP8_FONT_START    0x000
#define CHIP8_FONT_BYTES_PER_CHAR 5

typedef struct {
    uint8_t  memory[CHIP8_MEMORY_SIZE];
    uint8_t  v[CHIP8_NUM_REGISTERS];
    uint16_t i;
    uint16_t pc;

    uint8_t  sp;
    uint16_t stack[CHIP8_STACK_SIZE];

    uint8_t  delay_timer;
    uint8_t  sound_timer;

    uint8_t  display[CHIP8_DISPLAY_WIDTH * CHIP8_DISPLAY_HEIGHT];
    uint8_t  keypad[CHIP8_NUM_KEYS];

    int draw_flag;

    int     awaiting_key;
    uint8_t awaiting_key_register;
} Chip8;

/* Resets all state and loads the built-in hexadecimal font into memory. */
void chip8_init(Chip8 *chip8);

/* Loads a ROM file from disk into memory starting at CHIP8_PROGRAM_START.
 * Returns 1 on success, 0 on failure (file missing or too large). */
int chip8_load_rom(Chip8 *chip8, const char *path);

/* Fetches, decodes, and executes exactly one instruction. */
void chip8_emulate_cycle(Chip8 *chip8);

/* Decrements the delay and sound timers by 1 if they are non-zero.
 * Should be called at 60Hz, independent of the CPU instruction rate. */
void chip8_tick_timers(Chip8 *chip8);


#endif //INC_8_BIT_INTERPRETOR_CPU_H
