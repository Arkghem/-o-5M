// ecs_tests -- O5MWorld acceptance: entity identity, slot recycling, deferred
// destruction, component lifecycle and the centered deltaTime policy.
// Exit code 0 = all checks passed; run: ./build/ecs_tests or ctest -R ecs

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

#include "Scene/O5MComponent.h"
#include "Scene/O5MEntity.h"
#include "Scene/O5MEntityHandle.h"
#include "Scene/O5MWorld.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool cond, const char* msg, int line) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cerr << "[FAIL] line " << line << ": " << msg << "\n";
    }
}
#define CHECK(cond, msg) check((cond), (msg), __LINE__)

// Counters live outside the component: after its entity dies, reading the
// component's own counter would be a use-after-free in the test itself.
struct Recorder {
    int inits = 0;
    int updates = 0;
    int renders = 0;
    int destroys = 0;
    float lastDeltaTime = 0.0f;
};

class ProbeComponent : public O5MComponent {
public:
    explicit ProbeComponent(Recorder* recorder) : rec(recorder) {}

    Recorder* rec = nullptr;

    // Optional payload: kill `victim` from inside update(), i.e. destroy an
    // entity while the entity set is being iterated.
    O5MWorld* world = nullptr;
    O5MEntityHandle victim;

    void onInitializing(void) override { ++rec->inits; }
    void onDestroying(void) override { ++rec->destroys; }

    void update(float deltaTime) override {
        ++rec->updates;
        rec->lastDeltaTime = deltaTime;
        if (world != nullptr && victim.isValid()) {
            world->destroyEntity(victim);
            victim = O5MEntityHandle{};
        }
    }

    void render(void) override { ++rec->renders; }
};

// Destroys another entity (or its own) from inside onDestroying(): hooks are
// user code that re-enters the world while it is tearing an entity down.
// Templated on a tag because an entity holds one component per type.
template <int Tag>
class ReentrantComponent : public O5MComponent {
public:
    ReentrantComponent(O5MWorld* world, O5MEntityHandle victim)
        : world(world), victim(victim) {}

    O5MWorld* world = nullptr;
    O5MEntityHandle victim;

    void onDestroying(void) override {
        if (victim.isValid()) {
            const O5MEntityHandle target = victim;
            victim = O5MEntityHandle{};   // once
            world->destroyEntity(target);
        }
    }
};

bool nearly(float a, float b) { return std::fabs(a - b) < 1e-5f; }

void testIdentityAndStaleHandles(void) {
    O5MWorld world;

    const O5MEntityHandle a = world.createEntity("a");
    const O5MEntityHandle b = world.createEntity("b");

    CHECK(a.isValid() && b.isValid(), "createEntity returns valid handles");
    CHECK(a != b, "each entity gets a distinct handle");
    CHECK(world.getEntityCount() == 2, "count tracks creation");
    CHECK(world.getSlotCount() == 2, "slots grow with creation");
    CHECK(world.getEntity(a) != nullptr && world.getEntity(b) != nullptr,
          "handles resolve to live entities");
    CHECK(world.getEntity(a)->getName() == "a", "name round-trips");
    CHECK(world.getEntity(a)->getHandle() == a, "entity reports the handle it was issued");
    CHECK(world.isAlive(a) && world.isAlive(b), "isAlive agrees with getEntity");

    world.destroyEntity(a);
    CHECK(!world.isAlive(a), "destroyed entity is not alive");
    CHECK(world.getEntity(a) == nullptr, "stale handle resolves to nullptr");
    CHECK(world.getEntityCount() == 1, "destroy updates the count");
    CHECK(world.getSlotCount() == 2, "destroyed slot is kept for recycling");

    const O5MEntityHandle c = world.createEntity("c");
    CHECK(c.getIndex() == a.getIndex(), "the freed slot is recycled");
    CHECK(c.getGeneration() != a.getGeneration(), "recycling bumps the generation");
    CHECK(world.getEntity(a) == nullptr, "the old handle stays dead after recycling");
    CHECK(world.getEntity(c) != nullptr, "the new handle resolves");
    CHECK(world.getEntity(c)->getName() == "c", "recycled slot holds the new entity");
    CHECK(world.getEntityCount() == 2, "count after recycle");

    // None of these may corrupt the world.
    world.destroyEntity(c);
    world.destroyEntity(c);
    world.destroyEntity(O5MEntityHandle{});
    world.destroyEntity(O5MEntityHandle(9999, 1));
    CHECK(world.getEntityCount() == 1, "repeat / invalid destroys are no-ops");
    CHECK(world.getEntity(O5MEntityHandle{}) == nullptr, "an invalid handle never resolves");
}

