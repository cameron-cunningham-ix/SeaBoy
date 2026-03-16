#include "CPU.hpp"
#include "MMU.hpp"

// GBDocs CPU instruction set
// GBCTR Sharp SM83 instruction set

// Interrupt vector addresses - PanDocs.9
static constexpr uint16_t IV_VBLANK  = 0x0040;
static constexpr uint16_t IV_STAT    = 0x0048;
static constexpr uint16_t IV_TIMER   = 0x0050;
static constexpr uint16_t IV_SERIAL  = 0x0058;
static constexpr uint16_t IV_JOYPAD  = 0x0060;

// Interrupt bit masks in IF/IE (bits 0–4)
static constexpr uint8_t INT_VBLANK  = 0x01;
static constexpr uint8_t INT_STAT    = 0x02;
static constexpr uint8_t INT_TIMER   = 0x04;
static constexpr uint8_t INT_SERIAL  = 0x08;
static constexpr uint8_t INT_JOYPAD  = 0x10;

namespace SeaBoy
{
    // ------------------------------------------------------------------
    // Opcode handler declarations
    // Defined in opcodes/Opcodes.cpp and opcodes/OpcodesCB.cpp
    // ------------------------------------------------------------------

    // Forward-declare all 256 unprefixed handlers
    // Named op_XX where XX is the hex opcode
    namespace Opcodes
    {
        uint32_t op_00(CPU&); uint32_t op_01(CPU&); uint32_t op_02(CPU&); uint32_t op_03(CPU&);
        uint32_t op_04(CPU&); uint32_t op_05(CPU&); uint32_t op_06(CPU&); uint32_t op_07(CPU&);
        uint32_t op_08(CPU&); uint32_t op_09(CPU&); uint32_t op_0A(CPU&); uint32_t op_0B(CPU&);
        uint32_t op_0C(CPU&); uint32_t op_0D(CPU&); uint32_t op_0E(CPU&); uint32_t op_0F(CPU&);

        uint32_t op_10(CPU&); uint32_t op_11(CPU&); uint32_t op_12(CPU&); uint32_t op_13(CPU&);
        uint32_t op_14(CPU&); uint32_t op_15(CPU&); uint32_t op_16(CPU&); uint32_t op_17(CPU&);
        uint32_t op_18(CPU&); uint32_t op_19(CPU&); uint32_t op_1A(CPU&); uint32_t op_1B(CPU&);
        uint32_t op_1C(CPU&); uint32_t op_1D(CPU&); uint32_t op_1E(CPU&); uint32_t op_1F(CPU&);

        uint32_t op_20(CPU&); uint32_t op_21(CPU&); uint32_t op_22(CPU&); uint32_t op_23(CPU&);
        uint32_t op_24(CPU&); uint32_t op_25(CPU&); uint32_t op_26(CPU&); uint32_t op_27(CPU&);
        uint32_t op_28(CPU&); uint32_t op_29(CPU&); uint32_t op_2A(CPU&); uint32_t op_2B(CPU&);
        uint32_t op_2C(CPU&); uint32_t op_2D(CPU&); uint32_t op_2E(CPU&); uint32_t op_2F(CPU&);

        uint32_t op_30(CPU&); uint32_t op_31(CPU&); uint32_t op_32(CPU&); uint32_t op_33(CPU&);
        uint32_t op_34(CPU&); uint32_t op_35(CPU&); uint32_t op_36(CPU&); uint32_t op_37(CPU&);
        uint32_t op_38(CPU&); uint32_t op_39(CPU&); uint32_t op_3A(CPU&); uint32_t op_3B(CPU&);
        uint32_t op_3C(CPU&); uint32_t op_3D(CPU&); uint32_t op_3E(CPU&); uint32_t op_3F(CPU&);

        uint32_t op_40(CPU&); uint32_t op_41(CPU&); uint32_t op_42(CPU&); uint32_t op_43(CPU&);
        uint32_t op_44(CPU&); uint32_t op_45(CPU&); uint32_t op_46(CPU&); uint32_t op_47(CPU&);
        uint32_t op_48(CPU&); uint32_t op_49(CPU&); uint32_t op_4A(CPU&); uint32_t op_4B(CPU&);
        uint32_t op_4C(CPU&); uint32_t op_4D(CPU&); uint32_t op_4E(CPU&); uint32_t op_4F(CPU&);

