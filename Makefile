CXX := g++
CXXFLAGS_COMMON := -Wall -Wextra -Wpedantic -O2 -MMD -MP \
            -Isrc -Ipqueue/src -Ithird_party/PrayerTimes/src -Ithird_party/ArduinoJson/src \
			-Ithird_party/doctest -Itests -Ipqueue/tests \
			$(shell pkg-config --cflags sdbus-c++)
CXXFLAGS := -std=c++17 $(CXXFLAGS_COMMON)
CXXFLAGS_20 := -std=c++20 $(CXXFLAGS_COMMON)

LDFLAGS := $(shell pkg-config --libs sdbus-c++) -lcurl

BUILD_DIR := build/desktop
OBJ_DIR := $(BUILD_DIR)/obj/main
MAIN_TEST_API_OBJ_DIR := $(BUILD_DIR)/obj/main-test-api
TEST_OBJ_DIR := $(BUILD_DIR)/obj/tests
PQUEUE_PROFILING_OBJ_DIR := $(BUILD_DIR)/obj/pqueue-profiling

COV_BUILD_DIR := build/coverage
COV_OBJ_DIR := $(COV_BUILD_DIR)/obj
COV_FLAGS := -O0 -g --coverage

MAIN_TARGET := $(BUILD_DIR)/main
MAIN_TEST_API_TARGET := $(BUILD_DIR)/main-test-api
TEST_TARGET := $(BUILD_DIR)/tests
PQUEUE_PROFILING_TARGET := $(BUILD_DIR)/pqueue-profiling
COV_TEST_TARGET := $(COV_BUILD_DIR)/tests

# --- COMMON SOURCES ---

PQUEUE_SRC := \
	pqueue/src/pqueue/envelope.cpp \
	pqueue/src/pqueue/http/esp32_arduino_transport.cpp \
	pqueue/src/pqueue/http/outbox.cpp \
	pqueue/src/pqueue/http/posix_curl_transport.cpp \
	pqueue/src/pqueue/http/request_envelope.cpp \
	pqueue/src/pqueue/file_store.cpp \
	pqueue/src/pqueue/internal/lock_owner.cpp \
	pqueue/src/pqueue/diagnostics.cpp \
	pqueue/src/pqueue/storage_common.cpp \
	pqueue/src/pqueue/storage_posix.cpp \
	pqueue/src/pqueue/storage_littlefs.cpp \
	pqueue/src/pqueue/outbox.cpp \
	pqueue/src/pqueue/queue.cpp

COMMON_SRC := \
	third_party/PrayerTimes/src/PrayerTimes.cpp \
	src/api/backend_result.cpp \
	src/api/outbox_client.cpp \
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
	$(PQUEUE_SRC) \
	src/salah/state.cpp \
	src/salah/service.cpp \
	src/sensor_readings.cpp \
	src/switchbot/history_backend.cpp \
	src/switchbot/history_protocol.cpp \
	src/switchbot/history_sync.cpp \
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
	tests/api_outbox_client.cpp \
	tests/api_payloads.cpp \
	tests/forecast_openmeteo.cpp \
	tests/api_sensor_write_policy.cpp \
	tests/api_sync.cpp \
	tests/config.cpp \
	tests/main.cpp \
	pqueue/tests/posix/pqueue.cpp \
	pqueue/tests/posix/pqueue_envelope.cpp \
	pqueue/tests/posix/pqueue_file_store.cpp \
	pqueue/tests/posix/pqueue_diagnostics.cpp \
	pqueue/tests/posix/pqueue_http_outbox.cpp \
	pqueue/tests/posix/pqueue_http_request_envelope.cpp \
	pqueue/tests/posix/pqueue_outbox.cpp \
	pqueue/tests/posix/pqueue_queue_edges.cpp \
	tests/salah_state.cpp \
	tests/salah_service.cpp \
	tests/sensor_readings.cpp \
	tests/switchbot_ble.cpp \
	tests/switchbot_history_backend.cpp \
	tests/switchbot_history_protocol.cpp \
	tests/switchbot_protocol.cpp \
	tests/timing.cpp \
	tests/ui_state.cpp \
	tests/xiaomi_ble.cpp \
	tests/xiaomi_protocol.cpp \
	$(COMMON_SRC)

PQUEUE_PROFILING_SRC := \
	pqueue/tools/pqueue_profiling.cpp \
	$(PQUEUE_SRC)

# --- OBJECT CONVERSION ---

define make_objs
$(patsubst %.cpp,$(2)/%.o,$(1))
endef

MAIN_OBJ := $(call make_objs,$(MAIN_SRC),$(OBJ_DIR))
MAIN_TEST_API_OBJ := $(call make_objs,$(MAIN_TEST_API_SRC),$(MAIN_TEST_API_OBJ_DIR))
TEST_OBJ := $(call make_objs,$(TEST_SRC),$(TEST_OBJ_DIR))
PQUEUE_PROFILING_OBJ := $(call make_objs,$(PQUEUE_PROFILING_SRC),$(PQUEUE_PROFILING_OBJ_DIR))
COV_TEST_OBJ := $(call make_objs,$(TEST_SRC),$(COV_OBJ_DIR))

# --- RULES ---

.PHONY: all clean clean-coverage run run-test-api test tests run-tests coverage pqueue-profiling pqueue-tests

all: $(MAIN_TARGET)

$(MAIN_TARGET): $(MAIN_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(MAIN_TEST_API_TARGET): $(MAIN_TEST_API_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

test: run-tests

tests: run-tests

$(TEST_TARGET): $(TEST_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(PQUEUE_PROFILING_TARGET): $(PQUEUE_PROFILING_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

run: $(MAIN_TARGET)
	./$(MAIN_TARGET)

run-test-api: $(MAIN_TEST_API_TARGET)
	./$(MAIN_TEST_API_TARGET)

run-tests: $(TEST_TARGET)
	./$(TEST_TARGET)

pqueue-profiling:
	@test -f pqueue/tools/pqueue_profiling.cpp || { echo "missing pqueue/tools/pqueue_profiling.cpp"; exit 1; }
	$(MAKE) $(PQUEUE_PROFILING_TARGET)
	./$(PQUEUE_PROFILING_TARGET) all

pqueue-tests:
	$(MAKE) -C pqueue test

coverage: $(COV_TEST_TARGET)
	./$(COV_TEST_TARGET)
	gcovr -r . --object-directory $(COV_OBJ_DIR) --exclude 'third_party/.*' --exclude 'tests/.*' --exclude 'pqueue/tests/.*' --html --html-details -o $(COV_BUILD_DIR)/coverage.html

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

$(PQUEUE_PROFILING_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(COV_OBJ_DIR)/src/ble/desktop.o: src/ble/desktop.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_20) $(COV_FLAGS) -c $< -o $@

$(COV_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(COV_FLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) build/pqueue-spools build/spool

clean-coverage:
	rm -rf $(COV_BUILD_DIR)

-include $(MAIN_OBJ:.o=.d) $(MAIN_TEST_API_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(PQUEUE_PROFILING_OBJ:.o=.d) $(COV_TEST_OBJ:.o=.d)
