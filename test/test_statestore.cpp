/**
 * Tests for MCPStateStore — key-value state with change tracking
 */

#include "test_framework.h"
#include "MCPStateStore.h"

using namespace mcpd;

// ── Basic Operations ───────────────────────────────────────────────────

TEST(StateStore_EmptyOnCreation) {
    StateStore store;
    ASSERT_EQ((int)store.count(), 0);
    ASSERT_FALSE(store.isDirty());
    ASSERT_FALSE(store.has("anything"));
    ASSERT_EQ(store.get("anything"), String(""));
}

TEST(StateStore_SetAndGet) {
    StateStore store;
    bool changed = store.set("key1", "value1");
    ASSERT_TRUE(changed);
    ASSERT_EQ(store.get("key1"), String("value1"));
    ASSERT_EQ((int)store.count(), 1);
}

TEST(StateStore_SetOverwrite) {
    StateStore store;
    store.set("k", "v1");
    ASSERT_EQ(store.get("k"), String("v1"));

    bool changed = store.set("k", "v2");
    ASSERT_TRUE(changed);
    ASSERT_EQ(store.get("k"), String("v2"));
    ASSERT_EQ((int)store.count(), 1); // still 1
}

TEST(StateStore_SetSameValueNoChange) {
    StateStore store;
    store.set("k", "v");
    store.clearDirty();

    bool changed = store.set("k", "v");
    ASSERT_FALSE(changed);
    ASSERT_FALSE(store.isDirty());
}

TEST(StateStore_Has) {
    StateStore store;
    ASSERT_FALSE(store.has("x"));
    store.set("x", "1");
    ASSERT_TRUE(store.has("x"));
}

TEST(StateStore_Remove) {
    StateStore store;
    store.set("a", "1");
    store.set("b", "2");
    ASSERT_EQ((int)store.count(), 2);

    bool removed = store.remove("a");
    ASSERT_TRUE(removed);
    ASSERT_FALSE(store.has("a"));
    ASSERT_EQ((int)store.count(), 1);

    removed = store.remove("nonexistent");
    ASSERT_FALSE(removed);
}

TEST(StateStore_Clear) {
    StateStore store;
    store.set("a", "1");
    store.set("b", "2");
    store.set("c", "3");
    ASSERT_EQ((int)store.count(), 3);

    store.clear();
    ASSERT_EQ((int)store.count(), 0);
    ASSERT_FALSE(store.has("a"));
}

TEST(StateStore_EmptyKey) {
    StateStore store;
    ASSERT_FALSE(store.set("", "value"));
    ASSERT_FALSE(store.set(nullptr, "value"));
    ASSERT_EQ(store.get(nullptr), String(""));
    ASSERT_EQ((int)store.count(), 0);
}

// ── Namespaced Keys ────────────────────────────────────────────────────

TEST(StateStore_KeysNoPrefix) {
    StateStore store;
    store.set("a", "1");
    store.set("b", "2");
    store.set("c", "3");
    auto k = store.keys();
    ASSERT_EQ((int)k.size(), 3);
}

TEST(StateStore_KeysWithPrefix) {
    StateStore store;
    store.set("sensor.temp", "22.5");
    store.set("sensor.humidity", "65");
    store.set("config.name", "mydevice");
    store.set("config.port", "80");
    store.set("wifi.rssi", "-67");

    auto sensorKeys = store.keys("sensor.");
    ASSERT_EQ((int)sensorKeys.size(), 2);

    auto configKeys = store.keys("config.");
    ASSERT_EQ((int)configKeys.size(), 2);

    auto wifiKeys = store.keys("wifi.");
    ASSERT_EQ((int)wifiKeys.size(), 1);

    auto noneKeys = store.keys("nonexistent.");
    ASSERT_EQ((int)noneKeys.size(), 0);
}

// ── Dirty Tracking ─────────────────────────────────────────────────────

TEST(StateStore_DirtyAfterSet) {
    StateStore store;
    ASSERT_FALSE(store.isDirty());
    store.set("k", "v");
    ASSERT_TRUE(store.isDirty());
}