void testComponentLifecycle(void) {
    O5MWorld world;
    world.init();

    Recorder rec;
    const O5MEntityHandle handle = world.createEntity("probe");
    auto* probe = world.getEntity(handle)->addComponent<ProbeComponent>(&rec);
    probe->render();

    CHECK(rec.inits == 1, "a component added after world init is initialized immediately");
    CHECK(rec.renders == 1, "render reaches the component");

    world.update(0.5f);
    CHECK(rec.updates == 1, "update reaches the component");
    CHECK(nearly(rec.lastDeltaTime, 0.1f), "hitch clamp caps dt at maxDeltaTime (0.1s)");

    world.setMaxDeltaTime(1.0f);
    world.setTimeScale(0.5f);
    world.update(0.4f);
    CHECK(nearly(rec.lastDeltaTime, 0.2f), "time scale is applied after the clamp");

    world.setPaused(true);
    const int updatesBeforePause = rec.updates;
    world.update(0.4f);
    CHECK(rec.updates == updatesBeforePause + 1, "a paused world still ticks entities");
    CHECK(rec.lastDeltaTime == 0.0f, "a paused world hands out dt == 0");
    world.setPaused(false);

    world.update(-5.0f);
    CHECK(rec.lastDeltaTime == 0.0f, "negative dt is discarded");
    world.update(std::numeric_limits<float>::quiet_NaN());
    CHECK(rec.lastDeltaTime == 0.0f, "NaN dt is discarded");

    CHECK(nearly(static_cast<float>(world.getElapsedTime()), 0.3f),
          "elapsed time only accumulates the clamped/scaled dt");
    CHECK(world.getFrameIndex() == 5, "frame index counts every update call");
    CHECK(nearly(world.getDeltaTime(), 0.0f), "getDeltaTime reports the last handed-out dt");

    auto* again = world.getEntity(handle)->addComponent<ProbeComponent>(&rec);
    CHECK(again == probe, "adding the same component type twice returns the existing one");
    CHECK(rec.inits == 1, "the duplicate add does not re-initialize");
    CHECK(world.getEntity(handle)->getComponent<ProbeComponent>() == probe, "getComponent finds it");

    CHECK(world.getEntity(handle)->removeComponent<ProbeComponent>(), "removeComponent succeeds");
    CHECK(rec.destroys == 1, "removeComponent runs onDestroying");
    CHECK(!world.getEntity(handle)->removeComponent<ProbeComponent>(), "removing twice returns false");
    CHECK(world.getEntity(handle)->getComponent<ProbeComponent>() == nullptr,
          "getComponent returns nullptr when the type is absent");

    world.destroyEntity(handle);
    CHECK(world.getEntityCount() == 0, "entity destroyed");
}

void testDeferredDestructionDuringUpdate(void) {
    O5MWorld world;
    world.init();

    Recorder victimRec;
    Recorder killerRec;

    const O5MEntityHandle victim = world.createEntity("victim");
    world.getEntity(victim)->addComponent<ProbeComponent>(&victimRec);

    const O5MEntityHandle killer = world.createEntity("killer");
    auto* killerProbe = world.getEntity(killer)->addComponent<ProbeComponent>(&killerRec);
    killerProbe->world = &world;
    killerProbe->victim = victim;

    world.update(0.016f);

    CHECK(!world.isAlive(victim), "a destroy requested during update is applied");
    CHECK(victimRec.destroys == 1, "the deferred kill still runs onDestroying");
    CHECK(killerRec.updates == 1, "the requesting entity survives its own update");
    CHECK(world.isAlive(killer), "the killer is untouched");
    CHECK(world.getEntityCount() == 1, "count reflects the deferred kill");
}

