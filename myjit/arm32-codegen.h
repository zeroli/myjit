/*
 * MyJIT arm-codegen.h
 *
 * Copyright (C) 2017 Petr Krajca, <petr.krajca@upol.cz>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Constants used in this file taken from the Mono Project.
 *
 * Copyright (c) 2002-2003 Sergey Chaban <serge@wildwestsoftware.com>
 * Copyright 2005-2011 Novell Inc
 * Copyright 2011 Xamarin Inc
 */

#define is_imm8(x) (((x) & ~0xff) == 0)
#define ror(x, shift) ((((unsigned int) (x)) >> (shift)) | ((unsigned int) (x) << (32 - (shift))))
#define rol(x, shift) ((((unsigned int) (x)) << (shift)) | ((unsigned int) (x) >> (32 - (shift))))

typedef enum {
	ARMREG_R0 = 0,
	ARMREG_R1,
	ARMREG_R2,
	ARMREG_R3,
	ARMREG_R4,
	ARMREG_R5,
	ARMREG_R6,
	ARMREG_R7,
	ARMREG_R8,
	ARMREG_R9,
	ARMREG_R10,
	ARMREG_R11,
	ARMREG_R12,
	ARMREG_R13,
	ARMREG_R14,
	ARMREG_R15,


	/* aliases */
	/* args */
	ARMREG_A1 = ARMREG_R0,
	ARMREG_A2 = ARMREG_R1,
	ARMREG_A3 = ARMREG_R2,
	ARMREG_A4 = ARMREG_R3,

	/* local vars */
	ARMREG_V1 = ARMREG_R4,
	ARMREG_V2 = ARMREG_R5,
	ARMREG_V3 = ARMREG_R6,
	ARMREG_V4 = ARMREG_R7,
	ARMREG_V5 = ARMREG_R8,
	ARMREG_V6 = ARMREG_R9,
	ARMREG_V7 = ARMREG_R10,

	ARMREG_FP = ARMREG_R11,
	ARMREG_IP = ARMREG_R12,
	ARMREG_SP = ARMREG_R13,
	ARMREG_LR = ARMREG_R14,
	ARMREG_PC = ARMREG_R15,

	/* co-processor */
	ARMREG_CR0 = 0,
	ARMREG_CR1,
	ARMREG_CR2,
	ARMREG_CR3,
	ARMREG_CR4,
	ARMREG_CR5,
	ARMREG_CR6,
	ARMREG_CR7,
	ARMREG_CR8,
	ARMREG_CR9,
	ARMREG_CR10,
	ARMREG_CR11,
	ARMREG_CR12,
	ARMREG_CR13,
	ARMREG_CR14,
	ARMREG_CR15,

	/* XScale: acc0 on CP0 */
	ARMREG_ACC0 = ARMREG_CR0,

	ARMREG_MAX = ARMREG_R15
} ARMReg;

typedef enum {
	ARMCOND_EQ = 0x0,          /* Equal; Z = 1 */
	ARMCOND_NE = 0x1,          /* Not equal, or unordered; Z = 0 */
	ARMCOND_CS = 0x2,          /* Carry set; C = 1 */
	ARMCOND_HS = ARMCOND_CS,   /* Unsigned higher or same; */
	ARMCOND_CC = 0x3,          /* Carry clear; C = 0 */
	ARMCOND_LO = ARMCOND_CC,   /* Unsigned lower */
	ARMCOND_MI = 0x4,          /* Negative; N = 1 */
	ARMCOND_PL = 0x5,          /* Positive or zero; N = 0 */
	ARMCOND_VS = 0x6,          /* Overflow; V = 1 */
	ARMCOND_VC = 0x7,          /* No overflow; V = 0 */
	ARMCOND_HI = 0x8,          /* Unsigned higher; C = 1 && Z = 0 */
	ARMCOND_LS = 0x9,          /* Unsigned lower or same; C = 0 || Z = 1 */
	ARMCOND_GE = 0xA,          /* Signed greater than or equal; N = V */
	ARMCOND_LT = 0xB,          /* Signed less than; N != V */
	ARMCOND_GT = 0xC,          /* Signed greater than; Z = 0 && N = V */
	ARMCOND_LE = 0xD,          /* Signed less than or equal; Z = 1 && N != V */
	ARMCOND_AL = 0xE,          /* Always */
	ARMCOND_NV = 0xF,          /* Never */

	ARMCOND_SHIFT = 28
} ARMCond;

