/*
 * MyJIT
 * Copyright (C) 2010, 2015 Petr Krajca, <petr.krajca@upol.cz>
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
 */

#include "common86-codegen.h"
#include "util.h"

static inline int is_spilled(jit_value arg_id, jit_op * prepare_op, int * reg);
static int emit_push_callee_saved_regs(struct jit * jit, jit_op * op);
static int emit_push_caller_saved_regs(struct jit * jit, jit_op * op);
static int emit_pop_callee_saved_regs(struct jit * jit);
static int emit_pop_caller_saved_regs(struct jit * jit, jit_op * op);
static void emit_save_all_regs(struct jit *jit, jit_op *op);
static void emit_restore_all_regs(struct jit *jit, jit_op *op);


static jit_hw_reg * rmap_is_associated(jit_rmap * rmap, int reg_id, int fp, jit_value * virt_reg);
static jit_hw_reg * rmap_get(jit_rmap * rmap, jit_value reg);


#define JIT_GET_ADDR(jit, imm) (!jit_is_label(jit, (void *)(imm)) ? (imm) :  \
		(((jit_value)jit->buf + ((jit_label *)(imm))->pos - (jit_value)jit->ip)))

#include "sse2-specific.h"

#ifdef JIT_ARCH_I386
#include "x86-specific.h"
#endif

#ifdef JIT_ARCH_AMD64
#include "amd64-specific.h"
#endif

//
//
// Registers
//
//

/**
 * Emits PUSH instruction for the given GP or FP register
 */
static int emit_push_reg(struct jit * jit, jit_hw_reg * r, int stack_offset)
{
	if (!r->fp) {
		stack_offset += REG_SIZE;
		common86_mov_membase_reg(jit->ip, COMMON86_SP, -stack_offset, r->id, REG_SIZE);
	} else {
		stack_offset += 8;
		sse_movlpd_membase_xreg(jit->ip, r->id, COMMON86_SP, -stack_offset);
	}
	return stack_offset;
}
/**
 * Emits PUSH instruction for the given GP or FP register
 */

static int emit_pop_reg(struct jit * jit, jit_hw_reg * r, int stack_offset)
{
	if (!r->fp) {
		common86_mov_reg_membase(jit->ip, r->id, COMMON86_SP, stack_offset, REG_SIZE);
		stack_offset += REG_SIZE;
	} else {
		sse_movlpd_xreg_membase(jit->ip, r->id, COMMON86_SP, stack_offset);
		stack_offset += 8;
	}
	return stack_offset;
}

static int emit_push_callee_saved_regs(struct jit * jit, jit_op * op)
{
	int stack_offset = 0;
	for (int i = 0; i < jit->reg_al->gp_reg_cnt; i++) {
		jit_hw_reg * r = &(jit->reg_al->gp_regs[i]);
		if (r->callee_saved)
			for (struct jit_op * o = op->next; o != NULL; o = o->next) {
				if (GET_OP(o) == JIT_PROLOG) break;
				if (uses_hw_reg(o, r->id, 0)) {
					stack_offset = emit_push_reg(jit, r, stack_offset);
					break;
				}
			}
	}
	stack_offset = jit_value_align(stack_offset, JIT_STACK_ALIGNMENT);
	if (stack_offset) common86_alu_reg_imm(jit->ip, X86_SUB, COMMON86_SP, stack_offset);
	return stack_offset / REG_SIZE;
}

static int emit_pop_callee_saved_regs(struct jit * jit)
{
	int count = 0;
	struct jit_op * op = jit->current_func;
	jit_hw_reg *active_regs[32];

	for (int i = jit->reg_al->gp_reg_cnt - 1; i >= 0; i--) {
		jit_hw_reg * r = &(jit->reg_al->gp_regs[i]);
		if (r->callee_saved)
			for (struct jit_op * o = op->next; o != NULL; o = o->next) {
				if (GET_OP(o) == JIT_PROLOG) break;
				if (uses_hw_reg(o, r->id, 0)) {
					active_regs[count] = r;
					count++;
					break;
				}
			}
	}
	int stack_space = jit_value_align(count * REG_SIZE, JIT_STACK_ALIGNMENT);
	int stack_offset = stack_space - (count * REG_SIZE);

	for (int i = 0; i < count; i++) {
		stack_offset = emit_pop_reg(jit, active_regs[i], stack_offset);
	}
	if (stack_space) common86_alu_reg_imm(jit->ip, X86_ADD, COMMON86_SP, stack_space);
	return count;
}

static int generic_push_caller_saved_regs(struct jit * jit, jit_op * op, int reg_count,
		jit_hw_reg * regs, int fp, jit_hw_reg * skip_reg, int stack_offset)
{
	jit_value reg;
	int skip_reg_id = (skip_reg ? skip_reg->id :-1);
	for (int i = 0; i < reg_count; i++) {
		if ((regs[i].id == skip_reg_id) || (regs[i].callee_saved)) continue;
		jit_hw_reg * hreg = rmap_is_associated(op->regmap, regs[i].id, fp, &reg);
		if (hreg && jit_set_get(op->live_in, reg)) {
			stack_offset = emit_push_reg(jit, hreg, stack_offset);
		}
	}
	return stack_offset;
}

static int emit_push_caller_saved_regs(struct jit * jit, jit_op * op)
{
	int stack_offset = 0;
	struct jit_reg_allocator * al = jit->reg_al;
	while (op) {
		if (GET_OP(op) == JIT_CALL) break;
		op = op->next;
	}
	stack_offset = generic_push_caller_saved_regs(jit, op, al->gp_reg_cnt, al->gp_regs, 0, al->ret_reg, stack_offset);
	stack_offset = generic_push_caller_saved_regs(jit, op, al->fp_reg_cnt, al->fp_regs, 1, al->fpret_reg, stack_offset);
	if (stack_offset) common86_alu_reg_imm(jit->ip, X86_SUB, COMMON86_SP, stack_offset);
	int count = stack_offset / REG_SIZE;
	return count;
}

static int generic_pop_caller_saved_regs(struct jit * jit, jit_op * op, int reg_count,
		jit_hw_reg * regs, int fp, jit_hw_reg * skip_reg, int stack_offset)
{
	jit_value reg;
	int skip_reg_id = (skip_reg ? skip_reg->id :-1);
	for (int i = reg_count - 1; i >= 0; i--) {
		if ((regs[i].id == skip_reg_id) || (regs[i].callee_saved)) continue;
		jit_hw_reg * hreg = rmap_is_associated(op->regmap, regs[i].id, fp, &reg);
		if (hreg && jit_set_get(op->live_in, reg)) {
			stack_offset = emit_pop_reg(jit, hreg, stack_offset);
		}
	}
	return stack_offset;
}

static int emit_pop_caller_saved_regs(struct jit * jit, jit_op * op)
{
	struct jit_reg_allocator * al = jit->reg_al;
	int stack_offset  = 0;

	stack_offset = generic_pop_caller_saved_regs(jit, op, al->fp_reg_cnt, al->fp_regs, 1, al->fpret_reg, stack_offset);
	stack_offset = generic_pop_caller_saved_regs(jit, op, al->gp_reg_cnt, al->gp_regs, 0, al->ret_reg, stack_offset);

	if (stack_offset) common86_alu_reg_imm(jit->ip, X86_ADD, COMMON86_SP, stack_offset);
	int count = stack_offset / REG_SIZE;
	return count;
}