        uint32_t op_50(CPU&); uint32_t op_51(CPU&); uint32_t op_52(CPU&); uint32_t op_53(CPU&);
        uint32_t op_54(CPU&); uint32_t op_55(CPU&); uint32_t op_56(CPU&); uint32_t op_57(CPU&);
        uint32_t op_58(CPU&); uint32_t op_59(CPU&); uint32_t op_5A(CPU&); uint32_t op_5B(CPU&);
        uint32_t op_5C(CPU&); uint32_t op_5D(CPU&); uint32_t op_5E(CPU&); uint32_t op_5F(CPU&);

        uint32_t op_60(CPU&); uint32_t op_61(CPU&); uint32_t op_62(CPU&); uint32_t op_63(CPU&);
        uint32_t op_64(CPU&); uint32_t op_65(CPU&); uint32_t op_66(CPU&); uint32_t op_67(CPU&);
        uint32_t op_68(CPU&); uint32_t op_69(CPU&); uint32_t op_6A(CPU&); uint32_t op_6B(CPU&);
        uint32_t op_6C(CPU&); uint32_t op_6D(CPU&); uint32_t op_6E(CPU&); uint32_t op_6F(CPU&);

        uint32_t op_70(CPU&); uint32_t op_71(CPU&); uint32_t op_72(CPU&); uint32_t op_73(CPU&);
        uint32_t op_74(CPU&); uint32_t op_75(CPU&); uint32_t op_76(CPU&); uint32_t op_77(CPU&);
        uint32_t op_78(CPU&); uint32_t op_79(CPU&); uint32_t op_7A(CPU&); uint32_t op_7B(CPU&);
        uint32_t op_7C(CPU&); uint32_t op_7D(CPU&); uint32_t op_7E(CPU&); uint32_t op_7F(CPU&);

        uint32_t op_80(CPU&); uint32_t op_81(CPU&); uint32_t op_82(CPU&); uint32_t op_83(CPU&);
        uint32_t op_84(CPU&); uint32_t op_85(CPU&); uint32_t op_86(CPU&); uint32_t op_87(CPU&);
        uint32_t op_88(CPU&); uint32_t op_89(CPU&); uint32_t op_8A(CPU&); uint32_t op_8B(CPU&);
        uint32_t op_8C(CPU&); uint32_t op_8D(CPU&); uint32_t op_8E(CPU&); uint32_t op_8F(CPU&);

        uint32_t op_90(CPU&); uint32_t op_91(CPU&); uint32_t op_92(CPU&); uint32_t op_93(CPU&);
        uint32_t op_94(CPU&); uint32_t op_95(CPU&); uint32_t op_96(CPU&); uint32_t op_97(CPU&);
        uint32_t op_98(CPU&); uint32_t op_99(CPU&); uint32_t op_9A(CPU&); uint32_t op_9B(CPU&);
        uint32_t op_9C(CPU&); uint32_t op_9D(CPU&); uint32_t op_9E(CPU&); uint32_t op_9F(CPU&);

        uint32_t op_A0(CPU&); uint32_t op_A1(CPU&); uint32_t op_A2(CPU&); uint32_t op_A3(CPU&);
        uint32_t op_A4(CPU&); uint32_t op_A5(CPU&); uint32_t op_A6(CPU&); uint32_t op_A7(CPU&);
        uint32_t op_A8(CPU&); uint32_t op_A9(CPU&); uint32_t op_AA(CPU&); uint32_t op_AB(CPU&);
        uint32_t op_AC(CPU&); uint32_t op_AD(CPU&); uint32_t op_AE(CPU&); uint32_t op_AF(CPU&);

        uint32_t op_B0(CPU&); uint32_t op_B1(CPU&); uint32_t op_B2(CPU&); uint32_t op_B3(CPU&);
        uint32_t op_B4(CPU&); uint32_t op_B5(CPU&); uint32_t op_B6(CPU&); uint32_t op_B7(CPU&);
        uint32_t op_B8(CPU&); uint32_t op_B9(CPU&); uint32_t op_BA(CPU&); uint32_t op_BB(CPU&);
        uint32_t op_BC(CPU&); uint32_t op_BD(CPU&); uint32_t op_BE(CPU&); uint32_t op_BF(CPU&);

        uint32_t op_C0(CPU&); uint32_t op_C1(CPU&); uint32_t op_C2(CPU&); uint32_t op_C3(CPU&);
        uint32_t op_C4(CPU&); uint32_t op_C5(CPU&); uint32_t op_C6(CPU&); uint32_t op_C7(CPU&);
        uint32_t op_C8(CPU&); uint32_t op_C9(CPU&); uint32_t op_CA(CPU&); uint32_t op_CB(CPU&);
        uint32_t op_CC(CPU&); uint32_t op_CD(CPU&); uint32_t op_CE(CPU&); uint32_t op_CF(CPU&);

