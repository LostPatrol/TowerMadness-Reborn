// Core OpenGL rendering of original meshes, PNG materials, and Windows text.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <json.hpp>
#include "renderer.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>

namespace tmreborn {
namespace {
using Json = nlohmann::json;
using Microsoft::WRL::ComPtr;

// Original gameplay uses a 45-degree vertical FOV at camera height fifty.
constexpr float kCameraFov = 0.785398163f;
// Native eye keeps a fixed five-unit Y offset; its minimum zoom height is .8.
constexpr float kCameraGroundOffset = 5, kNativeCameraHeight = 50, kMinimumDistance = 0.8f;
// Desktop fitting extends the original maximum height for large boards and landscape viewports.
constexpr float kMaximumDistance = 160.0f;
// Menu thumbnails use a fixed resolution independent of window density.
constexpr int kPreviewWidth = 512, kPreviewHeight = 288;
// Keep browsing a large catalog bounded without rebuilding the current selection.
constexpr std::size_t kPreviewCacheLimit = 16;
// Vertex inputs retain original geometry, normals, and UV coordinates.
struct Vertex { float x, y, z, nx, ny, nz, u, v; };

// Original point-sprite flush truncates normalized RGBA into unsigned byte channels.
Color pointSpriteColor(Color color) {
    const auto channel = [](float value) { return std::trunc(std::clamp(value,0.0f,1.0f) * 255) / 255; };
    return {channel(color.r),channel(color.g),channel(color.b),channel(color.a)};
}

// Transpose resource matrices from their original row-major JSON encoding.
Mat4 resourceMatrix(const Json& array) {
    Mat4 matrix = Mat4::identity();
    if (!array.is_array() || array.size() != 16) return matrix;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            matrix.v[column*4+row] = array[row*4+column].get<float>();
    return matrix;
}

// Retain material RGBA while allowing descriptors without an explicit tint.
Color resourceColor(const Json& material) {
    if (!material.contains("color")) return {};
    const auto& value = material.at("color");
    return {value.at(0).get<float>(),value.at(1).get<float>(),
            value.at(2).get<float>(),value.at(3).get<float>()};
}

struct Pixels {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
    bool translucent = false;
};

// Decode original PNG assets with the Windows image codec already on the host.
Pixels decodePng(const std::filesystem::path& path) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&factory));
    if (FAILED(result)) throw std::runtime_error("Windows PNG decoder is unavailable");
    ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                               WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(result)) throw std::runtime_error("Cannot decode PNG: " + path.string());
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                     WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        throw std::runtime_error("Cannot convert PNG: " + path.string());
    UINT width = 0, height = 0;
    converter->GetSize(&width, &height);
    Pixels pixels;
    pixels.width = static_cast<int>(width); pixels.height = static_cast<int>(height);
    pixels.rgba.resize(static_cast<size_t>(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.rgba.size()),
                                    pixels.rgba.data())))
        throw std::runtime_error("Cannot read PNG pixels: " + path.string());
    for (size_t index = 3; index < pixels.rgba.size(); index += 4)
        if (pixels.rgba[index] > 0 && pixels.rgba[index] < 255) pixels.translucent = true;
    return pixels;
}

// Decode UTF-8 HUD labels before passing them to the Windows Unicode font API.
std::wstring wide(const std::string& text) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

// Rasterize a desktop label into alpha pixels without an additional font library.
Pixels rasterText(const std::string& text, int fontHeight) {
    const std::wstring label = wide(text);
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ previousFont = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
    Pixels pixels;
    pixels.width = std::max(1L, size.cx + 4); pixels.height = std::max(1L, size.cy + 4);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = pixels.width; info.bmiHeader.biHeight = -pixels.height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* raw = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
    HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    std::memset(raw, 0, static_cast<size_t>(pixels.width) * pixels.height * 4);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 2, 2, label.c_str(), static_cast<int>(label.size()));
    GdiFlush();
    const auto* source = static_cast<unsigned char*>(raw);
    pixels.rgba.resize(static_cast<size_t>(pixels.width) * pixels.height * 4);
    for (size_t index = 0; index < pixels.rgba.size(); index += 4) {
        pixels.rgba[index] = pixels.rgba[index + 1] = pixels.rgba[index + 2] = 255;
        pixels.rgba[index + 3] = std::max({source[index], source[index + 1], source[index + 2]});
    }
    pixels.translucent = true;
    SelectObject(dc, previousBitmap); SelectObject(dc, previousFont);
    DeleteObject(bitmap); DeleteObject(font); DeleteDC(dc);
    return pixels;
}

// Read generated resource JSON and preserve a filename in parse diagnostics.
Json readJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open asset: " + path.string());
    try { return Json::parse(input); }
    catch (const std::exception& e) { throw std::runtime_error(path.string() + ": " + e.what()); }
}

// Build a clip-space projection for HUD coordinates expressed in logical pixels.
Mat4 hudMatrix(float width, float height) {
    Mat4 result = Mat4::identity();
    result.v[0] = 2 / width; result.v[5] = -2 / height;
    result.v[12] = -1; result.v[13] = 1;
    return result;
}
} // namespace

struct Renderer::Impl {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    std::filesystem::path root;
    std::string error, info;
    // The original maps select their background mesh, with the ordinary sky as default.
    std::string skybox = "skybox";
    bool ownsCom = false;
    int width = 1, height = 1, pixelWidth = 1, pixelHeight = 1;
    // Logical board bounds exclude desktop controls while leaving HUD at full size.
    float viewportX = 0, viewportY = 0, viewportWidth = 0, viewportHeight = 0;
    float mapWidth = 22, mapHeight = 32;
    Vec3 target{11, 16, 0};
    float distance = 48;
    GLuint program = 0, scratchVao = 0, scratchVbo = 0, whiteTexture = 0;
    GLint mvpUniform = -1, modelUniform = -1, tintUniform = -1, unlitUniform = -1;
    GLint uvTransformUniform = -1;
    std::uint32_t visualFrame = 0;

    // Every modern entry point is loaded after the owning context becomes current.
#define GL_ENTRY(type, name) type name = nullptr
    GL_ENTRY(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays);
    GL_ENTRY(PFNGLBINDVERTEXARRAYPROC, BindVertexArray);
    GL_ENTRY(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays);
    GL_ENTRY(PFNGLGENBUFFERSPROC, GenBuffers);
    GL_ENTRY(PFNGLBINDBUFFERPROC, BindBuffer);
    GL_ENTRY(PFNGLBUFFERDATAPROC, BufferData);
    GL_ENTRY(PFNGLDELETEBUFFERSPROC, DeleteBuffers);
    GL_ENTRY(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray);
    GL_ENTRY(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer);
    GL_ENTRY(PFNGLCREATESHADERPROC, CreateShader);
    GL_ENTRY(PFNGLSHADERSOURCEPROC, ShaderSource);
    GL_ENTRY(PFNGLCOMPILESHADERPROC, CompileShader);
    GL_ENTRY(PFNGLGETSHADERIVPROC, GetShaderiv);
    GL_ENTRY(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog);
    GL_ENTRY(PFNGLDELETESHADERPROC, DeleteShader);
    GL_ENTRY(PFNGLCREATEPROGRAMPROC, CreateProgram);
    GL_ENTRY(PFNGLATTACHSHADERPROC, AttachShader);
    GL_ENTRY(PFNGLLINKPROGRAMPROC, LinkProgram);
    GL_ENTRY(PFNGLGETPROGRAMIVPROC, GetProgramiv);
    GL_ENTRY(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog);
    GL_ENTRY(PFNGLDELETEPROGRAMPROC, DeleteProgram);
    GL_ENTRY(PFNGLUSEPROGRAMPROC, UseProgram);
    GL_ENTRY(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation);
    GL_ENTRY(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv);
    GL_ENTRY(PFNGLUNIFORM4FPROC, Uniform4f);
    GL_ENTRY(PFNGLUNIFORM1IPROC, Uniform1i);
    GL_ENTRY(PFNGLACTIVETEXTUREPROC, ActiveTexture);
    GL_ENTRY(PFNGLGENERATEMIPMAPPROC, GenerateMipmap);
    GL_ENTRY(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers);
    GL_ENTRY(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer);
    GL_ENTRY(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers);
    GL_ENTRY(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D);
    GL_ENTRY(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus);
#undef GL_ENTRY

