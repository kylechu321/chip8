#include "cpu.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* The built-in hexadecimal font, 0-F, 5 bytes (8x5 pixels) each.
 * Stored at CHIP8_FONT_START so Fx29 can look characters up by index. */
static const uint8_t CHIP8_FONTSET[16 * CHIP8_FONT_BYTES_PER_CHAR] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, /* 0 */
    0x20, 0x60, 0x20, 0x20, 0x70, /* 1 */
    0xF0, 0x10, 0xF0, 0x80, 0xF0, /* 2 */
    0xF0, 0x10, 0xF0, 0x10, 0xF0, /* 3 */
    0x90, 0x90, 0xF0, 0x10, 0x10, /* 4 */
    0xF0, 0x80, 0xF0, 0x10, 0xF0, /* 5 */
    0xF0, 0x80, 0xF0, 0x90, 0xF0, /* 6 */
    0xF0, 0x10, 0x20, 0x40, 0x40, /* 7 */
    0xF0, 0x90, 0xF0, 0x90, 0xF0, /* 8 */
    0xF0, 0x90, 0xF0, 0x10, 0xF0, /* 9 */
    0xF0, 0x90, 0xF0, 0x90, 0x90, /* A */
    0xE0, 0x90, 0xE0, 0x90, 0xE0, /* B */
    0xF0, 0x80, 0x80, 0x80, 0xF0, /* C */
    0xE0, 0x90, 0x90, 0x90, 0xE0, /* D */
    0xF0, 0x80, 0xF0, 0x80, 0xF0, /* E */
    0xF0, 0x80, 0xF0, 0x80, 0x80, /* F */
};

void chip8_init(Chip8 *chip8)
{
    memset(chip8, 0, sizeof(*chip8));

    chip8->pc = CHIP8_PROGRAM_START;
    chip8->sp = 0;
    chip8->i = 0;

    memcpy(chip8->memory + CHIP8_FONT_START, CHIP8_FONTSET, sizeof(CHIP8_FONTSET));

    chip8->draw_flag = 1;

    /* Seed the RNG once here so Cxkk (RND) produces different sequences
     * across runs. */
    srand((unsigned int)time(NULL));
}

int chip8_load_rom(Chip8 *chip8, const char *path)
{
    FILE *rom = fopen(path, "rb");
    if (rom == NULL) {
        fprintf(stderr, "chip8_load_rom: could not open '%s'\n", path);
        return 0;
    }

    fseek(rom, 0, SEEK_END);
    long rom_size = ftell(rom);
    fseek(rom, 0, SEEK_SET);

    long max_size = CHIP8_MEMORY_SIZE - CHIP8_PROGRAM_START;
    if (rom_size <= 0 || rom_size > max_size) {
        fprintf(stderr, "chip8_load_rom: '%s' has invalid size (%ld bytes, max %ld)\n",
                path, rom_size, max_size);
        fclose(rom);
        return 0;
    }

    size_t bytes_read = fread(chip8->memory + CHIP8_PROGRAM_START, 1, (size_t)rom_size, rom);
    fclose(rom);

    if (bytes_read != (size_t)rom_size) {
        fprintf(stderr, "chip8_load_rom: failed to read all bytes of '%s'\n", path);
        return 0;
    }

    return 1;
}

void chip8_tick_timers(Chip8 *chip8)
{
    if (chip8->delay_timer > 0) {
        chip8->delay_timer--;
    }
    if (chip8->sound_timer > 0) {
        chip8->sound_timer--;
    }
}

/* Clears the display and sets draw_flag so the caller knows to redraw. */
static void op_00E0_cls(Chip8 *chip8)
{
    memset(chip8->display, 0, sizeof(chip8->display));
    chip8->draw_flag = 1;
}

/* Returns from a subroutine: pop the return address off the stack. */
static void op_00EE_ret(Chip8 *chip8)
{
    if (chip8->sp == 0) {
        fprintf(stderr, "chip8: stack underflow on RET\n");
        return;
    }
    chip8->sp--;
    chip8->pc = chip8->stack[chip8->sp];
}

/* Calls a subroutine at nnn: push the current PC, then jump. */
static void op_2nnn_call(Chip8 *chip8, uint16_t nnn)
{
    if (chip8->sp >= CHIP8_STACK_SIZE) {
        fprintf(stderr, "chip8: stack overflow on CALL\n");
        return;
    }
    chip8->stack[chip8->sp] = chip8->pc;
    chip8->sp++;
    chip8->pc = nnn;
}

/* Draws an n-byte sprite from memory[I] at (Vx, Vy), XORed onto the
 * display, wrapping at screen edges, and sets VF on any pixel collision. */
