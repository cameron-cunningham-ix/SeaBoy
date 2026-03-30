#include "DebuggerUI.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "imgui.h"
#include "SDL3/SDL.h"
#include "src/core/GameBoy.hpp"
#include "src/cartridge/Cartridge.hpp"

// ---------------------------------------------------------------------------
// SM83 Disassembler - table-driven opcode decoding
// ---------------------------------------------------------------------------

namespace
{
    struct OpInfo { const char* fmt; uint8_t len; };

    // Main opcode table (256 entries). Format specifiers:
    //   %02X = 8-bit immediate (n8), %04X = 16-bit immediate (n16),
    //   %+d  = signed 8-bit relative offset (e8)
    // CB prefix (0xCB) is handled separately.
    static const OpInfo kOps[256] = {
        // 0x00-0x0F
        {"NOP",1},{"LD BC,$%04X",3},{"LD (BC),A",1},{"INC BC",1},
        {"INC B",1},{"DEC B",1},{"LD B,$%02X",2},{"RLCA",1},
        {"LD ($%04X),SP",3},{"ADD HL,BC",1},{"LD A,(BC)",1},{"DEC BC",1},
        {"INC C",1},{"DEC C",1},{"LD C,$%02X",2},{"RRCA",1},
        // 0x10-0x1F
        {"STOP",1},{"LD DE,$%04X",3},{"LD (DE),A",1},{"INC DE",1},
        {"INC D",1},{"DEC D",1},{"LD D,$%02X",2},{"RLA",1},
        {"JR %+d",2},{"ADD HL,DE",1},{"LD A,(DE)",1},{"DEC DE",1},
        {"INC E",1},{"DEC E",1},{"LD E,$%02X",2},{"RRA",1},
        // 0x20-0x2F
        {"JR NZ,%+d",2},{"LD HL,$%04X",3},{"LD (HL+),A",1},{"INC HL",1},
        {"INC H",1},{"DEC H",1},{"LD H,$%02X",2},{"DAA",1},
        {"JR Z,%+d",2},{"ADD HL,HL",1},{"LD A,(HL+)",1},{"DEC HL",1},
        {"INC L",1},{"DEC L",1},{"LD L,$%02X",2},{"CPL",1},
        // 0x30-0x3F
        {"JR NC,%+d",2},{"LD SP,$%04X",3},{"LD (HL-),A",1},{"INC SP",1},
        {"INC (HL)",1},{"DEC (HL)",1},{"LD (HL),$%02X",2},{"SCF",1},
        {"JR C,%+d",2},{"ADD HL,SP",1},{"LD A,(HL-)",1},{"DEC SP",1},
        {"INC A",1},{"DEC A",1},{"LD A,$%02X",2},{"CCF",1},
        // 0x40-0x4F
        {"LD B,B",1},{"LD B,C",1},{"LD B,D",1},{"LD B,E",1},
        {"LD B,H",1},{"LD B,L",1},{"LD B,(HL)",1},{"LD B,A",1},
        {"LD C,B",1},{"LD C,C",1},{"LD C,D",1},{"LD C,E",1},
        {"LD C,H",1},{"LD C,L",1},{"LD C,(HL)",1},{"LD C,A",1},
        // 0x50-0x5F
        {"LD D,B",1},{"LD D,C",1},{"LD D,D",1},{"LD D,E",1},
        {"LD D,H",1},{"LD D,L",1},{"LD D,(HL)",1},{"LD D,A",1},
        {"LD E,B",1},{"LD E,C",1},{"LD E,D",1},{"LD E,E",1},
        {"LD E,H",1},{"LD E,L",1},{"LD E,(HL)",1},{"LD E,A",1},
        // 0x60-0x6F
        {"LD H,B",1},{"LD H,C",1},{"LD H,D",1},{"LD H,E",1},
        {"LD H,H",1},{"LD H,L",1},{"LD H,(HL)",1},{"LD H,A",1},
        {"LD L,B",1},{"LD L,C",1},{"LD L,D",1},{"LD L,E",1},
        {"LD L,H",1},{"LD L,L",1},{"LD L,(HL)",1},{"LD L,A",1},
        // 0x70-0x7F
        {"LD (HL),B",1},{"LD (HL),C",1},{"LD (HL),D",1},{"LD (HL),E",1},
        {"LD (HL),H",1},{"LD (HL),L",1},{"HALT",1},{"LD (HL),A",1},
        {"LD A,B",1},{"LD A,C",1},{"LD A,D",1},{"LD A,E",1},
        {"LD A,H",1},{"LD A,L",1},{"LD A,(HL)",1},{"LD A,A",1},
        // 0x80-0x8F
        {"ADD A,B",1},{"ADD A,C",1},{"ADD A,D",1},{"ADD A,E",1},
        {"ADD A,H",1},{"ADD A,L",1},{"ADD A,(HL)",1},{"ADD A,A",1},
        {"ADC A,B",1},{"ADC A,C",1},{"ADC A,D",1},{"ADC A,E",1},
        {"ADC A,H",1},{"ADC A,L",1},{"ADC A,(HL)",1},{"ADC A,A",1},
        // 0x90-0x9F
        {"SUB B",1},{"SUB C",1},{"SUB D",1},{"SUB E",1},
        {"SUB H",1},{"SUB L",1},{"SUB (HL)",1},{"SUB A",1},
        {"SBC A,B",1},{"SBC A,C",1},{"SBC A,D",1},{"SBC A,E",1},
        {"SBC A,H",1},{"SBC A,L",1},{"SBC A,(HL)",1},{"SBC A,A",1},
        // 0xA0-0xAF
        {"AND B",1},{"AND C",1},{"AND D",1},{"AND E",1},
        {"AND H",1},{"AND L",1},{"AND (HL)",1},{"AND A",1},
        {"XOR B",1},{"XOR C",1},{"XOR D",1},{"XOR E",1},
        {"XOR H",1},{"XOR L",1},{"XOR (HL)",1},{"XOR A",1},
        // 0xB0-0xBF
        {"OR B",1},{"OR C",1},{"OR D",1},{"OR E",1},
        {"OR H",1},{"OR L",1},{"OR (HL)",1},{"OR A",1},
        {"CP B",1},{"CP C",1},{"CP D",1},{"CP E",1},
        {"CP H",1},{"CP L",1},{"CP (HL)",1},{"CP A",1},
        // 0xC0-0xCF
        {"RET NZ",1},{"POP BC",1},{"JP NZ,$%04X",3},{"JP $%04X",3},
        {"CALL NZ,$%04X",3},{"PUSH BC",1},{"ADD A,$%02X",2},{"RST 00H",1},
        {"RET Z",1},{"RET",1},{"JP Z,$%04X",3},{"CB %02X",2},
        {"CALL Z,$%04X",3},{"CALL $%04X",3},{"ADC A,$%02X",2},{"RST 08H",1},
        // 0xD0-0xDF
        {"RET NC",1},{"POP DE",1},{"JP NC,$%04X",3},{"???",1},
        {"CALL NC,$%04X",3},{"PUSH DE",1},{"SUB $%02X",2},{"RST 10H",1},
        {"RET C",1},{"RETI",1},{"JP C,$%04X",3},{"???",1},
        {"CALL C,$%04X",3},{"???",1},{"SBC A,$%02X",2},{"RST 18H",1},
        // 0xE0-0xEF
        {"LDH ($FF%02X),A",2},{"POP HL",1},{"LD ($FF00+C),A",1},{"???",1},
        {"???",1},{"PUSH HL",1},{"AND $%02X",2},{"RST 20H",1},
        {"ADD SP,%+d",2},{"JP HL",1},{"LD ($%04X),A",3},{"???",1},
        {"???",1},{"???",1},{"XOR $%02X",2},{"RST 28H",1},
        // 0xF0-0xFF
        {"LDH A,($FF%02X)",2},{"POP AF",1},{"LD A,($FF00+C)",1},{"DI",1},
        {"???",1},{"PUSH AF",1},{"OR $%02X",2},{"RST 30H",1},
        {"LD HL,SP%+d",2},{"LD SP,HL",1},{"LD A,($%04X)",3},{"EI",1},
        {"???",1},{"???",1},{"CP $%02X",2},{"RST 38H",1},
    };

    // CB-prefix opcode names (all are 2 bytes total: CB + sub)
    static const char* kR8Names[8] = {"B","C","D","E","H","L","(HL)","A"};
    static const char* kCBBase[8]  = {"RLC","RRC","RL","RR","SLA","SRA","SWAP","SRL"};

    void formatCBOpcode(uint8_t sub, char* buf, int bufSize)
    {
        uint8_t reg = sub & 0x07;
        if (sub < 0x40)
        {
            // RLC..SRL group
            std::snprintf(buf, bufSize, "%s %s", kCBBase[sub >> 3], kR8Names[reg]);
        }
        else if (sub < 0x80)
        {
            std::snprintf(buf, bufSize, "BIT %d,%s", (sub >> 3) & 7, kR8Names[reg]);
        }
        else if (sub < 0xC0)
        {
            std::snprintf(buf, bufSize, "RES %d,%s", (sub >> 3) & 7, kR8Names[reg]);
        }
        else
        {
            std::snprintf(buf, bufSize, "SET %d,%s", (sub >> 3) & 7, kR8Names[reg]);
        }
    }
} // anonymous namespace