    struct Texture { GLuint id = 0; int width = 0, height = 0; bool translucent = false; };
    Texture whiteImage;
    struct Mesh {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        std::string texture;
        std::string blend;
        bool depthWrite = true;
    };
    struct ModelPart { std::string mesh; Mat4 transform = Mat4::identity(); Color tint{}; };
    struct Model { std::vector<ModelPart> parts; };
    std::map<std::string, Texture> textures;
    std::map<std::string, Texture> labels;
    std::map<std::string, Texture> previews;
    std::map<std::string, Mesh> meshes;
    std::map<std::string, Model> models;
    std::map<std::string, Json> materials;
    std::set<std::string> resourceWarnings;
    std::vector<Renderable> terrain;

    // Native default looks from positive Y, keeping UFO pads above the sheep barn.
    Vec3 eye() const { return target + Vec3{0, kCameraGroundOffset, distance}; }
    // Native zoom widens the vertical FOV below fifty; desktop extensions retain 45 degrees.
    float fov() const { return kCameraFov + std::max(0.0f,kNativeCameraHeight - distance) * (25.0f * 0.017453292f / 49.2f); }
    // Match rendering and picking to the same perspective camera.
    Mat4 viewProjection() const {
        return perspective(fov(), boardWidth() / boardHeight(), 1.0f,std::max(100.0f,2 * distance)) *
               lookAt(eye(), target, {0, 0, 1});
    }
    // A zero-sized viewport preserves the existing full-window developer scenes.
    float boardWidth() const { return viewportWidth > 0 ? viewportWidth : static_cast<float>(width); }
    // Keep the same full-window default for vertical projection.
    float boardHeight() const { return viewportHeight > 0 ? viewportHeight : static_cast<float>(height); }
    // Add actionable warnings without obscuring a previous asset failure.
    void warning(const std::string& message) {
        if (!resourceWarnings.insert(message).second) return;
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%s", message.c_str());
        if (error.empty()) error = message;
    }
    // Load the OpenGL 3.3 functions used by this single-context renderer.
    void loadGl() {
#define LOAD(name) name = reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress("gl" #name)); \
        if (!name) throw std::runtime_error("Missing OpenGL function gl" #name)
        LOAD(GenVertexArrays); LOAD(BindVertexArray); LOAD(DeleteVertexArrays);
        LOAD(GenBuffers); LOAD(BindBuffer); LOAD(BufferData); LOAD(DeleteBuffers);
        LOAD(EnableVertexAttribArray); LOAD(VertexAttribPointer);
        LOAD(CreateShader); LOAD(ShaderSource); LOAD(CompileShader); LOAD(GetShaderiv);
        LOAD(GetShaderInfoLog); LOAD(DeleteShader); LOAD(CreateProgram); LOAD(AttachShader);
        LOAD(LinkProgram); LOAD(GetProgramiv); LOAD(GetProgramInfoLog); LOAD(DeleteProgram);
        LOAD(UseProgram); LOAD(GetUniformLocation); LOAD(UniformMatrix4fv); LOAD(Uniform4f);
        LOAD(Uniform1i); LOAD(ActiveTexture); LOAD(GenerateMipmap);
        LOAD(GenFramebuffers); LOAD(BindFramebuffer); LOAD(DeleteFramebuffers);
        LOAD(FramebufferTexture2D); LOAD(CheckFramebufferStatus);
#undef LOAD
    }
    // Compile one core shader and surface the driver's complete diagnostic.
    GLuint shader(GLenum type, const char* source) {
        const GLuint object = CreateShader(type);
        ShaderSource(object, 1, &source, nullptr); CompileShader(object);
        GLint success = 0; GetShaderiv(object, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[4096]{}; GetShaderInfoLog(object, sizeof(log), nullptr, log);
            DeleteShader(object); throw std::runtime_error(std::string("OpenGL shader: ") + log);
        }
        return object;
    }
    // Link one texture-and-lighting program shared by map geometry and HUD.
    void createProgram() {
        const char* vertexSource = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
uniform mat4 mvp;
uniform mat4 model;
uniform vec4 uvTransform;
out vec2 texcoord;
out vec3 surfaceNormal;
void main() { gl_Position=mvp*vec4(position,1); texcoord=uv*uvTransform.xy+uvTransform.zw; surfaceNormal=mat3(model)*normal; }
)";
        const char* fragmentSource = R"(#version 330 core
in vec2 texcoord;
in vec3 surfaceNormal;
uniform sampler2D image;
uniform vec4 tint;
uniform bool unlit;
out vec4 color;
void main() {
    color=texture(image,texcoord)*tint;
    if(color.a<0.015) discard;
    float light=unlit?1.0:0.65+0.35*max(dot(normalize(surfaceNormal),normalize(vec3(-0.3,-0.5,1))),0.0);
    color.rgb*=light;
}
)";
        const GLuint vertex = shader(GL_VERTEX_SHADER, vertexSource);
        const GLuint fragment = shader(GL_FRAGMENT_SHADER, fragmentSource);
        program = CreateProgram(); AttachShader(program, vertex); AttachShader(program, fragment);
        LinkProgram(program); DeleteShader(vertex); DeleteShader(fragment);
        GLint success = 0; GetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char log[4096]{}; GetProgramInfoLog(program, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("OpenGL program: ") + log);
        }
        UseProgram(program);
        mvpUniform = GetUniformLocation(program, "mvp"); modelUniform = GetUniformLocation(program, "model");
        tintUniform = GetUniformLocation(program, "tint"); unlitUniform = GetUniformLocation(program, "unlit");
        uvTransformUniform = GetUniformLocation(program, "uvTransform");
        Uniform1i(GetUniformLocation(program, "image"), 0);
    }
    // Describe one interleaved original-mesh vertex to the core profile.
    void vertexLayout() {
        EnableVertexAttribArray(0); EnableVertexAttribArray(1); EnableVertexAttribArray(2);
        VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
        VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, nx)));
        VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    }
    // Upload RGBA pixels with original UV origin retained for model textures.
    Texture uploadTexture(const Pixels& pixels, bool mipmaps) {
        Texture texture; texture.width = pixels.width; texture.height = pixels.height;
        texture.translucent = pixels.translucent;
        glGenTextures(1, &texture.id); glBindTexture(GL_TEXTURE_2D, texture.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels.width, pixels.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels.rgba.data());
        if (mipmaps) GenerateMipmap(GL_TEXTURE_2D);
        return texture;
    }
    // Cache original descriptor references separately so aliases retain their samplers.
    Texture& texture(const std::string& name) {
        // PGL_setMaterial disables texturing for a material without a tex property.
        if (name.empty()) return whiteImage;
        if (name.starts_with("$preview:")) return previews.at(name);
        auto found = textures.find(name);
        if (found != textures.end()) return found->second;
        Json descriptor = Json::object();
        std::string file = name;
        if (name.ends_with(".texture.json")) {
            descriptor = readJson(root / name);
            file = "textures/" + descriptor.at("file").get<std::string>() + ".pvr.png";
        } else if (name.starts_with("textures/") && name.ends_with(".pvr.png")) {
            const std::string stem = name.substr(9,name.size() - 9 - std::string(".pvr.png").size());
            const auto path = root / "data" / (stem + ".texture.json");
            if (std::filesystem::exists(path)) descriptor = readJson(path);
        }
        const std::string min = descriptor.value("min",std::string("nearest"));
        Texture image = uploadTexture(decodePng(root / file),!descriptor.empty() && min.find("mipmap") != std::string::npos);
        if (!descriptor.empty()) {
            // PGL_loadTexture defaults to NEAREST and REPEAT, then applies these fields.
            GLint filter = GL_NEAREST;
            if (min == "linear") filter = GL_LINEAR;
            else if (min == "linear_mipmap_nearest") filter = GL_LINEAR_MIPMAP_NEAREST;
            else if (min == "linear_mipmap_linear") filter = GL_LINEAR_MIPMAP_LINEAR;
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,descriptor.value("mag",std::string("nearest")) == "linear" ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,descriptor.value("wraps",std::string("repeat")) == "clamp" ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,descriptor.value("wrapt",std::string("repeat")) == "clamp" ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        }
        return textures.emplace(name,std::move(image)).first->second;
    }
    // Preserve the descriptor name; its file alias and sampler are resolved together.
    std::string textureFile(const Json& material) {
        if (!material.contains("tex")) return "";
        const std::string name = material.at("tex").get<std::string>();
        return "data/" + name + ".texture.json";
    }
    // Upload immutable triangle vertices while retaining source material names.
    Mesh uploadMesh(const std::vector<Vertex>& vertices, std::string textureName) {
        Mesh mesh; mesh.texture = std::move(textureName); mesh.count = static_cast<GLsizei>(vertices.size());
        GenVertexArrays(1, &mesh.vao); GenBuffers(1, &mesh.vbo);
        BindVertexArray(mesh.vao); BindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        BufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
        vertexLayout(); return mesh;
    }
    // Set per-draw camera, object tint, and lighting without fixed-function calls.
    void uniforms(const Mat4& projection, const Mat4& model, Color color, bool unlit, Vec2 uvScale = {1,1}, Vec2 uvOffset = {}) {
        const Mat4 mvp = projection * model;
        UniformMatrix4fv(mvpUniform, 1, GL_FALSE, mvp.v.data());
        UniformMatrix4fv(modelUniform, 1, GL_FALSE, model.v.data());
        Uniform4f(tintUniform, color.r, color.g, color.b, color.a); Uniform1i(unlitUniform, unlit);
        Uniform4f(uvTransformUniform,uvScale.x,uvScale.y,uvOffset.x,uvOffset.y);
    }
    // Stream small diagnostic and HUD primitives through one reusable buffer.
    void primitive(const std::vector<Vertex>& vertices, GLenum mode, const Mat4& projection,
                   Color color, GLuint image = 0) {
        uniforms(projection, Mat4::identity(), color, true);
        glBindTexture(GL_TEXTURE_2D, image ? image : whiteTexture);
        BindVertexArray(scratchVao); BindBuffer(GL_ARRAY_BUFFER, scratchVbo);
        BufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(mode, 0, static_cast<GLsizei>(vertices.size()));
    }
    // Draw a textured HUD rectangle in logical desktop coordinates.
    void quad(float x, float y, float w, float h, Color color, GLuint image = 0,
              float u1 = 1, float v1 = 1) {
        primitive({{x,y,0,0,0,1,0,0}, {x+w,y,0,0,0,1,u1,0}, {x+w,y+h,0,0,0,1,u1,v1},
                   {x,y,0,0,0,1,0,0}, {x+w,y+h,0,0,0,1,u1,v1}, {x,y+h,0,0,0,1,0,v1}},
                  GL_TRIANGLES, hudMatrix(static_cast<float>(width), static_cast<float>(height)), color, image);
    }
    // Draw an unlit world quad using original bitmap alpha and an optional atlas row.
    void worldQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, const std::string& image, Color tint,
                   bool additive, int frame = 0, int frames = 1) {
        const float v0 = static_cast<float>(frame % std::max(1,frames)) / std::max(1,frames);
        const float v1 = v0 + 1.0f / std::max(1,frames);
        glBlendFunc(GL_SRC_ALPHA,additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        try {
            primitive({{a.x,a.y,a.z,0,0,1,0,v0},{b.x,b.y,b.z,0,0,1,1,v0},{c.x,c.y,c.z,0,0,1,1,v1},
                       {a.x,a.y,a.z,0,0,1,0,v0},{c.x,c.y,c.z,0,0,1,1,v1},{d.x,d.y,d.z,0,0,1,0,v1}},
                      GL_TRIANGLES,viewProjection(),tint,texture("textures/" + image).id);
        } catch (const std::exception& error) { warning(error.what()); }
    }
    // Cache DPI-aware labels and crop overflow without reducing readable font size.
    void text(const HudText& label, float maximumWidth = 0, float maximumHeight = 0) {
        if (label.text.empty()) return;
        const float dpi = static_cast<float>(pixelHeight) / height;
        const int fontHeight = std::max(1, static_cast<int>(std::max(12,label.fontSize) * dpi));
        const std::string key = std::to_string(fontHeight) + ':' + label.text;
        auto found = labels.find(key);
        if (found == labels.end()) {
            if (labels.size() > 256) {
                for (auto& [name, image] : labels) glDeleteTextures(1, &image.id);
                labels.clear();
            }
            found = labels.emplace(key, uploadTexture(rasterText(label.text, fontHeight), false)).first;
        }
        const Texture& image = found->second;
        const float imageWidth = image.width / dpi, imageHeight = image.height / dpi;
        const float drawWidth = maximumWidth > 0 ? std::min(imageWidth,maximumWidth) : imageWidth;
        const float drawHeight = maximumHeight > 0 ? std::min(imageHeight,maximumHeight) : imageHeight;
        quad(label.x,label.y,drawWidth,drawHeight,label.color,image.id,
             drawWidth/imageWidth,drawHeight/imageHeight);
    }
    // Load converted original map and mesh data; defined alongside export schema.
    void loadMap(const std::filesystem::path& mapFile);
    // Resolve an original model name to its mesh parts and local transforms.
    Model& model(const std::string& name);
    // Release all context-bound resources while the window still exists.
    ~Impl() {
        if (context) {
            SDL_GL_MakeCurrent(window, context);
            for (auto& [name, image] : textures) glDeleteTextures(1, &image.id);
            for (auto& [name, image] : labels) glDeleteTextures(1, &image.id);
            for (auto& [name, image] : previews) glDeleteTextures(1, &image.id);
            for (auto& [name, mesh] : meshes) {
                if (DeleteBuffers) DeleteBuffers(1, &mesh.vbo);
                if (DeleteVertexArrays) DeleteVertexArrays(1, &mesh.vao);
            }
            if (whiteTexture) glDeleteTextures(1, &whiteTexture);
            if (scratchVbo && DeleteBuffers) DeleteBuffers(1, &scratchVbo);
            if (scratchVao && DeleteVertexArrays) DeleteVertexArrays(1, &scratchVao);
            if (program && DeleteProgram) DeleteProgram(program);
            SDL_GL_DestroyContext(context);
        }
        if (ownsCom) CoUninitialize();
    }
};

