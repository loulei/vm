/*
 ============================================================================
 Name        : vm.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>
#include <assert.h>

enum {
	R_R0 = 0, R_R1, R_R2, R_R3, R_R4, R_R5, R_R6, R_R7, R_PC, /* program counter */
	R_COND, R_COUNT
};

enum {
	OP_BR = 0, /* branch */
	OP_ADD, /* add  */
	OP_LD, /* load */
	OP_ST, /* store */
	OP_JSR, /* jump register */
	OP_AND, /* bitwise and */
	OP_LDR, /* load register */
	OP_STR, /* store register */
	OP_RTI, /* unused */
	OP_NOT, /* bitwise not */
	OP_LDI, /* load indirect */
	OP_STI, /* store indirect */
	OP_JMP, /* jump */
	OP_RES, /* reserved (unused) */
	OP_LEA, /* load effective address */
	OP_TRAP /* execute trap */
};

enum {
	FL_POS = 1 << 0, /* P */
	FL_ZRO = 1 << 1, /* Z */
	FL_NEG = 1 << 2, /* N */
};

uint16_t memory[UINT16_MAX];
uint16_t regs[R_COUNT];

uint16_t sign_extend(uint16_t x, int bit_count) {
	if ((x >> (bit_count - 1)) & 1) {
		x |= (0xFFFF << bit_count);
	}
	return x;
}

void update_flags(uint16_t r) {
	if (regs[r] == 0) {
		regs[R_COND] = FL_ZRO;
	} else if (regs[r] >> 15) /* a 1 in the left-most bit indicates negative */
	{
		regs[R_COND] = FL_NEG;
	} else {
		regs[R_COND] = FL_POS;
	}
}

void ins(uint16_t instr) {
	uint16_t op = instr >> 12;
	switch (op) {
	case OP_ADD: {
		uint16_t dr = (instr >> 9) & 0x7;
		uint16_t sr = (instr >> 6) & 0x7;
		uint16_t imm_flag = (instr >> 5) & 0x1;
		if (imm_flag) {
			regs[dr] = regs[sr] + sign_extend(instr & 0x1F, 5);
		} else {
			uint16_t sr1 = instr & 0x7;
			regs[dr] = regs[sr] + regs[sr1];
		}
		update_flags(regs[dr]);
	}
		break;
	case OP_AND: {
		uint16_t dr = (instr >> 9) & 0x7;
		uint16_t sr = (instr >> 6) & 0x7;
		uint16_t imm_flag = (instr >> 5) & 0x1;
		if (imm_flag) {
			regs[dr] = regs[sr] & sign_extend(instr & 0x1F, 5);
		} else {
			uint16_t sr1 = instr & 0x7;
			regs[dr] = regs[sr] & regs[sr1];
		}
	}
		break;
	case OP_BR: {
		uint16_t cond_flag = (instr >> 9) & 0x7;
		if (cond_flag & regs[R_COND]) {
			uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
			regs[R_PC] += pc_offset;
		}
	}
		break;
	case OP_JMP: {
		uint16_t r1 = (instr >> 6) & 0x7;
		regs[R_PC] = regs[r1];
	}
		break;
	case OP_JSR: {
		uint16_t flag = (instr >> 11) & 0x1;
		regs[R_R7] = regs[R_PC];
		if (flag) {
			uint16_t pc_offset = sign_extend(instr & 0x7FF, 11);
			regs[R_PC] += pc_offset;
		} else {
			uint16_t base_r = (instr >> 6) & 0x7;
			regs[R_PC] = regs[base_r];
		}
	}
		break;
	default:
		break;
	}
}

void reset() {
	for (int i = 0; i < R_COUNT; i++) {
		regs[i] = i;
	}

	for (int i = 0; i < 65535; i++) {
		memory[i] = 0;
	}
}

void output_assert(int expression, const char *s) {
	assert(expression);
	printf("%s\n", s);
}

int main(void) {
	uint16_t instr;
	printf("¿ªÊ¼²âÊÔ£º\n");
	regs[1] = 0;
	regs[2] = 1;
	regs[3] = 2;
	instr = 0b0001001010000011;
	ins(instr);
	assert(regs[1] == 3);
	reset();

	instr = 0b0001001111101000;
	regs[1] = 0;
	regs[7] = 10;
	ins(instr);
	output_assert(regs[1] == 18, "OP_ADD ¡Ì");
	reset();

	instr = 0b0101000001000010;
	ins(instr);
	assert(regs[0] == 0);
	reset();
	instr = 0b0101001111100111;
	ins(instr);
	output_assert(regs[1] == 0x7, "OP_AND ¡Ì");
	reset();

	regs[R_PC] = 10;
	regs[R_COND] = 0b100;
	instr = 0b0000100000000010;
	ins(instr);
	output_assert(regs[R_PC] == 12, "OP_BR ¡Ì");
	reset();

	instr = 0b1100000111000010;
	ins(instr);
	output_assert(regs[R_PC] == 7, "OP_JMP ¡Ì");
	reset();

	// JSR
	instr = 0b0100100000000100;
	ins(instr);
	output_assert(regs[R_PC] == 12, "OP_JSR ¡Ì");
	reset();

	// JSRR
	instr = 0b0100000110000000;
	ins(instr);
	output_assert(regs[R_PC] == 6, "OP_JSRR ¡Ì");
	reset();

	return EXIT_SUCCESS;
}
