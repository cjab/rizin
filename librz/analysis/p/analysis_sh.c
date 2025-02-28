// SPDX-FileCopyrightText: 2010-2013 eloi <limited-entropy.com>
// SPDX-License-Identifier: LGPL-3.0-only

#include <string.h>
#include <rz_types.h>
#include <rz_lib.h>
#include <rz_asm.h>
#include <rz_analysis.h>
#include "../arch/sh/sh_il.h"

#define API static

#define LONG_SIZE 4
#define WORD_SIZE 2
#define BYTE_SIZE 1

/*
 * all processor instructions are implemented, but all FPU are missing
 * DIV1,MAC.W,MAC.L,rte,rts should be checked
 * also tests are written, but still could be some issues, if I misunderstand instructions.
 * If you found some bugs, please open an issue
 */

#define BIT_32(x)  x ",0x80000000,&"
#define S16_EXT(x) x ",DUP,0x8000,&,?{,0xFFFFFFFFFFFF0000,|,}"
#define S32_EXT(x) x ",DUP,0x80000000,&,?{,0xFFFFFFFF00000000,|,}"
#define IS_T       "sr,0x1,&,"
#define SET_T      "0x1,sr,|="
#define CLR_T      "0xFFFFFFFE,sr,&="
// Macros for different instruction types

#define IS_CLRT(x)   x == 0x0008
#define IS_NOP(x)    x == 0x0009
#define IS_RTS(x)    x == 0x000b
#define IS_SETT(x)   x == 0x0018
#define IS_DIV0U(x)  x == 0x0019
#define IS_SLEEP(x)  x == 0x001b
#define IS_CLRMAC(x) x == 0x0028
#define IS_RTE(x)    x == 0x002b

#define IS_STCSR1(x)            (((x)&0xF0CF) == 0x0002) // mask stc Rn,{SR,gbr,VBR,SSR}
#define IS_BSRF(x)              ((x)&0xf0ff) == 0x0003
#define IS_BRAF(x)              (((x)&0xf0ff) == 0x0023)
#define IS_MOVB_REG_TO_R0REL(x) (((x)&0xF00F) == 0x0004)
#define IS_MOVW_REG_TO_R0REL(x) (((x)&0xF00F) == 0x0005)
#define IS_MOVL_REG_TO_R0REL(x) (((x)&0xF00F) == 0x0006)
#define IS_MULL(x)              (((x)&0xF00F) == 0x0007)
#define IS_MOVB_R0REL_TO_REG(x) (((x)&0xF00F) == 0x000C)
#define IS_MOVW_R0REL_TO_REG(x) (((x)&0xF00F) == 0x000D)
#define IS_MOVL_R0REL_TO_REG(x) (((x)&0xF00F) == 0x000E)
#define IS_MACL(x)              (((x)&0xF00F) == 0x000F)
#define IS_MOVT(x)              (((x)&0xF0FF) == 0x0029)
#define IS_STSMACH(x)           (((x)&0xF0FF) == 0x000A)
#define IS_STSMACL(x)           (((x)&0xF0FF) == 0x001A)
#define IS_STSPR(x)             (((x)&0xF0FF) == 0x002A)
//#define IS_STSFPUL(x)	(((x) & 0xF0FF) == 0x005A)	//FP*: todo maybe someday
//#define IS_STSFPSCR(x)	(((x) & 0xF0FF) == 0x006A)
#define IS_MOVB_REG_TO_REGREF(x) (((x)&0xF00F) == 0x2000)
#define IS_MOVW_REG_TO_REGREF(x) (((x)&0xF00F) == 0x2001)
#define IS_MOVL_REG_TO_REGREF(x) (((x)&0xF00F) == 0x2002)
//#define invalid?(x)	(((x) & 0xF00F) == 0x2003)	//illegal on sh2e
#define IS_PUSHB(x)    (((x)&0xF00F) == 0x2004)
#define IS_PUSHW(x)    (((x)&0xF00F) == 0x2005)
#define IS_PUSHL(x)    (((x)&0xF00F) == 0x2006)
#define IS_DIV0S(x)    (((x)&0xF00F) == 0x2007)
#define IS_TSTRR(x)    (((x)&0xF00F) == 0x2008)
#define IS_AND_REGS(x) (((x)&0xF00F) == 0x2009)
#define IS_XOR_REGS(x) (((x)&0xF00F) == 0x200A)
#define IS_OR_REGS(x)  (((x)&0xF00F) == 0x200B)
#define IS_CMPSTR(x)   (((x)&0xF00F) == 0x200C)
#define IS_XTRCT(x)    (((x)&0xF00F) == 0x200D)
#define IS_MULUW(x)    (((x)&0xF00F) == 0x200E)
#define IS_MULSW(x)    (((x)&0xF00F) == 0x200F)
#define IS_CMPEQ(x)    (((x)&0xF00F) == 0x3000)
//#define invalid?(x)	(((x) & 0xF00F) == 0x3001)
#define IS_CMPHS(x) (((x)&0xF00F) == 0x3002)
#define IS_CMPGE(x) (((x)&0xF00F) == 0x3003)
#define IS_CMPHI(x) (((x)&0xF00F) == 0x3006)
#define IS_CMPGT(x) (((x)&0xF00F) == 0x3007)
#define IS_DIV1(x)  (((x)&0xF00F) == 0x3004)
#define IS_DMULU(x) (((x)&0xF00F) == 0x3005)
#define IS_DMULS(x) (((x)&0xF00F) == 0x300D)
#define IS_SUB(x)   (((x)&0xF00F) == 0x3008)
//#define invalid?(x)	(((x) & 0xF00F) == 0x3009)
#define IS_SUBC(x)      (((x)&0xF00F) == 0x300A)
#define IS_SUBV(x)      (((x)&0xF00F) == 0x300B)
#define IS_ADD(x)       (((x)&0xF00F) == 0x300C)
#define IS_ADDC(x)      (((x)&0xF00F) == 0x300E)
#define IS_ADDV(x)      (((x)&0xF00F) == 0x300F)
#define IS_MACW(x)      (((x)&0xF00F) == 0x400F)
#define IS_JSR(x)       (((x)&0xf0ff) == 0x400b)
#define IS_JMP(x)       (((x)&0xf0ff) == 0x402b)
#define IS_CMPPL(x)     (((x)&0xf0ff) == 0x4015)
#define IS_CMPPZ(x)     (((x)&0xf0ff) == 0x4011)
#define IS_LDCSR(x)     (((x)&0xF0FF) == 0x400E)
#define IS_LDCGBR(x)    (((x)&0xF0FF) == 0x401E)
#define IS_LDCVBR(x)    (((x)&0xF0FF) == 0x402E)
#define IS_LDCLSR(x)    (((x)&0xF0FF) == 0x4007)
#define IS_LDCLSRGBR(x) (((x)&0xF0FF) == 0x4017)
#define IS_LDCLSRVBR(x) (((x)&0xF0FF) == 0x4027)
#define IS_LDSMACH(x)   (((x)&0xF0FF) == 0x400A)
#define IS_LDSMACL(x)   (((x)&0xF0FF) == 0x401A)
#define IS_LDSLMACH(x)  (((x)&0xF0FF) == 0x4006)
#define IS_LDSLMACL(x)  (((x)&0xF0FF) == 0x4016)
#define IS_LDSPR(x)     (((x)&0xF0FF) == 0x402A)
#define IS_LDSLPR(x)    (((x)&0xF0FF) == 0x4026)
//#define IS_LDSFPUL(x)	(((x) & 0xF0FF) == 0x405A)	//FP*: todo maybe someday
//#define IS_LDSFPSCR(x)	(((x) & 0xF0FF) == 0x406A)
//#define IS_LDSLFPUL(x)	(((x) & 0xF0FF) == 0x4066)
//#define IS_LDSLFPSCR(x)	(((x) & 0xF0FF) == 0x4056)
#define IS_ROTCR(x) (((x)&0xF0FF) == 0x4025)
#define IS_ROTCL(x) (((x)&0xF0FF) == 0x4024)
#define IS_ROTL(x)  (((x)&0xF0FF) == 0x4004)
#define IS_ROTR(x)  (((x)&0xF0FF) == 0x4005)
// not on sh2e : shad, shld

