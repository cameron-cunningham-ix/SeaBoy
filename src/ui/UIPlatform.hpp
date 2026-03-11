#include <iostream>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder API for layout save/load
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "nfd.h"
#include "src/core/GameBoy.hpp"
#include "src/core/SaveState.hpp"
#include "src/cartridge/Cartridge.hpp"
#include "src/ui/DebuggerUI.hpp"

// Key bindings for the 8 GameBoy buttons.
// Stored as a plain struct so SettingsUI can read/write individual fields.
// Default mapping: Z=A, X=B, Return=Start, RShift=Select, arrow keys=D-pad.
struct JoypadBindings
{
    SDL_Scancode a      = SDL_SCANCODE_Z;
    SDL_Scancode b      = SDL_SCANCODE_X;
    SDL_Scancode start  = SDL_SCANCODE_RETURN;
    SDL_Scancode select = SDL_SCANCODE_RSHIFT;
    SDL_Scancode up     = SDL_SCANCODE_UP;
    SDL_Scancode down   = SDL_SCANCODE_DOWN;
    SDL_Scancode left   = SDL_SCANCODE_LEFT;
    SDL_Scancode right  = SDL_SCANCODE_RIGHT;
};

/// @brief SDL + ImGui platform for rendering and input
class UIPlatform
{
private:
    unsigned int* frameBuffer = nullptr;
    int windowWidth;
    int windowHeight;
    float mainScale;
    float frameRate = 59.7f; // GB native ~59.7 Hz
    int textureWidth;
    int textureHeight;

    // Keybinding rebind state
    int m_rebindingIndex = -1; // -1 = not rebinding, 0–7 = which button

    // Layout save/load state
    std::string m_currentLayoutName;
    std::string m_pendingLayoutLoad; // deferred load — applied before next DockSpace()
    bool m_openSaveLayoutPopup = false;
    char m_layoutNameBuf[128]{};

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    
public:
    // Key bindings - public so SettingsUI can read and modify them at runtime.
    JoypadBindings m_bindings;

    // ROM path requested via File > Open ROM. Consumed by main loop.
    std::string m_pendingROMPath;

    // Path of currently loaded ROM (for Restart).
    std::string m_currentROMPath;

    // Restart flag — signals main loop to reload current ROM.
    bool m_pendingRestart = false;

    // Debugger reference for Window menu toggles. Set via setDebugger().
    DebuggerUI* m_debugger = nullptr;
    void setDebugger(DebuggerUI* dbg) { m_debugger = dbg; }

    // GameBoy reference for palette changes. Set via setGameBoy().
    SeaBoy::GameBoy* m_gameBoy = nullptr;
    void setGameBoy(SeaBoy::GameBoy* gb) { m_gameBoy = gb; }

