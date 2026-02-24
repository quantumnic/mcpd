/**
 * mcpd — Audit Log tests
 */

#include "arduino_mock.h"
#include "test_framework.h"
#include "../src/mcpd.h"
#include "../src/mcpd.cpp"

using namespace mcpd;

// ── Construction & Defaults ──────────────────────────────────────────

TEST(Audit_DefaultCapacity) {
    AuditLog log;
    ASSERT_EQ((int)log.capacity(), 64);
    ASSERT_EQ((int)log.count(), 0);
    ASSERT_TRUE(log.isEnabled());
    ASSERT_EQ(log.currentSeq(), (uint32_t)0);
}

TEST(Audit_CustomCapacity) {
    AuditLog log(128);
    ASSERT_EQ((int)log.capacity(), 128);
}

TEST(Audit_ZeroCapacityClampedToOne) {
    AuditLog log(0);
    ASSERT_EQ((int)log.capacity(), 1);
}

// ── Tool Call Logging ────────────────────────────────────────────────

TEST(Audit_LogToolCall) {
    AuditLog log;
    log.logToolCall("admin", "gpio_write", "{\"pin\":2}", true);
    ASSERT_EQ((int)log.count(), 1);
    auto& e = log.entries()[0];
    ASSERT_EQ((int)e.action, (int)AuditAction::ToolCall);
    ASSERT_STR_EQ(e.actor.c_str(), "admin");
    ASSERT_STR_EQ(e.target.c_str(), "gpio_write");
    ASSERT_STR_EQ(e.detail.c_str(), "{\"pin\":2}");
    ASSERT_TRUE(e.success);
    ASSERT_EQ(e.seq, (uint32_t)1);
}

TEST(Audit_LogToolCallDefaultParams) {
    AuditLog log;
    log.logToolCall("user", "read_sensor");
    ASSERT_EQ((int)log.count(), 1);
    ASSERT_STR_EQ(log.entries()[0].detail.c_str(), "");
    ASSERT_TRUE(log.entries()[0].success);
}

TEST(Audit_LogToolCallFailed) {
    AuditLog log;
    log.logToolCall("admin", "gpio_write", "timeout", false);
    ASSERT_FALSE(log.entries()[0].success);
}

// ── Access Denied Logging ────────────────────────────────────────────

TEST(Audit_LogAccessDenied) {
    AuditLog log;
    log.logAccessDenied("guest", "gpio_write", "role not allowed");
    ASSERT_EQ((int)log.count(), 1);
    auto& e = log.entries()[0];
    ASSERT_EQ((int)e.action, (int)AuditAction::AccessDenied);
    ASSERT_STR_EQ(e.actor.c_str(), "guest");
    ASSERT_STR_EQ(e.target.c_str(), "gpio_write");
    ASSERT_FALSE(e.success);
}

TEST(Audit_LogAccessDeniedNoReason) {
    AuditLog log;
    log.logAccessDenied("guest", "secret_tool");
    ASSERT_STR_EQ(log.entries()[0].detail.c_str(), "");
}

// ── Authentication Logging ───────────────────────────────────────────

TEST(Audit_LogAuthSuccess) {
    AuditLog log;
    log.logAuth("key-abc", true, "valid token");
    ASSERT_EQ((int)log.entries()[0].action, (int)AuditAction::AuthSuccess);
    ASSERT_TRUE(log.entries()[0].success);
}

TEST(Audit_LogAuthFailure) {
    AuditLog log;
    log.logAuth("key-bad", false, "invalid token");
    ASSERT_EQ((int)log.entries()[0].action, (int)AuditAction::AuthFailure);
    ASSERT_FALSE(log.entries()[0].success);
}

TEST(Audit_LogAuthNoDetail) {
    AuditLog log;
    log.logAuth("key-x", true);
    ASSERT_STR_EQ(log.entries()[0].detail.c_str(), "");
}