// Load original model descriptors and expand the indexed converted mesh data.
Renderer::Impl::Model& Renderer::Impl::model(const std::string& name) {
    auto existing = models.find(name);
    if (existing != models.end()) return existing->second;
    // Native lightning and muzzle flashes load the original 2plane VBO directly.
    const Json descriptor = name.starts_with("$effect:")
        ? Json{{"geosets",{{"effect",{{"vbo","2plane"},{"material",name.substr(8)}}}}}}
        : readJson(root / "data" / (name+".model.json"));
    Model result;
    for (const auto& [partName, geo] : descriptor.at("geosets").items()) {
        const std::string vboName = geo.at("vbo").get<std::string>();
        const std::string materialName = geo.at("material").get<std::string>();
        const auto materialFound = materials.find(materialName);
        if (materialFound == materials.end())
            throw std::runtime_error("Missing original material: "+materialName+" in "+name);
        const Json& material = materialFound->second;
        const std::string meshKey = vboName+':'+materialName;
        if (!meshes.contains(meshKey)) {
            const auto path = root/"meshes"/(vboName+".mesh");
            std::ifstream input(path,std::ios::binary);
            char magic[4]{};
            std::uint32_t version = 0, vertexCount = 0, indexCount = 0;
            input.read(magic,4); input.read(reinterpret_cast<char*>(&version),4);
            input.read(reinterpret_cast<char*>(&vertexCount),4); input.read(reinterpret_cast<char*>(&indexCount),4);
            if (!input || std::memcmp(magic,"TMMS",4) != 0 || version != 1 || indexCount%3 != 0)
                throw std::runtime_error("Invalid converted mesh: "+path.string());
            struct OriginalVertex { float x,y,z,u,v; };
            std::vector<OriginalVertex> source(vertexCount);
            std::vector<std::uint16_t> indices(indexCount);
            input.read(reinterpret_cast<char*>(source.data()),source.size()*sizeof(OriginalVertex));
            input.read(reinterpret_cast<char*>(indices.data()),indices.size()*sizeof(std::uint16_t));
            if (!input) throw std::runtime_error("Truncated converted mesh: "+path.string());
            const auto offset = material.value("toffset",std::vector<float>{0,0});
            const auto scale = material.value("tscale",std::vector<float>{1,1});
            std::vector<Vertex> vertices;
            vertices.reserve(indexCount);
            for (size_t i = 0; i < indices.size(); i += 3) {
                if (indices[i] >= source.size() || indices[i+1] >= source.size() || indices[i+2] >= source.size())
                    throw std::runtime_error("Out-of-range mesh index: "+path.string());
                const auto& a = source[indices[i]];
                const auto& b = source[indices[i+1]];
                const auto& c = source[indices[i+2]];
                const Vec3 normal = normalized(cross(Vec3{b.x-a.x,b.y-a.y,b.z-a.z},Vec3{c.x-a.x,c.y-a.y,c.z-a.z}));
                for (size_t j = 0; j < 3; ++j) {
                    const auto& v = source[indices[i+j]];
                    vertices.push_back({v.x,v.y,v.z,normal.x,normal.y,normal.z,
                                        v.u*scale.at(0)+offset.at(0),v.v*scale.at(1)+offset.at(1)});
                }
            }
            Mesh mesh = uploadMesh(vertices,textureFile(material));
            mesh.blend = material.value("blend",std::string{});
            mesh.depthWrite = material.value("depthmask",1) != 0;
            meshes.emplace(meshKey,std::move(mesh));
        }
        result.parts.push_back({meshKey,resourceMatrix(geo.value("transformation",Json::array())),resourceColor(material)});
    }
    return models.emplace(name,std::move(result)).first->second;
}