static int is_active_register(struct jit_reg_allocator *al, jit_hw_reg *reg, jit_op *op)
{
	if (op->next == NULL) return 0;
	if ((GET_OP(op->next) == JIT_PUTARG) || (GET_OP(op->next) == JIT_FPUTARG) || (GET_OP(op->next) == JIT_CALL)) return 1;
	if ((GET_OP(op->next) == JIT_RETVAL) && (reg == al->ret_reg)) return 1;
	//if ((GET_OP(op->next) == JIT_FRETVAL) && (reg == al->fpret_reg)) return 1;

	if (op->next->regmap == NULL) return 1;
	if (op->prev->regmap == NULL) return 1;

	jit_value vreg;
	jit_hw_reg *hw = rmap_is_associated(op->regmap, reg->id, reg->fp, &vreg);

	if (hw) {
		if (op->prev && ((op->prev->live_in && jit_set_get(op->prev->live_in, vreg)) || ((op->prev->live_out && jit_set_get(op->prev->live_out, vreg))))) return 1;
		if (op->next && ((op->next->live_in && jit_set_get(op->next->live_in, vreg)) || ((op->next->live_out && jit_set_get(op->next->live_out, vreg))))) return 1;
		return 0;
	}
	return 0;
}

static int required_stack_space_for_regs(struct jit *jit, jit_op *op)
{

	struct jit_reg_allocator * al = jit->reg_al;

	int space = REG_SIZE; // flag register
	if (!jit_current_func_info(jit)->has_prolog) space += REG_SIZE;

	for (int i = 0; i < al->gp_reg_cnt; i++) {
		jit_hw_reg *reg = &al->gp_regs[i];
		if (!reg->callee_saved && is_active_register(al, reg, op))
			space += REG_SIZE;
	}

	for (int i = 0; i < al->fp_reg_cnt; i++) {
		jit_hw_reg *reg = &al->fp_regs[i];
		if (!reg->callee_saved && is_active_register(al, reg, op))
			space += sizeof(double) * 2;
	}
	return space;
}

static void emit_save_all_regs(struct jit *jit, jit_op *op)
{
	struct jit_reg_allocator * al = jit->reg_al;

	common86_pushf(jit->ip);

	for (int i = 0; i < al->gp_reg_cnt; i++) {
		jit_hw_reg *reg = &al->gp_regs[i];
		if (!reg->callee_saved && is_active_register(al, reg, op))
			common86_push_reg(jit->ip, reg->id);
	}

	for (int i = 0; i < al->fp_reg_cnt; i++) {
		jit_hw_reg *reg = &al->fp_regs[i];
		if (!reg->callee_saved && is_active_register(al, reg, op))
			common86_push_xmm_reg(jit->ip, reg->id);
	}

	int alignment = required_stack_space_for_regs(jit, op) % 16;
	if (alignment != 0) common86_alu_reg_imm(jit->ip, X86_SUB, COMMON86_SP, 16 - alignment);
}

static void emit_restore_all_regs(struct jit *jit, jit_op *op)
{
	int alignment = required_stack_space_for_regs(jit, op) % 16;
	if (alignment != 0) common86_alu_reg_imm(jit->ip, X86_ADD, COMMON86_SP, 16 - alignment);

	struct jit_reg_allocator * al = jit->reg_al;

	for (int i = al->fp_reg_cnt - 1; i >= 0; i--) {
		jit_hw_reg *reg = &al->fp_regs[i];
		if (!reg->callee_saved && is_active_register(al, reg, op))
			common86_pop_xmm_reg(jit->ip, reg->id);
	}

	for (int i = al->gp_reg_cnt - 1; i >= 0; i--) {
		jit_hw_reg *reg = &al->gp_regs[i];
		if (!reg->callee_saved && is_active_register(al, reg, op))
			common86_pop_reg(jit->ip, reg->id);
	}
	common86_popf(jit->ip);
}




/**
 * Emits LREG operation
 *
 * This operation loads value of the virtual register from the stack
 * into its hardware counterpart
 */
static void emit_lreg(struct jit * jit, int hreg_id, jit_value vreg)
{
	int stack_pos = GET_REG_POS(jit, vreg) ;

	if (JIT_REG_TYPE(vreg) == JIT_RTYPE_FLOAT) sse_movlpd_xreg_membase(jit->ip, hreg_id, COMMON86_BP, stack_pos);
	else common86_mov_reg_membase(jit->ip, hreg_id, COMMON86_BP, stack_pos, REG_SIZE);
}

/**
 * Emits UREG operation
 *
 * This operation unloads value of the virtual register on the stack
 * from its hardware counterpart
 */
static void emit_ureg(struct jit * jit, jit_value vreg, int hreg_id)
{
	int stack_pos = GET_REG_POS(jit, vreg);

	if (JIT_REG_TYPE(vreg) == JIT_RTYPE_FLOAT) sse_movlpd_membase_xreg(jit->ip, hreg_id, COMMON86_BP, stack_pos);
	else common86_mov_membase_reg(jit->ip, COMMON86_BP, stack_pos, hreg_id, REG_SIZE);
}

static void emit_get_arg_from_stack(struct jit * jit, int type, int size, int dreg, int stack_reg, int stack_pos)
{
	if (type != JIT_FLOAT_NUM) {
		if (size == REG_SIZE) common86_mov_reg_membase(jit->ip, dreg, stack_reg, stack_pos, REG_SIZE);
		else if (type == JIT_SIGNED_NUM)
			common86_movsx_reg_membase(jit->ip, dreg, stack_reg, stack_pos, size);
		else common86_movzx_reg_membase(jit->ip, dreg, stack_reg, stack_pos, size);
	} else {
		if (size == sizeof(float)) sse_cvtss2sd_reg_membase(jit->ip, dreg, stack_reg, stack_pos);
		else sse_movlpd_xreg_membase(jit->ip, dreg, stack_reg, stack_pos);
	}
}

/**
 * Emits GETARG operation
 *
 * This function is slightly larger since it takes into account for various calling
 * including that one which is used on AMD64.
 * Information on location of arguments is provided in the jit_init_arg_params function.
 */
static void emit_get_arg(struct jit * jit, jit_op * op)
{
	struct jit_func_info * info = jit_current_func_info(jit);

	int dreg = op->r_arg[0];
	int arg_id = op->r_arg[1];

	struct jit_inp_arg * arg = &(info->args[arg_id]);

	int size = arg->size;
	int type = arg->type;
	int reg_id = jit_mkreg(type == JIT_FLOAT_NUM ? JIT_RTYPE_FLOAT : JIT_RTYPE_INT, JIT_RTYPE_ARG, arg_id);

	int read_from_stack = 0;
	int stack_pos;

	if (!arg->passed_by_reg) {
		read_from_stack = 1;
		stack_pos = arg->location.stack_pos;

		// optimization which doesnot require EBP register
		if (!jit_current_func_info(jit)->has_prolog) {
			stack_pos -= REG_SIZE;
			stack_pos += jit->push_count * REG_SIZE;
			emit_get_arg_from_stack(jit, type, size, dreg, COMMON86_SP, stack_pos);
			return;
		}
	}

	if (arg->passed_by_reg && rmap_get(op->regmap, reg_id) == NULL) {
		// the register is not associated and the value has to be read from the memory
		read_from_stack = 1;
		stack_pos = arg->spill_pos;
	}

	if (read_from_stack) {
		emit_get_arg_from_stack(jit, type, size, dreg, COMMON86_BP, stack_pos);
		return;
	}

	jit_hw_reg *arg_reg = rmap_get(op->regmap, reg_id);
	if (type != JIT_FLOAT_NUM) {
		if (size == REG_SIZE) common86_mov_reg_reg(jit->ip, dreg, arg_reg->id, REG_SIZE);
		else if (type == JIT_SIGNED_NUM) common86_movsx_reg_reg(jit->ip, dreg, arg_reg->id, size);
		else common86_movzx_reg_reg(jit->ip, dreg, arg_reg->id, size);
	} else {
		if (size == sizeof(float)) sse_cvtss2sd_reg_reg(jit->ip, dreg, arg_reg->id);
		else sse_movsd_reg_reg(jit->ip, dreg, arg_reg->id);
	}
}