static void op_Dxyn_drw(Chip8 *chip8, uint8_t x, uint8_t y, uint8_t n)
{
    uint8_t start_x = chip8->v[x] % CHIP8_DISPLAY_WIDTH;
    uint8_t start_y = chip8->v[y] % CHIP8_DISPLAY_HEIGHT;
    chip8->v[0xF] = 0;

    for (uint8_t row = 0; row < n; row++) {
        uint8_t sprite_byte = chip8->memory[chip8->i + row];
        uint8_t py = (start_y + row) % CHIP8_DISPLAY_HEIGHT;

        for (uint8_t col = 0; col < 8; col++) {
            uint8_t sprite_pixel = (sprite_byte >> (7 - col)) & 0x1;
            if (sprite_pixel == 0) {
                continue;
            }

            uint8_t px = (start_x + col) % CHIP8_DISPLAY_WIDTH;
            uint16_t index = (uint16_t)py * CHIP8_DISPLAY_WIDTH + px;

            if (chip8->display[index] == 1) {
                chip8->v[0xF] = 1;
            }
            chip8->display[index] ^= 1;
        }
    }

    chip8->draw_flag = 1;
}

void chip8_emulate_cycle(Chip8 *chip8)
{
    /* If Fx0A is waiting for a key, do not fetch/advance until main.c
     * reports a key press via chip8->keypad and clears awaiting_key. */
    if (chip8->awaiting_key) {
        return;
    }

    uint16_t opcode = (uint16_t)(chip8->memory[chip8->pc] << 8) | chip8->memory[chip8->pc + 1];

    /* Common field extractions, named per the spec's variable conventions
     * (section 3.0): nnn/addr, n/nibble, x, y, kk/byte. */
    uint16_t nnn = opcode & 0x0FFF;
    uint8_t  n   = opcode & 0x000F;
    uint8_t  x   = (opcode & 0x0F00) >> 8;
    uint8_t  y   = (opcode & 0x00F0) >> 4;
    uint8_t  kk  = opcode & 0x00FF;

    /* Advance PC before executing so jump/call/skip instructions can
 * simply overwrite it without needing to "undo" this increment. */
    chip8->pc += 2;

    switch (opcode & 0xF000) {
    case 0x0000:
        switch (opcode) {
        case 0x00E0: op_00E0_cls(chip8); break;
        case 0x00EE: op_00EE_ret(chip8); break;
        default:
            /* 0nnn - SYS addr: ignored on modern interpreters. */
            break;
        }
        break;

    case 0x1000: /* 1nnn - JP addr */
        chip8->pc = nnn;
        break;

    case 0x2000: /* 2nnn - CALL addr */
        op_2nnn_call(chip8, nnn);
        break;

    case 0x3000: /* 3xkk - SE Vx, byte */
        if (chip8->v[x] == kk) {
            chip8->pc += 2;
        }
        break;

    case 0x4000: /* 4xkk - SNE Vx, byte */
        if (chip8->v[x] != kk) {
            chip8->pc += 2;
        }
        break;

    case 0x5000: /* 5xy0 - SE Vx, Vy */
        if (chip8->v[x] == chip8->v[y]) {
            chip8->pc += 2;
        }
        break;

    case 0x6000: /* 6xkk - LD Vx, byte */
        chip8->v[x] = kk;
        break;

    case 0x7000: /* 7xkk - ADD Vx, byte (no carry flag set) */
        chip8->v[x] = (uint8_t)(chip8->v[x] + kk);
        break;

    case 0x8000:
        switch (n) {
        case 0x0: /* 8xy0 - LD Vx, Vy */
            chip8->v[x] = chip8->v[y];
            break;
        case 0x1: /* 8xy1 - OR Vx, Vy */
            chip8->v[x] |= chip8->v[y];
            break;
        case 0x2: /* 8xy2 - AND Vx, Vy */
            chip8->v[x] &= chip8->v[y];
            break;
        case 0x3: /* 8xy3 - XOR Vx, Vy */
            chip8->v[x] ^= chip8->v[y];
            break;
        case 0x4: { /* 8xy4 - ADD Vx, Vy, set VF = carry */
            uint16_t sum = (uint16_t)chip8->v[x] + (uint16_t)chip8->v[y];
            chip8->v[0xF] = (sum > 0xFF) ? 1 : 0;
            chip8->v[x] = (uint8_t)sum;
            break;
        }case 0x5: { /* 8xy5 - SUB Vx, Vy, set VF = NOT borrow */
            uint8_t vf = (chip8->v[x] > chip8->v[y]) ? 1 : 0;
            chip8->v[x] = (uint8_t)(chip8->v[x] - chip8->v[y]);
            chip8->v[0xF] = vf;
            break;
        }
        case 0x6: { /* 8xy6 - SHR Vx, set VF = shifted-out bit */
            uint8_t vf = chip8->v[x] & 0x1;
            chip8->v[x] >>= 1;
            chip8->v[0xF] = vf;
            break;
        }
        case 0x7: { /* 8xy7 - SUBN Vx, Vy, set VF = NOT borrow */
            uint8_t vf = (chip8->v[y] > chip8->v[x]) ? 1 : 0;
            chip8->v[x] = (uint8_t)(chip8->v[y] - chip8->v[x]);
            chip8->v[0xF] = vf;
            break;
        }
        case 0xE: { /* 8xyE - SHL Vx, set VF = shifted-out bit */
            uint8_t vf = (chip8->v[x] & 0x80) >> 7;
            chip8->v[x] <<= 1;
            chip8->v[0xF] = vf;
            break;
        }
        default:
            fprintf(stderr, "chip8: unknown 8xy_ opcode 0x%04X\n", opcode);
            break;
        }
        break;

    case 0x9000: /* 9xy0 - SNE Vx, Vy */
        if (chip8->v[x] != chip8->v[y]) {
            chip8->pc += 2;
        }
        break;
    case 0xA000: /* Annn - LD I, addr */
        chip8->i = nnn;
        break;

    case 0xB000: /* Bnnn - JP V0, addr */
        chip8->pc = (uint16_t)(nnn + chip8->v[0]);
        break;

    case 0xC000: /* Cxkk - RND Vx, byte */
        chip8->v[x] = (uint8_t)(rand() % 256) & kk;
        break;

    case 0xD000: /* Dxyn - DRW Vx, Vy, nibble */
        op_Dxyn_drw(chip8, x, y, n);
        break;

    case 0xE000:
        switch (kk) {
        case 0x9E: /* Ex9E - SKP Vx */
            if (chip8->keypad[chip8->v[x] & 0xF]) {
                chip8->pc += 2;
            }
            break;
        case 0xA1: /* ExA1 - SKNP Vx */
            if (!chip8->keypad[chip8->v[x] & 0xF]) {
                chip8->pc += 2;
            }
            break;
        default:
            fprintf(stderr, "chip8: unknown Ex__ opcode 0x%04X\n", opcode);
            break;
        }
        break;

    case 0xF000:
        switch (kk) {
        case 0x07: /* Fx07 - LD Vx, DT */
            chip8->v[x] = chip8->delay_timer;
            break;
        case 0x0A: /* Fx0A - LD Vx, K (blocking wait for key press) */
            chip8->awaiting_key = 1;
            chip8->awaiting_key_register = x;
            break;
        case 0x15: /* Fx15 - LD DT, Vx */
            chip8->delay_timer = chip8->v[x];
            break;
        case 0x18: /* Fx18 - LD ST, Vx */
            chip8->sound_timer = chip8->v[x];
            break;
        case 0x1E: /* Fx1E - ADD I, Vx */
            chip8->i = (uint16_t)(chip8->i + chip8->v[x]);
            break;
        case 0x29: /* Fx29 - LD F, Vx: point I at the hex digit sprite */
            chip8->i = CHIP8_FONT_START + (chip8->v[x] & 0xF) * CHIP8_FONT_BYTES_PER_CHAR;
            break;
        case 0x33: { /* Fx33 - LD B, Vx: store BCD digits at I, I+1, I+2 */
            uint8_t value = chip8->v[x];
            chip8->memory[chip8->i]     = value / 100;
            chip8->memory[chip8->i + 1] = (value / 10) % 10;
            chip8->memory[chip8->i + 2] = value % 10;
            break;
        }
        case 0x55: /* Fx55 - LD [I], Vx: dump V0..Vx to memory */
            for (uint8_t reg = 0; reg <= x; reg++) {
                chip8->memory[chip8->i + reg] = chip8->v[reg];
            }
            break;
        case 0x65: /* Fx65 - LD Vx, [I]: load V0..Vx from memory */
            for (uint8_t reg = 0; reg <= x; reg++) {
                chip8->v[reg] = chip8->memory[chip8->i + reg];
            }
            break;
        default:
            fprintf(stderr, "chip8: unknown Fx__ opcode 0x%04X\n", opcode);
            break;
        }
        break;

    default:
        fprintf(stderr, "chip8: unknown opcode 0x%04X\n", opcode);
        break;
    }
}