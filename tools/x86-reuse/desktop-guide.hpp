// Browse the APK's complete local encyclopedia inside the native OpenGL window.
#pragma once

int __cdecl nativePng(const char* name);

namespace desktop_guide {
// The shipped pages are an explicit local allowlist; no URL can start a browser or network request.
const std::vector<std::string> Pages = {"guide.html", "towerpedia.html", "enemypedia.html", "faq.html", "credits.html",
    "plasmatron.html", "laser.html", "railgun.html", "mortar.html", "missile.html", "flak.html",
    "electroslow.html", "electroshock.html", "electroboost.html", "mine.html", "bait.html", "flamethrower.html"};
// Logical sizes retain the game's established density on high-DPI and small windows.
constexpr float TextSize = 18, LineHeight = 27, PageMargin = 24, ScrollWidth = 16;
struct Run { std::string text, link; };
struct Block {
    int heading = 0;
    std::string image;
    std::vector<Run> runs;
    std::vector<std::vector<std::string>> rows;
};
struct Page { std::string title; std::vector<Block> blocks; };
struct Hit { float x, y, width, height; std::string action; };
struct Font { GLuint base = 0; float ascent = 0; std::array<float, 256> widths{}; };
// Parsed pages and textures are reused after the first visit to avoid repeat disk work.
std::map<std::string, Page> pageCache;
std::map<std::string, GLuint> imageCache;
std::vector<std::pair<std::string, float>> history;
std::vector<Hit> hits;
Font bodyFont, headingFont;
std::string currentPage = "guide.html", pressedAction;
bool visible = false, draggingScroll = false;
float scroll = 0, contentHeight = 0, viewHeight = 0, viewTop = 0, panelLeft = 0, panelWidth = 0, fontScale = 0;
float pointerX = -1, pointerY = -1;
int focusedHit = -1;
// The held link keeps its original bounds so moving outside cancels the entire click.
Hit pressedHit{};
bool pressCancelled = false;

/** Release a held guide control before navigation or capture loss. */
void cancelPress(HWND window) {
    const bool captured = draggingScroll || !pressedAction.empty();
    draggingScroll = false; pressedAction.clear(); pressCancelled = false;
    if (captured && GetCapture() == window) ReleaseCapture();
}

/** Decode original UTF-8 and common HTML entities into the native font's Latin glyph range. */
std::string decode(const std::string& source) {
    std::string decoded;
    const std::map<std::string, std::string> entities = {{"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""},
        {"apos", "'"}, {"nbsp", " "}, {"ouml", "\xf6"}, {"auml", "\xe4"}, {"uuml", "\xfc"},
        {"Ouml", "\xd6"}, {"Auml", "\xc4"}, {"Uuml", "\xdc"}, {"szlig", "\xdf"}, {"ndash", "-"}, {"mdash", "-"}};
    for (std::size_t i = 0; i < source.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(source[i]);
        if (ch == '&') {
            const auto end = source.find(';', i + 1);
            if (end != std::string::npos && end - i < 12) {
                const auto name = source.substr(i + 1, end - i - 1);
                const auto found = entities.find(name);
                if (found != entities.end()) { decoded += found->second; i = end; continue; }
                if (!name.empty() && name.front() == '#') {
                    const unsigned long value = std::strtoul(name.c_str() + (name.size() > 1 && name[1] == 'x' ? 2 : 1), nullptr,
                        name.size() > 1 && name[1] == 'x' ? 16 : 10);
                    decoded += value && value < 256 ? static_cast<char>(value) : '?'; i = end; continue;
                }
            }
        }
        if (ch == 0xc2 || ch == 0xc3) {
            if (i + 1 < source.size()) { decoded += static_cast<char>(((ch & 31) << 6) | (source[++i] & 63)); continue; }
        }
        decoded += ch == '\r' || ch == '\n' || ch == '\t' ? ' ' : static_cast<char>(ch);
    }
    return decoded;
}

/** Read one quoted attribute from the original XHTML without executing markup. */
std::string attribute(const std::string& tag, const std::string& name) {
    const auto at = tag.find(name + "=");
    if (at == std::string::npos) return {};
    const auto start = at + name.size() + 1;
    if (start >= tag.size() || (tag[start] != '\"' && tag[start] != '\'')) return {};
    const auto end = tag.find(tag[start], start + 1);
    return end == std::string::npos ? std::string{} : decode(tag.substr(start + 1, end - start - 1));
}

/** Strip markup from a table cell while preserving all visible values. */
std::string cellText(const std::string& html) {
    std::string text;
    bool inTag = false;
    for (const char ch : html) {
        if (ch == '<') inTag = true;
        else if (ch == '>') { inTag = false; text += ' '; }
        else if (!inTag) text += ch;
    }
    std::istringstream words(decode(text)); std::string word, result;
    while (words >> word) { if (!result.empty()) result += ' '; result += word; }
    return result;
}

/** Recover every original table row and column, including secondary resistance/reload tables. */
Block parseTable(const std::string& source) {
    Block block;
    std::size_t pos = 0;
    while ((pos = source.find("<tr", pos)) != std::string::npos) {
        const auto rowEnd = source.find("</tr>", pos);
        if (rowEnd == std::string::npos) break;
        std::vector<std::string> cells;
        std::size_t cursor = source.find('>', pos) + 1;
        while (cursor < rowEnd) {
            const auto data = source.find("<td", cursor), head = source.find("<th", cursor);
            const auto start = std::min(data, head);
            if (start == std::string::npos || start >= rowEnd) break;
            const auto valueStart = source.find('>', start) + 1;
            const auto end = source.find(start == head ? "</th>" : "</td>", valueStart);
            if (end == std::string::npos || end > rowEnd) break;
            cells.push_back(cellText(source.substr(valueStart, end - valueStart)));
            cursor = end + 5;
        }
        if (!cells.empty()) block.rows.push_back(std::move(cells));
        pos = rowEnd + 5;
    }
    return block;
}

/** Parse only the shipped body blocks, retaining linked prose, images, headings, and footnotes. */
Page parsePage(const std::string& source) {
    Page page; Block current;
    std::string activeLink, pendingImage;
    const auto body = source.find("<body");
    std::size_t pos = body == std::string::npos ? 0 : source.find('>', body) + 1;
    const auto flush = [&]() {
        bool hasText = false;
        for (const auto& run : current.runs) if (run.text.find_first_not_of(" \t\r\n") != std::string::npos) hasText = true;
        if (hasText) {
            if (current.heading == 1 && page.title.empty()) {
                for (const auto& run : current.runs) page.title += run.text;
            } else page.blocks.push_back(std::move(current));
        }
        current = Block{};
    };
    while (pos < source.size()) {
        if (source[pos] != '<') {
            const auto end = source.find('<', pos);
            std::string text = decode(source.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
            if (!text.empty()) current.runs.push_back({std::move(text), activeLink});
            pos = end == std::string::npos ? source.size() : end; continue;
        }
        const auto end = source.find('>', pos);
        if (end == std::string::npos) break;
        const auto tag = source.substr(pos + 1, end - pos - 1);
        const auto nameEnd = tag.find_first_of(" \t\r\n/");
        const auto name = tag.substr(0, nameEnd);
        if (tag.compare(0, 5, "table") == 0) {
            flush(); const auto tableEnd = source.find("</table>", end);
            if (tableEnd == std::string::npos) break;
            page.blocks.push_back(parseTable(source.substr(end + 1, tableEnd - end - 1)));
            pos = tableEnd + 8; continue;
        }
        if (name == "a") activeLink = attribute(tag, "href");
        else if (tag == "/a") activeLink.clear();
        else if (name == "img") pendingImage = attribute(tag, "src");
        else if (name == "h1" || name == "h2" || name == "h3" || name == "p" || name == "li") {
            flush();
            if (name.front() == 'h') { current.heading = name[1] - '0'; current.image = pendingImage; pendingImage.clear(); }
            if (name == "li") current.runs.push_back({"- ", {}});
        } else if (tag == "/h1" || tag == "/h2" || tag == "/h3" || tag == "/p" || tag == "/li" || tag == "/div" || name == "br") flush();
        else if (tag == "/body") break;
        pos = end + 1;
    }
    flush();
    if (page.title.empty()) page.title = "Original Guide";
    return page;
}

/** Open an original offline page and keep a small, useful back stack. */
bool open(const std::string& filename) {
    std::string pageName = std::filesystem::path(filename).filename().string();
    const auto suffix = pageName.find("_de.html");
    if (suffix != std::string::npos) pageName.replace(suffix, 8, ".html");
    if (std::find(Pages.begin(), Pages.end(), pageName) == Pages.end()) return false;
    if (!pageCache.count(pageName)) {
        std::ifstream file(assetsDirectory / pageName, std::ios::binary);
        if (!file) { log("GUIDE_MISSING " + pageName); return false; }
        pageCache.emplace(pageName, parsePage(std::string(std::istreambuf_iterator<char>(file), {})));
    }
    if (visible && currentPage != pageName) history.push_back({currentPage, scroll});
    if (!visible) history.clear();
    currentPage = pageName; visible = true; scroll = 0; focusedHit = -1; pressedAction.clear();
    log("DESKTOP_GUIDE " + pageName);
    return true;
}

/** Navigate without crossing the local encyclopedia boundary. */
void activate(const std::string& action) {
    if (action == "#close") { visible = false; draggingScroll = false; }
    else if (action == "#back") {
        if (history.empty()) { if (currentPage != "guide.html") open("guide.html"); }
        else {
            const auto previous = history.back(); history.pop_back();
            currentPage = previous.first; scroll = previous.second; focusedHit = -1;
        }
    } else open(action);
}

/** Build DPI-scaled bitmap glyphs in the current context with measured advances. */
void makeFont(Font& font, int size, bool bold, float scale) {
    if (font.base) glDeleteLists(font.base, 256);
    font.base = glGenLists(256);
    HFONT selected = CreateFontW(-std::max(10, static_cast<int>(std::lround(size * scale))), 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
    HGDIOBJ previous = SelectObject(windowDc, selected);
    TEXTMETRICW metrics{}; GetTextMetricsW(windowDc, &metrics);
    font.ascent = metrics.tmAscent / scale;
    for (unsigned i = 0; i < 256; ++i) {
        int width = 0; GetCharWidth32W(windowDc, i, i, &width); font.widths[i] = width / scale;
    }
    wglUseFontBitmapsW(windowDc, 0, 256, font.base);
    SelectObject(windowDc, previous); DeleteObject(selected);
}

/** Return a measured logical advance for wrapping and link hit regions. */
float measure(const std::string& text, const Font& font) {
    float width = 0; for (const unsigned char ch : text) width += font.widths[ch]; return width;
}

/** Draw a solid logical rectangle using the current color. */
void rectangle(float x, float y, float width, float height) {
    glDisable(GL_TEXTURE_2D); glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + width, y); glVertex2f(x + width, y + height); glVertex2f(x, y + height); glEnd();
}

/** Draw top-aligned text; bitmap sizes already match the framebuffer density. */
void text(float x, float y, const std::string& value, const Font& font) {
    glDisable(GL_TEXTURE_2D); glRasterPos2f(x, y + font.ascent); glListBase(font.base);
    glCallLists(static_cast<GLsizei>(value.size()), GL_UNSIGNED_BYTE, value.data());
}

/** Test logical pointer coordinates against one rendered control. */
bool contains(const Hit& hit, float x, float y) {
    return x >= hit.x && x < hit.x + hit.width && y >= hit.y && y < hit.y + hit.height;
}

/** Wrap prose and long table headings without hiding a word or column. */
float paragraph(const std::vector<Run>& runs, float x, float y, float width, const Font& font, bool draw, bool heading = false) {
    float cursor = x, lineY = y;
    const float line = heading ? 31.0f : LineHeight;
    bool hasWord = false;
    for (const auto& run : runs) {
        std::istringstream words(run.text); std::string word;
        while (words >> word) {
            const float advance = measure(word, font), space = cursor == x ? 0 : font.widths[' '];
            if (cursor != x && cursor + space + advance > x + width) { lineY += line; cursor = x; }
            else cursor += space;
            const bool localLink = std::find(Pages.begin(), Pages.end(), run.link) != Pages.end();
            if (draw) {
                if (localLink) {
                    Hit hit{cursor, lineY, advance, line, run.link}; hits.push_back(hit);
                    const bool hot = contains(hit, pointerX, pointerY) || focusedHit == static_cast<int>(hits.size()) - 1;
                    glColor3f(hot ? 1.0f : 0.72f, hot ? 0.94f : 0.82f, hot ? 0.56f : 0.34f);
                } else if (heading) glColor3f(0.96f, 0.97f, 1.0f);
                else glColor3f(0.90f, 0.92f, 0.87f);
                text(cursor, lineY, word, font);
                if (localLink) rectangle(cursor, lineY + line - 5, advance, 1);
            }
            cursor += advance; hasWord = true;
        }
    }
    return hasWord ? lineY - y + line : 0;
}

/** Load each original PNG once into a private texture owned by this overlay. */
void picture(const std::string& filename, float x, float y, float size) {
    if (filename.empty() || filename.find_first_of("/\\:") != std::string::npos) return;
    auto found = imageCache.find(filename);
    if (found == imageCache.end()) {
        GLuint texture = 0; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
        if (!nativePng(std::filesystem::path(filename).stem().string().c_str())) { glDeleteTextures(1, &texture); texture = 0; }
        if (texture) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        }
        found = imageCache.emplace(filename, texture).first;
    }
    if (!found->second) return;
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, found->second); glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x, y); glTexCoord2f(1, 0); glVertex2f(x + size, y);
    glTexCoord2f(1, 1); glVertex2f(x + size, y + size); glTexCoord2f(0, 1); glVertex2f(x, y + size); glEnd();
    glDisable(GL_TEXTURE_2D);
}

