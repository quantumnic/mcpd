CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -DMCPD_TEST -pthread
INCLUDES = -I../../src -I../mock_includes -I..

# Targets
TESTS = test_jsonrpc test_tools test_mcp_http test_infrastructure test_modules test_auth_platform

.PHONY: all clean test

all: $(TESTS)

test_jsonrpc: ../test_jsonrpc.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_jsonrpc.cpp

test_tools: ../test_tools.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_tools.cpp

test_mcp_http: test_mcp_http.cpp ../arduino_mock.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ test_mcp_http.cpp

test_infrastructure: ../test_infrastructure.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_infrastructure.cpp

test_modules: ../test_modules.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_modules.cpp

test_auth_platform: ../test_auth_platform.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp ../../src/MCPDiagnostics.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_auth_platform.cpp

test: $(TESTS)
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo " mcpd Native Test Suite"
	@echo "══════════════════════════════════════════"
	@echo ""
	@./test_jsonrpc
	@./test_tools
	@./test_mcp_http
	@./test_infrastructure
	@./test_modules
	@./test_auth_platform
	@echo "All test suites completed."

clean:
	rm -f $(TESTS)