//
//
// Common ALU operation
//
//

/**
 * This function emits majority of ALU operations
 */
static void emit_alu_op(struct jit * jit, struct jit_op * op, int x86_op, int imm)
{
	if (imm) {
		if (op->r_arg[0] != op->r_arg[1]) common86_mov_reg_reg(jit->ip, op->r_arg[0], op->r_arg[1], REG_SIZE);
		common86_alu_reg_imm(jit->ip, x86_op, op->r_arg[0], op->r_arg[2]);

	}  else {
		if (op->r_arg[0] == op->r_arg[1]) {
			common86_alu_reg_reg(jit->ip, x86_op, op->r_arg[0], op->r_arg[2]);
		} else if (op->r_arg[0] == op->r_arg[2]) {
			common86_alu_reg_reg(jit->ip, x86_op, op->r_arg[0], op->r_arg[1]);
		} else {
			common86_mov_reg_reg(jit->ip, op->r_arg[0], op->r_arg[1], REG_SIZE);
			common86_alu_reg_reg(jit->ip, x86_op, op->r_arg[0], op->r_arg[2]);
		}
	}
}

/**
 * Emits the SUB operation, since it is not a commutative operation
 * it needs some extra care for some types of operands
 */
static void emit_sub_op(struct jit * jit, struct jit_op * op, int imm)
{
	if (imm) {
		if (op->r_arg[0] != op->r_arg[1]) common86_lea_membase(jit->ip, op->r_arg[0], op->r_arg[1], -op->r_arg[2]);
		else common86_alu_reg_imm(jit->ip, X86_SUB, op->r_arg[0], op->r_arg[2]);
		return;

	}
	if (op->r_arg[0] == op->r_arg[1]) {
		common86_alu_reg_reg(jit->ip, X86_SUB, op->r_arg[0], op->r_arg[2]);
	} else if (op->r_arg[0] == op->r_arg[2]) {
		common86_alu_reg_reg(jit->ip, X86_SUB, op->r_arg[0], op->r_arg[1]);
		common86_neg_reg(jit->ip, op->r_arg[0]);
	} else {
		common86_mov_reg_reg(jit->ip, op->r_arg[0], op->r_arg[1], REG_SIZE);
		common86_alu_reg_reg(jit->ip, X86_SUB, op->r_arg[0], op->r_arg[2]);
	}
}
/**
 * Emits the SUBX and SUBC operations, since it is not a commutative operation
 * it needs some extra care for some types of operands
 */

static void emit_subx_op(struct jit * jit, struct jit_op * op, int x86_op, int imm)
{
	if (imm) {
		if (op->r_arg[0] != op->r_arg[1]) common86_mov_reg_reg(jit->ip, op->r_arg[0], op->r_arg[1], REG_SIZE);
		common86_alu_reg_imm(jit->ip, x86_op, op->r_arg[0], op->r_arg[2]);
		return;

	}
	if (op->r_arg[0] == op->r_arg[1]) {
		common86_alu_reg_reg(jit->ip, x86_op, op->r_arg[0], op->r_arg[2]);
	} else if (op->r_arg[0] == op->r_arg[2]) {
		common86_mov_membase_reg(jit->ip, COMMON86_SP, -REG_SIZE, op->r_arg[2], REG_SIZE);
		common86_mov_reg_reg(jit->ip, op->r_arg[0], op->r_arg[1], REG_SIZE);
		common86_alu_reg_membase(jit->ip, x86_op, op->r_arg[0], COMMON86_SP, -REG_SIZE);
	} else {
		common86_mov_reg_reg(jit->ip, op->r_arg[0], op->r_arg[1], REG_SIZE);
		common86_alu_reg_reg(jit->ip, x86_op, op->r_arg[0], op->r_arg[2]);
	}
}
/**
 * Emits the RSB operation, since it is not a commutative operation
 * it needs some extra care for some types of operands
 */
static void emit_rsb_op(struct jit * jit, struct jit_op * op, int imm)
{
	if (imm) {
		if (op->r_arg[0] == op->r_arg[1]) common86_alu_reg_imm(jit->ip, X86_ADD, op->r_arg[0], -op->r_arg[2]);
		else common86_lea_membase(jit->ip, op->r_arg[0], op->r_arg[1], -op->r_arg[2]);
		common86_neg_reg(jit->ip, op->r_arg[0]);
		return;
	}

	if (op->r_arg[0] == op->r_arg[1]) { // O1 = O3 - O1
		common86_alu_reg_reg(jit->ip, X86_SUB, op->r_arg[0], op->r_arg[2]);
		common86_neg_reg(jit->ip, op->r_arg[0]);
	} else if (op->r_arg[0] == op->r_arg[2]) { // O1 = O1 - O2
		common86_alu_reg_reg(jit->ip, X86_SUB, op->r_arg[0], op->r_arg[1]);
	} else {
		common86_mov_reg_reg(jit->ip, op->r_arg[0], op->r_arg[2], REG_SIZE);
		common86_alu_reg_reg(jit->ip, X86_SUB, op->r_arg[0], op->r_arg[1]);
	}
}

/**
 * Emits operations for multiplications
 *
 * @param imm -- indicates whether the multiplier is constant or not
 * @param sign -- if zero, it considers values to be unsigned
 * @param high_bytes -- returns higher bytes of the results
 *
 * This implementation tries to optimize several cases,
 * and converts some types of multiplications into the bit shifts
 * or uses LEA operation to get the result more efficiently
 *
 * Unfortunately, x86 assembler assumes that the result value of the MUL operation
 * is stored into the EDX:EAX pair, and therefore, if these registers are in use,
 * their value have to be saved on the stack for a while and then returned back.
 * TODO: Register allocator should be aware of this issues and should take care
 * of this.
 */
