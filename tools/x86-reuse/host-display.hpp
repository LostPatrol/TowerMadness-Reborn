// Share a responsive logical canvas between the native renderer and pointer input.
#pragma once

// Retain the original UI density while widening the actual camera canvas for desktop windows.
int LogicalWidth = 480, LogicalHeight = 800;
// Baseline dimensions keep original labels and radial menus readable.
constexpr int PortraitCanvasWidth = 480, PortraitCanvasHeight = 800;
// Small windows remain usable through uniform scaling.
constexpr int MinimumClientWidth = 240, MinimumClientHeight = 400;
struct HostDisplay {
    int width = LogicalWidth, height = LogicalHeight;
    int x = 0, y = 0, viewportWidth = LogicalWidth, viewportHeight = LogicalHeight;

    /** Extend the logical canvas to the client aspect instead of stretching a portrait image. */
    void resize(int clientWidth, int clientHeight) {
        width = std::max(1, clientWidth); height = std::max(1, clientHeight);
        const double scale = std::min(static_cast<double>(width) / PortraitCanvasWidth, static_cast<double>(height) / PortraitCanvasHeight);
        LogicalWidth = std::max(PortraitCanvasWidth, static_cast<int>(std::lround(width / scale)));
        LogicalHeight = std::max(PortraitCanvasHeight, static_cast<int>(std::lround(height / scale)));
        viewportWidth = width; viewportHeight = height;
        x = 0; y = 0;
    }

    /** Convert native top-left pointer coordinates to the original logical canvas. */
    bool pointer(float& px, float& py) const {
        const int top = height - y - viewportHeight;
        const bool inside = px >= x && px < x + viewportWidth && py >= top && py < top + viewportHeight;
        px = (px - x) * LogicalWidth / viewportWidth;
        py = (py - top) * LogicalHeight / viewportHeight;
        return inside;
    }

    /** Convert logical verification clicks through the same scale used by real mouse input. */
    POINT clientPoint(int px, int py) const {
        return POINT{x + static_cast<LONG>(std::lround(static_cast<double>(px) * viewportWidth / LogicalWidth)),
            height - y - viewportHeight + static_cast<LONG>(std::lround(static_cast<double>(py) * viewportHeight / LogicalHeight))};
    }

    /** Reposition legacy scene probes by their camera or HUD anchor before native input. */
    POINT sceneProbePoint(int px, int py) const {
        const int extraWidth = LogicalWidth - PortraitCanvasWidth;
        const int extraHeight = LogicalHeight - PortraitCanvasHeight;
        if (py < 100 && px > 400) px += extraWidth;
        else if (py >= 736 && px >= 400) { px += extraWidth; py += extraHeight; }
        else if (py >= 736 && px <= 64) py += extraHeight;
        else { px += extraWidth / 2; py += extraHeight / 2; }
        return clientPoint(px, py);
    }
};
// The window and original renderer share one scale transform, including all input gestures.
HostDisplay hostDisplay;
// Bounded native probes may inspect the exact submitted frame without redrawing it.
void (*displayPresentObserver)(bool afterSwap, BOOL succeeded) = nullptr;

/** Map original GL viewport pixels to the actual high-resolution framebuffer. */
void __cdecl hostViewport(int x, int y, int width, int height) {
    const double sx = static_cast<double>(hostDisplay.viewportWidth) / LogicalWidth;
    const double sy = static_cast<double>(hostDisplay.viewportHeight) / LogicalHeight;
    glViewport(hostDisplay.x + static_cast<int>(std::lround(x * sx)), hostDisplay.y + static_cast<int>(std::lround(y * sy)),
        static_cast<int>(std::lround(width * sx)), static_cast<int>(std::lround(height * sy)));
}

/** Keep original clipping aligned with the scaled viewport. */
void __cdecl hostScissor(int x, int y, int width, int height) {
    const double sx = static_cast<double>(hostDisplay.viewportWidth) / LogicalWidth;
    const double sy = static_cast<double>(hostDisplay.viewportHeight) / LogicalHeight;
    glScissor(hostDisplay.x + static_cast<int>(std::lround(x * sx)), hostDisplay.y + static_cast<int>(std::lround(y * sy)),
        static_cast<int>(std::lround(width * sx)), static_cast<int>(std::lround(height * sy)));
}

/** Clear only unused borders after rendering, preserving the original GL state. */
void presentDisplay() {
    renderDesktopOverlay();
    if (hostDisplay.x || hostDisplay.y) {
        GLfloat color[4]{}; GLint box[4]{}; GLboolean mask[4]{};
        glGetFloatv(GL_COLOR_CLEAR_VALUE, color); glGetIntegerv(GL_SCISSOR_BOX, box); glGetBooleanv(GL_COLOR_WRITEMASK, mask);
        const bool clipped = glIsEnabled(GL_SCISSOR_TEST) != 0;
        glEnable(GL_SCISSOR_TEST); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glClearColor(0, 0, 0, 1);
        const auto clearBorder = [](int x, int y, int width, int height) {
            if (width <= 0 || height <= 0) return;
            glScissor(x, y, width, height); glClear(GL_COLOR_BUFFER_BIT);
        };
        clearBorder(0, 0, hostDisplay.x, hostDisplay.height);
        clearBorder(hostDisplay.x + hostDisplay.viewportWidth, 0, hostDisplay.width - hostDisplay.x - hostDisplay.viewportWidth, hostDisplay.height);
        clearBorder(0, 0, hostDisplay.width, hostDisplay.y);
        clearBorder(0, hostDisplay.y + hostDisplay.viewportHeight, hostDisplay.width, hostDisplay.height - hostDisplay.y - hostDisplay.viewportHeight);
        glClearColor(color[0], color[1], color[2], color[3]); glColorMask(mask[0], mask[1], mask[2], mask[3]);
        glScissor(box[0], box[1], box[2], box[3]); if (!clipped) glDisable(GL_SCISSOR_TEST);
    }
    if (displayPresentObserver) displayPresentObserver(false, TRUE);
    const BOOL swapped = SwapBuffers(windowDc);
    if (displayPresentObserver) displayPresentObserver(true, swapped);
}