// Place original map doodads and texture atlas terrain on the doubled navigation grid.
void Renderer::Impl::loadMap(const std::filesystem::path& mapFile) {
    // PGL_init parses this built-in JSON before loading the packaged material files.
    materials["default"] = {{"tex","checkerboard"},{"tscale",{8.0f,8.0f}}};
    for (const auto& entry : std::filesystem::directory_iterator(root/"data")) {
        const std::string filename = entry.path().filename().string();
        if (!filename.ends_with(".material.json")) continue;
        const Json materialFile = readJson(entry.path());
        for (const auto& [name, material] : materialFile.items()) materials[name] = material;
    }
    const Json map = readJson(mapFile);
    skybox = map.value("skybox",std::string("skybox"));
    const int columns = map.at("width").get<int>(), rows = map.at("height").get<int>();
    mapWidth = static_cast<float>(columns*2); mapHeight = static_cast<float>(rows*2);
    const Json atlas = readJson(root/"data"/(map.value("basetileAtlas",std::string("basetiles")) + ".atlas.json"));
    const auto& tileMaterial = materials.at(atlas.at("material").get<std::string>());
    std::map<std::string,Json> regions;
    for (const auto& region : atlas.at("subtextures")) regions[region.at("name").get<std::string>()] = region;
    std::vector<Vertex> ground;
    const float atlasWidth = atlas.at("width").get<float>(), atlasHeight = atlas.at("height").get<float>();
    for (int y = 0; y < rows; ++y) for (int x = 0; x < columns; ++x) {
        const int value = map.at("terrain").at(y*columns+x).get<int>();
        // MAP_alloc maps the original terrain codes to mud, grass, and forest.
        const std::string regionName = value == 1 ? "grass" : value == 2 ? "forest" : "mud";
        const Json& region = regions.at(regionName);
        const float u0 = region.at("position").at(0).get<float>()/atlasWidth;
        // Atlas positions use a bottom origin; original model UVs already target top-down PNG pixels.
        const float v0 = 1-(region.at("position").at(1).get<float>()+
                           region.at("size").at(1).get<float>())/atlasHeight;
        const float u1 = u0+region.at("size").at(0).get<float>()/atlasWidth;
        const float v1 = v0+region.at("size").at(1).get<float>()/atlasHeight;
        const float x0 = static_cast<float>(x*2), y0 = static_cast<float>(y*2);
        ground.insert(ground.end(),{{x0,y0,0,0,0,1,u0,v0},{x0+2,y0,0,0,0,1,u1,v0},
                                   {x0+2,y0+2,0,0,0,1,u1,v1},{x0,y0,0,0,0,1,u0,v0},
                                   {x0+2,y0+2,0,0,0,1,u1,v1},{x0,y0+2,0,0,0,1,u0,v1}});
    }
    meshes.emplace("$ground",uploadMesh(ground,textureFile(tileMaterial)));
    models["$ground"].parts.push_back({"$ground",Mat4::identity(),{}});
    terrain.push_back({"$ground"});
    const Json transitionAtlas = readJson(root/"data"/(map.value("transitionAtlas",std::string("transitions")) + ".atlas.json"));
    const auto& transitionMaterial = materials.at(transitionAtlas.at("material").get<std::string>());
    std::map<std::string,Json> transitionRegions;
    for (const auto& region : transitionAtlas.at("subtextures")) transitionRegions[region.at("name").get<std::string>()] = region;
    std::vector<Vertex> transitions;
    const float transitionWidth = transitionAtlas.at("width").get<float>();
    const float transitionHeight = transitionAtlas.at("height").get<float>();
    // Native overlays disable depth testing; a small lift preserves their order in the core renderer.
    constexpr float transitionLift = 0.002f;
    // MAP_prepareTransitions uses left, top, right, bottom bits in this order.
    constexpr std::array<Vec2,4> neighbors{{{-1,0},{0,1},{1,0},{0,-1}}};
    // Diagonal order follows the same clockwise traversal from the top-left corner.
    constexpr std::array<Vec2,4> diagonals{{{-1,1},{1,1},{1,-1},{-1,-1}}};
    // Select the native terrain code, treating off-map cells as absent.
    const auto terrainAt = [&](int x, int y) {
        return x < 0 || y < 0 || x >= columns || y >= rows ? -1 : map.at("terrain").at(y*columns+x).get<int>();
    };
    // Append one original atlas region, rotating only its UV corners.
    const auto overlay = [&](const std::string& name, float x0, float y0, float x1, float y1,
                             int rotation, float z) {
        const Json& region = transitionRegions.at(name);
        const float u0 = region.at("position").at(0).get<float>()/transitionWidth;
        const float u1 = u0+region.at("size").at(0).get<float>()/transitionWidth;
        const float v1 = 1-region.at("position").at(1).get<float>()/transitionHeight;
        const float v0 = v1-region.at("size").at(1).get<float>()/transitionHeight;
        const std::array<Vec2,4> uv{{{u0,v0},{u1,v0},{u1,v1},{u0,v1}}};
        const std::array<Vec2,4> positions{{{x0,y0},{x1,y0},{x1,y1},{x0,y1}}};
        // Native quads split across the bottom-left to top-right diagonal.
        constexpr std::array<int,6> indices{0,1,2,0,2,3};
        for (const int index : indices) {
            const Vec2 point = positions[index], coordinate = uv[(index+rotation)%4];
            transitions.push_back({point.x,point.y,z,0,0,1,coordinate.x,coordinate.y});
        }
    };
    for (int higher = 1; higher <= 2; ++higher) {
        const std::string prefix = higher == 1 ? "grass_transition_" : "forest_transition_";
        for (int x = 0; x < columns; ++x) for (int y = 0; y < rows; ++y) {
            if (terrainAt(x,y) >= higher) continue;
            int mask = 0;
            for (int side = 0; side < 4; ++side)
                if (terrainAt(x+static_cast<int>(neighbors[side].x),y+static_cast<int>(neighbors[side].y)) == higher)
                    mask |= 1 << side;
            const float x0 = static_cast<float>(x*2), y0 = static_cast<float>(y*2);
            const float z = transitionLift*static_cast<float>(higher);
            // Opposite edges use two left regions; adjacent edges share a native combined region.
            if (mask == 5 || mask == 10) {
                for (int side = 0; side < 4; ++side)
                    if ((mask & (1 << side)) != 0) overlay(prefix+"left",x0,y0,x0+2,y0+2,side,z);
            } else if (mask != 0) {
                // Canonical native masks are left=1, lefttop=3, lefttopright=7, surround=15.
                constexpr std::array<int,4> canonical{1,3,7,15};
                constexpr std::array<const char*,4> suffix{"left","lefttop","lefttopright","surround"};
                bool emitted = false;
                for (int shape = 0; shape < 4 && !emitted; ++shape) for (int rotation = 0; rotation < 4; ++rotation) {
                    const int rotated = ((canonical[shape] << rotation) | (canonical[shape] >> (4-rotation))) & 15;
                    if (rotated == mask) {
                        overlay(prefix+suffix[shape],x0,y0,x0+2,y0+2,rotation,z);
                        emitted = true;
                        break;
                    }
                }
            }
            for (int corner = 0; corner < 4; ++corner) {
                // A diagonal region is suppressed when either adjoining edge already covers it.
                const int adjacent = (1 << corner) | (1 << ((corner+1)%4));
                if ((mask & adjacent) != 0 ||
                    terrainAt(x+static_cast<int>(diagonals[corner].x),y+static_cast<int>(diagonals[corner].y)) != higher) continue;
                const Json& region = transitionRegions.at(prefix+"corner");
                const Json& base = regions.at("grass");
                const float cornerWidth = 2*region.at("size").at(0).get<float>()/base.at("size").at(0).get<float>();
                const float cornerHeight = 2*region.at("size").at(1).get<float>()/base.at("size").at(1).get<float>();
                const bool right = corner == 1 || corner == 2, top = corner < 2;
                const float left = right ? x0+2-cornerWidth : x0;
                const float bottom = top ? y0+2-cornerHeight : y0;
                overlay(prefix+"corner",left,bottom,left+cornerWidth,bottom+cornerHeight,corner,z);
            }
        }
    }
    if (!transitions.empty()) {
        Mesh transitionMesh = uploadMesh(transitions,textureFile(transitionMaterial));
        transitionMesh.blend = transitionMaterial.value("blend",std::string("alpha"));
        transitionMesh.depthWrite = false;
        meshes.emplace("$transitions",std::move(transitionMesh));
        models["$transitions"].parts.push_back({"$transitions",Mat4::identity(),resourceColor(transitionMaterial)});
        terrain.push_back({"$transitions"});
    }
    // MAP_alloc supplies these models; an explicit empty array disables procedural trees.
    const std::vector<std::string> treeModels = map.value("treeModels",std::vector<std::string>{"grove1","grove2","grove3","grove4","grove5"});
    // sync_initRand/sync_lrand48 implement MT19937 with seed zero for map decorations.
    std::mt19937 treeRandom(0);
    // Native MAP_prepareTrees displaces each axis by (sync_frand48-.5)*.47.
    constexpr float treeJitter = 0.47f;
    for (int x = 0; x < columns; ++x) for (int y = 0; y < rows; ++y) {
        if (terrainAt(x,y) != 2 || treeModels.empty()) continue;
        const std::string& name = treeModels[treeRandom()%treeModels.size()];
        const float jitterX = (static_cast<float>(treeRandom())*0x1p-32f-0.5f)*treeJitter;
        const float jitterY = (static_cast<float>(treeRandom())*0x1p-32f-0.5f)*treeJitter;
        terrain.push_back({name,{x*2.0f+1+jitterX,y*2.0f+1+jitterY,0}});
    }
    size_t ordinal = 0;
    for (const auto& doodad : map.at("doodads")) {
        Model placed = model(doodad.at("model").get<std::string>());
        const Mat4 transform = resourceMatrix(doodad.at("transform"));
        for (auto& part : placed.parts) part.transform = transform*part.transform;
        const std::string name = "$doodad"+std::to_string(ordinal++);
        models.emplace(name,std::move(placed)); terrain.push_back({name});
    }
}

