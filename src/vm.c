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

enum {
	TRAP_GETC = 0x20, // get character from keyboard, not echoed onto the terminal
	TRAP_OUT = 0x21,     // output a character
	TRAP_PUTS = 0x22,     // output a word string
	TRAP_IN = 0x23,     // get character from keyboard, echoed onto the terminal
	TRAP_PUTSP = 0x24,     // output a byte string
	TRAP_HALT = 0x25,     // halt the program
};

enum {
    MR_KBSR = 0xFE00,       // keyboard status
    MR_KBDR = 0xFE02        // keyboard data
};

uint16_t memory[UINT16_MAX];
uint16_t regs[R_COUNT];
int running = 1;

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

uint16_t check_key(){
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	return select(STDIN_FILENO+1, &readfds, NULL, NULL, &timeout) != 0;
}

uint16_t mem_read(uint16_t address) {
	if(address == MR_KBSR){
		if(check_key()){
			memory[MR_KBSR] = (1 << 15);
			memory[MR_KBDR] = getchar();
		}else{
			memory[MR_KBSR] = 0;
		}
	}
	return memory[address];
}

void mem_write(uint16_t address, uint16_t value) {
	memory[address] = value;
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
		update_flags(dr);
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
		update_flags(dr);
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
	case OP_LD: {
		uint16_t dr = (instr >> 9) & 0x7;
		uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
		uint16_t address = regs[R_PC] + pc_offset;
		regs[dr] = mem_read(address);
		update_flags(dr);
	}
		break;
	case OP_LDI: {
		uint16_t dr = (instr >> 9) & 0x7;
		uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
		uint16_t add = regs[R_PC] + pc_offset;
		uint16_t add1 = mem_read(add);
		regs[dr] = mem_read(add1);
		update_flags(dr);
	}
		break;
	case OP_LDR: {
		uint16_t dr = (instr >> 9) & 0x7;
		uint16_t base_r = (instr >> 6) & 0x7;
		uint16_t pc_offset = sign_extend(instr & 0x3F, 6);
		regs[dr] = mem_read(regs[base_r] + pc_offset);
		update_flags(dr);
	}
		break;
	case OP_LEA: {
		uint16_t dr = (instr >> 9) & 0x7;
		uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
		regs[dr] = regs[R_PC] + pc_offset;
		update_flags(dr);
	}
		break;
	case OP_NOT: {
		uint16_t dr = (instr >> 9) & 0x7;
		uint16_t sr = (instr >> 6) & 0x7;
		regs[dr] = ~regs[sr];
		update_flags(dr);
	}
		break;
	case OP_ST: {
		uint16_t sr = (instr >> 9) & 0x7;
		uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
		uint16_t add = regs[R_PC] + pc_offset;
		mem_write(add, regs[sr]);
	}
		break;
	case OP_STI: {
		uint16_t sr = (instr >> 9) & 0x7;
		uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
		uint16_t add = regs[R_PC] + pc_offset;
		uint16_t add1 = mem_read(add);
		mem_write(add1, regs[sr]);
	}
		break;
	case OP_STR: {
		uint16_t sr = (instr >> 9) & 0x7;
		uint16_t pc_offset = sign_extend(instr & 0x3F, 6);
		uint16_t base_r = (instr >> 6) & 0x7;
		mem_write(regs[base_r] + pc_offset, regs[sr]);
	}
		break;
	case OP_TRAP: {
		uint16_t trap_vect = instr & 0xFF;
		switch (trap_vect) {
		case TRAP_GETC: {
			regs[R_R0] = (uint16_t) getchar();
		}
			break;
		case TRAP_OUT: {
			putc((char) regs[R_R0], stdout);
			fflush(stdout);
		}
			break;
		case TRAP_PUTS: {
			uint16_t *c = memory + regs[R_R0];
			while (*c) {
				putc((char) *c, stdout);
				++c;
			}
			fflush(stdout);
		}
			break;
		case TRAP_IN: {
			printf("Enter a character:");
			char c = getchar();
			putc(c, stdout);
			regs[R_R0] = (uint16_t) c;
		}
			break;
		case TRAP_PUTSP: {
			uint16_t *c = memory + regs[R_R0];
			while (*c) {
				char cha = (*c) & 0xFF;
				putc(cha, stdout);
				char cha1 = (*c) >> 8;
				if (cha1) {
					putc(cha1, stdout);
				}
				c++;
			}
			fflush(stdout);
		}
			break;
		case TRAP_HALT: {
			puts("HALT");
			fflush(stdout);
			running = 0;
		}
			break;
		default:
			break;
		}
	}
		break;
	default:
		abort();
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

uint16_t swap16(uint16_t x){
	return (x << 8) | (x >> 8);
}

void read_image_file(FILE *file){
	uint16_t origin;
	fread(&origin, sizeof(origin), 1, file);
	origin = swap16(origin);

	uint16_t max_read = UINT16_MAX - origin;
	uint16_t *p = memory + origin;
	size_t read = fread(p, sizeof(uint16_t), max_read, file);

	while(read-- > 0){
		*p = swap16(*p);
		++p;
	}
}

int read_image(const char *image_path){
	FILE *file = fopen(image_path, "rb");
	if(!file)
		return 0;
	read_image_file(file);
	fclose(file);
	return 1;
}



struct termios original_tio;

void disable_input_buffering(){
	tcgetattr(STDIN_FILENO, &original_tio);
	struct termios new_tio = original_tio;
	new_tio.c_lflag &= ~ICANON & ~ECHO;
	tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering(){
	tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal){
	restore_input_buffering();
	printf("\n");
	exit(-2);
}

int test() {

	uint16_t instr;
	printf("¿ªÊ¼²âÊÔ£º\n");
	{
		regs[1] = 0;
		regs[2] = 1;
		regs[3] = 2;
		instr = 0b0001001010000011;
		ins(instr);
		assert(regs[1] == 3);
		reset();
	}

	{
		instr = 0b0001001111101000;
		regs[1] = 0;
		regs[7] = 10;
		ins(instr);
		output_assert(regs[1] == 18, "OP_ADD ¡Ì");
		reset();
	}

	{
		instr = 0b0101000001000010;
		ins(instr);
		assert(regs[0] == 0);
		reset();
		instr = 0b0101001111100111;
		ins(instr);
		output_assert(regs[1] == 0x7, "OP_AND ¡Ì");
		reset();
	}

	{
		regs[R_PC] = 10;
		regs[R_COND] = 0b100;
		instr = 0b0000100000000010;
		ins(instr);
		output_assert(regs[R_PC] == 12, "OP_BR ¡Ì");
		reset();
	}

	{
		instr = 0b1100000111000010;
		ins(instr);
		output_assert(regs[R_PC] == 7, "OP_JMP ¡Ì");
		reset();
	}

	{
		// JSR
		instr = 0b0100100000000100;
		ins(instr);
		output_assert(regs[R_PC] == 12, "OP_JSR ¡Ì");
		reset();
	}

	{
		// JSRR
		instr = 0b0100000110000000;
		ins(instr);
		output_assert(regs[R_PC] == 6, "OP_JSRR ¡Ì");
		reset();
	}

	{
		// LD
		instr = 0b0010111000000011;
		regs[R_PC] = 1;
		uint16_t add = regs[R_PC] + sign_extend(3, 9);
		memory[add] = 10;
		ins(instr);
		output_assert(regs[7] == 10, "OP_LD ¡Ì");
		reset();
	}

	{
		// LDI
		instr = 0b1010111000000010;
		regs[R_PC] = 1;
		uint16_t add = regs[R_PC] + sign_extend(2, 9);
		mem_write(add, 10);
		uint16_t add1 = mem_read(add);
		mem_write(add1, 100);
		ins(instr);
		output_assert(regs[7] == 100, "OP_LDI ¡Ì");
		reset();
	}

	{
		// LEA
		instr = 0b1110111000000010;
		regs[R_PC] = 10;
		ins(instr);
		output_assert(regs[7] == 12, "OP_LEA ¡Ì");
		reset();
	}

	{
		// NOT
		instr = 0b1001111000111111;
		regs[0] = 0b1111111111111111;
		ins(instr);
		output_assert(regs[7] == 0, "OP_NOT ¡Ì");
		reset();
	}

	{
		// ST
		instr = 0b0011111000000001;
		regs[7] = 7;
		regs[R_PC] = 1;
		uint16_t add = regs[R_PC] + sign_extend(1, 9);
		ins(instr);
		output_assert(regs[7] == mem_read(add), "OP_ST ¡Ì");
		reset();
	}

	{
		// STR
		instr = 0b0111111000000001;
		regs[7] = 7;
		uint16_t add = 0 + sign_extend(1, 6);
		ins(instr);
		output_assert(regs[7] == mem_read(add), "OP_STR ¡Ì");
		reset();
	}
	return EXIT_SUCCESS;
}


int main(int argc, char **argv) {
//	test();

	read_image(argv[1]);

	signal(SIGINT, handle_interrupt);
	disable_input_buffering();

	enum{
		PC_START = 0x3000
	};
	regs[R_PC] = PC_START;

//	int count = 100;
	while(running){
		uint16_t instr = mem_read(regs[R_PC]++);
		ins(instr);
//		uint16_t op = instr >> 12;
	}

	restore_input_buffering();

	return EXIT_SUCCESS;
}
