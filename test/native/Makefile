CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -DMCPD_TEST -pthread
INCLUDES = -I../../src -I../mock_includes -I..

# Targets
TESTS = test_jsonrpc test_mcp_http

.PHONY: all clean test

all: $(TESTS)

test_jsonrpc: ../test_jsonrpc.cpp ../arduino_mock.h ../test_framework.h ../ArduinoJson.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_jsonrpc.cpp

test_mcp_http: test_mcp_http.cpp ../arduino_mock.h ../ArduinoJson.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ test_mcp_http.cpp

test: $(TESTS)
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo " mcpd Native Test Suite"
	@echo "══════════════════════════════════════════"
	@echo ""
	@./test_jsonrpc
	@./test_mcp_http
	@echo "All test suites completed."

clean:
	rm -f $(TESTS)
