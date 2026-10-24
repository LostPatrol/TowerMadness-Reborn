// Adapt original map selection with smooth desktop navigation and a responsive thumbnail overview.
#pragma once

/** Route a deliberate desktop start through the original menu's touch dispatcher. */
void desktopMapMenuNativeTap(float x, float y);

/** Resolve configured navigation after controls are defined, without coupling the header order. */
bool desktopMapMenuBoundKey(WPARAM key, LPARAM flags);

namespace desktop_map {
// Native MAPMENU entries contain original metadata, cached mode, lock and completion fields.
struct Entry {
    const char* name; const char* description; int difficulty; void* mode;
    int loaded; int locked; int builtin; void* unlockPage; int completed; int total;
};
static_assert(sizeof(Entry) == 40, "Original map entry ABI");
struct Ref { bool catalog; int index; Entry* entry; };
// Preview images use original terrain rendering and live only in this GL context.
struct Thumbnail {
    GLuint texture; int width, height;
    float left, top, canvasWidth, canvasHeight;
};
std::map<std::string, Thumbnail> thumbnails;
// The native selector keeps its own lifecycle; these fields only describe desktop navigation.
void* owner = nullptr;
bool enabled = false, overview = false, pressed = false;
int filter = 0, page = 0, pressedAction = -1;
float pressX = 0, pressY = 0;
// Pointer position follows capture so dragging out also releases the visual button face.
float pointerX = 0, pointerY = 0;
// Fractional wheel notches survive high-resolution wheel messages and direction changes.
int wheelRemainder = 0;
bool wheelOverview = false;
// Visual displacement is independent of the immediately updated native selection.
double slide = 0;
LARGE_INTEGER slideClock{};
// Exponential settling stays smooth under repeated or reversed navigation.
constexpr double SlideDecay = 14.0, SlideRest = 0.001;
// Neighbor centers retain the existing thirty-percent canvas spacing.
constexpr float CarouselSpacing = 0.30f;
// Captured clicks tolerate a small amount of logical-coordinate pointer jitter.
constexpr float ClickTolerance = 12.0f;
// A bounded preview texture keeps overview loading incremental instead of blocking on 116 maps.
constexpr int MaximumPreviewWidth = 480;
// The native map renderer clears to RGB(51,51,51); exclude its top/bottom controls from terrain bounds.
constexpr int PreviewBackground = 51;
// The original preview has chrome in the top/bottom portions of the full logical canvas.
constexpr float PreviewContentBottomFraction = 1.0f / 6.0f, PreviewContentTopFraction = 11.0f / 12.0f;
// Keep the original Normal/Endless header visible throughout carousel motion.
constexpr float PreviewControlsBottom = 115.0f;
// Card geometry is shared by rendering and hit testing.
constexpr float Margin = 24.0f, CardGap = 16.0f, CardHeight = 250.0f, HeaderHeight = 150.0f;
// Windows bitmap glyphs avoid changing the original PGL font or atlas cache.
GLuint font = 0;
// Outline metrics allow the same font size at every desktop resolution and DPI.
std::array<GLYPHMETRICSFLOAT, 128> glyphs{};
struct Button { float x, y, w, h; int action; std::string label; };
std::vector<Button> buttons;

/** Read one verified MAPMENU word without exposing it to other desktop controls. */
int& word(void* state, unsigned index) { return static_cast<int*>(state)[index]; }

/** Return the original active map selector only while its page is visible. */
void* state() {
    if (!enabled || !originalMenu || !*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(originalMenu) + 4)
        || *static_cast<void**>(originalMenu) != findExport("startGamePage")) return nullptr;
    return *reinterpret_cast<void**>(static_cast<unsigned char*>(originalMenu) + 0x164);
}

/** Reset transient desktop state when the original menu allocates another selector. */
void bind(void* map) {
    if (owner == map) return;
    owner = map; overview = false; pressed = false; filter = 0; page = 0;
    pressedAction = -1; wheelRemainder = 0; wheelOverview = false; slide = 0; slideClock = {};
}

