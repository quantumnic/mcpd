/**
 * Tests for MCPEventStore — ring-buffer event log
 */

#include "test_framework.h"
#include "MCPEventStore.h"

using namespace mcpd;

// ── Basic Operations ───────────────────────────────────────────────────

TEST(EventStore_EmitAndCount) {
    EventStore store(8);
    ASSERT_EQ((int)store.count(), 0);
    ASSERT_EQ((int)store.capacity(), 8);

    store.emit("test", "hello");
    ASSERT_EQ((int)store.count(), 1);

    store.emit("test", "world");
    ASSERT_EQ((int)store.count(), 2);
}

TEST(EventStore_EmitReturnsSeq) {
    EventStore store(8);
    uint32_t s0 = store.emit("a", "1");
    uint32_t s1 = store.emit("b", "2");
    uint32_t s2 = store.emit("c", "3");
    ASSERT_EQ((int)s0, 0);
    ASSERT_EQ((int)s1, 1);
    ASSERT_EQ((int)s2, 2);
    ASSERT_EQ((int)store.nextSeq(), 3);
}

TEST(EventStore_AllReturnsOldestFirst) {
    EventStore store(8);
    store.emit("a", "first");
    store.emit("b", "second");
    store.emit("c", "third");

    auto events = store.all();
    ASSERT_EQ((int)events.size(), 3);
    ASSERT_STR_CONTAINS(events[0].data.c_str(), "first");
    ASSERT_STR_CONTAINS(events[1].data.c_str(), "second");
    ASSERT_STR_CONTAINS(events[2].data.c_str(), "third");
}

TEST(EventStore_DefaultSeverityIsInfo) {
    EventStore store(8);
    store.emit("tag", "data");
    auto events = store.all();
    ASSERT_EQ((int)events[0].severity, (int)EventSeverity::Info);
}

TEST(EventStore_CustomSeverity) {
    EventStore store(8);
    store.emit("err", "bad", EventSeverity::Error);
    store.emit("dbg", "ok", EventSeverity::Debug);
    auto events = store.all();
    ASSERT_EQ((int)events[0].severity, (int)EventSeverity::Error);
    ASSERT_EQ((int)events[1].severity, (int)EventSeverity::Debug);
}

// ── Ring Buffer Behavior ───────────────────────────────────────────────

TEST(EventStore_RingBufferEviction) {
    EventStore store(4);
    store.emit("a", "0");
    store.emit("b", "1");
    store.emit("c", "2");
    store.emit("d", "3");
    ASSERT_EQ((int)store.count(), 4);
    ASSERT_TRUE(store.isFull());

    // This should evict "0"
    store.emit("e", "4");
    ASSERT_EQ((int)store.count(), 4);

    auto events = store.all();
    ASSERT_EQ((int)events.size(), 4);
    ASSERT_EQ((int)events[0].seq, 1); // "0" was evicted
    ASSERT_EQ((int)events[3].seq, 4);
}

TEST(EventStore_RingBufferWraparound) {
    EventStore store(3);
    for (int i = 0; i < 10; i++) {
        store.emit("t", String(i));
    }
    ASSERT_EQ((int)store.count(), 3);
    auto events = store.all();
    ASSERT_EQ((int)events[0].seq, 7);
    ASSERT_EQ((int)events[1].seq, 8);
    ASSERT_EQ((int)events[2].seq, 9);
    ASSERT_STR_CONTAINS(events[0].data.c_str(), "7");
    ASSERT_STR_CONTAINS(events[2].data.c_str(), "9");
}

TEST(EventStore_CapacityOneWorks) {
    EventStore store(1);
    store.emit("a", "first");
    ASSERT_EQ((int)store.count(), 1);
    store.emit("b", "second");
    ASSERT_EQ((int)store.count(), 1);
    auto events = store.all();
    ASSERT_STR_CONTAINS(events[0].data.c_str(), "second");
}

TEST(EventStore_ZeroCapacityClampsToOne) {
    EventStore store(0);
    ASSERT_EQ((int)store.capacity(), 1);
    store.emit("a", "data");
    ASSERT_EQ((int)store.count(), 1);
}

// ── Filtering ──────────────────────────────────────────────────────────

TEST(EventStore_ByTag) {
    EventStore store(16);
    store.emit("temp", "22");
    store.emit("gpio", "1");
    store.emit("temp", "23");
    store.emit("gpio", "0");

    auto temps = store.byTag("temp");
    ASSERT_EQ((int)temps.size(), 2);
    ASSERT_STR_CONTAINS(temps[0].data.c_str(), "22");
    ASSERT_STR_CONTAINS(temps[1].data.c_str(), "23");

    auto gpios = store.byTag("gpio");
    ASSERT_EQ((int)gpios.size(), 2);
}

