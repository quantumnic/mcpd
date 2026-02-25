/**
 * mcpd — Tests for MCPRateLimit (Token Bucket Rate Limiting)
 */
#include "arduino_mock.h"
#include "test_framework.h"
#include "../src/mcpd.h"
#include "../src/mcpd.cpp"

using namespace mcpd;

// ═══════════════════════════════════════════════════════════════════════
//  Basic RateLimiter
// ═══════════════════════════════════════════════════════════════════════

TEST(rl_default_disabled) {
    RateLimiter rl;
    ASSERT_FALSE(rl.isEnabled());
    ASSERT_TRUE(rl.tryAcquire());
    ASSERT_TRUE(rl.tryAcquire());
}

TEST(rl_configure) {
    RateLimiter rl;
    rl.configure(10.0f, 5);
    ASSERT_TRUE(rl.isEnabled());
    ASSERT_EQ((int)rl.burstCapacity(), 5);
    ASSERT_TRUE(rl.requestsPerSecond() > 9.9f && rl.requestsPerSecond() < 10.1f);
}

TEST(rl_burst_capacity) {
    RateLimiter rl;
    rl.configure(10.0f, 3);
    ASSERT_TRUE(rl.tryAcquire());
    ASSERT_TRUE(rl.tryAcquire());
    ASSERT_TRUE(rl.tryAcquire());
    ASSERT_FALSE(rl.tryAcquire());
}

TEST(rl_stats_tracking) {
    RateLimiter rl;
    rl.configure(10.0f, 2);
    rl.tryAcquire();
    rl.tryAcquire();
    rl.tryAcquire();
    rl.tryAcquire();
    ASSERT_EQ((int)rl.totalAllowed(), 2);
    ASSERT_EQ((int)rl.totalDenied(), 2);
}

TEST(rl_reset_stats) {
    RateLimiter rl;
    rl.configure(10.0f, 2);
    rl.tryAcquire();
    rl.tryAcquire();
    rl.tryAcquire();
    rl.resetStats();
    ASSERT_EQ((int)rl.totalAllowed(), 0);
    ASSERT_EQ((int)rl.totalDenied(), 0);
}

TEST(rl_disable) {
    RateLimiter rl;
    rl.configure(1.0f, 1);
    rl.tryAcquire();
    ASSERT_FALSE(rl.tryAcquire());
    rl.disable();
    ASSERT_FALSE(rl.isEnabled());
    ASSERT_TRUE(rl.tryAcquire());
    ASSERT_TRUE(rl.tryAcquire());
}

TEST(rl_refill) {
    RateLimiter rl;
    // With auto-incrementing millis(), each call to millis() advances by 1.
    // At 1000 tokens/sec, each ms adds 1 token, so tokens refill between calls.
    // Use a low rate so auto-increment doesn't matter, then jump time.
    rl.configure(1.0f, 2);  // 1/sec
    rl.tryAcquire();
    rl.tryAcquire();
    ASSERT_FALSE(rl.tryAcquire());
    _mockMillis() += 2000;  // 2 seconds = 2 tokens
    ASSERT_TRUE(rl.tryAcquire());
}

TEST(rl_refill_capped) {
    RateLimiter rl;
    rl.configure(1.0f, 3);  // 1/sec, burst 3
    rl.tryAcquire();
    rl.tryAcquire();
    rl.tryAcquire();
    ASSERT_FALSE(rl.tryAcquire());
    _mockMillis() += 50000;  // 50 seconds — way more than 3 tokens
    // Should be capped at 3
    ASSERT_TRUE(rl.tryAcquire());
    ASSERT_TRUE(rl.tryAcquire());
    ASSERT_TRUE(rl.tryAcquire());
    ASSERT_FALSE(rl.tryAcquire());
}

TEST(rl_available_tokens) {
    RateLimiter rl;
    rl.configure(10.0f, 5);
    ASSERT_TRUE(rl.availableTokens() > 4.9f);
    rl.tryAcquire();
    ASSERT_TRUE(rl.availableTokens() > 3.5f && rl.availableTokens() < 4.5f);
}

TEST(rl_cost_parameter) {
    RateLimiter rl;
    rl.configure(10.0f, 10);
    ASSERT_TRUE(rl.tryAcquire(5.0f));
    ASSERT_FALSE(rl.tryAcquire(6.0f));
    ASSERT_TRUE(rl.tryAcquire(4.0f));
}

TEST(rl_zero_cost) {
    RateLimiter rl;
    rl.configure(10.0f, 1);
    rl.tryAcquire();
    ASSERT_TRUE(rl.tryAcquire(0.0f));
    ASSERT_TRUE(rl.tryAcquire(-1.0f));
}

TEST(rl_retry_after_ms) {
    RateLimiter rl;
    rl.configure(10.0f, 2);
    ASSERT_EQ((int)rl.retryAfterMs(), 0);
    rl.tryAcquire();
    rl.tryAcquire();
    unsigned long wait = rl.retryAfterMs();
    ASSERT_TRUE(wait > 0);
    ASSERT_TRUE(wait <= 200);
}