/** Advance presentation with QPC while keeping native selection available to every input. */
void advanceSlide() {
    LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    if (slideClock.QuadPart && frequency.QuadPart > 0) {
        const double elapsed = static_cast<double>(now.QuadPart - slideClock.QuadPart) / frequency.QuadPart;
        slide *= std::exp(-SlideDecay * std::max(0.0, elapsed));
        if (std::abs(slide) < SlideRest) slide = 0;
    }
    slideClock = now;
}

/** Keep the original preview facing forward, removing touch-carousel rotation and inertia. */
void steady(void* map) {
    word(map, 2) = 0; word(map, 0x52) = 0; word(map, 0x53) = 0;
    word(map, 0xf) = 0; word(map, 0x19) = 0; word(map, 0x4f) = 0;
}

/** Recognize the four campaign modes even after the original catalog adds them at completion. */
bool campaign(const char* name) {
    return name && (!std::strcmp(name, "PlainPlane") || !std::strcmp(name, "DividedAttention")
        || !std::strcmp(name, "MuttonsFolly") || !std::strcmp(name, "Madness"));
}

/** Build a filtered view of original entries while retaining their native lock fields. */
std::vector<Ref> entries(void* map) {
    std::vector<Ref> result;
    auto* campaignEntries = reinterpret_cast<Entry*>(word(map, 3));
    auto* catalogEntries = reinterpret_cast<Entry*>(word(map, 5));
    for (int index = 0; index < word(map, 4); ++index) {
        if (filter == 0 || filter == 1) result.push_back({false, index, campaignEntries + index});
    }
    if (filter == 1) return result;
    for (int index = 0; index < word(map, 9); ++index) {
        auto* entry = catalogEntries + index;
        if (!entry->name || campaign(entry->name)) continue;
        const int group = entry->difficulty < 3 ? 2 : entry->difficulty < 6 ? 3 : entry->difficulty < 9 ? 4 : 5;
        if (filter == 0 || filter == group) result.push_back({true, index, entry});
    }
    return result;
}

/** Select original entry fields only; starting and unlocking remain original game operations. */
void select(void* map, const Ref& ref) {
    word(map, 0xc) = ref.catalog ? 1 : 0;
    word(map, ref.catalog ? 0xb : 10) = ref.index;
    word(map, 6) = 0; word(map, 7) = ref.catalog ? 0x3f800000 : 0;
    steady(map);
    log(std::string("DESKTOP_MAP_SELECT name=") + ref.entry->name + " locked=" + std::to_string(ref.entry->locked));
}

/** Return the responsive number of visible cards without a separate input layout. */
int columns() { return std::clamp(static_cast<int>((LogicalWidth - Margin * 2 + CardGap) / 240), 1, 6); }

/** Keep small portrait windows navigable and large desktop windows useful. */
int rows() { return std::max(1, static_cast<int>((LogicalHeight - HeaderHeight - 95) / (CardHeight + CardGap))); }

/** Draw a colored panel using the temporary desktop GL state. */
void box(float x, float y, float w, float h, float r, float g, float b, float a = 1) {
    glDisable(GL_TEXTURE_2D); glColor4f(r, g, b, a);
    glBegin(GL_QUADS); glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h); glEnd();
}

