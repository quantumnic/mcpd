/**
 * mcpd — Access Control (RBAC) tests
 */

#include "arduino_mock.h"
#include "test_framework.h"
#include "../src/mcpd.h"
#include "../src/mcpd.cpp"

using namespace mcpd;

// ── Role Management ──────────────────────────────────────────────────

TEST(AC_AddRole) {
    AccessControl ac;
    ac.addRole("admin");
    ASSERT_TRUE(ac.hasRole("admin"));
    ASSERT_FALSE(ac.hasRole("viewer"));
}

TEST(AC_RemoveRole) {
    AccessControl ac;
    ac.addRole("admin");
    ac.addRole("viewer");
    ac.removeRole("admin");
    ASSERT_FALSE(ac.hasRole("admin"));
    ASSERT_TRUE(ac.hasRole("viewer"));
}

TEST(AC_RolesList) {
    AccessControl ac;
    ac.addRole("admin");
    ac.addRole("viewer");
    ac.addRole("operator");
    auto r = ac.roles();
    ASSERT_EQ((int)r.size(), 3);
}

// ── Key-to-Role Mapping ─────────────────────────────────────────────

TEST(AC_MapKeyToRole) {
    AccessControl ac;
    ac.mapKeyToRole("key-123", "admin");
    ASSERT_STR_EQ(ac.roleForKey("key-123").c_str(), "admin");
    ASSERT_TRUE(ac.hasRole("admin"));
}

TEST(AC_UnmapKey) {
    AccessControl ac;
    ac.mapKeyToRole("key-123", "admin");
    ac.unmapKey("key-123");
    ASSERT_STR_EQ(ac.roleForKey("key-123").c_str(), "");
}

TEST(AC_MultipleKeysOneRole) {
    AccessControl ac;
    ac.mapKeyToRole("key-1", "admin");
    ac.mapKeyToRole("key-2", "admin");
    ASSERT_STR_EQ(ac.roleForKey("key-1").c_str(), "admin");
    ASSERT_STR_EQ(ac.roleForKey("key-2").c_str(), "admin");
}

TEST(AC_KeyOverwrite) {
    AccessControl ac;
    ac.mapKeyToRole("key-1", "admin");
    ac.mapKeyToRole("key-1", "viewer");
    ASSERT_STR_EQ(ac.roleForKey("key-1").c_str(), "viewer");
}

// ── Tool Restrictions ────────────────────────────────────────────────

TEST(AC_RestrictTool) {
    AccessControl ac;
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("gpio_write", roles);
    ASSERT_TRUE(ac.isToolRestricted("gpio_write"));
    ASSERT_FALSE(ac.isToolRestricted("gpio_read"));
}

TEST(AC_UnrestrictTool) {
    AccessControl ac;
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("gpio_write", roles);
    ac.unrestrictTool("gpio_write");
    ASSERT_FALSE(ac.isToolRestricted("gpio_write"));
}

TEST(AC_ToolAllowedRoles) {
    AccessControl ac;
    std::vector<const char*> roles = {"admin", "operator"};
    ac.restrictTool("dangerous", roles);
    auto allowed = ac.toolAllowedRoles("dangerous");
    ASSERT_EQ((int)allowed.size(), 2);
    ASSERT_TRUE(allowed.count(String("admin")) > 0);
    ASSERT_TRUE(allowed.count(String("operator")) > 0);
}

// ── Access Checks ────────────────────────────────────────────────────

TEST(AC_CanAccess_Disabled) {
    AccessControl ac;
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("secret", roles);
    ASSERT_TRUE(ac.canAccess("secret", "any-key"));
}

TEST(AC_CanAccess_UnrestrictedTool) {
    AccessControl ac;
    ac.enable();
    ac.mapKeyToRole("key-1", "viewer");
    ASSERT_TRUE(ac.canAccess("gpio_read", "key-1"));
}

TEST(AC_CanAccess_Allowed) {
    AccessControl ac;
    ac.enable();
    ac.mapKeyToRole("key-admin", "admin");
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("gpio_write", roles);
    ASSERT_TRUE(ac.canAccess("gpio_write", "key-admin"));
}

TEST(AC_CanAccess_Denied) {
    AccessControl ac;
    ac.enable();
    ac.mapKeyToRole("key-viewer", "viewer");
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("gpio_write", roles);
    ASSERT_FALSE(ac.canAccess("gpio_write", "key-viewer"));
}

TEST(AC_CanAccess_UnknownKey_DefaultRole) {
    AccessControl ac;
    ac.enable();
    ac.setDefaultRole("guest");
    std::vector<const char*> r1 = {"admin"};
    ac.restrictTool("gpio_write", r1);
    std::vector<const char*> r2 = {"admin", "guest"};
    ac.restrictTool("gpio_read", r2);
    ASSERT_FALSE(ac.canAccess("gpio_write", "unknown-key"));
    ASSERT_TRUE(ac.canAccess("gpio_read", "unknown-key"));
}

