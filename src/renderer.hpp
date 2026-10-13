// OpenGL scene rendering, desktop camera controls, and logical-pixel HUD data.
#pragma once

#include "math.hpp"
#include <filesystem>
#include <cstdint>
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
    float pitch = 0;
    Vec3 pitchPivot{};
    Mat4 animationTransform = Mat4::identity();
    // Original PGL uses texture times RGBA with fixed-function lighting disabled.
    bool overrideMaterialColor = false, unlit = true;
    Vec2 textureScale{1,1}, textureOffset{};
    // Lightning chooses two distinct atlas strips on each presented frame.
    int atlasFrames = 0, atlasLayer = 0;
    std::uint32_t visualSeed = 0;
};

struct RenderLine {
    Vec3 from{}, to{};
    Color color{};
};

struct RenderSprite {
    std::string texture;
    Vec3 position{};
    float width = 1, height = 1;
    Color color{};
    bool ground = false, additive = false;
    // Native point sprites divide this size parameter by their camera distance.
    float pointSizeParameter = 0;
    // Flak and missile flashes use fixed native pixels rather than the screen-width multiplier.
    bool screenRelative = true;
    // Early flak flash shares the primary burst's distance despite its raised position.
    std::optional<Vec3> pointDistanceOrigin;
};

struct RenderRibbon {
    std::string texture;
    Vec3 from{}, to{};
    float width = 0.1f;
    Color color{};
    int frame = 0, frames = 1;
    bool additive = true;
};

struct RenderQuad {
    // Preserve native untextured geometry instead of deriving a camera-facing ribbon.
    std::array<Vec3,4> vertices;
    Color color{};
    bool additive = false;
};

struct HudText {
    std::string text;
    float x = 0, y = 0;
    Color color{};
    int fontSize = 18;
};

struct HudButton {
    std::string text;
    float x = 0, y = 0, width = 0, height = 0;
    bool selected = false;
    bool enabled = true;
    int action = -1;
    std::string icon{};
};

struct HudPanel {
    float x = 0, y = 0, width = 0, height = 0;
    Color color{};
};

struct HudImage {
    std::string texture;
    float x = 0, y = 0, width = 0, height = 0;
    Color color{};
};

struct SceneView {
    std::vector<Renderable> models;
    std::vector<RenderLine> lines;
    std::vector<HudText> texts;
    std::vector<HudButton> buttons;
    std::optional<Vec2> selectedCell;
    std::vector<HudPanel> panels;
    // Native existing-tower selection color, separate from the tower's material.
    Color cellColor{195.0f / 255,217.0f / 255,1,1};
    std::vector<RenderSprite> sprites;
    std::vector<RenderRibbon> ribbons;
    std::vector<HudImage> images;
    std::vector<RenderQuad> quads;
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
    // Reserve a logical-pixel battlefield so projection and picking avoid the HUD.
    void setBoardViewport(float x, float y, float width, float height);
    // Render map, simulation models, world lines, and desktop HUD, then present.
    void draw(const SceneView& scene);
    // Cache an actual original-map render without changing the live board or camera.
    std::string mapPreview(const std::string& mapName);
    // Intersect a logical-pixel mouse position with the world ground plane.
    std::optional<Vec2> pick(float screenX, float screenY) const;
    // Project a world point to logical pixels for diagnostics and UI anchoring.
    std::optional<Vec2> project(Vec3 world) const;
    // Move the camera target along the displayed horizontal and vertical ground axes.
    void pan(float worldX, float worldY);
    // Change camera distance exponentially using mouse-wheel steps.
    void zoom(float wheelSteps);
    // Restore the original centered camera, optionally fitting the reserved desktop board.
    void resetCamera(bool fit = true);
    // Save the presented framebuffer as PNG without changing scene rendering.
    bool capture(const std::filesystem::path& path);
    // Load every prepared texture and model, retaining per-resource failures in a report.
    bool verifyResources(const std::filesystem::path& output);
    // Return a human-readable initialization failure or resource warning.
    const std::string& error() const;
    // Describe the actual driver and core context used by the renderer.
    const std::string& graphicsInfo() const;

private:
    // Reuse the production scene path for window presentation and offscreen thumbnails.
    void drawScene(const SceneView& scene, bool present);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tmreborn
