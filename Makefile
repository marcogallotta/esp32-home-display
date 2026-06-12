CXX := g++
CXXFLAGS_COMMON := -Wall -Wextra -Wpedantic -O2 -MMD -MP \
            -Isrc -Ithird_party/PrayerTimes/src -Ithird_party/ArduinoJson/src \
			-Ithird_party/doctest -Itests \
			$(shell pkg-config --cflags sdbus-c++)
CXXFLAGS := -std=c++17 $(CXXFLAGS_COMMON)
CXXFLAGS_20 := -std=c++20 $(CXXFLAGS_COMMON)

LDFLAGS := $(shell pkg-config --libs sdbus-c++) -lcurl

BUILD_DIR := build/desktop
OBJ_DIR := $(BUILD_DIR)/obj/main
TEST_OBJ_DIR := $(BUILD_DIR)/obj/tests

COV_BUILD_DIR := build/coverage
COV_OBJ_DIR := $(COV_BUILD_DIR)/obj
COV_FLAGS := -O0 -g --coverage

MAIN_TARGET := $(BUILD_DIR)/main
TEST_TARGET := $(BUILD_DIR)/tests
COV_TEST_TARGET := $(COV_BUILD_DIR)/tests

# --- COMMON SOURCES ---

COMMON_SRC := \
	third_party/PrayerTimes/src/PrayerTimes.cpp \
	src/config.cpp \
	src/config_desktop.cpp \
	src/forecast/openmeteo.cpp \
	src/log.cpp \
	src/network_desktop.cpp \
	src/platform_desktop.cpp \
	src/salah/state.cpp \
	src/salah/service.cpp \
	src/sensor_readings.cpp \
	src/time_utils.cpp \
	src/timing.cpp \
	src/update.cpp \
	src/ui/display.cpp \
	src/ui/state.cpp

MAIN_SRC := \
	src/main.cpp \
	$(COMMON_SRC)

TEST_SRC := \
	tests/forecast_openmeteo.cpp \
	tests/config.cpp \
	tests/main.cpp \
	tests/salah_state.cpp \
	tests/salah_service.cpp \
	tests/sensor_readings.cpp \
	tests/network_connect_budget.cpp \
	tests/time_utils.cpp \
	tests/timing.cpp \
	tests/ui_state.cpp \
	$(COMMON_SRC)

# --- OBJECT CONVERSION ---

define make_objs
$(patsubst %.cpp,$(2)/%.o,$(1))
endef

MAIN_OBJ := $(call make_objs,$(MAIN_SRC),$(OBJ_DIR))
TEST_OBJ := $(call make_objs,$(TEST_SRC),$(TEST_OBJ_DIR))
COV_TEST_OBJ := $(call make_objs,$(TEST_SRC),$(COV_OBJ_DIR))

# --- RULES ---

.PHONY: all clean clean-coverage run test tests run-tests coverage

all: $(MAIN_TARGET)

$(MAIN_TARGET): $(MAIN_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

test: run-tests

tests: run-tests

$(TEST_TARGET): $(TEST_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS)

run: $(MAIN_TARGET)
	./$(MAIN_TARGET)

run-tests: $(TEST_TARGET)
	./$(TEST_TARGET)

coverage: $(COV_TEST_TARGET)
	./$(COV_TEST_TARGET)
	gcovr -r . --object-directory $(COV_OBJ_DIR) --exclude 'third_party/.*' --exclude 'tests/.*' --html --html-details -o $(COV_BUILD_DIR)/coverage.html

$(COV_TEST_TARGET): $(COV_TEST_OBJ)
	@mkdir -p $(COV_BUILD_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS) --coverage

# --- OBJECT BUILD ---

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(COV_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(COV_FLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

clean-coverage:
	rm -rf $(COV_BUILD_DIR)

-include $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(COV_TEST_OBJ:.o=.d)
