#pragma once
// Matemáticas del motor. Convenciones (ver docs/ARCHITECTURE.md):
// - Sistema diestro, Y hacia arriba, "delante" = -Z (como las cámaras de OpenGL y glTF).
// - Matrices 4x4 en orden de columnas: m[columna * 4 + fila].
// - Ángulos en radianes salvo que el nombre diga "Degrees".
#include <cmath>
#include <cstdint>

namespace titan {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

struct Vec2 {
    float x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(Vec2 o) const { return x == o.x && y == o.y; }
    float length() const { return std::sqrt(x * x + y * y); }
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const {
        const float l = length();
        return l > 1e-8f ? *this / l : Vec3{};
    }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    static Quat fromAxisAngle(const Vec3& axis, float radians) {
        const Vec3 a = axis.normalized();
        const float s = std::sin(radians * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
    }
    // Euler en radianes: primero X, luego Y, luego Z (ejes fijos del mundo).
    static Quat fromEuler(const Vec3& e) {
        const float cx = std::cos(e.x * 0.5f), sx = std::sin(e.x * 0.5f);
        const float cy = std::cos(e.y * 0.5f), sy = std::sin(e.y * 0.5f);
        const float cz = std::cos(e.z * 0.5f), sz = std::sin(e.z * 0.5f);
        return {sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz,
                cx * cy * cz + sx * sy * sz};
    }
    Vec3 toEuler() const {
        const float sinp = 2.0f * (w * y - z * x);
        return {std::atan2(2.0f * (w * x + y * z), 1.0f - 2.0f * (x * x + y * y)),
                std::fabs(sinp) >= 1.0f ? std::copysign(kPi / 2.0f, sinp) : std::asin(sinp),
                std::atan2(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z))};
    }
    Quat operator*(const Quat& q) const {
        return {w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w, w * q.w - x * q.x - y * q.y - z * q.z};
    }
    Quat conjugate() const { return {-x, -y, -z, w}; }
    Quat normalized() const {
        const float l = std::sqrt(x * x + y * y + z * z + w * w);
        return l > 1e-8f ? Quat{x / l, y / l, z / l, w / l} : Quat{};
    }
    Vec3 rotate(const Vec3& v) const {
        const Vec3 u{x, y, z};
        const Vec3 t = cross(u, v) * 2.0f;
        return v + t * w + cross(u, t);
    }
    bool operator==(const Quat& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
};

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rw = 0; rw < 4; ++rw) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += at(rw, k) * b.at(k, c);
                r.at(rw, c) = s;
            }
        return r;
    }
    Vec3 transformPoint(const Vec3& p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    Vec3 transformDirection(const Vec3& d) const {
        return {m[0] * d.x + m[4] * d.y + m[8] * d.z, m[1] * d.x + m[5] * d.y + m[9] * d.z,
                m[2] * d.x + m[6] * d.y + m[10] * d.z};
    }
    Vec3 translation() const { return {m[12], m[13], m[14]}; }

    static Mat4 translate(const Vec3& t) {
        Mat4 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    static Mat4 scale(const Vec3& s) {
        Mat4 r;
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }
    static Mat4 rotate(const Quat& q) {
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        Mat4 r;
        r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy + wz);     r.m[2] = 2 * (xz - wy);
        r.m[4] = 2 * (xy - wz);     r.m[5] = 1 - 2 * (xx + zz); r.m[6] = 2 * (yz + wx);
        r.m[8] = 2 * (xz + wy);     r.m[9] = 2 * (yz - wx);     r.m[10] = 1 - 2 * (xx + yy);
        return r;
    }
    // Traslación * Rotación * Escala, sin multiplicaciones completas.
    static Mat4 trs(const Vec3& t, const Quat& q, const Vec3& s) {
        Mat4 r = rotate(q);
        for (int i = 0; i < 3; ++i) { r.m[i] *= s.x; r.m[4 + i] *= s.y; r.m[8 + i] *= s.z; }
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    static Mat4 ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
        Mat4 r;
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = -2.0f / (zFar - zNear);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(zFar + zNear) / (zFar - zNear);
        return r;
    }
    static Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
        const float f = 1.0f / std::tan(fovY * 0.5f);
        Mat4 r;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = 2.0f * zFar * zNear / (zNear - zFar);
        r.m[15] = 0.0f;
        return r;
    }

    // Inversa general (cofactores). Devuelve false si la matriz es singular.
    bool inverse(Mat4& out) const;
    Mat4 inverted() const {
        Mat4 r;
        inverse(r);
        return r;
    }
    // Separa traslación, rotación y escala. Exacto para matrices TRS sin cizalla.
    void decompose(Vec3& t, Quat& q, Vec3& s) const;
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    static constexpr Color hex(uint32_t rgb, float alpha = 1.0f) {
        return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha};
    }
    constexpr bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    constexpr bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

} // namespace titan
