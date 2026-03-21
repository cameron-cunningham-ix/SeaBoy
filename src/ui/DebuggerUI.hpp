#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include "src/core/MMU.hpp"
#include "src/core/GameBoy.hpp"

struct SDL_Renderer;
struct SDL_Texture;

/// Debugger overlay - dockable ImGui panels for inspecting emulator state.
/// Constructed in main.cpp; render() is called each frame from UIPlatform::renderUI().
class DebuggerUI
{
public:
    DebuggerUI(SeaBoy::GameBoy& gb, SDL_Renderer* renderer);
    ~DebuggerUI();

    // Draw all debugger panels. Called between ImGui::NewFrame and ImGui::Render.
    void render();

    // Execution control - queried by main loop
    bool isPaused() const { return m_paused; }
    void pause()          { m_paused = true; }

    // Returns true once and clears the step flag.
    bool consumeStep();

    // Returns true once and clears the step-frame flag.
    bool consumeStepFrame();

    // Returns the requested instruction count once and clears the flag, or 0 if not pending.
    int consumeStepNInstr();

    // Returns true if PC matches an active breakpoint whose condition (if any) passes.
    // Non-const: increments hitCount on each PC match.
    bool checkBreakpoints(uint16_t pc);

    // Pure address lookup - used by disassembly panel for the '*' marker.
    bool isBreakpointAddr(uint16_t addr) const;

    // Fast check to skip breakpoint scanning when none are set.
    bool breakpointsEmpty() const { return m_breakpoints.empty(); }

    // Called by main.cpp when a data watchpoint fires. Stores the hit for display.
    void notifyWatchHit(const SeaBoy::WatchHit& hit)
        { m_watchHitActive = true; m_lastWatchHit = hit; }

    // Panel visibility - toggled from Window menu and panel X buttons
    bool m_showGame          = true;
    bool m_showControls      = false;
    bool m_showCPURegisters  = false;
    bool m_showBreakpoints   = false;
    bool m_showDisassembly   = false;
    bool m_showMemory        = false;
    bool m_showPPUState      = false;
    bool m_showIORegisters   = false;
    bool m_showOAM           = false;
    bool m_showTileViewer    = false;
    bool m_showTilemapViewer = false;
    bool m_showROMInfo       = false;
    bool m_showAPUDebugger   = false;
    bool m_showWatchpoints   = false;
    bool m_showExecHistory   = false;
    bool m_showEventLog      = false;
    bool m_showMemoryLog     = false;

private:
    SeaBoy::GameBoy& m_gb;
    SDL_Renderer*     m_renderer;

    // Execution state
    bool m_paused           = false;
    bool m_stepPending      = false;
    bool m_stepFramePending = false;
    bool m_stepNInstrPending = false;
    int  m_stepNInstr       = 1;    // instructions to step per "Step N instr" press

    // FPS tracking - average over 1-second windows
    uint32_t m_fpsFrameCount = 0;
    float    m_fpsAccum      = 0.0f; // accumulated seconds since last update
    float    m_fpsDisplay    = 0.0f; // last computed 1-second average

    // Breakpoints
    enum class BPOperand : uint8_t {
        RegA, RegB, RegC, RegD, RegE, RegH, RegL, RegF,   // 8-bit
        RegAF, RegBC, RegDE, RegHL, RegSP, RegPC,           // 16-bit pairs
        Mem8,   // peek8(lhsAddr)
        Mem16,  // peek8(lhsAddr) | peek8(lhsAddr+1)<<8
    };
    enum class BPCondOp : uint8_t { EQ, NE, LT, LE, GT, GE };

    struct Breakpoint {
        uint16_t  addr;
        bool      enabled      = true;
        bool      hasCondition = false;
        BPOperand lhsOperand   = BPOperand::RegA;
        uint16_t  lhsAddr      = 0;      // used when lhsOperand == Mem8/Mem16
        BPCondOp  condOp       = BPCondOp::EQ;
        uint16_t  rhsValue     = 0;
        uint32_t  hitCount     = 0;      // incremented on every PC match
        uint32_t  hitTarget    = 0;      // 0=every hit; N=fire only when hitCount==N
    };

