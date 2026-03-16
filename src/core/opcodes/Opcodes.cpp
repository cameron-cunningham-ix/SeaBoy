// Opcodes.cpp - Unprefixed opcode handlers for the Sharp SM83 CPU
//
// Each function implements one opcode and returns the T-cycles consumed.
// Conditional branches return the taken or not-taken count directly.
//
// Reference: https://rgbds.gbdev.io/docs/v1.0.1/gbz80.7
// Opcode table: https://gbdev.io/gb-opcodes/optables/
// PanDocs.14

#include "src/core/CPU.hpp"
#include "src/core/MMU.hpp"
#include "src/core/Registers.hpp"

namespace SeaBoy::Opcodes
{

// ---------------------------------------------------------------------------
// Helper: push a 16-bit value onto the stack
// PanDocs.25 OAM Corruption Bug: PUSH triggers Write corruption for each SP--
// IDU when SP is in 0xFE00–0xFEFF. Trigger fires with the pre-decrement value.
// ---------------------------------------------------------------------------
static void stackPush(CPU& cpu, uint16_t val) {
    cpu.mmu().triggerOAMCorrupt(cpu.regs().SP, OAMCorruptType::Write); // SP-- IDU (high byte)
    cpu.mmu().write8(--cpu.regs().SP, static_cast<uint8_t>(val >> 8));
    cpu.mmu().triggerOAMCorrupt(cpu.regs().SP, OAMCorruptType::Write); // SP-- IDU (low byte)
    cpu.mmu().write8(--cpu.regs().SP, static_cast<uint8_t>(val));
}

// Helper: pop a 16-bit value from the stack
// PanDocs.25 OAM Corruption Bug: POP triggers 3 corruption events when SP is in
// 0xFE00–0xFEFF - one read, one glitched write from the SP++ IDU, and a second
// read without a second glitched write.
static uint16_t stackPop(CPU& cpu) {
    uint16_t sp0 = cpu.regs().SP;
    cpu.mmu().triggerOAMCorrupt(sp0, OAMCorruptType::Read);    // M2 start: bus read
    uint8_t lo = cpu.mmu().read8(sp0);                          // M2 callback: PPU +4T
    cpu.mmu().triggerOAMCorrupt(sp0, OAMCorruptType::Write);   // M2 end: SP++ IDU
    cpu.regs().SP = static_cast<uint16_t>(sp0 + 1);
    cpu.mmu().triggerOAMCorrupt(cpu.regs().SP, OAMCorruptType::Read);  // M3 start: bus read
    uint8_t hi = cpu.mmu().read8(cpu.regs().SP++);              // M3 callback: PPU +4T
    return static_cast<uint16_t>(lo | (hi << 8));
}

// ---------------------------------------------------------------------------
// Helper: 8-bit ADD to A, updating flags
// ---------------------------------------------------------------------------
static void alu_add(CPU& cpu, uint8_t val, bool withCarry = false) {
    Registers& r  = cpu.regs();
    uint8_t    cy = (withCarry && r.flagC()) ? 1 : 0;
    uint16_t   result = static_cast<uint16_t>(r.A) + val + cy;

    r.setFlagZ((result & 0xFF) == 0);
    r.setFlagN(false);
    r.setFlagH(((r.A & 0x0F) + (val & 0x0F) + cy) > 0x0F);
    r.setFlagC(result > 0xFF);
    r.A = static_cast<uint8_t>(result);
}

// Helper: 8-bit SUB from A, updating flags
static void alu_sub(CPU& cpu, uint8_t val, bool withCarry = false) {
    Registers& r  = cpu.regs();
    uint8_t    cy = (withCarry && r.flagC()) ? 1 : 0;
    int        result = static_cast<int>(r.A) - val - cy;

    r.setFlagZ((result & 0xFF) == 0);
    r.setFlagN(true);
    r.setFlagH(static_cast<int>(r.A & 0x0F) - static_cast<int>(val & 0x0F) - cy < 0);
    r.setFlagC(result < 0);
    r.A = static_cast<uint8_t>(result);
}

// Helper: AND A with val
static void alu_and(CPU& cpu, uint8_t val) {
    Registers& r = cpu.regs();
    r.A &= val;
    r.setFlagZ(r.A == 0);
    r.setFlagN(false);
    r.setFlagH(true);
    r.setFlagC(false);
}

// Helper: OR A with val
static void alu_or(CPU& cpu, uint8_t val) {
    Registers& r = cpu.regs();
    r.A |= val;
    r.setFlagZ(r.A == 0);
    r.setFlagN(false);
    r.setFlagH(false);
    r.setFlagC(false);
}

// Helper: XOR A with val
static void alu_xor(CPU& cpu, uint8_t val) {
    Registers& r = cpu.regs();
    r.A ^= val;
    r.setFlagZ(r.A == 0);
    r.setFlagN(false);
    r.setFlagH(false);
    r.setFlagC(false);
}

// Helper: CP (compare) - same as SUB but A is not written
static void alu_cp(CPU& cpu, uint8_t val) {
    Registers& r = cpu.regs();
    int result   = static_cast<int>(r.A) - val;
    r.setFlagZ((result & 0xFF) == 0);
    r.setFlagN(true);
    r.setFlagH((r.A & 0x0F) < (val & 0x0F));
    r.setFlagC(result < 0);
}

// Helper: INC an 8-bit value, update flags (C unchanged)
static uint8_t alu_inc8(CPU& cpu, uint8_t val) {
    Registers& r = cpu.regs();
    uint8_t result = val + 1;
    r.setFlagZ(result == 0);
    r.setFlagN(false);
    r.setFlagH((val & 0x0F) == 0x0F);
    // carry not affected
    return result;
}

// Helper: DEC an 8-bit value, update flags (C unchanged)
static uint8_t alu_dec8(CPU& cpu, uint8_t val) {
    Registers& r = cpu.regs();
    uint8_t result = val - 1;
    r.setFlagZ(result == 0);
    r.setFlagN(true);
    r.setFlagH((val & 0x0F) == 0x00);
    // carry not affected
    return result;
}

// Helper: ADD HL, rp - 16-bit add, updates N/H/C flags only
static void alu_add_hl(CPU& cpu, uint16_t val) {
    Registers& r  = cpu.regs();
    uint32_t   result = static_cast<uint32_t>(r.getHL()) + val;
    r.setFlagN(false);
    r.setFlagH(((r.getHL() & 0x0FFF) + (val & 0x0FFF)) > 0x0FFF);
    r.setFlagC(result > 0xFFFF);
    r.setHL(static_cast<uint16_t>(result));
}

// ---------------------------------------------------------------------------
// 0x00 - 0x0F  (misc, loads, rotates)
// ---------------------------------------------------------------------------

// 0x00 NOP - 4T
uint32_t op_00(CPU&) { return 4; }

// 0x01 LD BC,n16 - 12T
uint32_t op_01(CPU& cpu) { 
    cpu.regs().setBC(cpu.fetch16());
    return 12;
}

// 0x02 LD (BC),A - 8T
uint32_t op_02(CPU& cpu) {
    cpu.mmu().write8(cpu.regs().getBC(), cpu.regs().A);
    return 8;
}

// 0x03 INC BC - 8T
uint32_t op_03(CPU& cpu) {
    uint16_t pre = cpu.regs().getBC();
    cpu.regs().setBC(static_cast<uint16_t>(pre + 1));
    cpu.internalCycle(); // IDU fires during this M-cycle; PPU advances 4T
    cpu.mmu().triggerOAMCorrupt(pre, OAMCorruptType::Write); // trigger after IDU M-cycle
    return 8;
}

// 0x04 INC B - 4T
uint32_t op_04(CPU& cpu) {
    cpu.regs().B = alu_inc8(cpu, cpu.regs().B);
    return 4;
}

// 0x05 DEC B - 4T
uint32_t op_05(CPU& cpu) {
    cpu.regs().B = alu_dec8(cpu, cpu.regs().B);
    return 4;
}

// 0x06 LD B,n8 - 8T
uint32_t op_06(CPU& cpu) { 
    cpu.regs().B = cpu.fetch8(); 
    return 8; 
}

// 0x07 RLCA - 4T
// GBZ80 - Rotate A left, old bit 7 into carry and bit 0
uint32_t op_07(CPU& cpu) {
    Registers& r  = cpu.regs();
    uint8_t    cy = (r.A >> 7) & 1;
    r.A           = static_cast<uint8_t>((r.A << 1) | cy);
    r.setFlagZ(false);
    r.setFlagN(false);
    r.setFlagH(false);
    r.setFlagC(cy != 0);
    return 4;
}

// 0x08 LD (n16),SP - 20T
uint32_t op_08(CPU& cpu) {
    uint16_t addr = cpu.fetch16();
    cpu.mmu().write16(addr, cpu.regs().SP);
    return 20;
}

// 0x09 ADD HL,BC - 8T
uint32_t op_09(CPU& cpu) {
    alu_add_hl(cpu, cpu.regs().getBC());
    cpu.internalCycle();
    return 8;
}

// 0x0A LD A,(BC) - 8T
uint32_t op_0A(CPU& cpu) {
     cpu.regs().A = cpu.mmu().read8(cpu.regs().getBC()); 
    return 8; 
}

// 0x0B DEC BC - 8T
uint32_t op_0B(CPU& cpu) {
    uint16_t pre = cpu.regs().getBC();
    cpu.regs().setBC(static_cast<uint16_t>(pre - 1));
    cpu.internalCycle();
    cpu.mmu().triggerOAMCorrupt(pre, OAMCorruptType::Write);
    return 8;
}

// 0x0C INC C - 4T
uint32_t op_0C(CPU& cpu) { 
    cpu.regs().C = alu_inc8(cpu, cpu.regs().C); 
    return 4; 
}

// 0x0D DEC C - 4T
uint32_t op_0D(CPU& cpu) { 
    cpu.regs().C = alu_dec8(cpu, cpu.regs().C); 
    return 4;
}

// 0x0E LD C,n8 - 8T
uint32_t op_0E(CPU& cpu) { 
    cpu.regs().C = cpu.fetch8(); 
    return 8; 
}

// 0x0F RRCA - 4T
// Rotate A right, old bit 0 into carry and bit 7
uint32_t op_0F(CPU& cpu) {
    Registers& r  = cpu.regs();
    uint8_t    cy = r.A & 1;
    r.A           = static_cast<uint8_t>((r.A >> 1) | (cy << 7));
    r.setFlagZ(false);
    r.setFlagN(false);
    r.setFlagH(false);
    r.setFlagC(cy != 0);
    return 4;
}

// ---------------------------------------------------------------------------
// 0x10 - 0x1F
// ---------------------------------------------------------------------------

// 0x10 STOP - 4T (1-byte opcode; PC advances past opcode only, no second-byte fetch)
// PanDocs STOP: second 0x00 byte is part of the encoding but PC does not advance past it.
// PanDocs.10 CGB Double Speed Mode: if KEY1 bit 0 is armed, toggle speed.
uint32_t op_10(CPU& cpu)
{
    MMU& mmu = cpu.mmu();
    uint8_t key1 = mmu.peek8(0xFF4Du);
    if (mmu.isCGBMode() && (key1 & 0x01u)) {
        mmu.toggleSpeed();
        mmu.resetTimerDIV();    // PanDocs.10: STOP resets DIV
        // 2050 M-cycle CPU stall during speed switch - PanDocs.10
        // Speed is now double; PPU/APU get half-rate cycles via onBusCycle
        for (uint32_t i = 0; i < 2050; i++) {
            cpu.internalCycle();
        }
        return 4 + 2050 * 4; // opcode(4) + stall (2050*4)
    }
    return 4;
}

// 0x11 LD DE,n16 - 12T
uint32_t op_11(CPU& cpu) { 
    cpu.regs().setDE(cpu.fetch16()); 
    return 12; 
}

// 0x12 LD (DE),A - 8T
uint32_t op_12(CPU& cpu) {
    cpu.mmu().write8(cpu.regs().getDE(), cpu.regs().A);
    return 8;
}

// 0x13 INC DE - 8T
uint32_t op_13(CPU& cpu) {
    uint16_t pre = cpu.regs().getDE();
    cpu.regs().setDE(static_cast<uint16_t>(pre + 1));
    cpu.internalCycle();
    cpu.mmu().triggerOAMCorrupt(pre, OAMCorruptType::Write);
    return 8;
}

// 0x14 INC D - 4T
uint32_t op_14(CPU& cpu) { 
    cpu.regs().D = alu_inc8(cpu, cpu.regs().D); 
    return 4; 
}

// 0x15 DEC D - 4T
uint32_t op_15(CPU& cpu) { 
    cpu.regs().D = alu_dec8(cpu, cpu.regs().D); 
    return 4; 
}

// 0x16 LD D,n8 - 8T
uint32_t op_16(CPU& cpu) { 
    cpu.regs().D = cpu.fetch8(); 
    return 8; 
}

// 0x17 RLA - 4T
// Rotate A left through carry
uint32_t op_17(CPU& cpu) {
    Registers& r   = cpu.regs();
    uint8_t    old = (r.A >> 7) & 1;
    r.A            = static_cast<uint8_t>((r.A << 1) | (r.flagC() ? 1 : 0));
    r.setFlagZ(false);
    r.setFlagN(false);
    r.setFlagH(false);
    r.setFlagC(old != 0);
    return 4;
}

// 0x18 JR e8 - 12T
uint32_t op_18(CPU& cpu) {
    int8_t offset = static_cast<int8_t>(cpu.fetch8());
    cpu.regs().PC = static_cast<uint16_t>(cpu.regs().PC + offset);
    cpu.internalCycle(); // branch taken internal cycle
    return 12;
}

// 0x19 ADD HL,DE - 8T
uint32_t op_19(CPU& cpu) {
    alu_add_hl(cpu, cpu.regs().getDE());
    cpu.internalCycle();
    return 8;
}

// 0x1A LD A,(DE) - 8T
uint32_t op_1A(CPU& cpu) { 
    cpu.regs().A = cpu.mmu().read8(cpu.regs().getDE()); 
    return 8; 
}

// 0x1B DEC DE - 8T
uint32_t op_1B(CPU& cpu) {
    uint16_t pre = cpu.regs().getDE();
    cpu.regs().setDE(static_cast<uint16_t>(pre - 1));
    cpu.internalCycle();
    cpu.mmu().triggerOAMCorrupt(pre, OAMCorruptType::Write);
    return 8;
}

// 0x1C INC E - 4T
uint32_t op_1C(CPU& cpu) { 
    cpu.regs().E = alu_inc8(cpu, cpu.regs().E); 
    return 4; 
}

// 0x1D DEC E - 4T
uint32_t op_1D(CPU& cpu) { 
    cpu.regs().E = alu_dec8(cpu, cpu.regs().E); 
    return 4; 
}

// 0x1E LD E,n8 - 8T
uint32_t op_1E(CPU& cpu) { 
    cpu.regs().E = cpu.fetch8(); 
    return 8; 
}

// 0x1F RRA - 4T
// Rotate A right through carry
uint32_t op_1F(CPU& cpu) {
    Registers& r   = cpu.regs();
    uint8_t    old = r.A & 1;
    r.A            = static_cast<uint8_t>((r.A >> 1) | (r.flagC() ? 0x80 : 0));
    r.setFlagZ(false);
    r.setFlagN(false);
    r.setFlagH(false);
    r.setFlagC(old != 0);
    return 4;
}

// ---------------------------------------------------------------------------
// 0x20 - 0x2F
// ---------------------------------------------------------------------------

// 0x20 JR NZ,e8 - 12T taken / 8T not taken
uint32_t op_20(CPU& cpu) {
    int8_t offset = static_cast<int8_t>(cpu.fetch8());
    if (!cpu.regs().flagZ()) {
        cpu.regs().PC = static_cast<uint16_t>(cpu.regs().PC + offset);
        cpu.internalCycle();
        return 12;
    }
    return 8;
}

// 0x21 LD HL,n16 - 12T
uint32_t op_21(CPU& cpu) { 
    cpu.regs().setHL(cpu.fetch16()); 
    return 12; 
}

// 0x22 LD (HL+),A - 8T
uint32_t op_22(CPU& cpu) {
    cpu.mmu().write8(cpu.regs().getHL(), cpu.regs().A);
    cpu.regs().setHL(cpu.regs().getHL() + 1);
    return 8;
}

// 0x23 INC HL - 8T
uint32_t op_23(CPU& cpu) {
    uint16_t pre = cpu.regs().getHL();
    cpu.regs().setHL(static_cast<uint16_t>(pre + 1));
    cpu.internalCycle();
    cpu.mmu().triggerOAMCorrupt(pre, OAMCorruptType::Write);
    return 8;
}

// 0x24 INC H - 4T
uint32_t op_24(CPU& cpu) { 
    cpu.regs().H = alu_inc8(cpu, cpu.regs().H); 
    return 4; 
}

// 0x25 DEC H - 4T
uint32_t op_25(CPU& cpu) { 
    cpu.regs().H = alu_dec8(cpu, cpu.regs().H); 
    return 4; 
}

// 0x26 LD H,n8 - 8T
uint32_t op_26(CPU& cpu) { 
    cpu.regs().H = cpu.fetch8(); 
    return 8; 
}

// 0x27 DAA - 4T
// Decimal Adjust Accumulator - GBZ80
uint32_t op_27(CPU& cpu) {
    Registers& r      = cpu.regs();
    uint8_t    adjust = 0;
    bool       newC   = false;

    if (!r.flagN())
    {
        if (r.flagH() || (r.A & 0x0F) > 9)  adjust |= 0x06;
        if (r.flagC() || r.A > 0x99) {
            adjust |= 0x60;
            newC = true;
        }
        r.A += adjust;
    }
    else
    {
        if (r.flagH()) adjust |= 0x06;
        if (r.flagC()) {
            adjust |= 0x60;
            newC = true;
        }
        r.A -= adjust;
    }
    r.setFlagZ(r.A == 0);
    r.setFlagH(false);
    r.setFlagC(newC);
    return 4;
}

// 0x28 JR Z,e8 - 12T taken / 8T not taken
uint32_t op_28(CPU& cpu) {
    int8_t offset = static_cast<int8_t>(cpu.fetch8());
    if (cpu.regs().flagZ()) {
        cpu.regs().PC = static_cast<uint16_t>(cpu.regs().PC + offset);
        cpu.internalCycle();
        return 12;
    }
    return 8;
}

// 0x29 ADD HL,HL - 8T
uint32_t op_29(CPU& cpu) {
    alu_add_hl(cpu, cpu.regs().getHL());
    cpu.internalCycle();
    return 8;
}

// 0x2A LD A,(HL+) - 8T
uint32_t op_2A(CPU& cpu) {
    uint16_t hl = cpu.regs().getHL();
    cpu.mmu().triggerOAMCorrupt(hl, OAMCorruptType::ReadWrite); // read + IDU same M-cycle
    cpu.regs().A = cpu.mmu().read8(hl);
    cpu.regs().setHL(static_cast<uint16_t>(hl + 1));
    return 8;
}

// 0x2B DEC HL - 8T
uint32_t op_2B(CPU& cpu) {
    uint16_t pre = cpu.regs().getHL();
    cpu.regs().setHL(static_cast<uint16_t>(pre - 1));
    cpu.internalCycle();
    cpu.mmu().triggerOAMCorrupt(pre, OAMCorruptType::Write);
    return 8;
}

// 0x2C INC L - 4T
uint32_t op_2C(CPU& cpu) { 
    cpu.regs().L = alu_inc8(cpu, cpu.regs().L); 
    return 4; 
}

// 0x2D DEC L - 4T
uint32_t op_2D(CPU& cpu) { 
    cpu.regs().L = alu_dec8(cpu, cpu.regs().L); 
    return 4; 
}

// 0x2E LD L,n8 - 8T
uint32_t op_2E(CPU& cpu) { 
    cpu.regs().L = cpu.fetch8(); 
    return 8; 
}

// 0x2F CPL - 4T  (complement A)
uint32_t op_2F(CPU& cpu) {
    cpu.regs().A = ~cpu.regs().A;
    cpu.regs().setFlagN(true);
    cpu.regs().setFlagH(true);
    return 4;
}

// ---------------------------------------------------------------------------
// 0x30 - 0x3F
// ---------------------------------------------------------------------------

// 0x30 JR NC,e8 - 12T taken / 8T not taken
uint32_t op_30(CPU& cpu) {
    int8_t offset = static_cast<int8_t>(cpu.fetch8());
    if (!cpu.regs().flagC()) {
        cpu.regs().PC = static_cast<uint16_t>(cpu.regs().PC + offset);
        cpu.internalCycle();
        return 12;
    }
    return 8;
}

// 0x31 LD SP,n16 - 12T
uint32_t op_31(CPU& cpu) { 
    cpu.regs().SP = cpu.fetch16(); 
    return 12; 
}

// 0x32 LD (HL-),A - 8T
uint32_t op_32(CPU& cpu) {
    cpu.mmu().write8(cpu.regs().getHL(), cpu.regs().A);
    cpu.regs().setHL(cpu.regs().getHL() - 1);
    return 8;
}

// 0x33 INC SP - 8T
uint32_t op_33(CPU& cpu) {
    uint16_t pre = cpu.regs().SP++;
    cpu.internalCycle();
    cpu.mmu().triggerOAMCorrupt(pre, OAMCorruptType::Write);
    return 8;
}

// 0x34 INC (HL) - 12T
uint32_t op_34(CPU& cpu) {
    uint16_t addr = cpu.regs().getHL();
    uint8_t  val  = cpu.mmu().read8(addr);
    cpu.mmu().write8(addr, alu_inc8(cpu, val));
    return 12;
}

// 0x35 DEC (HL) - 12T
uint32_t op_35(CPU& cpu) {
    uint16_t addr = cpu.regs().getHL();
    uint8_t  val  = cpu.mmu().read8(addr);
    cpu.mmu().write8(addr, alu_dec8(cpu, val));
    return 12;
}

// 0x36 LD (HL),n8 - 12T
uint32_t op_36(CPU& cpu) {
    cpu.mmu().write8(cpu.regs().getHL(), cpu.fetch8());
    return 12;
}

// 0x37 SCF - 4T  (set carry flag)
uint32_t op_37(CPU& cpu) {
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(false);
    cpu.regs().setFlagC(true);
    return 4;
}

// 0x38 JR C,e8 - 12T taken / 8T not taken
uint32_t op_38(CPU& cpu) {
    int8_t offset = static_cast<int8_t>(cpu.fetch8());
    if (cpu.regs().flagC()) {
        cpu.regs().PC = static_cast<uint16_t>(cpu.regs().PC + offset);
        cpu.internalCycle();
        return 12;
    }
    return 8;
}

// 0x39 ADD HL,SP - 8T
uint32_t op_39(CPU& cpu) {
    alu_add_hl(cpu, cpu.regs().SP);
    cpu.internalCycle();
    return 8;
}

// 0x3A LD A,(HL-) - 8T
uint32_t op_3A(CPU& cpu) {
    uint16_t hl = cpu.regs().getHL();
    cpu.mmu().triggerOAMCorrupt(hl, OAMCorruptType::ReadWrite); // read + IDU same M-cycle
    cpu.regs().A = cpu.mmu().read8(hl);
    cpu.regs().setHL(static_cast<uint16_t>(hl - 1));
    return 8;
}

// 0x3B DEC SP - 8T
uint32_t op_3B(CPU& cpu) {
    uint16_t pre = cpu.regs().SP--;
    cpu.internalCycle();
    cpu.mmu().triggerOAMCorrupt(pre, OAMCorruptType::Write);
    return 8;
}

// 0x3C INC A - 4T
uint32_t op_3C(CPU& cpu) { 
    cpu.regs().A = alu_inc8(cpu, cpu.regs().A); 
    return 4; 
}

// 0x3D DEC A - 4T
uint32_t op_3D(CPU& cpu) { 
    cpu.regs().A = alu_dec8(cpu, cpu.regs().A); 
    return 4; 
}

// 0x3E LD A,n8 - 8T
uint32_t op_3E(CPU& cpu) { 
    cpu.regs().A = cpu.fetch8(); 
    return 8; 
}

// 0x3F CCF - 4T  (complement carry flag)
uint32_t op_3F(CPU& cpu) {
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(false);
    cpu.regs().setFlagC(!cpu.regs().flagC());
    return 4;
}

// ---------------------------------------------------------------------------
// 0x40 - 0x7F  LD r8,r8 block + HALT
// 8-bit register-to-register loads; src/dst encoded in bits [2:0] and [5:3]
// GBZ80 LD r8,r8 grid
// ---------------------------------------------------------------------------

// Generic helper for LD dst,src where src/dst are r8 indices
// (HL) indirect costs an extra 4T (total 8T vs 4T for register-to-register)

static uint32_t ld_r8_r8(CPU& cpu, uint8_t dstIdx, uint8_t srcIdx) {
    uint8_t val;
    uint32_t cycles = 4;

    if (srcIdx == 6)
    {
        val    = cpu.mmu().read8(cpu.regs().getHL());
        cycles = 8;
    }
    else
    {
        val = *cpu.regs().r8Ptr(srcIdx);
    }

    if (dstIdx == 6)
    {
        cpu.mmu().write8(cpu.regs().getHL(), val);
        cycles = 8;
    }
    else
    {
        *cpu.regs().r8Ptr(dstIdx) = val;
    }

    return cycles;
}

// Row 0x40: LD B, r8
uint32_t op_40(CPU& cpu) { return ld_r8_r8(cpu, 0, 0); }
uint32_t op_41(CPU& cpu) { return ld_r8_r8(cpu, 0, 1); }
uint32_t op_42(CPU& cpu) { return ld_r8_r8(cpu, 0, 2); }
uint32_t op_43(CPU& cpu) { return ld_r8_r8(cpu, 0, 3); }
uint32_t op_44(CPU& cpu) { return ld_r8_r8(cpu, 0, 4); }
uint32_t op_45(CPU& cpu) { return ld_r8_r8(cpu, 0, 5); }
uint32_t op_46(CPU& cpu) { return ld_r8_r8(cpu, 0, 6); } // LD B,(HL)
uint32_t op_47(CPU& cpu) { return ld_r8_r8(cpu, 0, 7); }

// Row 0x48: LD C, r8
uint32_t op_48(CPU& cpu) { return ld_r8_r8(cpu, 1, 0); }
uint32_t op_49(CPU& cpu) { return ld_r8_r8(cpu, 1, 1); }
uint32_t op_4A(CPU& cpu) { return ld_r8_r8(cpu, 1, 2); }
uint32_t op_4B(CPU& cpu) { return ld_r8_r8(cpu, 1, 3); }
uint32_t op_4C(CPU& cpu) { return ld_r8_r8(cpu, 1, 4); }
uint32_t op_4D(CPU& cpu) { return ld_r8_r8(cpu, 1, 5); }
uint32_t op_4E(CPU& cpu) { return ld_r8_r8(cpu, 1, 6); } // LD C,(HL)
uint32_t op_4F(CPU& cpu) { return ld_r8_r8(cpu, 1, 7); }

// Row 0x50: LD D, r8
uint32_t op_50(CPU& cpu) { return ld_r8_r8(cpu, 2, 0); }
uint32_t op_51(CPU& cpu) { return ld_r8_r8(cpu, 2, 1); }
uint32_t op_52(CPU& cpu) { return ld_r8_r8(cpu, 2, 2); }
uint32_t op_53(CPU& cpu) { return ld_r8_r8(cpu, 2, 3); }
uint32_t op_54(CPU& cpu) { return ld_r8_r8(cpu, 2, 4); }
uint32_t op_55(CPU& cpu) { return ld_r8_r8(cpu, 2, 5); }
uint32_t op_56(CPU& cpu) { return ld_r8_r8(cpu, 2, 6); } // LD D,(HL)
uint32_t op_57(CPU& cpu) { return ld_r8_r8(cpu, 2, 7); }

// Row 0x58: LD E, r8
uint32_t op_58(CPU& cpu) { return ld_r8_r8(cpu, 3, 0); }
uint32_t op_59(CPU& cpu) { return ld_r8_r8(cpu, 3, 1); }
uint32_t op_5A(CPU& cpu) { return ld_r8_r8(cpu, 3, 2); }
uint32_t op_5B(CPU& cpu) { return ld_r8_r8(cpu, 3, 3); }
uint32_t op_5C(CPU& cpu) { return ld_r8_r8(cpu, 3, 4); }
uint32_t op_5D(CPU& cpu) { return ld_r8_r8(cpu, 3, 5); }
uint32_t op_5E(CPU& cpu) { return ld_r8_r8(cpu, 3, 6); } // LD E,(HL)
uint32_t op_5F(CPU& cpu) { return ld_r8_r8(cpu, 3, 7); }

// Row 0x60: LD H, r8
uint32_t op_60(CPU& cpu) { return ld_r8_r8(cpu, 4, 0); }
uint32_t op_61(CPU& cpu) { return ld_r8_r8(cpu, 4, 1); }
uint32_t op_62(CPU& cpu) { return ld_r8_r8(cpu, 4, 2); }
uint32_t op_63(CPU& cpu) { return ld_r8_r8(cpu, 4, 3); }
uint32_t op_64(CPU& cpu) { return ld_r8_r8(cpu, 4, 4); }
uint32_t op_65(CPU& cpu) { return ld_r8_r8(cpu, 4, 5); }
uint32_t op_66(CPU& cpu) { return ld_r8_r8(cpu, 4, 6); } // LD H,(HL)
uint32_t op_67(CPU& cpu) { return ld_r8_r8(cpu, 4, 7); }

// Row 0x68: LD L, r8
uint32_t op_68(CPU& cpu) { return ld_r8_r8(cpu, 5, 0); }
uint32_t op_69(CPU& cpu) { return ld_r8_r8(cpu, 5, 1); }
uint32_t op_6A(CPU& cpu) { return ld_r8_r8(cpu, 5, 2); }
uint32_t op_6B(CPU& cpu) { return ld_r8_r8(cpu, 5, 3); }
uint32_t op_6C(CPU& cpu) { return ld_r8_r8(cpu, 5, 4); }
uint32_t op_6D(CPU& cpu) { return ld_r8_r8(cpu, 5, 5); }
uint32_t op_6E(CPU& cpu) { return ld_r8_r8(cpu, 5, 6); } // LD L,(HL)
uint32_t op_6F(CPU& cpu) { return ld_r8_r8(cpu, 5, 7); }

// Row 0x70: LD (HL), r8
uint32_t op_70(CPU& cpu) { return ld_r8_r8(cpu, 6, 0); }
uint32_t op_71(CPU& cpu) { return ld_r8_r8(cpu, 6, 1); }
uint32_t op_72(CPU& cpu) { return ld_r8_r8(cpu, 6, 2); }
uint32_t op_73(CPU& cpu) { return ld_r8_r8(cpu, 6, 3); }
uint32_t op_74(CPU& cpu) { return ld_r8_r8(cpu, 6, 4); }
uint32_t op_75(CPU& cpu) { return ld_r8_r8(cpu, 6, 5); }

// 0x76 HALT - 4T
uint32_t op_76(CPU& cpu) {
    uint8_t pending = cpu.mmu().readIF() & cpu.mmu().readIE() & 0x1F;

    if (cpu.imeDelay() > 0 && pending) {
        // EI+HALT with buffered interrupt: PanDocs HALT / EI
        // EI's one-instruction delay has not yet fired (IME still 0), but an interrupt
        // is pending. Apply the EI latch NOW and back up PC to the HALT address so the
        // interrupt dispatch in the next step pushes the HALT address as the return PC.
        // This causes RETI in the ISR to re-enter HALT ("does not exit halt").
        cpu.regs().PC--;        // undo the fetch increment - PC = address of HALT
        cpu.setImeDelay(0);
        cpu.setIME(true);
        // Don't enter halt state; interrupt will fire at start of next step.
    } else if (!cpu.ime() && pending) {
        // HALT bug: IME=0 but interrupt pending - CPU wakes immediately.
        // Next fetch will re-read the same PC (double-read bug). PanDocs HALT
        cpu.setHaltBug(true);
    } else {
        cpu.setHalted(true);
    }
    return 4;
}

uint32_t op_77(CPU& cpu) { return ld_r8_r8(cpu, 6, 7); } // LD (HL),A

// Row 0x78: LD A, r8
uint32_t op_78(CPU& cpu) { return ld_r8_r8(cpu, 7, 0); }
uint32_t op_79(CPU& cpu) { return ld_r8_r8(cpu, 7, 1); }
uint32_t op_7A(CPU& cpu) { return ld_r8_r8(cpu, 7, 2); }
uint32_t op_7B(CPU& cpu) { return ld_r8_r8(cpu, 7, 3); }
uint32_t op_7C(CPU& cpu) { return ld_r8_r8(cpu, 7, 4); }
uint32_t op_7D(CPU& cpu) { return ld_r8_r8(cpu, 7, 5); }
uint32_t op_7E(CPU& cpu) { return ld_r8_r8(cpu, 7, 6); } // LD A,(HL)
uint32_t op_7F(CPU& cpu) { return ld_r8_r8(cpu, 7, 7); }

// ---------------------------------------------------------------------------
// 0x80 - 0xBF  ALU block (ADD/ADC/SUB/SBC/AND/XOR/OR/CP with r8)
// ---------------------------------------------------------------------------

// Generic ALU helper dispatching on row (0=ADD,1=ADC,2=SUB,3=SBC,4=AND,5=XOR,6=OR,7=CP)
static uint32_t alu_r8(CPU& cpu, uint8_t op, uint8_t srcIdx) {
    uint8_t val;
    uint32_t cycles = 4;

    if (srcIdx == 6)
    {
        val    = cpu.mmu().read8(cpu.regs().getHL());
        cycles = 8;
    }
    else
    {
        val = *cpu.regs().r8Ptr(srcIdx);
    }

    switch (op)
    {
        case 0: alu_add(cpu, val, false); break;
        case 1: alu_add(cpu, val, true);  break;
        case 2: alu_sub(cpu, val, false); break;
        case 3: alu_sub(cpu, val, true);  break;
        case 4: alu_and(cpu, val);        break;
        case 5: alu_xor(cpu, val);        break;
        case 6: alu_or(cpu,  val);        break;
        case 7: alu_cp(cpu,  val);        break;
    }
    return cycles;
}

// ADD A, r8 (0x80–0x87)
uint32_t op_80(CPU& cpu) { return alu_r8(cpu, 0, 0); }
uint32_t op_81(CPU& cpu) { return alu_r8(cpu, 0, 1); }
uint32_t op_82(CPU& cpu) { return alu_r8(cpu, 0, 2); }
uint32_t op_83(CPU& cpu) { return alu_r8(cpu, 0, 3); }
uint32_t op_84(CPU& cpu) { return alu_r8(cpu, 0, 4); }
uint32_t op_85(CPU& cpu) { return alu_r8(cpu, 0, 5); }
uint32_t op_86(CPU& cpu) { return alu_r8(cpu, 0, 6); }
uint32_t op_87(CPU& cpu) { return alu_r8(cpu, 0, 7); }

// ADC A, r8 (0x88–0x8F)
uint32_t op_88(CPU& cpu) { return alu_r8(cpu, 1, 0); }
uint32_t op_89(CPU& cpu) { return alu_r8(cpu, 1, 1); }
uint32_t op_8A(CPU& cpu) { return alu_r8(cpu, 1, 2); }
uint32_t op_8B(CPU& cpu) { return alu_r8(cpu, 1, 3); }
uint32_t op_8C(CPU& cpu) { return alu_r8(cpu, 1, 4); }
uint32_t op_8D(CPU& cpu) { return alu_r8(cpu, 1, 5); }
uint32_t op_8E(CPU& cpu) { return alu_r8(cpu, 1, 6); }
uint32_t op_8F(CPU& cpu) { return alu_r8(cpu, 1, 7); }

// SUB A, r8 (0x90–0x97)
uint32_t op_90(CPU& cpu) { return alu_r8(cpu, 2, 0); }
uint32_t op_91(CPU& cpu) { return alu_r8(cpu, 2, 1); }
uint32_t op_92(CPU& cpu) { return alu_r8(cpu, 2, 2); }
uint32_t op_93(CPU& cpu) { return alu_r8(cpu, 2, 3); }
uint32_t op_94(CPU& cpu) { return alu_r8(cpu, 2, 4); }
uint32_t op_95(CPU& cpu) { return alu_r8(cpu, 2, 5); }
uint32_t op_96(CPU& cpu) { return alu_r8(cpu, 2, 6); }
uint32_t op_97(CPU& cpu) { return alu_r8(cpu, 2, 7); }

// SBC A, r8 (0x98–0x9F)
uint32_t op_98(CPU& cpu) { return alu_r8(cpu, 3, 0); }
uint32_t op_99(CPU& cpu) { return alu_r8(cpu, 3, 1); }
uint32_t op_9A(CPU& cpu) { return alu_r8(cpu, 3, 2); }
uint32_t op_9B(CPU& cpu) { return alu_r8(cpu, 3, 3); }
uint32_t op_9C(CPU& cpu) { return alu_r8(cpu, 3, 4); }
uint32_t op_9D(CPU& cpu) { return alu_r8(cpu, 3, 5); }
uint32_t op_9E(CPU& cpu) { return alu_r8(cpu, 3, 6); }
uint32_t op_9F(CPU& cpu) { return alu_r8(cpu, 3, 7); }

// AND A, r8 (0xA0–0xA7)
uint32_t op_A0(CPU& cpu) { return alu_r8(cpu, 4, 0); }
uint32_t op_A1(CPU& cpu) { return alu_r8(cpu, 4, 1); }
uint32_t op_A2(CPU& cpu) { return alu_r8(cpu, 4, 2); }
uint32_t op_A3(CPU& cpu) { return alu_r8(cpu, 4, 3); }
uint32_t op_A4(CPU& cpu) { return alu_r8(cpu, 4, 4); }
uint32_t op_A5(CPU& cpu) { return alu_r8(cpu, 4, 5); }
uint32_t op_A6(CPU& cpu) { return alu_r8(cpu, 4, 6); }
uint32_t op_A7(CPU& cpu) { return alu_r8(cpu, 4, 7); }

// XOR A, r8 (0xA8–0xAF)
uint32_t op_A8(CPU& cpu) { return alu_r8(cpu, 5, 0); }
uint32_t op_A9(CPU& cpu) { return alu_r8(cpu, 5, 1); }
uint32_t op_AA(CPU& cpu) { return alu_r8(cpu, 5, 2); }
uint32_t op_AB(CPU& cpu) { return alu_r8(cpu, 5, 3); }
uint32_t op_AC(CPU& cpu) { return alu_r8(cpu, 5, 4); }
uint32_t op_AD(CPU& cpu) { return alu_r8(cpu, 5, 5); }
uint32_t op_AE(CPU& cpu) { return alu_r8(cpu, 5, 6); }
uint32_t op_AF(CPU& cpu) { return alu_r8(cpu, 5, 7); }

// OR A, r8 (0xB0–0xB7)
uint32_t op_B0(CPU& cpu) { return alu_r8(cpu, 6, 0); }
uint32_t op_B1(CPU& cpu) { return alu_r8(cpu, 6, 1); }
uint32_t op_B2(CPU& cpu) { return alu_r8(cpu, 6, 2); }
uint32_t op_B3(CPU& cpu) { return alu_r8(cpu, 6, 3); }
uint32_t op_B4(CPU& cpu) { return alu_r8(cpu, 6, 4); }
uint32_t op_B5(CPU& cpu) { return alu_r8(cpu, 6, 5); }
uint32_t op_B6(CPU& cpu) { return alu_r8(cpu, 6, 6); }
uint32_t op_B7(CPU& cpu) { return alu_r8(cpu, 6, 7); }

// CP A, r8 (0xB8–0xBF)
uint32_t op_B8(CPU& cpu) { return alu_r8(cpu, 7, 0); }
uint32_t op_B9(CPU& cpu) { return alu_r8(cpu, 7, 1); }
uint32_t op_BA(CPU& cpu) { return alu_r8(cpu, 7, 2); }
uint32_t op_BB(CPU& cpu) { return alu_r8(cpu, 7, 3); }
uint32_t op_BC(CPU& cpu) { return alu_r8(cpu, 7, 4); }
uint32_t op_BD(CPU& cpu) { return alu_r8(cpu, 7, 5); }
uint32_t op_BE(CPU& cpu) { return alu_r8(cpu, 7, 6); }
uint32_t op_BF(CPU& cpu) { return alu_r8(cpu, 7, 7); }

// ---------------------------------------------------------------------------
// 0xC0 - 0xFF  Control flow, stack, I/O, immediate ALU
// ---------------------------------------------------------------------------

// 0xC0 RET NZ - 20T taken / 8T not taken
uint32_t op_C0(CPU& cpu) {
    cpu.internalCycle(); // branch condition eval
    if (!cpu.regs().flagZ()) {
        cpu.regs().PC = stackPop(cpu);
        cpu.internalCycle(); // set PC
        return 20;
    }
    return 8;
}

// 0xC1 POP BC - 12T
uint32_t op_C1(CPU& cpu) {
    cpu.regs().setBC(stackPop(cpu));
    return 12;
}

// 0xC2 JP NZ,n16 - 16T taken / 12T not taken
uint32_t op_C2(CPU& cpu) {
    uint16_t addr = cpu.fetch16();
    if (!cpu.regs().flagZ()) {
        cpu.regs().PC = addr;
        cpu.internalCycle(); // set PC
        return 16;
    }
    return 12;
}

// 0xC3 JP n16 - 16T
uint32_t op_C3(CPU& cpu) {
    cpu.regs().PC = cpu.fetch16();
    cpu.internalCycle(); // set PC
    return 16;
}

// 0xC4 CALL NZ,n16 - 24T taken / 12T not taken
uint32_t op_C4(CPU& cpu) {
    uint16_t addr = cpu.fetch16();
    if (!cpu.regs().flagZ()) {
        cpu.internalCycle(); // internal before push
        stackPush(cpu, cpu.regs().PC);
        cpu.regs().PC = addr;
        return 24;
    }
    return 12;
}

// 0xC5 PUSH BC - 16T
uint32_t op_C5(CPU& cpu) {
    cpu.internalCycle(); // internal before push
    stackPush(cpu, cpu.regs().getBC());
    return 16;
}

// 0xC6 ADD A,n8 - 8T
uint32_t op_C6(CPU& cpu) { 
    alu_add(cpu, cpu.fetch8(), false); 
    return 8; 
}

// 0xC7 RST $00 - 16T
uint32_t op_C7(CPU& cpu) {
    cpu.internalCycle(); // internal before push
    stackPush(cpu, cpu.regs().PC);
    cpu.regs().PC = 0x0000;
    return 16;
}

// 0xC8 RET Z - 20T taken / 8T not taken
uint32_t op_C8(CPU& cpu) {
    cpu.internalCycle(); // branch condition eval
    if (cpu.regs().flagZ()) {
        cpu.regs().PC = stackPop(cpu);
        cpu.internalCycle(); // set PC
        return 20;
    }
    return 8;
}

// 0xC9 RET - 16T
uint32_t op_C9(CPU& cpu) {
    cpu.regs().PC = stackPop(cpu);
    cpu.internalCycle(); // set PC
    return 16;
}

// 0xCA JP Z,n16 - 16T taken / 12T not taken
uint32_t op_CA(CPU& cpu) {
    uint16_t addr = cpu.fetch16();
    if (cpu.regs().flagZ()) {
        cpu.regs().PC = addr;
        cpu.internalCycle(); // set PC
        return 16;
    }
    return 12;
}

// 0xCB PREFIX - dispatches into CB table (fetch sub-opcode + 4T prefix overhead)
uint32_t op_CB(CPU& cpu) {
    uint8_t sub = cpu.fetch8();
    // The 4T prefix fetch is accounted for by the sub-handler returning total cycles
    // including the prefix byte read. Each CB handler already returns the full cost.
    return cpu.dispatchCB(sub);
}

// 0xCC CALL Z,n16 - 24T taken / 12T not taken
uint32_t op_CC(CPU& cpu) {
    uint16_t addr = cpu.fetch16();
    if (cpu.regs().flagZ()) {
        cpu.internalCycle(); // internal before push
        stackPush(cpu, cpu.regs().PC);
        cpu.regs().PC = addr;
        return 24;
    }
    return 12;
}

// 0xCD CALL n16 - 24T
uint32_t op_CD(CPU& cpu) {
    uint16_t addr = cpu.fetch16();
    cpu.internalCycle(); // internal before push
    stackPush(cpu, cpu.regs().PC);
    cpu.regs().PC = addr;
    return 24;
}

// 0xCE ADC A,n8 - 8T
uint32_t op_CE(CPU& cpu) { 
    alu_add(cpu, cpu.fetch8(), true); 
    return 8; 
}

// 0xCF RST $08 - 16T
uint32_t op_CF(CPU& cpu) {
    cpu.internalCycle();
    stackPush(cpu, cpu.regs().PC);
    cpu.regs().PC = 0x0008;
    return 16;
}

// 0xD0 RET NC - 20T taken / 8T not taken
uint32_t op_D0(CPU& cpu) {
    cpu.internalCycle(); // branch condition eval
    if (!cpu.regs().flagC()) {
        cpu.regs().PC = stackPop(cpu);
        cpu.internalCycle(); // set PC
        return 20;
    }
    return 8;
}

// 0xD1 POP DE - 12T
uint32_t op_D1(CPU& cpu) { 
    cpu.regs().setDE(stackPop(cpu)); 
    return 12; 
}

// 0xD2 JP NC,n16 - 16T taken / 12T not taken
uint32_t op_D2(CPU& cpu) {
    uint16_t addr = cpu.fetch16();
    if (!cpu.regs().flagC()) {
        cpu.regs().PC = addr;
        cpu.internalCycle();
        return 16;
    }
    return 12;
}

// 0xD3 - illegal opcode; treated as NOP
uint32_t op_D3(CPU&) { return 4; }

// 0xD4 CALL NC,n16 - 24T taken / 12T not taken
uint32_t op_D4(CPU& cpu) {
    uint16_t addr = cpu.fetch16();
    if (!cpu.regs().flagC()) {
        cpu.internalCycle();
        stackPush(cpu, cpu.regs().PC);
        cpu.regs().PC = addr;
        return 24;
    }
    return 12;
}

// 0xD5 PUSH DE - 16T
uint32_t op_D5(CPU& cpu) {
    cpu.internalCycle();
    stackPush(cpu, cpu.regs().getDE());
    return 16;
}

// 0xD6 SUB A,n8 - 8T
uint32_t op_D6(CPU& cpu) { 
    alu_sub(cpu, cpu.fetch8(), false); 
    return 8; 
}

// 0xD7 RST $10 - 16T
uint32_t op_D7(CPU& cpu) {
    cpu.internalCycle();
    stackPush(cpu, cpu.regs().PC);
    cpu.regs().PC = 0x0010;
    return 16;
}

// 0xD8 RET C - 20T taken / 8T not taken
uint32_t op_D8(CPU& cpu) {
    cpu.internalCycle(); // branch condition eval
    if (cpu.regs().flagC()) {
        cpu.regs().PC = stackPop(cpu);
        cpu.internalCycle(); // set PC
        return 20;
    }
    return 8;
}

// 0xD9 RETI - 16T (RET + enable interrupts immediately, no delay)
uint32_t op_D9(CPU& cpu) {
    cpu.regs().PC = stackPop(cpu);
    cpu.setIME(true);
    cpu.setImeDelay(0);
    cpu.internalCycle(); // set PC
    return 16;
}

// 0xDA JP C,n16 - 16T taken / 12T not taken
uint32_t op_DA(CPU& cpu) {
    uint16_t addr = cpu.fetch16();
    if (cpu.regs().flagC()) {
        cpu.regs().PC = addr;
        cpu.internalCycle();
        return 16;
    }
    return 12;
}

// 0xDB - illegal opcode
uint32_t op_DB(CPU&) { return 4; }

// 0xDC CALL C,n16 - 24T taken / 12T not taken
uint32_t op_DC(CPU& cpu) {
    uint16_t addr = cpu.fetch16();
    if (cpu.regs().flagC()) {
        cpu.internalCycle();
        stackPush(cpu, cpu.regs().PC);
        cpu.regs().PC = addr;
        return 24;
    }
    return 12;
}

// 0xDD - illegal opcode
uint32_t op_DD(CPU&) { return 4; }

// 0xDE SBC A,n8 - 8T
uint32_t op_DE(CPU& cpu) { 
    alu_sub(cpu, cpu.fetch8(), true); 
    return 8; 
}

// 0xDF RST $18 - 16T
uint32_t op_DF(CPU& cpu) {
    cpu.internalCycle();
    stackPush(cpu, cpu.regs().PC);
    cpu.regs().PC = 0x0018;
    return 16;
}

// 0xE0 LDH (n8),A - 12T  (write A to 0xFF00+n8)
uint32_t op_E0(CPU& cpu) {
    uint16_t addr = static_cast<uint16_t>(0xFF00 | cpu.fetch8());
    cpu.mmu().write8(addr, cpu.regs().A);
    return 12;
}

// 0xE1 POP HL - 12T
uint32_t op_E1(CPU& cpu) { 
    cpu.regs().setHL(stackPop(cpu)); 
    return 12; 
}

// 0xE2 LDH (C),A - 8T  (write A to 0xFF00+C)
uint32_t op_E2(CPU& cpu) {
    cpu.mmu().write8(static_cast<uint16_t>(0xFF00 | cpu.regs().C), cpu.regs().A);
    return 8;
}

// 0xE3 - illegal opcode
uint32_t op_E3(CPU&) { return 4; }

// 0xE4 - illegal opcode
uint32_t op_E4(CPU&) { return 4; }

// 0xE5 PUSH HL - 16T
uint32_t op_E5(CPU& cpu) {
    cpu.internalCycle();
    stackPush(cpu, cpu.regs().getHL());
    return 16;
}

// 0xE6 AND A,n8 - 8T
uint32_t op_E6(CPU& cpu) { 
    alu_and(cpu, cpu.fetch8()); 
    return 8; 
}

// 0xE7 RST $20 - 16T
uint32_t op_E7(CPU& cpu) {
    cpu.internalCycle();
    stackPush(cpu, cpu.regs().PC);
    cpu.regs().PC = 0x0020;
    return 16;
}

// 0xE8 ADD SP,e8 - 16T
// GBZ80 - Z and N cleared; H and C set based on low byte arithmetic
uint32_t op_E8(CPU& cpu) {
    uint8_t  ue = cpu.fetch8(); // treat as signed offset but use unsigned for flag math
    uint16_t sp = cpu.regs().SP;
    int      r  = static_cast<int>(sp) + static_cast<int>(static_cast<int8_t>(ue));

    cpu.regs().setFlagZ(false);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(((sp & 0x0F) + (ue & 0x0F)) > 0x0F);
    cpu.regs().setFlagC(((sp & 0xFF) + ue) > 0xFF);
    cpu.regs().SP = static_cast<uint16_t>(r);
    cpu.internalCycle(); // 2 internal M-cycles
    cpu.internalCycle();
    return 16;
}

// 0xE9 JP HL - 4T
uint32_t op_E9(CPU& cpu) { 
    cpu.regs().PC = cpu.regs().getHL(); 
    return 4; 
}

// 0xEA LD (n16),A - 16T
uint32_t op_EA(CPU& cpu) {
    cpu.mmu().write8(cpu.fetch16(), cpu.regs().A);
    return 16;
}

// 0xEB - illegal opcode
uint32_t op_EB(CPU&) { return 4; }

// 0xEC - illegal opcode
uint32_t op_EC(CPU&) { return 4; }

// 0xED - illegal opcode
uint32_t op_ED(CPU&) { return 4; }

// 0xEE XOR A,n8 - 8T
uint32_t op_EE(CPU& cpu) { 
    alu_xor(cpu, cpu.fetch8()); 
    return 8; 
}

// 0xEF RST $28 - 16T
uint32_t op_EF(CPU& cpu) {
    cpu.internalCycle();
    stackPush(cpu, cpu.regs().PC);
    cpu.regs().PC = 0x0028;
    return 16;
}

// 0xF0 LDH A,(n8) - 12T  (read from 0xFF00+n8 into A)
uint32_t op_F0(CPU& cpu) {
    uint16_t addr = static_cast<uint16_t>(0xFF00 | cpu.fetch8());
    cpu.regs().A  = cpu.mmu().read8(addr);
    return 12;
}

// 0xF1 POP AF - 12T
uint32_t op_F1(CPU& cpu) { 
    cpu.regs().setAF(stackPop(cpu)); 
    return 12; 
}

// 0xF2 LDH A,(C) - 8T  (read from 0xFF00+C into A)
uint32_t op_F2(CPU& cpu) {
    cpu.regs().A = cpu.mmu().read8(static_cast<uint16_t>(0xFF00 | cpu.regs().C));
    return 8;
}

// 0xF3 DI - 4T  (disable interrupts)
uint32_t op_F3(CPU& cpu) {
    cpu.setIME(false);
    cpu.setImeDelay(0);  // cancel any pending EI
    return 4;
}

// 0xF4 - illegal opcode
uint32_t op_F4(CPU&) { return 4; }

// 0xF5 PUSH AF - 16T
uint32_t op_F5(CPU& cpu) {
    cpu.internalCycle();
    stackPush(cpu, cpu.regs().getAF());
    return 16;
}

// 0xF6 OR A,n8 - 8T
uint32_t op_F6(CPU& cpu) { 
    alu_or(cpu, cpu.fetch8()); 
    return 8; 
}

// 0xF7 RST $30 - 16T
uint32_t op_F7(CPU& cpu) {
    cpu.internalCycle();
    stackPush(cpu, cpu.regs().PC);
    cpu.regs().PC = 0x0030;
    return 16;
}

// 0xF8 LD HL,SP+e8 - 12T
// GBz80 - same flag behavor as ADD SP,e8
uint32_t op_F8(CPU& cpu) {
    uint8_t  ue = cpu.fetch8(); // treat as signed offset but use unsigned for flag math
    uint16_t sp = cpu.regs().SP;
    int      r  = static_cast<int>(sp) + static_cast<int>(static_cast<int8_t>(ue));

    cpu.regs().setFlagZ(false);
    cpu.regs().setFlagN(false);
    cpu.regs().setFlagH(((sp & 0x0F) + (ue & 0x0F)) > 0x0F);
    cpu.regs().setFlagC(((sp & 0xFF) + ue) > 0xFF);
    cpu.regs().setHL(static_cast<uint16_t>(r));
    cpu.internalCycle();
    return 12;
}

// 0xF9 LD SP,HL - 8T
uint32_t op_F9(CPU& cpu) {
    cpu.regs().SP = cpu.regs().getHL();
    cpu.internalCycle();
    return 8;
}

// 0xFA LD A,(n16) - 16T
uint32_t op_FA(CPU& cpu) {
    cpu.regs().A = cpu.mmu().read8(cpu.fetch16());
    return 16;
}

// 0xFB EI - 4T  (enable interrupts with one-instruction delay)
// GBZ80 - IME takes effect after the following instruction (2-step counter).
// Only starts the counter if not already counting, so chained EIs don't extend
// the delay window. PanDocs Interrupts
uint32_t op_FB(CPU& cpu) {
    if (cpu.imeDelay() == 0)
        cpu.setImeDelay(2);
    return 4;
}

// 0xFC - illegal opcode
uint32_t op_FC(CPU&) { return 4; }

// 0xFD - illegal opcode
uint32_t op_FD(CPU&) { return 4; }

// 0xFE CP A,n8 - 8T
uint32_t op_FE(CPU& cpu) { 
    alu_cp(cpu, cpu.fetch8()); 
    return 8; 
}

// 0xFF RST $38 - 16T
uint32_t op_FF(CPU& cpu) {
    cpu.internalCycle();
    stackPush(cpu, cpu.regs().PC);
    cpu.regs().PC = 0x0038;
    return 16;
}

}
