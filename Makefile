CXX := g++
CXXFLAGS_COMMON := -Wall -Wextra -Wpedantic -O2 -MMD -MP \
            -Isrc -Ilib/PrayerTimes/src -Ilib/ArduinoJson/src -Itests \
			$(shell pkg-config --cflags sdbus-c++)
CXXFLAGS := -std=c++17 $(CXXFLAGS_COMMON)
CXXFLAGS_20 := -std=c++20 $(CXXFLAGS_COMMON)

LDFLAGS := $(shell pkg-config --libs sdbus-c++) -lcurl

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj

MAIN_TARGET := $(BUILD_DIR)/main
TEST_TARGET := $(BUILD_DIR)/tests

# --- COMMON SOURCES ---

COMMON_SRC := \
	lib/PrayerTimes/src/PrayerTimes.cpp \
	src/api/backend_result.cpp \
	src/api/buffer.cpp \
	src/api/buffered_client.cpp \
	src/api/client.cpp \
	src/api/dropped_log.cpp \
	src/api/payloads.cpp \
	src/api/state.cpp \
	src/api_sync.cpp \
	src/ble/desktop.cpp \
	src/config.cpp \
	src/config_desktop.cpp \
	src/forecast/openmeteo.cpp \
	src/log.cpp \
	src/network_desktop.cpp \
	src/platform_desktop.cpp \
	src/salah/state.cpp \
	src/salah/service.cpp \
	src/sensor_readings.cpp \
	src/switchbot/protocol.cpp \
	src/switchbot/ble.cpp \
	src/timing.cpp \
	src/update.cpp \
	src/ui/display.cpp \
	src/ui/state.cpp \
	src/xiaomi/protocol.cpp \
	src/xiaomi/ble.cpp

MAIN_SRC := \
	src/main.cpp \
	$(COMMON_SRC)

TEST_SRC := \
	tests/config.cpp \
	tests/main.cpp \
	tests/salah_state.cpp \
	tests/salah_service.cpp \
	tests/timing.cpp \
	tests/ui_state.cpp \
	$(COMMON_SRC)

# --- OBJECT CONVERSION ---

define make_objs
$(patsubst %.cpp,$(OBJ_DIR)/%.o,$(1))
endef

MAIN_OBJ := $(call make_objs,$(MAIN_SRC))
TEST_OBJ := $(call make_objs,$(TEST_SRC))

# --- RULES ---

.PHONY: all clean run tests run-tests

all: $(MAIN_TARGET)

$(MAIN_TARGET): $(MAIN_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

tests: $(TEST_TARGET)
$(TEST_TARGET): $(TEST_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

run: $(MAIN_TARGET)
	./$(MAIN_TARGET)

run-tests: $(TEST_TARGET)
	./$(TEST_TARGET)

# --- OBJECT BUILD ---

$(OBJ_DIR)/src/ble/desktop.o: src/ble/desktop.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_20) -c $< -o $@

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