TEST(rl_retry_after_disabled) {
    RateLimiter rl;
    ASSERT_EQ((int)rl.retryAfterMs(), 0);
}

TEST(rl_to_json) {
    RateLimiter rl;
    rl.configure(10.0f, 5);
    rl.tryAcquire();
    String json = rl.toJson();
    ASSERT_TRUE(json.indexOf("\"enabled\":true") >= 0);
    ASSERT_TRUE(json.indexOf("\"burstCapacity\":5") >= 0);
    ASSERT_TRUE(json.indexOf("\"totalAllowed\":1") >= 0);
    ASSERT_TRUE(json.indexOf("\"totalDenied\":0") >= 0);
}

TEST(rl_to_json_disabled) {
    RateLimiter rl;
    String json = rl.toJson();
    ASSERT_TRUE(json.indexOf("\"enabled\":false") >= 0);
}

TEST(rl_reconfigure) {
    RateLimiter rl;
    rl.configure(10.0f, 5);
    rl.tryAcquire();
    rl.tryAcquire();
    ASSERT_EQ((int)rl.totalAllowed(), 2);
    rl.configure(20.0f, 10);
    ASSERT_EQ((int)rl.totalAllowed(), 0);
    ASSERT_EQ((int)rl.burstCapacity(), 10);
    ASSERT_TRUE(rl.requestsPerSecond() > 19.9f);
}

// ═══════════════════════════════════════════════════════════════════════
//  KeyedRateLimiter — Per-Key Rate Limiting
// ═══════════════════════════════════════════════════════════════════════

TEST(keyed_basic_usage) {
    KeyedRateLimiter krl(10.0f, 3, 8);
    ASSERT_TRUE(krl.isEnabled());
    ASSERT_EQ((int)krl.activeKeys(), 0);
    ASSERT_EQ((int)krl.maxKeys(), 8);
    ASSERT_TRUE(krl.tryAcquire("client-A"));
    ASSERT_EQ((int)krl.activeKeys(), 1);
    ASSERT_TRUE(krl.hasKey("client-A"));
}

TEST(keyed_independent_buckets) {
    KeyedRateLimiter krl(10.0f, 2, 8);
    krl.tryAcquire("A");
    krl.tryAcquire("A");
    ASSERT_FALSE(krl.tryAcquire("A"));
    ASSERT_TRUE(krl.tryAcquire("B"));
    ASSERT_TRUE(krl.tryAcquire("B"));
    ASSERT_FALSE(krl.tryAcquire("B"));
}

TEST(keyed_stats_tracking) {
    KeyedRateLimiter krl(10.0f, 1, 8);
    krl.tryAcquire("A");
    krl.tryAcquire("A");
    krl.tryAcquire("B");
    ASSERT_EQ((int)krl.totalAllowed(), 2);
    ASSERT_EQ((int)krl.totalDenied(), 1);
}

TEST(keyed_lru_eviction) {
    KeyedRateLimiter krl(10.0f, 5, 3);
    _mockMillis() += 10;
    krl.tryAcquire("A");
    _mockMillis() += 10;
    krl.tryAcquire("B");
    _mockMillis() += 10;
    krl.tryAcquire("C");
    ASSERT_EQ((int)krl.activeKeys(), 3);
    ASSERT_EQ((int)krl.evictions(), 0);
    _mockMillis() += 10;
    krl.tryAcquire("D");
    ASSERT_EQ((int)krl.activeKeys(), 3);
    ASSERT_EQ((int)krl.evictions(), 1);
    ASSERT_FALSE(krl.hasKey("A"));
    ASSERT_TRUE(krl.hasKey("D"));
}

TEST(keyed_remove_key) {
    KeyedRateLimiter krl(10.0f, 5, 8);
    krl.tryAcquire("A");
    krl.tryAcquire("B");
    ASSERT_EQ((int)krl.activeKeys(), 2);
    ASSERT_TRUE(krl.removeKey("A"));
    ASSERT_EQ((int)krl.activeKeys(), 1);
    ASSERT_FALSE(krl.hasKey("A"));
    ASSERT_TRUE(krl.hasKey("B"));
    ASSERT_FALSE(krl.removeKey("Z"));
    ASSERT_FALSE(krl.removeKey(nullptr));
}

TEST(keyed_reset) {
    KeyedRateLimiter krl(10.0f, 5, 8);
    krl.tryAcquire("A");
    krl.tryAcquire("B");
    krl.reset();
    ASSERT_EQ((int)krl.activeKeys(), 0);
    ASSERT_EQ((int)krl.totalAllowed(), 0);
    ASSERT_EQ((int)krl.totalDenied(), 0);
    ASSERT_EQ((int)krl.evictions(), 0);
}

TEST(keyed_disabled) {
    KeyedRateLimiter krl(10.0f, 1, 4);
    krl.tryAcquire("A");
    ASSERT_FALSE(krl.tryAcquire("A"));
    krl.setEnabled(false);
    ASSERT_TRUE(krl.tryAcquire("A"));
}