TEST(AC_CanAccess_NullKey) {
    AccessControl ac;
    ac.enable();
    ac.setDefaultRole("guest");
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("admin_tool", roles);
    ASSERT_FALSE(ac.canAccess("admin_tool", nullptr));
}

TEST(AC_CanAccess_EmptyKey) {
    AccessControl ac;
    ac.enable();
    ac.setDefaultRole("guest");
    std::vector<const char*> roles = {"guest"};
    ac.restrictTool("safe_tool", roles);
    ASSERT_TRUE(ac.canAccess("safe_tool", ""));
}

TEST(AC_DefaultRole) {
    AccessControl ac;
    ASSERT_STR_EQ(ac.defaultRole().c_str(), "guest");
    ac.setDefaultRole("anonymous");
    ASSERT_STR_EQ(ac.defaultRole().c_str(), "anonymous");
    ASSERT_TRUE(ac.hasRole("anonymous"));
}

TEST(AC_ToolsForRole) {
    AccessControl ac;
    ac.enable();
    std::vector<const char*> r1 = {"admin"};
    ac.restrictTool("admin_tool", r1);
    std::vector<const char*> r2 = {"admin", "viewer"};
    ac.restrictTool("shared_tool", r2);
    std::vector<String> all = {String("admin_tool"), String("shared_tool"), String("open_tool")};

    auto adminTools = ac.toolsForRole("admin", all);
    ASSERT_EQ((int)adminTools.size(), 3);

    auto viewerTools = ac.toolsForRole("viewer", all);
    ASSERT_EQ((int)viewerTools.size(), 2);

    auto guestTools = ac.toolsForRole("guest", all);
    ASSERT_EQ((int)guestTools.size(), 1);
}

TEST(AC_RemoveRole_CleansUpKeyMappings) {
    AccessControl ac;
    ac.mapKeyToRole("key-1", "admin");
    ac.mapKeyToRole("key-2", "admin");
    ac.mapKeyToRole("key-3", "viewer");
    ac.removeRole("admin");
    ASSERT_STR_EQ(ac.roleForKey("key-1").c_str(), "");
    ASSERT_STR_EQ(ac.roleForKey("key-2").c_str(), "");
    ASSERT_STR_EQ(ac.roleForKey("key-3").c_str(), "viewer");
}

TEST(AC_RemoveRole_CleansUpToolRestrictions) {
    AccessControl ac;
    std::vector<const char*> roles = {"admin", "viewer"};
    ac.restrictTool("tool1", roles);
    ac.removeRole("admin");
    auto allowed = ac.toolAllowedRoles("tool1");
    ASSERT_EQ((int)allowed.size(), 1);
    ASSERT_TRUE(allowed.count(String("viewer")) > 0);
}

TEST(AC_RestrictToolEmptyRoles) {
    AccessControl ac;
    ac.enable();
    std::vector<const char*> empty;
    ac.restrictTool("locked_tool", empty);
    ASSERT_TRUE(ac.isToolRestricted("locked_tool"));
    ac.mapKeyToRole("key-admin", "admin");
    ASSERT_FALSE(ac.canAccess("locked_tool", "key-admin"));
}

TEST(AC_BulkRestrict) {
    AccessControl ac;
    ac.enable();
    ac.mapKeyToRole("admin-key", "admin");
    ac.mapKeyToRole("viewer-key", "viewer");
    std::vector<const char*> tools = {"write1", "write2", "delete1"};
    std::vector<const char*> roles = {"admin"};
    ac.restrictDestructiveTools(tools, roles);
    ASSERT_TRUE(ac.canAccess("write1", "admin-key"));
    ASSERT_FALSE(ac.canAccess("write1", "viewer-key"));
    ASSERT_TRUE(ac.canAccess("write2", "admin-key"));
    ASSERT_FALSE(ac.canAccess("delete1", "viewer-key"));
}

TEST(AC_EnableDisable) {
    AccessControl ac;
    ASSERT_FALSE(ac.isEnabled());
    ac.enable();
    ASSERT_TRUE(ac.isEnabled());
    ac.disable();
    ASSERT_FALSE(ac.isEnabled());
}

TEST(AC_ToJSON) {
    AccessControl ac;
    ac.enable();
    ac.addRole("admin");
    ac.addRole("viewer");
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("gpio_write", roles);
    String json = ac.toJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"enabled\":true");
    ASSERT_STR_CONTAINS(json.c_str(), "\"admin\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"viewer\"");
    ASSERT_STR_CONTAINS(json.c_str(), "gpio_write");
}