// ── Session Logging ──────────────────────────────────────────────────

TEST(Audit_LogSessionStart) {
    AuditLog log;
    log.logSession("sess-123", true, "from 192.168.1.1");
    auto& e = log.entries()[0];
    ASSERT_EQ((int)e.action, (int)AuditAction::SessionStart);
    ASSERT_STR_EQ(e.actor.c_str(), "sess-123");
    ASSERT_TRUE(e.success);
}

TEST(Audit_LogSessionEnd) {
    AuditLog log;
    log.logSession("sess-123", false);
    ASSERT_EQ((int)log.entries()[0].action, (int)AuditAction::SessionEnd);
}

// ── Role Change Logging ──────────────────────────────────────────────

TEST(Audit_LogRoleChange) {
    AuditLog log;
    log.logRoleChange("admin", "added role operator");
    auto& e = log.entries()[0];
    ASSERT_EQ((int)e.action, (int)AuditAction::RoleChange);
    ASSERT_STR_EQ(e.actor.c_str(), "admin");
}

// ── Custom Event ─────────────────────────────────────────────────────

TEST(Audit_LogCustom) {
    AuditLog log;
    log.logCustom("system", "firmware", "OTA started", true);
    auto& e = log.entries()[0];
    ASSERT_EQ((int)e.action, (int)AuditAction::Custom);
    ASSERT_STR_EQ(e.target.c_str(), "firmware");
}

TEST(Audit_LogCustomFailure) {
    AuditLog log;
    log.logCustom("system", "firmware", "OTA failed checksum", false);
    ASSERT_FALSE(log.entries()[0].success);
}

// ── Generic log() ────────────────────────────────────────────────────

TEST(Audit_GenericLog) {
    AuditLog log;
    log.log(AuditAction::ToolCall, "user", "read_temp", "ok", true);
    ASSERT_EQ((int)log.count(), 1);
    ASSERT_EQ((int)log.entries()[0].action, (int)AuditAction::ToolCall);
}

// ── Sequence Numbers ─────────────────────────────────────────────────

TEST(Audit_SequenceMonotonic) {
    AuditLog log;
    log.logToolCall("a", "t1");
    log.logToolCall("b", "t2");
    log.logToolCall("c", "t3");
    ASSERT_EQ(log.entries()[0].seq, (uint32_t)1);
    ASSERT_EQ(log.entries()[1].seq, (uint32_t)2);
    ASSERT_EQ(log.entries()[2].seq, (uint32_t)3);
    ASSERT_EQ(log.currentSeq(), (uint32_t)3);
}

// ── Ring Buffer Eviction ─────────────────────────────────────────────

TEST(Audit_RingBufferEviction) {
    AuditLog log(3);
    log.logToolCall("a", "t1");
    log.logToolCall("b", "t2");
    log.logToolCall("c", "t3");
    log.logToolCall("d", "t4");
    ASSERT_EQ((int)log.count(), 3);
    // Oldest (t1) should be evicted
    ASSERT_STR_EQ(log.entries()[0].actor.c_str(), "b");
    ASSERT_STR_EQ(log.entries()[2].actor.c_str(), "d");
    ASSERT_EQ(log.currentSeq(), (uint32_t)4);
}

TEST(Audit_RingBufferCapacityOne) {
    AuditLog log(1);
    log.logToolCall("a", "t1");
    log.logToolCall("b", "t2");
    ASSERT_EQ((int)log.count(), 1);
    ASSERT_STR_EQ(log.entries()[0].actor.c_str(), "b");
}

// ── Enable / Disable ─────────────────────────────────────────────────

TEST(Audit_DisabledNoLog) {
    AuditLog log;
    log.setEnabled(false);
    log.logToolCall("a", "t1");
    ASSERT_EQ((int)log.count(), 0);
    ASSERT_EQ(log.currentSeq(), (uint32_t)0);
}

