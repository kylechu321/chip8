#include "memory_directory.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 00E0 - CLS */
void op_00E0_cls(Chip8 *chip8)
{
    memset(chip8->display, 0, sizeof(chip8->display));
    chip8->draw_flag = 1;
}

/* 00EE - RET */
void op_00EE_ret(Chip8 *chip8)
{
    if (chip8->sp == 0) {
        fprintf(stderr, "chip8: stack underflow on RET\n");
        return;
    }
    chip8->sp--;
    chip8->pc = chip8->stack[chip8->sp];
}

/* 2nnn - CALL addr */
void op_2nnn_call(Chip8 *chip8, uint16_t nnn)
{
    if (chip8->sp >= CHIP8_STACK_SIZE) {
        fprintf(stderr, "chip8: stack overflow on CALL\n");
        return;
    }
    chip8->stack[chip8->sp] = chip8->pc;
    chip8->sp++;
    chip8->pc = nnn;
}

/* 3xkk - SE Vx, byte */
void op_3xkk_se(Chip8 *chip8, uint8_t x, uint8_t kk)
{
    if (chip8->v[x] == kk) {
        chip8->pc += 2;
    }
}

/* 4xkk - SNE Vx, byte */
void op_4xkk_sne(Chip8 *chip8, uint8_t x, uint8_t kk)
{
    if (chip8->v[x] != kk) {
        chip8->pc += 2;
    }
}

/* 5xy0 - SE Vx, Vy */
void op_5xy0_se(Chip8 *chip8, uint8_t x, uint8_t y)
{
    if (chip8->v[x] == chip8->v[y]) {
        chip8->pc += 2;
    }
}

/* 6xkk - LD Vx, byte */
void op_6xkk_ld(Chip8 *chip8, uint8_t x, uint8_t kk)
{
    chip8->v[x] = kk;
}

/* 7xkk - ADD Vx, byte (no carry flag set) */
void op_7xkk_add(Chip8 *chip8, uint8_t x, uint8_t kk)
{
    chip8->v[x] = (uint8_t)(chip8->v[x] + kk);
}

/* 8xy_ - ALU family, dispatched by low nibble n */
void op_8xy_alu(Chip8 *chip8, uint8_t x, uint8_t y, uint8_t n)
{
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
    }
    case 0x5: { /* 8xy5 - SUB Vx, Vy, set VF = NOT borrow */
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
        fprintf(stderr, "chip8: unknown 8xy_ opcode (n=0x%X)\n", n);
        break;
    }
}

/* 9xy0 - SNE Vx, Vy */
void op_9xy0_sne(Chip8 *chip8, uint8_t x, uint8_t y)
{
    if (chip8->v[x] != chip8->v[y]) {
        chip8->pc += 2;
    }
}

/* Annn - LD I, addr */
void op_Annn_ld_i(Chip8 *chip8, uint16_t nnn)
{
    chip8->i = nnn;
}

/* Bnnn - JP V0, addr */
void op_Bnnn_jp_v0(Chip8 *chip8, uint16_t nnn)
{
    chip8->pc = (uint16_t)(nnn + chip8->v[0]);
}

/* Cxkk - RND Vx, byte */
void op_Cxkk_rnd(Chip8 *chip8, uint8_t x, uint8_t kk)
{
    chip8->v[x] = (uint8_t)(rand() % 256) & kk;
}

/* Dxyn - DRW Vx, Vy, nibble: draws an n-byte sprite from memory[I] at
 * (Vx, Vy), XORed onto the display, wrapping at screen edges, and sets
 * VF on any pixel collision. */
void op_Dxyn_drw(Chip8 *chip8, uint8_t x, uint8_t y, uint8_t n)
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

/* Ex__ - SKP/SKNP Vx, dispatched by low byte kk */
void op_Ex__key(Chip8 *chip8, uint8_t x, uint8_t kk)
{
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
        fprintf(stderr, "chip8: unknown Ex__ opcode (kk=0x%02X)\n", kk);
        break;
    }
}

/* Fx__ - all Fx opcodes, dispatched by low byte kk */
void op_Fx__misc(Chip8 *chip8, uint8_t x, uint8_t kk)
{
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
        fprintf(stderr, "chip8: unknown Fx__ opcode 0x%02X\n", kk);
        break;
    }
}