TEST(StateStore_ClearDirty) {
    StateStore store;
    store.set("a", "1");
    store.set("b", "2");
    ASSERT_TRUE(store.isDirty());

    store.clearDirty();
    ASSERT_FALSE(store.isDirty());
}

TEST(StateStore_DirtyKeys) {
    StateStore store;
    store.set("a", "1");
    store.set("b", "2");
    store.set("c", "3");
    store.clearDirty();

    store.set("b", "new");
    auto dk = store.dirtyKeys();
    ASSERT_EQ((int)dk.size(), 1);
    ASSERT_EQ(dk[0], String("b"));
}

// ── Change Listeners ───────────────────────────────────────────────────

TEST(StateStore_OnChangeNew) {
    StateStore store;
    std::string capturedKey, capturedOld, capturedNew;

    store.onChange([&](const char* key, const char* oldVal, const char* newVal) {
        capturedKey = key;
        capturedOld = oldVal;
        capturedNew = newVal;
    });

    store.set("temp", "22.5");
    ASSERT_EQ(capturedKey, std::string("temp"));
    ASSERT_EQ(capturedOld, std::string(""));
    ASSERT_EQ(capturedNew, std::string("22.5"));
}

TEST(StateStore_OnChangeUpdate) {
    StateStore store;
    std::string capturedOld, capturedNew;
    store.set("k", "old");

    store.onChange([&](const char* key, const char* oldVal, const char* newVal) {
        capturedOld = oldVal;
        capturedNew = newVal;
    });

    store.set("k", "new");
    ASSERT_EQ(capturedOld, std::string("old"));
    ASSERT_EQ(capturedNew, std::string("new"));
}

TEST(StateStore_OnChangeDelete) {
    StateStore store;
    std::string capturedOld, capturedNew;
    store.set("k", "val");

    store.onChange([&](const char* key, const char* oldVal, const char* newVal) {
        capturedOld = oldVal;
        capturedNew = newVal;
    });

    store.remove("k");
    ASSERT_EQ(capturedOld, std::string("val"));
    ASSERT_EQ(capturedNew, std::string(""));
}

TEST(StateStore_MultipleListeners) {
    StateStore store;
    int count1 = 0, count2 = 0;

    store.onChange([&](const char*, const char*, const char*) { count1++; });
    store.onChange([&](const char*, const char*, const char*) { count2++; });

    store.set("k", "v");
    ASSERT_EQ(count1, 1);
    ASSERT_EQ(count2, 1);
}

TEST(StateStore_RemoveListener) {
    StateStore store;
    int count = 0;
    size_t id = store.onChange([&](const char*, const char*, const char*) { count++; });

    store.set("a", "1");
    ASSERT_EQ(count, 1);

    store.removeListener(id);
    store.set("b", "2");
    ASSERT_EQ(count, 1); // no more notifications
}

TEST(StateStore_NoChangeNoNotify) {
    StateStore store;
    store.set("k", "v");

    int count = 0;
    store.onChange([&](const char*, const char*, const char*) { count++; });

    store.set("k", "v"); // same value
    ASSERT_EQ(count, 0);
}

// ── Bounded Size / Eviction ────────────────────────────────────────────

TEST(StateStore_MaxEntries) {
    StateStore store(3);
    ASSERT_EQ((int)store.maxEntries(), 3);

    store.set("a", "1");
    store.set("b", "2");
    store.set("c", "3");
    ASSERT_EQ((int)store.count(), 3);

    store.set("d", "4"); // should evict oldest-accessed
    ASSERT_EQ((int)store.count(), 3);
    ASSERT_TRUE(store.has("d"));
}

TEST(StateStore_EvictsOldestAccessed) {
    StateStore store(3);
    store.set("a", "1"); // t=0
    store.set("b", "2"); // t=0
    store.set("c", "3"); // t=0

    // Access "a" to make it recent
    store.get("a");

    store.set("d", "4"); // should evict "b" (oldest accessed)
    ASSERT_TRUE(store.has("a"));
    ASSERT_TRUE(store.has("d"));
    ASSERT_EQ((int)store.count(), 3);
}

