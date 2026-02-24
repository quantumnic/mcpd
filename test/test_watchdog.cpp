/**
 * Tests for MCPWatchdog — Software Watchdog (Task Health Monitor)
 */
#include "test_framework.h"
#include "../src/MCPWatchdog.h"

// ─── Basic Operations ───

TEST(add_entry) {
    mcpd::Watchdog wd(8);
    ASSERT(wd.add("task1", 1000));
    ASSERT_EQ((int)wd.count(), 1);
    ASSERT(wd.exists("task1"));
}

TEST(add_duplicate_fails) {
    mcpd::Watchdog wd(8);
    ASSERT(wd.add("task1", 1000));
    ASSERT(!wd.add("task1", 2000));
    ASSERT_EQ((int)wd.count(), 1);
}

TEST(add_null_name_fails) {
    mcpd::Watchdog wd(8);
    ASSERT(!wd.add(nullptr, 1000));
}

TEST(add_zero_timeout_fails) {
    mcpd::Watchdog wd(8);
    ASSERT(!wd.add("task1", 0));
}

TEST(add_over_capacity_fails) {
    mcpd::Watchdog wd(2);
    ASSERT(wd.add("a", 1000));
    ASSERT(wd.add("b", 1000));
    ASSERT(!wd.add("c", 1000));
}

TEST(remove_entry) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 1000);
    wd.add("task2", 2000);
    ASSERT(wd.remove("task1"));
    ASSERT_EQ((int)wd.count(), 1);
    ASSERT(!wd.exists("task1"));
    ASSERT(wd.exists("task2"));
}

TEST(remove_nonexistent_fails) {
    mcpd::Watchdog wd(8);
    ASSERT(!wd.remove("nope"));
}

TEST(clear_entries) {
    mcpd::Watchdog wd(8);
    wd.add("a", 1000);
    wd.add("b", 2000);
    wd.clear();
    ASSERT_EQ((int)wd.count(), 0);
}

// ─── Kick & Check ───

TEST(kick_resets_deadline) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    ASSERT(wd.kick("task1", 0));
    ASSERT_EQ((int)wd.check(50), 0);  // not expired yet
    ASSERT(wd.state("task1") == mcpd::WatchdogState::Healthy);
}

TEST(check_fires_on_timeout) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.kick("task1", 0);
    ASSERT_EQ((int)wd.check(100), 1);
    ASSERT(wd.state("task1") == mcpd::WatchdogState::Expired);
}

TEST(check_fires_only_once) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.kick("task1", 0);
    ASSERT_EQ((int)wd.check(100), 1);
    ASSERT_EQ((int)wd.check(200), 0);  // already expired, no re-fire
    ASSERT_EQ((int)wd.timeoutCount("task1"), 1);
}

TEST(kick_after_expiry_resets) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.kick("task1", 0);
    wd.check(100);  // expires
    ASSERT(wd.state("task1") == mcpd::WatchdogState::Expired);
    wd.kick("task1", 200);
    ASSERT(wd.state("task1") == mcpd::WatchdogState::Healthy);
    ASSERT_EQ((int)wd.check(250), 0);  // still within deadline
}

TEST(kick_nonexistent_fails) {
    mcpd::Watchdog wd(8);
    ASSERT(!wd.kick("nope", 0));
}

TEST(unstarted_entry_not_checked) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    // Never kicked, so not started
    ASSERT_EQ((int)wd.check(500), 0);
}

TEST(multiple_entries_timeout_independently) {
    mcpd::Watchdog wd(8);
    wd.add("fast", 50);
    wd.add("slow", 200);
    wd.kick("fast", 0);
    wd.kick("slow", 0);

    ASSERT_EQ((int)wd.check(60), 1);   // fast expired
    ASSERT(wd.state("fast") == mcpd::WatchdogState::Expired);
    ASSERT(wd.state("slow") == mcpd::WatchdogState::Healthy);

    ASSERT_EQ((int)wd.check(200), 1);  // slow expired
    ASSERT(wd.state("slow") == mcpd::WatchdogState::Expired);
}

// ─── Pause & Resume ───

TEST(pause_prevents_timeout) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.kick("task1", 0);
    wd.pause("task1");
    ASSERT(wd.state("task1") == mcpd::WatchdogState::Paused);
    ASSERT_EQ((int)wd.check(500), 0);  // paused, no fire
}

