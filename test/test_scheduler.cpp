/**
 * Tests for MCPScheduler — lightweight task scheduler
 *
 * Note: The mock millis() auto-increments on each call.
 * We use large jumps and >= comparisons to avoid timing sensitivity.
 */
#include "arduino_mock.h"
#include "test_framework.h"
#include "MCPScheduler.h"

// Helper: reset millis to a known base and return it
static unsigned long resetMillis(unsigned long base = 10000) {
    _mockMillis() = base;
    return base;
}

// ── Basic scheduling ───────────────────────────────────────────────

TEST(every_basic) {
    mcpd::Scheduler sched;
    int counter = 0;
    unsigned long base = resetMillis();

    sched.every(1000, [&]() { counter++; });

    // Not yet due
    sched.loop();
    ASSERT_EQ(counter, 0);

    // Advance well past 1000ms
    _mockMillis() = base + 2000;
    sched.loop();
    ASSERT_EQ(counter, 1);

    // Advance another 1000+
    _mockMillis() = base + 4000;
    sched.loop();
    ASSERT_EQ(counter, 2);
}

TEST(every_returns_index) {
    mcpd::Scheduler sched;
    resetMillis();

    int idx0 = sched.every(1000, []() {});
    int idx1 = sched.every(2000, []() {});

    ASSERT_EQ(idx0, 0);
    ASSERT_EQ(idx1, 1);
}

TEST(at_one_shot) {
    mcpd::Scheduler sched;
    int counter = 0;
    unsigned long base = resetMillis();

    sched.at(base + 500, [&]() { counter++; });
    ASSERT_EQ(sched.count(), (size_t)1);

    // Not yet
    sched.loop();
    ASSERT_EQ(counter, 0);

    // Past due
    _mockMillis() = base + 1000;
    sched.loop();
    ASSERT_EQ(counter, 1);

    // Should be removed — run loop again to GC, then check count
    _mockMillis() = base + 2000;
    sched.loop();
    ASSERT_EQ(counter, 1);
    ASSERT_EQ(sched.count(), (size_t)0);
}

TEST(times_limited_execution) {
    mcpd::Scheduler sched;
    int counter = 0;
    unsigned long base = resetMillis();

    sched.times(100, 3, [&]() { counter++; }, "limited");

    _mockMillis() = base + 200; sched.loop(); ASSERT_EQ(counter, 1);
    _mockMillis() = base + 400; sched.loop(); ASSERT_EQ(counter, 2);
    _mockMillis() = base + 600; sched.loop(); ASSERT_EQ(counter, 3);

    // Should stop after 3
    _mockMillis() = base + 800; sched.loop(); ASSERT_EQ(counter, 3);
    ASSERT_EQ(sched.count(), (size_t)0);
}

TEST(zero_interval_rejected) {
    mcpd::Scheduler sched;
    int idx = sched.every(0, []() {});
    ASSERT_EQ(idx, -1);
    ASSERT_EQ(sched.count(), (size_t)0);
}

TEST(max_tasks_limit) {
    mcpd::Scheduler sched(3);

    int idx0 = sched.every(1000, []() {});
    int idx1 = sched.every(1000, []() {});
    int idx2 = sched.every(1000, []() {});
    int idx3 = sched.every(1000, []() {});

    ASSERT_EQ(idx0, 0);
    ASSERT_EQ(idx1, 1);
    ASSERT_EQ(idx2, 2);
    ASSERT_EQ(idx3, -1);
    ASSERT_EQ(sched.count(), (size_t)3);
}

// ── Named task management ──────────────────────────────────────────

TEST(pause_resume) {
    mcpd::Scheduler sched;
    int counter = 0;
    unsigned long base = resetMillis();

    sched.every(100, [&]() { counter++; }, "sensor");

    _mockMillis() = base + 200; sched.loop();
    ASSERT_EQ(counter, 1);

    ASSERT_TRUE(sched.pause("sensor"));

    _mockMillis() = base + 500; sched.loop();
    ASSERT_EQ(counter, 1); // paused

    ASSERT_TRUE(sched.resume("sensor"));

    _mockMillis() = base + 800; sched.loop();
    ASSERT_EQ(counter, 2); // resumed
}

TEST(pause_nonexistent_returns_false) {
    mcpd::Scheduler sched;
    ASSERT_FALSE(sched.pause("nonexistent"));
    ASSERT_FALSE(sched.resume("nonexistent"));
}

TEST(remove_by_name) {
    mcpd::Scheduler sched;
    int counter = 0;
    unsigned long base = resetMillis();

    sched.every(100, [&]() { counter++; }, "temp");
    ASSERT_TRUE(sched.exists("temp"));

    ASSERT_TRUE(sched.remove("temp"));

    _mockMillis() = base + 500; sched.loop();
    ASSERT_EQ(counter, 0);
    ASSERT_FALSE(sched.exists("temp"));
}

TEST(remove_by_index) {
    mcpd::Scheduler sched;
    resetMillis();

    sched.every(100, []() {}, "a");
    sched.every(200, []() {}, "b");

    ASSERT_TRUE(sched.removeByIndex(0));
    ASSERT_FALSE(sched.exists("a"));
    ASSERT_TRUE(sched.exists("b"));
}