TEST(Audit_ReenableWorks) {
    AuditLog log;
    log.setEnabled(false);
    log.logToolCall("a", "t1");
    log.setEnabled(true);
    log.logToolCall("b", "t2");
    ASSERT_EQ((int)log.count(), 1);
    ASSERT_STR_EQ(log.entries()[0].actor.c_str(), "b");
}

// ── Query: byAction ──────────────────────────────────────────────────

TEST(Audit_ByAction) {
    AuditLog log;
    log.logToolCall("a", "t1");
    log.logAccessDenied("b", "t2");
    log.logToolCall("c", "t3");
    auto calls = log.byAction(AuditAction::ToolCall);
    ASSERT_EQ((int)calls.size(), 2);
    auto denied = log.byAction(AuditAction::AccessDenied);
    ASSERT_EQ((int)denied.size(), 1);
}

TEST(Audit_ByActionEmpty) {
    AuditLog log;
    log.logToolCall("a", "t1");
    auto denied = log.byAction(AuditAction::AccessDenied);
    ASSERT_EQ((int)denied.size(), 0);
}

// ── Query: byActor ───────────────────────────────────────────────────

TEST(Audit_ByActor) {
    AuditLog log;
    log.logToolCall("admin", "t1");
    log.logToolCall("guest", "t2");
    log.logToolCall("admin", "t3");
    auto admin = log.byActor("admin");
    ASSERT_EQ((int)admin.size(), 2);
}

// ── Query: byTarget ──────────────────────────────────────────────────

TEST(Audit_ByTarget) {
    AuditLog log;
    log.logToolCall("a", "gpio_write");
    log.logToolCall("b", "read_temp");
    log.logAccessDenied("c", "gpio_write");
    auto gpio = log.byTarget("gpio_write");
    ASSERT_EQ((int)gpio.size(), 2);
}

// ── Query: since (timestamp) ─────────────────────────────────────────

TEST(Audit_Since) {
    AuditLog log;
    log.logToolCall("a", "t1");
    unsigned long ts = millis();
    log.logToolCall("b", "t2");
    log.logToolCall("c", "t3");
    auto recent = log.since(ts);
    // All should match since millis() returns same value in mock
    ASSERT_GE((int)recent.size(), 1);
}

// ── Query: sinceSeq ──────────────────────────────────────────────────

TEST(Audit_SinceSeq) {
    AuditLog log;
    log.logToolCall("a", "t1");
    log.logToolCall("b", "t2");
    log.logToolCall("c", "t3");
    auto after1 = log.sinceSeq(1);
    ASSERT_EQ((int)after1.size(), 2);
    ASSERT_STR_EQ(after1[0].actor.c_str(), "b");
}

TEST(Audit_SinceSeqZero) {
    AuditLog log;
    log.logToolCall("a", "t1");
    auto all = log.sinceSeq(0);
    ASSERT_EQ((int)all.size(), 1);
}

// ── Query: failures ──────────────────────────────────────────────────

TEST(Audit_Failures) {
    AuditLog log;
    log.logToolCall("a", "t1", "", true);
    log.logAccessDenied("b", "t2");
    log.logToolCall("c", "t3", "err", false);
    auto fails = log.failures();
    ASSERT_EQ((int)fails.size(), 2);
}

// ── Query: last ──────────────────────────────────────────────────────

TEST(Audit_Last) {
    AuditLog log;
    log.logToolCall("a", "t1");
    log.logToolCall("b", "t2");
    log.logToolCall("c", "t3");
    auto l2 = log.last(2);
    ASSERT_EQ((int)l2.size(), 2);
    ASSERT_STR_EQ(l2[0].actor.c_str(), "b");
    ASSERT_STR_EQ(l2[1].actor.c_str(), "c");
}

TEST(Audit_LastMoreThanCount) {
    AuditLog log;
    log.logToolCall("a", "t1");
    auto all = log.last(100);
    ASSERT_EQ((int)all.size(), 1);
}