TEST(EventStore_ByTagNonExistent) {
    EventStore store(8);
    store.emit("temp", "22");
    auto result = store.byTag("nonexistent");
    ASSERT_EQ((int)result.size(), 0);
}

TEST(EventStore_BySeverity) {
    EventStore store(16);
    store.emit("a", "1", EventSeverity::Debug);
    store.emit("b", "2", EventSeverity::Info);
    store.emit("c", "3", EventSeverity::Warning);
    store.emit("d", "4", EventSeverity::Error);
    store.emit("e", "5", EventSeverity::Critical);

    auto warnings = store.bySeverity(EventSeverity::Warning);
    ASSERT_EQ((int)warnings.size(), 3); // warning, error, critical

    auto errors = store.bySeverity(EventSeverity::Error);
    ASSERT_EQ((int)errors.size(), 2); // error, critical

    auto all = store.bySeverity(EventSeverity::Debug);
    ASSERT_EQ((int)all.size(), 5);
}

TEST(EventStore_SinceTimestamp) {
    EventStore store(16);
    // All events will have timestamp ~0 (mocked millis)
    store.emit("a", "1");
    store.emit("b", "2");
    auto result = store.since(0);
    ASSERT_EQ((int)result.size(), 2);

    auto none = store.since(999999);
    ASSERT_EQ((int)none.size(), 0);
}

TEST(EventStore_SinceSeq) {
    EventStore store(16);
    store.emit("a", "0");
    store.emit("b", "1");
    store.emit("c", "2");
    store.emit("d", "3");

    auto result = store.sinceSeq(2);
    ASSERT_EQ((int)result.size(), 2);
    ASSERT_EQ((int)result[0].seq, 2);
    ASSERT_EQ((int)result[1].seq, 3);
}

TEST(EventStore_SinceSeqZero) {
    EventStore store(8);
    store.emit("a", "0");
    store.emit("b", "1");
    auto result = store.sinceSeq(0);
    ASSERT_EQ((int)result.size(), 2);
}

TEST(EventStore_Last) {
    EventStore store(16);
    for (int i = 0; i < 10; i++) {
        store.emit("t", String(i));
    }

    auto last3 = store.last(3);
    ASSERT_EQ((int)last3.size(), 3);
    ASSERT_EQ((int)last3[0].seq, 7);
    ASSERT_EQ((int)last3[2].seq, 9);
}

TEST(EventStore_LastMoreThanCount) {
    EventStore store(16);
    store.emit("a", "1");
    store.emit("b", "2");
    auto result = store.last(100);
    ASSERT_EQ((int)result.size(), 2);
}

TEST(EventStore_CombinedQuery) {
    EventStore store(16);
    store.emit("temp", "20", EventSeverity::Info);
    store.emit("gpio", "1", EventSeverity::Debug);
    store.emit("temp", "99", EventSeverity::Error);
    store.emit("gpio", "0", EventSeverity::Warning);

    // Tag "temp" + severity >= Warning
    auto result = store.query("temp", EventSeverity::Warning);
    ASSERT_EQ((int)result.size(), 1);
    ASSERT_STR_CONTAINS(result[0].data.c_str(), "99");
}

TEST(EventStore_QueryEmptyTag) {
    EventStore store(16);
    store.emit("a", "1", EventSeverity::Debug);
    store.emit("b", "2", EventSeverity::Error);

    // Empty tag = match all, severity >= Error
    auto result = store.query("", EventSeverity::Error);
    ASSERT_EQ((int)result.size(), 1);
    ASSERT_STR_CONTAINS(result[0].tag.c_str(), "b");
}

// ── Tags ───────────────────────────────────────────────────────────────

TEST(EventStore_Tags) {
    EventStore store(16);
    store.emit("temp", "1");
    store.emit("gpio", "2");
    store.emit("temp", "3");
    store.emit("adc", "4");

    auto tags = store.tags();
    ASSERT_EQ((int)tags.size(), 3);
    // Order should be: temp, gpio, adc (first occurrence order)
    ASSERT_STR_CONTAINS(tags[0].c_str(), "temp");
    ASSERT_STR_CONTAINS(tags[1].c_str(), "gpio");
    ASSERT_STR_CONTAINS(tags[2].c_str(), "adc");
}

