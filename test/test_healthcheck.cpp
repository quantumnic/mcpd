/**
 * Tests for MCPHealthCheck — Structured health monitoring
 */
#include "arduino_mock.h"
#include "test_framework.h"
#include "../src/mcpd.h"
#include "../src/mcpd.cpp"

using namespace mcpd;

// ─── Basic Registration ───

TEST(hc_empty) {
    HealthCheck hc;
    ASSERT_EQ((int)hc.checkCount(), 0);
    ASSERT(hc.isEnabled());
    ASSERT_EQ((int)hc.maxChecks(), (int)HealthCheck::DEFAULT_MAX_CHECKS);
    ASSERT_EQ((int)hc.totalRuns(), 0);
}

TEST(hc_add_check) {
    HealthCheck hc;
    ASSERT(hc.addCheck("wifi", []() -> HealthStatus {
        return HealthStatus::healthy("Connected");
    }));
    ASSERT_EQ((int)hc.checkCount(), 1);
    ASSERT(hc.hasCheck("wifi"));
    ASSERT(!hc.hasCheck("bluetooth"));
}

TEST(hc_add_duplicate) {
    HealthCheck hc;
    hc.addCheck("wifi", []() { return HealthStatus::healthy(); });
    ASSERT(!hc.addCheck("wifi", []() { return HealthStatus::degraded(); }));
    ASSERT_EQ((int)hc.checkCount(), 1);
}

TEST(hc_add_null_fn) {
    HealthCheck hc;
    ASSERT(!hc.addCheck("bad", nullptr));
    ASSERT_EQ((int)hc.checkCount(), 0);
}

TEST(hc_add_max_reached) {
    HealthCheck hc(2);
    hc.addCheck("a", []() { return HealthStatus::healthy(); });
    hc.addCheck("b", []() { return HealthStatus::healthy(); });
    ASSERT(!hc.addCheck("c", []() { return HealthStatus::healthy(); }));
    ASSERT_EQ((int)hc.checkCount(), 2);
}

TEST(hc_remove) {
    HealthCheck hc;
    hc.addCheck("wifi", []() { return HealthStatus::healthy(); });
    hc.addCheck("heap", []() { return HealthStatus::healthy(); });
    ASSERT(hc.removeCheck("wifi"));
    ASSERT_EQ((int)hc.checkCount(), 1);
    ASSERT(!hc.hasCheck("wifi"));
    ASSERT(hc.hasCheck("heap"));
}

TEST(hc_remove_nonexistent) {
    HealthCheck hc;
    ASSERT(!hc.removeCheck("nope"));
}

TEST(hc_check_names) {
    HealthCheck hc;
    hc.addCheck("wifi", []() { return HealthStatus::healthy(); });
    hc.addCheck("heap", []() { return HealthStatus::healthy(); });
    hc.addCheck("sensor", []() { return HealthStatus::healthy(); });
    auto names = hc.checkNames();
    ASSERT_EQ((int)names.size(), 3);
    ASSERT_EQ(names[0], String("wifi"));
    ASSERT_EQ(names[1], String("heap"));
    ASSERT_EQ(names[2], String("sensor"));
}

// ─── Running Checks ───

TEST(hc_run_all_healthy) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("a", []() { return HealthStatus::healthy("OK"); });
    hc.addCheck("b", []() { return HealthStatus::healthy("Fine"); });
    ASSERT(hc.run() == HealthLevel::Healthy);
    ASSERT_EQ((int)hc.totalRuns(), 1);
}

TEST(hc_run_degraded) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("a", []() { return HealthStatus::healthy(); });
    hc.addCheck("b", []() { return HealthStatus::degraded("Low memory"); });
    ASSERT(hc.run() == HealthLevel::Degraded);
}

TEST(hc_run_unhealthy) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("a", []() { return HealthStatus::healthy(); });
    hc.addCheck("b", []() { return HealthStatus::unhealthy("Dead"); });
    ASSERT(hc.run() == HealthLevel::Unhealthy);
}

TEST(hc_unhealthy_trumps_degraded) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("a", []() { return HealthStatus::degraded(); });
    hc.addCheck("b", []() { return HealthStatus::unhealthy(); });
    ASSERT(hc.run() == HealthLevel::Unhealthy);
}

TEST(hc_run_empty) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    ASSERT(hc.run() == HealthLevel::Healthy);
}

