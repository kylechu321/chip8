#include "memory_directory.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void chip8_execute(Chip8 *chip8, uint16_t opcode)
{
    // Common field extractions, named per the spec's variable conventions
    // (section 3.0): nnn/addr, n/nibble, x, y, kk/byte.
    uint16_t nnn = opcode & 0x0FFF;
    uint8_t  n   = opcode & 0x000F;
    uint8_t  x   = (opcode & 0x0F00) >> 8;
    uint8_t  y   = (opcode & 0x00F0) >> 4;
    uint8_t  kk  = opcode & 0x00FF;

    switch (opcode & 0xF000) {
    case 0x0000:
        switch (opcode) {
        case 0x00E0:
            // Clears the display and sets draw_flag so the caller knows to redraw.
            memset(chip8->display, 0, sizeof(chip8->display));
            chip8->draw_flag = 1;
            break;
        case 0x00EE:
            // Returns from a subroutine: pop the return address off the stack.
            if (chip8->sp == 0) {
                fprintf(stderr, "chip8: stack underflow on RET\n");
                break;
            }
            chip8->sp--;
            chip8->pc = chip8->stack[chip8->sp];
            break;
        default:
            // 0nnn - SYS addr: ignored on modern interpreters.
            break;
        }
        break;

    // 1nnn - JP addr
    case 0x1000:
        chip8->pc = nnn;
        break;

    // 2nnn - CALL addr
    case 0x2000:
        // Calls a subroutine at nnn: push the current PC, then jump.
        if (chip8->sp >= CHIP8_STACK_SIZE) {
            fprintf(stderr, "chip8: stack overflow on CALL\n");
            break;
        }
        chip8->stack[chip8->sp] = chip8->pc;
        chip8->sp++;
        chip8->pc = nnn;
        break;

    // 3xkk - SE Vx, byte
    case 0x3000:
        if (chip8->v[x] == kk) {
            chip8->pc += 2;
        }
        break;

    // 4xkk - SNE Vx, byte
    case 0x4000:
        if (chip8->v[x] != kk) {
            chip8->pc += 2;
        }
        break;

    // 5xy0 - SE Vx, Vy
    case 0x5000:
        if (chip8->v[x] == chip8->v[y]) {
            chip8->pc += 2;
        }
        break;

    // 6xkk - LD Vx, byte
    case 0x6000:
        chip8->v[x] = kk;
        break;

    // 7xkk - ADD Vx, byte (no carry flag set)
    case 0x7000:
        chip8->v[x] = (uint8_t)(chip8->v[x] + kk);
        break;

    case 0x8000:
        switch (n) {
        // 8xy0 - LD Vx, Vy
        case 0x0:
            chip8->v[x] = chip8->v[y];
            break;
        // 8xy1 - OR Vx, Vy
        case 0x1:
            chip8->v[x] |= chip8->v[y];
            break;
        // 8xy2 - AND Vx, Vy
        case 0x2:
            chip8->v[x] &= chip8->v[y];
            break;
        // 8xy3 - XOR Vx, Vy
        case 0x3:
            chip8->v[x] ^= chip8->v[y];
            break;
        // 8xy4 - ADD Vx, Vy, set VF = carry
        case 0x4: {
            uint16_t sum = (uint16_t)chip8->v[x] + (uint16_t)chip8->v[y];
            chip8->v[0xF] = (sum > 0xFF) ? 1 : 0;
            chip8->v[x] = (uint8_t)sum;
            break;
        }
        // 8xy5 - SUB Vx, Vy, set VF = NOT borrow
        case 0x5: {
            uint8_t vf = (chip8->v[x] > chip8->v[y]) ? 1 : 0;
            chip8->v[x] = (uint8_t)(chip8->v[x] - chip8->v[y]);
            chip8->v[0xF] = vf;
            break;
        }
        // 8xy6 - SHR Vx, set VF = shifted-out bit
        case 0x6: {
            uint8_t vf = chip8->v[x] & 0x1;
            chip8->v[x] >>= 1;
            chip8->v[0xF] = vf;
            break;
        }
        // 8xy7 - SUBN Vx, Vy, set VF = NOT borrow
        case 0x7: {
            uint8_t vf = (chip8->v[y] > chip8->v[x]) ? 1 : 0;
            chip8->v[x] = (uint8_t)(chip8->v[y] - chip8->v[x]);
            chip8->v[0xF] = vf;
            break;
        }
        // 8xyE - SHL Vx, set VF = shifted-out bit
        case 0xE: {
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

    // 9xy0 - SNE Vx, Vy
    case 0x9000:
        if (chip8->v[x] != chip8->v[y]) {
            chip8->pc += 2;
        }
        break;

    // Annn - LD I, addr
    case 0xA000:
        chip8->i = nnn;
        break;

    // Bnnn - JP V0, addr
    case 0xB000:
        chip8->pc = (uint16_t)(nnn + chip8->v[0]);
        break;

    // Cxkk - RND Vx, byte
    case 0xC000:
        chip8->v[x] = (uint8_t)(rand() % 256) & kk;
        break;

    // Dxyn - DRW Vx, Vy, nibble
    case 0xD000: {
        // Draws an n-byte sprite from memory[I] at (Vx, Vy), XORed onto the
        // display, wrapping at screen edges, and sets VF on any pixel collision.
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
        break;
    }

    case 0xE000:
        switch (kk) {
        // Ex9E - SKP Vx
        case 0x9E:
            if (chip8->keypad[chip8->v[x] & 0xF]) {
                chip8->pc += 2;
            }
            break;
        // ExA1 - SKNP Vx
        case 0xA1:
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
        // Fx07 - LD Vx, DT
        case 0x07:
            chip8->v[x] = chip8->delay_timer;
            break;
        // Fx0A - LD Vx, K (blocking wait for key press)
        case 0x0A:
            chip8->awaiting_key = 1;
            chip8->awaiting_key_register = x;
            break;
        // Fx15 - LD DT, Vx
        case 0x15:
            chip8->delay_timer = chip8->v[x];
            break;
        // Fx18 - LD ST, Vx
        case 0x18:
            chip8->sound_timer = chip8->v[x];
            break;
        // Fx1E - ADD I, Vx
        case 0x1E:
            chip8->i = (uint16_t)(chip8->i + chip8->v[x]);
            break;
        // Fx29 - LD F, Vx: point I at the hex digit sprite
        case 0x29:
            chip8->i = CHIP8_FONT_START + (chip8->v[x] & 0xF) * CHIP8_FONT_BYTES_PER_CHAR;
            break;
        // Fx33 - LD B, Vx: store BCD digits at I, I+1, I+2
        case 0x33: {
            uint8_t value = chip8->v[x];
            chip8->memory[chip8->i]     = value / 100;
            chip8->memory[chip8->i + 1] = (value / 10) % 10;
            chip8->memory[chip8->i + 2] = value % 10;
            break;
        }
        // Fx55 - LD [I], Vx: dump V0..Vx to memory
        case 0x55:
            for (uint8_t reg = 0; reg <= x; reg++) {
                chip8->memory[chip8->i + reg] = chip8->v[reg];
            }
            break;
        // Fx65 - LD Vx, [I]: load V0..Vx from memory
        case 0x65:
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