#pragma once

#include <cstdint>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace SeaBoy { class GameBoy; }

/// Debugger overlay — dockable ImGui panels for inspecting emulator state.
/// Constructed in main.cpp; render() is called each frame from UIPlatform::renderUI().
class DebuggerUI
{
public:
    DebuggerUI(SeaBoy::GameBoy& gb, SDL_Renderer* renderer);
    ~DebuggerUI();

    // Draw all debugger panels. Called between ImGui::NewFrame and ImGui::Render.
    void render();

    // Execution control — queried by main loop
    bool isPaused() const { return m_paused; }
    void pause()          { m_paused = true; }

    // Returns true once and clears the step flag.
    bool consumeStep();

    // Returns true once and clears the step-frame flag.
    bool consumeStepFrame();

    // Returns true if PC matches any active breakpoint.
    bool checkBreakpoints(uint16_t pc) const;

    // Fast check to skip breakpoint scanning when none are set.
    bool breakpointsEmpty() const { return m_breakpoints.empty(); }

private:
    SeaBoy::GameBoy& m_gb;
    SDL_Renderer*     m_renderer;

    // Execution state
    bool m_paused       = false;
    bool m_stepPending  = false;
    bool m_stepFramePending = false;

    // Breakpoints
    std::vector<uint16_t> m_breakpoints;
    char m_bpInputBuf[8]{};

    // Memory viewer state
    char m_memAddrBuf[8]{};

    // Tile/tilemap viewer textures (created lazily)
    SDL_Texture* m_tileTexture    = nullptr; // 128×192 (16×24 tiles)
    SDL_Texture* m_tilemapTexture = nullptr; // 256×256 (32×32 tiles)
    int  m_tilemapSelect = 0; // 0 = BG (9800), 1 = Window (9C00)

    void rebuildTileTexture();
    void rebuildTilemapTexture();

    // Panel rendering
    void renderControlPanel();
    void renderCPURegisters();
    void renderBreakpoints();
    void renderDisassembly();
    void renderMemoryViewer();
    void renderPPUState();
    void renderIORegisters();
    void renderOAMViewer();
    void renderTileViewer();
    void renderTilemapViewer();

    // Disassembler — decode one instruction at addr, return byte length
    uint8_t disassemble(uint16_t addr, char* outBuf, int bufSize) const;
};
