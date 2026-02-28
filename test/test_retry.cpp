/**
 * Tests for MCPRetry — retry policies with exponential backoff
 */
#include "test_framework.h"
#include "MCPRetry.h"

// ── RetryResult ────────────────────────────────────────────

TEST(Retry_SuccessResult) {
    auto r = mcpd::RetryResult::success("42");
    ASSERT_TRUE(r.succeeded);
    ASSERT_FALSE(r.canRetry);
    ASSERT_STR_EQ(r.value, "42");
}

TEST(Retry_RetryableResult) {
    auto r = mcpd::RetryResult::retryable("NAK");
    ASSERT_FALSE(r.succeeded);
    ASSERT_TRUE(r.canRetry);
    ASSERT_STR_EQ(r.error, "NAK");
}

TEST(Retry_FatalResult) {
    auto r = mcpd::RetryResult::fatal("bus error");
    ASSERT_FALSE(r.succeeded);
    ASSERT_FALSE(r.canRetry);
    ASSERT_STR_EQ(r.error, "bus error");
}

TEST(Retry_EmptySuccess) {
    auto r = mcpd::RetryResult::success();
    ASSERT_TRUE(r.succeeded);
    ASSERT_STR_EQ(r.value, "");
}

TEST(Retry_EmptyRetryable) {
    auto r = mcpd::RetryResult::retryable();
    ASSERT_STR_EQ(r.error, "error");
}

TEST(Retry_EmptyFatal) {
    auto r = mcpd::RetryResult::fatal();
    ASSERT_STR_EQ(r.error, "fatal error");
}

// ── RetryPolicy ────────────────────────────────────────────

TEST(Policy_DefaultValues) {
    mcpd::RetryPolicy p;
    ASSERT_EQ(p.maxRetries, (size_t)3);
    ASSERT_EQ(p.baseDelayMs, (unsigned long)100);
    ASSERT_EQ(p.maxDelayMs, (unsigned long)10000);
    ASSERT_EQ(p.totalTimeoutMs, (unsigned long)0);
}

TEST(Policy_ConstructorValues) {
    mcpd::RetryPolicy p(5, 200, 3.0f, 5000, mcpd::JitterMode::FULL);
    ASSERT_EQ(p.maxRetries, (size_t)5);
    ASSERT_EQ(p.baseDelayMs, (unsigned long)200);
    ASSERT_EQ(p.maxDelayMs, (unsigned long)5000);
}

TEST(Policy_ExponentialBackoff) {
    mcpd::RetryPolicy p(5, 100, 2.0f, 10000, mcpd::JitterMode::NONE);
    ASSERT_EQ(p.delayForAttempt(0), (unsigned long)100);
    ASSERT_EQ(p.delayForAttempt(1), (unsigned long)200);
    ASSERT_EQ(p.delayForAttempt(2), (unsigned long)400);
    ASSERT_EQ(p.delayForAttempt(3), (unsigned long)800);
}

TEST(Policy_CapsAtMaxDelay) {
    mcpd::RetryPolicy p(10, 100, 2.0f, 500, mcpd::JitterMode::NONE);
    ASSERT_EQ(p.delayForAttempt(0), (unsigned long)100);
    ASSERT_EQ(p.delayForAttempt(1), (unsigned long)200);
    ASSERT_EQ(p.delayForAttempt(2), (unsigned long)400);
    ASSERT_EQ(p.delayForAttempt(3), (unsigned long)500);
    ASSERT_EQ(p.delayForAttempt(10), (unsigned long)500);
}

TEST(Policy_FullJitterInRange) {
    mcpd::RetryPolicy p(3, 1000, 2.0f, 10000, mcpd::JitterMode::FULL);
    for (int i = 0; i < 20; i++) {
        unsigned long d = p.delayForAttempt(0);
        ASSERT_TRUE(d < 1000);
    }
}

TEST(Policy_EqualJitterInRange) {
    mcpd::RetryPolicy p(3, 1000, 2.0f, 10000, mcpd::JitterMode::EQUAL);
    for (int i = 0; i < 20; i++) {
        unsigned long d = p.delayForAttempt(0);
        ASSERT_TRUE(d >= 500);
        ASSERT_TRUE(d < 1000);
    }
}