TEST(remove_invalid_index) {
    mcpd::Scheduler sched;
    ASSERT_FALSE(sched.removeByIndex(-1));
    ASSERT_FALSE(sched.removeByIndex(0));
    ASSERT_FALSE(sched.removeByIndex(999));
}

TEST(reschedule) {
    mcpd::Scheduler sched;
    int counter = 0;
    unsigned long base = resetMillis();

    sched.every(10000, [&]() { counter++; }, "poll");

    // Reschedule to faster interval
    ASSERT_TRUE(sched.reschedule("poll", 100));

    _mockMillis() = base + 500;
    sched.loop();
    ASSERT_EQ(counter, 1);
}

TEST(reschedule_zero_rejected) {
    mcpd::Scheduler sched;
    resetMillis();
    sched.every(100, []() {}, "task");
    ASSERT_FALSE(sched.reschedule("task", 0));
}

TEST(reschedule_nonexistent) {
    mcpd::Scheduler sched;
    ASSERT_FALSE(sched.reschedule("nope", 500));
}

// ── Query methods ──────────────────────────────────────────────────

TEST(exec_count) {
    mcpd::Scheduler sched;
    unsigned long base = resetMillis();

    sched.every(100, []() {}, "counter");
    ASSERT_EQ(sched.execCount("counter"), (unsigned long)0);

    _mockMillis() = base + 200; sched.loop();
    ASSERT_EQ(sched.execCount("counter"), (unsigned long)1);

    _mockMillis() = base + 500; sched.loop();
    ASSERT_EQ(sched.execCount("counter"), (unsigned long)2);
}

TEST(exec_count_nonexistent) {
    mcpd::Scheduler sched;
    ASSERT_EQ(sched.execCount("nope"), (unsigned long)0);
}

TEST(get_task_info) {
    mcpd::Scheduler sched;
    resetMillis();

    sched.every(500, []() {}, "info");

    const auto* task = sched.get("info");
    ASSERT(task != nullptr);
    ASSERT_EQ(task->name, String("info"));
    ASSERT_EQ(task->intervalMs, (unsigned long)500);
    ASSERT_EQ(task->paused, false);
    ASSERT_EQ(task->oneShot, false);
}

TEST(get_nonexistent_returns_null) {
    mcpd::Scheduler sched;
    ASSERT(sched.get("nope") == nullptr);
}

TEST(get_by_index) {
    mcpd::Scheduler sched;
    resetMillis();
    sched.every(100, []() {}, "first");

    const auto* t = sched.getByIndex(0);
    ASSERT(t != nullptr);
    ASSERT_EQ(t->name, String("first"));

    ASSERT(sched.getByIndex(-1) == nullptr);
    ASSERT(sched.getByIndex(1) == nullptr);
}

TEST(count_and_size) {
    mcpd::Scheduler sched;
    resetMillis();

    ASSERT_EQ(sched.count(), (size_t)0);
    ASSERT_EQ(sched.size(), (size_t)0);

    sched.every(100, []() {}, "a");
    sched.every(200, []() {}, "b");

    ASSERT_EQ(sched.count(), (size_t)2);
    ASSERT_EQ(sched.size(), (size_t)2);
}

TEST(clear_all) {
    mcpd::Scheduler sched;
    resetMillis();

    sched.every(100, []() {});
    sched.every(200, []() {});
    sched.clear();

    ASSERT_EQ(sched.count(), (size_t)0);
    ASSERT_EQ(sched.size(), (size_t)0);
}

TEST(max_tasks_getter) {
    mcpd::Scheduler sched(16);
    ASSERT_EQ(sched.maxTasks(), (size_t)16);
}

// ── Multiple tasks ─────────────────────────────────────────────────

TEST(multiple_tasks_different_intervals) {
    mcpd::Scheduler sched;
    int fast = 0, slow = 0;
    unsigned long base = resetMillis();

    sched.every(100, [&]() { fast++; }, "fast");
    sched.every(500, [&]() { slow++; }, "slow");

    _mockMillis() = base + 200; sched.loop();
    ASSERT_EQ(fast, 1); ASSERT_EQ(slow, 0);

    _mockMillis() = base + 400; sched.loop();
    ASSERT_EQ(fast, 2); ASSERT_EQ(slow, 0);

    _mockMillis() = base + 700; sched.loop();
    ASSERT_EQ(fast, 3); ASSERT_EQ(slow, 1);
}

TEST(loop_returns_execution_count) {
    mcpd::Scheduler sched;
    unsigned long base = resetMillis();

    sched.every(100, []() {}, "a");
    sched.every(100, []() {}, "b");

    int count = sched.loop();
    ASSERT_EQ(count, 0);

    _mockMillis() = base + 500;
    count = sched.loop();
    ASSERT_EQ(count, 2);
}

