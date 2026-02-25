/**
 * Tests for MCPCircuitBreaker — circuit breaker pattern
 */
#include "test_framework.h"
#include "MCPCircuitBreaker.h"

// ── CircuitBreaker basic ───────────────────────────────────────

TEST(CB_StartsClosedState) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 3, 5000);
    ASSERT_TRUE(cb.isClosed());
    ASSERT_STR_EQ(cb.stateStr(), "closed");
    ASSERT_EQ(cb.failureCount(), (size_t)0);
}

TEST(CB_AllowsRequestsWhenClosed) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 3, 5000);
    ASSERT_TRUE(cb.allowRequest());
    ASSERT_TRUE(cb.allowRequest());
    ASSERT_TRUE(cb.allowRequest());
}

TEST(CB_StaysClosedBelowThreshold) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 3, 5000);
    cb.recordFailure();
    cb.recordFailure();
    ASSERT_TRUE(cb.isClosed());
    ASSERT_EQ(cb.failureCount(), (size_t)2);
    ASSERT_TRUE(cb.allowRequest());
}

TEST(CB_TripsOpenAtThreshold) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 3, 5000);
    cb.recordFailure();
    cb.recordFailure();
    cb.recordFailure();
    ASSERT_TRUE(cb.isOpen());
    ASSERT_STR_EQ(cb.stateStr(), "open");
}

TEST(CB_RejectsRequestsWhenOpen) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 3, 5000);
    cb.recordFailure();
    cb.recordFailure();
    cb.recordFailure();
    ASSERT_FALSE(cb.allowRequest());
    ASSERT_FALSE(cb.allowRequest());
    ASSERT_EQ(cb.totalRejected(), (size_t)2);
}

TEST(CB_SuccessResetsFailureCount) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 3, 5000);
    cb.recordFailure();
    cb.recordFailure();
    cb.recordSuccess();
    ASSERT_EQ(cb.failureCount(), (size_t)0);
    ASSERT_TRUE(cb.isClosed());
}

TEST(CB_TransitionsToHalfOpenAfterTimeout) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 2, 100);  // 100ms recovery
    cb.recordFailure();
    cb.recordFailure();
    ASSERT_TRUE(cb.isOpen());
    delay(150);
    ASSERT_TRUE(cb.allowRequest());
    ASSERT_TRUE(cb.isHalfOpen());
    ASSERT_STR_EQ(cb.stateStr(), "half_open");
}

TEST(CB_HalfOpenSuccessClosesCB) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 2, 100, 1);
    cb.recordFailure();
    cb.recordFailure();
    delay(150);
    cb.allowRequest();  // Triggers HALF_OPEN
    cb.recordSuccess();
    ASSERT_TRUE(cb.isClosed());
}

TEST(CB_HalfOpenFailureReopens) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 2, 100);
    cb.recordFailure();
    cb.recordFailure();
    delay(150);
    cb.allowRequest();
    cb.recordFailure();
    ASSERT_TRUE(cb.isOpen());
}

TEST(CB_HalfOpenMultipleSuccessesNeeded) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 2, 100, 3);  // Need 3 successes in half-open
    cb.recordFailure();
    cb.recordFailure();
    delay(150);
    cb.allowRequest();
    cb.recordSuccess();
    ASSERT_TRUE(cb.isHalfOpen());  // Still half-open, need 2 more
    cb.recordSuccess();
    ASSERT_TRUE(cb.isHalfOpen());  // Still half-open, need 1 more
    cb.recordSuccess();
    ASSERT_TRUE(cb.isClosed());    // Now closed
}

TEST(CB_ManualReset) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 2, 5000);
    cb.recordFailure();
    cb.recordFailure();
    ASSERT_TRUE(cb.isOpen());
    cb.reset();
    ASSERT_TRUE(cb.isClosed());
    ASSERT_EQ(cb.failureCount(), (size_t)0);
    ASSERT_TRUE(cb.allowRequest());
}

TEST(CB_ManualTrip) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 5, 5000);
    ASSERT_TRUE(cb.isClosed());
    cb.trip();
    ASSERT_TRUE(cb.isOpen());
    ASSERT_EQ(cb.tripCount(), (size_t)1);
}