// Construct graphics state without touching SDL or any audio devices.
Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
// Destroy context resources before the caller releases its SDL window.
Renderer::~Renderer() = default;

// Initialize a verified desktop core context and original scene resources.
bool Renderer::initialize(SDL_Window* window, const std::filesystem::path& root,
                          const std::filesystem::path& mapFile) {
    auto& r = *impl_;
    r.window = window; r.root = root;
    try {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        r.ownsCom = SUCCEEDED(com);
        r.context = SDL_GL_CreateContext(window);
        if (!r.context) throw std::runtime_error(SDL_GetError());
        r.loadGl();
        GLint major = 0, minor = 0, profile = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
        if (major < 3 || (major == 3 && minor < 3) || !(profile & GL_CONTEXT_CORE_PROFILE_BIT))
            throw std::runtime_error("OpenGL 3.3 core is required by this renderer");
        r.info = std::string(reinterpret_cast<const char*>(glGetString(GL_VERSION))) + " / " +
                 reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        SDL_Log("Graphics: %s", r.info.c_str());
        r.createProgram();
        r.GenVertexArrays(1, &r.scratchVao); r.GenBuffers(1, &r.scratchVbo);
        r.BindVertexArray(r.scratchVao); r.BindBuffer(GL_ARRAY_BUFFER, r.scratchVbo); r.vertexLayout();
        r.whiteTexture = r.uploadTexture({1,1,{255,255,255,255},false}, false).id;
        r.whiteImage = {r.whiteTexture,1,1,false};
        r.ActiveTexture(GL_TEXTURE0);
        glEnable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // Original exported meshes include two-sided sprites and unknown winding.
        glDisable(GL_CULL_FACE);
        SDL_GL_SetSwapInterval(1);
        r.loadMap(mapFile); resize(); resetCamera();
        return true;
    } catch (const std::exception& e) {
        r.error = e.what(); SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", r.error.c_str());
        return false;
    }
}

// Keep rendering in physical pixels and input/HUD in SDL logical pixels.
bool Renderer::changeMap(const std::filesystem::path& mapFile) {
    auto& r = *impl_;
    try {
        for (auto& [name, mesh] : r.meshes) {
            r.DeleteBuffers(1,&mesh.vbo); r.DeleteVertexArrays(1,&mesh.vao);
        }
        for (auto& [name, image] : r.textures) glDeleteTextures(1,&image.id);
        r.meshes.clear(); r.models.clear(); r.textures.clear(); r.terrain.clear(); r.materials.clear();
        r.error.clear(); r.resourceWarnings.clear(); r.loadMap(mapFile); resetCamera(); return true;
    } catch (const std::exception& error) {
        r.error = error.what(); return false;
    }
}

// Keep rendering in physical pixels and input/HUD in SDL logical pixels.
void Renderer::resize() {
    auto& r = *impl_;
    SDL_GetWindowSize(r.window, &r.width, &r.height);
    SDL_GetWindowSizeInPixels(r.window, &r.pixelWidth, &r.pixelHeight);
    r.width = std::max(1, r.width); r.height = std::max(1, r.height);
    r.pixelWidth = std::max(1, r.pixelWidth); r.pixelHeight = std::max(1, r.pixelHeight);
}