TEST(resume_restarts_deadline) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.kick("task1", 0);
    wd.pause("task1");
    ASSERT(wd.resume("task1", 1000));
    ASSERT(wd.state("task1") == mcpd::WatchdogState::Healthy);
    ASSERT_EQ((int)wd.check(1050), 0);  // within new deadline
    ASSERT_EQ((int)wd.check(1100), 1);  // expired from resume time
}

TEST(resume_non_paused_fails) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    ASSERT(!wd.resume("task1", 0));
}

TEST(kick_while_paused_fails) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.pause("task1");
    ASSERT(!wd.kick("task1", 0));
}

TEST(pause_nonexistent_fails) {
    mcpd::Watchdog wd(8);
    ASSERT(!wd.pause("nope"));
}

// ─── Callbacks ───

static const char* lastGlobalName = nullptr;
static uint32_t lastGlobalCount = 0;
static void globalCb(const char* name, uint32_t count) {
    lastGlobalName = name;
    lastGlobalCount = count;
}

TEST(global_callback_fires) {
    lastGlobalName = nullptr;
    lastGlobalCount = 0;

    mcpd::Watchdog wd(8);
    wd.onTimeout(globalCb);
    wd.add("task1", 100);
    wd.kick("task1", 0);
    wd.check(100);

    ASSERT(lastGlobalName != nullptr);
    ASSERT_STR_CONTAINS(lastGlobalName, "task1");
    ASSERT_EQ((int)lastGlobalCount, 1);
}

static const char* lastPerTaskName = nullptr;
static void perTaskCb(const char* name) {
    lastPerTaskName = name;
}

TEST(per_task_callback_fires) {
    lastPerTaskName = nullptr;

    mcpd::Watchdog wd(8);
    wd.add("task1", 100, perTaskCb);
    wd.kick("task1", 0);
    wd.check(100);

    ASSERT(lastPerTaskName != nullptr);
    ASSERT_STR_CONTAINS(lastPerTaskName, "task1");
}

TEST(both_callbacks_fire) {
    lastGlobalName = nullptr;
    lastPerTaskName = nullptr;

    mcpd::Watchdog wd(8);
    wd.onTimeout(globalCb);
    wd.add("task1", 100, perTaskCb);
    wd.kick("task1", 0);
    wd.check(100);

    ASSERT(lastGlobalName != nullptr);
    ASSERT(lastPerTaskName != nullptr);
}

// ─── Statistics ───

TEST(timeout_count_increments) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.kick("task1", 0);
    wd.check(100);  // expire #1
    ASSERT_EQ((int)wd.timeoutCount("task1"), 1);

    wd.kick("task1", 200);  // reset
    wd.check(300);  // expire #2
    ASSERT_EQ((int)wd.timeoutCount("task1"), 2);
}

TEST(reset_count) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.kick("task1", 0);
    wd.check(100);
    ASSERT(wd.resetCount("task1"));
    ASSERT_EQ((int)wd.timeoutCount("task1"), 0);
}

TEST(reset_count_nonexistent_fails) {
    mcpd::Watchdog wd(8);
    ASSERT(!wd.resetCount("nope"));
}

TEST(expired_count) {
    mcpd::Watchdog wd(8);
    wd.add("a", 50);
    wd.add("b", 200);
    wd.kick("a", 0);
    wd.kick("b", 0);
    wd.check(100);
    ASSERT_EQ((int)wd.expiredCount(), 1);
    ASSERT_EQ((int)wd.healthyCount(), 1);
}

TEST(paused_count) {
    mcpd::Watchdog wd(8);
    wd.add("a", 100);
    wd.add("b", 100);
    wd.pause("a");
    ASSERT_EQ((int)wd.pausedCount(), 1);
    ASSERT_EQ((int)wd.healthyCount(), 1);
}

// ─── Timeout Configuration ───

TEST(get_timeout) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 1234);
    ASSERT_EQ((int)wd.timeout("task1"), 1234);
}

TEST(set_timeout) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    ASSERT(wd.setTimeout("task1", 500));
    ASSERT_EQ((int)wd.timeout("task1"), 500);
}