        uint32_t op_D0(CPU&); uint32_t op_D1(CPU&); uint32_t op_D2(CPU&); uint32_t op_D3(CPU&);
        uint32_t op_D4(CPU&); uint32_t op_D5(CPU&); uint32_t op_D6(CPU&); uint32_t op_D7(CPU&);
        uint32_t op_D8(CPU&); uint32_t op_D9(CPU&); uint32_t op_DA(CPU&); uint32_t op_DB(CPU&);
        uint32_t op_DC(CPU&); uint32_t op_DD(CPU&); uint32_t op_DE(CPU&); uint32_t op_DF(CPU&);

        uint32_t op_E0(CPU&); uint32_t op_E1(CPU&); uint32_t op_E2(CPU&); uint32_t op_E3(CPU&);
        uint32_t op_E4(CPU&); uint32_t op_E5(CPU&); uint32_t op_E6(CPU&); uint32_t op_E7(CPU&);
        uint32_t op_E8(CPU&); uint32_t op_E9(CPU&); uint32_t op_EA(CPU&); uint32_t op_EB(CPU&);
        uint32_t op_EC(CPU&); uint32_t op_ED(CPU&); uint32_t op_EE(CPU&); uint32_t op_EF(CPU&);

        uint32_t op_F0(CPU&); uint32_t op_F1(CPU&); uint32_t op_F2(CPU&); uint32_t op_F3(CPU&);
        uint32_t op_F4(CPU&); uint32_t op_F5(CPU&); uint32_t op_F6(CPU&); uint32_t op_F7(CPU&);
        uint32_t op_F8(CPU&); uint32_t op_F9(CPU&); uint32_t op_FA(CPU&); uint32_t op_FB(CPU&);
        uint32_t op_FC(CPU&); uint32_t op_FD(CPU&); uint32_t op_FE(CPU&); uint32_t op_FF(CPU&);
    }

    namespace OpcodesCB
    {
        uint32_t cb_00(CPU&); uint32_t cb_01(CPU&); uint32_t cb_02(CPU&); uint32_t cb_03(CPU&);
        uint32_t cb_04(CPU&); uint32_t cb_05(CPU&); uint32_t cb_06(CPU&); uint32_t cb_07(CPU&);
        uint32_t cb_08(CPU&); uint32_t cb_09(CPU&); uint32_t cb_0A(CPU&); uint32_t cb_0B(CPU&);
        uint32_t cb_0C(CPU&); uint32_t cb_0D(CPU&); uint32_t cb_0E(CPU&); uint32_t cb_0F(CPU&);

        uint32_t cb_10(CPU&); uint32_t cb_11(CPU&); uint32_t cb_12(CPU&); uint32_t cb_13(CPU&);
        uint32_t cb_14(CPU&); uint32_t cb_15(CPU&); uint32_t cb_16(CPU&); uint32_t cb_17(CPU&);
        uint32_t cb_18(CPU&); uint32_t cb_19(CPU&); uint32_t cb_1A(CPU&); uint32_t cb_1B(CPU&);
        uint32_t cb_1C(CPU&); uint32_t cb_1D(CPU&); uint32_t cb_1E(CPU&); uint32_t cb_1F(CPU&);

        uint32_t cb_20(CPU&); uint32_t cb_21(CPU&); uint32_t cb_22(CPU&); uint32_t cb_23(CPU&);
        uint32_t cb_24(CPU&); uint32_t cb_25(CPU&); uint32_t cb_26(CPU&); uint32_t cb_27(CPU&);
        uint32_t cb_28(CPU&); uint32_t cb_29(CPU&); uint32_t cb_2A(CPU&); uint32_t cb_2B(CPU&);
        uint32_t cb_2C(CPU&); uint32_t cb_2D(CPU&); uint32_t cb_2E(CPU&); uint32_t cb_2F(CPU&);

        uint32_t cb_30(CPU&); uint32_t cb_31(CPU&); uint32_t cb_32(CPU&); uint32_t cb_33(CPU&);
        uint32_t cb_34(CPU&); uint32_t cb_35(CPU&); uint32_t cb_36(CPU&); uint32_t cb_37(CPU&);
        uint32_t cb_38(CPU&); uint32_t cb_39(CPU&); uint32_t cb_3A(CPU&); uint32_t cb_3B(CPU&);
        uint32_t cb_3C(CPU&); uint32_t cb_3D(CPU&); uint32_t cb_3E(CPU&); uint32_t cb_3F(CPU&);