/** Lay out all statistics; narrow windows turn each level into a fully labelled card. */
float table(const Block& block, float x, float y, float width, bool draw) {
    if (block.rows.empty()) return 0;
    const auto& headers = block.rows.front();
    const bool narrow = width < 650;
    float cursorY = y;
    if (narrow) {
        for (std::size_t row = 1; row < block.rows.size(); ++row) {
            const auto& cells = block.rows[row];
            const float start = cursorY;
            for (std::size_t col = 0; col < cells.size(); ++col) {
                const std::string label = col < headers.size() ? headers[col] : "Value";
                cursorY += paragraph({{label + ": " + cells[col], {}}}, x + 12, cursorY + 5, width - 24, bodyFont, draw);
            }
            cursorY += 14;
            if (draw) { glColor3f(0.28f, 0.33f, 0.17f); rectangle(x, cursorY - 2, width, 1); }
            if (cursorY <= start) break;
        }
    } else {
        const std::size_t columns = headers.size();
        if (!columns) return 0;
        const float columnWidth = width / columns;
        for (std::size_t row = 0; row < block.rows.size(); ++row) {
            float height = LineHeight;
            for (const auto& value : block.rows[row])
                height = std::max(height, paragraph({{value, {}}}, 0, 0, columnWidth - 20, bodyFont, false));
            height += 16;
            if (draw) {
                if (!row) glColor3f(0.28f, 0.32f, 0.14f);
                else if (row % 2) glColor3f(0.14f, 0.18f, 0.105f);
                else glColor3f(0.105f, 0.14f, 0.085f);
                rectangle(x, cursorY, width, height - 1);
                for (std::size_t col = 0; col < block.rows[row].size(); ++col)
                    paragraph({{block.rows[row][col], {}}}, x + col * columnWidth + 10, cursorY + 8, columnWidth - 20, bodyFont, true, !row);
            }
            cursorY += height;
        }
    }
    return cursorY - y + 24;
}

