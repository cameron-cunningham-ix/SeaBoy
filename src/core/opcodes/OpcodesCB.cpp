// OpcodesCB.cpp - CB-prefix opcode handlers for the Sharp SM83 CPU
//
// All CB-prefix instructions operate on an r8 operand encoded in bits [2:0].
// The operation is encoded in bits [7:3]:
//   0x00-0x07  RLC r8
//   0x08-0x0F  RRC r8
//   0x10-0x17  RL  r8
//   0x18-0x1F  RR  r8
//   0x20-0x27  SLA r8
//   0x28-0x2F  SRA r8
//   0x30-0x37  SWAP r8
//   0x38-0x3F  SRL r8
//   0x40-0x7F  BIT b, r8  (bit b = bits[5:3])
//   0x80-0xBF  RES b, r8
//   0xC0-0xFF  SET b, r8
//
// All handlers return total T-cycles INCLUDING the 0xCB prefix fetch (4T).
// Register operands cost 8T total; (HL) indirect operands cost 16T total
// (except BIT (HL) = 12T; RES/SET (HL) = 16T).
//
// Reference: https://rgbds.gbdev.io/docs/v1.0.1/gbz80.7
// PanDocs.14

#include "src/core/CPU.hpp"
#include "src/core/MMU.hpp"
#include "src/core/Registers.hpp"