TEST(Policy_DecorrelatedJitter) {
    mcpd::RetryPolicy p(3, 100, 2.0f, 5000, mcpd::JitterMode::DECORRELATED);
    unsigned long d = p.delayForAttempt(0, 0);
    ASSERT_TRUE(d >= 100);
    ASSERT_TRUE(d <= 5000);
}

TEST(Policy_JitterStr) {
    mcpd::RetryPolicy p;
    p.jitter = mcpd::JitterMode::NONE;
    ASSERT_STR_EQ(p.jitterStr(), "none");
    p.jitter = mcpd::JitterMode::FULL;
    ASSERT_STR_EQ(p.jitterStr(), "full");
    p.jitter = mcpd::JitterMode::EQUAL;
    ASSERT_STR_EQ(p.jitterStr(), "equal");
    p.jitter = mcpd::JitterMode::DECORRELATED;
    ASSERT_STR_EQ(p.jitterStr(), "decorrelated");
}

TEST(Policy_ToJson) {
    mcpd::RetryPolicy p(3, 100, 2.0f, 10000);
    String json = p.toJson();
    ASSERT_TRUE(json.indexOf("\"maxRetries\":3") >= 0);
    ASSERT_TRUE(json.indexOf("\"baseDelayMs\":100") >= 0);
    ASSERT_TRUE(json.indexOf("\"maxDelayMs\":10000") >= 0);
    ASSERT_TRUE(json.indexOf("\"jitter\":\"none\"") >= 0);
}

// ── RetryStats ─────────────────────────────────────────────

TEST(Stats_DefaultZero) {
    mcpd::RetryStats s;
    ASSERT_EQ(s.totalAttempts, (size_t)0);
    ASSERT_EQ(s.totalSuccesses, (size_t)0);
    ASSERT_EQ(s.totalRetries, (size_t)0);
    ASSERT_EQ(s.totalFailures, (size_t)0);
}

TEST(Stats_Reset) {
    mcpd::RetryStats s;
    s.totalAttempts = 10;
    s.totalSuccesses = 5;
    s.reset();
    ASSERT_EQ(s.totalAttempts, (size_t)0);
    ASSERT_EQ(s.totalSuccesses, (size_t)0);
}

TEST(Stats_ToJson) {
    mcpd::RetryStats s;
    s.totalAttempts = 5;
    s.totalSuccesses = 3;
    String json = s.toJson();
    ASSERT_TRUE(json.indexOf("\"totalAttempts\":5") >= 0);
    ASSERT_TRUE(json.indexOf("\"totalSuccesses\":3") >= 0);
}

// ── RetryExecutor ──────────────────────────────────────────

TEST(Executor_SuccessOnFirstTry) {
    mcpd::RetryPolicy p(3, 10);
    mcpd::RetryExecutor exec(p);
    auto result = exec.execute([]() {
        return mcpd::RetryResult::success("ok");
    });
    ASSERT_TRUE(result.succeeded);
    ASSERT_STR_EQ(result.value, "ok");
    ASSERT_EQ(exec.stats().totalAttempts, (size_t)1);
    ASSERT_EQ(exec.stats().totalRetries, (size_t)0);
    ASSERT_EQ(exec.stats().totalSuccesses, (size_t)1);
}

TEST(Executor_SuccessAfterRetries) {
    mcpd::RetryPolicy p(5, 1);  // 1ms delay for fast tests
    mcpd::RetryExecutor exec(p);
    int callCount = 0;
    auto result = exec.execute([&callCount]() -> mcpd::RetryResult {
        callCount++;
        if (callCount < 3) return mcpd::RetryResult::retryable("not ready");
        return mcpd::RetryResult::success("done");
    });
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(callCount, 3);
    ASSERT_EQ(exec.stats().totalAttempts, (size_t)3);
    ASSERT_EQ(exec.stats().totalRetries, (size_t)2);
    ASSERT_EQ(exec.stats().totalSuccesses, (size_t)1);
}

