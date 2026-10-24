// Anchor the original 320-point menu artwork on a real desktop camera canvas.
#pragma once

// Original menu assets, labels, and hit regions occupy 320 points, independently of camera width.
constexpr float MenuArtworkWidth = 320.0f, MenuSidebarWidth = 96.0f;
// Widen the original side strip while keeping its vertical rhythm and native hit layout.
constexpr float MenuSidebarScale = 1.4f, DesktopSidebarWidth = MenuSidebarWidth * MenuSidebarScale;

int __cdecl nativePng(const char* name);

namespace desktop_skin {
// Reuse the original side-tab artwork instead of introducing another desktop button theme.
GLuint tabTexture = 0;
bool textureAttempted = false;

/** Paint the native green diagonal tab artwork, with its original translucent edges. */
void button(float x, float y, float width, float height, bool selected = false, bool pressed = false) {
    if (!textureAttempted) {
        textureAttempted = true;
        glGenTextures(1, &tabTexture); glBindTexture(GL_TEXTURE_2D, tabTexture);
        if (!nativePng("uielements")) { glDeleteTextures(1, &tabTexture); tabTexture = 0; }
        else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
    }
    // Keep hit geometry stable while the held face moves slightly inward.
    constexpr float PressInset = 2.0f, PressBrightness = 0.65f;
    if (pressed) { x += PressInset; y += PressInset; width -= PressInset * 2; height -= PressInset * 2; }
    const float brightness = pressed ? PressBrightness : 1.0f;
    glDisable(GL_TEXTURE_2D);
    glColor4f((selected ? 0.27f : 0.14f)*brightness, (selected ? 0.31f : 0.20f)*brightness, 0.10f*brightness, 0.94f);
    glBegin(GL_QUADS); glVertex2f(x,y); glVertex2f(x+width,y);
    glVertex2f(x+width,y+height); glVertex2f(x,y+height); glEnd();
    if (tabTexture) {
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tabTexture);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f((selected ? 0.70f : 0.46f)*brightness, (selected ? 0.80f : 0.56f)*brightness, (selected ? 0.30f : 0.24f)*brightness, 0.86f);
        // Match the native side-tab UVs and tint its neutral art with the original olive palette.
        glBegin(GL_QUADS);
        glTexCoord2f(0.5f,0.8125f); glVertex2f(x,y); glTexCoord2f(0.875f,0.8125f); glVertex2f(x+width,y);
        glTexCoord2f(0.875f,1); glVertex2f(x+width,y+height); glTexCoord2f(0.5f,1); glVertex2f(x,y+height);
        glEnd(); glDisable(GL_TEXTURE_2D);
    }
}
}
// Original local draw helpers have verified non-relative five-byte prologues.
using OriginalMenuTabs = void(__cdecl*)(void*, void*, float);
using OriginalMenuLogo = void(__cdecl*)(void*, float);
OriginalMenuTabs originalLandscapeTabs = nullptr;
OriginalMenuLogo originalLandscapeLogo = nullptr;
OriginalMenuLogo originalLandscapeBackPanel = nullptr;
// Page callback shims scope the 2D anchor, so full-window transitions keep their original canvas.
using OriginalMenuPageRender = void(__cdecl*)(void*, void*, float);
std::map<void*, OriginalMenuPageRender> originalLandscapePageRenders;
// Dedicated full-canvas calls in settlement/unlock renderers retain their original draw implementations.
using OriginalMenuBackdropModel = void(__cdecl*)(void*, const float*);
using OriginalMenuBackdropBox = void(__cdecl*)(float, float, float, float, float);
OriginalMenuBackdropModel originalLandscapeBackdropModel = nullptr;
OriginalMenuBackdropBox originalLandscapeBackdropBox = nullptr;
/** Render the desktop map selector before applying the legacy page anchor. */
bool renderDesktopMapMenu(void* menu, void* page, float alpha);

/** Center content while preserving the native size of labels and buttons. */
float menuContentOffset() { return std::max(0.0f, (LogicalWidth - MenuArtworkWidth) * 0.5f); }

/** Keep the title, achievements, and version together against the right edge. */
float menuLogoOffset() { return std::max(0.0f, LogicalWidth - MenuArtworkWidth); }

// Native generic tabs use page entries and these original hit-layout constants.
void* heldLandscapePage = nullptr;
int heldLandscapeTab = -1;
bool landscapePointerHeld = false, landscapePressCancelled = false;