TEST(EventStore_TagsEmpty) {
    EventStore store(8);
    auto tags = store.tags();
    ASSERT_EQ((int)tags.size(), 0);
}

// ── JSON Serialization ─────────────────────────────────────────────────

TEST(EventStore_ToJSONEmpty) {
    EventStore store(8);
    String json = store.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "[]");
}

TEST(EventStore_ToJSONSingle) {
    EventStore store(8);
    store.emit("temp", "{\"value\":22.5}");
    String json = store.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"tag\":\"temp\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"severity\":\"info\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"value\":22.5");
    ASSERT_STR_CONTAINS(json.c_str(), "\"seq\":0");
}

TEST(EventStore_ToJSONPlainData) {
    EventStore store(8);
    store.emit("msg", "hello world");
    String json = store.toJSON();
    // Plain string data should be quoted
    ASSERT_STR_CONTAINS(json.c_str(), "\"data\":\"hello world\"");
}

TEST(EventStore_ToJSONArray) {
    EventStore store(8);
    store.emit("arr", "[1,2,3]");
    String json = store.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"data\":[1,2,3]");
}

TEST(EventStore_ToJSONFiltered) {
    EventStore store(8);
    store.emit("a", "1");
    store.emit("b", "2");
    auto filtered = store.byTag("a");
    String json = store.toJSON(filtered);
    ASSERT_STR_CONTAINS(json.c_str(), "\"tag\":\"a\"");
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"tag\":\"b\"");
}

// ── Stats ──────────────────────────────────────────────────────────────

TEST(EventStore_StatsJSON) {
    EventStore store(8);
    store.emit("a", "1", EventSeverity::Info);
    store.emit("b", "2", EventSeverity::Error);
    store.emit("c", "3", EventSeverity::Error);

    String stats = store.statsJSON();
    ASSERT_STR_CONTAINS(stats.c_str(), "\"count\":3");
    ASSERT_STR_CONTAINS(stats.c_str(), "\"capacity\":8");
    ASSERT_STR_CONTAINS(stats.c_str(), "\"full\":false");
    ASSERT_STR_CONTAINS(stats.c_str(), "\"info\":1");
    ASSERT_STR_CONTAINS(stats.c_str(), "\"error\":2");
}

TEST(EventStore_StatsEvicted) {
    EventStore store(2);
    store.emit("a", "1");
    store.emit("b", "2");
    store.emit("c", "3"); // evicts "a"

    String stats = store.statsJSON();
    ASSERT_STR_CONTAINS(stats.c_str(), "\"evicted\":1");
    ASSERT_STR_CONTAINS(stats.c_str(), "\"full\":true");
}

// ── Listeners ──────────────────────────────────────────────────────────

TEST(EventStore_Listener) {
    EventStore store(8);
    int callCount = 0;
    String lastTag;

    store.onEvent([&](const Event& e) {
        callCount++;
        lastTag = e.tag;
    });

    store.emit("sensor", "42");
    ASSERT_EQ(callCount, 1);
    ASSERT_STR_CONTAINS(lastTag.c_str(), "sensor");

    store.emit("gpio", "1");
    ASSERT_EQ(callCount, 2);
    ASSERT_STR_CONTAINS(lastTag.c_str(), "gpio");
}

TEST(EventStore_MultipleListeners) {
    EventStore store(8);
    int count1 = 0, count2 = 0;

    store.onEvent([&](const Event&) { count1++; });
    store.onEvent([&](const Event&) { count2++; });

    store.emit("test", "data");
    ASSERT_EQ(count1, 1);
    ASSERT_EQ(count2, 1);
}

TEST(EventStore_ClearListeners) {
    EventStore store(8);
    int callCount = 0;

    store.onEvent([&](const Event&) { callCount++; });
    store.emit("a", "1");
    ASSERT_EQ(callCount, 1);

    store.clearListeners();
    store.emit("b", "2");
    ASSERT_EQ(callCount, 1); // unchanged
}

// ── Clear ──────────────────────────────────────────────────────────────

TEST(EventStore_Clear) {
    EventStore store(8);
    store.emit("a", "1");
    store.emit("b", "2");
    ASSERT_EQ((int)store.count(), 2);

    store.clear();
    ASSERT_EQ((int)store.count(), 0);
    ASSERT_EQ((int)store.nextSeq(), 0);
    ASSERT_FALSE(store.isFull());

    auto events = store.all();
    ASSERT_EQ((int)events.size(), 0);
}