        uint32_t cb_40(CPU&); uint32_t cb_41(CPU&); uint32_t cb_42(CPU&); uint32_t cb_43(CPU&);
        uint32_t cb_44(CPU&); uint32_t cb_45(CPU&); uint32_t cb_46(CPU&); uint32_t cb_47(CPU&);
        uint32_t cb_48(CPU&); uint32_t cb_49(CPU&); uint32_t cb_4A(CPU&); uint32_t cb_4B(CPU&);
        uint32_t cb_4C(CPU&); uint32_t cb_4D(CPU&); uint32_t cb_4E(CPU&); uint32_t cb_4F(CPU&);

        uint32_t cb_50(CPU&); uint32_t cb_51(CPU&); uint32_t cb_52(CPU&); uint32_t cb_53(CPU&);
        uint32_t cb_54(CPU&); uint32_t cb_55(CPU&); uint32_t cb_56(CPU&); uint32_t cb_57(CPU&);
        uint32_t cb_58(CPU&); uint32_t cb_59(CPU&); uint32_t cb_5A(CPU&); uint32_t cb_5B(CPU&);
        uint32_t cb_5C(CPU&); uint32_t cb_5D(CPU&); uint32_t cb_5E(CPU&); uint32_t cb_5F(CPU&);

        uint32_t cb_60(CPU&); uint32_t cb_61(CPU&); uint32_t cb_62(CPU&); uint32_t cb_63(CPU&);
        uint32_t cb_64(CPU&); uint32_t cb_65(CPU&); uint32_t cb_66(CPU&); uint32_t cb_67(CPU&);
        uint32_t cb_68(CPU&); uint32_t cb_69(CPU&); uint32_t cb_6A(CPU&); uint32_t cb_6B(CPU&);
        uint32_t cb_6C(CPU&); uint32_t cb_6D(CPU&); uint32_t cb_6E(CPU&); uint32_t cb_6F(CPU&);

        uint32_t cb_70(CPU&); uint32_t cb_71(CPU&); uint32_t cb_72(CPU&); uint32_t cb_73(CPU&);
        uint32_t cb_74(CPU&); uint32_t cb_75(CPU&); uint32_t cb_76(CPU&); uint32_t cb_77(CPU&);
        uint32_t cb_78(CPU&); uint32_t cb_79(CPU&); uint32_t cb_7A(CPU&); uint32_t cb_7B(CPU&);
        uint32_t cb_7C(CPU&); uint32_t cb_7D(CPU&); uint32_t cb_7E(CPU&); uint32_t cb_7F(CPU&);

        uint32_t cb_80(CPU&); uint32_t cb_81(CPU&); uint32_t cb_82(CPU&); uint32_t cb_83(CPU&);
        uint32_t cb_84(CPU&); uint32_t cb_85(CPU&); uint32_t cb_86(CPU&); uint32_t cb_87(CPU&);
        uint32_t cb_88(CPU&); uint32_t cb_89(CPU&); uint32_t cb_8A(CPU&); uint32_t cb_8B(CPU&);
        uint32_t cb_8C(CPU&); uint32_t cb_8D(CPU&); uint32_t cb_8E(CPU&); uint32_t cb_8F(CPU&);

        uint32_t cb_90(CPU&); uint32_t cb_91(CPU&); uint32_t cb_92(CPU&); uint32_t cb_93(CPU&);
        uint32_t cb_94(CPU&); uint32_t cb_95(CPU&); uint32_t cb_96(CPU&); uint32_t cb_97(CPU&);
        uint32_t cb_98(CPU&); uint32_t cb_99(CPU&); uint32_t cb_9A(CPU&); uint32_t cb_9B(CPU&);
        uint32_t cb_9C(CPU&); uint32_t cb_9D(CPU&); uint32_t cb_9E(CPU&); uint32_t cb_9F(CPU&);

        uint32_t cb_A0(CPU&); uint32_t cb_A1(CPU&); uint32_t cb_A2(CPU&); uint32_t cb_A3(CPU&);
        uint32_t cb_A4(CPU&); uint32_t cb_A5(CPU&); uint32_t cb_A6(CPU&); uint32_t cb_A7(CPU&);
        uint32_t cb_A8(CPU&); uint32_t cb_A9(CPU&); uint32_t cb_AA(CPU&); uint32_t cb_AB(CPU&);
        uint32_t cb_AC(CPU&); uint32_t cb_AD(CPU&); uint32_t cb_AE(CPU&); uint32_t cb_AF(CPU&);