//#define IS_SHIFT1(x)	(((x) & 0xF0DE) == 0x4000)	//unused (treated as switch-case)
// other shl{l,r}{,2,8,16} in switch case also.

#define IS_STSLMACL(x) (((x)&0xF0FF) == 0x4012)
#define IS_STSLMACH(x) (((x)&0xF0FF) == 0x4002)
#define IS_STCLSR(x)   (((x)&0xF0FF) == 0x4003)
#define IS_STCLGBR(x)  (((x)&0xF0FF) == 0x4013)
#define IS_STCLVBR(x)  (((x)&0xF0FF) == 0x4023)
// todo: other stc.l not on sh2e
#define IS_STSLPR(x) (((x)&0xF0FF) == 0x4022)
//#define IS_STSLFPUL(x)	(((x) & 0xF0FF) == 0x4052)
//#define IS_STSLFPSCR(x)	(((x) & 0xF0FF) == 0x4062)
#define IS_TASB(x) (((x)&0xF0FF) == 0x401B)
#define IS_DT(x)   (((x)&0xF0FF) == 0x4010)

#define IS_MOVB_REGREF_TO_REG(x) (((x)&0xF00F) == 0x6000)
#define IS_MOVW_REGREF_TO_REG(x) (((x)&0xF00F) == 0x6001)
#define IS_MOVL_REGREF_TO_REG(x) (((x)&0xF00F) == 0x6002)
#define IS_MOV_REGS(x)           (((x)&0xf00f) == 0x6003)
#define IS_MOVB_POP(x)           (((x)&0xF00F) == 0x6004)
#define IS_MOVW_POP(x)           (((x)&0xF00F) == 0x6005)
#define IS_MOVL_POP(x)           (((x)&0xF00F) == 0x6006)
#define IS_NOT(x)                (((x)&0xF00F) == 0x6007)
#define IS_SWAPB(x)              (((x)&0xF00F) == 0x6008)
#define IS_SWAPW(x)              (((x)&0xF00F) == 0x6009)
#define IS_NEG(x)                (((x)&0xF00F) == 0x600B)
#define IS_NEGC(x)               (((x)&0xF00F) == 0x600A)
#define IS_EXT(x)                (((x)&0xF00C) == 0x600C) // match ext{s,u}.{b,w}

#define IS_MOVB_R0_REGDISP(x) (((x)&0xFF00) == 0x8000)
#define IS_MOVW_R0_REGDISP(x) (((x)&0xFF00) == 0x8100)
//#define illegal?(x)	(((x) & 0xF900) == 0x8000)	//match 8{2,3,6,7}00
#define IS_MOVB_REGDISP_R0(x) (((x)&0xFF00) == 0x8400)
#define IS_MOVW_REGDISP_R0(x) (((x)&0xFF00) == 0x8500)
#define IS_CMPIMM(x)          (((x)&0xFF00) == 0x8800)
//#define illegal?(x)	(((x) & 0xFB00) == 0x8A00)	//match 8{A,E}00
#define IS_BT(x)       (((x)&0xff00) == 0x8900)
#define IS_BF(x)       (((x)&0xff00) == 0x8B00)
#define IS_BTS(x)      (((x)&0xff00) == 0x8D00)
#define IS_BFS(x)      (((x)&0xff00) == 0x8F00)
#define IS_BT_OR_BF(x) IS_BT(x) || IS_BTS(x) || IS_BF(x) || IS_BFS(x)

#define IS_MOVB_R0_GBRREF(x)   (((x)&0xFF00) == 0xC000)
#define IS_MOVW_R0_GBRREF(x)   (((x)&0xFF00) == 0xC100)
#define IS_MOVL_R0_GBRREF(x)   (((x)&0xFF00) == 0xC200)
#define IS_TRAP(x)             (((x)&0xFF00) == 0xC300)
#define IS_MOVB_GBRREF_R0(x)   (((x)&0xFF00) == 0xC400)
#define IS_MOVW_GBRREF_R0(x)   (((x)&0xFF00) == 0xC500)
#define IS_MOVL_GBRREF_R0(x)   (((x)&0xFF00) == 0xC600)
#define IS_MOVA_PCREL_R0(x)    (((x)&0xFF00) == 0xC700)
#define IS_BINLOGIC_IMM_R0(x)  (((x)&0xFC00) == 0xC800) // match C{8,9,A,B}00
#define IS_BINLOGIC_IMM_GBR(x) (((x)&0xFC00) == 0xCC00) // match C{C,D,E,F}00 : *.b #imm, @(R0,gbr)

/* Compute PC-relative displacement for branch instructions */
#define GET_BRA_OFFSET(x) ((x)&0x0fff)
#define GET_BTF_OFFSET(x) ((x)&0x00ff)

/* Compute reg nr for BRAF,BSR,BSRF,JMP,JSR */
#define GET_TARGET_REG(x) (((x) >> 8) & 0x0f)
#define GET_SOURCE_REG(x) (((x) >> 4) & 0x0f)

/* index of PC reg in regs[] array*/
#define PC_IDX 16

/* for {bra,bsr} only: (sign-extend 12bit offset)<<1 + PC +4 */
static ut64 disarm_12bit_offset(RzAnalysisOp *op, unsigned int insoff) {
	ut64 off = insoff;
	/* sign extend if higher bit is 1 (0x0800) */
	if ((off & 0x0800) == 0x0800) {
		off |= ~0xFFF;
	}
	return (op->addr) + (off << 1) + 4;
}

/* for bt,bf sign-extended offsets : return PC+4+ (exts.b offset)<<1 */
static ut64 disarm_8bit_offset(ut64 pc, ut32 offs) {
	/* pc (really, op->addr) is 64 bits, so we need to sign-extend
	 * to 64 bits instead of the 32 the actual CPU does */
	ut64 off = offs;
	/* sign extend if higher bit is 1 (0x08) */
	if ((off & 0x80) == 0x80) {
		off |= ~0xFF;
	}
	return (off << 1) + pc + 4;
}

static char *regs[] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "pc" };

static RzAnalysisValue *analysis_fill_ai_rg(RzAnalysis *analysis, int idx) {
	RzAnalysisValue *ret = rz_analysis_value_new();
	ret->type = RZ_ANALYSIS_VAL_REG;
	ret->reg = rz_reg_get(analysis->reg, regs[idx], RZ_REG_TYPE_GPR);
	return ret;
}

static RzAnalysisValue *analysis_fill_im(RzAnalysis *analysis, st32 v) {
	RzAnalysisValue *ret = rz_analysis_value_new();
	ret->type = RZ_ANALYSIS_VAL_IMM;
	ret->imm = v;
	return ret;
}

/* Implements @(disp,Rn) , size=1 for .b, 2 for .w, 4 for .l */
static RzAnalysisValue *analysis_fill_reg_disp_mem(RzAnalysis *analysis, int reg, st64 delta, st64 size) {
	RzAnalysisValue *ret = analysis_fill_ai_rg(analysis, reg);
	ret->type = RZ_ANALYSIS_VAL_MEM;
	ret->memref = size;
	ret->delta = delta * size;
	return ret;
}

/* Rn */
static RzAnalysisValue *analysis_fill_reg_ref(RzAnalysis *analysis, int reg, st64 size) {
	RzAnalysisValue *ret = analysis_fill_ai_rg(analysis, reg);
	ret->type = RZ_ANALYSIS_VAL_MEM;
	ret->memref = size;
	return ret;
}

/* @(R0,Rx) references for all sizes */
static RzAnalysisValue *analysis_fill_r0_reg_ref(RzAnalysis *analysis, int reg, st64 size) {
	RzAnalysisValue *ret = analysis_fill_ai_rg(analysis, 0);
	ret->type = RZ_ANALYSIS_VAL_MEM;
	ret->regdelta = rz_reg_get(analysis->reg, regs[reg], RZ_REG_TYPE_GPR);
	ret->memref = size;
	return ret;
}