static void emit_mul_op(struct jit * jit, struct jit_op * op, int imm, int sign, int high_bytes)
{
	jit_value dest = op->r_arg[0];
	jit_value factor1 = op->r_arg[1];
	jit_value factor2 = op->r_arg[2];

	// optimization for special cases
	if ((!high_bytes) && (imm)) {
		switch (factor2) {
			case 2: if (factor1 == dest) common86_shift_reg_imm(jit->ip, X86_SHL, dest, 1);
				else common86_lea_memindex(jit->ip, dest, factor1, 0, factor1, 0);
				return;

			case 3: common86_lea_memindex(jit->ip, dest, factor1, 0, factor1, 1); return;

			case 4: if (factor1 != dest) common86_mov_reg_reg(jit->ip, dest, factor1, REG_SIZE);
				common86_shift_reg_imm(jit->ip, X86_SHL, dest, 2);
				return;
			case 5: common86_lea_memindex(jit->ip, dest, factor1, 0, factor1, 2);
				return;
			case 8: if (factor1 != dest) common86_mov_reg_reg(jit->ip, dest, factor1, REG_SIZE);
				common86_shift_reg_imm(jit->ip, X86_SHL, dest, 3);
				return;
			case 9: common86_lea_memindex(jit->ip, dest, factor1, 0, factor1, 3);
				return;
		}
	}


	// generic multiplication
	int ax_in_use = jit_reg_in_use(op, COMMON86_AX, 0);
	int dx_in_use = jit_reg_in_use(op, COMMON86_DX, 0);

	if ((dest != COMMON86_AX) && ax_in_use) common86_push_reg(jit->ip, COMMON86_AX);
	if ((dest != COMMON86_DX) && dx_in_use) common86_push_reg(jit->ip, COMMON86_DX);

	if (imm) {
		if (factor1 != COMMON86_AX) common86_mov_reg_reg(jit->ip, COMMON86_AX, factor1, REG_SIZE);
		common86_mov_reg_imm(jit->ip, COMMON86_DX, factor2);
		common86_mul_reg(jit->ip, COMMON86_DX, sign);
	} else {
		if (factor1 == COMMON86_AX) common86_mul_reg(jit->ip, factor2, sign);
		else if (factor2 == COMMON86_AX) common86_mul_reg(jit->ip, factor1, sign);
		else {
			common86_mov_reg_reg(jit->ip, COMMON86_AX, factor1, REG_SIZE);
			common86_mul_reg(jit->ip, factor2, sign);
		}
	}

	if (!high_bytes) {
		if (dest != COMMON86_AX) common86_mov_reg_reg(jit->ip, dest, COMMON86_AX, REG_SIZE);
	} else {
		if (dest != COMMON86_DX) common86_mov_reg_reg(jit->ip, dest, COMMON86_DX, REG_SIZE);
	}

	if ((dest != COMMON86_DX) && dx_in_use) common86_pop_reg(jit->ip, COMMON86_DX);
	if ((dest != COMMON86_AX) && ax_in_use) common86_pop_reg(jit->ip, COMMON86_AX);
}

/**
 * Emits operations for multiplications
 *
 * @param imm -- indicates whether the divisor is a constant or not
 * @param sign -- if zero, it considers values are considered as unsigned
 * @param modulo -- returns modulo
 *
 * This implementation tries to optimize several cases,
 * and converts some types of multiplications into the bit shifts
 *
 * Unfortunately, x86 assembler assumes that the dividend of the DIV operation
 * is stored into the EDX:EAX pair, and therefore, if these registers are in use,
 * their value have to be saved on the stack for a while and then returned back.
 * TODO: Register allocator should be aware of this issues and should take care
 * of this.
 */

static void emit_div_op(struct jit * jit, struct jit_op * op, int imm, int sign, int modulo)
{
	jit_value dest = op->r_arg[0];
	jit_value dividend = op->r_arg[1];
	jit_value divisor = op->r_arg[2];

	if (imm && ((divisor == 2) || (divisor == 4) || (divisor == 8))) {
		if (dest != dividend) common86_mov_reg_reg(jit->ip, dest, dividend, REG_SIZE);
		if (!modulo) {
			switch (divisor) {
				case 2: common86_shift_reg_imm(jit->ip, sign ? X86_SAR : X86_SHR, dest, 1); break;
				case 4: common86_shift_reg_imm(jit->ip, sign ? X86_SAR : X86_SHR, dest, 2); break;
				case 8: common86_shift_reg_imm(jit->ip, sign ? X86_SAR : X86_SHR, dest, 3); break;
			}
			return;
		}
		if (modulo && !sign) {
			switch (divisor) {
				case 2: common86_alu_reg_imm(jit->ip, X86_AND, dest, 0x1); break;
				case 4: common86_alu_reg_imm(jit->ip, X86_AND, dest, 0x3); break;
				case 8: common86_alu_reg_imm(jit->ip, X86_AND, dest, 0x7); break;
			}
			return;
		}
	}

	int ax_in_use = jit_reg_in_use(op, COMMON86_AX, 0);
	int dx_in_use = jit_reg_in_use(op, COMMON86_DX, 0);

	if ((dest != COMMON86_AX) && ax_in_use) common86_push_reg(jit->ip, COMMON86_AX);
	if ((dest != COMMON86_DX) && dx_in_use) common86_push_reg(jit->ip, COMMON86_DX);

	if (imm) {
		if (dividend != COMMON86_AX) common86_mov_reg_reg(jit->ip, COMMON86_AX, dividend, REG_SIZE);
		if (sign) common86_cdq(jit->ip);
		else common86_alu_reg_reg(jit->ip, X86_XOR, COMMON86_DX, COMMON86_DX);
		if (dest != COMMON86_BX) common86_push_reg(jit->ip, COMMON86_BX);
		common86_mov_reg_imm_size(jit->ip, COMMON86_BX, divisor, REG_SIZE);
		common86_div_reg(jit->ip, COMMON86_BX, sign);
		if (dest != COMMON86_BX) common86_pop_reg(jit->ip, COMMON86_BX);
	} else {
		if ((divisor == COMMON86_AX) || (divisor == COMMON86_DX)) {
			common86_push_reg(jit->ip, divisor);
		}

		if (dividend != COMMON86_AX) common86_mov_reg_reg(jit->ip, COMMON86_AX, dividend, REG_SIZE);

		if (sign) common86_cdq(jit->ip);
		else common86_alu_reg_reg(jit->ip, X86_XOR, COMMON86_DX, COMMON86_DX);

		if ((divisor == COMMON86_AX) || (divisor == COMMON86_DX)) {
			common86_div_membase(jit->ip, COMMON86_SP, 0, sign);
			common86_alu_reg_imm(jit->ip, X86_ADD, COMMON86_SP, REG_SIZE);
		} else {
			common86_div_reg(jit->ip, divisor, sign);
		}
	}

	if (!modulo) {
		if (dest != COMMON86_AX) common86_mov_reg_reg(jit->ip, dest, COMMON86_AX, REG_SIZE);
	} else {
		if (dest != COMMON86_DX) common86_mov_reg_reg(jit->ip, dest, COMMON86_DX, REG_SIZE);
	}

	if ((dest != COMMON86_DX) && dx_in_use) common86_pop_reg(jit->ip, COMMON86_DX);
	if ((dest != COMMON86_AX) && ax_in_use) common86_pop_reg(jit->ip, COMMON86_AX);
}
/**
 * Emits bit shift operations
 *
 * Unfortunately, only constant or register ECX can be used to shift value.
 * This leads to a weird situation if the shifted value is in the ECX register.
 */
static void emit_shift_op(struct jit * jit, struct jit_op * op, int shift_op, int imm)
{
	if (imm) {
		if (op->r_arg[0] != op->r_arg[1]) common86_mov_reg_reg(jit->ip, op->r_arg[0], op->r_arg[1], REG_SIZE);
		common86_shift_reg_imm(jit->ip, shift_op, op->r_arg[0], op->r_arg[2]);
	} else {
		int destreg = op->r_arg[0];
		int valreg = op->r_arg[1];
		int shiftreg = op->r_arg[2];

		if (destreg != COMMON86_CX) {

			int cx_in_use = jit_reg_in_use(op, COMMON86_CX, 0);

			if (cx_in_use && (shiftreg != COMMON86_CX)) common86_push_reg(jit->ip, COMMON86_CX);
			if (shiftreg != COMMON86_CX) common86_mov_reg_reg(jit->ip, COMMON86_CX, shiftreg, REG_SIZE);
			if (destreg != valreg) {
				if (valreg != COMMON86_CX) common86_mov_reg_reg(jit->ip, destreg, valreg, REG_SIZE);
				else common86_mov_reg_membase(jit->ip, destreg, COMMON86_SP, 0, REG_SIZE);
			}
			common86_shift_reg(jit->ip, shift_op, destreg);
			if (cx_in_use && (shiftreg != COMMON86_CX)) common86_pop_reg(jit->ip, COMMON86_CX);
		} else {
			// ECX is the destination register
			jit_hw_reg * tmp = jit_get_unused_reg(jit->reg_al, op, 0);
			int tmpreg = (tmp ? tmp->id : COMMON86_AX);

			int tmp_in_use = jit_reg_in_use(op, tmpreg, 0);

			if (tmp_in_use) common86_push_reg(jit->ip, tmpreg);

			if (tmpreg != valreg) common86_mov_reg_reg(jit->ip, tmpreg, valreg, REG_SIZE);
			if (shiftreg != COMMON86_CX) common86_mov_reg_reg(jit->ip, COMMON86_CX, shiftreg, REG_SIZE);
			common86_shift_reg(jit->ip, shift_op, tmpreg);
			common86_mov_reg_reg(jit->ip, destreg, tmpreg, REG_SIZE);

			if (tmp_in_use) common86_pop_reg(jit->ip, tmpreg);
		}
	}
}