        uint32_t cb_B0(CPU&); uint32_t cb_B1(CPU&); uint32_t cb_B2(CPU&); uint32_t cb_B3(CPU&);
        uint32_t cb_B4(CPU&); uint32_t cb_B5(CPU&); uint32_t cb_B6(CPU&); uint32_t cb_B7(CPU&);
        uint32_t cb_B8(CPU&); uint32_t cb_B9(CPU&); uint32_t cb_BA(CPU&); uint32_t cb_BB(CPU&);
        uint32_t cb_BC(CPU&); uint32_t cb_BD(CPU&); uint32_t cb_BE(CPU&); uint32_t cb_BF(CPU&);

        uint32_t cb_C0(CPU&); uint32_t cb_C1(CPU&); uint32_t cb_C2(CPU&); uint32_t cb_C3(CPU&);
        uint32_t cb_C4(CPU&); uint32_t cb_C5(CPU&); uint32_t cb_C6(CPU&); uint32_t cb_C7(CPU&);
        uint32_t cb_C8(CPU&); uint32_t cb_C9(CPU&); uint32_t cb_CA(CPU&); uint32_t cb_CB(CPU&);
        uint32_t cb_CC(CPU&); uint32_t cb_CD(CPU&); uint32_t cb_CE(CPU&); uint32_t cb_CF(CPU&);

        uint32_t cb_D0(CPU&); uint32_t cb_D1(CPU&); uint32_t cb_D2(CPU&); uint32_t cb_D3(CPU&);
        uint32_t cb_D4(CPU&); uint32_t cb_D5(CPU&); uint32_t cb_D6(CPU&); uint32_t cb_D7(CPU&);
        uint32_t cb_D8(CPU&); uint32_t cb_D9(CPU&); uint32_t cb_DA(CPU&); uint32_t cb_DB(CPU&);
        uint32_t cb_DC(CPU&); uint32_t cb_DD(CPU&); uint32_t cb_DE(CPU&); uint32_t cb_DF(CPU&);

        uint32_t cb_E0(CPU&); uint32_t cb_E1(CPU&); uint32_t cb_E2(CPU&); uint32_t cb_E3(CPU&);
        uint32_t cb_E4(CPU&); uint32_t cb_E5(CPU&); uint32_t cb_E6(CPU&); uint32_t cb_E7(CPU&);
        uint32_t cb_E8(CPU&); uint32_t cb_E9(CPU&); uint32_t cb_EA(CPU&); uint32_t cb_EB(CPU&);
        uint32_t cb_EC(CPU&); uint32_t cb_ED(CPU&); uint32_t cb_EE(CPU&); uint32_t cb_EF(CPU&);

        uint32_t cb_F0(CPU&); uint32_t cb_F1(CPU&); uint32_t cb_F2(CPU&); uint32_t cb_F3(CPU&);
        uint32_t cb_F4(CPU&); uint32_t cb_F5(CPU&); uint32_t cb_F6(CPU&); uint32_t cb_F7(CPU&);
        uint32_t cb_F8(CPU&); uint32_t cb_F9(CPU&); uint32_t cb_FA(CPU&); uint32_t cb_FB(CPU&);
        uint32_t cb_FC(CPU&); uint32_t cb_FD(CPU&); uint32_t cb_FE(CPU&); uint32_t cb_FF(CPU&);
    }

    // ------------------------------------------------------------------
    // Opcode dispatch table definitions
    // ------------------------------------------------------------------

    using namespace Opcodes;
    using namespace OpcodesCB;