// @(disp,PC) for size=2(.w), size=4(.l). disp is 0-extended
static RzAnalysisValue *analysis_pcrel_disp_mov(RzAnalysis *analysis, RzAnalysisOp *op, ut8 disp, int size) {
	RzAnalysisValue *ret = rz_analysis_value_new();
	ret->type = RZ_ANALYSIS_VAL_MEM;
	if (size == 2) {
		ret->base = op->addr + 4;
		ret->delta = disp << 1;
	} else {
		ret->base = (op->addr + 4) & ~0x03;
		ret->delta = disp << 2;
	}
	ret->memref = size;
	return ret;
}

//= PC+4+R<reg>
static RzAnalysisValue *analysis_regrel_jump(RzAnalysis *analysis, RzAnalysisOp *op, ut8 reg) {
	RzAnalysisValue *ret = rz_analysis_value_new();
	ret->type = RZ_ANALYSIS_VAL_REG;
	ret->reg = rz_reg_get(analysis->reg, regs[reg], RZ_REG_TYPE_GPR);
	ret->base = op->addr + 4;
	return ret;
}

/* 16 decoder routines, based on 1st nibble value */
static int first_nibble_is_0(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) { // STOP
	if (IS_BSRF(code)) {
		/* Call 'far' subroutine Rn+PC+4 */
		op->type = RZ_ANALYSIS_OP_TYPE_UCALL;
		op->delay = 1;
		op->dst = analysis_regrel_jump(analysis, op, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "1,SETD,pc,2,+,pr,=,r%d,2,+,pc,+=", GET_TARGET_REG(code));
	} else if (IS_BRAF(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_UJMP;
		op->dst = analysis_regrel_jump(analysis, op, GET_TARGET_REG(code));
		op->eob = true;
		op->delay = 1;
		rz_strbuf_setf(&op->esil, "1,SETD,r%d,2,+,pc,+=", GET_TARGET_REG(code));
	} else if (IS_RTS(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_RET;
		op->delay = 1;
		op->eob = true;
		rz_strbuf_setf(&op->esil, "pr,pc,=");
	} else if (IS_RTE(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_RET;
		op->delay = 1;
		op->eob = true;
		// rz_strbuf_setf (&op->esil, "1,SETD,r15,[4],4,+,pc,=,r15,4,+,[4],0xFFF0FFF,&,sr,=,8,r15,+=");
		// not sure if should be added 4 to pc
		rz_strbuf_setf(&op->esil, "1,SETD,r15,[4],pc,=,r15,4,+,[4],0xFFF0FFF,&,sr,=,8,r15,+=");
	} else if (IS_MOVB_REG_TO_R0REL(code)) { // 0000nnnnmmmm0100 mov.b <REG_M>,@(R0,<REG_N>)
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_r0_reg_ref(analysis, GET_TARGET_REG(code), BYTE_SIZE);
		rz_strbuf_setf(&op->esil, "r%d,0xFF,&,r0,r%d,+,=[1]", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_MOVW_REG_TO_R0REL(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_r0_reg_ref(analysis, GET_TARGET_REG(code), WORD_SIZE);
		rz_strbuf_setf(&op->esil, "r%d,0xFFFF,&,r0,r%d,+,=[2]", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_MOVL_REG_TO_R0REL(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_r0_reg_ref(analysis, GET_TARGET_REG(code), LONG_SIZE);
		rz_strbuf_setf(&op->esil, "r%d,r0,r%d,+,=[4]", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_MOVB_R0REL_TO_REG(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->src[0] = analysis_fill_r0_reg_ref(analysis, GET_SOURCE_REG(code), BYTE_SIZE);
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r0,r%d,+,[1],r%d,=,0x000000FF,r%d,&=,r%d,0x80,&,?{,0xFFFFFF00,r%d,|=,}", GET_SOURCE_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
	} else if (IS_MOVW_R0REL_TO_REG(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->src[0] = analysis_fill_r0_reg_ref(analysis, GET_SOURCE_REG(code), WORD_SIZE);
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r0,r%d,+,[2],r%d,=,0x0000FFFF,r%d,&=,r%d,0x8000,&,?{,0xFFFF0000,r%d,|=,}", GET_SOURCE_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
	} else if (IS_MOVL_R0REL_TO_REG(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->src[0] = analysis_fill_r0_reg_ref(analysis, GET_SOURCE_REG(code), LONG_SIZE);
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r0,r%d,+,[4],r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_NOP(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_NOP;
		rz_strbuf_setf(&op->esil, " ");
	} else if (IS_CLRT(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_UNK;
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=");
	} else if (IS_SETT(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_UNK;
		rz_strbuf_setf(&op->esil, "0x1,sr,|=");
	} else if (IS_CLRMAC(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_UNK;
		rz_strbuf_setf(&op->esil, "0,mach,=,0,macl,=");
	} else if (IS_DIV0U(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_DIV;
		rz_strbuf_setf(&op->esil, "0xFFFFFCFE,sr,&=");
	} else if (IS_MOVT(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "0x1,sr,&,r%d,=", GET_TARGET_REG(code));
	} else if (IS_MULL(code)) { // multiply long
		op->type = RZ_ANALYSIS_OP_TYPE_MUL;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,r%d,*,macl,=", GET_TARGET_REG(code), GET_SOURCE_REG(code));
	} else if (IS_SLEEP(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_UNK;
		rz_strbuf_setf(&op->esil, "sleep_called,TRAP");
	} else if (IS_STSMACH(code)) { // 0000nnnn0000101_ sts MAC*,<REG_N>
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "mach,r%d,=", GET_TARGET_REG(code));
	} else if (IS_STSMACL(code)) { // 0000nnnn0000101_ sts MAC*,<REG_N>
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "macl,r%d,=", GET_TARGET_REG(code));
	} else if (IS_STSLMACL(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "macl,r%d,=", GET_TARGET_REG(code));
	} else if (IS_STCSR1(code)) { // 0000nnnn00010010 stc {sr,gbr,vbr,ssr},<REG_N>
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		// todo: plug in src
		switch (GET_SOURCE_REG(code)) {
		case 0:
			rz_strbuf_setf(&op->esil, "sr,r%d,=", GET_TARGET_REG(code));
			break;
		case 1:
			rz_strbuf_setf(&op->esil, "gbr,r%d,=", GET_TARGET_REG(code));
			break;
		case 2:
			rz_strbuf_setf(&op->esil, "vbr,r%d,=", GET_TARGET_REG(code));
			break;
		default:
			rz_strbuf_setf(&op->esil, "%s", "");
			break;
		}
	} else if (IS_STSPR(code)) { // 0000nnnn00101010 sts PR,<REG_N>
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "pr,r%d,=", GET_TARGET_REG(code));
	} else if (IS_MACL(code)) {
		rz_strbuf_setf(&op->esil,
			"mach,0x80000000,&,!," // mach_old sign (0)
			S32_EXT("r%d,[4]") "," //@Rn sign extended
			S32_EXT("r%d,[4]") "," //@Rm sign extended
					   "*," //(1)
					   "macl,32,mach,<<,|," // macl | (mach << 32)
					   "+," // MAC+@Rm*@Rn
					   "32," S32_EXT("r%d,[4]") "," //@Rn sign extended
			S32_EXT("r%d,[4]") "," //@Rm sign extended
					   "*,"
					   "4,r%d,+=," // Rn+=4
					   "4,r%d,+=," // Rm+=4
					   "0xffffffff00000000,&,>>,mach,=," // MACH > mach
					   "0xffffffff,&,macl,=,"
					   "0x2,sr,&,!,?{,BREAK,}," // if S==0 BREAK
					   "0x00007fff,mach,>,"
					   "0x80000000,mach,&,!,&,"
					   "?{,0x00007fff,mach,=,0xffffffff,macl,=,}," // if (mach>0x00007fff&&mach>0) mac=0x00007fffffffffff
					   "0xffff8000,mach,<,"
					   "0x80000000,mach,&,!,!,&,"
					   "?{,0xffff8000,mach,=,0x0,macl,=,}," // if (mach>0xffff8000&&mach<0) mac=0xffff800000000000
			,
			GET_TARGET_REG(code), GET_SOURCE_REG(code),
			GET_TARGET_REG(code), GET_SOURCE_REG(code),
			GET_TARGET_REG(code), GET_SOURCE_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_MUL;
	}
	return op->size;
}

// nibble=1; 0001nnnnmmmmi4*4 mov.l <REG_M>,@(<disp>,<REG_N>)
static int movl_reg_rdisp(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	op->type = RZ_ANALYSIS_OP_TYPE_STORE;
	op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
	op->dst = analysis_fill_reg_disp_mem(analysis, GET_TARGET_REG(code), code & 0x0F, LONG_SIZE);
	rz_strbuf_setf(&op->esil, "r%d,r%d,0x%x,+,=[4]", GET_SOURCE_REG(code), GET_TARGET_REG(code), (code & 0xF) << 2);
	return op->size;
}

static int first_nibble_is_2(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	if (IS_MOVB_REG_TO_REGREF(code)) { // 0010nnnnmmmm0000 mov.b <REG_M>,@<REG_N>
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_reg_ref(analysis, GET_TARGET_REG(code), BYTE_SIZE);
		rz_strbuf_setf(&op->esil, "r%d,r%d,=[1]", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_MOVW_REG_TO_REGREF(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_reg_ref(analysis, GET_TARGET_REG(code), WORD_SIZE);
		rz_strbuf_setf(&op->esil, "r%d,r%d,=[2]", GET_SOURCE_REG(code) & 0xFF, GET_TARGET_REG(code));
	} else if (IS_MOVL_REG_TO_REGREF(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_reg_ref(analysis, GET_TARGET_REG(code), LONG_SIZE);
		rz_strbuf_setf(&op->esil, "r%d,r%d,=[4]", GET_SOURCE_REG(code) & 0xFF, GET_TARGET_REG(code));
	} else if (IS_AND_REGS(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_AND;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,r%d,&=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_XOR_REGS(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_XOR;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,r%d,^=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_OR_REGS(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_OR;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,r%d,|=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_PUSHB(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_PUSH;
		rz_strbuf_setf(&op->esil, "1,r%d,-=,r%d,r%d,=[1]", GET_TARGET_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_PUSHW(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_PUSH;
		rz_strbuf_setf(&op->esil, "2,r%d,-=,r%d,r%d,=[2]", GET_TARGET_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_PUSHL(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_PUSH;
		rz_strbuf_setf(&op->esil, "4,r%d,-=,r%d,r%d,=[4]", GET_TARGET_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_TSTRR(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_ACMP;
		rz_strbuf_setf(&op->esil, "1,sr,|=,r%d,r%d,&,?{,0xFFFFFFFE,sr,&=,}", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_CMPSTR(code)) { // 0010nnnnmmmm1100 cmp/str <REG_M>,<REG_N>
		op->type = RZ_ANALYSIS_OP_TYPE_ACMP; // maybe not?
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,24,r%d,r%d,^,>>,0xFF,&,!,?{,1,sr,|=,},16,r%d,r%d,^,>>,0xFF,&,!,?{,1,sr,|=,},8,r%d,r%d,^,>>,0xFF,&,!,?{,1,sr,|=,},r%d,r%d,^,0xFF,&,!,?{,1,sr,|=,}", GET_SOURCE_REG(code), GET_TARGET_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_XTRCT(code)) { // 0010nnnnmmmm1101 xtrct <REG_M>,<REG_N>
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "16,r%d,0xFFFF,&,<<,16,r%d,0xFFFF0000,&,>>,|,r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
	} else if (IS_DIV0S(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_DIV;
		rz_strbuf_setf(&op->esil, "0xFFFFFCFE,sr,&=,r%d,0x80000000,&,?{,0x200,sr,|=,},r%d,0x80000000,&,?{,0x100,sr,|=,},sr,1,sr,<<,^,0x200,&,?{,1,sr,|=,}", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_MULUW(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_MUL;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,0xFFFF,&,r%d,0xFFFF,&,*,macl,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_MULSW(code)) { // 0010nnnnmmmm111_ mul{s,u}.w <REG_M>,<REG_N>
		op->type = RZ_ANALYSIS_OP_TYPE_MUL;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, S16_EXT("r%d") "," S16_EXT("r%d") ",*,macl,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	}

	return op->size;
}

static int first_nibble_is_3(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	// TODO Handle carry/overflow , CMP/xx?
	if (IS_ADD(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_ADD;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,r%d,+=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_ADDC(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_ADD;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "sr,0x1,&,0xFFFFFFFE,sr,&=,r%d,+=,31,$c,sr,|,sr,:=,r%d,r%d,+=,31,$c,sr,|,sr,:=", GET_TARGET_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_ADDV(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_ADD;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,r%d,+=,31,$o,sr,|=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_SUB(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_SUB;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,r%d,-=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_SUBC(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_SUB;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "sr,1,&," CLR_T ",r%d,-=,31,$b,sr,|,sr,:=,r%d,r%d,-=,31,$b,sr,|,sr,:=", GET_TARGET_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_SUBV(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_SUB;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, CLR_T ",r%d,r%d,-=,31,$o,sr,|,sr,:=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_CMPEQ(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_CMP;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&,r%d,r%d,^,!,|,sr,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_CMPGE(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_CMP;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,r%d,>=,?{,0x1,sr,|=,}", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_CMPGT(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_CMP;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,r%d,>,?{,0x1,sr,|=,}", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_CMPHI(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_CMP;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,0x100000000,+,r%d,0x100000000,+,>,?{,0x1,sr,|=,}", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_CMPHS(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_CMP;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,0x100000000,+,r%d,0x100000000,+,>=,?{,0x1,sr,|=,}", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_DIV1(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_DIV;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		rz_strbuf_setf(&op->esil,
			"1,sr,>>,sr,^,0x80,&," // old_Q^M
			"0xFFFFFF7F,sr,&=,"
			"1,r%d,DUP,0x80000000,&,?{,0x80,sr,|=,},<<,sr,0x1,&,|,r%d,=," // shift Q<-Rn<-T
			"DUP,!,!,?{,"
			"r%d,NUM," // Rn_old (before subtract)
			"r%d,r%d,+=,"
			"r%d,<,}{," // tmp0
			"r%d,NUM," // Rn_old (before subtract)
			"r%d,r%d,-=,"
			"r%d,>,}," // tmp0
			"sr,0x80,&,!,!,^," // Q^tmp0
			"sr,0x100,&,?{,!,}," // if (M) !(Q^tmp0)
			"0xFFFFFF7F,sr,&=," // Q==0
			"?{,0x80,sr,|=,}," // Q=!(Q^tmp0)or(Q^tmp0)
			CLR_T ","
			"1,sr,>>,sr,^,0x80,&,!,sr,|=", // sr=!Q^M
			GET_TARGET_REG(code), GET_TARGET_REG(code),
			GET_TARGET_REG(code),
			GET_SOURCE_REG(code), GET_TARGET_REG(code),
			GET_TARGET_REG(code),
			GET_TARGET_REG(code),
			GET_SOURCE_REG(code), GET_TARGET_REG(code),
			GET_TARGET_REG(code));
	} else if (IS_DMULU(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_MUL;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "32,r%d,r%d,*,DUP,0xFFFFFFFF,&,macl,=,>>,mach,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_DMULS(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_MUL;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->src[1] = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "32,r%d,r%d,0x80000000,&,?{,0xFFFFFFFF00000000,+,},r%d,r%d,0x80000000,&,?{,0xFFFFFFFF00000000,+,},*,DUP,0xFFFFFFFF,&,macl,=,>>,mach,=", GET_SOURCE_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
	}
	return op->size;
}

static int first_nibble_is_4(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	switch (code & 0xF0FF) { // TODO: change to common } else if construction
	case 0x4020: // shal
		op->type = RZ_ANALYSIS_OP_TYPE_SAL;
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,0x80000000,&,?{,0x1,sr,|=,},1,r%d,<<=", GET_TARGET_REG(code), GET_TARGET_REG(code));
		break;
	case 0x4021: // shar
		op->type = RZ_ANALYSIS_OP_TYPE_SAR;
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,0x1,&,?{,0x1,sr,|=,},0,r%d,0x80000000,&,?{,0x80000000,+,},1,r%d,>>=,r%d,|=", GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
		break;
	case 0x4000: // shll
		op->type = RZ_ANALYSIS_OP_TYPE_SHL;
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,0x80000000,&,?{,0x1,sr,|=,},1,r%d,<<=", GET_TARGET_REG(code), GET_TARGET_REG(code));
		break;
	case 0x4008: // shll2
		op->type = RZ_ANALYSIS_OP_TYPE_SHL;
		rz_strbuf_setf(&op->esil, "2,r%d,<<=", GET_TARGET_REG(code));
		break;
	case 0x4018: // shll8
		op->type = RZ_ANALYSIS_OP_TYPE_SHL;
		rz_strbuf_setf(&op->esil, "8,r%d,<<=", GET_TARGET_REG(code));
		break;
	case 0x4028: // shll16
		op->type = RZ_ANALYSIS_OP_TYPE_SHL;
		rz_strbuf_setf(&op->esil, "16,r%d,<<=", GET_TARGET_REG(code));
		break;
	case 0x4001: // shlr
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,0x1,&,?{,0x1,sr,|=,},1,r%d,>>=", GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_SHR;
		break;
	case 0x4009: // shlr2
		rz_strbuf_setf(&op->esil, "2,r%d,>>=", GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_SHR;
		break;
	case 0x4019: // shlr8
		rz_strbuf_setf(&op->esil, "8,r%d,>>=", GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_SHR;
		break;
	case 0x4029: // shlr16
		rz_strbuf_setf(&op->esil, "16,r%d,>>=", GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_SHR;
		break;
	default:
		break;
	}

	if (IS_JSR(code)) {
		// op->type = RZ_ANALYSIS_OP_TYPE_UCALL; //call to reg
		op->type = RZ_ANALYSIS_OP_TYPE_RCALL; // call to reg
		op->delay = 1;
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "1,SETD,pc,2,+,pr,=,r%d,pc,=", GET_TARGET_REG(code));
	} else if (IS_JMP(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_UJMP; // jmp to reg
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		op->delay = 1;
		op->eob = true;
		rz_strbuf_setf(&op->esil, "1,SETD,r%d,pc,=", GET_TARGET_REG(code));
	} else if (IS_CMPPL(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_CMP;
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,0,r%d,>,?{,0x1,sr,|=,}", GET_TARGET_REG(code));
	} else if (IS_CMPPZ(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_CMP;
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,0,r%d,>=,?{,0x1,sr,|=,}", GET_TARGET_REG(code));
	} else if (IS_LDCLSR(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_POP;
		rz_strbuf_setf(&op->esil, "r%d,[4],0x0FFF0FFF,&,sr,=,4,r%d,+=", GET_TARGET_REG(code), GET_TARGET_REG(code));
	} else if (IS_LDCLSRGBR(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_POP;
		rz_strbuf_setf(&op->esil, "r%d,[4],gbr,=,4,r%d,+=", GET_TARGET_REG(code), GET_TARGET_REG(code));
	} else if (IS_LDCLSRVBR(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_POP;
		rz_strbuf_setf(&op->esil, "r%d,[4],vbr,=,4,r%d,+=", GET_TARGET_REG(code), GET_TARGET_REG(code));
		// todo ssr?
	} else if (IS_LDSLMACH(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_POP;
		rz_strbuf_setf(&op->esil, "r%d,[4],mach,=,4,r%d,+=", GET_TARGET_REG(code), GET_TARGET_REG(code));
	} else if (IS_LDSLMACL(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_POP;
		rz_strbuf_setf(&op->esil, "r%d,[4],macl,=,4,r%d,+=", GET_TARGET_REG(code), GET_TARGET_REG(code));
	} else if (IS_LDSLPR(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_POP;
		rz_strbuf_setf(&op->esil, "r%d,[4],pr,=,4,r%d,+=", GET_TARGET_REG(code), GET_TARGET_REG(code));
	} else if (IS_LDCSR(code)) {
		rz_strbuf_setf(&op->esil, "r%d,0x0FFF0FFF,&,sr,=", GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
	} else if (IS_LDCGBR(code)) {
		rz_strbuf_setf(&op->esil, "r%d,gbr,=", GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
	} else if (IS_LDCVBR(code)) {
		rz_strbuf_setf(&op->esil, "r%d,vbr,=", GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
	} else if (IS_LDSMACH(code)) {
		rz_strbuf_setf(&op->esil, "r%d,mach,=", GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
	} else if (IS_LDSMACL(code)) {
		rz_strbuf_setf(&op->esil, "r%d,macl,=", GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
	} else if (IS_LDSPR(code)) {
		rz_strbuf_setf(&op->esil, "r%d,pr,=", GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
	} else if (IS_ROTR(code)) {
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,0x1,&,sr,|=,0x1,r%d,>>>,r%d,=", GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = (code & 1) ? RZ_ANALYSIS_OP_TYPE_ROR : RZ_ANALYSIS_OP_TYPE_ROL;
	} else if (IS_ROTL(code)) {
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,0x1,r%d,<<<,r%d,=,r%d,0x1,&,sr,|=", GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = (code & 1) ? RZ_ANALYSIS_OP_TYPE_ROR : RZ_ANALYSIS_OP_TYPE_ROL;
	} else if (IS_ROTCR(code)) {
		rz_strbuf_setf(&op->esil, "0,sr,0x1,&,?{,0x80000000,},0xFFFFFFFE,sr,&=,r%d,1,&,sr,|=,1,r%d,>>=,r%d,|=", GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = (code & 1) ? RZ_ANALYSIS_OP_TYPE_ROR : RZ_ANALYSIS_OP_TYPE_ROL;
	} else if (IS_ROTCL(code)) {
		rz_strbuf_setf(&op->esil, "sr,0x1,&,0xFFFFFFFE,sr,&=,r%d,0x80000000,&,?{,1,sr,|=,},1,r%d,<<=,r%d,|=", GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = (code & 1) ? RZ_ANALYSIS_OP_TYPE_ROR : RZ_ANALYSIS_OP_TYPE_ROL;
	} else if (IS_STCLSR(code)) {
		rz_strbuf_setf(&op->esil, "4,r%d,-=,sr,r%d,=[4]", GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_PUSH;
	} else if (IS_STCLGBR(code)) {
		rz_strbuf_setf(&op->esil, "4,r%d,-=,gbr,r%d,=[4]", GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_PUSH;
	} else if (IS_STCLVBR(code)) {
		rz_strbuf_setf(&op->esil, "4,r%d,-=,vbr,r%d,=[4]", GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_PUSH;
	} else if (IS_STSLMACL(code)) {
		rz_strbuf_setf(&op->esil, "4,r%d,-=,macl,r%d,=[4]", GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_PUSH;
	} else if (IS_STSLMACH(code)) {
		rz_strbuf_setf(&op->esil, "4,r%d,-=,mach,r%d,=[4]", GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_PUSH;
	} else if (IS_STSLPR(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_PUSH;
		rz_strbuf_setf(&op->esil, "4,r%d,-=,pr,r%d,=[4]", GET_TARGET_REG(code), GET_TARGET_REG(code));
	} else if (IS_TASB(code)) {
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r%d,[1],!,?{,0x80,r%d,=[1],1,sr,|=,}", GET_TARGET_REG(code), GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_UNK;
	} else if (IS_DT(code)) {
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,1,r%d,-=,$z,sr,|,sr,:=", GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_UNK;
	} else if (IS_MACW(code)) {
		rz_strbuf_setf(&op->esil,
			"0x2,sr,&,!,?{," // if S==0
			S16_EXT("r%d,[2]") "," //@Rn sign extended
			S16_EXT("r%d,[2]") "," //@Rm sign extended
					   "*,"
					   "macl,32,mach,<<,|," // macl | (mach << 32)
					   "+," // MAC+@Rm*@Rn
					   "32," S16_EXT("r%d,[2]") "," //@Rn sign extended
			S16_EXT("r%d,[2]") "," //@Rm sign extended
					   "*,"
					   "0xffffffff00000000,&,>>,mach,=," // MACH > mach
					   "0xffffffff,&,macl,=,"
					   "}{," // if S==1
			S16_EXT("r%d,[2]") "," //@Rn sign extended
			S16_EXT("r%d,[2]") "," //@Rm sign extended
					   "*"
					   "macl,+=," // macl+(@Rm+@Rm)
					   "31,$o,?{," // if overflow
					   "macl,0x80000000,&,?{,"
					   "0x7fffffff,macl,=,"
					   "}{,"
					   "0x80000000,macl,=,"
					   "},"
					   "},"
					   "},"
					   "2,r%d,+=," // Rn+=2
					   "2,r%d,+=,", // Rm+=2
			GET_TARGET_REG(code), GET_SOURCE_REG(code),
			GET_TARGET_REG(code), GET_SOURCE_REG(code),
			GET_TARGET_REG(code), GET_SOURCE_REG(code),
			GET_TARGET_REG(code), GET_SOURCE_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_MUL;
	}
	return op->size;
}

// nibble=5; 0101nnnnmmmmi4*4 mov.l @(<disp>,<REG_M>),<REG_N>
static int movl_rdisp_reg(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
	op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
	op->src[0] = analysis_fill_reg_disp_mem(analysis, GET_SOURCE_REG(code), code & 0x0F, LONG_SIZE);
	rz_strbuf_setf(&op->esil, "r%d,0x%x,+,[4],r%d,=", GET_SOURCE_REG(code), (code & 0xF) * 4, GET_TARGET_REG(code));
	return op->size;
}

static int first_nibble_is_6(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	if (IS_MOV_REGS(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_MOVB_REGREF_TO_REG(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->src[0] = analysis_fill_reg_ref(analysis, GET_SOURCE_REG(code), BYTE_SIZE);
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "0x000000FF,r%d,&=,r%d,[1],DUP,0x80,&,?{,0xFFFFFF00,|=,},r%d,=", GET_TARGET_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_MOVW_REGREF_TO_REG(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->src[0] = analysis_fill_reg_ref(analysis, GET_SOURCE_REG(code), WORD_SIZE);
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "0x0000FFFF,r%d,&=,r%d,[2],DUP,0x8000,&,?{,0xFFFF0000,|=,},r%d,=", GET_TARGET_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_MOVL_REGREF_TO_REG(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->src[0] = analysis_fill_reg_ref(analysis, GET_SOURCE_REG(code), LONG_SIZE);
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,[4],r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
	} else if (IS_EXT(code)) {
		// ext{s,u}.{b,w} instructs. todo : more detail ?
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		switch (code & 0xF) {
		case 0xC: // EXTU.B
			rz_strbuf_setf(&op->esil, "r%d,0xFF,&,r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
			break;
		case 0xD: // EXTU.W
			rz_strbuf_setf(&op->esil, "r%d,0xFFFF,&,r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
			break;
		case 0xE: // EXTS.B
			rz_strbuf_setf(&op->esil, "r%d,0xFF,&,DUP,0x80,&,?{,0xFFFFFF00,|,},r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
			break;
		case 0xF: // EXTS.W
			rz_strbuf_setf(&op->esil, "r%d,0xFFFF,&,DUP,0x8000,&,?{,0xFFFF0000,|,},r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
			break;
		default:
			rz_strbuf_setf(&op->esil, "TODO,NOT IMPLEMENTED");
			break;
		}
	} else if (IS_MOVB_POP(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_POP;
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,[1],DUP,0x80,&,?{,0xFFFFFF00,|,},r%d,=,1,r%d,+=", GET_SOURCE_REG(code), GET_TARGET_REG(code), GET_SOURCE_REG(code));
	} else if (IS_MOVW_POP(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_POP;
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,[2],DUP,0x8000,&,?{,0xFFFF0000,|,},r%d,=,2,r%d,+=", GET_SOURCE_REG(code), GET_TARGET_REG(code), GET_SOURCE_REG(code));
	} else if (IS_MOVL_POP(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_POP;
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
		rz_strbuf_setf(&op->esil, "r%d,[4],r%d,=,4,r%d,+=", GET_SOURCE_REG(code), GET_TARGET_REG(code), GET_SOURCE_REG(code));
	} else if (IS_NEG(code)) {
		// todo: neg and negc details
		op->type = RZ_ANALYSIS_OP_TYPE_UNK;
		rz_strbuf_setf(&op->esil, "r%d,0,-,r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
	} else if (IS_NEGC(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_UNK;
		rz_strbuf_setf(&op->esil, "1,sr,&,0xFFFFFFFE,sr,&=,r%d,+,0,-,31,$b,sr,|,sr,=,r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
	} else if (IS_NOT(code)) {
		// todo : details?
		rz_strbuf_setf(&op->esil, "0xFFFFFFFF,r%d,^,r%d,=", GET_SOURCE_REG(code), GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_NOT;
		op->src[0] = analysis_fill_ai_rg(analysis, GET_SOURCE_REG(code));
		op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
	} else if (IS_SWAPB(code)) {
		rz_strbuf_setf(&op->esil, "r%d,0xFFFF0000,&,8,r%d,0xFF,&,<<,|,8,r%d,0xFF00,&,>>,|,r%d,=", GET_SOURCE_REG(code), GET_SOURCE_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
		// todo : details
	} else if (IS_SWAPW(code)) {
		rz_strbuf_setf(&op->esil, "16,r%d,0xFFFF,&,<<,16,r%d,0xFFFF0000,&,>>,|,r%d,=", GET_SOURCE_REG(code), GET_SOURCE_REG(code), GET_TARGET_REG(code));
		op->type = RZ_ANALYSIS_OP_TYPE_MOV;
	}
	return op->size;
}

// nibble=7; 0111nnnni8*1.... add #<imm>,<REG_N>
static int add_imm(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	op->type = RZ_ANALYSIS_OP_TYPE_ADD;
	op->src[0] = analysis_fill_im(analysis, (st8)(code & 0xFF)); // Casting to (st8) forces sign-extension.
	op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
	rz_strbuf_setf(&op->esil, "0x%x,DUP,0x80,&,?{,0xFFFFFF00,|,},r%d,+=", code & 0xFF, GET_TARGET_REG(code));
	return op->size;
}

static int first_nibble_is_8(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	if (IS_BT_OR_BF(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_CJMP; // Jump if true or jump if false insns
		op->jump = disarm_8bit_offset(op->addr, GET_BTF_OFFSET(code));
		op->fail = op->addr + 2;
		op->eob = true;
		if (IS_BT(code)) {
			rz_strbuf_setf(&op->esil, "sr,1,&,?{,0x%" PFMT64x ",pc,=,}", op->jump);
		} else if (IS_BTS(code)) {
			rz_strbuf_setf(&op->esil, "1,SETD,sr,1,&,?{,0x%" PFMT64x ",pc,=,}", op->jump);
			op->delay = 1; // Only /S versions have a delay slot
		} else if (IS_BFS(code)) {
			rz_strbuf_setf(&op->esil, "1,SETD,sr,1,&,!,?{,0x%" PFMT64x ",pc,=,}", op->jump);
			op->delay = 1; // Only /S versions have a delay slot
		} else if (IS_BF(code)) {
			rz_strbuf_setf(&op->esil, "sr,1,&,!,?{,0x%" PFMT64x ",pc,=,}", op->jump);
		}
	} else if (IS_MOVB_REGDISP_R0(code)) {
		// 10000100mmmmi4*1 mov.b @(<disp>,<REG_M>),R0
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->dst = analysis_fill_ai_rg(analysis, 0);
		op->src[0] = analysis_fill_reg_disp_mem(analysis, GET_SOURCE_REG(code), code & 0x0F, BYTE_SIZE);
		rz_strbuf_setf(&op->esil, "r%d,0x%x,+,[1],DUP,0x80,&,?{,0xFFFFFF00,|,},r0,=", GET_SOURCE_REG(code), code & 0xF);
	} else if (IS_MOVW_REGDISP_R0(code)) {
		// 10000101mmmmi4*2 mov.w @(<disp>,<REG_M>),R0
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->dst = analysis_fill_ai_rg(analysis, 0);
		op->src[0] = analysis_fill_reg_disp_mem(analysis, GET_SOURCE_REG(code), code & 0x0F, WORD_SIZE);
		rz_strbuf_setf(&op->esil, "r%d,0x%x,+,[2],DUP,0x8000,&,?{,0xFFFF0000,|,},r0,=", GET_SOURCE_REG(code), (code & 0xF) * 2);
	} else if (IS_CMPIMM(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_CMP;
		rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,0x%x,DUP,0x80,&,?{,0xFFFFFF00,|,},r0,==,$z,sr,|,sr,:=", code & 0xFF);
	} else if (IS_MOVB_R0_REGDISP(code)) {
		/* 10000000mmmmi4*1 mov.b R0,@(<disp>,<REG_M>)*/
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, 0);
		op->dst = analysis_fill_reg_disp_mem(analysis, GET_SOURCE_REG(code), code & 0x0F, BYTE_SIZE);
		rz_strbuf_setf(&op->esil, "r0,0xFF,&,0x%x,r%d,+,=[1]", code & 0xF, GET_SOURCE_REG(code));
	} else if (IS_MOVW_R0_REGDISP(code)) {
		// 10000001mmmmi4*2 mov.w R0,@(<disp>,<REG_M>))
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, 0);
		op->dst = analysis_fill_reg_disp_mem(analysis, GET_SOURCE_REG(code), code & 0x0F, WORD_SIZE);
		rz_strbuf_setf(&op->esil, "r0,0xFFFF,&,0x%x,r%d,+,=[2]", (code & 0xF) * 2, GET_SOURCE_REG(code));
	}
	return op->size;
}

// nibble=9; 1001nnnni8p2.... mov.w @(<disp>,PC),<REG_N>
static int movw_pcdisp_reg(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
	op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
	op->src[0] = rz_analysis_value_new();
	op->src[0]->base = (code & 0xFF) * 2 + op->addr + 4;
	op->src[0]->memref = 1;
	rz_strbuf_setf(&op->esil, "0x%" PFMT64x ",[2],r%d,=,r%d,0x8000,&,?{,0xFFFF0000,r%d,|=,}", op->src[0]->base, GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
	return op->size;
}

// nibble=A; 1010i12......... bra <bdisp12>
static int bra(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	/* Unconditional branch, relative to PC */
	op->type = RZ_ANALYSIS_OP_TYPE_JMP;
	op->delay = 1;
	op->jump = disarm_12bit_offset(op, GET_BRA_OFFSET(code));
	op->eob = true;
	rz_strbuf_setf(&op->esil, "1,SETD,0x%" PFMT64x ",pc,=", op->jump);
	return op->size;
}

// nibble=B; 1011i12......... bsr <bdisp12>
static int bsr(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	/* Subroutine call, relative to PC */
	op->type = RZ_ANALYSIS_OP_TYPE_CALL;
	op->jump = disarm_12bit_offset(op, GET_BRA_OFFSET(code));
	op->delay = 1;
	rz_strbuf_setf(&op->esil, "1,SETD,pc,2,+,pr,=,0x%" PFMT64x ",pc,=", op->jump);
	return op->size;
}

static int first_nibble_is_c(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	if (IS_TRAP(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_SWI;
		op->val = (ut8)(code & 0xFF);
		rz_strbuf_setf(&op->esil, "4,r15,-=,sr,r15,=[4],4,r15,-=,2,pc,-,r15,=[4],2,0x%x,<<,4,+,vbr,+,pc,=", code & 0xFF);
	} else if (IS_MOVA_PCREL_R0(code)) {
		// 11000111i8p4.... mova @(<disp>,PC),R0
		op->type = RZ_ANALYSIS_OP_TYPE_LEA;
		op->src[0] = analysis_pcrel_disp_mov(analysis, op, code & 0xFF, LONG_SIZE); // this is wrong !
		op->dst = analysis_fill_ai_rg(analysis, 0); // Always R0
		rz_strbuf_setf(&op->esil, "0x%x,pc,+,r0,=", (code & 0xFF) * 4);
	} else if (IS_BINLOGIC_IMM_R0(code)) { // 110010__i8 (binop) #imm, R0
		op->src[0] = analysis_fill_im(analysis, code & 0xFF);
		op->src[1] = analysis_fill_ai_rg(analysis, 0); // Always R0
		op->dst = analysis_fill_ai_rg(analysis, 0); // Always R0 except tst #imm, R0
		switch (code & 0xFF00) {
		case 0xC800: // tst
			// TODO : get correct op->dst ! (T flag)
			op->type = RZ_ANALYSIS_OP_TYPE_ACMP;
			rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r0,0x%x,&,!,?{,1,sr,|=,}", code & 0xFF);
			break;
		case 0xC900: // and
			op->type = RZ_ANALYSIS_OP_TYPE_AND;
			rz_strbuf_setf(&op->esil, "0x%x,r0,&=", code & 0xFF);
			break;
		case 0xCA00: // xor
			op->type = RZ_ANALYSIS_OP_TYPE_XOR;
			rz_strbuf_setf(&op->esil, "0x%x,r0,^=", code & 0xFF);
			break;
		case 0xCB00: // or
			op->type = RZ_ANALYSIS_OP_TYPE_OR;
			rz_strbuf_setf(&op->esil, "0x%x,r0,|=", code & 0xFF);
			break;
		}
	} else if (IS_BINLOGIC_IMM_GBR(code)) { // 110011__i8 (binop).b #imm, @(R0,gbr)
		op->src[0] = analysis_fill_im(analysis, code & 0xFF);
		switch (code & 0xFF00) {
		case 0xCC00: // tst
			// TODO : get correct op->dst ! (T flag)
			op->type = RZ_ANALYSIS_OP_TYPE_ACMP;
			rz_strbuf_setf(&op->esil, "0xFFFFFFFE,sr,&=,r0,gbr,+,[1],0x%x,&,!,?{,1,sr,|=,}", code & 0xFF);
			break;
		case 0xCD00: // and
			op->type = RZ_ANALYSIS_OP_TYPE_AND;
			rz_strbuf_setf(&op->esil, "r0,gbr,+,[1],0x%x,&,r0,gbr,+,=[1]", code & 0xFF);
			break;
		case 0xCE00: // xor
			op->type = RZ_ANALYSIS_OP_TYPE_XOR;
			rz_strbuf_setf(&op->esil, "r0,gbr,+,[1],0x%x,^,r0,gbr,+,=[1]", code & 0xFF);
			break;
		case 0xCF00: // or
			op->type = RZ_ANALYSIS_OP_TYPE_OR;
			rz_strbuf_setf(&op->esil, "r0,gbr,+,[1],0x%x,|,r0,gbr,+,=[1]", code & 0xFF);
			break;
		}
		// TODO : implement @(R0,gbr) dest and src[1]
	} else if (IS_MOVB_R0_GBRREF(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, 0);
		rz_strbuf_setf(&op->esil, "r0,gbr,0x%x,+,=[1]", code & 0xFF);
	} else if (IS_MOVW_R0_GBRREF(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, 0);
		rz_strbuf_setf(&op->esil, "r0,gbr,0x%x,+,=[2]", (code & 0xFF) * 2);
	} else if (IS_MOVL_R0_GBRREF(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_STORE;
		op->src[0] = analysis_fill_ai_rg(analysis, 0);
		rz_strbuf_setf(&op->esil, "r0,gbr,0x%x,+,=[4]", (code & 0xFF) * 4);
	} else if (IS_MOVB_GBRREF_R0(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->dst = analysis_fill_ai_rg(analysis, 0);
		rz_strbuf_setf(&op->esil, "gbr,0x%x,+,[1],DUP,0x80,&,?{,0xFFFFFF00,|,},r0,=", (code & 0xFF));
	} else if (IS_MOVW_GBRREF_R0(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->dst = analysis_fill_ai_rg(analysis, 0);
		rz_strbuf_setf(&op->esil, "gbr,0x%x,+,[2],DUP,0x8000,&,?{,0xFFFF0000,|,},r0,=", (code & 0xFF) * 2);
	} else if (IS_MOVL_GBRREF_R0(code)) {
		op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
		op->dst = analysis_fill_ai_rg(analysis, 0);
		rz_strbuf_setf(&op->esil, "gbr,0x%x,+,[4],r0,=", (code & 0xFF) * 4);
	}

	return op->size;
}

// nibble=d; 1101nnnni8 : mov.l @(<disp>,PC), Rn
static int movl_pcdisp_reg(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	op->type = RZ_ANALYSIS_OP_TYPE_LOAD;
	op->src[0] = analysis_pcrel_disp_mov(analysis, op, code & 0xFF, LONG_SIZE);
	// TODO: check it
	op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
	// rz_strbuf_setf (&op->esil, "0x%x,[4],r%d,=", (code & 0xFF) * 4 + (op->addr & 0xfffffff3) + 4, GET_TARGET_REG (code));
	rz_strbuf_setf(&op->esil, "0x%" PFMT64x ",[4],r%d,=", (code & 0xFF) * 4 + ((op->addr >> 2) << 2) + 4, GET_TARGET_REG(code));
	return op->size;
}

// nibble=e; 1110nnnni8*1.... mov #<imm>,<REG_N>
static int mov_imm_reg(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	op->type = RZ_ANALYSIS_OP_TYPE_MOV;
	op->dst = analysis_fill_ai_rg(analysis, GET_TARGET_REG(code));
	op->src[0] = analysis_fill_im(analysis, (st8)(code & 0xFF));
	rz_strbuf_setf(&op->esil, "0x%x,r%d,=,r%d,0x80,&,?{,0xFFFFFF00,r%d,|=,}", code & 0xFF, GET_TARGET_REG(code), GET_TARGET_REG(code), GET_TARGET_REG(code));
	return op->size;
}

// nibble=f;
static int fpu_insn(RzAnalysis *analysis, RzAnalysisOp *op, ut16 code) {
	// Not interested on FPU stuff for now
	op->family = RZ_ANALYSIS_OP_FAMILY_FPU;
	return op->size;
}

/* Table of routines for further analysis based on 1st nibble */
static int (*first_nibble_decode[])(RzAnalysis *, RzAnalysisOp *, ut16) = {
	first_nibble_is_0,
	movl_reg_rdisp,
	first_nibble_is_2,
	first_nibble_is_3,
	first_nibble_is_4,
	movl_rdisp_reg,
	first_nibble_is_6,
	add_imm,
	first_nibble_is_8,
	movw_pcdisp_reg,
	bra,
	bsr,
	first_nibble_is_c,
	movl_pcdisp_reg,
	mov_imm_reg,
	fpu_insn
};

/* This is the basic operation analysis. Just initialize and jump to
 * routines defined in first_nibble_decode table
 */
static int sh_op(RzAnalysis *analysis, RzAnalysisOp *op, ut64 addr, const ut8 *data, int len, RzAnalysisOpMask mask) {
	ut8 op_MSB, op_LSB;
	int ret;
	if (!data || len < 2) {
		return 0;
	}
	op->addr = addr;
	op->type = RZ_ANALYSIS_OP_TYPE_UNK;

	op->size = 2;

	op_MSB = analysis->big_endian ? data[0] : data[1];
	op_LSB = analysis->big_endian ? data[1] : data[0];
	ut16 opcode = (ut16)op_MSB << 8 | op_LSB;
	ret = first_nibble_decode[(op_MSB >> 4) & 0x0F](analysis, op, opcode);

	// RzIL uplifting
	SHOp *ilop = sh_disassembler(opcode);
	SHILContext *ctx = RZ_NEW0(SHILContext);
	ctx->use_banked = true;

	rz_sh_il_opcode(analysis, op, addr, ilop, ctx);
	RZ_FREE(ctx);
	RZ_FREE(ilop);

	return ret;
}

/* Set the profile register */
static RZ_OWN char *sh_get_reg_profile(RzAnalysis *analysis) {
	// TODO Add fpu regs
	const char *p =
		"=PC	pc\n"
		"=SN	r0\n"
		"=SP	r15\n"
		"=BP	r14\n"
		"=A0	r4\n"
		"=A1	r5\n"
		"=A2	r6\n"
		"=A3	r7\n"
		"=R0	r0\n"
		"gpr	r0		.32	0		0\n"
		"gpr	r1		.32	4		0\n"
		"gpr	r2		.32	8		0\n"
		"gpr	r3		.32	12		0\n"
		"gpr	r4		.32	16		0\n"
		"gpr	r5		.32	20		0\n"
		"gpr	r6		.32	24		0\n"
		"gpr	r7		.32	28		0\n"
		"gpr	r0b		.32	32		0\n"
		"gpr	r1b		.32	36		0\n"
		"gpr	r2b		.32	40		0\n"
		"gpr	r3b		.32	44		0\n"
		"gpr	r4b		.32	48		0\n"
		"gpr	r5b		.32	52		0\n"
		"gpr	r6b		.32	56		0\n"
		"gpr	r7b		.32	60		0\n"
		"gpr	r8		.32	64		0\n"
		"gpr	r9		.32	68		0\n"
		"gpr	r10		.32	72		0\n"
		"gpr	r11		.32	76		0\n"
		"gpr	r12		.32	80		0\n"
		"gpr	r13		.32	84		0\n"
		"gpr	r14		.32	88		0\n"
		"gpr	r15		.32	92		0\n"
		"gpr	pc		.32	96		0\n"
		"gpr	sr		.32	100		0\n"
		"gpr	sr_t	.1	100.0	0\n"
		"gpr	sr_s	.1	100.1	0\n"
		"gpr	sr_i	.4	100.4	0\n"
		"gpr	sr_q	.1	101.0	0\n"
		"gpr	sr_m	.1	101.1	0\n"
		"gpr	sr_f	.1	101.7	0\n"
		"gpr	sr_b	.1	103.4	0\n"
		"gpr	sr_r	.1	103.5	0\n"
		"gpr	sr_d	.1	103.6	0\n"
		"gpr	gbr		.32	104		0\n"
		"gpr	ssr		.32	108		0\n"
		"gpr	spc		.32	112		0\n"
		"gpr	sgr		.32	116		0\n"
		"gpr	dbr		.32	120		0\n"
		"gpr	vbr		.32	124		0\n"
		"gpr	mach	.32	128		0\n"
		"gpr	macl	.32	132		0\n"
		"gpr	pr		.32	136		0\n";

	return strdup(p);
}

static int archinfo(RzAnalysis *a, RzAnalysisInfoType query) {
	switch (query) {
	case RZ_ANALYSIS_ARCHINFO_MIN_OP_SIZE:
		/* fall-thru */
	case RZ_ANALYSIS_ARCHINFO_MAX_OP_SIZE:
		/* fall-thru */
	case RZ_ANALYSIS_ARCHINFO_TEXT_ALIGN:
		/* fall-thru */
	case RZ_ANALYSIS_ARCHINFO_DATA_ALIGN:
		return 2;
	case RZ_ANALYSIS_ARCHINFO_CAN_USE_POINTERS:
		return true;
	default:
		return -1;
	}
}

RzAnalysisPlugin rz_analysis_plugin_sh = {
	.name = "sh",
	.desc = "SH-4 code analysis plugin",
	.license = "LGPL3",
	.arch = "sh",
	.archinfo = archinfo,
	.bits = 32,
	.op = &sh_op,
	.get_reg_profile = &sh_get_reg_profile,
	.esil = true,
	.il_config = rz_sh_il_config

};

#ifndef RZ_PLUGIN_INCORE
RZ_API RzLibStruct rizin_plugin = {
	.type = RZ_LIB_TYPE_ANALYSIS,
	.data = &rz_analysis_plugin_sh,
	.version = RZ_VERSION
};
#endif
