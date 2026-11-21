#pragma once
#include <cmath>
#include <cstdint>

namespace forja {

struct Vec2 {
    float x = 0.0f, y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }

    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSq() const { return x * x + y * y; }
    Vec2 normalized() const {
        float l = length();
        return l > 1e-6f ? Vec2{x / l, y / l} : Vec2{};
    }
};

inline constexpr Vec2 operator*(float s, Vec2 v) { return {v.x * s, v.y * s}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

// Color lineal RGBA en el rango 0..1.
struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    static constexpr Color hex(uint32_t rgb, float alpha = 1.0f) {
        return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha};
    }
};

// Matriz 4x4 en orden de columnas (el que espera OpenGL).
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 ortho(float left, float right, float bottom, float top) {
        Mat4 r;
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = -1.0f;
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        return r;
    }
};

constexpr float kPi = 3.14159265358979f;

} // namespace forja