TEST(set_timeout_zero_fails) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    ASSERT(!wd.setTimeout("task1", 0));
}

TEST(set_timeout_nonexistent_fails) {
    mcpd::Watchdog wd(8);
    ASSERT(!wd.setTimeout("nope", 100));
}

TEST(timeout_nonexistent_returns_zero) {
    mcpd::Watchdog wd(8);
    ASSERT_EQ((int)wd.timeout("nope"), 0);
}

// ─── Capacity ───

TEST(capacity_reported) {
    mcpd::Watchdog wd(16);
    ASSERT_EQ((int)wd.capacity(), 16);
}

// ─── JSON Serialization ───

TEST(json_empty) {
    mcpd::Watchdog wd(4);
    char buf[256];
    wd.toJson(buf, sizeof(buf));
    ASSERT_STR_CONTAINS(buf, "\"entries\":[]");
    ASSERT_STR_CONTAINS(buf, "\"count\":0");
    ASSERT_STR_CONTAINS(buf, "\"capacity\":4");
}

TEST(json_with_entries) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.kick("task1", 0);

    char buf[512];
    wd.toJson(buf, sizeof(buf));
    ASSERT_STR_CONTAINS(buf, "\"name\":\"task1\"");
    ASSERT_STR_CONTAINS(buf, "\"timeoutMs\":100");
    ASSERT_STR_CONTAINS(buf, "\"state\":\"healthy\"");
    ASSERT_STR_CONTAINS(buf, "\"started\":true");
}

TEST(json_expired_state) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.kick("task1", 0);
    wd.check(100);

    char buf[512];
    wd.toJson(buf, sizeof(buf));
    ASSERT_STR_CONTAINS(buf, "\"state\":\"expired\"");
    ASSERT_STR_CONTAINS(buf, "\"timeoutCount\":1");
}

TEST(json_paused_state) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.pause("task1");

    char buf[512];
    wd.toJson(buf, sizeof(buf));
    ASSERT_STR_CONTAINS(buf, "\"state\":\"paused\"");
}

TEST(json_small_buffer) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);

    char buf[10];
    int len = wd.toJson(buf, sizeof(buf));
    ASSERT(len < 10);  // truncated but no crash
}

TEST(json_null_buffer) {
    mcpd::Watchdog wd(8);
    ASSERT_EQ(wd.toJson(nullptr, 0), 0);
}

// ─── Edge Cases ───

TEST(state_of_nonexistent_returns_expired) {
    mcpd::Watchdog wd(8);
    ASSERT(wd.state("nope") == mcpd::WatchdogState::Expired);
}

TEST(timeout_count_of_nonexistent_returns_zero) {
    mcpd::Watchdog wd(8);
    ASSERT_EQ((int)wd.timeoutCount("nope"), 0);
}

TEST(exists_nonexistent) {
    mcpd::Watchdog wd(8);
    ASSERT(!wd.exists("nope"));
}

TEST(remove_middle_entry_preserves_order) {
    mcpd::Watchdog wd(8);
    wd.add("a", 100);
    wd.add("b", 200);
    wd.add("c", 300);
    wd.remove("b");
    ASSERT_EQ((int)wd.count(), 2);
    ASSERT(wd.exists("a"));
    ASSERT(wd.exists("c"));
    ASSERT_EQ((int)wd.timeout("c"), 300);
}

TEST(set_timeout_affects_next_check) {
    mcpd::Watchdog wd(8);
    wd.add("task1", 100);
    wd.kick("task1", 0);
    wd.setTimeout("task1", 500);
    ASSERT_EQ((int)wd.check(200), 0);  // would have expired at 100, but now 500
    ASSERT_EQ((int)wd.check(500), 1);  // now expires
}

TEST(watchdog_state_to_string) {
    ASSERT_STR_CONTAINS(mcpd::watchdogStateToString(mcpd::WatchdogState::Healthy), "healthy");
    ASSERT_STR_CONTAINS(mcpd::watchdogStateToString(mcpd::WatchdogState::Expired), "expired");
    ASSERT_STR_CONTAINS(mcpd::watchdogStateToString(mcpd::WatchdogState::Paused), "paused");
}

// ─── Main ───

int main() {
    printf("\n📋 MCPWatchdog Tests\n");
    printf("  ════════════════════════════════════════\n\n");
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