TEST(CB_StatsTracking) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 3, 5000);
    cb.recordSuccess();
    cb.recordSuccess();
    cb.recordFailure();
    cb.recordSuccess();
    ASSERT_EQ(cb.totalSuccesses(), (size_t)3);
    ASSERT_EQ(cb.totalFailures(), (size_t)1);
}

TEST(CB_TripCountTracking) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 2, 100);
    // First trip
    cb.recordFailure();
    cb.recordFailure();
    ASSERT_EQ(cb.tripCount(), (size_t)1);
    // Reset and trip again
    cb.reset();
    cb.recordFailure();
    cb.recordFailure();
    ASSERT_EQ(cb.tripCount(), (size_t)2);
}

TEST(CB_RetryAfterMs) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 2, 1000);
    ASSERT_EQ(cb.retryAfterMs(), (unsigned long)0);  // Not open
    cb.recordFailure();
    cb.recordFailure();
    unsigned long ra = cb.retryAfterMs();
    ASSERT_TRUE(ra > 0 && ra <= 1000);
}

TEST(CB_RetryAfterZeroWhenClosed) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 2, 1000);
    ASSERT_EQ(cb.retryAfterMs(), (unsigned long)0);
}

TEST(CB_KeyStored) {
    mcpd::CircuitBreaker cb;
    cb.configure("i2c-sensor", 3, 5000);
    ASSERT_STR_EQ(cb.key(), "i2c-sensor");
}

TEST(CB_StateChangeCallback) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 2, 100);
    int callCount = 0;
    mcpd::CircuitState lastState = mcpd::CircuitState::CLOSED;
    cb.onStateChange([&](const char*, mcpd::CircuitState s) {
        callCount++;
        lastState = s;
    });
    cb.recordFailure();
    ASSERT_EQ(callCount, 0);  // Not tripped yet
    cb.recordFailure();
    ASSERT_EQ(callCount, 1);
    ASSERT_TRUE(lastState == mcpd::CircuitState::OPEN);
    // Wait for half-open
    delay(150);
    cb.allowRequest();
    ASSERT_EQ(callCount, 2);
    ASSERT_TRUE(lastState == mcpd::CircuitState::HALF_OPEN);
    // Success → closed
    cb.recordSuccess();
    ASSERT_EQ(callCount, 3);
    ASSERT_TRUE(lastState == mcpd::CircuitState::CLOSED);
}

TEST(CB_JsonSerialization) {
    mcpd::CircuitBreaker cb;
    cb.configure("spi-flash", 3, 5000);
    cb.recordSuccess();
    cb.recordFailure();
    String json = cb.toJson();
    ASSERT_TRUE(json.indexOf("\"key\":\"spi-flash\"") >= 0);
    ASSERT_TRUE(json.indexOf("\"state\":\"closed\"") >= 0);
    ASSERT_TRUE(json.indexOf("\"failureCount\":1") >= 0);
    ASSERT_TRUE(json.indexOf("\"failureThreshold\":3") >= 0);
    ASSERT_TRUE(json.indexOf("\"totalSuccesses\":1") >= 0);
    ASSERT_TRUE(json.indexOf("\"totalFailures\":1") >= 0);
}

TEST(CB_JsonOpenState) {
    mcpd::CircuitBreaker cb;
    cb.configure("test", 2, 5000);
    cb.recordFailure();
    cb.recordFailure();
    String json = cb.toJson();
    ASSERT_TRUE(json.indexOf("\"state\":\"open\"") >= 0);
    ASSERT_TRUE(json.indexOf("\"tripCount\":1") >= 0);
}

// ── CircuitBreakerRegistry ─────────────────────────────────────

TEST(CBR_CreatesBreakersOnGet) {
    mcpd::CircuitBreakerRegistry reg(3, 5000);
    ASSERT_EQ(reg.count(), (size_t)0);
    auto& cb = reg.get("sensor-1");
    ASSERT_EQ(reg.count(), (size_t)1);
    ASSERT_STR_EQ(cb.key(), "sensor-1");
    ASSERT_TRUE(cb.isClosed());
}