/** Measure or paint the complete document using one shared layout routine. */
float document(const Page& page, float x, float y, float width, bool draw) {
    const float start = y;
    for (const auto& block : page.blocks) {
        if (!block.rows.empty()) { y += table(block, x, y, width, draw); continue; }
        if (block.heading) {
            y += 12;
            const float inset = block.image.empty() ? 0.0f : 88.0f;
            const float height = std::max(block.image.empty() ? 0.0f : 72.0f,
                paragraph(block.runs, x + inset, y + (inset ? 20 : 0), width - inset, headingFont, draw, true));
            if (draw && !block.image.empty()) picture(block.image, x, y, 72);
            y += height + 12;
        } else y += paragraph(block.runs, x, y, width, bodyFont, draw) + 14;
    }
    return y - start + 20;
}

/** Draw a keyboard-focusable navigation control. */
void button(float x, float y, float width, const std::string& label, const std::string& action) {
    hits.push_back({x, y, width, 38, action});
    const bool hot = contains(hits.back(), pointerX, pointerY) || focusedHit == static_cast<int>(hits.size()) - 1;
    const bool selected = action == currentPage;
    const bool held = !pressCancelled && pressedAction==action && pressedHit.x==x && pressedHit.y==y;
    desktop_skin::button(x,y,width,38,hot||selected,held);
    desktop_map::buttonLabel(x,y,width,38,label);
}

