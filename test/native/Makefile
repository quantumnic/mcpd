CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-unused-parameter -DMCPD_TEST -pthread
INCLUDES = -I../../src -I../mock_includes -I..

# Targets
TESTS = test_jsonrpc test_tools test_mcp_http test_infrastructure test_modules test_auth_platform test_robustness test_session test_content_transport test_advanced test_integration test_output_annotations test_2025_11_25 test_tasks test_validation test_cache test_scheduler test_pipeline test_toolgroups test_eventstore test_statestore test_accesscontrol

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

test_robustness: ../test_robustness.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_robustness.cpp

test_session: ../test_session.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp ../../src/MCPSession.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_session.cpp

test_content_transport: ../test_content_transport.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_content_transport.cpp

test_advanced: ../test_advanced.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_advanced.cpp

test_integration: ../test_integration.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp ../../src/MCPAuth.h ../../src/MCPMetrics.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_integration.cpp

test_output_annotations: ../test_output_annotations.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp ../../src/MCPResource.h ../../src/MCPResourceTemplate.h ../../src/MCPTool.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_output_annotations.cpp

test_2025_11_25: ../test_2025_11_25.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp ../../src/MCPIcon.h ../../src/MCPTool.h ../../src/MCPResource.h ../../src/MCPResourceTemplate.h ../../src/MCPPrompt.h ../../src/MCPContent.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_2025_11_25.cpp

test_tasks: ../test_tasks.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp ../../src/MCPTask.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_tasks.cpp

test_validation: ../test_validation.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp ../../src/MCPValidation.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_validation.cpp

test_cache: ../test_cache.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp ../../src/MCPCache.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_cache.cpp

test_scheduler: ../test_scheduler.cpp ../arduino_mock.h ../test_framework.h ../../src/MCPScheduler.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_scheduler.cpp

test_pipeline: ../test_pipeline.cpp ../arduino_mock.h ../test_framework.h ../../src/MCPPipeline.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_pipeline.cpp

test_toolgroups: ../test_toolgroups.cpp ../arduino_mock.h ../test_framework.h ../../src/mcpd.h ../../src/mcpd.cpp ../../src/MCPToolGroup.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_toolgroups.cpp

test_eventstore: ../test_eventstore.cpp ../arduino_mock.h ../test_framework.h ../../src/MCPEventStore.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_eventstore.cpp

test_statestore: ../test_statestore.cpp ../arduino_mock.h ../test_framework.h ../../src/MCPStateStore.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_statestore.cpp

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
	@./test_robustness
	@./test_session
	@./test_content_transport
	@./test_advanced
	@./test_integration
	@./test_output_annotations
	@./test_2025_11_25
	@./test_tasks
	@./test_validation
	@./test_cache
	@./test_scheduler
	@./test_pipeline
	@./test_toolgroups
	@./test_eventstore
	@./test_statestore
	@echo "All test suites completed."

clean:
	rm -f $(TESTS)

test_accesscontrol: ../test_accesscontrol.cpp ../arduino_mock.h ../test_framework.h ../../src/MCPAccessControl.h ../../src/mcpd.h ../../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ ../test_accesscontrol.cpp