DebuggerUI::DebuggerUI(SeaBoy::GameBoy& gb, SDL_Renderer* renderer)
    : m_gb(gb)
    , m_renderer(renderer)
{
    m_gb.setExecCallback(&DebuggerUI::onExecSnapshot, this);
    m_gb.setEventCallback(&DebuggerUI::onGameEvent, this);
    m_gb.setWriteTraceCallback(&DebuggerUI::onMemWrite, this);
}

DebuggerUI::~DebuggerUI()
{
    if (m_tileTexture)    SDL_DestroyTexture(m_tileTexture);
    if (m_tilemapTexture) SDL_DestroyTexture(m_tilemapTexture);
}

bool DebuggerUI::consumeStep()
{
    if (m_stepPending) { m_stepPending = false; return true; }
    return false;
}

bool DebuggerUI::consumeStepFrame()
{
    if (m_stepFramePending) { m_stepFramePending = false; return true; }
    return false;
}

int DebuggerUI::consumeStepNInstr()
{
    if (m_stepNInstrPending) { m_stepNInstrPending = false; return m_stepNInstr; }
    return 0;
}

bool DebuggerUI::isBreakpointAddr(uint16_t addr) const
{
    for (const auto& bp : m_breakpoints)
        if (bp.enabled && bp.addr == addr) return true;
    return false;
}

bool DebuggerUI::evalCondition(const Breakpoint& bp) const
{
    const auto& regs = m_gb.cpu().registers();
    uint16_t lhs = 0;
    switch (bp.lhsOperand)
    {
        case BPOperand::RegA:  lhs = regs.A;       break;
        case BPOperand::RegB:  lhs = regs.B;       break;
        case BPOperand::RegC:  lhs = regs.C;       break;
        case BPOperand::RegD:  lhs = regs.D;       break;
        case BPOperand::RegE:  lhs = regs.E;       break;
        case BPOperand::RegH:  lhs = regs.H;       break;
        case BPOperand::RegL:  lhs = regs.L;       break;
        case BPOperand::RegF:  lhs = regs.F;       break;
        case BPOperand::RegAF: lhs = regs.getAF(); break;
        case BPOperand::RegBC: lhs = regs.getBC(); break;
        case BPOperand::RegDE: lhs = regs.getDE(); break;
        case BPOperand::RegHL: lhs = regs.getHL(); break;
        case BPOperand::RegSP: lhs = regs.SP;      break;
        case BPOperand::RegPC: lhs = regs.PC;      break;
        case BPOperand::Mem8:
            lhs = m_gb.mmu().peek8(bp.lhsAddr);
            break;
        case BPOperand::Mem16:
            lhs = static_cast<uint16_t>(m_gb.mmu().peek8(bp.lhsAddr)) |
                  (static_cast<uint16_t>(m_gb.mmu().peek8(static_cast<uint16_t>(bp.lhsAddr + 1u))) << 8);
            break;
    }
    switch (bp.condOp)
    {
        case BPCondOp::EQ: return lhs == bp.rhsValue;
        case BPCondOp::NE: return lhs != bp.rhsValue;
        case BPCondOp::LT: return lhs <  bp.rhsValue;
        case BPCondOp::LE: return lhs <= bp.rhsValue;
        case BPCondOp::GT: return lhs >  bp.rhsValue;
        case BPCondOp::GE: return lhs >= bp.rhsValue;
    }
    return true;
}