TEST(Executor_FailAfterMaxRetries) {
    mcpd::RetryPolicy p(2, 1);
    mcpd::RetryExecutor exec(p);
    auto result = exec.execute([]() {
        return mcpd::RetryResult::retryable("fail");
    });
    ASSERT_FALSE(result.succeeded);
    ASSERT_EQ(exec.stats().totalAttempts, (size_t)3);  // initial + 2 retries
    ASSERT_EQ(exec.stats().totalRetries, (size_t)2);
    ASSERT_EQ(exec.stats().totalFailures, (size_t)1);
}

TEST(Executor_FatalStopsImmediately) {
    mcpd::RetryPolicy p(5, 1);
    mcpd::RetryExecutor exec(p);
    int callCount = 0;
    auto result = exec.execute([&callCount]() -> mcpd::RetryResult {
        callCount++;
        return mcpd::RetryResult::fatal("hardware broken");
    });
    ASSERT_FALSE(result.succeeded);
    ASSERT_FALSE(result.canRetry);
    ASSERT_EQ(callCount, 1);
    ASSERT_EQ(exec.stats().totalFatalErrors, (size_t)1);
    ASSERT_EQ(exec.stats().totalRetries, (size_t)0);
}

TEST(Executor_ZeroRetries) {
    mcpd::RetryPolicy p(0, 1);
    mcpd::RetryExecutor exec(p);
    auto result = exec.execute([]() {
        return mcpd::RetryResult::retryable("fail");
    });
    ASSERT_FALSE(result.succeeded);
    ASSERT_EQ(exec.stats().totalAttempts, (size_t)1);
    ASSERT_EQ(exec.stats().totalFailures, (size_t)1);
    ASSERT_EQ(exec.stats().totalRetries, (size_t)0);
}

TEST(Executor_OnRetryCallback) {
    mcpd::RetryPolicy p(3, 1);
    mcpd::RetryExecutor exec(p);
    int retryCount = 0;
    exec.onRetry([&retryCount](size_t attempt, const char* error, unsigned long delayMs) {
        retryCount++;
    });
    exec.execute([]() { return mcpd::RetryResult::retryable("err"); });
    ASSERT_EQ(retryCount, 3);
}

TEST(Executor_OnGiveUpCallback) {
    mcpd::RetryPolicy p(2, 1);
    mcpd::RetryExecutor exec(p);
    bool gaveUp = false;
    size_t giveUpAttempts = 0;
    exec.onGiveUp([&](size_t attempts, const char* lastError) {
        gaveUp = true;
        giveUpAttempts = attempts;
    });
    exec.execute([]() { return mcpd::RetryResult::retryable("err"); });
    ASSERT_TRUE(gaveUp);
    ASSERT_EQ(giveUpAttempts, (size_t)3);
}

TEST(Executor_NoGiveUpOnSuccess) {
    mcpd::RetryPolicy p(3, 1);
    mcpd::RetryExecutor exec(p);
    bool gaveUp = false;
    exec.onGiveUp([&](size_t, const char*) { gaveUp = true; });
    exec.execute([]() { return mcpd::RetryResult::success("ok"); });
    ASSERT_FALSE(gaveUp);
}

TEST(Executor_TotalTimeout) {
    mcpd::RetryPolicy p(100, 50);  // Many retries, 50ms each
    p.totalTimeoutMs = 120;        // But total max 120ms
    mcpd::RetryExecutor exec(p);
    auto result = exec.execute([]() {
        return mcpd::RetryResult::retryable("slow");
    });
    ASSERT_FALSE(result.succeeded);
    // Should have timed out before all 100 retries
    ASSERT_TRUE(exec.stats().totalAttempts < 100);
}

TEST(Executor_ResetStats) {
    mcpd::RetryPolicy p(2, 1);
    mcpd::RetryExecutor exec(p);
    exec.execute([]() { return mcpd::RetryResult::retryable("fail"); });
    ASSERT_TRUE(exec.stats().totalAttempts > 0);
    exec.resetStats();
    ASSERT_EQ(exec.stats().totalAttempts, (size_t)0);
}

TEST(Executor_SetPolicy) {
    mcpd::RetryExecutor exec;
    mcpd::RetryPolicy p(7, 500);
    exec.setPolicy(p);
    ASSERT_EQ(exec.policy().maxRetries, (size_t)7);
    ASSERT_EQ(exec.policy().baseDelayMs, (unsigned long)500);
}

