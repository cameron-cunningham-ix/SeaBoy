#include <iostream>
#include <cstring>
#include <cmath>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "nfd.h"
#include "src/core/GameBoy.hpp"

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

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    
public:
    // Key bindings - public so SettingsUI can read and modify them at runtime.
    JoypadBindings m_bindings;

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

    /// @brief Render the UI and emulation display
    void renderUI()
    {
        // Start ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
        // Set ImGui window to match viewport (SDL window)
        int currWindowWidth, currWindowHeight;
        SDL_GetWindowSizeInPixels(window, &windowWidth, &windowHeight);
        // ImGui::SetNextWindowSize(ImVec2(currWindowWidth, currWindowHeight));
        // ImVec2 availSize = ImVec2((float)currWindowWidth, (float)currWindowHeight);
       

        
        SDL_UpdateTexture(texture, NULL, frameBuffer, textureWidth * sizeof(unsigned int));
        SDL_RenderTexture(renderer, texture, NULL, NULL);
       

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        // Clear renderer for next frame
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 1);
        SDL_RenderClear(renderer);
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
                    if (!e.key.repeat && gb)
                        forwardKey(e.key.scancode, true, gb);
                    switch (e.key.scancode)
                    {
                        case SDL_SCANCODE_ESCAPE:
                            running = false;
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