typedef enum {
	ARMSHIFT_LSL = 0,
	ARMSHIFT_LSR = 1,
	ARMSHIFT_ASR = 2,
	ARMSHIFT_ROR = 3,

	ARMSHIFT_ASL = ARMSHIFT_LSL
	/* rrx = (ror, 1) */
} ARMShiftType;

/*
typedef struct {
	armword_t PSR_c : 8;
	armword_t PSR_x : 8;
	armword_t PSR_s : 8;
	armword_t PSR_f : 8;
} ARMPSR;
*/

typedef enum {
	ARMOP_AND = 0x0,
	ARMOP_EOR = 0x1,
	ARMOP_SUB = 0x2,
	ARMOP_RSB = 0x3,
	ARMOP_ADD = 0x4,
	ARMOP_ADC = 0x5,
	ARMOP_SBC = 0x6,
	ARMOP_RSC = 0x7,
	ARMOP_TST = 0x8,
	ARMOP_TEQ = 0x9,
	ARMOP_CMP = 0xa,
	ARMOP_CMN = 0xb,
	ARMOP_ORR = 0xc,
	ARMOP_MOV = 0xd,
	ARMOP_BIC = 0xe,
	ARMOP_MVN = 0xf,


	/* not really opcodes */

	ARMOP_STR = 0x0,
	ARMOP_LDR = 0x1,

	/* ARM2+ */
	ARMOP_MUL   = 0x0, /* Rd := Rm*Rs */
	ARMOP_MLA   = 0x1, /* Rd := (Rm*Rs)+Rn */

	/* ARM3M+ */
	ARMOP_UMULL = 0x4,
	ARMOP_UMLAL = 0x5,
	ARMOP_SMULL = 0x6,
	ARMOP_SMLAL = 0x7,

	/* for data transfers with register offset */
	ARM_UP   = 1,
	ARM_DOWN = 0
} ARMOpcode;

/**
 * Returns even number of bits necessary to rotate to the left
 * to obtain a imm8 value. If no such value exists, returns -1.
 * This value can be used along with the ROR-feature of data operations.
 */
static inline int arm32_imm_rotate(int x)
{
	if (is_imm8(x)) return 0;
	for (int i = 2; i < 32; i += 2) {
		x = rol(x, 2);
		if (is_imm8(x)) return i;
	}
	return -1;
}

static inline int arm32_encode_imm(int x)
{
	int r = arm32_imm_rotate(x);
	if (r == -1) abort();
	return (r << 8) | ((((unsigned int)x) >> r) & 0xff);
}

#define arm32_emit(ins, op) \
	*((unsigned int *)(ins)) = op; \
	(ins) = (unsigned char *)((unsigned int*)(ins) + 1);


#define arm32_encode_dataop(ins, _cond, _imm, _opcode, _s, _rd, _rn, _op2) \
	do { \
		unsigned int _op = 0; \
		_op |= (_cond) << 28; \
		_op |= (_imm) << 25; \
		_op |= (_opcode) << 21; \
		_op |= (_s) << 20; \
		_op |= (_rn) << 16; \
		_op |= (_rd) << 12; \
		_op |= ((_op2) & 0xfff); \
		arm32_emit((ins), _op); \
	} while (0)

#define arm32_encode_branch(ins, _cond, _link, _offset) \
	do { \
		unsigned int _op = 0; \
		_op |= _cond << 28; \
		_op |= 0x5 << 25; \
		_op |= _link << 24; \
		_op |= (_offset) & 0xffffff;\
		arm32_emit((ins), _op); \
	} while (0)


#define arm32_branch(ins, _cond, _offset) \
	arm32_encode_branch(ins, _cond, 0, _offset - 2)

#define arm32_bx(ins, _cond, _rn) \
	do { \
		unsigned int op = 0; \
		op |= _cond << 28; \
		op |= 0x12fff1 << 4; \
		op |= _rn;\
		arm32_emit(ins, op); \
	} while (0)

