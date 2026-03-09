#include "DebuggerUI.h"

#include <algorithm>
#include <cstdlib>
#include "imgui.h"
#include "src/core/GameBoy.hpp"

DebuggerUI::DebuggerUI(SeaBoy::GameBoy& gb, SDL_Renderer* renderer)
    : m_gb(gb)
    , m_renderer(renderer)
{
}

DebuggerUI::~DebuggerUI() = default;

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

bool DebuggerUI::checkBreakpoints(uint16_t pc) const
{
    return std::find(m_breakpoints.begin(), m_breakpoints.end(), pc) != m_breakpoints.end();
}

// ---------------------------------------------------------------------------
// Panel: Execution Controls
// ---------------------------------------------------------------------------

void DebuggerUI::renderControlPanel()
{
    if (!ImGui::Begin("Controls")) { ImGui::End(); return; }

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
    }

    // Keyboard shortcuts (work even when panel not focused)
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
        m_paused = !m_paused;
    if (m_paused && ImGui::IsKeyPressed(ImGuiKey_F10, false))
        m_stepPending = true;
    if (m_paused && ImGui::IsKeyPressed(ImGuiKey_F6, false))
        m_stepFramePending = true;

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Panel: CPU Registers
// ---------------------------------------------------------------------------

void DebuggerUI::renderCPURegisters()
{
    if (!ImGui::Begin("CPU Registers")) { ImGui::End(); return; }

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
    if (!ImGui::Begin("Breakpoints")) { ImGui::End(); return; }

    ImGui::SetNextItemWidth(60.0f);
    bool enter = ImGui::InputText("##bp_addr", m_bpInputBuf, sizeof(m_bpInputBuf),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Add") || enter)
    {
        if (m_bpInputBuf[0] != '\0')
        {
            uint16_t addr = static_cast<uint16_t>(std::strtol(m_bpInputBuf, nullptr, 16));
            // Avoid duplicates
            if (std::find(m_breakpoints.begin(), m_breakpoints.end(), addr) == m_breakpoints.end())
                m_breakpoints.push_back(addr);
            m_bpInputBuf[0] = '\0';
        }
    }

    ImGui::Separator();

    int removeIdx = -1;
    for (int i = 0; i < static_cast<int>(m_breakpoints.size()); ++i)
    {
        ImGui::PushID(i);
        ImGui::Text("%04X", m_breakpoints[i]);
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
            removeIdx = i;
        ImGui::PopID();
    }
    if (removeIdx >= 0)
        m_breakpoints.erase(m_breakpoints.begin() + removeIdx);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Main render entry point
// ---------------------------------------------------------------------------

void DebuggerUI::render()
{
    renderControlPanel();
    renderCPURegisters();
    renderBreakpoints();
}