void testIteration(void) {
    O5MWorld world;
    world.init();

    Recorder rec0;
    Recorder rec1;
    const O5MEntityHandle h0 = world.createEntity("h0");
    world.getEntity(h0)->addComponent<ProbeComponent>(&rec0);
    const O5MEntityHandle h1 = world.createEntity("h1");
    world.getEntity(h1)->addComponent<ProbeComponent>(&rec1);

    int visited = 0;
    O5MEntityHandle createdInside{};

    world.forEachEntity([&](O5MEntity& entity) {
        ++visited;
        if (entity.getHandle() == h0) {
            createdInside = world.createEntity("late");   // snapshot: not visited now
            world.destroyEntity(h1);                      // deferred: loop stays valid
        }
        CHECK(world.isAlive(h1), "destroy stays deferred while iterating");
    });

    CHECK(visited == 2, "forEachEntity visits the snapshot only");
    CHECK(!world.isAlive(h1), "the deferred destroy lands after the pass");
    CHECK(world.getEntity(createdInside) != nullptr, "an entity created inside the pass exists after it");
    CHECK(world.getEntityCount() == 2, "count after one deferred destroy and one creation");
}

void testDestroyReentrancy(void) {
    O5MWorld world;
    world.init();

    Recorder selfRec;
    Recorder otherRec;

    const O5MEntityHandle other = world.createEntity("other");
    world.getEntity(other)->addComponent<ProbeComponent>(&otherRec);

    const O5MEntityHandle victim = world.createEntity("victim");
    world.getEntity(victim)->addComponent<ProbeComponent>(&selfRec);
    // Components tear down in reverse creation order, so these two hooks run
    // while destroySlot(victim) is still on the stack.
    world.getEntity(victim)->addComponent<ReentrantComponent<0>>(&world, victim);
    world.getEntity(victim)->addComponent<ReentrantComponent<1>>(&world, other);

    world.destroyEntity(victim);

    CHECK(!world.isAlive(victim), "re-entrant self destroy leaves the entity dead");
    CHECK(!world.isAlive(other), "a hook can destroy another entity");
    CHECK(selfRec.destroys == 1, "each component is torn down exactly once");
    CHECK(otherRec.destroys == 1, "the other entity was torn down too");
    CHECK(world.getEntityCount() == 0, "count is consistent after re-entrant destroys");
    CHECK(world.getSlotCount() == 2, "both slots are still available for recycling");
}

void testEntityActiveGate(void) {
    O5MWorld world;
    world.init();

    Recorder rec;
    const O5MEntityHandle handle = world.createEntity("gated");
    world.getEntity(handle)->addComponent<ProbeComponent>(&rec);

    world.getEntity(handle)->setActive(false);
    world.update(0.016f);
    world.render();
    CHECK(rec.updates == 0 && rec.renders == 0, "an inactive entity skips update and render");

    world.getEntity(handle)->setActive(true);
    world.update(0.016f);
    CHECK(rec.updates == 1, "reactivating resumes updates");
}

void testWorldIndependenceAndTeardown(void) {
    O5MWorld world1;
    O5MWorld world2;
    world1.init();
    world2.init();

    const O5MEntityHandle h1 = world1.createEntity("w1");
    const O5MEntityHandle h2 = world2.createEntity("w2");

    world1.destroyEntity(h1);
    CHECK(world2.isAlive(h2), "destroying in one world does not touch another");
    CHECK(world1.getEntityCount() == 0 && world2.getEntityCount() == 1, "counts are per world");

    // Handles carry no world id: they are only meaningful in the world that
    // issued them (documented precondition).

    Recorder rec;
    {
        O5MWorld dying;
        dying.init();
        const O5MEntityHandle handle = dying.createEntity("dying");
        dying.getEntity(handle)->addComponent<ProbeComponent>(&rec);
        CHECK(rec.inits == 1, "component initialized in the temporary world");
    }
    CHECK(rec.destroys == 1, "~O5MWorld tears live components down");
}

} // namespace

int main() {
    testIdentityAndStaleHandles();
    testComponentLifecycle();
    testDeferredDestructionDuringUpdate();
    testIteration();
    testDestroyReentrancy();
    testEntityActiveGate();
    testWorldIndependenceAndTeardown();

    if (g_failures == 0) {
        std::cout << "[ok] ecs_tests: " << g_checks << " checks passed\n";
        return 0;
    }
    std::cerr << "[FAIL] ecs_tests: " << g_failures << " of " << g_checks << " checks failed\n";
    return 1;
}