/** Find only native generic sidebar tabs using the original callback and entry table. */
int landscapeMenuTabAt(float x, float y, void* page) {
    if (!page || !originalMenu || !*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(originalMenu)+4)) return -1;
    if (*static_cast<void**>(page) != images.front()->base+0x2a8876 || x<0 || x>=DesktopSidebarWidth) return -1;
    const auto* base = images.front()->base;
    const float first = *reinterpret_cast<const float*>(base+0x362e80);
    const float pitch = *reinterpret_cast<const float*>(base+0x362e84);
    const float top = *reinterpret_cast<const float*>(base+0x362ea8);
    const float bottom = *reinterpret_cast<const float*>(base+0x362eac);
    for (int i=0; i<16 && *reinterpret_cast<void**>(static_cast<unsigned char*>(page)+0x18+8*i); ++i)
        if (y>=first+i*pitch+top && y<=first+i*pitch+bottom) return i;
    return -1;
}

/** Cancel native sidebar feedback when Windows interrupts the held pointer. */
void cancelLandscapeMenuPress() {
    landscapePointerHeld=false; landscapePressCancelled=true; heldLandscapeTab=-1; heldLandscapePage=nullptr;
}

/** Observe the real logical pointer; reject a release after leaving its native tab. */
bool landscapeMenuPointer(UINT message, float x, float y) {
    void* page = originalMenu ? *static_cast<void**>(originalMenu) : nullptr;
    const int hit = landscapeMenuTabAt(x,y,page);
    if (message==WM_LBUTTONDOWN) {
        heldLandscapePage=page; heldLandscapeTab=hit; landscapePointerHeld=true; landscapePressCancelled=false;
    } else if (message==WM_MOUSEMOVE && landscapePointerHeld && heldLandscapeTab>=0) {
        if (page!=heldLandscapePage || hit!=heldLandscapeTab) landscapePressCancelled=true;
    } else if (message==WM_LBUTTONUP) {
        const bool touchesTab=heldLandscapeTab>=0 || hit>=0;
        const bool accepted=!landscapePressCancelled && (!touchesTab
            || (landscapePointerHeld && page==heldLandscapePage && hit==heldLandscapeTab));
        landscapePointerHeld=false; heldLandscapeTab=-1; heldLandscapePage=nullptr; landscapePressCancelled=false;
        return accepted;
    }
    return true;
}

/** Shift only the menu's orthographic canvas; native perspective cameras retain their aspect. */
void __cdecl landscapeMenuPageRender(void* menu, void* page, float alpha) {
    if (renderDesktopMapMenu(menu, page, alpha)) return;
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glTranslatef(menuContentOffset(), 0, 0);
    glMatrixMode(GL_MODELVIEW);
    originalLandscapePageRenders.at(page)(menu, page, alpha);
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

/** Hold the original side strip at the window edge, including its native transition. */
void __cdecl landscapeMenuTabs(void* menu, void* page, float alpha) {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glTranslatef(-menuContentOffset(), 0, 0);
    glScalef(MenuSidebarScale, 1, 1);
    glMatrixMode(GL_MODELVIEW);
    originalLandscapeTabs(menu, page, alpha);
    if (landscapePointerHeld && !landscapePressCancelled && page==heldLandscapePage && heldLandscapeTab>=0) {
        const auto* base = images.front()->base;
        const float first = *reinterpret_cast<const float*>(base+0x362e80);
        const float pitch = *reinterpret_cast<const float*>(base+0x362e84);
        const float top = *reinterpret_cast<const float*>(base+0x362ea8);
        const float bottom = *reinterpret_cast<const float*>(base+0x362eac);
        const float y=first+heldLandscapeTab*pitch+top;
        const float slide=-(1-alpha)* *reinterpret_cast<float*>(static_cast<unsigned char*>(menu)+0x110);
        // Tint only the verified native tab face in the same animated/scaled canvas.
        glPushAttrib(GL_ALL_ATTRIB_BITS); glPushMatrix(); glLoadIdentity();
        glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_ALPHA_TEST);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE); glDisable(GL_FOG);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        // Darken the existing face enough to distinguish a held press from its selected tint.
        constexpr float NativePressShade = 0.28f;
        glColor4f(0,0,0,NativePressShade*alpha);
        glBegin(GL_QUADS); glVertex2f(slide,y); glVertex2f(slide+MenuSidebarWidth,y);
        glVertex2f(slide+MenuSidebarWidth,y+bottom-top); glVertex2f(slide,y+bottom-top); glEnd();
        glPopMatrix(); glPopAttrib();
    }
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