static void emit_cond_op(struct jit * jit, struct jit_op * op, int amd64_cond, int imm, int sign)
{
	if (imm) common86_alu_reg_imm(jit->ip, X86_CMP, op->r_arg[1], op->r_arg[2]);
	else common86_alu_reg_reg(jit->ip, X86_CMP, op->r_arg[1], op->r_arg[2]);
	if ((op->r_arg[0] != COMMON86_SI) && (op->r_arg[0] != COMMON86_DI)) {
		common86_mov_reg_imm(jit->ip, op->r_arg[0], 0);
		common86_set_reg(jit->ip, amd64_cond, op->r_arg[0], sign);
	} else {
		common86_xchg_reg_reg(jit->ip, COMMON86_AX, op->r_arg[0], REG_SIZE);
		common86_mov_reg_imm(jit->ip, COMMON86_AX, 0);
		common86_set_reg(jit->ip, amd64_cond, COMMON86_AX, sign);
		common86_xchg_reg_reg(jit->ip, COMMON86_AX, op->r_arg[0], REG_SIZE);
	}
}

static void emit_branch_op(struct jit * jit, struct jit_op * op, int cond, int imm, int sign)
{
	if (imm) common86_alu_reg_imm(jit->ip, X86_CMP, op->r_arg[1], op->r_arg[2]);
	else common86_alu_reg_reg(jit->ip, X86_CMP, op->r_arg[1], op->r_arg[2]);

	op->patch_addr = JIT_BUFFER_OFFSET(jit);

	common86_branch_disp32(jit->ip, cond, JIT_GET_ADDR(jit, op->r_arg[0]), sign);
}

static void emit_branch_mask_op(struct jit * jit, struct jit_op * op, int cond, int imm)
{
	if (imm) common86_test_reg_imm(jit->ip, op->r_arg[1], op->r_arg[2]);
	else common86_test_reg_reg(jit->ip, op->r_arg[1], op->r_arg[2]);

	op->patch_addr = JIT_BUFFER_OFFSET(jit);

	common86_branch_disp32(jit->ip, cond, JIT_GET_ADDR(jit, op->r_arg[0]), 0);
}

static void emit_branch_overflow_op(struct jit * jit, struct jit_op * op, int alu_op, int imm, int negation)
{
	if (imm) common86_alu_reg_imm(jit->ip, alu_op, op->r_arg[1], op->r_arg[2]);
	else common86_alu_reg_reg(jit->ip, alu_op, op->r_arg[1], op->r_arg[2]);

	op->patch_addr = JIT_BUFFER_OFFSET(jit);

	if (!negation) common86_branch_disp32(jit->ip, X86_CC_O, JIT_GET_ADDR(jit, op->r_arg[0]), 0);
	else common86_branch_disp32(jit->ip, X86_CC_NO, JIT_GET_ADDR(jit, op->r_arg[0]), 0);
}

/* determines whether the argument value was spilled out or not,
 * if the register is associated with the hardware register
 * it is returned through the reg argument
 */
// FIXME: reports as spilled also argument which contains appropriate value
static int is_spilled(jit_value arg_id, jit_op * prepare_op, int * reg)
{
        jit_hw_reg * hreg = rmap_get(prepare_op->regmap, arg_id);

        if (hreg) {
                *reg = hreg->id;
                return 0;
        } else return 1;
}

/**
 * Emits all LD operations
 */
static void emit_ld_op(struct jit * jit, jit_op * op, jit_value a1, jit_value a2)
{
	if (op->arg_size == REG_SIZE) {
		if (IS_IMM(op)) common86_mov_reg_mem(jit->ip, a1, a2, op->arg_size);
		else common86_mov_reg_membase(jit->ip, a1, a2, 0, op->arg_size);
		return;
	}

	switch (op->code) {
		case (JIT_LD | IMM | SIGNED): common86_movsx_reg_mem(jit->ip, a1, a2, op->arg_size); break;
		case (JIT_LD | IMM | UNSIGNED): common86_movzx_reg_mem(jit->ip, a1, a2, op->arg_size); break;
		case (JIT_LD | REG | SIGNED): common86_movsx_reg_membase(jit->ip, a1, a2, 0, op->arg_size); break;
		case (JIT_LD | REG | UNSIGNED): common86_movzx_reg_membase(jit->ip, a1, a2, 0, op->arg_size); break;
		default: assert(0);
	}
}

/**
 * Emits all LDX operations
 */
static void emit_ldx_op(struct jit * jit, jit_op * op, jit_value a1, jit_value a2, jit_value a3)
{
	if (op->arg_size == REG_SIZE) {
		if (IS_IMM(op)) common86_mov_reg_membase(jit->ip, a1, a2, a3, op->arg_size);
		else common86_mov_reg_memindex(jit->ip, a1, a2, 0, a3, 0, op->arg_size);
		return;
	}

	switch (op->code) {
		case (JIT_LDX | IMM | SIGNED): common86_movsx_reg_membase(jit->ip, a1, a2, a3, op->arg_size); break;
		case (JIT_LDX | IMM | UNSIGNED): common86_movzx_reg_membase(jit->ip, a1, a2, a3, op->arg_size); break;
		case (JIT_LDX | REG | SIGNED): common86_movsx_reg_memindex(jit->ip, a1, a2, 0, a3, 0, op->arg_size); break;
		case (JIT_LDX | REG | UNSIGNED): common86_movzx_reg_memindex(jit->ip, a1, a2, 0, a3, 0, op->arg_size); break;
		default: assert(0);
	}
}

struct transfer_info {
	int sourcereg;
	int destreg;
	int scrapreg;
	int scrap_in_use;
	int counterreg;
	int counter_in_use;
	int block_size;
	unsigned char *loop_addr;
};