// ── Stats ────────────────────────────────────────────────────────────

TEST(Audit_CountByAction) {
    AuditLog log;
    log.logToolCall("a", "t1");
    log.logToolCall("b", "t2");
    log.logAccessDenied("c", "t3");
    ASSERT_EQ((int)log.countByAction(AuditAction::ToolCall), 2);
    ASSERT_EQ((int)log.countByAction(AuditAction::AccessDenied), 1);
    ASSERT_EQ((int)log.countByAction(AuditAction::AuthSuccess), 0);
}

TEST(Audit_CountFailures) {
    AuditLog log;
    log.logToolCall("a", "t1");
    log.logAccessDenied("b", "t2");
    ASSERT_EQ((int)log.countFailures(), 1);
}

// ── Serialization ────────────────────────────────────────────────────

TEST(Audit_ToJSON) {
    AuditLog log;
    log.logToolCall("admin", "gpio_write", "{}", true);
    String json = log.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"action\":\"tool_call\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"actor\":\"admin\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"target\":\"gpio_write\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"success\":true");
}

TEST(Audit_ToJSONEmpty) {
    AuditLog log;
    ASSERT_STR_EQ(log.toJSON().c_str(), "[]");
}

TEST(Audit_EntryToJSON) {
    AuditLog log;
    log.logToolCall("admin", "gpio_write");
    String json = log.entries()[0].toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"seq\":1");
    ASSERT_STR_CONTAINS(json.c_str(), "\"action\":\"tool_call\"");
}

TEST(Audit_StatsJSON) {
    AuditLog log(32);
    log.logToolCall("a", "t1");
    log.logAccessDenied("b", "t2");
    log.logAuth("k", true);
    log.logAuth("k", false);
    String json = log.statsJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"total\":4");
    ASSERT_STR_CONTAINS(json.c_str(), "\"tool_calls\":1");
    ASSERT_STR_CONTAINS(json.c_str(), "\"access_denied\":1");
    ASSERT_STR_CONTAINS(json.c_str(), "\"auth_success\":1");
    ASSERT_STR_CONTAINS(json.c_str(), "\"auth_failure\":1");
    ASSERT_STR_CONTAINS(json.c_str(), "\"capacity\":32");
}

// ── Listener ─────────────────────────────────────────────────────────

TEST(Audit_Listener) {
    AuditLog log;
    int callCount = 0;
    AuditAction lastAction = AuditAction::Custom;
    log.setListener([&](const AuditEntry& e) {
        callCount++;
        lastAction = e.action;
    });
    log.logToolCall("a", "t1");
    log.logAccessDenied("b", "t2");
    ASSERT_EQ(callCount, 2);
    ASSERT_EQ((int)lastAction, (int)AuditAction::AccessDenied);
}

TEST(Audit_ClearListener) {
    AuditLog log;
    int callCount = 0;
    log.setListener([&](const AuditEntry&) { callCount++; });
    log.logToolCall("a", "t1");
    log.clearListener();
    log.logToolCall("b", "t2");
    ASSERT_EQ(callCount, 1);
}

// ── Clear / Reset ────────────────────────────────────────────────────

TEST(Audit_Clear) {
    AuditLog log;
    log.logToolCall("a", "t1");
    log.logToolCall("b", "t2");
    log.clear();
    ASSERT_EQ((int)log.count(), 0);
    ASSERT_EQ(log.currentSeq(), (uint32_t)2); // seq preserved
}

TEST(Audit_Reset) {
    AuditLog log;
    log.logToolCall("a", "t1");
    log.reset();
    ASSERT_EQ((int)log.count(), 0);
    ASSERT_EQ(log.currentSeq(), (uint32_t)0); // seq also reset
}

// ── SetCapacity ──────────────────────────────────────────────────────

