// Small scene math operations and deterministic visual samples independent of combat.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace tmreborn {

struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };

// Use a separate avalanche hash without consuming the battle random engine.
inline float visualRandom(std::uint32_t seed) {
    seed ^= seed >> 16; seed *= 0x7feb352du;
    seed ^= seed >> 15; seed *= 0x846ca68bu;
    seed ^= seed >> 16;
    return static_cast<float>(seed) * 0x1p-32f;
}

// Keep an event's visual choices stable throughout its lifetime.
inline std::uint32_t visualSeed(int bornAt, Vec3 origin) {
    return static_cast<std::uint32_t>(bornAt) ^ std::bit_cast<std::uint32_t>(origin.x)
        ^ (std::bit_cast<std::uint32_t>(origin.y) * 31u) ^ std::bit_cast<std::uint32_t>(origin.z);
}

// Add two world-space vectors.
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
// Subtract two world-space vectors.
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
// Scale a world-space vector.
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
// Compute the vector dot product.
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// Compute the right-handed vector cross product.
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
// Normalize a nonzero vector while keeping a zero vector finite.
inline Vec3 normalized(Vec3 a) {
    const float length = std::sqrt(dot(a, a));
    return length > 0 ? a * (1 / length) : Vec3{};
}

struct Mat4 {
    std::array<float, 16> v{};
    // Construct an identity transform.
    static Mat4 identity() {
        Mat4 m;
        m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1;
        return m;
    }
};

// Compose column-major transforms with the rightmost operation applied first.
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result.v[column * 4 + row] += a.v[k * 4 + row] * b.v[column * 4 + k];
    return result;
}

// Compose world translation, yaw, uniform scale, and local pivot pitch.
inline Mat4 modelMatrix(Vec3 position, float rotation, float scale, float pitch = 0, Vec3 pivot = {}) {
    Mat4 m = Mat4::identity();
    const float cosine = std::cos(rotation) * scale;
    const float sine = std::sin(rotation) * scale;
    m.v[0] = cosine; m.v[1] = sine;
    const float pitchCosine = std::cos(pitch), pitchSine = std::sin(pitch);
    // Pitch is a conventional right-handed rotation around local X.
    m.v[4] = -sine * pitchCosine; m.v[5] = cosine * pitchCosine; m.v[6] = scale * pitchSine;
    m.v[8] = sine * pitchSine; m.v[9] = -cosine * pitchSine; m.v[10] = scale * pitchCosine;
    // Native build/sale scale includes the articulated turret's local pivot displacement.
    const float pivotY = scale * (pivot.y * (1 - pitchCosine) + pivot.z * pitchSine);
    const float pivotZ = scale * (pivot.z * (1 - pitchCosine) - pivot.y * pitchSine);
    m.v[12] = position.x - std::sin(rotation) * pivotY;
    m.v[13] = position.y + std::cos(rotation) * pivotY;
    m.v[14] = position.z + pivotZ;
    return m;
}

// Build the local Y rotation used by the recovered enemy death transforms.
inline Mat4 rotationY(float radians) {
    Mat4 matrix = Mat4::identity();
    const float cosine = std::cos(radians), sine = std::sin(radians);
    matrix.v[0] = cosine; matrix.v[2] = -sine;
    matrix.v[8] = sine; matrix.v[10] = cosine;
    return matrix;
}

// Preserve the independent radial and longitudinal scales of original effect meshes.
inline Mat4 scaleMatrix(Vec3 scale) {
    Mat4 matrix = Mat4::identity();
    matrix.v[0] = scale.x; matrix.v[5] = scale.y; matrix.v[10] = scale.z;
    return matrix;
}

// Build a perspective projection using radians and a positive near distance.
inline Mat4 perspective(float fov, float aspect, float nearPlane, float farPlane) {
    Mat4 m;
    const float focal = 1 / std::tan(fov / 2);
    m.v[0] = focal / aspect; m.v[5] = focal;
    m.v[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    m.v[11] = -1;
    m.v[14] = 2 * farPlane * nearPlane / (nearPlane - farPlane);
    return m;
}

// Build a right-handed camera view with world Z as up.
inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 forward = normalized(target - eye);
    const Vec3 right = normalized(cross(forward, up));
    const Vec3 vertical = cross(right, forward);
    Mat4 m = Mat4::identity();
    m.v[0] = right.x; m.v[4] = right.y; m.v[8] = right.z;
    m.v[1] = vertical.x; m.v[5] = vertical.y; m.v[9] = vertical.z;
    m.v[2] = -forward.x; m.v[6] = -forward.y; m.v[10] = -forward.z;
    m.v[12] = -dot(right, eye); m.v[13] = -dot(vertical, eye); m.v[14] = dot(forward, eye);
    return m;
}

// Transform a homogeneous point without hiding the perspective divide.
inline std::array<float, 4> transform(const Mat4& m, std::array<float, 4> p) {
    std::array<float, 4> result{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            result[row] += m.v[column * 4 + row] * p[column];
    return result;
}

} // namespace tmreborn