    /// @brief
    /// @param title Title of SDL window created, appears at top
    /// @param windowWidth SDL window width including UI
    /// @param windowHeight SDL window height including UI
    /// @param textureWidth Width of texture
    /// @param textureHeight Height of texture
    UIPlatform(char* title, int windowWidth, int windowHeight, int textureWidth, int textureHeight)
    {
        // SDL Initializations
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO))
        {
            SDL_GetError();
            std::cerr << "SDL failed to initialize\n";
        }

        mainScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;

        window = SDL_CreateWindow(title, (int)windowWidth*mainScale, (int)windowHeight*mainScale, windowFlags);
        if (!window)
        {
            SDL_GetError();
            std::cerr << "SDL failed to initialize the window\n";
        }
        this->windowWidth = windowWidth*mainScale;
        this->windowHeight = windowHeight*mainScale;
        
        renderer = SDL_CreateRenderer(window, NULL);
        if (!renderer)
        {
            SDL_GetError();
            std::cerr << "SDL failed to initialize the renderer\n";
        }

        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_ShowWindow(window);
        
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STREAMING, textureWidth, textureHeight);
        if (!texture)
        {
            SDL_GetError();
            std::cerr << "SDL failed to initialize the texture\n";
        }
        this->textureWidth = textureWidth;
        this->textureHeight = textureHeight;
        // Sets texture scale mode to nearest, making it pixel perfect instead of blurry
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

        // Allocate framebuffer
        frameBuffer = (unsigned int*)calloc(textureWidth * textureHeight, sizeof(unsigned int));
        if (!frameBuffer)
        {
            SDL_GetError();
            std::cerr << "SDL failed to allocate framebuffer\n";
        }
        // this->textureScale = textureScale;
        SDL_Log("SDL Initialized\n");

        // ImGui Initialization
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
        io.IniFilename = nullptr; // We manage layouts ourselves

        ImFontConfig fontConfig;
        ImFont* roboto = io.Fonts->AddFontFromFileTTF("fonts/Roboto/Roboto-Regular.ttf", 16.0f * mainScale, &fontConfig);

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();

        // Setup scaling
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(mainScale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
        style.FontScaleDpi = mainScale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

        // Setup Platform/Renderer backends
        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        // Initialize NFD
        if (NFD_Init() != NFD_OKAY)
        {
            SDL_Log("Native File Dialog failed to initialize\n");
        }
    }

    ~UIPlatform()
    {
        NFD_Quit();
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    /// @brief Returns the SDL renderer (needed by DebuggerUI for texture creation)
    SDL_Renderer* getRenderer() const { return renderer; }

    /// @brief Returns the game display texture (needed by DebuggerUI for ImGui::Image)
    SDL_Texture* getGameTexture() const { return texture; }

    /// @brief Render the UI and emulation display
    /// @param renderExtraUI Optional callback to render additional ImGui panels (e.g. DebuggerUI)
    template<typename F = void(*)()>
    void renderUI(F renderExtraUI = [](){})
    {
        // Upload framebuffer to texture before starting ImGui frame
        SDL_UpdateTexture(texture, NULL, frameBuffer, textureWidth * sizeof(unsigned int));

        // Clear renderer
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Start ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Fullscreen dockspace host window
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGuiWindowFlags hostFlags =
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##DockHost", nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        // Apply deferred layout load before DockSpace rebuilds the tree
        applyPendingLayout();

        // Dockspace
        ImGuiID dockspaceId = ImGui::GetID("MainDockspace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        // Menu bar
        bool openKeybindings = false;
        if (ImGui::BeginMenuBar())
        {
            // --- File menu ---
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open ROM..."))
                {
                    nfdchar_t* path = nullptr;
                    nfdfilteritem_t filters[1] = { { "GameBoy ROM", "gb,gbc" } };
                    if (NFD_OpenDialog(&path, filters, 1, nullptr) == NFD_OKAY)
                    {
                        m_pendingROMPath = path;
                        NFD_FreePath(path);
                    }
                }
                if (ImGui::MenuItem("Restart", nullptr, false, !m_currentROMPath.empty()))
                    m_pendingRestart = true;

                ImGui::Separator();
                {
                    bool hasROM = m_gameBoy && !m_currentROMPath.empty();
                    if (ImGui::MenuItem("Save State", "F5", false, hasROM))
                    {
                        std::string ssPath = saveStatePath();
                        m_gameBoy->saveState(ssPath);
                    }
                    if (ImGui::MenuItem("Load State", "F8", false, hasROM))
                    {
                        std::string ssPath = saveStatePath();
                        m_gameBoy->loadState(ssPath);
                    }
                }
                ImGui::Separator();
                {
                    bool hasBattery = m_gameBoy && !m_currentROMPath.empty() &&
                        m_gameBoy->mmu().cartridge() &&
                        m_gameBoy->mmu().cartridge()->sramSize() > 0;
                    if (ImGui::MenuItem("Save File", nullptr, false, hasBattery))
                    {
                        std::string savPath = SeaBoy::SaveFile::getSavePath(m_currentROMPath);
                        m_gameBoy->saveSRAM(savPath);
                    }
                    if (ImGui::MenuItem("Load Save File", nullptr, false, hasBattery))
                    {
                        std::string savPath = SeaBoy::SaveFile::getSavePath(m_currentROMPath);
                        m_gameBoy->loadSRAM(savPath);
                    }
                }

                ImGui::EndMenu();
            }

            // --- Options menu ---
            if (ImGui::BeginMenu("Options"))
            {
                if (ImGui::MenuItem("Keybindings..."))
                    openKeybindings = true;

                if (ImGui::BeginMenu("Palette"))
                {
                    struct PalettePreset { const char* name; uint32_t shades[4]; };
                    static const PalettePreset presets[] = {
                        { "Classic",       { 0xFFFFFFFF, 0xAAAAAAFF, 0x555555FF, 0x000000FF } },
                        { "Green (DMG)",   { 0x9BBC0FFF, 0x8BAC0FFF, 0x306230FF, 0x0F380FFF } },
                        { "Brown (Pocket)",{ 0xF5E6C8FF, 0xC6A882FF, 0x8B6F47FF, 0x4A3728FF } },
                        { "Blue",          { 0xE0F8F8FF, 0x88C8E8FF, 0x3478A0FF, 0x183048FF } },
                    };
                    for (const auto& p : presets)
                    {
                        if (ImGui::MenuItem(p.name))
                        {
                            if (m_gameBoy)
                                m_gameBoy->ppu().palettes().setShades(p.shades);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            // --- Window menu ---
            if (m_debugger && ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem("Game",           nullptr, &m_debugger->m_showGame);
                ImGui::Separator();
                ImGui::MenuItem("Controls",       nullptr, &m_debugger->m_showControls);
                ImGui::MenuItem("CPU Registers",  nullptr, &m_debugger->m_showCPURegisters);
                ImGui::MenuItem("Breakpoints",    nullptr, &m_debugger->m_showBreakpoints);
                ImGui::MenuItem("Disassembly",    nullptr, &m_debugger->m_showDisassembly);
                ImGui::MenuItem("Memory",         nullptr, &m_debugger->m_showMemory);
                ImGui::MenuItem("PPU State",      nullptr, &m_debugger->m_showPPUState);
                ImGui::MenuItem("I/O Registers",  nullptr, &m_debugger->m_showIORegisters);
                ImGui::MenuItem("OAM",            nullptr, &m_debugger->m_showOAM);
                ImGui::MenuItem("Tile Viewer",    nullptr, &m_debugger->m_showTileViewer);
                ImGui::MenuItem("Tilemap Viewer", nullptr, &m_debugger->m_showTilemapViewer);

                ImGui::Separator();

                // Save Layout
                std::string saveLabel = m_currentLayoutName.empty()
                    ? "Save Layout..."
                    : "Save Layout (" + m_currentLayoutName + ")";
                if (ImGui::MenuItem(saveLabel.c_str()))
                {
                    if (m_currentLayoutName.empty())
                        m_openSaveLayoutPopup = true;
                    else
                        saveLayout(m_currentLayoutName);
                }

                if (ImGui::MenuItem("Save Layout As..."))
                    m_openSaveLayoutPopup = true;

                // Load Layout submenu
                if (ImGui::BeginMenu("Load Layout"))
                {
                    auto layouts = enumerateLayouts();
                    if (layouts.empty())
                        ImGui::MenuItem("(no saved layouts)", nullptr, false, false);
                    else
                    {
                        for (const auto& layoutName : layouts)
                        {
                            bool isCurrent = (layoutName == m_currentLayoutName);
                            if (ImGui::MenuItem(layoutName.c_str(), nullptr, isCurrent))
                                loadLayout(layoutName);
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        // Keybindings popup modal — OpenPopup must be in same scope as BeginPopupModal
        if (openKeybindings)
            ImGui::OpenPopup("Keybindings");
        renderKeybindingsPopup();

        // Save Layout popup — deferred OpenPopup from menu
        if (m_openSaveLayoutPopup)
        {
            ImGui::OpenPopup("Save Layout As");
            m_openSaveLayoutPopup = false;
        }
        renderSaveLayoutPopup();

        ImGui::End(); // ##DockHost

        // Game display window - dockable (guarded by visibility flag)
        bool showGame = !m_debugger || m_debugger->m_showGame;
        if (showGame)
        {
            ImGui::Begin("Game", m_debugger ? &m_debugger->m_showGame : nullptr,
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImVec2 avail = ImGui::GetContentRegionAvail();
            // Maintain aspect ratio (160:144)
            float scale = (std::min)(avail.x / static_cast<float>(textureWidth),
                                     avail.y / static_cast<float>(textureHeight));
            ImVec2 imageSize(static_cast<float>(textureWidth) * scale,
                             static_cast<float>(textureHeight) * scale);
            // Center the image
            ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - imageSize.x) * 0.5f,
                                       cursor.y + (avail.y - imageSize.y) * 0.5f));
            ImGui::Image(reinterpret_cast<ImTextureID>(texture), imageSize);
            ImGui::End(); // Game
        }

        // Render additional UI (e.g. debugger panels)
        renderExtraUI();

        // Finalize
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    /// @brief Write data to frameBuffer
    /// @param display 
    void writeToBuffer(const uint32_t* display)
    {
        for (int i = 0, c = 0; i < textureHeight; i++)
        {
            for (int j = 0; j < textureWidth; j++, c++)
            {
                frameBuffer[c] = display[j + i*textureWidth];
            }
        }
    }

    /// @brief Process input events
    /// @param gb Optional GameBoy instance to forward button presses to. May be nullptr.
    /// @return True when running
    bool processInput(SeaBoy::GameBoy* gb = nullptr)
    {
        bool running = true;
        SDL_Event e;

        while (SDL_PollEvent(&e))
        {
            ImGui_ImplSDL3_ProcessEvent(&e);
            switch(e.type)
            {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                {
                    // Keybinding rebind capture — intercept the key press
                    if (m_rebindingIndex >= 0 && !e.key.repeat)
                    {
                        if (e.key.scancode != SDL_SCANCODE_ESCAPE) // Escape cancels rebind
                            *bindingField(m_rebindingIndex) = e.key.scancode;
                        m_rebindingIndex = -1;
                        break;
                    }
                    if (!e.key.repeat && gb)
                        forwardKey(e.key.scancode, true, gb);
                    switch (e.key.scancode)
                    {
                        case SDL_SCANCODE_ESCAPE:
                            running = false;
                            break;
                        case SDL_SCANCODE_F5:
                            if (m_gameBoy && !m_currentROMPath.empty())
                                m_gameBoy->saveState(saveStatePath());
                            break;
                        case SDL_SCANCODE_F8:
                            if (m_gameBoy && !m_currentROMPath.empty())
                                m_gameBoy->loadState(saveStatePath());
                            break;
                    }
                }
                break;

                case SDL_EVENT_KEY_UP:
                {
                    if (!e.key.repeat && gb)
                        forwardKey(e.key.scancode, false, gb);
                }
                break;
            }
        }

        return running;
    }

    unsigned int vec4ToRGBA(ImVec4 vec4)
    {
        unsigned int r = vec4.x * 255;
        unsigned int g = vec4.y * 255;
        unsigned int b = vec4.z * 255;
        unsigned int a = vec4.w * 255;
        unsigned int val = (r << 24) | g << 16 | b << 8 | a;
        return val;
    }

private:
    // Derive save state file path from current ROM path (replaces extension with .ss0).
    std::string saveStatePath() const
    {
        size_t dot = m_currentROMPath.rfind('.');
        std::string base = (dot != std::string::npos)
            ? m_currentROMPath.substr(0, dot) : m_currentROMPath;
        return base + ".ss0";
    }

    // Returns a pointer to the scancode field in m_bindings for the given index (0–7).
    SDL_Scancode* bindingField(int index)
    {
        SDL_Scancode* fields[] = {
            &m_bindings.a, &m_bindings.b, &m_bindings.start, &m_bindings.select,
            &m_bindings.up, &m_bindings.down, &m_bindings.left, &m_bindings.right
        };
        return (index >= 0 && index < 8) ? fields[index] : nullptr;
    }

    // Keybindings popup modal — called from within the dockspace host window.
    void renderKeybindingsPopup()
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Keybindings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        static const char* buttonNames[] = {
            "A", "B", "Start", "Select", "Up", "Down", "Left", "Right"
        };

        if (ImGui::BeginTable("##keybinds", 3, ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Key",    ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("",       ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < 8; ++i)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(buttonNames[i]);

                ImGui::TableSetColumnIndex(1);
                if (m_rebindingIndex == i)
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Press a key...");
                else
                    ImGui::TextUnformatted(SDL_GetScancodeName(*bindingField(i)));

                ImGui::TableSetColumnIndex(2);
                ImGui::PushID(i);
                if (m_rebindingIndex == i)
                {
                    if (ImGui::Button("Cancel"))
                        m_rebindingIndex = -1;
                }
                else
                {
                    if (ImGui::Button("Rebind"))
                        m_rebindingIndex = i;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::Button("Close"))
        {
            m_rebindingIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- Layout save/load helpers ---

    std::string getLayoutDir()
    {
        const char* base = SDL_GetBasePath();
        return std::string(base ? base : "./") + "layouts";
    }

    std::vector<std::string> enumerateLayouts()
    {
        std::vector<std::string> names;
        std::string dir = getLayoutDir();
        if (!std::filesystem::exists(dir))
            return names;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (entry.path().extension() == ".ini")
                names.push_back(entry.path().stem().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    void saveLayout(const std::string& name)
    {
        std::string dir = getLayoutDir();
        std::filesystem::create_directories(dir);
        std::string path = dir + "/" + name + ".ini";

        size_t len = 0;
        const char* iniData = ImGui::SaveIniSettingsToMemory(&len);

        std::ofstream out(path, std::ios::binary);
        out.write(iniData, static_cast<std::streamsize>(len));

        // Append visibility section
        out << "\n[SeaBoy][Visibility]\n";
        if (m_debugger)
        {
            out << "showGame=" << m_debugger->m_showGame << "\n";
            out << "showControls=" << m_debugger->m_showControls << "\n";
            out << "showCPURegisters=" << m_debugger->m_showCPURegisters << "\n";
            out << "showBreakpoints=" << m_debugger->m_showBreakpoints << "\n";
            out << "showDisassembly=" << m_debugger->m_showDisassembly << "\n";
            out << "showMemory=" << m_debugger->m_showMemory << "\n";
            out << "showPPUState=" << m_debugger->m_showPPUState << "\n";
            out << "showIORegisters=" << m_debugger->m_showIORegisters << "\n";
            out << "showOAM=" << m_debugger->m_showOAM << "\n";
            out << "showTileViewer=" << m_debugger->m_showTileViewer << "\n";
            out << "showTilemapViewer=" << m_debugger->m_showTilemapViewer << "\n";
        }

        m_currentLayoutName = name;
    }

    // Defers loading to next frame (before DockSpace() is called).
    void loadLayout(const std::string& name)
    {
        m_pendingLayoutLoad = name;
    }

    // Actually applies the layout — must be called before DockSpace().
    void applyPendingLayout()
    {
        if (m_pendingLayoutLoad.empty()) return;

        std::string path = getLayoutDir() + "/" + m_pendingLayoutLoad + ".ini";
        std::ifstream in(path, std::ios::binary);
        if (!in) { m_pendingLayoutLoad.clear(); return; }

        std::string content((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

        // Extract and apply visibility section
        const std::string marker = "[SeaBoy][Visibility]";
        size_t pos = content.find(marker);
        std::string imguiIni = (pos != std::string::npos) ? content.substr(0, pos) : content;

        if (pos != std::string::npos && m_debugger)
        {
            std::istringstream vis(content.substr(pos + marker.size()));
            std::string line;
            while (std::getline(vis, line))
            {
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string key = line.substr(0, eq);
                bool val = (line.substr(eq + 1)[0] == '1');
                if      (key == "showGame")          m_debugger->m_showGame = val;
                else if (key == "showControls")      m_debugger->m_showControls = val;
                else if (key == "showCPURegisters")  m_debugger->m_showCPURegisters = val;
                else if (key == "showBreakpoints")   m_debugger->m_showBreakpoints = val;
                else if (key == "showDisassembly")   m_debugger->m_showDisassembly = val;
                else if (key == "showMemory")        m_debugger->m_showMemory = val;
                else if (key == "showPPUState")      m_debugger->m_showPPUState = val;
                else if (key == "showIORegisters")   m_debugger->m_showIORegisters = val;
                else if (key == "showOAM")           m_debugger->m_showOAM = val;
                else if (key == "showTileViewer")    m_debugger->m_showTileViewer = val;
                else if (key == "showTilemapViewer") m_debugger->m_showTilemapViewer = val;
            }
        }

        // Clear existing dock tree so DockSpace() rebuilds from loaded ini
        ImGuiID dockId = ImGui::GetID("MainDockspace");
        ImGui::DockBuilderRemoveNode(dockId);

        ImGui::LoadIniSettingsFromMemory(imguiIni.c_str(), imguiIni.size());
        m_currentLayoutName = m_pendingLayoutLoad;
        m_pendingLayoutLoad.clear();
    }

    void renderSaveLayoutPopup()
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Save Layout As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::Text("Layout name:");
        bool enter = ImGui::InputText("##layoutname", m_layoutNameBuf, sizeof(m_layoutNameBuf),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere(-1);

        bool validName = m_layoutNameBuf[0] != '\0';
        if ((ImGui::Button("Save") || enter) && validName)
        {
            saveLayout(m_layoutNameBuf);
            m_layoutNameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_layoutNameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Maps a SDL scancode to a GameBoy button via m_bindings and forwards the event.
    void forwardKey(SDL_Scancode sc, bool pressed, SeaBoy::GameBoy* gb)
    {
        struct Entry { SDL_Scancode sc; SeaBoy::Button btn; };
        const Entry map[] = {
            { m_bindings.a,      SeaBoy::Button::A      },
            { m_bindings.b,      SeaBoy::Button::B      },
            { m_bindings.start,  SeaBoy::Button::Start  },
            { m_bindings.select, SeaBoy::Button::Select },
            { m_bindings.up,     SeaBoy::Button::Up     },
            { m_bindings.down,   SeaBoy::Button::Down   },
            { m_bindings.left,   SeaBoy::Button::Left   },
            { m_bindings.right,  SeaBoy::Button::Right  },
        };
        for (const auto& entry : map)
        {
            if (entry.sc == sc)
            {
                gb->setButton(entry.btn, pressed);
                break;
            }
        }
    }
};