static void emit_transfer_init(struct jit * jit, jit_op * op, jit_value destreg, jit_value srcreg, jit_value cnt, int block_size)
{
	struct transfer_info *tinf = JIT_MALLOC(sizeof(struct transfer_info));
	tinf->sourcereg = srcreg;
	tinf->destreg = destreg;
	tinf->block_size = block_size;

	jit_hw_reg * scrap = jit_get_unused_reg_with_index(jit->reg_al, op, 0, 0);
	if (scrap) tinf->scrapreg = scrap->id;
	else {
		for (int i = 0; i < jit->reg_al->gp_reg_cnt; i++) {
			jit_hw_reg *r = &jit->reg_al->gp_regs[i];
			if ((r->id != srcreg) && (r->id != destreg) && (!IS_IMM(op) && (r->id != cnt))) {
				tinf->scrapreg = r->id;
				break;
			}
		}
	}
	//tinf->scrapreg = (scrap ? scrap->id : COMMON86_AX);
	tinf->scrap_in_use = jit_reg_in_use(op, tinf->scrapreg, 0);

	if (IS_IMM(op)) {
		jit_hw_reg * counter = jit_get_unused_reg_with_index(jit->reg_al, op, 0, 1);
		if (counter) tinf->counterreg = counter->id;
		else {
			for (int i = 0; i < jit->reg_al->gp_reg_cnt; i++) {
				jit_hw_reg *r = &jit->reg_al->gp_regs[i];
				if ((r->id != srcreg) && (r->id != destreg) && (r->id != tinf->scrapreg)) {
					tinf->counterreg = r->id;
					break;
				}
			}
		}
//		tinf->counterreg = (counter ? counter->id : COMMON86_CX);
		tinf->counter_in_use = jit_reg_in_use(op, tinf->counterreg, 0);
	} else {
		if (jit_set_get(op->live_out, op->arg[2])) {
			jit_hw_reg * counter = jit_get_unused_reg_with_index(jit->reg_al, op, 0, 1);
			tinf->counterreg = (counter ? counter->id : cnt);
			tinf->counter_in_use = jit_reg_in_use(op, tinf->counterreg, 0);
		} else {
			tinf->counterreg = cnt;
			tinf->counter_in_use = 0;
		}
	}

	if (tinf->counter_in_use) common86_mov_membase_reg(jit->ip, COMMON86_SP, -REG_SIZE, tinf->counterreg, REG_SIZE);
	if (tinf->scrap_in_use) common86_mov_membase_reg(jit->ip, COMMON86_SP, -REG_SIZE * 2, tinf->scrapreg, REG_SIZE);

	if (IS_IMM(op)) common86_mov_reg_imm(jit->ip, tinf->counterreg, cnt * block_size);
	else if ((tinf->counterreg != cnt) || block_size > 1) {
		int shift;
		if (block_size == 1) shift = 0;
		else if (block_size == 2) shift = 1;
		else if (block_size == 4) shift = 2;
		else if (block_size == 8) shift = 3;
		else assert(0);
		common86_lea_memindex(jit->ip, tinf->counterreg, X86_NOBASEREG, 0, cnt, shift);
	}

////////////////

	tinf->loop_addr = jit->ip;
	op->addendum = tinf;

	if (block_size == REG_SIZE) common86_mov_reg_memindex(jit->ip, tinf->scrapreg, srcreg, -block_size, tinf->counterreg, 0, block_size);
	else common86_movsx_reg_memindex(jit->ip, tinf->scrapreg, srcreg, -block_size, tinf->counterreg, 0, block_size);
}

static void emit_transfer_loop(struct jit *jit, jit_op *op)
{
	struct transfer_info *tinf = (struct transfer_info *)op->addendum;
	jit_value loop = (jit_value) tinf->loop_addr;

	common86_mov_memindex_reg(jit->ip, tinf->destreg, -tinf->block_size, tinf->counterreg, 0, tinf->scrapreg, tinf->block_size);
	common86_alu_reg_imm(jit->ip, X86_SUB, tinf->counterreg, tinf->block_size);
	common86_branch_disp(jit->ip, X86_CC_NZ, loop - (jit_value) jit->ip, 0);

	if (tinf->counter_in_use) common86_mov_reg_membase(jit->ip, tinf->counterreg, COMMON86_SP, -REG_SIZE, REG_SIZE);
	if (tinf->scrap_in_use) common86_mov_reg_membase(jit->ip, tinf->scrapreg, COMMON86_SP, -REG_SIZE * 2, REG_SIZE);
}

static void emit_transfer_op(struct jit *jit, jit_op *op, int alu_op)
{
	jit_op *init_op = op->prev;
	while (GET_OP(init_op) != JIT_TRANSFER)
		init_op = init_op->prev;

	struct transfer_info *tinf = (struct transfer_info *)init_op->addendum;

	if (op->arg[1] == R_OUT) {
		common86_alu_reg_memindex(jit->ip, alu_op, tinf->scrapreg, tinf->destreg, -tinf->block_size, tinf->counterreg, 0);
	} else if (op->r_arg[1] != -1) {
		if ((op->r_arg[1] == tinf->counterreg) && (tinf->counter_in_use)) {
			common86_alu_reg_membase(jit->ip, alu_op, tinf->scrapreg, COMMON86_SP, -REG_SIZE);
		} else if ((op->r_arg[1] == tinf->scrapreg) && (tinf->scrap_in_use)) {
			common86_alu_reg_membase(jit->ip, alu_op, tinf->scrapreg, COMMON86_SP, -REG_SIZE * 2);
		} else common86_alu_reg_reg(jit->ip, alu_op, tinf->scrapreg, op->r_arg[1]);
	}
	else common86_alu_reg_membase(jit->ip, alu_op, tinf->scrapreg, COMMON86_BP, GET_REG_POS(jit, op->arg[1]));


	if (op->arg[0]) emit_transfer_loop(jit, (jit_op *)op->arg[0]);
}

static void emit_memcpy(struct jit * jit, jit_op * op, jit_value a1, jit_value a2, jit_value a3)
{
	emit_transfer_init(jit, op, a1, a2, a3, 1);
	emit_transfer_loop(jit, op);
}

static void emit_memset(struct jit *jit, jit_op *op, jit_value a1, jit_value a2, jit_value a3, int block_size)
{
	jit_hw_reg * counter = jit_get_unused_reg_with_index(jit->reg_al, op, 0, 0);
	int counterreg = 0;
	if (counter) counterreg = counter->id;
	else {
		// FIXME: duplicitni kod s funkcemi pro transfer
		for (int i = 0; i < jit->reg_al->gp_reg_cnt; i++) {
			jit_hw_reg *r = &jit->reg_al->gp_regs[i];
			if ((r->id != a1) && (r->id != a2) && (!IS_IMM(op) && (r->id != a3))) {
				counterreg = r->id;
				break;
			}
		}
	}
//        int counterreg = (counter ? counter->id : COMMON86_CX);
        int counter_in_use = jit_reg_in_use(op, counterreg, 0);
	if (counter_in_use) common86_push_reg(jit->ip, counterreg);
	common86_mov_reg_reg(jit->ip, counterreg, a2, REG_SIZE);
	if (block_size == 2) common86_shift_reg_imm(jit->ip, X86_SHL, counterreg, 1);
	if (block_size == 4) common86_shift_reg_imm(jit->ip, X86_SHL, counterreg, 2);
	if (block_size == 8) common86_shift_reg_imm(jit->ip, X86_SHL, counterreg, 3);


	jit_value loop = (jit_value) jit->ip;
	if (IS_IMM(op))
		common86_mov_memindex_imm(jit->ip, a1, -block_size, counterreg, 0, a3, block_size);
	else
		common86_mov_memindex_reg(jit->ip, a1, -block_size, counterreg, 0, a3, block_size);
	common86_alu_reg_imm(jit->ip, X86_SUB, counterreg, block_size);
	common86_branch_disp(jit->ip, X86_CC_NZ, loop - (jit_value) jit->ip, 0);

	if (counter_in_use) common86_pop_reg(jit->ip, counterreg);
}


int jit_allocai(struct jit * jit, int size)
{
	jit_value real_size = jit_value_align(size, JIT_STACK_ALIGNMENT);

	jit_add_op(jit, JIT_ALLOCA | IMM, SPEC(IMM, NO, NO), real_size, 0, 0, 0, NULL);
	jit_current_func_info(jit)->allocai_mem += real_size;

	return -(jit_current_func_info(jit)->allocai_mem);
}