#define arm32_patch(target, pos) \
	do { \
		long __p =  ((long)(pos)) >> 2; \
		long __t =  ((long)(target)) >> 2; \
		long __location = (__p - __t - 2); \
			*(int *)(target) &= ~(0xffffff); /* 24 bits */\
			*(int *)(target) |= (0xffffff & __location); /* 24 bits */\
	} while (0)

#define arm32_cond_movw_reg_imm16(ins, _cond, _rd, _imm) \
	do { \
		unsigned int _op = 0; \
		_op |= _cond << 28; \
		_op |= 0x30 << 20; \
		_op |= (_imm & 0xf000) << 4; \
		_op |= (_rd) << 12; \
		_op |= (_imm) & 0xfff; \
		arm32_emit(ins, _op); \
	} while (0)

#define arm32_cond_movt_reg_imm16(ins, _cond, _rd, _imm) \
	do { \
		unsigned int _op = 0; \
		_op |= (_cond) << 28; \
		_op |= 0x34 << 20; \
		_op |= (_imm & 0xf000) << 4; \
		_op |= (_rd) << 12; \
		_op |= (_imm) & 0xfff; \
		arm32_emit((ins), _op); \
	} while (0)

#define arm32_cond_mov_reg_imm32(ins, _cond, _rd, _imm) \
	do { \
		/*if ((_imm) & 0xffff)*/ arm32_cond_movw_reg_imm16(ins, _cond, _rd, (_imm) & 0xffff); \
		/*if ((_imm) & 0xffff0000)*/ arm32_cond_movt_reg_imm16(ins, _cond, _rd, ((unsigned int) (_imm)) >> 16 & 0xffff); \
		/*if (!(_imm)) arm32_cond_movw_reg_imm16(ins, _cond, _rd, 0); */ \
	} while (0)


#define arm32_mov_reg_imm32(ins, _rd, _imm) \
	arm32_cond_mov_reg_imm32(ins, ARMCOND_AL, _rd, _imm)
	
#define arm32_alu_reg_reg(ins, _opcode, _rd, _rn, _rm) \
	arm32_encode_dataop(ins, ARMCOND_AL, 0, _opcode, 0, _rd, _rn, _rm)

#define arm32_alu_reg_imm(ins, _opcode, _rd, _rn, _imm) \
	arm32_encode_dataop(ins, ARMCOND_AL, 1, _opcode, 0, _rd, _rn, arm32_encode_imm(_imm))

#define arm32_alucc_reg_reg(ins, _opcode, _cc, _rd, _rn, _rm) \
	arm32_encode_dataop(ins, ARMCOND_AL, 0, _opcode, _cc, _rd, _rn, _rm)

#define arm32_alucc_reg_imm(ins, _opcode, _cc, _rd, _rn, _imm) \
	arm32_encode_dataop(ins, ARMCOND_AL, 1, _opcode, _cc, _rd, _rn, arm32_encode_imm(_imm))

#define arm32_mov_reg_reg(ins, _rd, _rm) \
	arm32_alu_reg_reg(ins, ARMOP_MOV, _rd, 0, _rm)

#define arm32_cmp_reg_reg(ins, _rd, _rn, _rm) \
	arm32_alucc_reg_reg(ins, ARMOP_CMP, 1, _rd, _rn, _rm)

#define arm32_mul(ins, rd, rm, rn) \
	do { \
		int _op = ARMCOND_AL << 28; \
		_op |= (rd) << 16; \
		_op |= (rm) << 8; \
		_op |= 0x9 << 4; \
		_op |= (rn); \
		arm32_emit(ins, _op);	\
	} while (0) 

#define arm32_hmul(ins, rd, rm, rn) \
	do { \
		int _op = ARMCOND_AL << 28; \
		_op |= 0x75 << 20; \
		_op |= (rd) << 16; \
		_op |= 0xf << 12; \
		_op |= (rm) << 8; \
		_op |= 0x1 << 4; \
		_op |= (rn); \
		arm32_emit(ins, _op);	\
	} while (0) 


#define arm32_xdiv(ins, tag, rd, rn, rm) \
	do { \
		int _op = ARMCOND_AL << 28; \
		_op |= tag << 20; \
		_op |= (rd) << 16; \
		_op |= 0xf << 12; \
		_op |= (rm) << 8; \
		_op |= 0x1 << 4; \
		_op |= (rn); \
		arm32_emit(ins, _op);	\
	} while (0)