TEST(EventStore_ClearAndReuse) {
    EventStore store(4);
    store.emit("a", "1");
    store.emit("b", "2");
    store.clear();

    store.emit("c", "3");
    ASSERT_EQ((int)store.count(), 1);
    auto events = store.all();
    ASSERT_EQ((int)events[0].seq, 0); // seq resets
    ASSERT_STR_CONTAINS(events[0].tag.c_str(), "c");
}

// ── Severity String Conversion ─────────────────────────────────────────

TEST(EventStore_SeverityToString) {
    ASSERT_STR_CONTAINS(severityToString(EventSeverity::Debug), "debug");
    ASSERT_STR_CONTAINS(severityToString(EventSeverity::Info), "info");
    ASSERT_STR_CONTAINS(severityToString(EventSeverity::Warning), "warning");
    ASSERT_STR_CONTAINS(severityToString(EventSeverity::Error), "error");
    ASSERT_STR_CONTAINS(severityToString(EventSeverity::Critical), "critical");
}

TEST(EventStore_SeverityFromString) {
    ASSERT_EQ((int)severityFromString("debug"), (int)EventSeverity::Debug);
    ASSERT_EQ((int)severityFromString("info"), (int)EventSeverity::Info);
    ASSERT_EQ((int)severityFromString("warning"), (int)EventSeverity::Warning);
    ASSERT_EQ((int)severityFromString("error"), (int)EventSeverity::Error);
    ASSERT_EQ((int)severityFromString("critical"), (int)EventSeverity::Critical);
    ASSERT_EQ((int)severityFromString(nullptr), (int)EventSeverity::Info);
    ASSERT_EQ((int)severityFromString("unknown"), (int)EventSeverity::Info);
}

// ── Edge Cases ─────────────────────────────────────────────────────────

TEST(EventStore_EmptyData) {
    EventStore store(8);
    store.emit("tag", "");
    auto events = store.all();
    ASSERT_EQ((int)events.size(), 1);
    ASSERT_EQ((int)events[0].data.length(), 0);
}

TEST(EventStore_EmptyTag) {
    EventStore store(8);
    store.emit("", "data");
    auto events = store.all();
    ASSERT_EQ((int)events[0].tag.length(), 0);

    auto byEmpty = store.byTag("");
    ASSERT_EQ((int)byEmpty.size(), 1);
}

TEST(EventStore_LargePayload) {
    EventStore store(4);
    String big = "";
    for (int i = 0; i < 100; i++) big += "data";
    store.emit("big", big);
    auto events = store.all();
    ASSERT_EQ((int)events[0].data.length(), 400);
}

TEST(EventStore_SequentialAfterEviction) {
    EventStore store(2);
    store.emit("a", "0"); // seq 0
    store.emit("b", "1"); // seq 1
    store.emit("c", "2"); // seq 2, evicts 0
    store.emit("d", "3"); // seq 3, evicts 1

    ASSERT_EQ((int)store.nextSeq(), 4);
    auto events = store.all();
    ASSERT_EQ((int)events[0].seq, 2);
    ASSERT_EQ((int)events[1].seq, 3);
}

TEST(EventStore_SinceSeqAfterEviction) {
    EventStore store(3);
    for (int i = 0; i < 10; i++) store.emit("t", String(i));

    // Store has seq 7,8,9. Ask for sinceSeq(0) should return all 3
    auto result = store.sinceSeq(0);
    ASSERT_EQ((int)result.size(), 3);
    ASSERT_EQ((int)result[0].seq, 7);
}

TEST(EventStore_QueryOnEmptyStore) {
    EventStore store(8);
    auto result = store.query("tag", EventSeverity::Debug, 0);
    ASSERT_EQ((int)result.size(), 0);
    ASSERT_EQ((int)store.tags().size(), 0);
    ASSERT_STR_CONTAINS(store.toJSON().c_str(), "[]");
}

TEST(EventStore_IsFullTransition) {
    EventStore store(2);
    ASSERT_FALSE(store.isFull());
    store.emit("a", "1");
    ASSERT_FALSE(store.isFull());
    store.emit("b", "2");
    ASSERT_TRUE(store.isFull());
    store.emit("c", "3"); // still full
    ASSERT_TRUE(store.isFull());
}

// ── Run all ────────────────────────────────────────────────────────────

int main() {
    printf("\n  === MCPEventStore Tests ===\n\n");
    TEST_SUMMARY();
    return _tests_failed;
}