    const std::array<CPU::OpcodeHandler, 256> CPU::kOpcodes = {
        op_00, op_01, op_02, op_03, op_04, op_05, op_06, op_07,
        op_08, op_09, op_0A, op_0B, op_0C, op_0D, op_0E, op_0F,
        op_10, op_11, op_12, op_13, op_14, op_15, op_16, op_17,
        op_18, op_19, op_1A, op_1B, op_1C, op_1D, op_1E, op_1F,
        op_20, op_21, op_22, op_23, op_24, op_25, op_26, op_27,
        op_28, op_29, op_2A, op_2B, op_2C, op_2D, op_2E, op_2F,
        op_30, op_31, op_32, op_33, op_34, op_35, op_36, op_37,
        op_38, op_39, op_3A, op_3B, op_3C, op_3D, op_3E, op_3F,
        op_40, op_41, op_42, op_43, op_44, op_45, op_46, op_47,
        op_48, op_49, op_4A, op_4B, op_4C, op_4D, op_4E, op_4F,
        op_50, op_51, op_52, op_53, op_54, op_55, op_56, op_57,
        op_58, op_59, op_5A, op_5B, op_5C, op_5D, op_5E, op_5F,
        op_60, op_61, op_62, op_63, op_64, op_65, op_66, op_67,
        op_68, op_69, op_6A, op_6B, op_6C, op_6D, op_6E, op_6F,
        op_70, op_71, op_72, op_73, op_74, op_75, op_76, op_77,
        op_78, op_79, op_7A, op_7B, op_7C, op_7D, op_7E, op_7F,
        op_80, op_81, op_82, op_83, op_84, op_85, op_86, op_87,
        op_88, op_89, op_8A, op_8B, op_8C, op_8D, op_8E, op_8F,
        op_90, op_91, op_92, op_93, op_94, op_95, op_96, op_97,
        op_98, op_99, op_9A, op_9B, op_9C, op_9D, op_9E, op_9F,
        op_A0, op_A1, op_A2, op_A3, op_A4, op_A5, op_A6, op_A7,
        op_A8, op_A9, op_AA, op_AB, op_AC, op_AD, op_AE, op_AF,
        op_B0, op_B1, op_B2, op_B3, op_B4, op_B5, op_B6, op_B7,
        op_B8, op_B9, op_BA, op_BB, op_BC, op_BD, op_BE, op_BF,
        op_C0, op_C1, op_C2, op_C3, op_C4, op_C5, op_C6, op_C7,
        op_C8, op_C9, op_CA, op_CB, op_CC, op_CD, op_CE, op_CF,
        op_D0, op_D1, op_D2, op_D3, op_D4, op_D5, op_D6, op_D7,
        op_D8, op_D9, op_DA, op_DB, op_DC, op_DD, op_DE, op_DF,
        op_E0, op_E1, op_E2, op_E3, op_E4, op_E5, op_E6, op_E7,
        op_E8, op_E9, op_EA, op_EB, op_EC, op_ED, op_EE, op_EF,
        op_F0, op_F1, op_F2, op_F3, op_F4, op_F5, op_F6, op_F7,
        op_F8, op_F9, op_FA, op_FB, op_FC, op_FD, op_FE, op_FF,
    };

    const std::array<CPU::OpcodeHandler, 256> CPU::kCBOpcodes = {
        cb_00, cb_01, cb_02, cb_03, cb_04, cb_05, cb_06, cb_07,
        cb_08, cb_09, cb_0A, cb_0B, cb_0C, cb_0D, cb_0E, cb_0F,
        cb_10, cb_11, cb_12, cb_13, cb_14, cb_15, cb_16, cb_17,
        cb_18, cb_19, cb_1A, cb_1B, cb_1C, cb_1D, cb_1E, cb_1F,
        cb_20, cb_21, cb_22, cb_23, cb_24, cb_25, cb_26, cb_27,
        cb_28, cb_29, cb_2A, cb_2B, cb_2C, cb_2D, cb_2E, cb_2F,
        cb_30, cb_31, cb_32, cb_33, cb_34, cb_35, cb_36, cb_37,
        cb_38, cb_39, cb_3A, cb_3B, cb_3C, cb_3D, cb_3E, cb_3F,
        cb_40, cb_41, cb_42, cb_43, cb_44, cb_45, cb_46, cb_47,
        cb_48, cb_49, cb_4A, cb_4B, cb_4C, cb_4D, cb_4E, cb_4F,
        cb_50, cb_51, cb_52, cb_53, cb_54, cb_55, cb_56, cb_57,
        cb_58, cb_59, cb_5A, cb_5B, cb_5C, cb_5D, cb_5E, cb_5F,
        cb_60, cb_61, cb_62, cb_63, cb_64, cb_65, cb_66, cb_67,
        cb_68, cb_69, cb_6A, cb_6B, cb_6C, cb_6D, cb_6E, cb_6F,
        cb_70, cb_71, cb_72, cb_73, cb_74, cb_75, cb_76, cb_77,
        cb_78, cb_79, cb_7A, cb_7B, cb_7C, cb_7D, cb_7E, cb_7F,
        cb_80, cb_81, cb_82, cb_83, cb_84, cb_85, cb_86, cb_87,
        cb_88, cb_89, cb_8A, cb_8B, cb_8C, cb_8D, cb_8E, cb_8F,
        cb_90, cb_91, cb_92, cb_93, cb_94, cb_95, cb_96, cb_97,
        cb_98, cb_99, cb_9A, cb_9B, cb_9C, cb_9D, cb_9E, cb_9F,
        cb_A0, cb_A1, cb_A2, cb_A3, cb_A4, cb_A5, cb_A6, cb_A7,
        cb_A8, cb_A9, cb_AA, cb_AB, cb_AC, cb_AD, cb_AE, cb_AF,
        cb_B0, cb_B1, cb_B2, cb_B3, cb_B4, cb_B5, cb_B6, cb_B7,
        cb_B8, cb_B9, cb_BA, cb_BB, cb_BC, cb_BD, cb_BE, cb_BF,
        cb_C0, cb_C1, cb_C2, cb_C3, cb_C4, cb_C5, cb_C6, cb_C7,
        cb_C8, cb_C9, cb_CA, cb_CB, cb_CC, cb_CD, cb_CE, cb_CF,
        cb_D0, cb_D1, cb_D2, cb_D3, cb_D4, cb_D5, cb_D6, cb_D7,
        cb_D8, cb_D9, cb_DA, cb_DB, cb_DC, cb_DD, cb_DE, cb_DF,
        cb_E0, cb_E1, cb_E2, cb_E3, cb_E4, cb_E5, cb_E6, cb_E7,
        cb_E8, cb_E9, cb_EA, cb_EB, cb_EC, cb_ED, cb_EE, cb_EF,
        cb_F0, cb_F1, cb_F2, cb_F3, cb_F4, cb_F5, cb_F6, cb_F7,
        cb_F8, cb_F9, cb_FA, cb_FB, cb_FC, cb_FD, cb_FE, cb_FF,
    };

