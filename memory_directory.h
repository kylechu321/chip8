#ifndef INC_8_BIT_INTERPRETOR_MEMORY_DIRECTORY_H
#define INC_8_BIT_INTERPRETOR_MEMORY_DIRECTORY_H

#include <stdint.h>
#include "cpu.h"

/* Executes one already-fetched opcode. cpu.c has already advanced PC. */
void chip8_execute(Chip8 *chip8, uint16_t opcode);

#endif //INC_8_BIT_INTERPRETOR_MEMORY_DIRECTORY_H