// Set one shared logical rectangle for camera rendering and mouse coordinates.
void Renderer::setBoardViewport(float x, float y, float width, float height) {
    auto& r = *impl_;
    r.viewportX = std::clamp(x,0.0f,static_cast<float>(r.width - 1));
    r.viewportY = std::clamp(y,0.0f,static_cast<float>(r.height - 1));
    r.viewportWidth = std::clamp(width,1.0f,r.width - r.viewportX);
    r.viewportHeight = std::clamp(height,1.0f,r.height - r.viewportY);
}

// Present the same scene path used to generate original-map menu thumbnails.
void Renderer::draw(const SceneView& scene) {
    drawScene(scene,true);
}

// Render opaque meshes first and sorted translucent parts without depth writes.
void Renderer::drawScene(const SceneView& scene, bool present) {
    auto& r = *impl_;
    if (!r.context || !r.program) return;
    if (present) resize();
    if (present) ++r.visualFrame;
    glViewport(0, 0, r.pixelWidth, r.pixelHeight);
    glClearColor(0.14f, 0.23f, 0.3f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const float dpiX = static_cast<float>(r.pixelWidth) / r.width,dpiY = static_cast<float>(r.pixelHeight) / r.height;
    glViewport(static_cast<int>(std::lround(r.viewportX * dpiX)),static_cast<int>(std::lround((r.height - r.viewportY - r.boardHeight()) * dpiY)),
               static_cast<int>(std::lround(r.boardWidth() * dpiX)),static_cast<int>(std::lround(r.boardHeight() * dpiY)));
    r.UseProgram(r.program); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    struct DrawPart { Impl::Mesh* mesh; Impl::Texture* texture; Mat4 transform; Color tint; float distance; bool unlit; Vec2 uvScale,uvOffset; bool overrideColor; };
    std::vector<DrawPart> opaque, transparent;
    // Keep individual broken resources visible as diagnostics while rendering the rest.
    const auto append = [&](const Renderable& object) {
        try {
            const auto& model = r.model(object.model);
            for (const auto& part : model.parts) {
                auto& mesh = r.meshes.at(part.mesh);
                auto& texture = r.texture(mesh.texture);
                const Color tint = object.overrideMaterialColor ? Color{std::clamp(object.tint.r,0.0f,1.0f),std::clamp(object.tint.g,0.0f,1.0f),
                    std::clamp(object.tint.b,0.0f,1.0f),std::clamp(object.tint.a,0.0f,1.0f)} : Color{part.tint.r*object.tint.r, part.tint.g*object.tint.g,
                                 part.tint.b*object.tint.b, part.tint.a*object.tint.a};
                const Mat4 transform = modelMatrix(object.position, object.rotation, object.scale, object.pitch, object.pitchPivot) * object.animationTransform * part.transform;
                Vec2 uvScale = object.textureScale, uvOffset = object.textureOffset;
                if (object.atlasFrames > 1) {
                    // Separate visual hashing never samples the combat random engine.
                    const std::uint32_t sample = (r.visualFrame + object.visualSeed) * 0x9e3779b9u;
                    const int first = static_cast<int>((sample >> 16) % object.atlasFrames);
                    const int frame = object.atlasLayer == 0 ? first : (first + 1 + sample % (object.atlasFrames - 1)) % object.atlasFrames;
                    uvScale.y = 1.0f / object.atlasFrames;
                    uvOffset.y = static_cast<float>(object.atlasFrames - 1 - frame) / object.atlasFrames;
                }
                DrawPart draw{&mesh, &texture, transform, tint, dot(object.position-r.eye(),object.position-r.eye()),object.unlit,uvScale,uvOffset,object.overrideMaterialColor};
                (!mesh.blend.empty() || !mesh.depthWrite || texture.translucent || tint.a < 1 ? transparent : opaque).push_back(draw);
            }
        } catch (const std::exception& e) { r.warning(e.what()); }
    };
    for (const auto& object : r.terrain) append(object);
    for (const auto& object : scene.models) append(object);
    if (scene.selectedCell) {
        // Original selection uses its textured rect, not a wireframe footprint.
        Renderable highlight{"selection",{std::floor(scene.selectedCell->x) + 1,std::floor(scene.selectedCell->y) + 1,0.1f},0,1,scene.cellColor};
        highlight.animationTransform = scaleMatrix({2,2,1});
        highlight.overrideMaterialColor = true;
        append(highlight);
    }
    const Mat4 camera = r.viewProjection();
    // Render the packaged background around the eye before terrain without depth writes.
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    try {
        for (const auto& part : r.model(r.skybox).parts) {
            const auto& mesh = r.meshes.at(part.mesh);
            const auto& texture = r.texture(mesh.texture);
            r.uniforms(camera,modelMatrix(r.eye(),0,1) * part.transform,part.tint,true);
            glBindTexture(GL_TEXTURE_2D,texture.id); r.BindVertexArray(mesh.vao);
            glDrawArrays(GL_TRIANGLES,0,mesh.count);
        }
    } catch (const std::exception& error) { r.warning(error.what()); }
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    const auto render = [&](const DrawPart& part) {
        // Original effect materials distinguish additive and alpha-weighted additive light.
        const bool alphaOverride = part.overrideColor && part.tint.a < 1;
        const GLenum sourceBlend = !alphaOverride && part.mesh->blend == "additive" ? GL_ONE : GL_SRC_ALPHA;
        const GLenum destinationBlend = !alphaOverride && part.mesh->blend.starts_with("additive") ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA;
        glBlendFunc(sourceBlend,destinationBlend);
        r.uniforms(camera, part.transform, part.tint, part.unlit,part.uvScale,part.uvOffset);
        glBindTexture(GL_TEXTURE_2D, part.texture->id); r.BindVertexArray(part.mesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, part.mesh->count);
    };
    for (const auto& part : opaque) render(part);
    std::sort(transparent.begin(), transparent.end(), [](const auto& a, const auto& b) {
        return a.distance > b.distance;
    });
    glDepthMask(GL_FALSE);
    // Ground shadows and shockwaves keep depth testing but do not occlude units.
    for (const auto& sprite : scene.sprites) if (sprite.ground) {
        const Vec3 horizontal{sprite.width / 2,0,0},vertical{0,sprite.height / 2,0};
        r.worldQuad(sprite.position - horizontal - vertical,sprite.position + horizontal - vertical,
                    sprite.position + horizontal + vertical,sprite.position - horizontal + vertical,
                    sprite.texture,sprite.color,sprite.additive);
    }
    for (const auto& part : transparent) render(part);
    const Vec3 forward = normalized(r.target - r.eye()),horizontal = normalized(cross(forward,{0,0,1})),vertical = cross(horizontal,forward);
    for (const auto& sprite : scene.sprites) if (!sprite.ground) {
        float width = sprite.width, height = sprite.height;
        if (sprite.pointSizeParameter > 0) {
            const Vec3 distanceOrigin = sprite.pointDistanceOrigin.value_or(sprite.position);
            const float cameraDistance = std::sqrt(dot(distanceOrigin - r.eye(),distanceOrigin - r.eye()));
            const auto clip = transform(camera,{sprite.position.x,sprite.position.y,sprite.position.z,1});
            if (clip[3] <= 0 || cameraDistance <= 0) continue;
            // Convert native pixel size to a billboard at this exact projected depth.
            constexpr float NativeScreenWidth = 320;
            const float pixels = sprite.pointSizeParameter / cameraDistance * (sprite.screenRelative ? r.boardWidth() / NativeScreenWidth : 1);
            width = height = pixels * clip[3] * (2 * std::tan(r.fov() / 2)) / r.boardHeight();
        }
        const Vec3 across = horizontal * (width / 2),up = vertical * (height / 2);
        const Color color = sprite.pointSizeParameter > 0 ? pointSpriteColor(sprite.color) : sprite.color;
        r.worldQuad(sprite.position - across + up,sprite.position + across + up,
                    sprite.position + across - up,sprite.position - across - up,
                    sprite.texture,color,sprite.additive);
    }
    for (const auto& ribbon : scene.ribbons) {
        const Vec3 side = normalized(cross(ribbon.to - ribbon.from,r.eye() - ribbon.from)) * (ribbon.width / 2);
        r.worldQuad(ribbon.from + side,ribbon.to + side,ribbon.to - side,ribbon.from - side,
                    ribbon.texture,ribbon.color,ribbon.additive,ribbon.frame,ribbon.frames);
    }
    for (const auto& quad : scene.quads) {
        glBlendFunc(GL_SRC_ALPHA,quad.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        std::vector<Vertex> vertices;
        // Native EffectTrail explicitly uses these two triangles without a texture.
        for (const int index : {0,1,2,0,2,3}) {
            const Vec3 point = quad.vertices[index];
            vertices.push_back({point.x,point.y,point.z,0,0,1,0,0});
        }
        r.primitive(vertices,GL_TRIANGLES,camera,quad.color);
    }
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for (const auto& line : scene.lines)
        r.primitive({{line.from.x,line.from.y,line.from.z,0,0,1,0,0},
                     {line.to.x,line.to.y,line.to.z,0,0,1,0,0}}, GL_LINES, camera, line.color);
    glDisable(GL_DEPTH_TEST);
    glViewport(0,0,r.pixelWidth,r.pixelHeight);
    for (const auto& panel : scene.panels)
        r.quad(panel.x,panel.y,panel.width,panel.height,panel.color);
    for (const auto& image : scene.images) {
        if (image.texture.empty()) continue;
        try {
            const auto& texture = r.texture(image.texture);
            // Framebuffer images have bottom-up UVs; PNG controls remain top-down.
            if (image.texture.starts_with("$preview:")) r.quad(image.x,image.y + image.height,image.width,-image.height,image.color,texture.id);
            else r.quad(image.x,image.y,image.width,image.height,image.color,texture.id);
        } catch (const std::exception& error) { r.warning(error.what()); }
    }
    float mouseX = 0, mouseY = 0;
    const SDL_MouseButtonFlags mouseButtons = SDL_GetMouseState(&mouseX,&mouseY);
    // Preserve an inset for icons and readable labels within each button.
    constexpr float buttonPadding = 7, iconGap = 6;
    // Match the default HUD type size rather than shrinking long button labels.
    constexpr int buttonFontSize = 18;
    for (const auto& button : scene.buttons) {
        const bool hovered = button.enabled && SDL_GetMouseFocus() == r.window &&
                             mouseX >= button.x && mouseX < button.x+button.width &&
                             mouseY >= button.y && mouseY < button.y+button.height;
        const bool pressed = hovered && (mouseButtons & SDL_BUTTON_LMASK) != 0;
        Color fill = !button.enabled ? Color{0.16f,0.17f,0.19f,0.95f} :
                     button.selected ? Color{0.24f,0.49f,0.24f,0.96f} : Color{0.09f,0.13f,0.18f,0.96f};
        if (hovered) {
            // A held click darkens the same control that brightens on hover.
            const float highlight = pressed ? 0.75f : 1.35f;
            fill.r *= highlight; fill.g *= highlight; fill.b *= highlight;
        }
        r.quad(button.x, button.y, button.width, button.height, fill);
        float textX = button.x+buttonPadding;
        const float contentHeight = button.height-2*buttonPadding;
        if (!button.icon.empty() && contentHeight > 0 && button.width > 2*buttonPadding) {
            try {
                const auto& icon = r.texture("textures/"+button.icon);
                // Limit icon width so every ordinary button retains label space.
                const float iconSpace = std::min(contentHeight,(button.width-2*buttonPadding)*0.28f);
                const float scale = iconSpace/std::max(icon.width,icon.height);
                const float iconWidth = icon.width*scale, iconHeight = icon.height*scale;
                r.quad(textX,button.y+(button.height-iconHeight)/2,iconWidth,iconHeight,
                       button.enabled ? Color{} : Color{0.7f,0.7f,0.7f,1},icon.id);
                textX += iconWidth+iconGap;
            } catch (const std::exception& error) { r.warning(error.what()); }
        }
        const float textWidth = button.x+button.width-buttonPadding-textX;
        const float textY = button.y+std::max(0.0f,(button.height-buttonFontSize-4)/2);
        if (textWidth > 0 && button.height > 0)
            r.text({button.text,textX,textY,
                    button.enabled ? Color{} : Color{0.72f,0.74f,0.77f,1},buttonFontSize},
                   textWidth,button.y+button.height-textY);
    }
    for (const auto& label : scene.texts) r.text(label);
    glDepthMask(GL_TRUE);
    if (present) SDL_GL_SwapWindow(r.window);
}

// Borrow map resources for one offscreen render, then restore the exact live board.
std::string Renderer::mapPreview(const std::string& mapName) {
    auto& r = *impl_;
    const std::string key = "$preview:" + mapName;
    if (r.previews.contains(key)) return key;
    if (!r.context) return {};
    auto meshes = std::move(r.meshes);
    auto models = std::move(r.models);
    auto materials = std::move(r.materials);
    auto terrain = std::move(r.terrain);
    const std::string skybox = r.skybox;
    const Vec3 target = r.target;
    const float distance = r.distance, mapWidth = r.mapWidth, mapHeight = r.mapHeight;
    const std::array<float,4> board{r.viewportX,r.viewportY,r.viewportWidth,r.viewportHeight};
    const std::array<int,4> sizes{r.width,r.height,r.pixelWidth,r.pixelHeight};
    GLint framebuffer = 0, viewport[4]{}, drawBuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer); glGetIntegerv(GL_VIEWPORT,viewport); glGetIntegerv(GL_DRAW_BUFFER,&drawBuffer);
    GLuint fbo = 0, color = 0, depth = 0;
    bool generated = false;
    try {
        r.loadMap(r.root / "data" / (mapName + ".map.json"));
        r.width = r.pixelWidth = kPreviewWidth; r.height = r.pixelHeight = kPreviewHeight;
        r.viewportX = r.viewportY = 0; r.viewportWidth = kPreviewWidth; r.viewportHeight = kPreviewHeight;
        resetCamera();
        glGenTextures(1,&color); glBindTexture(GL_TEXTURE_2D,color);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,kPreviewWidth,kPreviewHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenTextures(1,&depth); glBindTexture(GL_TEXTURE_2D,depth);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,kPreviewWidth,kPreviewHeight,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,nullptr);
        r.GenFramebuffers(1,&fbo); r.BindFramebuffer(GL_FRAMEBUFFER,fbo);
        r.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        r.FramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth,0);
        if (r.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Map preview framebuffer is incomplete.");
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        drawScene({},false);
        generated = true;
    } catch (const std::exception& error) { r.warning(error.what()); }
    // Preview failure releases only borrowed resources; the playable map is retained.
    for (auto& [name, mesh] : r.meshes) { r.DeleteBuffers(1,&mesh.vbo); r.DeleteVertexArrays(1,&mesh.vao); }
    r.meshes = std::move(meshes); r.models = std::move(models); r.materials = std::move(materials); r.terrain = std::move(terrain);
    r.skybox = skybox; r.target = target; r.distance = distance; r.mapWidth = mapWidth; r.mapHeight = mapHeight;
    r.viewportX = board[0]; r.viewportY = board[1]; r.viewportWidth = board[2]; r.viewportHeight = board[3];
    r.width = sizes[0]; r.height = sizes[1]; r.pixelWidth = sizes[2]; r.pixelHeight = sizes[3];
    r.BindFramebuffer(GL_FRAMEBUFFER,framebuffer); glDrawBuffer(drawBuffer); glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    if (fbo) r.DeleteFramebuffers(1,&fbo);
    if (depth) glDeleteTextures(1,&depth);
    if (!generated) { if (color) glDeleteTextures(1,&color); return {}; }
    if (r.previews.size() >= kPreviewCacheLimit) {
        glDeleteTextures(1,&r.previews.begin()->second.id); r.previews.erase(r.previews.begin());
    }
    r.previews.emplace(key,Impl::Texture{color,kPreviewWidth,kPreviewHeight,false});
    return key;
}