TEST(CBR_ReturnsSameBreaker) {
    mcpd::CircuitBreakerRegistry reg(3, 5000);
    auto& cb1 = reg.get("motor");
    cb1.recordFailure();
    auto& cb2 = reg.get("motor");
    ASSERT_EQ(cb2.failureCount(), (size_t)1);
    ASSERT_EQ(reg.count(), (size_t)1);
}

TEST(CBR_MultipleDifferentBreakers) {
    mcpd::CircuitBreakerRegistry reg(3, 5000);
    reg.get("a");
    reg.get("b");
    reg.get("c");
    ASSERT_EQ(reg.count(), (size_t)3);
}

TEST(CBR_HasKey) {
    mcpd::CircuitBreakerRegistry reg(3, 5000);
    ASSERT_FALSE(reg.has("xyz"));
    reg.get("xyz");
    ASSERT_TRUE(reg.has("xyz"));
}

TEST(CBR_RemoveKey) {
    mcpd::CircuitBreakerRegistry reg(3, 5000);
    reg.get("a");
    reg.get("b");
    ASSERT_EQ(reg.count(), (size_t)2);
    ASSERT_TRUE(reg.remove("a"));
    ASSERT_EQ(reg.count(), (size_t)1);
    ASSERT_FALSE(reg.has("a"));
    ASSERT_TRUE(reg.has("b"));
}

TEST(CBR_RemoveNonExistent) {
    mcpd::CircuitBreakerRegistry reg(3, 5000);
    ASSERT_FALSE(reg.remove("nope"));
}

TEST(CBR_LRUEviction) {
    mcpd::CircuitBreakerRegistry reg(2, 5000, 3);  // max 3 breakers
    reg.get("a");
    delay(10);
    reg.get("b");
    delay(10);
    reg.get("c");
    ASSERT_EQ(reg.count(), (size_t)3);
    delay(10);
    // Access "a" to make it recent
    reg.get("a");
    delay(10);
    // Adding "d" should evict "b" (least recently accessed)
    reg.get("d");
    ASSERT_EQ(reg.count(), (size_t)3);
    ASSERT_TRUE(reg.has("a"));
    ASSERT_FALSE(reg.has("b"));  // Evicted
    ASSERT_TRUE(reg.has("c"));
    ASSERT_TRUE(reg.has("d"));
}

TEST(CBR_ResetAll) {
    mcpd::CircuitBreakerRegistry reg(2, 5000);
    auto& a = reg.get("a");
    auto& b = reg.get("b");
    a.recordFailure();
    a.recordFailure();
    b.recordFailure();
    b.recordFailure();
    ASSERT_TRUE(a.isOpen());
    ASSERT_TRUE(b.isOpen());
    reg.resetAll();
    ASSERT_TRUE(a.isClosed());
    ASSERT_TRUE(b.isClosed());
}

TEST(CBR_OpenCount) {
    mcpd::CircuitBreakerRegistry reg(2, 5000);
    auto& a = reg.get("a");
    auto& b = reg.get("b");
    reg.get("c");
    a.recordFailure();
    a.recordFailure();
    ASSERT_EQ(reg.openCount(), (size_t)1);
    b.recordFailure();
    b.recordFailure();
    ASSERT_EQ(reg.openCount(), (size_t)2);
}

TEST(CBR_GetByState) {
    mcpd::CircuitBreakerRegistry reg(2, 5000);
    auto& a = reg.get("x");
    reg.get("y");
    auto& c = reg.get("z");
    a.recordFailure();
    a.recordFailure();
    c.recordFailure();
    c.recordFailure();
    const char* keys[4];
    size_t n = reg.getByState(mcpd::CircuitState::OPEN, keys, 4);
    ASSERT_EQ(n, (size_t)2);
    n = reg.getByState(mcpd::CircuitState::CLOSED, keys, 4);
    ASSERT_EQ(n, (size_t)1);
}

TEST(CBR_GlobalStateChangeCallback) {
    mcpd::CircuitBreakerRegistry reg(2, 5000);
    int callCount = 0;
    reg.onStateChange([&](const char*, mcpd::CircuitState) {
        callCount++;
    });
    auto& a = reg.get("a");
    a.recordFailure();
    a.recordFailure();
    ASSERT_EQ(callCount, 1);
    // New breaker also gets callback
    auto& b = reg.get("b");
    b.recordFailure();
    b.recordFailure();
    ASSERT_EQ(callCount, 2);
}