TEST(keyed_null_empty_key) {
    KeyedRateLimiter krl(10.0f, 1, 4);
    ASSERT_TRUE(krl.tryAcquire(nullptr));
    ASSERT_TRUE(krl.tryAcquire(""));
    ASSERT_EQ((int)krl.activeKeys(), 0);
}

TEST(keyed_cost_parameter) {
    KeyedRateLimiter krl(10.0f, 10, 4);
    ASSERT_TRUE(krl.tryAcquire("A", 7.0f));
    ASSERT_FALSE(krl.tryAcquire("A", 5.0f));
    ASSERT_TRUE(krl.tryAcquire("A", 2.0f));
}

TEST(keyed_refill) {
    KeyedRateLimiter krl(1.0f, 2, 4);  // 1/sec
    krl.tryAcquire("X");
    krl.tryAcquire("X");
    ASSERT_FALSE(krl.tryAcquire("X"));
    _mockMillis() += 2000;
    ASSERT_TRUE(krl.tryAcquire("X"));
}

TEST(keyed_configure) {
    KeyedRateLimiter krl(10.0f, 2, 4);
    krl.tryAcquire("A");
    krl.tryAcquire("A");
    ASSERT_FALSE(krl.tryAcquire("A"));
    krl.configure(20.0f, 5);
    ASSERT_TRUE(krl.tryAcquire("A"));
}

TEST(keyed_to_json) {
    KeyedRateLimiter krl(10.0f, 5, 16);
    krl.tryAcquire("A");
    krl.tryAcquire("B");
    String json = krl.toJson();
    ASSERT_TRUE(json.indexOf("\"enabled\":true") >= 0);
    ASSERT_TRUE(json.indexOf("\"activeKeys\":2") >= 0);
    ASSERT_TRUE(json.indexOf("\"maxKeys\":16") >= 0);
    ASSERT_TRUE(json.indexOf("\"totalAllowed\":2") >= 0);
    ASSERT_TRUE(json.indexOf("\"evictions\":0") >= 0);
}

TEST(keyed_pool_size_1) {
    KeyedRateLimiter krl(10.0f, 5, 1);
    krl.tryAcquire("A");
    ASSERT_TRUE(krl.hasKey("A"));
    _mockMillis() += 10;
    krl.tryAcquire("B");
    ASSERT_FALSE(krl.hasKey("A"));
    ASSERT_TRUE(krl.hasKey("B"));
    ASSERT_EQ((int)krl.evictions(), 1);
}

TEST(keyed_max_keys_zero) {
    KeyedRateLimiter krl(10.0f, 5, 0);
    ASSERT_EQ((int)krl.maxKeys(), 1);
    ASSERT_TRUE(krl.tryAcquire("A"));
}

TEST(keyed_long_key_truncation) {
    KeyedRateLimiter krl(10.0f, 2, 4);
    const char* longKey = "this-is-a-very-long-client-identifier-that-exceeds-32";
    ASSERT_TRUE(krl.tryAcquire(longKey));
    ASSERT_EQ((int)krl.activeKeys(), 1);
}

TEST(keyed_many_evictions) {
    KeyedRateLimiter krl(10.0f, 5, 2);
    char key[8];
    for (int i = 0; i < 10; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        _mockMillis() += 10;
        krl.tryAcquire(key);
    }
    ASSERT_EQ((int)krl.activeKeys(), 2);
    ASSERT_EQ((int)krl.evictions(), 8);
}

// ═══════════════════════════════════════════════════════════════════════
//  Server Integration
// ═══════════════════════════════════════════════════════════════════════

TEST(server_ratelimit_setup) {
    Server server("test");
    server.setRateLimit(10.0f, 5);
    ASSERT_TRUE(server.rateLimiter().isEnabled());
    ASSERT_EQ((int)server.rateLimiter().burstCapacity(), 5);
}

TEST(server_ratelimit_enforced) {
    Server server("test");
    server.setRateLimit(10.0f, 2);
    String req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\",\"version\":\"1.0\"}}}";
    String resp1 = server._processJsonRpc(req);
    ASSERT_TRUE(resp1.indexOf("\"result\"") >= 0);
    String resp2 = server._processJsonRpc(req);
    ASSERT_TRUE(resp2.indexOf("\"result\"") >= 0);
    String resp3 = server._processJsonRpc(req);
    ASSERT_TRUE(resp3.indexOf("-32000") >= 0 || resp3.indexOf("rate") >= 0 || resp3.indexOf("Rate") >= 0);
}

TEST(server_ratelimit_in_info) {
    Server server("test");
    server.setRateLimit(10.0f, 5);
    String req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\",\"version\":\"1.0\"}}}";
    String resp = server._processJsonRpc(req);
    ASSERT_TRUE(resp.indexOf("rateLimit") >= 0);
}

TEST(rl_version) {
    ASSERT_EQ(String(MCPD_VERSION), String("0.47.0"));
}

// ─── Main ───

int main() {
    printf("\n📋 MCPRateLimit Tests\n");
    printf("  ════════════════════════════════════════\n\n");
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