TEST(hc_run_disabled_system) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("a", []() { return HealthStatus::unhealthy(); });
    hc.setEnabled(false);
    ASSERT(hc.run() == HealthLevel::Healthy);
    ASSERT(!hc.isEnabled());
}

TEST(hc_check_result) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("wifi", []() { return HealthStatus::degraded("Weak signal"); });
    hc.run();
    HealthStatus result = hc.checkResult("wifi");
    ASSERT(result.level == HealthLevel::Degraded);
    ASSERT_EQ(result.message, String("Weak signal"));
}

TEST(hc_check_result_not_found) {
    HealthCheck hc;
    HealthStatus result = hc.checkResult("nope");
    ASSERT(result.level == HealthLevel::Unhealthy);
}

// ─── Non-Critical Checks ───

TEST(hc_noncritical_degraded_ignored) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("critical", []() { return HealthStatus::healthy(); }, true);
    hc.addCheck("optional", []() { return HealthStatus::degraded("Meh"); }, false);
    ASSERT(hc.run() == HealthLevel::Healthy);
}

TEST(hc_noncritical_unhealthy_ignored) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("critical", []() { return HealthStatus::healthy(); }, true);
    hc.addCheck("optional", []() { return HealthStatus::unhealthy("Down"); }, false);
    ASSERT(hc.run() == HealthLevel::Healthy);
}

// ─── Enable/Disable Individual Checks ───

TEST(hc_disable_check) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("bad", []() { return HealthStatus::unhealthy("Broken"); });
    ASSERT(hc.setCheckEnabled("bad", false));
    ASSERT(hc.run() == HealthLevel::Healthy);
}

TEST(hc_enable_check) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("bad", []() { return HealthStatus::unhealthy("Broken"); });
    hc.setCheckEnabled("bad", false);
    hc.run();
    hc.setCheckEnabled("bad", true);
    ASSERT(hc.run() == HealthLevel::Unhealthy);
}

TEST(hc_set_enabled_nonexistent) {
    HealthCheck hc;
    ASSERT(!hc.setCheckEnabled("nope", false));
}

// ─── Caching ───

TEST(hc_cache_reuses_results) {
    int callCount = 0;
    HealthCheck hc;
    hc.setCacheDuration(10000);
    hc.addCheck("counter", [&callCount]() {
        callCount++;
        return HealthStatus::healthy();
    });
    hc.run();
    ASSERT_EQ(callCount, 1);
    hc.run();
    ASSERT_EQ(callCount, 1);  // cached
    ASSERT_EQ((int)hc.totalRuns(), 1);
}

TEST(hc_invalidate_cache) {
    int callCount = 0;
    HealthCheck hc;
    hc.setCacheDuration(10000);
    hc.addCheck("counter", [&callCount]() {
        callCount++;
        return HealthStatus::healthy();
    });
    hc.run();
    hc.invalidate();
    hc.run();
    ASSERT_EQ(callCount, 2);
}

TEST(hc_no_cache) {
    int callCount = 0;
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("counter", [&callCount]() {
        callCount++;
        return HealthStatus::healthy();
    });
    hc.run();
    hc.run();
    hc.run();
    ASSERT_EQ(callCount, 3);
    ASSERT_EQ((int)hc.totalRuns(), 3);
}

// ─── Listeners ───

TEST(hc_listener_on_change) {
    HealthLevel captured_old = HealthLevel::Healthy;
    HealthLevel captured_new = HealthLevel::Healthy;
    int notifyCount = 0;
    bool isUnhealthy = false;

    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("dynamic", [&isUnhealthy]() {
        return isUnhealthy ? HealthStatus::unhealthy() : HealthStatus::healthy();
    });
    hc.onChange([&](HealthLevel oldL, HealthLevel newL) {
        captured_old = oldL;
        captured_new = newL;
        notifyCount++;
    });

    hc.run();  // healthy -> healthy
    ASSERT_EQ(notifyCount, 0);

    isUnhealthy = true;
    hc.run();  // healthy -> unhealthy
    ASSERT_EQ(notifyCount, 1);
    ASSERT(captured_old == HealthLevel::Healthy);
    ASSERT(captured_new == HealthLevel::Unhealthy);

    hc.run();  // unhealthy -> unhealthy
    ASSERT_EQ(notifyCount, 1);

    isUnhealthy = false;
    hc.run();  // unhealthy -> healthy
    ASSERT_EQ(notifyCount, 2);
}