TEST(Executor_AccumulatesStats) {
    mcpd::RetryPolicy p(1, 1);
    mcpd::RetryExecutor exec(p);
    exec.execute([]() { return mcpd::RetryResult::success("ok"); });
    exec.execute([]() { return mcpd::RetryResult::retryable("fail"); });
    ASSERT_EQ(exec.stats().totalAttempts, (size_t)3);  // 1 + 2
    ASSERT_EQ(exec.stats().totalSuccesses, (size_t)1);
    ASSERT_EQ(exec.stats().totalFailures, (size_t)1);
}

TEST(Executor_DelayAccumulated) {
    mcpd::RetryPolicy p(2, 10, 2.0f, 10000, mcpd::JitterMode::NONE);
    mcpd::RetryExecutor exec(p);
    exec.execute([]() { return mcpd::RetryResult::retryable("fail"); });
    // Delay: attempt 0 = 10ms, attempt 1 = 20ms → total 30ms
    ASSERT_EQ(exec.stats().totalDelayMs, (unsigned long)30);
}

TEST(Executor_FatalFirstAttemptNoRetry) {
    mcpd::RetryPolicy p(5, 1);
    mcpd::RetryExecutor exec(p);
    int retryCallbacks = 0;
    exec.onRetry([&](size_t, const char*, unsigned long) { retryCallbacks++; });
    exec.execute([]() { return mcpd::RetryResult::fatal("dead"); });
    ASSERT_EQ(retryCallbacks, 0);
    ASSERT_EQ(exec.stats().totalRetries, (size_t)0);
}

TEST(Executor_FatalAfterRetries) {
    mcpd::RetryPolicy p(5, 1);
    mcpd::RetryExecutor exec(p);
    int call = 0;
    auto result = exec.execute([&call]() -> mcpd::RetryResult {
        call++;
        if (call < 3) return mcpd::RetryResult::retryable("flaky");
        return mcpd::RetryResult::fatal("broken");
    });
    ASSERT_FALSE(result.succeeded);
    ASSERT_FALSE(result.canRetry);
    ASSERT_EQ(call, 3);
    ASSERT_EQ(exec.stats().totalRetries, (size_t)2);
    ASSERT_EQ(exec.stats().totalFatalErrors, (size_t)1);
}

// ── RetryPolicyRegistry ────────────────────────────────────

TEST(Registry_InitialEmpty) {
    mcpd::RetryPolicyRegistry reg(8);
    ASSERT_EQ(reg.count(), (size_t)0);
    ASSERT_EQ(reg.maxPolicies(), (size_t)8);
}

TEST(Registry_SetAndGet) {
    mcpd::RetryPolicyRegistry reg;
    mcpd::RetryPolicy p(5, 200);
    reg.set("i2c-sensor", p);
    ASSERT_EQ(reg.count(), (size_t)1);
    ASSERT_TRUE(reg.has("i2c-sensor"));
    const mcpd::RetryPolicy* got = reg.get("i2c-sensor");
    ASSERT_TRUE(got != nullptr);
    ASSERT_EQ(got->maxRetries, (size_t)5);
    ASSERT_EQ(got->baseDelayMs, (unsigned long)200);
}

TEST(Registry_GetNonexistent) {
    mcpd::RetryPolicyRegistry reg;
    ASSERT_FALSE(reg.has("nope"));
    ASSERT_TRUE(reg.get("nope") == nullptr);
}

TEST(Registry_OverwriteExisting) {
    mcpd::RetryPolicyRegistry reg;
    mcpd::RetryPolicy p1(3, 100);
    mcpd::RetryPolicy p2(7, 500);
    reg.set("sensor", p1);
    reg.set("sensor", p2);
    ASSERT_EQ(reg.count(), (size_t)1);
    ASSERT_EQ(reg.get("sensor")->maxRetries, (size_t)7);
}

TEST(Registry_Remove) {
    mcpd::RetryPolicyRegistry reg;
    reg.set("a", mcpd::RetryPolicy(1, 10));
    reg.set("b", mcpd::RetryPolicy(2, 20));
    ASSERT_TRUE(reg.remove("a"));
    ASSERT_EQ(reg.count(), (size_t)1);
    ASSERT_FALSE(reg.has("a"));
    ASSERT_TRUE(reg.has("b"));
}

