#include "cpu.h"
#include "memory_directory.h"

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
        op_3xkk_se(chip8, x, kk);
        break;

    case 0x4000: /* 4xkk - SNE Vx, byte */
        op_4xkk_sne(chip8, x, kk);
        break;

    case 0x5000: /* 5xy0 - SE Vx, Vy */
        op_5xy0_se(chip8, x, y);
        break;

    case 0x6000: /* 6xkk - LD Vx, byte */
        op_6xkk_ld(chip8, x, kk);
        break;

    case 0x7000: /* 7xkk - ADD Vx, byte */
        op_7xkk_add(chip8, x, kk);
        break;

    case 0x8000: /* 8xy_ - ALU family */
        op_8xy_alu(chip8, x, y, n);
        break;

    case 0x9000: /* 9xy0 - SNE Vx, Vy */
        op_9xy0_sne(chip8, x, y);
        break;

    case 0xA000: /* Annn - LD I, addr */
        op_Annn_ld_i(chip8, nnn);
        break;

    case 0xB000: /* Bnnn - JP V0, addr */
        op_Bnnn_jp_v0(chip8, nnn);
        break;

    case 0xC000: /* Cxkk - RND Vx, byte */
        op_Cxkk_rnd(chip8, x, kk);
        break;

    case 0xD000: /* Dxyn - DRW Vx, Vy, nibble */
        op_Dxyn_drw(chip8, x, y, n);
        break;

    case 0xE000: /* Ex__ - SKP/SKNP Vx */
        op_Ex__key(chip8, x, kk);
        break;

    case 0xF000: /* Fx__ - timers, key wait, I-register, BCD, reg dump/load */
        op_Fx__misc(chip8, x, kk);
        break;

    default:
        fprintf(stderr, "chip8: unknown opcode 0x%04X\n", opcode);
        break;
    }
}