/** Draw readable logical-coordinate labels with a private Windows font display list. */
void text(float x, float y, const std::string& value, float scale = 1, float maxWidth = 100000) {
    if (!font) {
        font = glGenLists(128);
        HFONT face = CreateFontA(-21, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial");
        HGDIOBJ old = SelectObject(windowDc, face);
        wglUseFontOutlinesA(windowDc, 0, 128, font, 0.001f, 0, WGL_FONT_POLYGONS, glyphs.data());
        SelectObject(windowDc, old); DeleteObject(face);
    }
    float advance = 0;
    for (const unsigned char character : value) if (character < glyphs.size()) advance += glyphs[character].gmfCellIncX;
    if (advance > 0) scale = std::min(scale, maxWidth / (26 * advance));
    glDisable(GL_TEXTURE_2D); glColor4f(0.96f, 0.97f, 1, 1);
    glPushMatrix(); glTranslatef(x, y + 26 * scale, 0); glScalef(26 * scale, -26 * scale, 1);
    glListBase(font); glCallLists(static_cast<GLsizei>(value.size()), GL_UNSIGNED_BYTE, value.data()); glPopMatrix();
}

/** Center visible glyph bounds inside the actual button, including short filters and descenders. */
void buttonLabel(float x, float y, float width, float height, const std::string& label) {
    if (label.empty()) return;
    if (!font) text(0, 0, "");
    float left = (std::numeric_limits<float>::max)(), right = -left, top = -left, bottom = left, pen = 0;
    for (const unsigned char character : label) {
        if (character >= glyphs.size()) continue;
        const auto& glyph = glyphs[character];
        if (glyph.gmfBlackBoxX > 0 && glyph.gmfBlackBoxY > 0) {
            left = std::min(left, pen + glyph.gmfptGlyphOrigin.x);
            right = std::max(right, pen + glyph.gmfptGlyphOrigin.x + glyph.gmfBlackBoxX);
            top = std::max(top, glyph.gmfptGlyphOrigin.y);
            bottom = std::min(bottom, glyph.gmfptGlyphOrigin.y - glyph.gmfBlackBoxY);
        }
        pen += glyph.gmfCellIncX;
    }
    if (right <= left || top <= bottom) return;
    // Match text()'s em scale while reserving horizontal and vertical breathing room.
    constexpr float EmSize = 26, HorizontalPadding = 12, VerticalPadding = 6;
    const float scale = std::max(0.01f, std::min({1.0f,
        (width - HorizontalPadding * 2) / ((right - left) * EmSize),
        (height - VerticalPadding * 2) / ((top - bottom) * EmSize)}));
    const float baselineX = x + (width - (right - left) * EmSize * scale) / 2 - left * EmSize * scale;
    const float baselineY = y + (height - (top - bottom) * EmSize * scale) / 2 + top * EmSize * scale;
    text(baselineX, baselineY - EmSize * scale, label, scale);
}

/** Add a visible control and the identical hit rectangle. */
void button(float x, float y, float w, float h, const std::string& label, int action, bool selected = false) {
    buttons.push_back({x, y, w, h, action, label});
    const bool down = pressed && pressedAction == action && pointerX >= x && pointerX <= x + w
        && pointerY >= y && pointerY <= y + h && std::abs(pointerX - pressX) <= ClickTolerance
        && std::abs(pointerY - pressY) <= ClickTolerance;
    desktop_skin::button(x, y, w, h, selected, down);
    buttonLabel(x, y + (down ? 2.0f : 0.0f), w, h, label);
}

/** Establish a top-left 2D overlay without leaking native renderer state changes. */
void begin() {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST); glDisable(GL_SCISSOR_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, LogicalWidth, LogicalHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
}

/** Restore projection, texture transform, and all GL flags after desktop drawing. */
void end() {
    glMatrixMode(GL_TEXTURE); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glPopAttrib(); glMatrixMode(GL_MODELVIEW);
}

/** Render the unchanged original page with its existing content anchor. */
void nativeRender(void* menu, void* pageObject, float alpha) {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glTranslatef(menuContentOffset(), 0, 0); glMatrixMode(GL_MODELVIEW);
    originalLandscapePageRenders.at(pageObject)(menu, pageObject, alpha);
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

/** Cache one original map preview per frame, preserving original entry ownership and game state. */
void preview(void* menu, void* pageObject, void* map, const std::vector<Ref>& list, bool carousel = false) {
    const int capacity = carousel ? static_cast<int>(list.size()) : columns() * rows();
    const int first = carousel ? 0 : page * capacity;
    for (int at = first; at < std::min(static_cast<int>(list.size()), first + capacity); ++at) {
        const auto& ref = list[at];
        if (thumbnails.count(ref.entry->name)) continue;
        // Locked entries render their original lock artwork rather than inventing an unlocked preview.
        std::array<int, 0x59> saved{};
        std::memcpy(saved.data(), map, sizeof(saved));
        select(map, ref);
        if (ref.entry->loaded < 0) ref.entry->loaded = 0;
        GLint viewport[4]{}; glGetIntegerv(GL_VIEWPORT, viewport);
        const int previewWidth = std::min(MaximumPreviewWidth, hostDisplay.viewportWidth);
        const int previewHeight = std::max(1, static_cast<int>(std::lround(static_cast<double>(previewWidth) * LogicalHeight / LogicalWidth)));
        const int contentBottom = static_cast<int>(previewHeight * PreviewContentBottomFraction);
        const int contentTop = static_cast<int>(previewHeight * PreviewContentTopFraction);
        glViewport(0, 0, previewWidth, previewHeight);
        nativeRender(menu, pageObject, 1);
        // Find the complete original terrain and title rather than trimming wide or tall maps to a fixed crop.
        std::vector<unsigned char> pixels(previewWidth * previewHeight * 3);
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT); glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, previewWidth, previewHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data()); glPopClientAttrib();
        int left = previewWidth, right = -1, bottom = previewHeight, top = -1;
        for (int y = contentBottom; y < contentTop; ++y) {
            for (int x = 0; x < previewWidth; ++x) {
                const unsigned char* pixel = pixels.data() + (y * previewWidth + x) * 3;
                if (std::abs(pixel[0] - PreviewBackground) < 6 && std::abs(pixel[1] - PreviewBackground) < 6
                    && std::abs(pixel[2] - PreviewBackground) < 6) continue;
                left = std::min(left, x); right = std::max(right, x); bottom = std::min(bottom, y); top = std::max(top, y);
            }
        }
        if (right < left) { left = 0; right = previewWidth - 1; bottom = contentBottom; top = contentTop - 1; }
        left = std::max(0, left - 4); right = std::min(previewWidth - 1, right + 4);
        bottom = std::max(contentBottom, bottom - 4); top = std::min(contentTop - 1, top + 4);
        const int textureWidth = right - left + 1, textureHeight = top - bottom + 1;
        GLuint texture = 0; glGenTextures(1, &texture);
        glPushAttrib(GL_TEXTURE_BIT); glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, left, bottom, textureWidth, textureHeight, 0); glPopAttrib();
        thumbnails.emplace(ref.entry->name, Thumbnail{texture, textureWidth, textureHeight,
            static_cast<float>(left), static_cast<float>(previewHeight - top - 1),
            static_cast<float>(previewWidth), static_cast<float>(previewHeight)});
        std::memcpy(map, saved.data(), sizeof(saved));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        break;
    }
}