// Intersect a camera ray with Z=0 using the same basis as lookAt.
std::optional<Vec2> Renderer::pick(float x, float y) const {
    const auto& r = *impl_;
    x -= r.viewportX; y -= r.viewportY;
    if (x < 0 || y < 0 || x >= r.boardWidth() || y >= r.boardHeight()) return std::nullopt;
    const Vec3 eye = r.eye(), forward = normalized(r.target-eye);
    const Vec3 right = normalized(cross(forward,{0,0,1}));
    const Vec3 vertical = cross(right,forward);
    const float tangent = std::tan(r.fov()/2);
    const Vec3 ray = forward + right*((2*x/r.boardWidth()-1)*tangent*r.boardWidth()/r.boardHeight()) +
                     vertical*((1-2*y/r.boardHeight())*tangent);
    if (std::abs(ray.z) < 0.0001f) return std::nullopt;
    const float t = -eye.z/ray.z;
    if (t < 0) return std::nullopt;
    const Vec3 point = eye+ray*t;
    if (point.x < 0 || point.y < 0 || point.x >= r.mapWidth || point.y >= r.mapHeight) return std::nullopt;
    return Vec2{point.x,point.y};
}

// Return projected logical coordinates for camera and resize verification.
std::optional<Vec2> Renderer::project(Vec3 world) const {
    const auto& r = *impl_;
    const auto clip = transform(r.viewProjection(),{world.x,world.y,world.z,1});
    if (clip[3] <= 0) return std::nullopt;
    return Vec2{r.viewportX + (clip[0]/clip[3]+1)*0.5f*r.boardWidth(),r.viewportY + (1-clip[1]/clip[3])*0.5f*r.boardHeight()};
}

