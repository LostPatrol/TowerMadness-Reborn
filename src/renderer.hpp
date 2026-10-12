// OpenGL scene rendering, desktop camera controls, and logical-pixel HUD data.
#pragma once

#include "math.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace tmreborn {

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

struct Renderable {
    std::string model;
    Vec3 position{};
    float rotation = 0;
    float scale = 1;
    Color tint{};
    float animationTime = 0;
};

struct RenderLine {
    Vec3 from{}, to{};
    Color color{};
};

struct HudText {
    std::string text;
    float x = 0, y = 0;
    Color color{};
};

struct HudButton {
    std::string text;
    float x = 0, y = 0, width = 0, height = 0;
    bool selected = false;
    bool enabled = true;
    int action = -1;
};

struct SceneView {
    std::vector<Renderable> models;
    std::vector<RenderLine> lines;
    std::vector<HudText> texts;
    std::vector<HudButton> buttons;
    std::optional<Vec2> selectedCell;
};

class Renderer {
public:
    // Keep graphics state private and tied to one SDL window.
    Renderer();
    // Release GPU resources before the owning window is destroyed.
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Create a core 3.3 context and load the converted original map assets.
    bool initialize(SDL_Window* window, const std::filesystem::path& assetsRoot,
                    const std::filesystem::path& mapFile);
    // Replace the board and its GPU resources when the player changes levels.
    bool changeMap(const std::filesystem::path& mapFile);
    // Refresh logical and drawable sizes from SDL after resize or DPI changes.
    void resize();
    // Render map, simulation models, world lines, and desktop HUD, then present.
    void draw(const SceneView& scene);
    // Intersect a logical-pixel mouse position with the world ground plane.
    std::optional<Vec2> pick(float screenX, float screenY) const;
    // Project a world point to logical pixels for diagnostics and UI anchoring.
    std::optional<Vec2> project(Vec3 world) const;
    // Move the camera target in world ground coordinates.
    void pan(float worldX, float worldY);
    // Change camera distance exponentially using mouse-wheel steps.
    void zoom(float wheelSteps);
    // Restore the map-centered camera with a view covering the whole board.
    void resetCamera();
    // Save the presented framebuffer as PNG without changing scene rendering.
    bool capture(const std::filesystem::path& path);
    // Load every prepared texture and model, retaining per-resource failures in a report.
    bool verifyResources(const std::filesystem::path& output);
    // Return a human-readable initialization failure or resource warning.
    const std::string& error() const;
    // Describe the actual driver and core context used by the renderer.
    const std::string& graphicsInfo() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tmreborn