/** Anchor the original title group without changing the menu world or asset geometry. */
void __cdecl landscapeMenuLogo(void* menu, float alpha) {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glTranslatef(menuLogoOffset() - menuContentOffset(), 0, 0);
    glMatrixMode(GL_MODELVIEW);
    originalLandscapeLogo(menu, alpha);
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

/** Preserve the full-width original translucent panel behind centered page controls. */
void __cdecl landscapeMenuBackPanel(void* menu, float alpha) {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glTranslatef(-menuContentOffset(), 0, 0);
    glMatrixMode(GL_MODELVIEW);
    originalLandscapeBackPanel(menu, alpha);
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

/** Keep the settlement's original full-canvas textured backdrop aligned with the window. */
void __cdecl landscapeSettlementBackdrop(void* model, const float* color) {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glTranslatef(-menuContentOffset(), 0, 0);
    glMatrixMode(GL_MODELVIEW);
    originalLandscapeBackdropModel(model, color);
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

/** Keep each unlock slide's original full-canvas dark box behind the centered content. */
void __cdecl landscapeUnlockBackdrop(float x, float y, float width, float height, float alpha) {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glTranslatef(-menuContentOffset(), 0, 0);
    glMatrixMode(GL_MODELVIEW);
    originalLandscapeBackdropBox(x, y, width, height, alpha);
    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

/** Identify centered map/settlement pages, whose touch handlers contain no edge sidebar. */
bool centeredLandscapeMenuPage() {
    if (!originalMenu) return false;
    const void* page = *static_cast<void**>(originalMenu);
    return page == findExport("startGamePage") || page == findExport("endOfGamePage");
}

/** Return the same anchor selected by the original draw helper for an existing UI point. */
float menuPointOffset(float x, float y) {
    if (centeredLandscapeMenuPage()) return menuContentOffset();
    if (x < MenuSidebarWidth) return 0;
    if (y < 100 || y > LogicalHeight - 35) return menuLogoOffset();
    return menuContentOffset();
}

/** Adapt old verification points before passing them through the real Windows mouse path. */
POINT menuVerificationPoint(int x, int y) {
    // Legacy scripts selected the unbounded version hit strip beyond the 320-point artwork.
    if (!centeredLandscapeMenuPage() && y > LogicalHeight - 35 && x > MenuArtworkWidth) x = 305;
    const float screenX = !centeredLandscapeMenuPage() && x < MenuSidebarWidth
        ? x * MenuSidebarScale : x + menuPointOffset(static_cast<float>(x), static_cast<float>(y));
    return POINT{static_cast<LONG>(std::lround(screenX)), y};
}

/** Convert a physical-canvas touch to the corresponding unchanged native hit region. */
void menuTouchPoint(float& x, float& y) {
    if (!originalMenu || !*reinterpret_cast<unsigned char*>(static_cast<unsigned char*>(originalMenu) + 4)) return;
    if (centeredLandscapeMenuPage()) { x -= menuContentOffset(); return; }
    if (x < DesktopSidebarWidth) { x /= MenuSidebarScale; return; }
    if (y < 100 || y > LogicalHeight - 35) x -= menuLogoOffset();
    else x -= menuContentOffset();
    // Empty desktop space must not alias the original left-side tab hit zones.
    if (x < MenuSidebarWidth) x = 10000;
}

/** Preserve a verified original helper in a trampoline before redirecting its private mapping. */
void* installLandscapeMenuHelper(std::uint32_t address, void* replacement, unsigned char finalPush = 0x57) {
    auto* entry = images.front()->base + address;
    // Complete PUSH/MOV/PUSH/PUSH instructions contain no PIC-relative operands.
    const unsigned char Prologue[] = {0x55, 0x89, 0xe5, 0x53, finalPush};
    if (std::memcmp(entry, Prologue, sizeof(Prologue))) throw std::runtime_error("Unexpected original menu helper entry");
    constexpr unsigned TrampolineSize = sizeof(Prologue) + 5;
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, TrampolineSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!trampoline) throw std::runtime_error("Menu helper trampoline allocation failed");
    std::memcpy(trampoline, entry, sizeof(Prologue)); trampoline[sizeof(Prologue)] = 0xe9;
    const auto back = reinterpret_cast<std::uint32_t>(entry + sizeof(Prologue)) - reinterpret_cast<std::uint32_t>(trampoline + TrampolineSize);
    std::memcpy(trampoline + sizeof(Prologue) + 1, &back, sizeof(back));
    entry[0] = 0xe9;
    const auto next = reinterpret_cast<std::uint32_t>(replacement) - reinterpret_cast<std::uint32_t>(entry + 5);
    std::memcpy(entry + 1, &next, sizeof(next));
    FlushInstructionCache(GetCurrentProcess(), trampoline, TrampolineSize);
    FlushInstructionCache(GetCurrentProcess(), entry, 5);
    return trampoline;
}

/** Redirect one verified background call without affecting the shared renderer's other callers. */
void installLandscapeBackdropCall(std::uint32_t address, const void* expectedTarget, const void* replacement) {
    auto* call = images.front()->base + address;
    std::int32_t originalDisplacement = 0;
    std::memcpy(&originalDisplacement, call + 1, sizeof(originalDisplacement));
    if (call[0] != 0xe8 || call + 5 + originalDisplacement != expectedTarget)
        throw std::runtime_error("Unexpected original menu backdrop call");
    const auto displacement = reinterpret_cast<std::uint32_t>(replacement) - reinterpret_cast<std::uint32_t>(call + 5);
    std::memcpy(call + 1, &displacement, sizeof(displacement));
    FlushInstructionCache(GetCurrentProcess(), call, 5);
}

/** Reuse original tab/title rendering with responsive anchors; original files remain immutable. */
void installLandscapeMenu() {
    originalLandscapeTabs = reinterpret_cast<OriginalMenuTabs>(installLandscapeMenuHelper(0x2a8406, reinterpret_cast<void*>(&landscapeMenuTabs)));
    originalLandscapeLogo = reinterpret_cast<OriginalMenuLogo>(installLandscapeMenuHelper(0x2a799a, reinterpret_cast<void*>(&landscapeMenuLogo)));
    originalLandscapeBackPanel = reinterpret_cast<OriginalMenuLogo>(installLandscapeMenuHelper(0x2a8321, reinterpret_cast<void*>(&landscapeMenuBackPanel), 0x56));
    originalLandscapeBackdropModel = reinterpret_cast<OriginalMenuBackdropModel>(findExport("PGL_renderModelWithColor"));
    originalLandscapeBackdropBox = reinterpret_cast<OriginalMenuBackdropBox>(findExport("PGLU_drawBGBoxProper"));
    // These five calls exclusively draw full-canvas backgrounds; models and slide text stay centered.
    installLandscapeBackdropCall(0x2b3980, reinterpret_cast<void*>(originalLandscapeBackdropModel), reinterpret_cast<void*>(&landscapeSettlementBackdrop));
    for (const std::uint32_t address : {0x2aeaf6u, 0x2aff3bu, 0x2b17c4u, 0x2b20adu})
        installLandscapeBackdropCall(address, reinterpret_cast<void*>(originalLandscapeBackdropBox), reinterpret_cast<void*>(&landscapeUnlockBackdrop));
    // These original page tables share the renderer callback ABI at byte offset four.
    constexpr const char* PageNames[] = {"mainMenuPage", "mainMenuPagePaid", "mainMenuPageZero", "mainMenuPageGameInProgress",
        "mainMenuPageGameInProgressPaid", "mainMenuPageGameInProgressZero", "startGamePage", "endOfGamePage", "optionsMenuPage",
        "scoresMenuPage", "showScoresPage", "offlineScoresMenuPage", "uploadOptionsPage", "newsMenuPage", "accountChoosePage",
        "accountCreatePage", "accountChooseConfirmPage", "allowUploadPage", "unlockHardPage", "unlockMediumPage", "unlockInsanePage", "unlockOutroPage", "outroPage"};
    for (const char* name : PageNames) {
        auto* page = static_cast<unsigned char*>(findExport(name));
        if (!page) continue;
        auto& render = *reinterpret_cast<OriginalMenuPageRender*>(page + 4);
        if (!render) continue;
        originalLandscapePageRenders.emplace(page, render);
        render = &landscapeMenuPageRender;
    }
    log("LANDSCAPE_MENU native sidebar left; native title right; map/options/settlement centered");
}
