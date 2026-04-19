CXX := g++
CXXFLAGS_COMMON := -Wall -Wextra -Wpedantic -O2 -MMD -MP \
            -Isrc -Ilib/PrayerTimes/src -Ilib/ArduinoJson/src -Itests \
			$(shell pkg-config --cflags sdbus-c++)
CXXFLAGS := -std=c++17 $(CXXFLAGS_COMMON)
CXXFLAGS_20 := -std=c++20 $(CXXFLAGS_COMMON)

LDFLAGS := $(shell pkg-config --libs sdbus-c++) -lcurl

ARDUINO_CLI := arduino-cli
FQBN := esp32:esp32:esp32s3:PartitionScheme=min_spiffs
PORT ?= /dev/ttyACM0
ESP32_BUILD_DIR := esp32-build
ESP32_DATA_DIR := esp32-data
SKETCH_DIR := .
ESP32_EXTRA_INCLUDES := -Isrc

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj

MAIN_TARGET := $(BUILD_DIR)/main
TEST_TARGET := $(BUILD_DIR)/tests

# --- COMMON SOURCES ---

COMMON_SRC := \
	lib/PrayerTimes/src/PrayerTimes.cpp \
	src/ble/desktop.cpp \
	src/config.cpp \
	src/config_desktop.cpp \
	src/timing.cpp \
	src/update.cpp \
	src/forecast/network_desktop.cpp \
	src/forecast/openmeteo.cpp \
	src/platform_desktop.cpp \
	src/salah/state.cpp \
	src/salah/service.cpp \
	src/switchbot/protocol.cpp \
	src/switchbot/ble_desktop.cpp \
	src/ui/display.cpp \
	src/ui/state.cpp

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

.PHONY: all clean run tests esp32-compile esp32-upload esp32-monitor

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
	$/$(TEST_TARGET)

$(BUILD_DIR)/spiffs.bin: config.json
	mkdir -p $(ESP32_DATA_DIR)
	cp config.json $(ESP32_DATA_DIR)
	mkdir -p $(BUILD_DIR)
	mkspiffs -c $(ESP32_DATA_DIR) -b 4096 -p 256 -s 0x20000 $(BUILD_DIR)/spiffs.bin

esp32-compile: $(BUILD_DIR)/spiffs.bin
	$(ARDUINO_CLI) compile \
		--fqbn $(FQBN) \
		--library $(CURDIR)/lib/PrayerTimes \
		--library $(CURDIR)/lib/ArduinoJson \
		--build-path $(ESP32_BUILD_DIR) \
		--build-property compiler.cpp.extra_flags="$(ESP32_EXTRA_INCLUDES)" \
		--build-property compiler.c.extra_flags="$(ESP32_EXTRA_INCLUDES)" \
		$(SKETCH_DIR)

esp32-upload: esp32-compile
	$(ARDUINO_CLI) upload \
		-p $(PORT) \
		--fqbn $(FQBN) \
		$(SKETCH_DIR) \
		--input-dir $(ESP32_BUILD_DIR)
	esptool --chip esp32s3 --port $(PORT) write_flash 0x3D0000 $(BUILD_DIR)/spiffs.bin

esp32-monitor:
	$(ARDUINO_CLI) monitor -p $(PORT) -c baudrate=115200

# --- OBJECT BUILD ---

$(OBJ_DIR)/src/ble/desktop.o: src/ble/desktop.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_20) -c $< -o $@

$(OBJ_DIR)/src/switchbot/ble_desktop.o: src/switchbot/ble_desktop.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_20) -c $< -o $@

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(ESP32_BUILD_DIR)
	rm -rf $(ESP32_DATA_DIR)

-include $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