TEST(AC_StatsJSON) {
    AccessControl ac;
    ac.enable();
    ac.addRole("admin");
    ac.addRole("viewer");
    ac.mapKeyToRole("k1", "admin");
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("t1", roles);
    String json = ac.statsJSON();
    ASSERT_STR_CONTAINS(json.c_str(), "\"enabled\":true");
    ASSERT_STR_CONTAINS(json.c_str(), "\"roles\":2");
    ASSERT_STR_CONTAINS(json.c_str(), "\"keyMappings\":1");
    ASSERT_STR_CONTAINS(json.c_str(), "\"restrictedTools\":1");
}

TEST(AC_RestrictToolWithStringSet) {
    AccessControl ac;
    ac.enable();
    std::set<String> roles;
    roles.insert(String("admin"));
    roles.insert(String("operator"));
    ac.restrictToolSet("motor_control", roles);
    auto allowed = ac.toolAllowedRoles("motor_control");
    ASSERT_EQ((int)allowed.size(), 2);
}

// ── Server Integration Tests ─────────────────────────────────────────

TEST(AC_Server_AccessControlAccessor) {
    Server server("test");
    auto& ac = server.accessControl();
    ac.addRole("admin");
    ASSERT_TRUE(ac.hasRole("admin"));
}

TEST(AC_Server_RBACBlocksToolCall) {
    Server server("test");
    server.addTool("read_sensor", "Read sensor", "{\"type\":\"object\"}", [](const JsonObject&) {
        return String("42");
    });
    server.addTool("reset_device", "Reset device", "{\"type\":\"object\"}", [](const JsonObject&) {
        return String("reset ok");
    });
    auto& ac = server.accessControl();
    ac.enable();
    ac.mapKeyToRole("admin-key", "admin");
    ac.mapKeyToRole("viewer-key", "viewer");
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("reset_device", roles);
    ASSERT_TRUE(ac.canAccess("reset_device", "admin-key"));
    ASSERT_FALSE(ac.canAccess("reset_device", "viewer-key"));
    ASSERT_TRUE(ac.canAccess("read_sensor", "admin-key"));
    ASSERT_TRUE(ac.canAccess("read_sensor", "viewer-key"));
}

TEST(AC_Server_DisabledRBACAllowsAll) {
    Server server("test");
    auto& ac = server.accessControl();
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("secret_tool", roles);
    ASSERT_TRUE(ac.canAccess("secret_tool", "any-key"));
}

TEST(AC_MultipleRolesPerTool) {
    AccessControl ac;
    ac.enable();
    ac.mapKeyToRole("admin-key", "admin");
    ac.mapKeyToRole("op-key", "operator");
    ac.mapKeyToRole("viewer-key", "viewer");
    std::vector<const char*> roles = {"admin", "operator"};
    ac.restrictTool("motor", roles);
    ASSERT_TRUE(ac.canAccess("motor", "admin-key"));
    ASSERT_TRUE(ac.canAccess("motor", "op-key"));
    ASSERT_FALSE(ac.canAccess("motor", "viewer-key"));
}

TEST(AC_GuestDefaultAccess) {
    AccessControl ac;
    ac.enable();
    ac.setDefaultRole("guest");
    std::vector<const char*> r1 = {"guest", "admin"};
    ac.restrictTool("public_tool", r1);
    std::vector<const char*> r2 = {"admin"};
    ac.restrictTool("private_tool", r2);
    ASSERT_TRUE(ac.canAccess("public_tool"));
    ASSERT_FALSE(ac.canAccess("private_tool"));
}

TEST(AC_NoDefaultRole_DeniesRestricted) {
    AccessControl ac;
    ac.enable();
    ac.setDefaultRole("");
    std::vector<const char*> roles = {"admin"};
    ac.restrictTool("tool1", roles);
    ASSERT_FALSE(ac.canAccess("tool1"));
}

TEST(AC_ToolsForRole_EmptyAllTools) {
    AccessControl ac;
    ac.enable();
    std::vector<String> empty;
    auto result = ac.toolsForRole("admin", empty);
    ASSERT_EQ((int)result.size(), 0);
}

TEST(AC_RestrictSameTool_Overwrites) {
    AccessControl ac;
    std::vector<const char*> r1 = {"admin"};
    ac.restrictTool("tool1", r1);
    std::vector<const char*> r2 = {"viewer", "operator"};
    ac.restrictTool("tool1", r2);
    auto roles = ac.toolAllowedRoles("tool1");
    ASSERT_EQ((int)roles.size(), 2);
    ASSERT_TRUE(roles.count(String("viewer")) > 0);
    ASSERT_TRUE(roles.count(String("operator")) > 0);
    ASSERT_FALSE(roles.count(String("admin")) > 0);
}

TEST(AC_UnrestrictedToolAllowedRoles_Empty) {
    AccessControl ac;
    auto roles = ac.toolAllowedRoles("nonexistent");
    ASSERT_EQ((int)roles.size(), 0);
}

// ── main ───────────────────────────────────────────────────────────────

int main() {
    printf("\n🔐 MCPAccessControl Tests\n");
    printf("  ════════════════════════════════════════\n\n");
    // Tests auto-register and run
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
