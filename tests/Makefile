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
CXXFLAGS  ?= -std=gnu++17 -O0 -g1 -Wall -Wno-unused-variable -Wno-unused-but-set-variable \
             -Wno-format -Wno-sign-compare -Wno-unused-function
# Tests reach into private members of the real classes instead of editing components/.
CXXFLAGS  += -fno-access-control

# Calendar phase tables, generated from plantOS.yaml by the same Python code the firmware
# build uses (components/calendar_manager/phase_tables.py).
CALENDAR_TABLES := $(BUILD)/generated/calendar_tables.h
CALENDAR_PY     := $(REPO)/components/calendar_manager/phase_tables.py tools/gen_calendar_tables.py tools/calendar_yaml.py

# Include order matters: stubs shadow the real ESPHome/ESP-IDF headers; $(BUILD)/inc maps
# "esphome/components/<name>/..." onto the repo's components/ directory via a symlink.
INCLUDES := -Istubs -Isupport -I$(BUILD)/inc -I$(BUILD)/generated -I$(REPO)/components/sensor_filter
DEFINES  := -DPLANTOS_GOLDEN_DIR='"$(abspath golden)"'

# Real firmware sources compiled unmodified for the host.
COMPONENT_SRCS := \
    calendar_manager/CalendarManager.cpp \
    persistent_state_manager/persistent_state_manager.cpp \
    actuator_safety_gate/ActuatorSafetyGate.cpp \
    alert_service/alert_service.cpp \
    ezo_ph_uart/ezo_ph_uart.cpp \
    $(patsubst $(REPO)/components/%,%,$(wildcard $(REPO)/components/plantos_controller/*.cpp))

TEST_SRCS := $(wildcard *.cpp) $(wildcard support/*.cpp) stubs/host_stubs.cpp

OBJS := $(patsubst %.cpp,$(BUILD)/obj/components/%.o,$(COMPONENT_SRCS)) \
        $(patsubst %.cpp,$(BUILD)/obj/tests/%.o,$(TEST_SRCS))
DEPS := $(OBJS:.o=.d)
BIN  := $(BUILD)/plantos_tests

.PHONY: all run test golden pytest clean
all: run
test: run

run: $(BIN) pytest
	@python3 tools/check_golden.py $(REPO)/plantOS.yaml golden/haupt_schedule.csv
	@./$(BIN) $(ARGS)

# Python side of the calendar: validation of bad configs, rendering, golden expansion.
pytest:
	@python3 tools/test_phase_tables.py

golden: $(BIN)
	PLANTOS_WRITE_GOLDEN=1 ./$(BIN) golden

$(BIN): $(OBJS)
	@$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(BUILD)/inc/esphome/components:
	@mkdir -p $(BUILD)/inc/esphome
	@ln -sfn $(REPO)/components $@

$(CALENDAR_TABLES): $(REPO)/plantOS.yaml $(CALENDAR_PY)
	@mkdir -p $(dir $@)
	@python3 tools/gen_calendar_tables.py $< $@

$(BUILD)/obj/components/%.o: $(REPO)/components/%.cpp | $(BUILD)/inc/esphome/components
	@mkdir -p $(dir $@)
	@echo "  CXX components/$*.cpp"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -MMD -MP -c $< -o $@

$(BUILD)/obj/tests/%.o: %.cpp | $(BUILD)/inc/esphome/components $(CALENDAR_TABLES)
	@mkdir -p $(dir $@)
	@echo "  CXX tests/$*.cpp"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD)

-include $(DEPS)