TEST(hc_remove_listener) {
    int notifyCount = 0;
    bool isUnhealthy = false;

    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("dynamic", [&isUnhealthy]() {
        return isUnhealthy ? HealthStatus::unhealthy() : HealthStatus::healthy();
    });
    size_t id = hc.onChange([&](HealthLevel, HealthLevel) { notifyCount++; });

    isUnhealthy = true;
    hc.run();
    ASSERT_EQ(notifyCount, 1);

    hc.removeListener(id);
    isUnhealthy = false;
    hc.run();
    ASSERT_EQ(notifyCount, 1);  // no more notifications
}

TEST(hc_remove_listener_nonexistent) {
    HealthCheck hc;
    ASSERT(!hc.removeListener(999));
}

// ─── Reset ───

TEST(hc_reset) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("a", []() { return HealthStatus::unhealthy(); });
    hc.onChange([](HealthLevel, HealthLevel) {});
    hc.run();
    hc.reset();
    ASSERT_EQ((int)hc.checkCount(), 0);
    ASSERT_EQ((int)hc.totalRuns(), 0);
    ASSERT(hc.overallHealth() == HealthLevel::Healthy);
}

// ─── Level to String ───

TEST(hc_level_to_string) {
    ASSERT_STR_CONTAINS(HealthCheck::levelToString(HealthLevel::Healthy), "healthy");
    ASSERT_STR_CONTAINS(HealthCheck::levelToString(HealthLevel::Degraded), "degraded");
    ASSERT_STR_CONTAINS(HealthCheck::levelToString(HealthLevel::Unhealthy), "unhealthy");
}

// ─── JSON ───

TEST(hc_json_empty) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    String json = hc.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"status\":\"healthy\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"checks\":{}");
}

TEST(hc_json_with_checks) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("wifi", []() { return HealthStatus::healthy("Connected"); });
    hc.addCheck("heap", []() { return HealthStatus::degraded("Low"); });
    String json = hc.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"status\":\"degraded\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"wifi\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"heap\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"Connected\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"Low\"");
}

TEST(hc_json_escapes_quotes) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("test", []() { return HealthStatus::healthy("say \"hello\""); });
    String json = hc.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\\\"hello\\\"");
}

TEST(hc_stats_json) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("a", []() { return HealthStatus::healthy(); });
    hc.addCheck("b", []() { return HealthStatus::healthy(); });
    hc.run();
    String json = hc.statsJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"checkCount\":2");
    ASSERT_STR_CONTAINS(json.c_str(), "\"totalRuns\":1");
    ASSERT_STR_CONTAINS(json.c_str(), "\"enabled\":true");
}

// ─── Server Integration ───

TEST(hc_server_accessor) {
    mcpd::Server server("test");
    auto& hc = server.healthCheck();
    ASSERT(hc.addCheck("test", []() { return HealthStatus::healthy("OK"); }));
    ASSERT_EQ((int)server.healthCheck().checkCount(), 1);
}

// ─── Edge Cases ───

TEST(hc_overall_without_run) {
    HealthCheck hc;
    ASSERT(hc.overallHealth() == HealthLevel::Healthy);
}

TEST(hc_multiple_unhealthy) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("a", []() { return HealthStatus::unhealthy("A down"); });
    hc.addCheck("b", []() { return HealthStatus::unhealthy("B down"); });
    hc.addCheck("c", []() { return HealthStatus::degraded("C weak"); });
    ASSERT(hc.run() == HealthLevel::Unhealthy);
}

TEST(hc_latency_tracked) {
    HealthCheck hc;
    hc.setCacheDuration(0);
    hc.addCheck("slow", []() { return HealthStatus::healthy("Done"); });
    hc.run();
    HealthStatus result = hc.checkResult("slow");
    ASSERT(result.latencyMs >= 0);
    ASSERT_EQ(result.message, String("Done"));
}

TEST(hc_cache_duration) {
    HealthCheck hc;
    ASSERT_EQ((int)hc.cacheDuration(), (int)HealthCheck::DEFAULT_CACHE_MS);
    hc.setCacheDuration(1000);
    ASSERT_EQ((int)hc.cacheDuration(), 1000);
}

TEST(hc_version) {
    ASSERT_EQ(String(MCPD_VERSION), String("0.48.0"));
}

// ─── Main ───

int main() {
    printf("\n📋 MCPHealthCheck Tests\n");
    printf("  ════════════════════════════════════════\n\n");
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
