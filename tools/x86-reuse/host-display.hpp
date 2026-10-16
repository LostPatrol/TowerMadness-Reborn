// Scale the original portrait canvas into a resizable native OpenGL window.
#pragma once

// Original UI coordinates stay stable while the framebuffer uses the actual client pixels.
constexpr int LogicalWidth = 480, LogicalHeight = 800;
// Small windows remain usable; larger windows retain the original portrait aspect ratio.
constexpr int MinimumClientWidth = 240, MinimumClientHeight = 400;
struct HostDisplay {
    int width = LogicalWidth, height = LogicalHeight;
    int x = 0, y = 0, viewportWidth = LogicalWidth, viewportHeight = LogicalHeight;

    /** Recompute centered letterboxing without changing original camera or UI coordinates. */
    void resize(int clientWidth, int clientHeight) {
        width = std::max(1, clientWidth); height = std::max(1, clientHeight);
        const double scale = std::min(static_cast<double>(width) / LogicalWidth, static_cast<double>(height) / LogicalHeight);
        viewportWidth = std::max(1, static_cast<int>(std::lround(LogicalWidth * scale)));
        viewportHeight = std::max(1, static_cast<int>(std::lround(LogicalHeight * scale)));
        x = (width - viewportWidth) / 2; y = (height - viewportHeight) / 2;
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
};
// The window and original renderer share one scale transform, including all input gestures.
HostDisplay hostDisplay;

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
    SwapBuffers(windowDc);
}