namespace SeaBoy::OpcodesCB
{

// ---------------------------------------------------------------------------
// Helpers: read/write r8 operand (including (HL) indirect)
// ---------------------------------------------------------------------------

static uint8_t readR8(CPU& cpu, uint8_t idx) {
    if (idx == 6)
        return cpu.mmu().read8(cpu.regs().getHL());
    return *cpu.regs().r8Ptr(idx);
}

static void writeR8(CPU& cpu, uint8_t idx, uint8_t val) {
    if (idx == 6)
        cpu.mmu().write8(cpu.regs().getHL(), val);
    else *cpu.regs().r8Ptr(idx) = val;
}

// Is the operand the (HL) indirect form?
static bool isHL(uint8_t idx) { return idx == 6; }

// ---------------------------------------------------------------------------
// Rotate / shift operations
// ---------------------------------------------------------------------------

// RLC - rotate left; old bit 7 -> carry and bit 0
static uint8_t do_rlc(CPU& cpu, uint8_t val) {
    uint8_t cy  = (val >> 7) & 1;
    uint8_t out = static_cast<uint8_t>((val << 1) | cy);
    cpu.regs().setFlagZ(out == 0);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(false);
    cpu.regs().setFlagC(cy != 0);
    return out;
}

// RRC - rotate right; old bit 0 -> carry and bit 7
static uint8_t do_rrc(CPU& cpu, uint8_t val) {
    uint8_t cy  = val & 1;
    uint8_t out = static_cast<uint8_t>((val >> 1) | (cy << 7));
    cpu.regs().setFlagZ(out == 0);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(false);
    cpu.regs().setFlagC(cy != 0);
    return out;
}

// RL - rotate left through carry
static uint8_t do_rl(CPU& cpu, uint8_t val) {
    uint8_t old = (val >> 7) & 1;
    uint8_t out = static_cast<uint8_t>((val << 1) | (cpu.regs().flagC() ? 1 : 0));
    cpu.regs().setFlagZ(out == 0);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(false);
    cpu.regs().setFlagC(old != 0);
    return out;
}

// RR - rotate right through carry
static uint8_t do_rr(CPU& cpu, uint8_t val) {
    uint8_t old = val & 1;
    uint8_t out = static_cast<uint8_t>((val >> 1) | (cpu.regs().flagC() ? 0x80 : 0));
    cpu.regs().setFlagZ(out == 0);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(false);
    cpu.regs().setFlagC(old != 0);
    return out;
}

// SLA - shift left arithmetic; bit 7 -> carry, bit 0 = 0
static uint8_t do_sla(CPU& cpu, uint8_t val) {
    uint8_t cy  = (val >> 7) & 1;
    uint8_t out = static_cast<uint8_t>(val << 1);
    cpu.regs().setFlagZ(out == 0);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(false);
    cpu.regs().setFlagC(cy != 0);
    return out;
}

// SRA - shift right arithmetic; bit 0 -> carry, bit 7 preserved
static uint8_t do_sra(CPU& cpu, uint8_t val) {
    uint8_t cy  = val & 1;
    uint8_t out = static_cast<uint8_t>((val >> 1) | (val & 0x80));
    cpu.regs().setFlagZ(out == 0);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(false);
    cpu.regs().setFlagC(cy != 0);
    return out;
}

// SWAP - swap upper and lower nibbles
static uint8_t do_swap(CPU& cpu, uint8_t val) {
    uint8_t out = static_cast<uint8_t>((val << 4) | (val >> 4));
    cpu.regs().setFlagZ(out == 0);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(false);
    cpu.regs().setFlagC(false);
    return out;
}

// SRL - shift right logical; bit 0 -> carry, bit 7 = 0
static uint8_t do_srl(CPU& cpu, uint8_t val) {
    uint8_t cy  = val & 1;
    uint8_t out = static_cast<uint8_t>(val >> 1);
    cpu.regs().setFlagZ(out == 0);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(false);
    cpu.regs().setFlagC(cy != 0);
    return out;
}

// BIT b, r8 - test bit, set Z flag; N cleared, H set
static void do_bit(CPU& cpu, uint8_t bit, uint8_t val) {
    cpu.regs().setFlagZ(((val >> bit) & 1) == 0);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(true);
    // C not affected
}

// ---------------------------------------------------------------------------
// Generic dispatch helpers
// ---------------------------------------------------------------------------

using ShiftFn = uint8_t(*)(CPU&, uint8_t);

static uint32_t shift_op(CPU& cpu, uint8_t idx, ShiftFn fn) {
    uint8_t val = readR8(cpu, idx);
    writeR8(cpu, idx, fn(cpu, val));
    return isHL(idx) ? 16u : 8u;
}

static uint32_t bit_op(CPU& cpu, uint8_t bit, uint8_t idx) {
    do_bit(cpu, bit, readR8(cpu, idx));
    return isHL(idx) ? 12u : 8u;
}

static uint32_t res_op(CPU& cpu, uint8_t bit, uint8_t idx) {
    writeR8(cpu, idx, readR8(cpu, idx) & ~static_cast<uint8_t>(1u << bit));
    return isHL(idx) ? 16u : 8u;
}

static uint32_t set_op(CPU& cpu, uint8_t bit, uint8_t idx) {
    writeR8(cpu, idx, readR8(cpu, idx) | static_cast<uint8_t>(1u << bit));
    return isHL(idx) ? 16u : 8u;
}

// ---------------------------------------------------------------------------
// Handler definitions
// Naming: cb_NN where NN is the sub-opcode hex (after the 0xCB prefix byte)
// ---------------------------------------------------------------------------

// 0x00-0x07  RLC r8
uint32_t cb_00(CPU& cpu) { return shift_op(cpu, 0, do_rlc); }
uint32_t cb_01(CPU& cpu) { return shift_op(cpu, 1, do_rlc); }
uint32_t cb_02(CPU& cpu) { return shift_op(cpu, 2, do_rlc); }
uint32_t cb_03(CPU& cpu) { return shift_op(cpu, 3, do_rlc); }
uint32_t cb_04(CPU& cpu) { return shift_op(cpu, 4, do_rlc); }
uint32_t cb_05(CPU& cpu) { return shift_op(cpu, 5, do_rlc); }
uint32_t cb_06(CPU& cpu) { return shift_op(cpu, 6, do_rlc); } // RLC (HL) - 16T
uint32_t cb_07(CPU& cpu) { return shift_op(cpu, 7, do_rlc); }

// 0x08-0x0F  RRC r8
uint32_t cb_08(CPU& cpu) { return shift_op(cpu, 0, do_rrc); }
uint32_t cb_09(CPU& cpu) { return shift_op(cpu, 1, do_rrc); }
uint32_t cb_0A(CPU& cpu) { return shift_op(cpu, 2, do_rrc); }
uint32_t cb_0B(CPU& cpu) { return shift_op(cpu, 3, do_rrc); }
uint32_t cb_0C(CPU& cpu) { return shift_op(cpu, 4, do_rrc); }
uint32_t cb_0D(CPU& cpu) { return shift_op(cpu, 5, do_rrc); }
uint32_t cb_0E(CPU& cpu) { return shift_op(cpu, 6, do_rrc); } // RRC (HL) - 16T
uint32_t cb_0F(CPU& cpu) { return shift_op(cpu, 7, do_rrc); }

// 0x10-0x17  RL r8
uint32_t cb_10(CPU& cpu) { return shift_op(cpu, 0, do_rl); }
uint32_t cb_11(CPU& cpu) { return shift_op(cpu, 1, do_rl); }
uint32_t cb_12(CPU& cpu) { return shift_op(cpu, 2, do_rl); }
uint32_t cb_13(CPU& cpu) { return shift_op(cpu, 3, do_rl); }
uint32_t cb_14(CPU& cpu) { return shift_op(cpu, 4, do_rl); }
uint32_t cb_15(CPU& cpu) { return shift_op(cpu, 5, do_rl); }
uint32_t cb_16(CPU& cpu) { return shift_op(cpu, 6, do_rl); } // RL (HL) - 16T
uint32_t cb_17(CPU& cpu) { return shift_op(cpu, 7, do_rl); }

// 0x18-0x1F  RR r8
uint32_t cb_18(CPU& cpu) { return shift_op(cpu, 0, do_rr); }
uint32_t cb_19(CPU& cpu) { return shift_op(cpu, 1, do_rr); }
uint32_t cb_1A(CPU& cpu) { return shift_op(cpu, 2, do_rr); }
uint32_t cb_1B(CPU& cpu) { return shift_op(cpu, 3, do_rr); }
uint32_t cb_1C(CPU& cpu) { return shift_op(cpu, 4, do_rr); }
uint32_t cb_1D(CPU& cpu) { return shift_op(cpu, 5, do_rr); }
uint32_t cb_1E(CPU& cpu) { return shift_op(cpu, 6, do_rr); } // RR (HL) - 16T
uint32_t cb_1F(CPU& cpu) { return shift_op(cpu, 7, do_rr); }

// 0x20-0x27  SLA r8
uint32_t cb_20(CPU& cpu) { return shift_op(cpu, 0, do_sla); }
uint32_t cb_21(CPU& cpu) { return shift_op(cpu, 1, do_sla); }
uint32_t cb_22(CPU& cpu) { return shift_op(cpu, 2, do_sla); }
uint32_t cb_23(CPU& cpu) { return shift_op(cpu, 3, do_sla); }
uint32_t cb_24(CPU& cpu) { return shift_op(cpu, 4, do_sla); }
uint32_t cb_25(CPU& cpu) { return shift_op(cpu, 5, do_sla); }
uint32_t cb_26(CPU& cpu) { return shift_op(cpu, 6, do_sla); } // SLA (HL) - 16T
uint32_t cb_27(CPU& cpu) { return shift_op(cpu, 7, do_sla); }

// 0x28-0x2F  SRA r8
uint32_t cb_28(CPU& cpu) { return shift_op(cpu, 0, do_sra); }
uint32_t cb_29(CPU& cpu) { return shift_op(cpu, 1, do_sra); }
uint32_t cb_2A(CPU& cpu) { return shift_op(cpu, 2, do_sra); }
uint32_t cb_2B(CPU& cpu) { return shift_op(cpu, 3, do_sra); }
uint32_t cb_2C(CPU& cpu) { return shift_op(cpu, 4, do_sra); }
uint32_t cb_2D(CPU& cpu) { return shift_op(cpu, 5, do_sra); }
uint32_t cb_2E(CPU& cpu) { return shift_op(cpu, 6, do_sra); } // SRA (HL) - 16T
uint32_t cb_2F(CPU& cpu) { return shift_op(cpu, 7, do_sra); }

// 0x30-0x37  SWAP r8
uint32_t cb_30(CPU& cpu) { return shift_op(cpu, 0, do_swap); }
uint32_t cb_31(CPU& cpu) { return shift_op(cpu, 1, do_swap); }
uint32_t cb_32(CPU& cpu) { return shift_op(cpu, 2, do_swap); }
uint32_t cb_33(CPU& cpu) { return shift_op(cpu, 3, do_swap); }
uint32_t cb_34(CPU& cpu) { return shift_op(cpu, 4, do_swap); }
uint32_t cb_35(CPU& cpu) { return shift_op(cpu, 5, do_swap); }
uint32_t cb_36(CPU& cpu) { return shift_op(cpu, 6, do_swap); } // SWAP (HL) - 16T
uint32_t cb_37(CPU& cpu) { return shift_op(cpu, 7, do_swap); }

// 0x38-0x3F  SRL r8
uint32_t cb_38(CPU& cpu) { return shift_op(cpu, 0, do_srl); }
uint32_t cb_39(CPU& cpu) { return shift_op(cpu, 1, do_srl); }
uint32_t cb_3A(CPU& cpu) { return shift_op(cpu, 2, do_srl); }
uint32_t cb_3B(CPU& cpu) { return shift_op(cpu, 3, do_srl); }
uint32_t cb_3C(CPU& cpu) { return shift_op(cpu, 4, do_srl); }
uint32_t cb_3D(CPU& cpu) { return shift_op(cpu, 5, do_srl); }
uint32_t cb_3E(CPU& cpu) { return shift_op(cpu, 6, do_srl); } // SRL (HL) - 16T
uint32_t cb_3F(CPU& cpu) { return shift_op(cpu, 7, do_srl); }

// 0x40-0x7F  BIT b, r8  (bit = (opcode >> 3) & 7, reg = opcode & 7)
uint32_t cb_40(CPU& cpu) { return bit_op(cpu, 0, 0); }
uint32_t cb_41(CPU& cpu) { return bit_op(cpu, 0, 1); }
uint32_t cb_42(CPU& cpu) { return bit_op(cpu, 0, 2); }
uint32_t cb_43(CPU& cpu) { return bit_op(cpu, 0, 3); }
uint32_t cb_44(CPU& cpu) { return bit_op(cpu, 0, 4); }
uint32_t cb_45(CPU& cpu) { return bit_op(cpu, 0, 5); }
uint32_t cb_46(CPU& cpu) { return bit_op(cpu, 0, 6); }
uint32_t cb_47(CPU& cpu) { return bit_op(cpu, 0, 7); }
uint32_t cb_48(CPU& cpu) { return bit_op(cpu, 1, 0); }
uint32_t cb_49(CPU& cpu) { return bit_op(cpu, 1, 1); }
uint32_t cb_4A(CPU& cpu) { return bit_op(cpu, 1, 2); }
uint32_t cb_4B(CPU& cpu) { return bit_op(cpu, 1, 3); }
uint32_t cb_4C(CPU& cpu) { return bit_op(cpu, 1, 4); }
uint32_t cb_4D(CPU& cpu) { return bit_op(cpu, 1, 5); }
uint32_t cb_4E(CPU& cpu) { return bit_op(cpu, 1, 6); }
uint32_t cb_4F(CPU& cpu) { return bit_op(cpu, 1, 7); }
uint32_t cb_50(CPU& cpu) { return bit_op(cpu, 2, 0); }
uint32_t cb_51(CPU& cpu) { return bit_op(cpu, 2, 1); }
uint32_t cb_52(CPU& cpu) { return bit_op(cpu, 2, 2); }
uint32_t cb_53(CPU& cpu) { return bit_op(cpu, 2, 3); }
uint32_t cb_54(CPU& cpu) { return bit_op(cpu, 2, 4); }
uint32_t cb_55(CPU& cpu) { return bit_op(cpu, 2, 5); }
uint32_t cb_56(CPU& cpu) { return bit_op(cpu, 2, 6); }
uint32_t cb_57(CPU& cpu) { return bit_op(cpu, 2, 7); }
uint32_t cb_58(CPU& cpu) { return bit_op(cpu, 3, 0); }
uint32_t cb_59(CPU& cpu) { return bit_op(cpu, 3, 1); }
uint32_t cb_5A(CPU& cpu) { return bit_op(cpu, 3, 2); }
uint32_t cb_5B(CPU& cpu) { return bit_op(cpu, 3, 3); }
uint32_t cb_5C(CPU& cpu) { return bit_op(cpu, 3, 4); }
uint32_t cb_5D(CPU& cpu) { return bit_op(cpu, 3, 5); }
uint32_t cb_5E(CPU& cpu) { return bit_op(cpu, 3, 6); }
uint32_t cb_5F(CPU& cpu) { return bit_op(cpu, 3, 7); }
uint32_t cb_60(CPU& cpu) { return bit_op(cpu, 4, 0); }
uint32_t cb_61(CPU& cpu) { return bit_op(cpu, 4, 1); }
uint32_t cb_62(CPU& cpu) { return bit_op(cpu, 4, 2); }
uint32_t cb_63(CPU& cpu) { return bit_op(cpu, 4, 3); }
uint32_t cb_64(CPU& cpu) { return bit_op(cpu, 4, 4); }
uint32_t cb_65(CPU& cpu) { return bit_op(cpu, 4, 5); }
uint32_t cb_66(CPU& cpu) { return bit_op(cpu, 4, 6); }
uint32_t cb_67(CPU& cpu) { return bit_op(cpu, 4, 7); }
uint32_t cb_68(CPU& cpu) { return bit_op(cpu, 5, 0); }
uint32_t cb_69(CPU& cpu) { return bit_op(cpu, 5, 1); }
uint32_t cb_6A(CPU& cpu) { return bit_op(cpu, 5, 2); }
uint32_t cb_6B(CPU& cpu) { return bit_op(cpu, 5, 3); }
uint32_t cb_6C(CPU& cpu) { return bit_op(cpu, 5, 4); }
uint32_t cb_6D(CPU& cpu) { return bit_op(cpu, 5, 5); }
uint32_t cb_6E(CPU& cpu) { return bit_op(cpu, 5, 6); }
uint32_t cb_6F(CPU& cpu) { return bit_op(cpu, 5, 7); }
uint32_t cb_70(CPU& cpu) { return bit_op(cpu, 6, 0); }
uint32_t cb_71(CPU& cpu) { return bit_op(cpu, 6, 1); }
uint32_t cb_72(CPU& cpu) { return bit_op(cpu, 6, 2); }
uint32_t cb_73(CPU& cpu) { return bit_op(cpu, 6, 3); }
uint32_t cb_74(CPU& cpu) { return bit_op(cpu, 6, 4); }
uint32_t cb_75(CPU& cpu) { return bit_op(cpu, 6, 5); }
uint32_t cb_76(CPU& cpu) { return bit_op(cpu, 6, 6); }
uint32_t cb_77(CPU& cpu) { return bit_op(cpu, 6, 7); }
uint32_t cb_78(CPU& cpu) { return bit_op(cpu, 7, 0); }
uint32_t cb_79(CPU& cpu) { return bit_op(cpu, 7, 1); }
uint32_t cb_7A(CPU& cpu) { return bit_op(cpu, 7, 2); }
uint32_t cb_7B(CPU& cpu) { return bit_op(cpu, 7, 3); }
uint32_t cb_7C(CPU& cpu) { return bit_op(cpu, 7, 4); }
uint32_t cb_7D(CPU& cpu) { return bit_op(cpu, 7, 5); }
uint32_t cb_7E(CPU& cpu) { return bit_op(cpu, 7, 6); }
uint32_t cb_7F(CPU& cpu) { return bit_op(cpu, 7, 7); }

// 0x80-0xBF  RES b, r8
uint32_t cb_80(CPU& cpu) { return res_op(cpu, 0, 0); }
uint32_t cb_81(CPU& cpu) { return res_op(cpu, 0, 1); }
uint32_t cb_82(CPU& cpu) { return res_op(cpu, 0, 2); }
uint32_t cb_83(CPU& cpu) { return res_op(cpu, 0, 3); }
uint32_t cb_84(CPU& cpu) { return res_op(cpu, 0, 4); }
uint32_t cb_85(CPU& cpu) { return res_op(cpu, 0, 5); }
uint32_t cb_86(CPU& cpu) { return res_op(cpu, 0, 6); }
uint32_t cb_87(CPU& cpu) { return res_op(cpu, 0, 7); }
uint32_t cb_88(CPU& cpu) { return res_op(cpu, 1, 0); }
uint32_t cb_89(CPU& cpu) { return res_op(cpu, 1, 1); }
uint32_t cb_8A(CPU& cpu) { return res_op(cpu, 1, 2); }
uint32_t cb_8B(CPU& cpu) { return res_op(cpu, 1, 3); }
uint32_t cb_8C(CPU& cpu) { return res_op(cpu, 1, 4); }
uint32_t cb_8D(CPU& cpu) { return res_op(cpu, 1, 5); }
uint32_t cb_8E(CPU& cpu) { return res_op(cpu, 1, 6); }
uint32_t cb_8F(CPU& cpu) { return res_op(cpu, 1, 7); }
uint32_t cb_90(CPU& cpu) { return res_op(cpu, 2, 0); }
uint32_t cb_91(CPU& cpu) { return res_op(cpu, 2, 1); }
uint32_t cb_92(CPU& cpu) { return res_op(cpu, 2, 2); }
uint32_t cb_93(CPU& cpu) { return res_op(cpu, 2, 3); }
uint32_t cb_94(CPU& cpu) { return res_op(cpu, 2, 4); }
uint32_t cb_95(CPU& cpu) { return res_op(cpu, 2, 5); }
uint32_t cb_96(CPU& cpu) { return res_op(cpu, 2, 6); }
uint32_t cb_97(CPU& cpu) { return res_op(cpu, 2, 7); }
uint32_t cb_98(CPU& cpu) { return res_op(cpu, 3, 0); }
uint32_t cb_99(CPU& cpu) { return res_op(cpu, 3, 1); }
uint32_t cb_9A(CPU& cpu) { return res_op(cpu, 3, 2); }
uint32_t cb_9B(CPU& cpu) { return res_op(cpu, 3, 3); }
uint32_t cb_9C(CPU& cpu) { return res_op(cpu, 3, 4); }
uint32_t cb_9D(CPU& cpu) { return res_op(cpu, 3, 5); }
uint32_t cb_9E(CPU& cpu) { return res_op(cpu, 3, 6); }
uint32_t cb_9F(CPU& cpu) { return res_op(cpu, 3, 7); }
uint32_t cb_A0(CPU& cpu) { return res_op(cpu, 4, 0); }
uint32_t cb_A1(CPU& cpu) { return res_op(cpu, 4, 1); }
uint32_t cb_A2(CPU& cpu) { return res_op(cpu, 4, 2); }
uint32_t cb_A3(CPU& cpu) { return res_op(cpu, 4, 3); }
uint32_t cb_A4(CPU& cpu) { return res_op(cpu, 4, 4); }
uint32_t cb_A5(CPU& cpu) { return res_op(cpu, 4, 5); }
uint32_t cb_A6(CPU& cpu) { return res_op(cpu, 4, 6); }
uint32_t cb_A7(CPU& cpu) { return res_op(cpu, 4, 7); }
uint32_t cb_A8(CPU& cpu) { return res_op(cpu, 5, 0); }
uint32_t cb_A9(CPU& cpu) { return res_op(cpu, 5, 1); }
uint32_t cb_AA(CPU& cpu) { return res_op(cpu, 5, 2); }
uint32_t cb_AB(CPU& cpu) { return res_op(cpu, 5, 3); }
uint32_t cb_AC(CPU& cpu) { return res_op(cpu, 5, 4); }
uint32_t cb_AD(CPU& cpu) { return res_op(cpu, 5, 5); }
uint32_t cb_AE(CPU& cpu) { return res_op(cpu, 5, 6); }
uint32_t cb_AF(CPU& cpu) { return res_op(cpu, 5, 7); }
uint32_t cb_B0(CPU& cpu) { return res_op(cpu, 6, 0); }
uint32_t cb_B1(CPU& cpu) { return res_op(cpu, 6, 1); }
uint32_t cb_B2(CPU& cpu) { return res_op(cpu, 6, 2); }
uint32_t cb_B3(CPU& cpu) { return res_op(cpu, 6, 3); }
uint32_t cb_B4(CPU& cpu) { return res_op(cpu, 6, 4); }
uint32_t cb_B5(CPU& cpu) { return res_op(cpu, 6, 5); }
uint32_t cb_B6(CPU& cpu) { return res_op(cpu, 6, 6); }
uint32_t cb_B7(CPU& cpu) { return res_op(cpu, 6, 7); }
uint32_t cb_B8(CPU& cpu) { return res_op(cpu, 7, 0); }
uint32_t cb_B9(CPU& cpu) { return res_op(cpu, 7, 1); }
uint32_t cb_BA(CPU& cpu) { return res_op(cpu, 7, 2); }
uint32_t cb_BB(CPU& cpu) { return res_op(cpu, 7, 3); }
uint32_t cb_BC(CPU& cpu) { return res_op(cpu, 7, 4); }
uint32_t cb_BD(CPU& cpu) { return res_op(cpu, 7, 5); }
uint32_t cb_BE(CPU& cpu) { return res_op(cpu, 7, 6); }
uint32_t cb_BF(CPU& cpu) { return res_op(cpu, 7, 7); }

// 0xC0-0xFF  SET b, r8
uint32_t cb_C0(CPU& cpu) { return set_op(cpu, 0, 0); }
uint32_t cb_C1(CPU& cpu) { return set_op(cpu, 0, 1); }
uint32_t cb_C2(CPU& cpu) { return set_op(cpu, 0, 2); }
uint32_t cb_C3(CPU& cpu) { return set_op(cpu, 0, 3); }
uint32_t cb_C4(CPU& cpu) { return set_op(cpu, 0, 4); }
uint32_t cb_C5(CPU& cpu) { return set_op(cpu, 0, 5); }
uint32_t cb_C6(CPU& cpu) { return set_op(cpu, 0, 6); }
uint32_t cb_C7(CPU& cpu) { return set_op(cpu, 0, 7); }
uint32_t cb_C8(CPU& cpu) { return set_op(cpu, 1, 0); }
uint32_t cb_C9(CPU& cpu) { return set_op(cpu, 1, 1); }
uint32_t cb_CA(CPU& cpu) { return set_op(cpu, 1, 2); }
uint32_t cb_CB(CPU& cpu) { return set_op(cpu, 1, 3); }
uint32_t cb_CC(CPU& cpu) { return set_op(cpu, 1, 4); }
uint32_t cb_CD(CPU& cpu) { return set_op(cpu, 1, 5); }
uint32_t cb_CE(CPU& cpu) { return set_op(cpu, 1, 6); }
uint32_t cb_CF(CPU& cpu) { return set_op(cpu, 1, 7); }
uint32_t cb_D0(CPU& cpu) { return set_op(cpu, 2, 0); }
uint32_t cb_D1(CPU& cpu) { return set_op(cpu, 2, 1); }
uint32_t cb_D2(CPU& cpu) { return set_op(cpu, 2, 2); }
uint32_t cb_D3(CPU& cpu) { return set_op(cpu, 2, 3); }
uint32_t cb_D4(CPU& cpu) { return set_op(cpu, 2, 4); }
uint32_t cb_D5(CPU& cpu) { return set_op(cpu, 2, 5); }
uint32_t cb_D6(CPU& cpu) { return set_op(cpu, 2, 6); }
uint32_t cb_D7(CPU& cpu) { return set_op(cpu, 2, 7); }
uint32_t cb_D8(CPU& cpu) { return set_op(cpu, 3, 0); }
uint32_t cb_D9(CPU& cpu) { return set_op(cpu, 3, 1); }
uint32_t cb_DA(CPU& cpu) { return set_op(cpu, 3, 2); }
uint32_t cb_DB(CPU& cpu) { return set_op(cpu, 3, 3); }
uint32_t cb_DC(CPU& cpu) { return set_op(cpu, 3, 4); }
uint32_t cb_DD(CPU& cpu) { return set_op(cpu, 3, 5); }
uint32_t cb_DE(CPU& cpu) { return set_op(cpu, 3, 6); }
uint32_t cb_DF(CPU& cpu) { return set_op(cpu, 3, 7); }
uint32_t cb_E0(CPU& cpu) { return set_op(cpu, 4, 0); }
uint32_t cb_E1(CPU& cpu) { return set_op(cpu, 4, 1); }
uint32_t cb_E2(CPU& cpu) { return set_op(cpu, 4, 2); }
uint32_t cb_E3(CPU& cpu) { return set_op(cpu, 4, 3); }
uint32_t cb_E4(CPU& cpu) { return set_op(cpu, 4, 4); }
uint32_t cb_E5(CPU& cpu) { return set_op(cpu, 4, 5); }
uint32_t cb_E6(CPU& cpu) { return set_op(cpu, 4, 6); }
uint32_t cb_E7(CPU& cpu) { return set_op(cpu, 4, 7); }
uint32_t cb_E8(CPU& cpu) { return set_op(cpu, 5, 0); }
uint32_t cb_E9(CPU& cpu) { return set_op(cpu, 5, 1); }
uint32_t cb_EA(CPU& cpu) { return set_op(cpu, 5, 2); }
uint32_t cb_EB(CPU& cpu) { return set_op(cpu, 5, 3); }
uint32_t cb_EC(CPU& cpu) { return set_op(cpu, 5, 4); }
uint32_t cb_ED(CPU& cpu) { return set_op(cpu, 5, 5); }
uint32_t cb_EE(CPU& cpu) { return set_op(cpu, 5, 6); }
uint32_t cb_EF(CPU& cpu) { return set_op(cpu, 5, 7); }
uint32_t cb_F0(CPU& cpu) { return set_op(cpu, 6, 0); }
uint32_t cb_F1(CPU& cpu) { return set_op(cpu, 6, 1); }
uint32_t cb_F2(CPU& cpu) { return set_op(cpu, 6, 2); }
uint32_t cb_F3(CPU& cpu) { return set_op(cpu, 6, 3); }
uint32_t cb_F4(CPU& cpu) { return set_op(cpu, 6, 4); }
uint32_t cb_F5(CPU& cpu) { return set_op(cpu, 6, 5); }
uint32_t cb_F6(CPU& cpu) { return set_op(cpu, 6, 6); }
uint32_t cb_F7(CPU& cpu) { return set_op(cpu, 6, 7); }
uint32_t cb_F8(CPU& cpu) { return set_op(cpu, 7, 0); }
uint32_t cb_F9(CPU& cpu) { return set_op(cpu, 7, 1); }
uint32_t cb_FA(CPU& cpu) { return set_op(cpu, 7, 2); }
uint32_t cb_FB(CPU& cpu) { return set_op(cpu, 7, 3); }
uint32_t cb_FC(CPU& cpu) { return set_op(cpu, 7, 4); }
uint32_t cb_FD(CPU& cpu) { return set_op(cpu, 7, 5); }
uint32_t cb_FE(CPU& cpu) { return set_op(cpu, 7, 6); }
uint32_t cb_FF(CPU& cpu) { return set_op(cpu, 7, 7); }

}
