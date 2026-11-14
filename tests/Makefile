# Host-side unit tests for the pure logic of the PlantOS firmware.
# Needs only g++ (C++17), make and python3. See README.md.
#
#   make            build + run all tests
#   make run ARGS=calendar     run tests whose name contains "calendar"
#   make golden     regenerate golden/*.csv from the current code (review the diff!)
#   make clean

REPO      := $(abspath ..)
BUILD     := build
CXX       ?= g++
CXXFLAGS  ?= -std=gnu++17 -O1 -g -Wall -Wno-unused-variable -Wno-unused-but-set-variable \
             -Wno-format -Wno-sign-compare -Wno-unused-function
# Tests reach into private members of the real classes instead of editing components/.
CXXFLAGS  += -fno-access-control

# ArduinoJson (header-only) comes from the ESPHome build tree or PlatformIO cache; nothing is downloaded.
# It is copied once into $(BUILD)/vendor so the tests keep working after `task clean`.
ARDUINOJSON_DIR ?= $(firstword $(wildcard \
    $(REPO)/.esphome/build/*/.piolibdeps/*/ArduinoJson/src \
    $(REPO)/.platformio/lib/ArduinoJson/src \
    $(HOME)/.platformio/lib/ArduinoJson/src))
ARDUINOJSON_VENDORED := $(BUILD)/vendor/ArduinoJson

SCHEDULE_JSON := $(BUILD)/schedule.json

# Include order matters: stubs shadow the real ESPHome/ESP-IDF headers; $(BUILD)/inc maps
# "esphome/components/<name>/..." onto the repo's components/ directory via a symlink.
INCLUDES := -Istubs -Isupport -I$(BUILD)/inc -I$(REPO)/components/sensor_filter -isystem $(ARDUINOJSON_VENDORED)
DEFINES  := -DPLANTOS_SCHEDULE_JSON_PATH='"$(abspath $(SCHEDULE_JSON))"' \
            -DPLANTOS_GOLDEN_DIR='"$(abspath golden)"' \
            -DARDUINOJSON_ENABLE_STD_STRING=1 -DARDUINOJSON_USE_LONG_LONG=1

# Real firmware sources compiled unmodified for the host.
COMPONENT_SRCS := \
    calendar_manager/CalendarManager.cpp \
    persistent_state_manager/persistent_state_manager.cpp \
    actuator_safety_gate/ActuatorSafetyGate.cpp \
    alert_service/alert_service.cpp \
    $(patsubst $(REPO)/components/%,%,$(wildcard $(REPO)/components/plantos_controller/*.cpp))

TEST_SRCS := $(wildcard *.cpp) $(wildcard support/*.cpp) stubs/host_stubs.cpp

OBJS := $(patsubst %.cpp,$(BUILD)/obj/components/%.o,$(COMPONENT_SRCS)) \
        $(patsubst %.cpp,$(BUILD)/obj/tests/%.o,$(TEST_SRCS))
DEPS := $(OBJS:.o=.d)
BIN  := $(BUILD)/plantos_tests

.PHONY: all run test golden clean
all: run
test: run

run: $(BIN) $(SCHEDULE_JSON)
	@python3 tools/check_golden.py $(SCHEDULE_JSON) golden/haupt_schedule.csv
	@./$(BIN) $(ARGS)

golden: $(BIN)
	PLANTOS_WRITE_GOLDEN=1 ./$(BIN) golden

$(BIN): $(OBJS)
	@$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(BUILD)/inc/esphome/components:
	@mkdir -p $(BUILD)/inc/esphome
	@ln -sfn $(REPO)/components $@

$(ARDUINOJSON_VENDORED)/ArduinoJson.h:
	@test -n "$(ARDUINOJSON_DIR)" || { echo "ArduinoJson sources not found (expected under .esphome/build/*/.piolibdeps/)."; \
	    echo "Run 'task build' once, or pass ARDUINOJSON_DIR=<path to ArduinoJson/src>."; exit 1; }
	@mkdir -p $(ARDUINOJSON_VENDORED)
	@cp -r $(ARDUINOJSON_DIR)/. $(ARDUINOJSON_VENDORED)/

$(SCHEDULE_JSON): $(REPO)/plantOS.yaml tools/extract_schedule.py
	@mkdir -p $(BUILD)
	@python3 tools/extract_schedule.py $< $@

$(BUILD)/obj/components/%.o: $(REPO)/components/%.cpp | $(BUILD)/inc/esphome/components $(ARDUINOJSON_VENDORED)/ArduinoJson.h
	@mkdir -p $(dir $@)
	@echo "  CXX components/$*.cpp"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -MMD -MP -c $< -o $@

$(BUILD)/obj/tests/%.o: %.cpp | $(BUILD)/inc/esphome/components $(ARDUINOJSON_VENDORED)/ArduinoJson.h $(SCHEDULE_JSON)
	@mkdir -p $(dir $@)
	@echo "  CXX tests/$*.cpp"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD)

-include $(DEPS)