/** Render a modal native encyclopedia with scrolling and original artwork. */
bool render() {
    if (!visible) return false;
    const float scale = static_cast<float>(hostDisplay.viewportHeight) / LogicalHeight;
    glPushAttrib(GL_ALL_ATTRIB_BITS); glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    GLint previousMode = GL_MODELVIEW; glGetIntegerv(GL_MATRIX_MODE, &previousMode);
    glViewport(hostDisplay.x, hostDisplay.y, hostDisplay.viewportWidth, hostDisplay.viewportHeight);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, LogicalWidth, LogicalHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST); glDisable(GL_FOG); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    // Original PGL may leave client arrays enabled; immediate drawing must not inherit their state.
    glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (!bodyFont.base || std::abs(fontScale - scale) > 0.005f) {
        makeFont(bodyFont, static_cast<int>(TextSize), false, scale); makeFont(headingFont, 24, true, scale); fontScale = scale;
    }
    hits.clear();
    panelWidth = std::min(1180.0f, LogicalWidth - 32.0f); panelLeft = (LogicalWidth - panelWidth) / 2;
    glColor4f(0.075f, 0.105f, 0.055f, 0.97f); rectangle(0, 0, static_cast<float>(LogicalWidth), static_cast<float>(LogicalHeight));
    glColor3f(0.105f, 0.14f, 0.075f); rectangle(panelLeft, 20, panelWidth, LogicalHeight - 40.0f);
    desktop_skin::button(panelLeft,20,panelWidth,56);
    const auto& page = pageCache.at(currentPage);
    desktop_map::text(panelLeft+PageMargin,35,page.title,0.9f,panelWidth-130);
    button(panelLeft + panelWidth - 90, 29, 70, "CLOSE", "#close");
    const std::array<std::pair<const char*, const char*>, 6> navigation = {{{"BACK", "#back"}, {"GUIDE", "guide.html"},
        {"TOWERS", "towerpedia.html"}, {"ENEMIES", "enemypedia.html"}, {"FAQ", "faq.html"}, {"CREDITS", "credits.html"}}};
    const float navWidth = (panelWidth - 2 * PageMargin - 5 * 6) / 6;
    for (std::size_t i = 0; i < navigation.size(); ++i)
        button(panelLeft + PageMargin + i * (navWidth + 6), 85, navWidth, navigation[i].first, navigation[i].second);
    viewTop = 145; viewHeight = LogicalHeight - viewTop - 72;
    const float documentWidth = panelWidth - 2 * PageMargin - ScrollWidth - 10;
    contentHeight = document(page, 0, 0, documentWidth, false);
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, contentHeight - viewHeight));
    glEnable(GL_SCISSOR_TEST);
    const float sx = static_cast<float>(hostDisplay.viewportWidth) / LogicalWidth;
    glScissor(hostDisplay.x + static_cast<int>((panelLeft + PageMargin) * sx),
        hostDisplay.y + static_cast<int>((LogicalHeight - viewTop - viewHeight) * scale),
        static_cast<int>(documentWidth * sx), static_cast<int>(viewHeight * scale));
    const auto navigationHits = hits.size();
    document(page, panelLeft + PageMargin, viewTop - scroll, documentWidth, true);
    glDisable(GL_SCISSOR_TEST);
    // Offscreen links cannot receive pointer or keyboard focus.
    hits.erase(std::remove_if(hits.begin() + navigationHits, hits.end(), [](const Hit& hit) {
        return hit.y < viewTop || hit.y + hit.height > viewTop + viewHeight;
    }), hits.end());
    if (contentHeight > viewHeight) {
        const float trackX = panelLeft + panelWidth - PageMargin - ScrollWidth;
        glColor3f(0.16f, 0.20f, 0.105f); rectangle(trackX, viewTop, ScrollWidth, viewHeight);
        const float thumb = std::max(32.0f, viewHeight * viewHeight / contentHeight);
        glColor3f(0.59f, 0.57f, 0.29f); rectangle(trackX + 3, viewTop + scroll / (contentHeight - viewHeight) * (viewHeight - thumb), ScrollWidth - 6, thumb);
    }
    glColor3f(0.55f, 0.61f, 0.43f);
    text(panelLeft + PageMargin, LogicalHeight - 55.0f,
        panelWidth < 700 ? "Offline guide  |  Scroll: wheel  |  Esc: close" : "Original offline guide  |  Wheel / PgUp / PgDn  |  Esc: close", bodyFont);
    glMatrixMode(GL_TEXTURE); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(previousMode);
    glPopClientAttrib(); glPopAttrib();
    return true;
}

