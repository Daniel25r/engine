#include "Test.h"
#include "titan/core/Scene.h"

using namespace titan;

TEST("eventos: crear y destruir entidades emite eventos") {
    EventBus bus;
    Scene s("S", &bus);
    int created = 0, destroyed = 0;
    std::string lastDestroyed;
    auto subA = bus.subscribe<EntityCreatedEvent>([&](const EntityCreatedEvent&) { ++created; });
    auto subB = bus.subscribe<EntityDestroyedEvent>([&](const EntityDestroyedEvent& e) {
        ++destroyed;
        lastDestroyed = e.scene->info(e.entity).name; // los datos aún existen
    });
    Entity p = s.createEntity("Padre");
    s.createEntity("Hijo", p);
    s.destroyEntityImmediate(p);
    CHECK_EQ(created, 2);
    CHECK_EQ(destroyed, 2);
    CHECK_EQ(lastDestroyed, std::string("Padre")); // los hijos se avisan antes que el padre
}

TEST("eventos: la suscripción se da de baja al destruirse") {
    EventBus bus;
    int calls = 0;
    {
        auto sub = bus.subscribe<AppPausedEvent>([&](const AppPausedEvent&) { ++calls; });
        bus.emit(AppPausedEvent{});
    }
    bus.emit(AppPausedEvent{});
    CHECK_EQ(calls, 1);
}

TEST("eventos: darse de baja durante la emisión es seguro") {
    EventBus bus;
    int a = 0, b = 0;
    Subscription subB;
    auto subA = bus.subscribe<AppResumedEvent>([&](const AppResumedEvent&) {
        ++a;
        subB.reset();
    });
    subB = bus.subscribe<AppResumedEvent>([&](const AppResumedEvent&) { ++b; });
    bus.emit(AppResumedEvent{});
    bus.emit(AppResumedEvent{});
    CHECK_EQ(a, 2);
    CHECK_EQ(b, 0);
}

TEST("eventos: una suscripción que sobrevive al bus no falla") {
    Subscription sub;
    {
        EventBus bus;
        sub = bus.subscribe<AppPausedEvent>([](const AppPausedEvent&) {});
    }
    sub.reset();
    CHECK(true);
}