void jit_patch_local_addrs(struct jit *jit)
{
	for (jit_op * op = jit_op_first(jit->ops); op != NULL; op = op->next) {
		if ((GET_OP(op) == JIT_REF_CODE) || (GET_OP(op) == JIT_REF_DATA)) {
			unsigned char *buf = jit->buf + (intptr_t) op->patch_addr;
			jit_value addr = jit_is_label(jit, (void *)op->arg[1]) ? ((jit_label *)op->arg[1])->pos : op->arg[1];
			common86_mov_reg_imm(buf, op->r_arg[0], jit->buf + addr);
		}


		if ((GET_OP(op) == JIT_DATA_REF_CODE) || (GET_OP(op) == JIT_DATA_REF_DATA)) {
			unsigned char *buf = jit->buf + (intptr_t) op->patch_addr;
			jit_value addr = jit_is_label(jit, (void *)op->arg[0]) ? ((jit_label *)op->arg[0])->pos : op->arg[0];
			*((jit_value *)buf) = (jit_value) (jit->buf + addr);
		}
	}
}

//
//
// Main switch
//
//
void jit_gen_op(struct jit * jit, struct jit_op * op)
{
	jit_value a1 = op->r_arg[0];
	jit_value a2 = op->r_arg[1];
	jit_value a3 = op->r_arg[2];
	int imm = IS_IMM(op);
	int sign = IS_SIGNED(op);

	int found = 1;

	switch (GET_OP(op)) {
		case JIT_ADD:
			if ((a1 != a2) && (a1 != a3)) {
				if (imm) common86_lea_membase(jit->ip, a1, a2, a3);
				else common86_lea_memindex(jit->ip, a1, a2, 0, a3, 0);
			} else emit_alu_op(jit, op, X86_ADD, imm);
			break;

		case JIT_ADDC: 	emit_alu_op(jit, op, X86_ADD, imm); break;
		case JIT_ADDX: 	emit_alu_op(jit, op, X86_ADC, imm); break;
		case JIT_SUB: 	emit_sub_op(jit, op, imm); break;
		case JIT_SUBC: 	emit_subx_op(jit, op, X86_SUB, imm); break;
		case JIT_SUBX: 	emit_subx_op(jit, op, X86_SBB, imm); break;
		case JIT_RSB: 	emit_rsb_op(jit, op, imm); break;
		case JIT_NEG:
				if (a1 != a2) common86_mov_reg_reg(jit->ip, a1, a2, REG_SIZE);
				common86_neg_reg(jit->ip, a1);
				break;
		case JIT_OR: 	emit_alu_op(jit, op, X86_OR, imm); break;
		case JIT_XOR: 	emit_alu_op(jit, op, X86_XOR, imm); break;
		case JIT_AND: 	emit_alu_op(jit, op, X86_AND, imm); break;
		case JIT_NOT: 	if (a1 != a2) common86_mov_reg_reg(jit->ip, a1, a2, REG_SIZE);
			      	common86_not_reg(jit->ip, a1);
			      	break;
		case JIT_LSH: 	emit_shift_op(jit, op, X86_SHL, imm); break;
		case JIT_RSH: 	emit_shift_op(jit, op, sign ? X86_SAR : X86_SHR, imm); break;

		case JIT_LT: 	emit_cond_op(jit, op, X86_CC_LT, imm, sign); break;
		case JIT_LE: 	emit_cond_op(jit, op, X86_CC_LE, imm, sign); break;
		case JIT_GT: 	emit_cond_op(jit, op, X86_CC_GT, imm, sign); break;
		case JIT_GE: 	emit_cond_op(jit, op, X86_CC_GE, imm, sign); break;
		case JIT_EQ: 	emit_cond_op(jit, op, X86_CC_EQ, imm, sign); break;
		case JIT_NE: 	emit_cond_op(jit, op, X86_CC_NE, imm, sign); break;

		case JIT_BLT: 	emit_branch_op(jit, op, X86_CC_LT, imm, sign); break;
		case JIT_BLE: 	emit_branch_op(jit, op, X86_CC_LE, imm, sign); break;
		case JIT_BGT: 	emit_branch_op(jit, op, X86_CC_GT, imm, sign); break;
		case JIT_BGE: 	emit_branch_op(jit, op, X86_CC_GE, imm, sign); break;
		case JIT_BEQ: 	emit_branch_op(jit, op, X86_CC_EQ, imm, sign); break;
		case JIT_BNE: 	emit_branch_op(jit, op, X86_CC_NE, imm, sign); break;

		case JIT_BMS: 	emit_branch_mask_op(jit, op, X86_CC_NZ, imm); break;
		case JIT_BMC: 	emit_branch_mask_op(jit, op, X86_CC_Z, imm); break;

		case JIT_BOADD: emit_branch_overflow_op(jit, op, X86_ADD, imm, 0); break;
		case JIT_BOSUB: emit_branch_overflow_op(jit, op, X86_SUB, imm, 0); break;

		case JIT_BNOADD: emit_branch_overflow_op(jit, op, X86_ADD, imm, 1); break;
		case JIT_BNOSUB: emit_branch_overflow_op(jit, op, X86_SUB, imm, 1); break;

		case JIT_MUL: 	emit_mul_op(jit, op, imm, sign, 0); break;
		case JIT_HMUL: 	emit_mul_op(jit, op, imm, sign, 1); break;
		case JIT_DIV: 	emit_div_op(jit, op, imm, sign, 0); break;
		case JIT_MOD: 	emit_div_op(jit, op, imm, sign, 1); break;

		case JIT_CALL: 	emit_funcall(jit, op, imm); break;
		case JIT_PATCH: do {
					struct jit_op *target = (struct jit_op *) a1;
					if (!target->in_use) break;
					switch (GET_OP(target)) {
						case JIT_REF_CODE:
						case JIT_REF_DATA:
							target->arg[1] = JIT_BUFFER_OFFSET(jit);
							break;
						case JIT_DATA_REF_CODE:
						case JIT_DATA_REF_DATA:
							target->arg[0] = JIT_BUFFER_OFFSET(jit);
							break;
						default: {
							jit_value pa = target->patch_addr;
							common86_patch(jit->buf + pa, jit->ip);
						}

					}
				} while (0);
				break;
		case JIT_JMP:
			op->patch_addr = JIT_BUFFER_OFFSET(jit);
			if (op->code & REG) common86_jump_reg(jit->ip, a1);
			else common86_jump_disp32(jit->ip, JIT_GET_ADDR(jit, a1));
			break;
		case JIT_RET:
			if (!imm && (a1 != COMMON86_AX)) common86_mov_reg_reg(jit->ip, COMMON86_AX, a1, REG_SIZE);
			if (imm) common86_mov_reg_imm(jit->ip, COMMON86_AX, a1);
			emit_pop_callee_saved_regs(jit);
			if (jit_current_func_info(jit)->has_prolog) {
				common86_mov_reg_reg(jit->ip, COMMON86_SP, COMMON86_BP, REG_SIZE);
				common86_pop_reg(jit->ip, COMMON86_BP);
			}
			common86_ret(jit->ip);
			break;

		case JIT_PUTARG: funcall_put_arg(jit, op); break;
		case JIT_FPUTARG: funcall_fput_arg(jit, op); break;
		case JIT_GETARG: emit_get_arg(jit, op); break;
		case JIT_MSG: 	emit_msg_op(jit, op); break;
		case JIT_FMSG: 	emit_fmsg_op(jit, op); break;
		case JIT_TRACE: emit_trace_op(jit, op);
				while (((uintptr_t) jit->ip) % 16)
					common86_nop(jit->ip);
				break;

		case JIT_LD: 	emit_ld_op(jit, op, a1, a2); break;
		case JIT_LDX: 	emit_ldx_op(jit, op, a1, a2, a3); break;
		case JIT_FST: 	emit_sse_fst_op(jit, op, a1, a2); break;
		case JIT_FSTX: 	emit_sse_fstx_op(jit, op, a1, a2, a3); break;
		case JIT_FLD: 	emit_sse_fld_op(jit, op, a1, a2); break;
		case JIT_FLDX: 	emit_sse_fldx_op(jit, op, a1, a2, a3); break;
		case JIT_MEMCPY: emit_memcpy(jit, op, a1, a2, a3); break;
		case JIT_MEMSET: emit_memset(jit, op, a1, a2, a3, op->arg_size); break;
		case JIT_TRANSFER: emit_transfer_init(jit, op, a1, a2, a3, op->arg_size); break;
		case JIT_TRANSFER_CPY: emit_transfer_loop(jit, (jit_op *)a1); break;
		case JIT_TRANSFER_XOR: emit_transfer_op(jit, op, X86_XOR); break;
		case JIT_TRANSFER_AND: emit_transfer_op(jit, op, X86_AND); break;
		case JIT_TRANSFER_OR:  emit_transfer_op(jit, op, X86_OR); break;
		case JIT_TRANSFER_ADD: emit_transfer_op(jit, op, X86_ADD); break;
		case JIT_TRANSFER_SUB: emit_transfer_op(jit, op, X86_SUB); break;

		case JIT_ALLOCA: break;
		case JIT_DECL_ARG: break;
		case JIT_RETVAL: break; // reg. allocator takes care of the proper register assignment
		case JIT_LABEL: ((jit_label *)a1)->pos = JIT_BUFFER_OFFSET(jit); break;

		case JIT_CODE_ALIGN:
				while (((uintptr_t) jit->ip) % op->arg[0])
					common86_nop(jit->ip);
				break;

		case JIT_REF_CODE:
		case JIT_REF_DATA:
			op->patch_addr = JIT_BUFFER_OFFSET(jit);
			common86_mov_reg_imm_size(jit->ip, a1, 0xdeadbeefcafebabe, sizeof(void *));
			break;

		// platform independent opcodes handled in the jitlib-core.c
		case JIT_DATA_BYTE: break;
		case JIT_FULL_SPILL: break;


		default: found = 0;
	}

	if (found) return;


	switch (op->code) {
		case (JIT_MOV | REG): if (a1 != a2) common86_mov_reg_reg(jit->ip, a1, a2, REG_SIZE); break;
		case (JIT_MOV | IMM):
			if (a2 == 0) common86_alu_reg_reg(jit->ip, X86_XOR, a1, a1);
			else common86_mov_reg_imm_size(jit->ip, a1, a2, 8);
			break;

		case JIT_PREPARE: funcall_prepare(jit, op, a1 + a2);
				  jit->push_count += emit_push_caller_saved_regs(jit, op);
				  break;

		case JIT_PROLOG: emit_prolog_op(jit, op); break;
		case (JIT_ST | IMM): common86_mov_mem_reg(jit->ip, a1, a2, op->arg_size); break;
		case (JIT_ST | REG): common86_mov_membase_reg(jit->ip, a1, 0, a2, op->arg_size); break;
		case (JIT_STX | IMM): common86_mov_membase_reg(jit->ip, a2, a1, a3, op->arg_size); break;
		case (JIT_STX | REG): common86_mov_memindex_reg(jit->ip, a1, 0, a2, 0, a3, op->arg_size); break;

		//
		// Floating-point operations;
		//
		case (JIT_FMOV | REG): sse_movsd_reg_reg(jit->ip, a1, a2); break;
		case (JIT_FMOV | IMM): sse_mov_reg_safeimm(jit, op, a1, &op->flt_imm); break;
		case (JIT_FADD | REG): emit_sse_alu_op(jit, op, X86_SSE_ADD); break;
		case (JIT_FSUB | REG): emit_sse_sub_op(jit, op, a1, a2, a3); break;
		case (JIT_FRSB | REG): emit_sse_sub_op(jit, op, a1, a3, a2); break;
		case (JIT_FMUL | REG): emit_sse_alu_op(jit, op, X86_SSE_MUL); break;
		case (JIT_FDIV | REG): emit_sse_div_op(jit, a1, a2, a3); break;
                case (JIT_FNEG | REG): emit_sse_neg_op(jit, op, a1, a2); break;
		case (JIT_FBLT | REG): emit_sse_branch(jit, op, a1, a2, a3, X86_CC_LT); break;
                case (JIT_FBGT | REG): emit_sse_branch(jit, op, a1, a2, a3, X86_CC_GT); break;
                case (JIT_FBGE | REG): emit_sse_branch(jit, op, a1, a2, a3, X86_CC_GE); break;
                case (JIT_FBLE | REG): emit_sse_branch(jit, op, a1, a3, a2, X86_CC_GE); break;
                case (JIT_FBEQ | REG): emit_sse_branch(jit, op, a1, a3, a2, X86_CC_EQ); break;
                case (JIT_FBNE | REG): emit_sse_branch(jit, op, a1, a3, a2, X86_CC_NE); break;

		case (JIT_EXT | REG): sse_cvtsi2sd_reg_reg(jit->ip, a1, a2); break;
                case (JIT_TRUNC | REG): sse_cvttsd2si_reg_reg(jit->ip, a1, a2); break;
		case (JIT_CEIL | REG): emit_sse_floor(jit, a1, a2, 0); break;
                case (JIT_FLOOR | REG): emit_sse_floor(jit, a1, a2, 1); break;
		case (JIT_ROUND | REG): emit_sse_round(jit, op, a1, a2); break;

		case (JIT_FRET | REG): emit_fret_op(jit, op); break;
		case JIT_FRETVAL: emit_fretval_op(jit, op); break;

		case (JIT_UREG): emit_ureg(jit, a1, a2); break;
		case (JIT_LREG): emit_lreg(jit, a1, a2); break;
		case (JIT_SYNCREG):  emit_ureg(jit, a1, a2); break;
		case JIT_RENAMEREG: common86_mov_reg_reg(jit->ip, a1, a2, REG_SIZE); break;

		case JIT_CODESTART: break;
		case JIT_NOP: break;


		// platform specific opcodes; used by optimizer
		case (JIT_X86_STI | IMM): common86_mov_mem_imm(jit->ip, a1, a2, op->arg_size); break;
		case (JIT_X86_STI | REG): common86_mov_membase_imm(jit->ip, a1, 0, a2, op->arg_size); break;
		case (JIT_X86_STXI | IMM): common86_mov_membase_imm(jit->ip, a2, a1, a3, op->arg_size); break;
		case (JIT_X86_STXI | REG): common86_mov_memindex_imm(jit->ip, a1, 0, a2, 0, a3, op->arg_size); break;
		case (JIT_X86_ADDMUL | REG): common86_lea_memindex(jit->ip, a1, a2, 0, a3, op->arg_size); break;
		case (JIT_X86_ADDMUL | IMM): common86_lea_memindex(jit->ip, a1, X86_NOBASEREG, a3, a2, op->arg_size); break;
		case (JIT_X86_ADDIMM): {
			jit_value tmp;
			memcpy(&tmp, &op->flt_imm, sizeof(jit_value));
			common86_lea_memindex(jit->ip, a1, a2, tmp, a3, 0); break;
		}

		default: printf("common86: unknown operation (opcode: 0x%x)\n", GET_OP(op) >> 3);
	}
}