bool DebuggerUI::checkBreakpoints(uint16_t pc)
{
    for (auto& bp : m_breakpoints)
    {
        if (!bp.enabled || bp.addr != pc) continue;
        ++bp.hitCount;
        if (bp.hitTarget > 0 && bp.hitCount != bp.hitTarget) continue;
        if (bp.hasCondition && !evalCondition(bp)) continue;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Panel: Execution Controls
// ---------------------------------------------------------------------------

void DebuggerUI::renderControlPanel()
{
    if (!ImGui::Begin("Controls", &m_showControls)) { ImGui::End(); return; }

    if (ImGui::Button(m_paused ? "Resume (F5)" : "Pause (F5)"))
        m_paused = !m_paused;

    if (m_paused)
    {
        ImGui::SameLine();
        if (ImGui::Button("Step (F10)"))
            m_stepPending = true;

        ImGui::SameLine();
        if (ImGui::Button("Step Frame (F6)"))
            m_stepFramePending = true;

        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("##stepN", &m_stepNInstr, 0, 0);
        if (m_stepNInstr < 1) m_stepNInstr = 1;
        ImGui::SameLine();
        if (ImGui::Button("Step N instr"))
            m_stepNInstrPending = true;
    }

    // Calculate average framerate
    m_fpsFrameCount++;
    m_fpsAccum += ImGui::GetIO().DeltaTime;
    if (m_fpsAccum >= 1.0f)
    {
        m_fpsDisplay    = static_cast<float>(m_fpsFrameCount) / m_fpsAccum;
        m_fpsFrameCount = 0;
        m_fpsAccum      = 0.0f;
    }
    ImGui::Text("FPS: %.1f", m_fpsDisplay);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Panel: CPU Registers
// ---------------------------------------------------------------------------

void DebuggerUI::renderCPURegisters()
{
    if (!ImGui::Begin("CPU Registers", &m_showCPURegisters)) { ImGui::End(); return; }

    const auto& r = m_gb.cpu().registers();

    ImGui::Text("AF: %04X   (A=%02X F=%02X)", r.getAF(), r.A, r.F);
    ImGui::Text("BC: %04X   (B=%02X C=%02X)", r.getBC(), r.B, r.C);
    ImGui::Text("DE: %04X   (D=%02X E=%02X)", r.getDE(), r.D, r.E);
    ImGui::Text("HL: %04X   (H=%02X L=%02X)", r.getHL(), r.H, r.L);
    ImGui::Separator();
    ImGui::Text("SP: %04X", r.SP);
    ImGui::Text("PC: %04X", r.PC);
    ImGui::Separator();
    ImGui::Text("Flags: Z=%d N=%d H=%d C=%d",
        r.flagZ(), r.flagN(), r.flagH(), r.flagC());
    ImGui::Text("IME: %d  HALT: %d",
        static_cast<int>(m_gb.cpu().ime()),
        static_cast<int>(m_gb.cpu().halted()));

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Panel: Breakpoints
// ---------------------------------------------------------------------------

void DebuggerUI::renderBreakpoints()
{
    if (!ImGui::Begin("Breakpoints", &m_showBreakpoints)) { ImGui::End(); return; }

    static const char* kOperandNames[] = {
        "A","B","C","D","E","H","L","F",
        "AF","BC","DE","HL","SP","PC","[n]8","[n]16"
    };
    static const char* kCondOpNames[] = { "==","!=","<","<=",">",">=" };

    // --- Add row ---
    ImGui::SetNextItemWidth(54.0f);
    bool enter = ImGui::InputText("##bp_addr", m_bpInputBuf, sizeof(m_bpInputBuf),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool doAdd = ImGui::Button("Add") || (enter && m_bpInputBuf[0] != '\0');
    ImGui::SameLine();
    ImGui::Checkbox("Cond##add", &m_bpAddCond);

    if (m_bpAddCond)
    {
        ImGui::SetNextItemWidth(68.0f);
        ImGui::Combo("##bp_oper", &m_bpAddOperandIdx, kOperandNames, 16);
        if (m_bpAddOperandIdx >= 14)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50.0f);
            ImGui::InputText("##bp_lhsa", m_bpAddLhsAddrBuf, sizeof(m_bpAddLhsAddrBuf),
                ImGuiInputTextFlags_CharsHexadecimal);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(42.0f);
        ImGui::Combo("##bp_op", &m_bpAddOpIdx, kCondOpNames, 6);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50.0f);
        ImGui::InputText("##bp_rhs", m_bpAddRhsBuf, sizeof(m_bpAddRhsBuf),
            ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50.0f);
        ImGui::InputScalar("Hit##add", ImGuiDataType_U32, &m_bpAddHitTarget);
    }

    if (doAdd && m_bpInputBuf[0] != '\0')
    {
        uint16_t addr = static_cast<uint16_t>(std::strtol(m_bpInputBuf, nullptr, 16));
        bool dup = false;
        for (const auto& b : m_breakpoints) if (b.addr == addr) { dup = true; break; }
        if (!dup)
        {
            Breakpoint bp;
            bp.addr         = addr;
            bp.hasCondition = m_bpAddCond;
            bp.lhsOperand   = static_cast<BPOperand>(m_bpAddOperandIdx);
            bp.lhsAddr      = static_cast<uint16_t>(std::strtol(m_bpAddLhsAddrBuf, nullptr, 16));
            bp.condOp       = static_cast<BPCondOp>(m_bpAddOpIdx);
            bp.rhsValue     = static_cast<uint16_t>(std::strtol(m_bpAddRhsBuf, nullptr, 16));
            bp.hitTarget    = m_bpAddHitTarget;
            m_breakpoints.push_back(bp);
        }
        m_bpInputBuf[0] = '\0';
    }

    ImGui::Separator();

    // --- List ---
    int removeIdx = -1;
    for (int i = 0; i < static_cast<int>(m_breakpoints.size()); ++i)
    {
        auto& bp = m_breakpoints[i];
        ImGui::PushID(i);

        // Enable toggle
        ImGui::Checkbox("##en", &bp.enabled);
        ImGui::SameLine();

        // Address (dimmed when disabled)
        if (!bp.enabled)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::Text("$%04X", bp.addr);
        if (!bp.enabled)
            ImGui::PopStyleColor();
        ImGui::SameLine();

        // Condition summary
        if (bp.hasCondition)
        {
            char condStr[48];
            if (bp.lhsOperand >= BPOperand::Mem8)
                std::snprintf(condStr, sizeof(condStr), "[$%04X]%s$%04X",
                    bp.lhsAddr, kCondOpNames[static_cast<int>(bp.condOp)], bp.rhsValue);
            else
                std::snprintf(condStr, sizeof(condStr), "%s%s$%04X",
                    kOperandNames[static_cast<int>(bp.lhsOperand)],
                    kCondOpNames[static_cast<int>(bp.condOp)],
                    bp.rhsValue);
            ImGui::TextUnformatted(condStr);
        }
        else
        {
            ImGui::TextDisabled("(unconditional)");
        }
        ImGui::SameLine();

        // Hit count display
        if (bp.hitTarget > 0)
            ImGui::Text("hit:%u/%u", bp.hitCount, bp.hitTarget);
        else
            ImGui::Text("hit:%u", bp.hitCount);
        ImGui::SameLine();

        if (ImGui::SmallButton("Rst")) bp.hitCount = 0;
        ImGui::SameLine();

        // Edit toggle
        bool editOpen = (m_bpEditIdx == i);
        if (ImGui::SmallButton(editOpen ? "v" : ">"))
        {
            if (editOpen)
            {
                m_bpEditIdx = -1;
            }
            else
            {
                m_bpEditIdx          = i;
                m_bpEditCond         = bp.hasCondition;
                m_bpEditOperandIdx   = static_cast<int>(bp.lhsOperand);
                std::snprintf(m_bpEditLhsAddrBuf, sizeof(m_bpEditLhsAddrBuf), "%04X", bp.lhsAddr);
                m_bpEditOpIdx        = static_cast<int>(bp.condOp);
                std::snprintf(m_bpEditRhsBuf, sizeof(m_bpEditRhsBuf), "%04X", bp.rhsValue);
                m_bpEditHitTarget    = bp.hitTarget;
            }
        }
        ImGui::SameLine();

        if (ImGui::SmallButton("X")) removeIdx = i;

        // Inline edit section
        if (editOpen)
        {
            ImGui::Indent(16.0f);
            ImGui::Checkbox("Condition##edit", &m_bpEditCond);
            if (m_bpEditCond)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(68.0f);
                ImGui::Combo("##edit_oper", &m_bpEditOperandIdx, kOperandNames, 16);
                if (m_bpEditOperandIdx >= 14)
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(50.0f);
                    ImGui::InputText("##edit_lhsa", m_bpEditLhsAddrBuf, sizeof(m_bpEditLhsAddrBuf),
                        ImGuiInputTextFlags_CharsHexadecimal);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(72.0f);
                ImGui::Combo("##edit_op", &m_bpEditOpIdx, kCondOpNames, 6);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50.0f);
                ImGui::InputText("##edit_rhs", m_bpEditRhsBuf, sizeof(m_bpEditRhsBuf),
                    ImGuiInputTextFlags_CharsHexadecimal);
            }
            ImGui::SetNextItemWidth(60.0f);
            ImGui::InputScalar("Hit target##edit", ImGuiDataType_U32, &m_bpEditHitTarget);

            if (ImGui::SmallButton("Apply"))
            {
                bp.hasCondition = m_bpEditCond;
                bp.lhsOperand   = static_cast<BPOperand>(m_bpEditOperandIdx);
                bp.lhsAddr      = static_cast<uint16_t>(std::strtol(m_bpEditLhsAddrBuf, nullptr, 16));
                bp.condOp       = static_cast<BPCondOp>(m_bpEditOpIdx);
                bp.rhsValue     = static_cast<uint16_t>(std::strtol(m_bpEditRhsBuf, nullptr, 16));
                bp.hitTarget    = m_bpEditHitTarget;
                m_bpEditIdx = -1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel")) m_bpEditIdx = -1;
            ImGui::Unindent(16.0f);
        }

        ImGui::PopID();
    }

    if (removeIdx >= 0)
    {
        m_breakpoints.erase(m_breakpoints.begin() + removeIdx);
        if (m_bpEditIdx == removeIdx)        m_bpEditIdx = -1;
        else if (m_bpEditIdx > removeIdx)    --m_bpEditIdx;
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// SM83 Disassembler
// ---------------------------------------------------------------------------

uint8_t DebuggerUI::disassemble(uint16_t addr, char* buf, int bufSize) const
{
    uint8_t op = m_gb.mmu().peek8(addr);
    if (op == 0xCB)
    {
        uint8_t sub = m_gb.mmu().peek8(static_cast<uint16_t>(addr + 1));
        formatCBOpcode(sub, buf, bufSize);
        return 2;
    }

    const OpInfo& info = kOps[op];
    if (info.len == 1)
    {
        std::snprintf(buf, bufSize, "%s", info.fmt);
    }
    else if (info.len == 2)
    {
        uint8_t n8 = m_gb.mmu().peek8(static_cast<uint16_t>(addr + 1));
        // JR/ADD SP/LD HL,SP use signed offset (%+d)
        if (op == 0x18 || op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38
            || op == 0xE8 || op == 0xF8)
        {
            std::snprintf(buf, bufSize, info.fmt, static_cast<int8_t>(n8));
        }
        else
        {
            std::snprintf(buf, bufSize, info.fmt, n8);
        }
    }
    else // len == 3
    {
        uint8_t lo = m_gb.mmu().peek8(static_cast<uint16_t>(addr + 1));
        uint8_t hi = m_gb.mmu().peek8(static_cast<uint16_t>(addr + 2));
        uint16_t n16 = static_cast<uint16_t>(lo | (hi << 8));
        std::snprintf(buf, bufSize, info.fmt, n16);
    }
    return info.len;
}

// ---------------------------------------------------------------------------
// Panel: Disassembly
// ---------------------------------------------------------------------------

void DebuggerUI::renderDisassembly()
{
    if (!ImGui::Begin("Disassembly", &m_showDisassembly)) { ImGui::End(); return; }

    uint16_t pc = m_gb.cpu().registers().PC;
    char buf[64];

    // Walk backward heuristic: start 48 bytes before PC, forward-decode
    uint16_t scanAddr = static_cast<uint16_t>(pc - 48);
    std::vector<uint16_t> preAddrs;
    while (scanAddr != pc && scanAddr < pc)
    {
        preAddrs.push_back(scanAddr);
        uint8_t len = disassemble(scanAddr, buf, sizeof(buf));
        scanAddr = static_cast<uint16_t>(scanAddr + len);
    }
    // Keep last 12 before PC
    if (preAddrs.size() > 12)
        preAddrs.erase(preAddrs.begin(),
                       preAddrs.begin() + static_cast<int>(preAddrs.size()) - 12);

    // Forward: PC + 20 instructions
    std::vector<uint16_t> fwdAddrs;
    fwdAddrs.push_back(pc);
    uint16_t fwd = pc;
    for (int i = 0; i < 20; ++i)
    {
        uint8_t len = disassemble(fwd, buf, sizeof(buf));
        fwd = static_cast<uint16_t>(fwd + len);
        fwdAddrs.push_back(fwd);
    }

    // Render all lines
    auto renderLine = [&](uint16_t a) {
        uint8_t len = disassemble(a, buf, sizeof(buf));
        bool isCurrent = (a == pc);
        // Show breakpoint marker (pure lookup - no hit-count side effect)
        bool isBP = isBreakpointAddr(a);
        if (isBP)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "*");
        else
            ImGui::TextUnformatted(" ");
        ImGui::SameLine(0.0f, 0.0f);

        if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
        // Show raw bytes
        char rawBytes[10];
        if (len == 1)
            std::snprintf(rawBytes, sizeof(rawBytes), "%02X      ", m_gb.mmu().peek8(a));
        else if (len == 2)
            std::snprintf(rawBytes, sizeof(rawBytes), "%02X %02X   ",
                m_gb.mmu().peek8(a), m_gb.mmu().peek8(static_cast<uint16_t>(a + 1)));
        else
            std::snprintf(rawBytes, sizeof(rawBytes), "%02X %02X %02X",
                m_gb.mmu().peek8(a), m_gb.mmu().peek8(static_cast<uint16_t>(a + 1)),
                m_gb.mmu().peek8(static_cast<uint16_t>(a + 2)));

        ImGui::Text("%04X  %s  %s", a, rawBytes, buf);
        if (isCurrent)
        {
            ImGui::PopStyleColor();
            ImGui::SetScrollHereY(0.3f);
        }
    };

    for (uint16_t a : preAddrs) renderLine(a);
    for (uint16_t a : fwdAddrs) renderLine(a);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Panel: Memory Hex Viewer
// ---------------------------------------------------------------------------

void DebuggerUI::renderMemoryViewer()
{
    if (!ImGui::Begin("Memory", &m_showMemory)) { ImGui::End(); return; }

    // Address jump
    ImGui::SetNextItemWidth(60.0f);
    bool jump = ImGui::InputText("##mem_addr", m_memAddrBuf, sizeof(m_memAddrBuf),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Go") || jump)
    {
        if (m_memAddrBuf[0] != '\0')
        {
            uint16_t addr = static_cast<uint16_t>(std::strtol(m_memAddrBuf, nullptr, 16));
            int row = addr / 16;
            // Scroll to target row. SetScrollY requires pixel offset.
            float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            ImGui::SetScrollY(static_cast<float>(row) * lineHeight);
        }
    }

    ImGui::Separator();

    // 4096 rows × 16 bytes = 64KB
    ImGuiListClipper clipper;
    clipper.Begin(4096);
    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            uint16_t base = static_cast<uint16_t>(row * 16);

            // Address
            ImGui::Text("%04X: ", base);

            // Hex bytes
            for (int col = 0; col < 16; ++col)
            {
                ImGui::SameLine(0.0f, col == 8 ? 8.0f : 2.0f);
                uint8_t byte = m_gb.mmu().peek8(static_cast<uint16_t>(base + col));
                ImGui::Text("%02X", byte);
            }

            // ASCII column
            ImGui::SameLine(0.0f, 12.0f);
            char ascii[17];
            for (int col = 0; col < 16; ++col)
            {
                uint8_t byte = m_gb.mmu().peek8(static_cast<uint16_t>(base + col));
                ascii[col] = (byte >= 0x20 && byte <= 0x7E)
                    ? static_cast<char>(byte) : '.';
            }
            ascii[16] = '\0';
            ImGui::TextUnformatted(ascii);
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Panel: PPU State
// ---------------------------------------------------------------------------

void DebuggerUI::renderPPUState()
{
    if (!ImGui::Begin("PPU State", &m_showPPUState)) { ImGui::End(); return; }

    const auto& p = m_gb.ppu();
    static const char* kModes[] = {"HBlank (0)", "VBlank (1)", "OAMScan (2)", "Drawing (3)"};
    ImGui::Text("Mode: %s", kModes[static_cast<int>(p.mode())]);
    ImGui::Text("LY: %3d   LineCycle: %3d", p.ly(), p.lineCycle());
    ImGui::Separator();
    ImGui::Text("LCDC: %02X  STAT: %02X", p.lcdc(), p.stat());
    ImGui::Text("SCY:  %02X  SCX:  %02X", p.scy(), p.scx());
    ImGui::Text("LYC:  %02X", p.lyc());
    ImGui::Text("WY:   %02X  WX:   %02X", p.wy(), p.wx());
    ImGui::Separator();
    ImGui::Text("BGP:  %02X  OBP0: %02X  OBP1: %02X",
        p.palettes().readBGP(), p.palettes().readOBP0(), p.palettes().readOBP1());

    // LCDC bit breakdown
    ImGui::Separator();
    ImGui::Text("LCDC bits:");
    uint8_t lcdc = p.lcdc();
    ImGui::Text("  LCD Enable:    %d", (lcdc >> 7) & 1);
    ImGui::Text("  Win TileMap:   %s", (lcdc & 0x40) ? "9C00" : "9800");
    ImGui::Text("  Window Enable: %d", (lcdc >> 5) & 1);
    ImGui::Text("  BG/Win Tiles:  %s", (lcdc & 0x10) ? "8000" : "8800");
    ImGui::Text("  BG TileMap:    %s", (lcdc & 0x08) ? "9C00" : "9800");
    ImGui::Text("  OBJ Size:      %s", (lcdc & 0x04) ? "8x16" : "8x8");
    ImGui::Text("  OBJ Enable:    %d", (lcdc >> 1) & 1);
    ImGui::Text("  BG/Win Enable: %d", lcdc & 1);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Panel: I/O Registers
// ---------------------------------------------------------------------------

void DebuggerUI::renderIORegisters()
{
    if (!ImGui::Begin("I/O Registers", &m_showIORegisters)) { ImGui::End(); return; }

    // Timer
    ImGui::Text("Timer:");
    const auto& t = m_gb.timer();
    ImGui::Text("  DIV:  %02X  TIMA: %02X  TMA: %02X  TAC: %02X",
        t.read(0xFF04), t.read(0xFF05), t.read(0xFF06), t.read(0xFF07));

    ImGui::Separator();

    // Interrupts
    uint8_t ie = m_gb.mmu().readIE();
    uint8_t if_ = m_gb.mmu().readIF();
    ImGui::Text("Interrupts:");
    ImGui::Text("  IE: %02X  IF: %02X", ie, if_);
    ImGui::Text("  VBlank: IE=%d IF=%d", ie & 1, if_ & 1);
    ImGui::Text("  STAT:   IE=%d IF=%d", (ie >> 1) & 1, (if_ >> 1) & 1);
    ImGui::Text("  Timer:  IE=%d IF=%d", (ie >> 2) & 1, (if_ >> 2) & 1);
    ImGui::Text("  Serial: IE=%d IF=%d", (ie >> 3) & 1, (if_ >> 3) & 1);
    ImGui::Text("  Joypad: IE=%d IF=%d", (ie >> 4) & 1, (if_ >> 4) & 1);

    ImGui::Separator();

    // Joypad
    ImGui::Text("Joypad:");
    ImGui::Text("  P1: %02X", m_gb.mmu().peek8(0xFF00));

    ImGui::Separator();

    // Serial
    ImGui::Text("Serial:");
    ImGui::Text("  SB: %02X  SC: %02X",
        m_gb.mmu().peek8(0xFF01), m_gb.mmu().peek8(0xFF02));

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Panel: OAM Viewer
// ---------------------------------------------------------------------------

void DebuggerUI::renderOAMViewer()
{
    if (!ImGui::Begin("OAM", &m_showOAM)) { ImGui::End(); return; }

    const uint8_t* oam = m_gb.ppu().rawOAM();

    ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("oam_table", 7, tableFlags))
    {
        ImGui::TableSetupColumn("#",     ImGuiTableColumnFlags_None, 24.0f);
        ImGui::TableSetupColumn("Y",     ImGuiTableColumnFlags_None, 30.0f);
        ImGui::TableSetupColumn("X",     ImGuiTableColumnFlags_None, 30.0f);
        ImGui::TableSetupColumn("Tile",  ImGuiTableColumnFlags_None, 36.0f);
        ImGui::TableSetupColumn("Attr",  ImGuiTableColumnFlags_None, 36.0f);
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_None, 80.0f);
        ImGui::TableSetupColumn("Pos",   ImGuiTableColumnFlags_None, 60.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 40; ++i)
        {
            int base = i * 4;
            uint8_t y    = oam[base];
            uint8_t x    = oam[base + 1];
            uint8_t tile = oam[base + 2];
            uint8_t attr = oam[base + 3];

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%2d", i);
            ImGui::TableNextColumn(); ImGui::Text("%3d", y);
            ImGui::TableNextColumn(); ImGui::Text("%3d", x);
            ImGui::TableNextColumn(); ImGui::Text("%02X", tile);
            ImGui::TableNextColumn(); ImGui::Text("%02X", attr);
            ImGui::TableNextColumn();
            ImGui::Text("%s%s%s%s",
                (attr & 0x80) ? "Pri " : "",
                (attr & 0x40) ? "FlY " : "",
                (attr & 0x20) ? "FlX " : "",
                (attr & 0x10) ? "Pal1" : "Pal0");
            ImGui::TableNextColumn();
            // Screen position (OAM Y/X are offset by 16/8)
            ImGui::Text("%d,%d", x - 8, y - 16);
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Tile Viewer - decode all 384 VRAM tiles into a 128×192 texture
// ---------------------------------------------------------------------------

// DMG grayscale shades (RGBA8888)
namespace
{
    constexpr uint32_t kShades[4] = {
        0xFFFFFFFF, // white
        0xAAAAAAFF, // light gray
        0x555555FF, // dark gray
        0x000000FF, // black
    };
}

void DebuggerUI::rebuildTileTexture()
{
    if (!m_tileTexture)
    {
        m_tileTexture = SDL_CreateTexture(m_renderer,
            SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
            16 * 8, 24 * 8); // 128×192
        SDL_SetTextureScaleMode(m_tileTexture, SDL_SCALEMODE_NEAREST);
    }

    void* pixels = nullptr;
    int pitch = 0;
    if (!SDL_LockTexture(m_tileTexture, nullptr, &pixels, &pitch))
        return;

    auto* dst = static_cast<uint32_t*>(pixels);
    const int dstPitch = pitch / static_cast<int>(sizeof(uint32_t));
    const uint8_t* vram = m_gb.ppu().rawVRAM();
    const uint8_t bgp = m_gb.ppu().palettes().readBGP();

    for (int tileIdx = 0; tileIdx < 384; ++tileIdx)
    {
        int gridX = tileIdx % 16;
        int gridY = tileIdx / 16;
        int tileBase = tileIdx * 16;

        for (int row = 0; row < 8; ++row)
        {
            uint8_t lo = vram[tileBase + row * 2];
            uint8_t hi = vram[tileBase + row * 2 + 1];
            for (int col = 0; col < 8; ++col)
            {
                int bit = 7 - col;
                uint8_t colorID = static_cast<uint8_t>(
                    (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
                uint8_t shade = (bgp >> (colorID * 2)) & 0x03;
                int dstX = gridX * 8 + col;
                int dstY = gridY * 8 + row;
                dst[dstY * dstPitch + dstX] = kShades[shade];
            }
        }
    }

    SDL_UnlockTexture(m_tileTexture);
}

void DebuggerUI::renderTileViewer()
{
    if (!ImGui::Begin("Tile Viewer", &m_showTileViewer)) { ImGui::End(); return; }

    rebuildTileTexture();

    constexpr float scale = 2.0f;
    ImVec2 size(128.0f * scale, 192.0f * scale);
    ImGui::Image(reinterpret_cast<ImTextureID>(m_tileTexture), size);

    // Tooltip with tile index on hover
    if (ImGui::IsItemHovered())
    {
        ImVec2 mp = ImGui::GetMousePos();
        ImVec2 ip = ImGui::GetItemRectMin();
        int tx = static_cast<int>((mp.x - ip.x) / (8.0f * scale));
        int ty = static_cast<int>((mp.y - ip.y) / (8.0f * scale));
        if (tx >= 0 && tx < 16 && ty >= 0 && ty < 24)
        {
            int idx = ty * 16 + tx;
            ImGui::SetTooltip("Tile %d ($%04X)", idx, 0x8000 + idx * 16);
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Tilemap Viewer - decode a 32×32 tile BG/Window map into 256×256 texture
// ---------------------------------------------------------------------------

void DebuggerUI::rebuildTilemapTexture()
{
    if (!m_tilemapTexture)
    {
        m_tilemapTexture = SDL_CreateTexture(m_renderer,
            SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
            256, 256);
        SDL_SetTextureScaleMode(m_tilemapTexture, SDL_SCALEMODE_NEAREST);
    }

    void* pixels = nullptr;
    int pitch = 0;
    if (!SDL_LockTexture(m_tilemapTexture, nullptr, &pixels, &pitch))
        return;

    auto* dst = static_cast<uint32_t*>(pixels);
    const int dstPitch = pitch / static_cast<int>(sizeof(uint32_t));
    const uint8_t* vram = m_gb.ppu().rawVRAM();
    const uint8_t lcdc = m_gb.ppu().lcdc();
    const uint8_t bgp  = m_gb.ppu().palettes().readBGP();

    // Tilemap base: 0x9800 or 0x9C00 (VRAM-relative: 0x1800 or 0x1C00)
    uint16_t mapBase = (m_tilemapSelect == 0)
        ? static_cast<uint16_t>((lcdc & 0x08) ? 0x1C00 : 0x1800)   // BG map from LCDC bit 3
        : static_cast<uint16_t>((lcdc & 0x40) ? 0x1C00 : 0x1800);  // Win map from LCDC bit 6

    // Tile data addressing: LCDC bit 4
    // 1 -> unsigned 0x8000-based (VRAM offset 0x0000)
    // 0 -> signed 0x8800-based (VRAM offset 0x1000, index is signed)
    bool unsignedMode = (lcdc & 0x10) != 0;

    for (int tileY = 0; tileY < 32; ++tileY)
    {
        for (int tileX = 0; tileX < 32; ++tileX)
        {
            uint8_t tileIndex = vram[mapBase + tileY * 32 + tileX];

            // Resolve tile data offset in VRAM
            int tileDataOffset;
            if (unsignedMode)
            {
                tileDataOffset = tileIndex * 16;
            }
            else
            {
                // Signed: tile 0 is at 0x9000 (VRAM 0x1000)
                auto signedIdx = static_cast<int8_t>(tileIndex);
                tileDataOffset = 0x1000 + signedIdx * 16;
            }

            // Decode 8×8 tile pixels
            for (int row = 0; row < 8; ++row)
            {
                uint8_t lo = vram[tileDataOffset + row * 2];
                uint8_t hi = vram[tileDataOffset + row * 2 + 1];
                for (int col = 0; col < 8; ++col)
                {
                    int bit = 7 - col;
                    uint8_t colorID = static_cast<uint8_t>(
                        (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
                    uint8_t shade = (bgp >> (colorID * 2)) & 0x03;
                    int px = tileX * 8 + col;
                    int py = tileY * 8 + row;
                    dst[py * dstPitch + px] = kShades[shade];
                }
            }
        }
    }

    SDL_UnlockTexture(m_tilemapTexture);
}

void DebuggerUI::renderTilemapViewer()
{
    if (!ImGui::Begin("Tilemap Viewer", &m_showTilemapViewer)) { ImGui::End(); return; }

    // Map selection
    ImGui::RadioButton("BG Map", &m_tilemapSelect, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Window Map", &m_tilemapSelect, 1);

    rebuildTilemapTexture();

    constexpr float scale = 1.5f;
    ImVec2 size(256.0f * scale, 256.0f * scale);
    ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
    ImGui::Image(reinterpret_cast<ImTextureID>(m_tilemapTexture), size);

    // Draw SCX/SCY viewport rectangle overlay (160×144 visible area)
    if (m_tilemapSelect == 0) // Only meaningful for BG map
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float scx = static_cast<float>(m_gb.ppu().scx()) * scale;
        float scy = static_cast<float>(m_gb.ppu().scy()) * scale;
        float vpW = 160.0f * scale;
        float vpH = 144.0f * scale;
        ImVec2 vpMin(cursorScreen.x + scx, cursorScreen.y + scy);
        ImVec2 vpMax(vpMin.x + vpW, vpMin.y + vpH);
        drawList->AddRect(vpMin, vpMax, IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);
    }

    // Tooltip with tile coordinates and index
    if (ImGui::IsItemHovered())
    {
        ImVec2 mp = ImGui::GetMousePos();
        int tx = static_cast<int>((mp.x - cursorScreen.x) / (8.0f * scale));
        int ty = static_cast<int>((mp.y - cursorScreen.y) / (8.0f * scale));
        if (tx >= 0 && tx < 32 && ty >= 0 && ty < 32)
        {
            uint8_t lcdc = m_gb.ppu().lcdc();
            uint16_t mapBase = (m_tilemapSelect == 0)
                ? static_cast<uint16_t>((lcdc & 0x08) ? 0x1C00 : 0x1800)
                : static_cast<uint16_t>((lcdc & 0x40) ? 0x1C00 : 0x1800);
            uint8_t tileIdx = m_gb.ppu().rawVRAM()[mapBase + ty * 32 + tx];
            ImGui::SetTooltip("Map[%d,%d] Tile $%02X", tx, ty, tileIdx);
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Main render entry point
// ---------------------------------------------------------------------------

void DebuggerUI::render()
{
    // Global keyboard shortcuts work regardless of which panels are open
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
        m_paused = !m_paused;
    if (m_paused && ImGui::IsKeyPressed(ImGuiKey_F10, true))
        m_stepPending = true;
    if (m_paused && ImGui::IsKeyPressed(ImGuiKey_F6, true))
        m_stepFramePending = true;

    if (m_showControls)      renderControlPanel();
    if (m_showCPURegisters)  renderCPURegisters();
    if (m_showBreakpoints)   renderBreakpoints();
    if (m_showDisassembly)   renderDisassembly();
    if (m_showMemory)        renderMemoryViewer();
    if (m_showPPUState)      renderPPUState();
    if (m_showIORegisters)   renderIORegisters();
    if (m_showOAM)           renderOAMViewer();
    if (m_showTileViewer)    renderTileViewer();
    if (m_showTilemapViewer) renderTilemapViewer();
    if (m_showROMInfo)       renderROMInfo();
    if (m_showAPUDebugger)   renderAPUDebugger();
    if (m_showWatchpoints)   renderWatchpoints();
    if (m_showExecHistory)   renderExecHistory();
    if (m_showEventLog)      renderEventLog();
    if (m_showMemoryLog)     renderMemoryLog();
}

// ---------------------------------------------------------------------------
// ROM Info panel
// ---------------------------------------------------------------------------

void DebuggerUI::renderROMInfo()
{
    if (!ImGui::Begin("ROM Info", &m_showROMInfo)) { ImGui::End(); return; }

    const SeaBoy::Cartridge* cart = m_gb.mmu().cartridge();
    if (!cart)
    {
        ImGui::TextDisabled("No ROM loaded.");
        ImGui::End();
        return;
    }

    const uint8_t* rom = cart->romData();
    const size_t   romSz = cart->romSize();

    // Title - 15 bytes at 0x0134 (older ROMs may use all 15; CGB ROMs use 11)
    char title[16]{};
    if (romSz > 0x0143u)
        std::memcpy(title, rom + 0x0134, 15);
    else if (romSz > 0x0134u)
        std::memcpy(title, rom + 0x0134, romSz - 0x0134u);
    for (int i = 0; i < 15 && title[i]; ++i)
        if (title[i] < 0x20 || title[i] > 0x7E) title[i] = '?';

    // CGB flag
    const char* cgbStr = "DMG Only";
    const uint8_t cgb = cart->cgbFlag();
    if      (cgb == 0xC0) cgbStr = "CGB Only";
    else if (cgb == 0x80) cgbStr = "CGB Compatible";

    // ROM size
    char romSzBuf[24];
    if (romSz >= 1024u * 1024u)
        std::snprintf(romSzBuf, sizeof(romSzBuf), "%zu MB", romSz / (1024u * 1024u));
    else
        std::snprintf(romSzBuf, sizeof(romSzBuf), "%zu KB", romSz / 1024u);

    // RAM size
    const size_t ramSz = cart->sramSize();
    char ramSzBuf[24];
    if      (ramSz == 0)              std::snprintf(ramSzBuf, sizeof(ramSzBuf), "None");
    else if (ramSz >= 1024u * 1024u)  std::snprintf(ramSzBuf, sizeof(ramSzBuf), "%zu MB", ramSz / (1024u * 1024u));
    else                              std::snprintf(ramSzBuf, sizeof(ramSzBuf), "%zu KB", ramSz / 1024u);

    // New licensee code - 2 ASCII bytes at 0x0144-0x0145
    char licensee[3]{};
    if (romSz > 0x0145u)
    {
        licensee[0] = (rom[0x0144] >= 0x20 && rom[0x0144] <= 0x7E) ? static_cast<char>(rom[0x0144]) : '?';
        licensee[1] = (rom[0x0145] >= 0x20 && rom[0x0145] <= 0x7E) ? static_cast<char>(rom[0x0145]) : '?';
    }


    const uint8_t type = cart->typeCode();

    if (ImGui::BeginTable("##rominfo", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        auto row = [](const char* label, const char* value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value);
        };

        row("Title",    title[0] ? title : "(none)");
        row("MBC Type", SeaBoy::Cartridge::typeString(type));
        row("CGB Mode", cgbStr);
        row("ROM Size", romSzBuf);
        row("RAM Size", ramSzBuf);
        row("Battery",  SeaBoy::Cartridge::hasBattery(type) ? "Yes" : "No");
        row("Licensee", licensee[0] ? licensee : "(none)");

        ImGui::EndTable();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Panel: APU Debugger
// ---------------------------------------------------------------------------

namespace
{
    // Duty pattern strings matching kDutyTable in APU.cpp
    static const char* kDutyPatterns[4] = {
        "_______#",  // 12.5%
        "#______#",  // 25%
        "#____###",  // 50%
        "_######_",  // 75%
    };
    static const char* kDutyPercent[4] = { "12.5%", "25%", "50%", "75%" };
    static const char* kCh3OutputLabels[4] = { "Mute", "100%", "50%", "25%" };
}

void DebuggerUI::renderPulseSection(const char* label, bool active, bool dacEnabled,
    uint8_t volume, uint16_t period, uint8_t lengthTimer, bool lengthEnable,
    uint8_t dutyMode, uint8_t dutyStep, const float* oscBuf)
{
    ImGui::SeparatorText(label);
    ImGui::Text("Active: %d  DAC: %d  Vol: %2d  Period: %03X",
        active, dacEnabled, volume, period);
    ImGui::Text("Duty: [%s] %s  Step: %d  Len: %d/64  LenEn: %d",
        kDutyPatterns[dutyMode & 3], kDutyPercent[dutyMode & 3],
        dutyStep, lengthTimer, lengthEnable);

    char plotLabel[16];
    std::snprintf(plotLabel, sizeof(plotLabel), "##osc_%s", label);
    ImGui::PlotLines(plotLabel, oscBuf, kOscBufSize, m_oscOffset,
        nullptr, 0.0f, 1.0f, ImVec2(ImGui::GetContentRegionAvail().x, 40.0f));
}

void DebuggerUI::renderAPUDebugger()
{
    if (!ImGui::Begin("APU Debugger", &m_showAPUDebugger)) { ImGui::End(); return; }

    // Drain per-channel samples from APU into oscilloscope ring buffers
    {
        float tmp[4][256];
        uint32_t got = m_gb.apu().drainChannelSamples(
            tmp[0], tmp[1], tmp[2], tmp[3], 256);
        for (uint32_t i = 0; i < got; ++i)
        {
            for (int ch = 0; ch < 4; ++ch)
                m_oscCh[ch][m_oscOffset] = tmp[ch][i];
            m_oscOffset = (m_oscOffset + 1) % kOscBufSize;
        }
    }

    const auto ds = m_gb.apu().getDebugState();

    // --- Master Control ---
    ImGui::SeparatorText("Master Control");
    ImGui::Text("Power: %s   NR52: %02X", ds.powered ? "ON" : "OFF", ds.nr52);
    ImGui::Text("CH1:%d  CH2:%d  CH3:%d  CH4:%d",
        ds.nr52 & 1, (ds.nr52 >> 1) & 1, (ds.nr52 >> 2) & 1, (ds.nr52 >> 3) & 1);

    uint8_t volL = ((ds.nr50 >> 4) & 7) + 1;
    uint8_t volR = (ds.nr50 & 7) + 1;
    ImGui::Text("NR50: %02X   Vol L: %d  Vol R: %d", ds.nr50, volL, volR);
    ImGui::Text("NR51: %02X   Panning:", ds.nr51);

    static const char* kChNames[4] = { "CH1", "CH2", "CH3", "CH4" };
    for (int i = 0; i < 4; ++i)
    {
        bool l = (ds.nr51 >> (4 + i)) & 1;
        bool r = (ds.nr51 >> i) & 1;
        const char* pan = (l && r) ? "L+R" : l ? "L  " : r ? "  R" : "---";
        ImGui::Text("  %s: %s", kChNames[i], pan);
    }

    // --- CH1: Pulse + Sweep ---
    renderPulseSection("CH1: Pulse + Sweep",
        ds.ch1Active, ds.ch1DacEnabled, ds.ch1Volume, ds.ch1Period,
        ds.ch1LengthTimer, ds.ch1LengthEnable, ds.ch1DutyMode, ds.ch1DutyStep,
        m_oscCh[0]);

    uint8_t sweepPace = (ds.nr10 >> 4) & 0x07u;
    uint8_t sweepDir  = (ds.nr10 >> 3) & 0x01u;
    uint8_t sweepStep = ds.nr10 & 0x07u;
    ImGui::Text("  Sweep  NR10:%02X  pace=%d  dir=%s  step=%d  en=%d  shadow=%03X",
        ds.nr10, sweepPace, sweepDir ? "sub" : "add", sweepStep,
        static_cast<int>(ds.sweepEnabled), ds.sweepShadow);

    // --- CH2: Pulse ---
    renderPulseSection("CH2: Pulse",
        ds.ch2Active, ds.ch2DacEnabled, ds.ch2Volume, ds.ch2Period,
        ds.ch2LengthTimer, ds.ch2LengthEnable, ds.ch2DutyMode, ds.ch2DutyStep,
        m_oscCh[1]);

    // --- CH3: Wave ---
    ImGui::SeparatorText("CH3: Wave");
    ImGui::Text("Active: %d  DAC: %d  Period: %03X  SampleIdx: %d",
        ds.ch3Active, ds.ch3DacEnabled, ds.ch3Period, ds.ch3SampleIndex);
    ImGui::Text("Output: %s  Len: %d/256  LenEn: %d",
        kCh3OutputLabels[ds.ch3OutputLevel & 3], ds.ch3LengthTimer, ds.ch3LengthEnable);

    // Wave RAM: 32 nibbles decoded from 16 bytes
    float waveNibs[32];
    for (int i = 0; i < 32; ++i)
    {
        uint8_t byte = ds.waveRam[i >> 1];
        uint8_t nib  = (i & 1) ? (byte & 0x0Fu) : (byte >> 4);
        waveNibs[i]  = static_cast<float>(nib) / 15.0f;
    }
    ImGui::Text("Wave RAM:");
    ImGui::PlotLines("##waveram", waveNibs, 32, 0,
        nullptr, 0.0f, 1.0f, ImVec2(ImGui::GetContentRegionAvail().x, 50.0f));

    // Hex dump of wave RAM (2 rows of 8 bytes)
    char hexRow[48];
    std::snprintf(hexRow, sizeof(hexRow),
        "%02X %02X %02X %02X %02X %02X %02X %02X",
        ds.waveRam[0], ds.waveRam[1], ds.waveRam[2], ds.waveRam[3],
        ds.waveRam[4], ds.waveRam[5], ds.waveRam[6], ds.waveRam[7]);
    ImGui::TextUnformatted(hexRow);
    std::snprintf(hexRow, sizeof(hexRow),
        "%02X %02X %02X %02X %02X %02X %02X %02X",
        ds.waveRam[8],  ds.waveRam[9],  ds.waveRam[10], ds.waveRam[11],
        ds.waveRam[12], ds.waveRam[13], ds.waveRam[14], ds.waveRam[15]);
    ImGui::TextUnformatted(hexRow);

    // CH3 oscilloscope
    ImGui::PlotLines("##oscch3", m_oscCh[2], kOscBufSize, m_oscOffset,
        nullptr, 0.0f, 1.0f, ImVec2(ImGui::GetContentRegionAvail().x, 40.0f));

    // --- CH4: Noise ---
    ImGui::SeparatorText("CH4: Noise");
    uint8_t clkShift = (ds.nr43 >> 4) & 0x0Fu;
    uint8_t lfsrMode = (ds.nr43 >> 3) & 0x01u;
    uint8_t divisor  = ds.nr43 & 0x07u;
    ImGui::Text("Active: %d  DAC: %d  Vol: %2d  Len: %d/64  LenEn: %d",
        ds.ch4Active, ds.ch4DacEnabled, ds.ch4Volume, ds.ch4LengthTimer, ds.ch4LengthEnable);
    ImGui::Text("NR43: %02X   ClkShift: %d  LFSR: %s  Divisor: %d",
        ds.nr43, clkShift, lfsrMode ? "7-bit" : "15-bit", divisor);
    ImGui::Text("LFSR state: %04X", ds.lfsr);
    ImGui::PlotLines("##oscch4", m_oscCh[3], kOscBufSize, m_oscOffset,
        nullptr, 0.0f, 1.0f, ImVec2(ImGui::GetContentRegionAvail().x, 40.0f));

    // --- Frame Sequencer ---
    ImGui::SeparatorText("Frame Sequencer");
    ImGui::Text("Step: %d  (next: %s)",
        ds.frameSeqStep,
        ds.frameSeqStep == 7 ? "env" :
        (ds.frameSeqStep == 2 || ds.frameSeqStep == 6) ? "len+sweep" :
        (ds.frameSeqStep % 2 == 0) ? "len" : "---");

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Panel: Watchpoints (data breakpoints)
// ---------------------------------------------------------------------------

void DebuggerUI::renderWatchpoints()
{
    static const char* kTypeLabels[]          = { "Read", "Write", "Read+Write" };
    static const SeaBoy::WatchType kTypeValues[] = {
        SeaBoy::WatchType::Read,
        SeaBoy::WatchType::Write,
        SeaBoy::WatchType::ReadWrite,
    };

    const char* titleSuffix = m_watchHitActive ? "Watchpoints (!)###Watchpoints" : "Watchpoints###Watchpoints";
    if (!ImGui::Begin(titleSuffix, &m_showWatchpoints)) { ImGui::End(); return; }

    // --- Add row ---
    ImGui::SetNextItemWidth(60.0f);
    bool enter = ImGui::InputText("##wp_addr", m_wpInputBuf, sizeof(m_wpInputBuf),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::Combo("##wp_type", &m_wpTypeCombo, kTypeLabels, 3);
    ImGui::SameLine();
    if (ImGui::Button("Add") || enter)
    {
        if (m_wpInputBuf[0] != '\0')
        {
            uint16_t addr = static_cast<uint16_t>(std::strtol(m_wpInputBuf, nullptr, 16));
            SeaBoy::WatchType wtype = kTypeValues[m_wpTypeCombo];
            // Update or append in UI list
            bool found = false;
            for (auto& e : m_watchpoints)
            {
                if (e.addr == addr) { e.type = wtype; found = true; break; }
            }
            if (!found)
                m_watchpoints.push_back({ addr, wtype });
            m_gb.addWatchpoint({ addr, wtype });
            m_wpInputBuf[0] = '\0';
        }
    }

    // --- Active watchpoints list ---
    ImGui::SeparatorText("Active");

    int removeIdx = -1;
    for (int i = 0; i < static_cast<int>(m_watchpoints.size()); ++i)
    {
        ImGui::PushID(i);
        const auto& e = m_watchpoints[i];
        // Derive combo index from type
        int tidx = (e.type == SeaBoy::WatchType::Read) ? 0 :
                   (e.type == SeaBoy::WatchType::Write) ? 1 : 2;
        ImGui::Text("%04X  %-10s", e.addr, kTypeLabels[tidx]);
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
            removeIdx = i;
        ImGui::PopID();
    }
    if (removeIdx >= 0)
    {
        m_gb.removeWatchpoint(m_watchpoints[removeIdx].addr);
        m_watchpoints.erase(m_watchpoints.begin() + removeIdx);
    }

    if (m_watchpoints.empty())
        ImGui::TextDisabled("(none)");

    if (!m_watchpoints.empty())
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear All"))
        {
            m_gb.clearWatchpoints();
            m_watchpoints.clear();
        }
    }

    // --- Last hit ---
    ImGui::SeparatorText("Last Hit");
    if (m_watchHitActive)
    {
        const char* hitType = (m_lastWatchHit.type == SeaBoy::WatchType::Read) ? "Read" : "Write";
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
            "Addr: %04X  Type: %s  Val: %02X  PC: %04X",
            m_lastWatchHit.addr, hitType,
            m_lastWatchHit.value, m_lastWatchHit.pc);
        if (ImGui::SmallButton("Clear Hit"))
            m_watchHitActive = false;
    }
    else
    {
        ImGui::TextDisabled("(no hit yet)");
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Execution History Ring Buffer
// ---------------------------------------------------------------------------

void DebuggerUI::onExecSnapshot(void* ctx, const SeaBoy::GameBoy::ExecSnapshot& snap)
{
    auto* self = static_cast<DebuggerUI*>(ctx);
    if (!self->m_historyEnabled) return;
    self->m_historyBuf[self->m_historyHead] = snap;
    self->m_historyHead = (self->m_historyHead + 1) % kHistorySize;
    if (self->m_historyCount < kHistorySize) ++self->m_historyCount;
}

void DebuggerUI::renderExecHistory()
{
    if (!ImGui::Begin("Execution History", &m_showExecHistory)) { ImGui::End(); return; }

    // --- Controls ---
    if (ImGui::Checkbox("Record", &m_historyEnabled) && m_historyEnabled)
    {
        // Clear stale entries when re-enabling so display starts fresh
        m_historyHead  = 0;
        m_historyCount = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        m_historyHead  = 0;
        m_historyCount = 0;
    }
    ImGui::SameLine();
    ImGui::Text("%d / %d", m_historyCount, kHistorySize);

    ImGui::Separator();

    // --- Table ---
    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_ScrollY       |
        ImGuiTableFlags_RowBg         |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##exec_hist", 6, tableFlags,
            ImVec2(0.0f, 0.0f))) // stretch to window
    {
        ImGui::End();
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed,  42.0f);
    ImGui::TableSetupColumn("PC",       ImGuiTableColumnFlags_WidthFixed,  46.0f);
    ImGui::TableSetupColumn("Op",       ImGuiTableColumnFlags_WidthFixed,  28.0f);
    ImGui::TableSetupColumn("Mnemonic", ImGuiTableColumnFlags_WidthFixed, 200.0f);
    ImGui::TableSetupColumn("Regs",     ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Flags",    ImGuiTableColumnFlags_WidthFixed,  70.0f);
    ImGui::TableHeadersRow();

    char mnemonicBuf[64];
    for (int i = 0; i < m_historyCount; ++i)
    {
        int slot = (m_historyHead - m_historyCount + i + kHistorySize) % kHistorySize;
        const auto& s = m_historyBuf[slot];
        bool isNewest = (i == m_historyCount - 1);

        ImGui::TableNextRow();

        if (isNewest)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(ImVec4(0.2f, 0.55f, 0.2f, 0.4f)));

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%d", (m_historyHead - m_historyCount + i + kHistorySize) % 10000);

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%04X", s.pc);

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%02X", s.opcode);

        ImGui::TableSetColumnIndex(3);
        disassemble(s.pc, mnemonicBuf, sizeof(mnemonicBuf));
        ImGui::TextUnformatted(mnemonicBuf);

        ImGui::TableSetColumnIndex(4);
        ImGui::Text("AF:%04X BC:%04X DE:%04X HL:%04X SP:%04X",
            s.regs.getAF(), s.regs.getBC(), s.regs.getDE(), s.regs.getHL(), s.regs.SP);

        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%c%c%c%c %s%s",
            s.regs.flagZ() ? 'Z' : '-',
            s.regs.flagN() ? 'N' : '-',
            s.regs.flagH() ? 'H' : '-',
            s.regs.flagC() ? 'C' : '-',
            s.ime    ? "I" : "-",
            s.halted ? "H" : "-");

        // if (isNewest)
        //     ImGui::SetScrollHereY(1.0f); // keep newest visible when running
    }

    // When paused, don't auto-scroll - let user browse freely
    // (SetScrollHereY on the last row above handles the running case)

    ImGui::EndTable();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Event / Interrupt Log
// ---------------------------------------------------------------------------

void DebuggerUI::onGameEvent(void* ctx, const SeaBoy::GameBoy::GameEvent& ev)
{
    auto* ui = static_cast<DebuggerUI*>(ctx);
    if (!ui->m_eventEnabled) return;

    ui->m_eventBuf[ui->m_eventHead] = ev;
    ui->m_eventHead = (ui->m_eventHead + 1) % kEventBufSize;
    if (ui->m_eventCount < kEventBufSize)
        ++ui->m_eventCount;

    // Pause on serviced interrupt if requested
    using EK = SeaBoy::GameBoy::EventKind;
    if (ev.kind == EK::IntServiced && ev.param < 5 && ui->m_pauseOnIRQ[ev.param])
        ui->m_paused = true;
}

void DebuggerUI::renderEventLog()
{
    if (!ImGui::Begin("Event Log", &m_showEventLog)) { ImGui::End(); return; }

    using EK = SeaBoy::GameBoy::EventKind;

    static const char* kKindNames[] = {
        "IntReq", "IntSvc", "IMEen", "IMEdis", "Halt+", "Halt-", "RETI"
    };
    static const char* kIRQNames[] = { "VBlank", "STAT", "Timer", "Serial", "Joypad" };

    // --- Controls ---
    if (ImGui::Checkbox("Record", &m_eventEnabled) && m_eventEnabled)
    {
        m_eventHead  = 0;
        m_eventCount = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { m_eventHead = 0; m_eventCount = 0; }
    ImGui::SameLine();
    ImGui::Text("%d / %d", m_eventCount, kEventBufSize);

    // Per-kind filter toggles
    ImGui::Separator();
    ImGui::TextUnformatted("Filter:");
    for (int k = 0; k < 7; ++k)
    {
        ImGui::SameLine();
        ImGui::Checkbox(kKindNames[k], &m_eventFilter[k]);
    }

    // Pause-on-IRQ row
    ImGui::TextUnformatted("Pause on:");
    for (int b = 0; b < 5; ++b)
    {
        ImGui::SameLine();
        ImGui::Checkbox(kIRQNames[b], &m_pauseOnIRQ[b]);
    }

    ImGui::Separator();

    // --- Table ---
    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_ScrollY       |
        ImGuiTableFlags_RowBg         |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##evlog", 5, tableFlags, ImVec2(0.0f, 0.0f)))
    {
        ImGui::End();
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Cycle",   ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("PC",      ImGuiTableColumnFlags_WidthFixed,  48.0f);
    ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_WidthFixed,  60.0f);
    ImGui::TableSetupColumn("Detail",  ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("IE  IF",  ImGuiTableColumnFlags_WidthFixed,  70.0f);
    ImGui::TableHeadersRow();

    bool scrollToBottom = false;
    for (int i = 0; i < m_eventCount; ++i)
    {
        int slot = (m_eventHead - m_eventCount + i + kEventBufSize) % kEventBufSize;
        const auto& ev = m_eventBuf[slot];
        int kindIdx = static_cast<int>(ev.kind);
        if (kindIdx < 0 || kindIdx > 6) continue;
        if (!m_eventFilter[kindIdx]) continue;

        bool isNewest = (i == m_eventCount - 1);

        ImGui::TableNextRow();

        // Row highlight: green for IntServiced, yellow for IntRequested
        if (ev.kind == EK::IntServiced)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(ImVec4(0.2f, 0.55f, 0.2f, 0.4f)));
        else if (ev.kind == EK::IntRequested)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(ImVec4(0.55f, 0.55f, 0.15f, 0.4f)));
        else if (ev.kind == EK::HaltEnter)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.55f, 0.4f)));

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%llu", static_cast<unsigned long long>(ev.cycle));

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%04X", ev.pc);

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(kKindNames[kindIdx]);

        ImGui::TableSetColumnIndex(3);
        if (ev.kind == EK::IntRequested || ev.kind == EK::IntServiced)
        {
            ImGui::Text("%s (bit%u)", ev.param < 5 ? kIRQNames[ev.param] : "?", ev.param);
        }
        else if (ev.kind == EK::HaltExit)
        {
            // param = pending IF & IE bits; list which IRQs are pending
            char detail[48] = "";
            for (uint8_t b = 0; b < 5; ++b)
                if (ev.param & (1u << b))
                {
                    if (detail[0]) std::strncat(detail, ",", sizeof(detail) - std::strlen(detail) - 1);
                    std::strncat(detail, kIRQNames[b], sizeof(detail) - std::strlen(detail) - 1);
                }
            ImGui::TextUnformatted(detail);
        }

        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%02X  %02X", ev.ie, ev.ifReg);

        // if (isNewest) scrollToBottom = true;
    }

    // if (scrollToBottom)
    //     ImGui::SetScrollHereY(1.0f);

    ImGui::EndTable();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Memory Write Log
// ---------------------------------------------------------------------------

void DebuggerUI::onMemWrite(void* ctx, uint16_t addr, uint8_t prevVal, uint8_t newVal)
{
    auto* ui = static_cast<DebuggerUI*>(ctx);
    if (!ui->m_memLogEnabled) return;

    // Address filter
    if (ui->m_memLogFilterEnabled && ui->m_memLogAddrFilterBuf[0] != '\0')
    {
        uint16_t filter = static_cast<uint16_t>(std::strtol(ui->m_memLogAddrFilterBuf, nullptr, 16));
        if (addr != filter) return;
    }

    // Optionally skip no-op writes (prev == new)
    if (ui->m_memLogHideNoOp && prevVal == newVal) return;

    MemLogEntry entry;
    entry.cycle   = ui->m_gb.totalCycles();
    entry.pc      = ui->m_gb.cpu().registers().PC;
    entry.addr    = addr;
    entry.prevVal = prevVal;
    entry.newVal  = newVal;

    ui->m_memLogBuf[ui->m_memLogHead] = entry;
    ui->m_memLogHead = (ui->m_memLogHead + 1) % kMemLogSize;
    if (ui->m_memLogCount < kMemLogSize)
        ++ui->m_memLogCount;
}

void DebuggerUI::renderMemoryLog()
{
    if (!ImGui::Begin("Memory Log", &m_showMemoryLog)) { ImGui::End(); return; }

    // --- Controls ---
    if (ImGui::Checkbox("Record", &m_memLogEnabled) && m_memLogEnabled)
    {
        m_memLogHead  = 0;
        m_memLogCount = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { m_memLogHead = 0; m_memLogCount = 0; }
    ImGui::SameLine();
    ImGui::Text("%d / %d", m_memLogCount, kMemLogSize);
    ImGui::SameLine();
    ImGui::Checkbox("Hide no-op", &m_memLogHideNoOp);

    ImGui::Checkbox("Filter addr", &m_memLogFilterEnabled);
    if (m_memLogFilterEnabled)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::InputText("##memlog_filter", m_memLogAddrFilterBuf, sizeof(m_memLogAddrFilterBuf),
            ImGuiInputTextFlags_CharsHexadecimal);
    }

    ImGui::Separator();

    // --- Table ---
    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_ScrollY       |
        ImGuiTableFlags_RowBg         |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##memlog", 6, tableFlags, ImVec2(0.0f, 0.0f)))
    {
        ImGui::End();
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Cycle",  ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("PC",     ImGuiTableColumnFlags_WidthFixed,  48.0f);
    ImGui::TableSetupColumn("Addr",   ImGuiTableColumnFlags_WidthFixed,  48.0f);
    ImGui::TableSetupColumn("Prev",   ImGuiTableColumnFlags_WidthFixed,  36.0f);
    ImGui::TableSetupColumn("New",    ImGuiTableColumnFlags_WidthFixed,  36.0f);
    ImGui::TableSetupColumn("Delta",  ImGuiTableColumnFlags_WidthFixed,  50.0f);
    ImGui::TableHeadersRow();

    bool scrollToBottom = false;
    for (int i = 0; i < m_memLogCount; ++i)
    {
        int slot = (m_memLogHead - m_memLogCount + i + kMemLogSize) % kMemLogSize;
        const auto& e = m_memLogBuf[slot];

        ImGui::TableNextRow();

        const int delta = static_cast<int>(e.newVal) - static_cast<int>(e.prevVal);
        if (delta > 0)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(ImVec4(0.15f, 0.45f, 0.15f, 0.4f)));
        else if (delta < 0)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(ImVec4(0.45f, 0.15f, 0.15f, 0.4f)));

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%llu", static_cast<unsigned long long>(e.cycle));

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%04X", e.pc);

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%04X", e.addr);

        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%02X", e.prevVal);

        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%02X", e.newVal);

        ImGui::TableSetColumnIndex(5);
        if (delta > 0)      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "+%d", delta);
        else if (delta < 0) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d",  delta);
        else                ImGui::TextDisabled("=");

        if (i == m_memLogCount - 1) scrollToBottom = true;
    }

    if (scrollToBottom)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndTable();
    ImGui::End();
}