TEST(CBR_InheritsConfiguration) {
    mcpd::CircuitBreakerRegistry reg(4, 2000, 8, 2);
    auto& cb = reg.get("test");
    ASSERT_EQ(cb.failureThreshold(), (size_t)4);
    ASSERT_EQ(cb.recoveryTimeoutMs(), (unsigned long)2000);
}

TEST(CBR_JsonSerialization) {
    mcpd::CircuitBreakerRegistry reg(3, 5000, 8);
    reg.get("motor");
    reg.get("sensor");
    String json = reg.toJson();
    ASSERT_TRUE(json.indexOf("\"count\":2") >= 0);
    ASSERT_TRUE(json.indexOf("\"maxBreakers\":8") >= 0);
    ASSERT_TRUE(json.indexOf("\"motor\"") >= 0);
    ASSERT_TRUE(json.indexOf("\"sensor\"") >= 0);
}

TEST(CBR_MaxBreakers) {
    mcpd::CircuitBreakerRegistry reg(3, 5000, 4);
    ASSERT_EQ(reg.maxBreakers(), (size_t)4);
}

// ── Integration scenarios ──────────────────────────────────────

TEST(CB_TypicalSensorFailureScenario) {
    // Simulate: sensor works, then starts failing, then recovers
    mcpd::CircuitBreaker cb;
    cb.configure("dht22", 3, 200);

    // Normal operation
    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(cb.allowRequest());
        cb.recordSuccess();
    }
    ASSERT_TRUE(cb.isClosed());

    // Sensor starts failing
    cb.recordFailure();
    cb.recordFailure();
    ASSERT_TRUE(cb.isClosed());  // Not yet tripped
    cb.recordFailure();
    ASSERT_TRUE(cb.isOpen());    // Tripped!

    // Calls rejected during cooldown
    ASSERT_FALSE(cb.allowRequest());
    ASSERT_FALSE(cb.allowRequest());

    // Wait for recovery
    delay(250);

    // Probe succeeds
    ASSERT_TRUE(cb.allowRequest());
    ASSERT_TRUE(cb.isHalfOpen());
    cb.recordSuccess();
    ASSERT_TRUE(cb.isClosed());  // Back to normal

    // Stats check
    ASSERT_EQ(cb.totalSuccesses(), (size_t)11);
    ASSERT_EQ(cb.totalFailures(), (size_t)3);
    ASSERT_EQ(cb.totalRejected(), (size_t)2);
    ASSERT_EQ(cb.tripCount(), (size_t)1);
}

TEST(CB_MultipleTripsScenario) {
    mcpd::CircuitBreaker cb;
    cb.configure("i2c", 2, 100);

    // First trip
    cb.recordFailure();
    cb.recordFailure();
    ASSERT_TRUE(cb.isOpen());

    // Wait, half-open, fail again → re-trip
    delay(150);
    cb.allowRequest();
    cb.recordFailure();
    ASSERT_TRUE(cb.isOpen());
    ASSERT_EQ(cb.tripCount(), (size_t)2);

    // Wait, half-open, succeed this time
    delay(150);
    cb.allowRequest();
    cb.recordSuccess();
    ASSERT_TRUE(cb.isClosed());
    ASSERT_EQ(cb.tripCount(), (size_t)2);
}

TEST(CBR_PeripheralIsolation) {
    // Different peripherals have independent circuit breakers
    mcpd::CircuitBreakerRegistry reg(2, 100);
    auto& i2c = reg.get("i2c-bus");
    auto& spi = reg.get("spi-bus");

    // I2C fails
    i2c.recordFailure();
    i2c.recordFailure();
    ASSERT_TRUE(i2c.isOpen());

    // SPI still works
    ASSERT_TRUE(spi.isClosed());
    ASSERT_TRUE(spi.allowRequest());
    spi.recordSuccess();
    ASSERT_TRUE(spi.isClosed());
}

int main() {
    printf("\n MCPCircuitBreaker Tests\n");
    printf("  ========================================\n\n");
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
