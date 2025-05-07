// viewer.cpp
// Build with:
//   g++ -std=c++17 viewer.cpp -o viewer.exe -lraylib -lopengl32 -lwinmm -lgdi32

#include "raylib.h"
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <algorithm>

// Simple 2D integer point
struct Vec2i { int x, y; };

// Trim whitespace from both ends of a string
static std::string trim(const std::string &s) {
    const char *ws = " \t\r\n";
    auto start = s.find_first_not_of(ws);
    auto end   = s.find_last_not_of(ws);
    return (start==std::string::npos) ? "" : s.substr(start, end-start+1);
}

int main() {
    // Load vegetation frames
    std::ifstream vegFile("grass_states.csv");
    if (!vegFile.is_open()) {
        std::cerr << "Error: could not open grass_states.csv\n";
        return 1;
    }
    int WIDTH=0, HEIGHT=0, SAVE_INTERVAL=0, MAX_TICKS=0;
    std::string line;
    while (std::getline(vegFile, line)) {
        if (line.rfind("#", 0) == 0) {
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(2, eq-2);
                int val = std::stoi(line.substr(eq+1));
                if      (key == "WIDTH")         WIDTH = val;
                else if (key == "HEIGHT")        HEIGHT = val;
                else if (key == "SAVE_INTERVAL") SAVE_INTERVAL = val;
                else if (key == "MAX_TICKS")     MAX_TICKS = val;
            }
        } else if (line.rfind("tick,", 0) == 0) {
            break;
        }
    }
    if (!WIDTH || !HEIGHT || !SAVE_INTERVAL || !MAX_TICKS) {
        std::cerr << "Error: invalid settings in grass_states.csv\n";
        return 1;
    }
    int NUM_FRAMES = MAX_TICKS / SAVE_INTERVAL;
    std::vector<std::vector<Vec2i>> grassFrames(NUM_FRAMES);

    while (std::getline(vegFile, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string tok;
        int tick, x, y;
        std::getline(ss, tok, ','); tick = std::stoi(tok);
        std::getline(ss, tok, ','); // skip id
        std::getline(ss, tok, ','); x = std::stoi(tok);
        std::getline(ss, tok, ','); y = std::stoi(tok);
        int idx = tick / SAVE_INTERVAL;
        if (idx >= 0 && idx < NUM_FRAMES)
            grassFrames[idx].push_back({x, y});
    }
    vegFile.close();

    // Load water frames
    std::ifstream worldFile("world_state.csv");
    if (!worldFile.is_open()) {
        std::cerr << "Error: could not open world_state.csv\n";
        return 1;
    }
    std::getline(worldFile, line); // skip header
    std::vector<std::tuple<int,int>> waterFrames(NUM_FRAMES);
    while (std::getline(worldFile, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string tok;
        int x, y;
        std::getline(ss, tok, ','); x    = std::stoi(trim(tok));
        std::getline(ss, tok, ','); y    = std::stoi(trim(tok));
        std::getline(ss, tok, ','); // type string
        std::string type = trim(tok);
        
        if ( type == "Water"){
            waterFrames.push_back({x, y});
        }
    }
    worldFile.close();

    // Window & drawing setup
    const int SCALE = 4;             // base pixels per tile
    float zoom = 1.0f;               // current zoom factor
    const float ZOOM_SPEED = 0.1f;   // zoom increment

    InitWindow(WIDTH * SCALE, HEIGHT * SCALE, "Ecosystem Viewer");
    SetExitKey(KEY_ESCAPE);

    bool paused = false;
    bool fullscreen = false;
    float playbackSpeed = 1.0f;
    const float BASE_FPS = 10.0f;
    const float BASE_FRAME_TIME = 1.0f / BASE_FPS;
    float timer = 0.0f;
    int frame = 0;

    // Main render loop
    while (!WindowShouldClose()) {
        // INPUT: pause, speed, fullscreen
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_RIGHT)) playbackSpeed = std::min(playbackSpeed * 2.0f, 16.0f);
        if (IsKeyPressed(KEY_LEFT))  playbackSpeed = std::max(playbackSpeed * 0.5f, 0.25f);
        if (IsKeyPressed(KEY_F)) {
            fullscreen = !fullscreen;
            ToggleFullscreen();
        }
        // INPUT: zoom via mouse wheel
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            zoom = std::clamp(zoom + wheel * ZOOM_SPEED, 0.1f, 10.0f);
        }

        // UPDATE: advance frame based on timer
        float dt = GetFrameTime();
        if (!paused) {
            timer += dt * playbackSpeed;
            if (timer >= BASE_FRAME_TIME) {
                int steps = int(timer / BASE_FRAME_TIME);
                frame = (frame + steps) % NUM_FRAMES;
                timer -= steps * BASE_FRAME_TIME;
            }
        }

        // DRAW: water then grass
        BeginDrawing();
          ClearBackground(BLACK);
          float drawScale = SCALE * zoom;
          for (auto &p : waterFrames) {
            DrawRectangle(int(std::get<0>(p) * drawScale), int(std::get<1>(p) * drawScale),
                          int(drawScale), int(drawScale), BLUE);
          }
          for (auto &p : grassFrames[frame]) {
            DrawRectangle(int(p.x * drawScale), int(p.y * drawScale),
                          int(drawScale), int(drawScale), GREEN);
          }
          // OVERLAY: info text
          DrawText(TextFormat("Frame %d/%d  Tick %d",
                             frame+1, NUM_FRAMES, frame * SAVE_INTERVAL),
                   10, 10, 20, WHITE);
          DrawText(TextFormat("Speed: %.2fx %s",
                             playbackSpeed,
                             paused?"(Paused)":""),
                   10, 40, 20, WHITE);
          DrawText("Controls: [Space]=Pause  [←/→]=Speed  [F]=Fullscreen  [Wheel]=Zoom  [Esc]=Exit",
                   10, HEIGHT * SCALE - 30, 20, LIGHTGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