TEST(Registry_RemoveNonexistent) {
    mcpd::RetryPolicyRegistry reg;
    ASSERT_FALSE(reg.remove("nope"));
}

TEST(Registry_LRUEviction) {
    mcpd::RetryPolicyRegistry reg(2);
    reg.set("a", mcpd::RetryPolicy(1, 10));
    delay(10);
    reg.set("b", mcpd::RetryPolicy(2, 20));
    delay(10);
    // "a" is oldest, should be evicted
    reg.set("c", mcpd::RetryPolicy(3, 30));
    ASSERT_EQ(reg.count(), (size_t)2);
    ASSERT_FALSE(reg.has("a"));
    ASSERT_TRUE(reg.has("b"));
    ASSERT_TRUE(reg.has("c"));
}

TEST(Registry_Clear) {
    mcpd::RetryPolicyRegistry reg;
    reg.set("a", mcpd::RetryPolicy());
    reg.set("b", mcpd::RetryPolicy());
    reg.clear();
    ASSERT_EQ(reg.count(), (size_t)0);
}

TEST(Registry_Keys) {
    mcpd::RetryPolicyRegistry reg;
    reg.set("alpha", mcpd::RetryPolicy());
    reg.set("beta", mcpd::RetryPolicy());
    const char* keys[4];
    size_t n = reg.keys(keys, 4);
    ASSERT_EQ(n, (size_t)2);
}

TEST(Registry_Execute) {
    mcpd::RetryPolicyRegistry reg;
    reg.set("sensor", mcpd::RetryPolicy(2, 1));
    int calls = 0;
    auto result = reg.execute("sensor", [&calls]() -> mcpd::RetryResult {
        calls++;
        if (calls < 2) return mcpd::RetryResult::retryable("not ready");
        return mcpd::RetryResult::success("42");
    });
    ASSERT_TRUE(result.succeeded);
    ASSERT_EQ(calls, 2);
}

TEST(Registry_ExecuteWithDefault) {
    mcpd::RetryPolicyRegistry reg;
    mcpd::RetryPolicy defaultP(1, 1);
    int calls = 0;
    auto result = reg.execute("unregistered", [&calls]() -> mcpd::RetryResult {
        calls++;
        return mcpd::RetryResult::retryable("fail");
    }, defaultP);
    ASSERT_FALSE(result.succeeded);
    ASSERT_EQ(calls, 2);  // initial + 1 retry
}

TEST(Registry_StatsAccumulate) {
    mcpd::RetryPolicyRegistry reg;
    reg.set("s1", mcpd::RetryPolicy(1, 1));
    reg.execute("s1", []() { return mcpd::RetryResult::success("ok"); });
    reg.execute("s1", []() { return mcpd::RetryResult::retryable("fail"); });
    const mcpd::RetryStats* st = reg.stats("s1");
    ASSERT_TRUE(st != nullptr);
    ASSERT_EQ(st->totalSuccesses, (size_t)1);
    ASSERT_EQ(st->totalFailures, (size_t)1);
}

TEST(Registry_StatsNonexistent) {
    mcpd::RetryPolicyRegistry reg;
    ASSERT_TRUE(reg.stats("nope") == nullptr);
}

TEST(Registry_ResetAllStats) {
    mcpd::RetryPolicyRegistry reg;
    reg.set("a", mcpd::RetryPolicy(0, 1));
    reg.execute("a", []() { return mcpd::RetryResult::success("ok"); });
    reg.resetAllStats();
    ASSERT_EQ(reg.stats("a")->totalAttempts, (size_t)0);
}

TEST(Registry_ToJson) {
    mcpd::RetryPolicyRegistry reg;
    reg.set("test", mcpd::RetryPolicy(3, 100));
    String json = reg.toJson();
    ASSERT_TRUE(json.indexOf("\"count\":1") >= 0);
    ASSERT_TRUE(json.indexOf("\"test\"") >= 0);
    ASSERT_TRUE(json.indexOf("\"maxRetries\":3") >= 0);
}