/** Consume modal input before any original map, camera, or tower handler receives it. */
bool input(HWND window, UINT message, WPARAM first, LPARAM second) {
    if (message == WM_KILLFOCUS || message == WM_CANCELMODE || message == WM_CAPTURECHANGED || message == WM_SIZE)
        cancelPress(window);
    if (!visible) return false;
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
        pointerX = static_cast<float>(static_cast<short>(LOWORD(second)));
        pointerY = static_cast<float>(static_cast<short>(HIWORD(second))); hostDisplay.pointer(pointerX, pointerY);
        const float trackX = panelLeft + panelWidth - PageMargin - ScrollWidth;
        if (message == WM_LBUTTONDOWN && pointerX >= trackX && pointerX <= trackX + ScrollWidth && pointerY >= viewTop && pointerY <= viewTop + viewHeight) {
            draggingScroll = true; SetCapture(window);
        }
        if (draggingScroll && contentHeight > viewHeight) {
            const float thumb = std::max(32.0f, viewHeight * viewHeight / contentHeight);
            scroll = std::clamp((pointerY - viewTop - thumb / 2) / (viewHeight - thumb), 0.0f, 1.0f) * (contentHeight - viewHeight);
        }
        if (message == WM_LBUTTONDOWN) {
            pressedAction.clear(); pressCancelled = false;
            if (!draggingScroll) for (const auto& hit : hits)
                if (contains(hit, pointerX, pointerY) && (hit.y < viewTop || pointerY < viewTop + viewHeight)) {
                    pressedAction = hit.action; pressedHit = hit; SetCapture(window);
                }
        }
        if (message == WM_MOUSEMOVE && !pressedAction.empty()
            && (!contains(pressedHit, pointerX, pointerY) || !(first & MK_LBUTTON))) pressCancelled = true;
        if (message == WM_LBUTTONUP) {
            const std::string action = !draggingScroll && !pressCancelled && contains(pressedHit,pointerX,pointerY)
                ? pressedAction : std::string{};
            cancelPress(window);
            if (!action.empty()) activate(action);
        }
        return true;
    }
    if (message == WM_MOUSEWHEEL) { cancelPress(window); scroll -= static_cast<short>(HIWORD(first)) / 120.0f * LineHeight * 3; return true; }
    if (message == WM_KEYDOWN) {
        cancelPress(window);
        if (first == VK_ESCAPE) activate("#close");
        else if (first == VK_BACK) activate("#back");
        else if (first == VK_UP) scroll -= LineHeight * 2;
        else if (first == VK_DOWN) scroll += LineHeight * 2;
        else if (first == VK_PRIOR) scroll -= viewHeight * 0.85f;
        else if (first == VK_NEXT || first == VK_SPACE) scroll += viewHeight * 0.85f;
        else if (first == VK_HOME) scroll = 0;
        else if (first == VK_END) scroll = std::max(0.0f, contentHeight - viewHeight);
        else if (first == VK_TAB && !hits.empty()) focusedHit = (focusedHit + (GetKeyState(VK_SHIFT) < 0 ? static_cast<int>(hits.size()) - 1 : 1)) % static_cast<int>(hits.size());
        else if (first == VK_RETURN && focusedHit >= 0 && focusedHit < static_cast<int>(hits.size())) activate(hits[focusedHit].action);
        return true;
    }
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_KEYUP || message == WM_CHAR || message == WM_SYSCHAR;
}
}