#define arm32_sdiv(ins, rd, rn, rm) \
	arm32_xdiv(ins, 0x71, rd, rn, rm);


#define arm32_udiv(ins, rd, rn, rm) \
	arm32_xdiv(ins, 0x73, rd, rn, rm);

#define arm32_pushall(ins) \
	do { \
		unsigned int op = 0; \
		op |= ARMCOND_AL << 28; \
		op |= 0x92d << 16; \
		op |= 0xfff; \
		arm32_emit(ins, op); \
	} while (0)

#define arm32_popall(ins) \
	do { \
		unsigned int op = 0; \
		op |= ARMCOND_AL << 28; \
		op |= 0x8bd << 16; \
		op |= 0xfff; \
		arm32_emit(ins, op); \
	} while (0)

#define arm32_pushall_but_r0(ins) \
	do { \
		unsigned int op = 0; \
		op |= ARMCOND_AL << 28; \
		op |= 0x92d << 16; \
		op |= 0xffe; \
		arm32_emit(ins, op); \
	} while (0)


#define arm32_popall_but_r0(ins) \
	do { \
		unsigned int op = 0; \
		op |= ARMCOND_AL << 28; \
		op |= 0x8bd << 16; \
		op |= 0xffe; \
		arm32_emit(ins, op); \
	} while (0)

// FIXME: negative values
#define arm32_single_data_transfer(ins, load, regs, byte, rd, rn, op2) \
	do { \
		unsigned int op = 0; \
		op |= ARMCOND_AL << 28; \
		op |= 0x1 << 26; \
		op |= regs << 25; /* imm */ \
		op |= 0x1 << 24; /* post/pref index */ \
		op |= 0x1 << 23; /* rn + rm */ \
		op |= byte << 22; \
		op |= 0x0 << 21; /* write */ \
		op |= load << 20; /* load */ \
		op |= rn << 16; \
		op |= rd << 12; \
		op |= op2; \
		arm32_emit(ins, op);\
	} while (0) \

#define arm32_ld_reg(ins, rd, rn, rm) \
	arm32_single_data_transfer(ins, 1, 1, 0, rd, rn, rm)

#define arm32_ldub_reg(ins, rd, rn, rm) \
	arm32_single_data_transfer(ins, 1, 1, 1, rd, rn, rm)

#define arm32_ld_imm(ins, rd, rn, imm) \
	arm32_single_data_transfer(ins, 1, 0, 0, rd, rn, arm32_encode_imm(imm))

#define arm32_ldub_imm(ins, rd, rn, imm) \
	arm32_single_data_transfer(ins, 1, 0, 1, rd, rn, arm32_encode_imm(imm))

#define arm32_st_reg(ins, rd, rn, rm) \
	arm32_single_data_transfer(ins, 0, 1, 0, rd, rn, rm)

#define arm32_stb_reg(ins, rd, rn, rm) \
	arm32_single_data_transfer(ins, 0, 1, 1, rd, rn, rm)

#define arm32_st_imm(ins, rd, rn, imm) \
	arm32_single_data_transfer(ins, 0, 0, 0, rd, rn, arm32_encode_imm(imm))

#define arm32_stb_imm(ins, rd, rn, imm) \
	arm32_single_data_transfer(ins, 0, 0, 1, rd, rn, arm32_encode_imm(imm))


#define arm32_signed_and_half_data_transfer_reg(ins, load, signed_value, halfword, rd, rn, rm) \
	do { \
		unsigned int op = 0; \
		op |= ARMCOND_AL << 28; \
		op |= 0x1 << 24; \
		op |= 0x1 << 23; /* UP => rn + rm */ \
		op |= 0x0 << 22; \
		op |= 0x0 << 21; /* write */ \
		op |= load << 20; /* load */ \
		op |= rn << 16; \
		op |= rd << 12; \
		op |= 0x1 << 7; \
		op |= signed_value << 6; \
		op |= halfword << 5; \
		op |= 0x1 << 4; \
		op |= rm; \
		arm32_emit(ins, op);\
	} while (0) \