TEST(Registry_MultipleKeys) {
    mcpd::RetryPolicyRegistry reg;
    reg.set("i2c", mcpd::RetryPolicy(3, 100));
    reg.set("spi", mcpd::RetryPolicy(5, 50));
    reg.set("uart", mcpd::RetryPolicy(2, 200));
    ASSERT_EQ(reg.count(), (size_t)3);
    ASSERT_EQ(reg.get("i2c")->maxRetries, (size_t)3);
    ASSERT_EQ(reg.get("spi")->maxRetries, (size_t)5);
    ASSERT_EQ(reg.get("uart")->maxRetries, (size_t)2);
}

// ── Integration-style tests ────────────────────────────────

TEST(Retry_SimulatedSensorRead) {
    // Simulate a sensor that fails twice then succeeds
    mcpd::RetryPolicy policy(5, 1, 2.0f, 100, mcpd::JitterMode::NONE);
    mcpd::RetryExecutor exec(policy);
    int sensorState = 0;
    auto result = exec.execute([&sensorState]() -> mcpd::RetryResult {
        sensorState++;
        if (sensorState <= 2) return mcpd::RetryResult::retryable("I2C NAK");
        return mcpd::RetryResult::success("23.5");
    });
    ASSERT_TRUE(result.succeeded);
    ASSERT_STR_EQ(result.value, "23.5");
}

TEST(Retry_SimulatedHardwareFailure) {
    // Simulate hardware that fails permanently after retries
    mcpd::RetryPolicy policy(3, 1);
    mcpd::RetryExecutor exec(policy);
    bool calledGiveUp = false;
    exec.onGiveUp([&](size_t attempts, const char* err) {
        calledGiveUp = true;
    });
    auto result = exec.execute([]() {
        return mcpd::RetryResult::retryable("bus timeout");
    });
    ASSERT_FALSE(result.succeeded);
    ASSERT_TRUE(calledGiveUp);
}

TEST(Retry_MixedResultSequence) {
    // retryable → retryable → fatal
    mcpd::RetryPolicy policy(5, 1);
    mcpd::RetryExecutor exec(policy);
    int step = 0;
    auto result = exec.execute([&step]() -> mcpd::RetryResult {
        step++;
        if (step <= 2) return mcpd::RetryResult::retryable("busy");
        return mcpd::RetryResult::fatal("short circuit");
    });
    ASSERT_FALSE(result.succeeded);
    ASSERT_FALSE(result.canRetry);
    ASSERT_STR_EQ(result.error, "short circuit");
    ASSERT_EQ(exec.stats().totalFatalErrors, (size_t)1);
    ASSERT_EQ(exec.stats().totalRetries, (size_t)2);
}

TEST(Retry_WithBackoffVerification) {
    mcpd::RetryPolicy p(3, 10, 2.0f, 10000, mcpd::JitterMode::NONE);
    mcpd::RetryExecutor exec(p);
    unsigned long delays[3];
    int retryIdx = 0;
    exec.onRetry([&](size_t attempt, const char*, unsigned long delayMs) {
        if (retryIdx < 3) delays[retryIdx++] = delayMs;
    });
    exec.execute([]() { return mcpd::RetryResult::retryable("err"); });
    ASSERT_EQ(delays[0], (unsigned long)10);   // attempt 0: base
    ASSERT_EQ(delays[1], (unsigned long)20);   // attempt 1: base * 2
    ASSERT_EQ(delays[2], (unsigned long)40);   // attempt 2: base * 4
}

TEST(Retry_ResultLongErrorTruncated) {
    // Error > 63 chars should be truncated safely
    auto r = mcpd::RetryResult::retryable(
        "This is a very long error message that exceeds the buffer size and should be safely truncated");
    ASSERT_TRUE(strlen(r.error) < 64);
}

TEST(Retry_ResultLongValueTruncated) {
    char longVal[200];
    memset(longVal, 'A', 199);
    longVal[199] = '\0';
    auto r = mcpd::RetryResult::success(longVal);
    ASSERT_TRUE(strlen(r.value) < 128);
}

// ── Main ───────────────────────────────────────────────────

int main() {
    printf("\n MCPRetry Tests\n");
    printf("  ========================================\n\n");
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