TEST(Audit_SetCapacityShrink) {
    AuditLog log(10);
    for (int i = 0; i < 10; i++) {
        log.logToolCall("a", "t");
    }
    log.setCapacity(3);
    ASSERT_EQ((int)log.count(), 3);
    ASSERT_EQ((int)log.capacity(), 3);
    // Should keep the 3 most recent (evicts oldest from front)
    ASSERT_EQ(log.entries()[0].seq, (uint32_t)8);
}

TEST(Audit_SetCapacityGrow) {
    AuditLog log(3);
    log.logToolCall("a", "t");
    log.setCapacity(100);
    ASSERT_EQ((int)log.capacity(), 100);
    ASSERT_EQ((int)log.count(), 1);
}

TEST(Audit_SetCapacityZero) {
    AuditLog log(5);
    log.setCapacity(0);
    ASSERT_EQ((int)log.capacity(), 1);
}

// ── Action String Conversion ─────────────────────────────────────────

TEST(Audit_ActionStrings) {
    ASSERT_STR_EQ(auditActionToString(AuditAction::ToolCall), "tool_call");
    ASSERT_STR_EQ(auditActionToString(AuditAction::AccessDenied), "access_denied");
    ASSERT_STR_EQ(auditActionToString(AuditAction::AuthSuccess), "auth_success");
    ASSERT_STR_EQ(auditActionToString(AuditAction::AuthFailure), "auth_failure");
    ASSERT_STR_EQ(auditActionToString(AuditAction::SessionStart), "session_start");
    ASSERT_STR_EQ(auditActionToString(AuditAction::SessionEnd), "session_end");
    ASSERT_STR_EQ(auditActionToString(AuditAction::RoleChange), "role_change");
    ASSERT_STR_EQ(auditActionToString(AuditAction::Custom), "custom");
}

// ── Server Integration ───────────────────────────────────────────────

TEST(Audit_ServerAccessor) {
    Server server("test-server");
    server.auditLog().logToolCall("admin", "gpio_write");
    ASSERT_EQ((int)server.auditLog().count(), 1);
}

TEST(Audit_ServerDefaultEnabled) {
    Server server("test-server");
    ASSERT_TRUE(server.auditLog().isEnabled());
}

// ── Null Safety ──────────────────────────────────────────────────────

TEST(Audit_NullActorSafe) {
    AuditLog log;
    log.logToolCall(nullptr, "t1");
    ASSERT_STR_EQ(log.entries()[0].actor.c_str(), "");
}

TEST(Audit_NullTargetSafe) {
    AuditLog log;
    log.log(AuditAction::Custom, "actor", nullptr, nullptr, true);
    ASSERT_STR_EQ(log.entries()[0].target.c_str(), "");
    ASSERT_STR_EQ(log.entries()[0].detail.c_str(), "");
}

// ── JSON with empty optional fields ──────────────────────────────────

TEST(Audit_JSONOmitsEmptyFields) {
    AuditLog log;
    log.logAuth("key-x", true);
    String json = log.entries()[0].toJSON();
    // target and detail are empty → should not appear in JSON
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"target\"");
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"detail\"");
}

// ── Multiple action types interleaved ────────────────────────────────

TEST(Audit_MixedActions) {
    AuditLog log;
    log.logToolCall("admin", "gpio_write");
    log.logAccessDenied("guest", "gpio_write");
    log.logAuth("key-1", true);
    log.logAuth("key-2", false);
    log.logSession("s1", true);
    log.logSession("s1", false);
    log.logRoleChange("admin", "added viewer");
    log.logCustom("system", "fw", "v1.2");
    ASSERT_EQ((int)log.count(), 8);
    ASSERT_EQ(log.currentSeq(), (uint32_t)8);
}

// ── main ───────────────────────────────────────────────────────────────

int main() {
    printf("\n📋 MCPAuditLog Tests\n");
    printf("  ════════════════════════════════════════\n\n");
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