    // ------------------------------------------------------------------
    // CPU implementation
    // ------------------------------------------------------------------

    CPU::CPU(MMU& mmu) : m_mmu(mmu) {}

    void CPU::reset(bool cgb, uint8_t headerChecksum)
    {
        // PanDocs Power_Up_Sequence - post-boot register values
        if (cgb)
        {
            // CGB post-boot register values
            // A=0x11 is critical — games check this to detect CGB hardware
            m_regs.A  = 0x11;
            m_regs.setF(0x80);    // Z=1 N=0 H=0 C=0 on CGB
            m_regs.B  = 0x00;
            m_regs.C  = 0x00;
            m_regs.D  = 0xFF;
            m_regs.E  = 0x56;
            m_regs.H  = 0x00;
            m_regs.L  = 0x0D;
        }
        else
        {
            // DMG post-boot register values
            (void)headerChecksum; // Affects F initial value; simplified for now
            m_regs.A  = 0x01;
            m_regs.setF(0xB0);    // Z=1 N=0 H=1 C=1 on DMG
            m_regs.B  = 0x00;
            m_regs.C  = 0x13;
            m_regs.D  = 0x00;
            m_regs.E  = 0xD8;
            m_regs.H  = 0x01;
            m_regs.L  = 0x4D;
        }
        m_regs.SP = 0xFFFE;
        m_regs.PC = 0x0100;   // Skip boot ROM; start at cartridge entry point

        m_ime      = false;
        m_halted   = false;
        m_haltBug  = false;
        m_imeDelay = 0;
    }

    uint8_t CPU::fetch8()
    {
        uint8_t v = m_mmu.read8(m_regs.PC);
        m_regs.PC++;
        return v;
    }

    uint16_t CPU::fetch16()
    {
        uint16_t v = m_mmu.read16(m_regs.PC);
        m_regs.PC += 2;
        return v;
    }

    void CPU::internalCycle()
    {
        m_mmu.tickCycle();
    }