#define arm32_signed_and_half_data_transfer_imm(ins, load, signed_value, halfword, rd, rn, imm) \
	do { \
		int _abs = (imm < 0 ? -imm : imm); \
		unsigned int op = 0; \
		op |= ARMCOND_AL << 28; \
		op |= 0x1 << 24; \
		op |= ((imm) >= 0) << 23; /* UP => rn + rm */ \
		op |= 0x1 << 22; \
		op |= 0x0 << 21; /* write */ \
		op |= load << 20; /* load */ \
		op |= rn << 16; \
		op |= rd << 12; \
		op |= ((_abs >> 4) & 0xf) << 8; \
		op |= 0x1 << 7; \
		op |= signed_value << 6; \
		op |= halfword << 5; \
		op |= 0x1 << 4; \
		op |= (_abs & 0xf); \
		arm32_emit(ins, op);\
	} while (0) \



#define arm32_ldsb_reg(ins, rd, rn, rm) \
	arm32_signed_and_half_data_transfer_reg(ins, 1, 1, 0, rd, rn, rm)

#define arm32_ldsh_reg(ins, rd, rn, rm) \
	arm32_signed_and_half_data_transfer_reg(ins, 1, 1, 1, rd, rn, rm)

#define arm32_lduh_reg(ins, rd, rn, rm) \
	arm32_signed_and_half_data_transfer_reg(ins, 1, 0, 1, rd, rn, rm)

#define arm32_ldsb_imm(ins, rd, rn, imm) \
	arm32_signed_and_half_data_transfer_imm(ins, 1, 1, 0, rd, rn, imm)

#define arm32_ldsh_imm(ins, rd, rn, imm) \
	arm32_signed_and_half_data_transfer_imm(ins, 1, 1, 1, rd, rn, imm)

#define arm32_lduh_imm(ins, rd, rn, imm) \
	arm32_signed_and_half_data_transfer_imm(ins, 1, 0, 1, rd, rn, imm)

#define arm32_sth_reg(ins, rd, rn, rm) \
	arm32_signed_and_half_data_transfer_reg(ins, 0, 0, 1, rd, rn, rm)

#define arm32_sth_imm(ins, rd, rn, imm) \
	arm32_signed_and_half_data_transfer_imm(ins, 0, 0, 1, rd, rn, imm)



#define arm32_shift_reg(ins, type, rd, rn, rs) \
	do { \
		unsigned _op = 0; \
		_op |= ARMCOND_AL << 28; \
		_op |= 0xd << 21; \
		_op |= 0 << 20; /* s */  \
		_op |= (rd << 12); \
		_op |= type << 5; \
		_op |= 1 << 4; /* reg */; \
		_op |= rs << 8; \
		_op |= rn; \
		arm32_emit(ins, _op); \
	} \
	while (0)

#define arm32_lsh_reg(ins, rd, rn, rs) \
	arm32_shift_reg(ins, ARMSHIFT_LSL, rd, rn, rs)

#define arm32_rsh_reg(ins, rd, rn, rs) \
	arm32_shift_reg(ins, ARMSHIFT_LSR, rd, rn, rs)

#define arm32_rsa_reg(ins, rd, rn, rs) \
	arm32_shift_reg(ins, ARMSHIFT_ASR, rd, rn, rs)

#define arm32_shift_imm(ins, type, rd, rn, rs) \
	do { \
		unsigned _op = 0; \
		_op |= ARMCOND_AL << 28; \
		_op |= 0xd << 21; \
		_op |= 0 << 20; /* s */  \
		_op |= ((rd) << 12); \
		_op |= type << 5; \
		_op |= 0 << 4; /* imm */; \
		_op |= ((rs) & 0x1f) << 7; \
		_op |= rn; \
		arm32_emit(ins, _op); \
	} \
	while (0)

#define arm32_lsh_imm(ins, rd, rn, imm) \
	arm32_shift_imm(ins, ARMSHIFT_LSL, rd, rn, imm)

#define arm32_rsh_imm(ins, rd, rn, imm) \
	arm32_shift_imm(ins, ARMSHIFT_LSR, rd, rn, imm)

#define arm32_rsa_imm(ins, rd, rn, imm) \
	arm32_shift_imm(ins, ARMSHIFT_ASR, rd, rn, imm)

