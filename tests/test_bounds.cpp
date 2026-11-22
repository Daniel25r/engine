#include "Test.h"
#include "titan/core/Bounds.h"

using namespace titan;

TEST("culling: caja transformada con giro, escala y traslación") {
    const AABB unit{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    const AABB moved = transformAABB(unit, Mat4::translate({10, 0, 0}) * Mat4::scale({2, 1, 1}));
    CHECK_VEC3(moved.min, 9, -0.5, -0.5);
    CHECK_VEC3(moved.max, 11, 0.5, 0.5);
    const AABB turned = transformAABB(unit, Mat4::rotate(Quat::fromEuler({0, 45 * kDegToRad, 0})));
    CHECK_NEAR(turned.max.x, 0.70710678, 1e-4); // la diagonal de la cara
    CHECK_NEAR(turned.max.y, 0.5, 1e-5);
    CHECK_NEAR(turned.max.z, 0.70710678, 1e-4);
    AABB grown = AABB{{0, 0, 0}, {2, 2, 2}}.inflated(0.5f);
    CHECK_VEC3(grown.min, -1, -1, -1);
    CHECK_VEC3(grown.max, 3, 3, 3);
    AABB e;
    CHECK(!e.valid());
    e.expand({1, 2, 3});
    e.expand({-1, 0, 5});
    CHECK(e.valid());
    CHECK_VEC3(e.min, -1, 0, 3);
    CHECK_VEC3(e.max, 1, 2, 5);
}

TEST("culling: frustum de perspectiva (delante, detrás, a los lados, lejos)") {
    // Cámara en el origen mirando a -Z, 60 grados, de 0,1 a 100.
    const Mat4 vp = Mat4::perspective(60 * kDegToRad, 16.0f / 9.0f, 0.1f, 100.0f);
    const Frustum f = Frustum::fromMatrix(vp);
    auto box = [](Vec3 c, float h) { return AABB{c - Vec3{h, h, h}, c + Vec3{h, h, h}}; };
    CHECK(f.intersects(box({0, 0, -10}, 1)));       // delante
    CHECK(!f.intersects(box({0, 0, 10}, 1)));       // detrás
    CHECK(!f.intersects(box({-50, 0, -10}, 1)));    // muy a la izquierda
    CHECK(!f.intersects(box({0, 30, -10}, 1)));     // muy arriba
    CHECK(!f.intersects(box({0, 0, -150}, 1)));     // más allá del plano lejano
    CHECK(f.intersects(box({0, 0, -100}, 2)));      // atraviesa el plano lejano
    CHECK(f.intersects(box({-11, 0, -10}, 2)));     // asoma por el borde izquierdo (en z = -10 el borde está en x ≈ -10,26)
    CHECK(!f.intersects(box({-12, 0, -10}, 0.5f))); // y una caja pequeña un poco más allá ya no
    CHECK(f.intersects(box({0, 0, 0}, 0.5f)));      // envuelve a la cámara
    CHECK(f.containsPoint({0, 0, -1}));
    CHECK(!f.containsPoint({0, 0, 1}));
    CHECK(!f.intersects(AABB{}));                   // caja vacía: nunca visible
}

TEST("culling: frustum de una cámara movida y de una ortográfica (2D y sombras)") {
    // Cámara en (100, 0, 0) mirando a +X (girada -90 grados en Y).
    const Mat4 camWorld = Mat4::trs({100, 0, 0}, Quat::fromEuler({0, -90 * kDegToRad, 0}), {1, 1, 1});
    const Frustum f = Frustum::fromMatrix(Mat4::perspective(60 * kDegToRad, 1, 0.1f, 50) * camWorld.inverted());
    CHECK(f.intersects(AABB{{109, -1, -1}, {111, 1, 1}}));
    CHECK(!f.intersects(AABB{{89, -1, -1}, {91, 1, 1}}));
    CHECK(!f.intersects(AABB{{-1, -1, -1}, {1, 1, 1}}));
    const Frustum o = Frustum::fromMatrix(Mat4::ortho(-10, 10, -5, 5, 0.1f, 100));
    CHECK(o.intersects(AABB{{9, 4, -2}, {11, 6, -1}}));   // esquina
    CHECK(!o.intersects(AABB{{11, 0, -2}, {12, 1, -1}}));
    CHECK(!o.intersects(AABB{{0, 0, 1}, {1, 1, 2}}));     // detrás de la cámara
    CHECK(o.intersects(AABB{{0, 0, -5}, {0, 0, -5}}));    // caja plana (sprite)
}