    std::vector<Breakpoint> m_breakpoints;

    // Add-dialog state
    char     m_bpInputBuf[8]{};
    bool     m_bpAddCond         = false;
    int      m_bpAddOperandIdx   = 0;
    char     m_bpAddLhsAddrBuf[8]{};
    int      m_bpAddOpIdx        = 0;
    char     m_bpAddRhsBuf[8]{};
    uint32_t m_bpAddHitTarget    = 0;

    // Inline edit state
    int      m_bpEditIdx         = -1;   // which row is expanded (-1=none)
    bool     m_bpEditCond        = false;
    int      m_bpEditOperandIdx  = 0;
    char     m_bpEditLhsAddrBuf[8]{};
    int      m_bpEditOpIdx       = 0;
    char     m_bpEditRhsBuf[8]{};
    uint32_t m_bpEditHitTarget   = 0;

    bool evalCondition(const Breakpoint& bp) const;

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
    void renderROMInfo();

    // Data watchpoints
    struct WatchpointEntry { uint16_t addr; SeaBoy::WatchType type; };
    std::vector<WatchpointEntry> m_watchpoints;
    char m_wpInputBuf[8]{};
    int  m_wpTypeCombo = 2; // default: Read+Write (index 2)
    bool             m_watchHitActive = false;
    SeaBoy::WatchHit m_lastWatchHit{};

    void renderWatchpoints();

    // Execution history ring buffer
    static constexpr int kHistorySize = 256;
    std::array<SeaBoy::GameBoy::ExecSnapshot, kHistorySize> m_historyBuf{};
    int  m_historyHead    = 0;   // next-write slot
    int  m_historyCount   = 0;   // valid entries, capped at kHistorySize
    bool m_historyEnabled = false;

    static void onExecSnapshot(void* ctx, const SeaBoy::GameBoy::ExecSnapshot& snap);
    void renderExecHistory();

    // Event / interrupt log
    static constexpr int kEventBufSize = 512;
    std::array<SeaBoy::GameBoy::GameEvent, kEventBufSize> m_eventBuf{};
    int  m_eventHead    = 0;
    int  m_eventCount   = 0;
    bool m_eventEnabled = false;
    // Per-kind filter (indexed by EventKind cast to int, 0-6)
    bool m_eventFilter[7]{ true, true, true, true, true, true, true };
    // Pause emulation when IRQ bit N is serviced (IntServiced event)
    bool m_pauseOnIRQ[5]{};

    static void onGameEvent(void* ctx, const SeaBoy::GameBoy::GameEvent& ev);
    void renderEventLog();

    // Memory write log
    struct MemLogEntry {
        uint64_t cycle;
        uint16_t pc;
        uint16_t addr;
        uint8_t  prevVal;
        uint8_t  newVal;
    };
    static constexpr int kMemLogSize = 256;
    std::array<MemLogEntry, kMemLogSize> m_memLogBuf{};
    int  m_memLogHead    = 0;
    int  m_memLogCount   = 0;
    bool m_memLogEnabled = false;
    bool m_memLogFilterEnabled  = false;
    char m_memLogAddrFilterBuf[8]{};
    bool m_memLogHideNoOp       = true;   // hide writes where prevVal == newVal

    static void onMemWrite(void* ctx, uint16_t addr, uint8_t prevVal, uint8_t newVal);
    void renderMemoryLog();

    // APU oscilloscope - per-channel ring buffers (256 samples each, written each frame)
    static constexpr int kOscBufSize = 256;
    float m_oscCh[4][kOscBufSize]{};
    int   m_oscOffset = 0;

    void renderAPUDebugger();
    void renderPulseSection(const char* label, bool active, bool dacEnabled,
        uint8_t volume, uint16_t period, uint8_t lengthTimer, bool lengthEnable,
        uint8_t dutyMode, uint8_t dutyStep, const float* oscBuf);

    // Disassembler - decode one instruction at addr, return byte length
    uint8_t disassemble(uint16_t addr, char* outBuf, int bufSize) const;
};
