#include "Test.h"
#include "titan/core/Math.h"

using namespace titan;

TEST("math: Euler <-> cuaternión ida y vuelta") {
    const Vec3 e{0.3f, -0.7f, 1.2f};
    const Vec3 back = Quat::fromEuler(e).toEuler();
    CHECK_VEC3(back, e.x, e.y, e.z);
}

TEST("math: rotar 90 grados en Y lleva -Z a -X") {
    const Quat q = Quat::fromAxisAngle({0, 1, 0}, kPi / 2);
    CHECK_VEC3(q.rotate({0, 0, -1}), -1, 0, 0);
    CHECK_VEC3(Mat4::rotate(q).transformDirection({0, 0, -1}), -1, 0, 0);
}

TEST("math: TRS por su inversa es la identidad") {
    const Mat4 m = Mat4::trs({1, 2, 3}, Quat::fromEuler({0.4f, 0.1f, -0.9f}), {2, 0.5f, 3});
    const Mat4 id = m * m.inverted();
    for (int i = 0; i < 16; ++i) CHECK_NEAR(id.m[i], (i % 5 == 0) ? 1.0 : 0.0, 1e-4);
}

TEST("math: descomponer recupera T, R y S") {
    const Vec3 t{4, -1, 2}, s{1.5f, 2, 0.5f};
    const Quat q = Quat::fromEuler({0.2f, 1.1f, -0.4f});
    Vec3 t2, s2;
    Quat q2;
    Mat4::trs(t, q, s).decompose(t2, q2, s2);
    CHECK_VEC3(t2, t.x, t.y, t.z);
    CHECK_VEC3(s2, s.x, s.y, s.z);
    // q y -q son la misma rotación.
    const float sign = (q.w * q2.w + q.x * q2.x + q.y * q2.y + q.z * q2.z) < 0 ? -1.0f : 1.0f;
    CHECK_NEAR(q2.x * sign, q.x, 1e-4);
    CHECK_NEAR(q2.w * sign, q.w, 1e-4);
}

TEST("math: proyección ortográfica lleva los bordes a -1..1") {
    const Mat4 p = Mat4::ortho(-4, 4, -2, 2, 0.1f, 100);
    CHECK_VEC3(p.transformPoint({4, 2, -1}), 1, 1, p.transformPoint({0, 0, -1}).z);
    CHECK_VEC3(p.transformPoint({-4, -2, -1}), -1, -1, p.transformPoint({0, 0, -1}).z);
}
