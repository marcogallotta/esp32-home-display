CXX := g++
CXXFLAGS_COMMON := -Wall -Wextra -Wpedantic -O2 -MMD -MP \
            -Isrc -Ithird_party/PrayerTimes/src -Ithird_party/ArduinoJson/src \
			-Ithird_party/doctest -Itests \
			$(shell pkg-config --cflags sdbus-c++)
CXXFLAGS := -std=c++17 $(CXXFLAGS_COMMON)
CXXFLAGS_20 := -std=c++20 $(CXXFLAGS_COMMON)

LDFLAGS := $(shell pkg-config --libs sdbus-c++) -lcurl

BUILD_DIR := .build_desktop
OBJ_DIR := $(BUILD_DIR)/obj/main
MAIN_TEST_API_OBJ_DIR := $(BUILD_DIR)/obj/main-test-api
TEST_OBJ_DIR := $(BUILD_DIR)/obj/tests

COV_BUILD_DIR := .build_coverage
COV_OBJ_DIR := $(COV_BUILD_DIR)/obj
COV_FLAGS := -O0 -g --coverage

MAIN_TARGET := $(BUILD_DIR)/main
MAIN_TEST_API_TARGET := $(BUILD_DIR)/main-test-api
TEST_TARGET := $(BUILD_DIR)/tests
COV_TEST_TARGET := $(COV_BUILD_DIR)/tests

# --- COMMON SOURCES ---

COMMON_SRC := \
	third_party/PrayerTimes/src/PrayerTimes.cpp \
	src/api/backend_result.cpp \
	src/api/buffer.cpp \
	src/api/buffered_client.cpp \
	src/api/client.cpp \
	src/api/disk_buffer.cpp \
	src/api/dropped_log.cpp \
	src/api/payloads.cpp \
	src/api/request_file_store.cpp \
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

MAIN_TEST_API_SRC := \
	src/main_test_api.cpp \
	$(COMMON_SRC)

TEST_SRC := \
	tests/api_buffer.cpp \
	tests/api_disk_buffer.cpp \
	tests/api_payloads.cpp \
	tests/api_request_file_store.cpp \
	tests/config.cpp \
	tests/main.cpp \
	tests/salah_state.cpp \
	tests/salah_service.cpp \
	tests/sensor_readings.cpp \
	tests/timing.cpp \
	tests/ui_state.cpp \
	$(COMMON_SRC)

# --- OBJECT CONVERSION ---

define make_objs
$(patsubst %.cpp,$(2)/%.o,$(1))
endef

MAIN_OBJ := $(call make_objs,$(MAIN_SRC),$(OBJ_DIR))
MAIN_TEST_API_OBJ := $(call make_objs,$(MAIN_TEST_API_SRC),$(MAIN_TEST_API_OBJ_DIR))
TEST_OBJ := $(call make_objs,$(TEST_SRC),$(TEST_OBJ_DIR))
COV_TEST_OBJ := $(call make_objs,$(TEST_SRC),$(COV_OBJ_DIR))

# --- RULES ---

.PHONY: all clean clean-coverage run run-test-api tests run-tests coverage

all: $(MAIN_TARGET)

$(MAIN_TARGET): $(MAIN_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(MAIN_TEST_API_TARGET): $(MAIN_TEST_API_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

tests: $(TEST_TARGET)
$(TEST_TARGET): $(TEST_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

run: $(MAIN_TARGET)
	./$(MAIN_TARGET)

run-test-api: $(MAIN_TEST_API_TARGET)
	./$(MAIN_TEST_API_TARGET)

run-tests: $(TEST_TARGET)
	./$(TEST_TARGET)

coverage: $(COV_TEST_TARGET)
	./$(COV_TEST_TARGET)
	gcovr -r . --object-directory $(COV_OBJ_DIR) --exclude 'third_party/.*' --exclude 'tests/.*' --html --html-details -o $(COV_BUILD_DIR)/coverage.html

$(COV_TEST_TARGET): $(COV_TEST_OBJ)
	@mkdir -p $(COV_BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS) --coverage

# --- OBJECT BUILD ---

$(OBJ_DIR)/src/ble/desktop.o: src/ble/desktop.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_20) -c $< -o $@

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(MAIN_TEST_API_OBJ_DIR)/src/ble/desktop.o: src/ble/desktop.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_20) -c $< -o $@

$(MAIN_TEST_API_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_OBJ_DIR)/src/ble/desktop.o: src/ble/desktop.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_20) -c $< -o $@

$(TEST_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(COV_OBJ_DIR)/src/ble/desktop.o: src/ble/desktop.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_20) $(COV_FLAGS) -c $< -o $@

$(COV_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(COV_FLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

clean-coverage:
	rm -rf $(COV_BUILD_DIR)

-include $(MAIN_OBJ:.o=.d) $(MAIN_TEST_API_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(COV_TEST_OBJ:.o=.d)