/** Find the selected entry in the current native group without changing its lock state. */
int selection(void* map, const std::vector<Ref>& list) {
    const bool catalog = word(map, 0xc) != 0;
    const int selected = word(map, catalog ? 0xb : 10);
    for (int index = 0; index < static_cast<int>(list.size()); ++index)
        if (list[index].catalog == catalog && list[index].index == selected) return index;
    return 0;
}

/** Draw an original preview at its continuous carousel position and original center size. */
void carouselCard(const Ref& ref, double position) {
    const float distance = std::min(1.0f, static_cast<float>(std::abs(position)));
    const float width = std::min(250.0f, LogicalWidth * 0.23f);
    const float center = LogicalWidth / 2.0f + static_cast<float>(position) * LogicalWidth * CarouselSpacing;
    const float top = LogicalHeight * 0.25f, availableHeight = LogicalHeight * 0.42f;
    const auto found = thumbnails.find(ref.entry->name);
    if (found != thumbnails.end()) {
        const auto& thumb = found->second;
        const float scale = std::min(width / thumb.width, availableHeight / thumb.height);
        const float originalWidth = thumb.width * LogicalWidth / thumb.canvasWidth;
        const float originalHeight = thumb.height * LogicalHeight / thumb.canvasHeight;
        const float w = originalWidth + (thumb.width * scale - originalWidth) * distance;
        const float h = originalHeight + (thumb.height * scale - originalHeight) * distance;
        const float originalLeft = thumb.left * LogicalWidth / thumb.canvasWidth;
        const float originalTop = thumb.top * LogicalHeight / thumb.canvasHeight;
        const float x = originalLeft + (LogicalWidth / 2.0f - w / 2 - originalLeft) * distance
            + static_cast<float>(position) * LogicalWidth * CarouselSpacing;
        const float y = originalTop + (top + (availableHeight - h) / 2 - originalTop) * distance;
        const float innerAlpha = 1.0f - 0.35f * distance, outerAlpha = innerAlpha - 0.45f * distance;
        const float red = 1.0f - 0.20f * distance, green = 1.0f - 0.14f * distance;
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, thumb.texture);
        glBegin(GL_QUADS);
        glColor4f(red, green, red, position < 0 ? outerAlpha : innerAlpha);
        glTexCoord2f(0, 1); glVertex2f(x, y); glTexCoord2f(0, 0); glVertex2f(x, y + h);
        glColor4f(red, green, red, position < 0 ? innerAlpha : outerAlpha);
        glTexCoord2f(1, 0); glVertex2f(x + w, y + h); glTexCoord2f(1, 1); glVertex2f(x + w, y);
        glEnd(); glDisable(GL_TEXTURE_2D);
    }
    if (std::abs(position) < 0.75) return;
    const float labelY = top + availableHeight + 12;
    buttonLabel(center - width / 2, labelY, width, 36, ref.entry->name ? ref.entry->name : "Map");
    buttonLabel(center - width / 2, labelY + 36, width, 26,
        ref.entry->locked ? "LOCKED" : position < 0 ? "Previous map" : "Next map");
}