// Pan within a generous map margin without restricting ordinary exploration.
bool Renderer::verifyResources(const std::filesystem::path& output) {
    auto& r = *impl_;
    Json report;
    report["textures"] = Json::array(); report["models"] = Json::array();
    int failures = 0;
    for (const auto& file : std::filesystem::directory_iterator(r.root / "textures")) {
        if (file.path().extension() != ".png") continue;
        try {
            const Pixels pixels = decodePng(file.path());
            report["textures"].push_back({{"name",file.path().filename().string()},{"width",pixels.width},{"height",pixels.height},{"loaded",true}});
        } catch (const std::exception& error) {
            ++failures; report["textures"].push_back({{"name",file.path().filename().string()},{"loaded",false},{"error",error.what()}});
        }
    }
    for (const auto& file : std::filesystem::directory_iterator(r.root / "data")) {
        const std::string name = file.path().filename().string();
        if (!name.ends_with(".model.json")) continue;
        try {
            const auto& model = r.model(name.substr(0,name.size() - std::string(".model.json").size()));
            for (const auto& part : model.parts) r.texture(r.meshes.at(part.mesh).texture);
            report["models"].push_back({{"name",name},{"loaded",true}});
        } catch (const std::exception& error) {
            ++failures; report["models"].push_back({{"name",name},{"loaded",false},{"error",error.what()}});
        }
    }
    report["failures"] = failures; report["allLoaded"] = failures == 0;
    std::ofstream(output / "resource-load-checks.json") << report.dump(2);
    return failures == 0;
}

// Pan within a generous map margin without restricting ordinary exploration.
void Renderer::pan(float x, float y) {
    auto& r = *impl_;
    // Native negative X is screen right; keep desktop drags and D/A aligned with that view.
    r.target.x = std::clamp(r.target.x-x,-r.mapWidth,r.mapWidth*2);
    r.target.y = std::clamp(r.target.y+y,-r.mapHeight,r.mapHeight*2);
}
// Apply smooth multiplicative wheel zoom around the current camera target.
void Renderer::zoom(float steps) {
    impl_->distance = std::clamp(impl_->distance*std::exp(-steps*0.12f),kMinimumDistance,kMaximumDistance);
}
// Fit the map and treetops inside the actual board rectangle with a small margin.
void Renderer::resetCamera(bool fit) {
    auto& r = *impl_;
    r.target = {r.mapWidth/2,r.mapHeight/2,0};
    r.distance = kNativeCameraHeight;
    if (!fit) return;
    // Test the real perspective rather than assuming a portrait or landscape aspect.
    constexpr float FrameLimit = 0.92f, TreeHeight = 3.0f;
    constexpr int FitIterations = 18;
    float low = kMinimumDistance,high = kMaximumDistance;
    for (int step = 0; step < FitIterations; ++step) {
        r.distance = (low + high) * 0.5f;
        const Mat4 camera = r.viewProjection();
        bool fits = true;
        for (const float x : {0.0f,r.mapWidth}) for (const float y : {0.0f,r.mapHeight}) for (const float z : {0.0f,TreeHeight}) {
            const auto clip = transform(camera,{x,y,z,1});
            if (clip[3] <= 0 || std::abs(clip[0]/clip[3]) > FrameLimit || std::abs(clip[1]/clip[3]) > FrameLimit) fits = false;
        }
        if (fits) high = r.distance; else low = r.distance;
    }
    r.distance = high;
}

// Native updateCamera advances remaining look/height deltas by twice the realtime frame interval.
void Renderer::focusAbduction(Vec2 position, float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    auto& r = *impl_;
    // makeSureCameraIsGood preserves target ground Z and the +5 lateral eye offset even after input locks.
    constexpr float FinalHeight = 10, DeltaRate = 2;
    const float factor = DeltaRate * seconds;
    r.target.x += (position.x - r.target.x) * factor;
    r.target.y += (position.y - r.target.y) * factor;
    r.target.z = 0;
    r.distance = std::clamp(r.distance + (FinalHeight - r.distance) * factor,kMinimumDistance,kMaximumDistance);
}
// Expose asset or initialization diagnostics to the desktop caller.
const std::string& Renderer::error() const { return impl_->error; }
// Expose the real driver string for the validation report.
const std::string& Renderer::graphicsInfo() const { return impl_->info; }

// Capture the real front buffer and encode a top-down PNG with Windows WIC.
bool Renderer::capture(const std::filesystem::path& path) {
    auto& r = *impl_;
    if (!r.context) return false;
    try {
        std::vector<unsigned char> bottomUp(static_cast<size_t>(r.pixelWidth)*r.pixelHeight*4);
        std::vector<unsigned char> topDown(bottomUp.size());
        glReadBuffer(GL_FRONT); glPixelStorei(GL_PACK_ALIGNMENT,1);
        glReadPixels(0,0,r.pixelWidth,r.pixelHeight,GL_RGBA,GL_UNSIGNED_BYTE,bottomUp.data());
        const size_t stride = static_cast<size_t>(r.pixelWidth)*4;
        for (int row = 0; row < r.pixelHeight; ++row) {
            std::memcpy(topDown.data()+row*stride,bottomUp.data()+(r.pixelHeight-1-row)*stride,stride);
            // WIC's PNG encoder accepts BGRA; GL supplies RGBA from the real framebuffer.
            for (size_t column = 0; column < stride; column += 4)
                std::swap(topDown[row*stride+column],topDown[row*stride+column+2]);
        }
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
        if (!factory || FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)) ||
            FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)) ||
            FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(&frame,nullptr)) || FAILED(frame->Initialize(nullptr)))
            throw std::runtime_error("Cannot open screenshot PNG: "+path.string());
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetSize(r.pixelWidth,r.pixelHeight)) || FAILED(frame->SetPixelFormat(&format)) ||
            format != GUID_WICPixelFormat32bppBGRA ||
            FAILED(frame->WritePixels(r.pixelHeight,static_cast<UINT>(stride),static_cast<UINT>(topDown.size()),topDown.data())) ||
            FAILED(frame->Commit()) || FAILED(encoder->Commit()))
            throw std::runtime_error("Cannot encode screenshot PNG: "+path.string());
        glReadBuffer(GL_BACK);
        return true;
    } catch (const std::exception& e) {
        r.warning(e.what()); glReadBuffer(GL_BACK); return false;
    }
}
} // namespace tmreborn