TEST(StateStore_UnlimitedByDefault) {
    StateStore store; // maxEntries=0
    ASSERT_EQ((int)store.maxEntries(), 0);

    for (int i = 0; i < 100; i++) {
        store.set(String(String("k") + String(i)).c_str(), "v");
    }
    ASSERT_EQ((int)store.count(), 100);
}

// ── TTL ────────────────────────────────────────────────────────────────

TEST(StateStore_TTLBasic) {
    StateStore store;
    // Set with TTL — in mock env millis() is always 0 so age=0, never expires
    // This tests that TTL metadata is stored and non-TTL behavior works
    store.set("permanent", "value", 0);
    store.set("temporary", "value", 5000);
    ASSERT_TRUE(store.has("permanent"));
    ASSERT_TRUE(store.has("temporary")); // not expired yet (age=0 ≤ 5000)
    ASSERT_EQ(store.get("temporary"), String("value"));
}

TEST(StateStore_TTLZeroNoExpiry) {
    StateStore store;
    store.set("permanent", "value", 0);
    ASSERT_TRUE(store.has("permanent"));
    ASSERT_EQ(store.get("permanent"), String("value"));
}

TEST(StateStore_PurgeExpired) {
    StateStore store;
    // All entries at millis()=0 with ttl=0 → never expire
    store.set("a", "1");
    store.set("b", "2", 0);
    size_t purged = store.purgeExpired();
    ASSERT_EQ((int)purged, 0);
    ASSERT_EQ((int)store.count(), 2);
}

// ── Transactions ───────────────────────────────────────────────────────

TEST(StateStore_TransactionCommit) {
    StateStore store;
    store.set("x", "old");

    int notifications = 0;
    store.onChange([&](const char*, const char*, const char*) { notifications++; });

    store.begin();
    ASSERT_TRUE(store.inTransaction());

    store.set("x", "new");
    store.set("y", "added");

    // Not yet applied
    ASSERT_EQ(store.get("x"), String("old"));
    ASSERT_FALSE(store.has("y"));
    ASSERT_EQ(notifications, 0);

    size_t applied = store.commit();
    ASSERT_FALSE(store.inTransaction());
    ASSERT_EQ((int)applied, 2);
    ASSERT_EQ(store.get("x"), String("new"));
    ASSERT_EQ(store.get("y"), String("added"));
    ASSERT_EQ(notifications, 2);
}

TEST(StateStore_TransactionRollback) {
    StateStore store;
    store.set("x", "original");

    store.begin();
    store.set("x", "changed");
    store.set("y", "new");
    store.rollback();

    ASSERT_FALSE(store.inTransaction());
    ASSERT_EQ(store.get("x"), String("original"));
    ASSERT_FALSE(store.has("y"));
}

TEST(StateStore_CommitWithoutBegin) {
    StateStore store;
    size_t applied = store.commit();
    ASSERT_EQ((int)applied, 0);
}

// ── JSON Serialization ─────────────────────────────────────────────────

TEST(StateStore_ToJSON) {
    StateStore store;
    store.set("a", "1");
    store.set("b", "hello");
    String json = store.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"a\":\"1\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"b\":\"hello\"");
}

TEST(StateStore_ToJSONEmpty) {
    StateStore store;
    ASSERT_EQ(store.toJSON(), String("{}"));
}

TEST(StateStore_ToJSONEscaping) {
    StateStore store;
    store.set("msg", "hello \"world\"");
    String json = store.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\\\"world\\\"");
}

TEST(StateStore_FromJSON) {
    StateStore store;
    size_t imported = store.fromJSON("{\"a\":\"1\",\"b\":\"two\",\"c\":\"three\"}");
    ASSERT_EQ((int)imported, 3);
    ASSERT_EQ(store.get("a"), String("1"));
    ASSERT_EQ(store.get("b"), String("two"));
    ASSERT_EQ(store.get("c"), String("three"));
}

TEST(StateStore_FromJSONMerges) {
    StateStore store;
    store.set("existing", "keep");
    store.fromJSON("{\"new\":\"val\"}");
    ASSERT_TRUE(store.has("existing"));
    ASSERT_TRUE(store.has("new"));
}