/** Resolve a wrapped visual slot even when one wheel event advances several maps. */
const Ref& carouselEntry(const std::vector<Ref>& list, int selectedAt, long long offset) {
    const auto count = static_cast<long long>(list.size());
    return list[static_cast<std::size_t>(((selectedAt + offset) % count + count) % count)];
}

/** Keep neighboring hit regions fixed while original preview artwork moves beneath them. */
void drawCarousel(const std::vector<Ref>& list, int selectedAt) {
    const long long middle = static_cast<long long>(std::llround(-slide));
    // Native rendering owns settled center frames; transition frames reuse its exact captured bounds.
    if (slide != 0) box(0, PreviewControlsBottom, static_cast<float>(LogicalWidth),
        LogicalHeight * (1 - PreviewContentBottomFraction) - PreviewControlsBottom, 0.2f, 0.2f, 0.2f);
    for (int distance = 2; distance >= 0; --distance) {
        for (int side : {-1, 1}) {
            if (!distance && side > 0) continue;
            const long long offset = middle + distance * side;
            if (!slide && std::abs(offset) != 1) continue;
            carouselCard(carouselEntry(list, selectedAt, offset), slide + offset);
        }
    }
    const float width = std::min(250.0f, LogicalWidth * 0.23f);
    for (int direction : {-1, 1}) {
        const float center = LogicalWidth / 2.0f + direction * LogicalWidth * CarouselSpacing;
        buttons.push_back({center - width / 2, LogicalHeight * 0.25f, width,
            LogicalHeight * 0.42f + 65, direction < 0 ? 1 : 2, "Neighbor preview"});
    }
}

