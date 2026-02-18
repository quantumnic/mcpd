# mcpd Host-Side Unit Tests
#
# Requirements: g++ with C++17, ArduinoJson header (installed via PlatformIO or manually)
#
# Usage:
#   make            # build and run all tests
#   make clean      # remove build artifacts

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-unused-parameter -I. -Imock_includes -I../src -DMCPD_TEST
LDFLAGS =

# Try to find ArduinoJson. PlatformIO installs it in ~/.platformio
ARDUINOJSON_PATH := $(shell find $(HOME)/.platformio/packages -name "ArduinoJson" -type d 2>/dev/null | head -1)
ifdef ARDUINOJSON_PATH
    CXXFLAGS += -I$(ARDUINOJSON_PATH)/src
else
    # Try Homebrew or system paths
    ARDUINOJSON_PATH := $(shell find /usr/local/include /opt/homebrew/include -name "ArduinoJson.h" -type f 2>/dev/null | head -1 | xargs dirname 2>/dev/null)
    ifdef ARDUINOJSON_PATH
        CXXFLAGS += -I$(ARDUINOJSON_PATH)
    else
        $(warning ArduinoJson not found. Install via: pio pkg install -g -l "bblanchon/ArduinoJson")
    endif
endif

TESTS = test_jsonrpc test_tools

.PHONY: all clean test

all: test

test: $(TESTS)
	@echo ""
	@for t in $(TESTS); do echo "Running $$t..."; ./$$t; done

test_jsonrpc: test_jsonrpc.cpp arduino_mock.h test_framework.h ../src/mcpd.h ../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test_tools: test_tools.cpp arduino_mock.h test_framework.h ../src/mcpd.h ../src/mcpd.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TESTS)
