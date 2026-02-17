// ArduinoJson wrapper for native mock builds
#pragma once
#include "../arduino_mock.h"

#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_STD_STRING 1
#define ARDUINOJSON_ENABLE_STD_STREAM 0

#include "../ArduinoJson.h"