/** Draw original map thumbnails with group filters and pagination on any supported aspect ratio. */
void drawOverview(void* map, const std::vector<Ref>& list) {
    box(0, 0, static_cast<float>(LogicalWidth), static_cast<float>(LogicalHeight), 0.045f, 0.065f, 0.085f);
    text(Margin, 18, "MAP OVERVIEW");
    button(LogicalWidth - 160.0f, 18, 136, 42, "Back", 10);
    // Filter buttons wrap rather than clipping on the minimum portrait canvas.
    constexpr const char* Labels[] = {"All", "Campaign", "Easy", "Medium", "Hard", "Madness"};
    const int filterColumns = std::max(1, static_cast<int>((LogicalWidth - 2 * Margin) / 120));
    for (int index = 0; index < 6; ++index)
        button(Margin + (index % filterColumns) * 120, 70.0f + (index / filterColumns) * 38, 112, 34, Labels[index], 20 + index, filter == index);
    const int count = columns(), capacity = count * rows();
    const float width = (LogicalWidth - Margin * 2 - CardGap * (count - 1)) / count;
    const int pageCount = std::max(1, (static_cast<int>(list.size()) + capacity - 1) / capacity);
    page = std::clamp(page, 0, pageCount - 1);
    const auto selectedIndex = word(map, word(map, 0xc) ? 0xb : 10);
    for (int slot = 0; slot < capacity; ++slot) {
        const int index = page * capacity + slot;
        if (index >= static_cast<int>(list.size())) break;
        const Ref& ref = list[index];
        const float x = Margin + (slot % count) * (width + CardGap), y = HeaderHeight + (slot / count) * (CardHeight + CardGap);
        const bool selected = ref.catalog == (word(map, 0xc) != 0) && ref.index == selectedIndex;
        button(x, y, width, CardHeight, "", 1000 + index, selected);
        const auto thumb = thumbnails.find(ref.entry->name);
        if (thumb != thumbnails.end() && !ref.entry->locked) {
            const float imageWidth = std::min(width - 10, 180.0f * thumb->second.width / thumb->second.height);
            const float imageHeight = imageWidth * thumb->second.height / thumb->second.width;
            const float imageX = x + (width - imageWidth) / 2;
            const float imageY = y + 5 + (180 - imageHeight) / 2;
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, thumb->second.texture); glColor4f(1, 1, 1, 1);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 1); glVertex2f(imageX, imageY); glTexCoord2f(1, 1); glVertex2f(imageX + imageWidth, imageY);
            glTexCoord2f(1, 0); glVertex2f(imageX + imageWidth, imageY + imageHeight); glTexCoord2f(0, 0); glVertex2f(imageX, imageY + imageHeight); glEnd();
        } else {
            box(x + 5, y + 5, width - 10, 180, 0.08f, 0.11f, 0.13f);
            text(x + 15, y + 40, ref.entry->locked ? "LOCKED" : "Loading preview...");
        }
        std::string label = ref.entry->name;
        if (label.size() > 23) label.resize(23);
        text(x + 10, y + 195, label, 1, width - 20);
        text(x + 10, y + 222, ref.entry->locked ? "Campaign locked" : selected ? "Selected" : "Click to select", 0.85f, width - 20);
    }
    button(Margin, LogicalHeight - 63.0f, 120, 42, "< Previous", 11);
    button(LogicalWidth - Margin - 120, LogicalHeight - 63.0f, 120, 42, "Next >", 12);
    text(LogicalWidth / 2.0f - 100, LogicalHeight - 103.0f,
        std::to_string(page + 1) + " / " + std::to_string(pageCount) + "   " + std::to_string(list.size()) + " maps");
}

/** Switch native selections using the currently chosen group, wrapping at either end. */
bool step(int direction) {
    void* map = state(); if (!map) return false;
    if (word(map, 8) || word(map, 0x57)) return true;
    bind(map);
    const auto list = entries(map); if (list.empty()) return true;
    const bool catalog = word(map, 0xc) != 0;
    const int selected = word(map, catalog ? 0xb : 10);
    int at = -1;
    for (int index = 0; index < static_cast<int>(list.size()); ++index)
        if (list[index].catalog == catalog && list[index].index == selected) { at = index; break; }
    if (at < 0 && direction < 0) at = 0;
    const int count = static_cast<int>(list.size());
    at = ((at + direction) % count + count) % count;
    advanceSlide();
    if (!overview && count > 1) slide += direction;
    select(map, list[at]);
    return true;
}

/** Apply only desktop selection actions, delegating play and lock decisions to native touch code. */
void activate(int action, void* map) {
    if (action == 1) step(-1);
    else if (action == 2) step(1);
    else if (action == 3) { overview = true; page = 0; }
    else if (action == 4) { filter = 1; overview = true; page = 0; }
    else if (action == 5) desktopMapMenuNativeTap(menuContentOffset() + 240, 250);
    else if (action == 6) reinterpret_cast<void(__cdecl*)(void*)>(findExport("MAPMENU_backButton"))(originalMenu);
    else if (action == 10) overview = false;
    else if (action == 11) page = std::max(0, page - 1);
    else if (action == 12) { const int capacity = columns() * rows(); page = std::min(page + 1, std::max(0, (static_cast<int>(entries(map).size()) - 1) / capacity)); }
    else if (action >= 20 && action < 26) { filter = action - 20; page = 0; }
    else if (action >= 1000) {
        const auto list = entries(map); const int at = action - 1000;
        if (at < static_cast<int>(list.size())) { select(map, list[at]); overview = false; }
    }
}
}

/** Replace only native map-page interaction; the original game start and campaign logic remain intact. */
void installDesktopMapMenu() { desktop_map::enabled = true; log("DESKTOP_MAP_MENU arrows; native selection; responsive original-map overview"); }

/** Let configured desktop actions reuse map navigation without fabricating touch gestures. */
bool desktopMapMenuStep(int direction) { return desktop_map::step(direction); }

