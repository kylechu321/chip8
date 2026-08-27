#ifndef CHIP8_MEMORY_DIRECTORY_H
#define CHIP8_MEMORY_DIRECTORY_H

#include "cpu.h"
#include <stdint.h>

/* 00E0 - CLS */
void op_00E0_cls(Chip8 *chip8);

/* 00EE - RET */
void op_00EE_ret(Chip8 *chip8);

/* 2nnn - CALL addr */
void op_2nnn_call(Chip8 *chip8, uint16_t nnn);

/* 3xkk - SE Vx, byte */
void op_3xkk_se(Chip8 *chip8, uint8_t x, uint8_t kk);

/* 4xkk - SNE Vx, byte */
void op_4xkk_sne(Chip8 *chip8, uint8_t x, uint8_t kk);

/* 5xy0 - SE Vx, Vy */
void op_5xy0_se(Chip8 *chip8, uint8_t x, uint8_t y);

/* 6xkk - LD Vx, byte */
void op_6xkk_ld(Chip8 *chip8, uint8_t x, uint8_t kk);

/* 7xkk - ADD Vx, byte */
void op_7xkk_add(Chip8 *chip8, uint8_t x, uint8_t kk);

/* 8xy_ - ALU family, dispatched by low nibble n */
void op_8xy_alu(Chip8 *chip8, uint8_t x, uint8_t y, uint8_t n);

/* 9xy0 - SNE Vx, Vy */
void op_9xy0_sne(Chip8 *chip8, uint8_t x, uint8_t y);

/* Annn - LD I, addr */
void op_Annn_ld_i(Chip8 *chip8, uint16_t nnn);

/* Bnnn - JP V0, addr */
void op_Bnnn_jp_v0(Chip8 *chip8, uint16_t nnn);

/* Cxkk - RND Vx, byte */
void op_Cxkk_rnd(Chip8 *chip8, uint8_t x, uint8_t kk);

/* Dxyn - DRW Vx, Vy, nibble */
void op_Dxyn_drw(Chip8 *chip8, uint8_t x, uint8_t y, uint8_t n);

/* Ex__ - SKP/SKNP Vx, dispatched by low byte kk */
void op_Ex__key(Chip8 *chip8, uint8_t x, uint8_t kk);

/* Fx__ - all Fx opcodes (timers, key wait, I-register, BCD, reg dump/load),
 * dispatched by low byte kk */
void op_Fx__misc(Chip8 *chip8, uint8_t x, uint8_t kk);

#endif /* CHIP8_MEMORY_DIRECTORY_H */