    uint32_t CPU::handleInterrupts()
    {
        // PanDocs.9 - Interrupt Service Routine
        // IF bits and IE bits are ANDed; the lowest set bit wins.
        uint8_t pending = m_mmu.readIF() & m_mmu.readIE() & 0x1F;
        if (pending == 0)
            return 0;

        if (!m_ime)
        {
            // Interrupts pending but IME disabled - wake from HALT but don't service.
            // (HALT bug is set at HALT decode time, not here.)
            return 0;
        }

        // Disable further interrupts (IME cleared before M1)
        m_ime = false;

        uint16_t vector = 0;
        uint8_t  bit    = 0;

        if      (pending & INT_VBLANK)  { bit = INT_VBLANK;  vector = IV_VBLANK;  }
        else if (pending & INT_STAT)    { bit = INT_STAT;     vector = IV_STAT;    }
        else if (pending & INT_TIMER)   { bit = INT_TIMER;    vector = IV_TIMER;   }
        else if (pending & INT_SERIAL)  { bit = INT_SERIAL;   vector = IV_SERIAL;  }
        else if (pending & INT_JOYPAD)  { bit = INT_JOYPAD;   vector = IV_JOYPAD;  }

        // ISR dispatch = 5 M-cycles (20 T-cycles):
        //   M1-M2: two internal wait cycles
        //   M3:    push PCH to --SP  (may write to $FFFF=IE, mutating it mid-dispatch)
        //   CHECK: re-read IF & IE after M3:
        //          - originally selected bit still set -> normal dispatch (M4, clear IF, jump)
        //          - originally selected bit gone, but another bit pending -> re-pick and dispatch that
        //          - originally selected bit gone, no bits remain -> full cancel (PC=$0000, IF untouched)
        //   M4:    push PCL to --SP  (always executes; SP decrements by 2 in all paths)
        //   M5:    set PC to vector / $0000 (internal)
        // PanDocs Interrupts - ie_push cancellation behaviour
        m_mmu.tickCycle(); // M1: internal wait
        m_mmu.tickCycle(); // M2: internal wait

        // M3: push high byte of PC
        m_mmu.write8(--m_regs.SP, static_cast<uint8_t>(m_regs.PC >> 8));

        // After M3, re-evaluate IF & IE (write may have mutated IE at $FFFF)
        uint8_t postM3 = m_mmu.readIF() & m_mmu.readIE() & 0x1F;
        if (!(postM3 & bit))
        {
            // Originally selected interrupt is gone - re-pick or fully cancel
            bit    = 0;
            vector = 0;
            if      (postM3 & INT_VBLANK) { bit = INT_VBLANK; vector = IV_VBLANK; }
            else if (postM3 & INT_STAT)   { bit = INT_STAT;   vector = IV_STAT;   }
            else if (postM3 & INT_TIMER)  { bit = INT_TIMER;  vector = IV_TIMER;  }
            else if (postM3 & INT_SERIAL) { bit = INT_SERIAL; vector = IV_SERIAL; }
            else if (postM3 & INT_JOYPAD) { bit = INT_JOYPAD; vector = IV_JOYPAD; }
            // If bit==0 here: full cancellation -> vector stays 0 -> PC=$0000, IF not cleared
        }

        // M4: push low byte of PC (always, even on full cancellation)
        m_mmu.write8(--m_regs.SP, static_cast<uint8_t>(m_regs.PC & 0xFF));

        if (bit != 0)
        {
            m_regs.PC = vector;
            m_mmu.writeIF(m_mmu.readIF() & ~bit);
        }
        else
        {
            // Full cancellation: PC=$0000, IF not cleared, IME stays 0
            m_regs.PC = 0x0000;
        }

        m_mmu.tickCycle(); // M5: set PC (internal)

        return 20;
    }

    uint32_t CPU::step()
    {
        // 1. Service pending interrupts (exits HALT, returns 20 cycles if dispatched)
        uint8_t pending = m_mmu.readIF() & m_mmu.readIE() & 0x1F;

        if (m_halted && pending)
        {
            m_halted = false; // Wake from HALT - PanDocs.9.2
        }

        if (pending)
        {
            uint32_t intCycles = handleInterrupts();
            if (intCycles > 0)
                return intCycles;
        }

        // 2. HALT idle - if still halted, burn 4 T-cycles
        if (m_halted)
        {
            m_mmu.tickCycle(); // 4T idle cycle
            return 4;
        }

        // 3. Fetch opcode
        //    HALT bug: PC not incremented on the bugged fetch - PanDocs.9.2
        uint8_t opcode;
        if (m_haltBug)
        {
            opcode     = m_mmu.read8(m_regs.PC); // PC not incremented
            m_haltBug  = false;
        }
        else
        {
            opcode = fetch8();
        }

        // 4. Dispatch
        uint32_t cycles = kOpcodes[opcode](*this);

        // 5. Tick the EI delay counter.
        //    EI sets m_imeDelay=2 (if not already counting). Each step decrements it.
        //    When it reaches 0, IME is set. The interrupt is then checked at the START
        //    of the following step, meaning exactly one instruction executes between EI
        //    and the interrupt being taken. Chained EIs don't extend the window because
        //    subsequent EIs find m_imeDelay>0 and leave it alone. PanDocs Interrupts
        if (m_imeDelay > 0)
        {
            --m_imeDelay;
            if (m_imeDelay == 0)
                m_ime = true;
        }

        return cycles;
    }

}