TEST(StateStore_FromJSONNull) {
    StateStore store;
    size_t imported = store.fromJSON(nullptr);
    ASSERT_EQ((int)imported, 0);
}

TEST(StateStore_FromJSONInvalid) {
    StateStore store;
    size_t imported = store.fromJSON("not json");
    ASSERT_EQ((int)imported, 0);
}

TEST(StateStore_FromJSONEscaped) {
    StateStore store;
    store.fromJSON("{\"msg\":\"hello\\nworld\"}");
    String val = store.get("msg");
    ASSERT_TRUE(val.indexOf('\n') >= 0);
}

TEST(StateStore_RoundTrip) {
    StateStore store1;
    store1.set("sensor.temp", "22.5");
    store1.set("sensor.humidity", "65");
    store1.set("config.name", "myesp");
    String json = store1.toJSON();

    StateStore store2;
    size_t imported = store2.fromJSON(json.c_str());
    ASSERT_EQ((int)imported, 3);
    ASSERT_EQ(store2.get("sensor.temp"), String("22.5"));
    ASSERT_EQ(store2.get("sensor.humidity"), String("65"));
    ASSERT_EQ(store2.get("config.name"), String("myesp"));
}

// ── Detailed JSON ──────────────────────────────────────────────────────

TEST(StateStore_DetailedJSON) {
    StateStore store;
    store.set("k", "v");
    String json = store.toDetailedJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"value\":\"v\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"ttl\":");
    ASSERT_STR_CONTAINS(json.c_str(), "\"dirty\":");
    ASSERT_STR_CONTAINS(json.c_str(), "\"age\":");
}

// ── Stats JSON ─────────────────────────────────────────────────────────

TEST(StateStore_StatsJSON) {
    StateStore store(64);
    store.set("a", "1");
    store.set("b", "2");
    String stats = store.statsJSON();
    ASSERT_STR_CONTAINS(stats.c_str(), "\"count\":2");
    ASSERT_STR_CONTAINS(stats.c_str(), "\"maxEntries\":64");
    ASSERT_STR_CONTAINS(stats.c_str(), "\"dirty\":2");
    ASSERT_STR_CONTAINS(stats.c_str(), "\"listeners\":0");
    ASSERT_STR_CONTAINS(stats.c_str(), "\"inTransaction\":false");
}

// ── Edge Cases ─────────────────────────────────────────────────────────

TEST(StateStore_SpecialCharacters) {
    StateStore store;
    store.set("path/to/key", "value with spaces");
    ASSERT_EQ(store.get("path/to/key"), String("value with spaces"));
}

TEST(StateStore_EmptyValue) {
    StateStore store;
    store.set("k", "");
    ASSERT_TRUE(store.has("k"));
    ASSERT_EQ(store.get("k"), String(""));
}

TEST(StateStore_LongValues) {
    StateStore store;
    String longVal;
    for (int i = 0; i < 100; i++) longVal += "abcdefghij";
    store.set("big", longVal.c_str());
    ASSERT_EQ(store.get("big"), longVal);
}

TEST(StateStore_ClearNotifiesListeners) {
    StateStore store;
    store.set("a", "1");
    store.set("b", "2");

    int count = 0;
    store.onChange([&](const char*, const char*, const char*) { count++; });
    store.clear();
    ASSERT_EQ(count, 2);
}

TEST(StateStore_EvictionNotifiesListener) {
    StateStore store(2);
    int evictions = 0;
    std::string evictedKey;

    store.onChange([&](const char* key, const char* oldVal, const char* newVal) {
        if (std::string(newVal).empty()) {
            evictions++;
            evictedKey = key;
        }
    });

    store.set("a", "1");
    store.set("b", "2");
    store.set("c", "3"); // should evict one

    ASSERT_EQ(evictions, 1);
}

TEST(StateStore_RemoveNonexistentListener) {
    StateStore store;
    bool removed = store.removeListener(999);
    ASSERT_FALSE(removed);
}

// ── main ───────────────────────────────────────────────────────────────

int main() {
    printf("\n🔑 MCPStateStore Tests\n");
    printf("  ════════════════════════════════════════\n\n");
    // Tests auto-register and run
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