TEST(one_shot_mixed_with_repeating) {
    mcpd::Scheduler sched;
    int oneshot = 0, repeating = 0;
    unsigned long base = resetMillis();

    sched.at(base + 300, [&]() { oneshot++; }, "once");
    sched.every(100, [&]() { repeating++; }, "repeat");

    _mockMillis() = base + 200; sched.loop();
    ASSERT_EQ(oneshot, 0); ASSERT_GE(repeating, 1);

    _mockMillis() = base + 500; sched.loop();
    ASSERT_EQ(oneshot, 1); ASSERT_GE(repeating, 2);

    _mockMillis() = base + 800; sched.loop();
    ASSERT_EQ(oneshot, 1); ASSERT_GE(repeating, 3);
}

// ── Edge cases ─────────────────────────────────────────────────────

TEST(at_immediate_past_due) {
    mcpd::Scheduler sched;
    int counter = 0;
    resetMillis(1000);

    sched.at(50, [&]() { counter++; });

    sched.loop();
    ASSERT_EQ(counter, 1);
}

TEST(empty_name_not_findable) {
    mcpd::Scheduler sched;
    resetMillis();

    int idx = sched.every(100, []() {}, "");
    ASSERT_EQ(idx, 0);

    ASSERT_FALSE(sched.pause(""));
    ASSERT_FALSE(sched.remove(""));
}

TEST(null_name) {
    mcpd::Scheduler sched;
    resetMillis();

    int idx = sched.every(100, []() {}, nullptr);
    ASSERT_EQ(idx, 0);
}

TEST(null_callback_no_crash) {
    mcpd::Scheduler sched;
    unsigned long base = resetMillis();

    sched.every(100, nullptr);
    _mockMillis() = base + 200;
    sched.loop(); // should not crash
}

TEST(garbage_collection_after_removal) {
    mcpd::Scheduler sched;
    unsigned long base = resetMillis();

    sched.every(100, []() {}, "a");
    sched.every(100, []() {}, "b");
    sched.remove("a");

    // Loop triggers b and GCs a
    _mockMillis() = base + 500;
    sched.loop();

    ASSERT_EQ(sched.size(), (size_t)1);
    ASSERT_TRUE(sched.exists("b"));
}

// ── toJSON ─────────────────────────────────────────────────────────

TEST(to_json_empty) {
    mcpd::Scheduler sched;
    String json = sched.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"taskCount\":0");
    ASSERT_STR_CONTAINS(json.c_str(), "\"tasks\":[]");
}

TEST(to_json_with_tasks) {
    mcpd::Scheduler sched;
    resetMillis();

    sched.every(1000, []() {}, "sensor");
    sched.every(5000, []() {}, "battery");

    String json = sched.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"taskCount\":2");
    ASSERT_STR_CONTAINS(json.c_str(), "\"sensor\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"battery\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"intervalMs\":1000");
    ASSERT_STR_CONTAINS(json.c_str(), "\"intervalMs\":5000");
}

TEST(to_json_paused_task) {
    mcpd::Scheduler sched;
    resetMillis();

    sched.every(100, []() {}, "paused_task");
    sched.pause("paused_task");

    String json = sched.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"paused\":true");
}

TEST(to_json_one_shot) {
    mcpd::Scheduler sched;
    resetMillis();

    sched.at(99999, []() {}, "once");

    String json = sched.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"oneShot\":true");
}

TEST(to_json_max_executions) {
    mcpd::Scheduler sched;
    resetMillis();

    sched.times(100, 5, []() {}, "limited");

    String json = sched.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"maxExecutions\":5");
}

// ── Capacity ───────────────────────────────────────────────────────

TEST(fill_to_capacity) {
    mcpd::Scheduler sched(8);
    resetMillis();

    for (int i = 0; i < 8; i++) {
        String name = "task" + String(i);
        int idx = sched.every(10000 * (unsigned long)(i + 1), []() {}, name.c_str());
        ASSERT(idx >= 0);
    }
    ASSERT_EQ(sched.count(), (size_t)8);

    ASSERT_EQ(sched.every(100, []() {}), -1);
    ASSERT_EQ(sched.at(99999, []() {}), -1);
}

TEST(remove_and_readd) {
    mcpd::Scheduler sched(2);
    unsigned long base = resetMillis();

    sched.every(100, []() {}, "a");
    sched.every(100, []() {}, "b");

    ASSERT_EQ(sched.every(100, []() {}), -1);

    sched.remove("b");
    _mockMillis() = base + 500; sched.loop(); // GC

    int idx = sched.every(100, []() {}, "c");
    ASSERT(idx >= 0);
}

TEST(default_max_tasks) {
    mcpd::Scheduler sched;
    ASSERT_EQ(sched.maxTasks(), (size_t)32);
}

TEST(late_execution_single_fire) {
    mcpd::Scheduler sched;
    int counter = 0;
    unsigned long base = resetMillis();

    sched.every(100, [&]() { counter++; });

    // Skip way ahead — should only fire once
    _mockMillis() = base + 5000;
    sched.loop();
    ASSERT_EQ(counter, 1);
}

int main() {
    printf("\n  === MCPScheduler Tests ===\n\n");
    TEST_SUMMARY();
    return _tests_failed;
}