/** Forward a deliberate desktop tap through the original menu page and start-button dispatcher. */
void desktopMapMenuNativeTap(float x, float y) {
    hostTouch = HostTouch{}; hostTouch.x = hostTouch.previousX = hostTouch.startX = x;
    hostTouch.y = hostTouch.previousY = hostTouch.startY = y; hostTouch.tapCount = 1; hostTouchCount = 1;
    // Native page taps need no EAGLView gesture bookkeeping after desktop drag filtering.
    reinterpret_cast<void(__cdecl*)(void*, void*)>(findExport("MENU_handleTouchEnded"))(originalMenu, &hostTouchSet);
    if (void* map = desktop_map::state())
        log("DESKTOP_MAP_NATIVE_TAP x=" + std::to_string(x) + " y=" + std::to_string(y)
            + " starting=" + std::to_string(desktop_map::word(map, 8)));
}

/** Render the real map preview, then desktop controls or the requested original-map overview. */
bool renderDesktopMapMenu(void* menu, void* pageObject, float alpha) {
    using namespace desktop_map;
    if (!enabled || pageObject != findExport("startGamePage")) { owner = nullptr; return false; }
    void* map = *reinterpret_cast<void**>(static_cast<unsigned char*>(menu) + 0x164);
    if (!map) return false;
    bind(map); steady(map); advanceSlide();
    if (overview || word(map, 8) || word(map, 0x57)) slide = 0;
    if (word(map, 8) || word(map, 0x57)) wheelRemainder = 0;
    const auto list = entries(map);
    const int selectedAt = selection(map, list);
    if (overview) preview(menu, pageObject, map, list);
    else if (list.size() > 1 && !word(map, 8) && !word(map, 0x57)) {
        const long long middle = static_cast<long long>(std::llround(-slide));
        std::vector<Ref> adjacent{list[selectedAt]};
        for (int distance = 0; distance <= 2; ++distance) {
            adjacent.push_back(carouselEntry(list, selectedAt, middle - distance));
            adjacent.push_back(carouselEntry(list, selectedAt, middle + distance));
        }
        preview(menu, pageObject, map, adjacent, true);
    }
    nativeRender(menu, pageObject, alpha);
    begin(); buttons.clear();
    // The original endless-mode chooser remains a native modal with its own exact hit regions.
    if (word(map, 0x57)) { end(); return true; }
    if (overview) drawOverview(map, list);
    else {
        if (list.size() > 1 && !word(map, 8)) {
            drawCarousel(list, selectedAt);
        }
        const float center = LogicalWidth / 2.0f;
        const float arrowY = LogicalHeight * 0.42f;
        box(0, 0, static_cast<float>(LogicalWidth), 40, 0.07f, 0.09f, 0.11f);
        button(Margin, 8, 120, 42, "Back", 6);
        const int selectedIndex = word(map, word(map, 0xc) ? 0xb : 10);
        const int selectedCount = word(map, word(map, 0xc) ? 9 : 4);
        auto* selected = selectedIndex >= 0 && selectedIndex < selectedCount
            ? reinterpret_cast<Entry*>(word(map, word(map, 0xc) ? 5 : 3)) + selectedIndex : nullptr;
        const std::string title = selected && selected->name ? selected->name : "Select a map";
        float titleWidth = 0;
        for (const unsigned char character : title) if (character < glyphs.size()) titleWidth += glyphs[character].gmfCellIncX * 26;
        text(center - std::min(250.0f, titleWidth) / 2, 5, title, 1, 250);
        button(std::max(Margin, center - 290), arrowY, 70, 70, "<", 1);
        button(std::min(LogicalWidth - Margin - 70, center + 220), arrowY, 70, 70, ">", 2);
        // Cover the old campaign/DLC toggle so the catalog remains understandable after its fourth campaign.
        box(center - 170, LogicalHeight - 140.0f, 340, 140, 0.07f, 0.10f, 0.13f);
        button(center - 160, LogicalHeight - 60.0f, 150, 48, "Campaign", 4);
        button(center + 10, LogicalHeight - 60.0f, 150, 48, "Map overview", 3);
        button(center - 110, LogicalHeight - 123.0f, 220, 48, "Play selected map", 5);
        buttonLabel(center - 250, 80, 500, 34, "Left / Right to select  |  Enter to play");
    }
    end(); return true;
}

/** Consume overview and arrow events before legacy touch-carousel dispatch. */
bool desktopMapMenuInput(HWND window, UINT message, WPARAM first, LPARAM second) {
    using namespace desktop_map;
    void* map = state(); if (!map) return false;
    bind(map);
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE || message == WM_KILLFOCUS || message == WM_SIZE) {
        pressed = false; pressedAction = -1;
        if (message != WM_CAPTURECHANGED && GetCapture() == window) ReleaseCapture();
        return false;
    }
    if (word(map, 8) && (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_KEYDOWN || message == WM_MOUSEWHEEL)) return true;
    if (word(map, 0x57) && message == WM_KEYDOWN) {
        if (first == VK_ESCAPE) word(map, 0x57) = 0;
        return true;
    }
    if (message == WM_KEYDOWN) {
        if (desktopMapMenuBoundKey(first, second)) return true;
        if (first == VK_LEFT) return step(-1);
        if (first == VK_RIGHT) return step(1);
        if (first == VK_ESCAPE && overview) { overview = false; return true; }
        if (first == 'M') { overview = !overview; page = 0; return true; }
        if ((first == VK_RETURN || first == VK_SPACE) && !overview) {
            if (!(second & (1LL << 30))) desktopMapMenuNativeTap(menuContentOffset() + 240, 250);
            return true;
        }
    }
    if (message == WM_MOUSEWHEEL) {
        if (word(map, 0x57)) return true;
        if (wheelOverview != overview) { wheelRemainder = 0; wheelOverview = overview; }
        wheelRemainder += static_cast<short>(HIWORD(first));
        const int notches = wheelRemainder / WHEEL_DELTA;
        wheelRemainder %= WHEEL_DELTA;
        if (overview) {
            const int capacity = columns() * rows();
            const int lastPage = std::max(0, (static_cast<int>(entries(map).size()) - 1) / capacity);
            page = std::clamp(page - notches, 0, lastPage);
        } else if (notches) step(-notches);
        return true;
    }
    if (message == WM_RBUTTONUP && overview) { overview = false; return true; }
    if (message != WM_LBUTTONDOWN && message != WM_LBUTTONUP && message != WM_MOUSEMOVE) return false;
    float x = static_cast<float>(static_cast<short>(LOWORD(second))), y = static_cast<float>(static_cast<short>(HIWORD(second)));
    hostDisplay.pointer(x, y);
    pointerX = x; pointerY = y;
    if (message == WM_MOUSEMOVE) {
        if (pressed && pressedAction >= 0) {
            bool inside = false;
            for (const auto& button : buttons) if (button.action == pressedAction
                && x >= button.x && x <= button.x + button.w && y >= button.y && y <= button.y + button.h) inside = true;
            if (!inside || !(first & MK_LBUTTON)) pressedAction = -3;
        }
        return overview || pressed || (first & MK_LBUTTON);
    }
    if (message == WM_LBUTTONDOWN) {
        pressedAction = -1;
        if (!word(map, 0x57)) {
            for (const auto& button : buttons)
                if (x >= button.x && x <= button.x + button.w && y >= button.y && y <= button.y + button.h) pressedAction = button.action;
        }
        if (pressedAction < 0 && !overview) pressedAction = -2;
        pressX = x; pressY = y; pressed = true; SetCapture(window); return true;
    }
    if (pressed) {
        const int action = pressedAction;
        pressed = false; pressedAction = -1; ReleaseCapture();
        if (std::abs(x - pressX) <= ClickTolerance && std::abs(y - pressY) <= ClickTolerance) {
            if (action == -2) {
                // The mobile selector accepts start taps only in the right half of its artwork.
                if (!word(map, 0x57) && y > 100 && y < LogicalHeight - 150 && std::abs(x - LogicalWidth / 2.0f) < 160)
                    desktopMapMenuNativeTap(menuContentOffset() + 240, y);
                else desktopMapMenuNativeTap(x, y);
            }
            else {
                for (const auto& button : buttons)
                    if (button.action == action && x >= button.x && x <= button.x + button.w
                        && y >= button.y && y <= button.y + button.h) { activate(action, map); break; }
            }
        }
        return true;
    }
    return overview;
}
