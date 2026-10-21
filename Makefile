# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alejandro Bustos

PREFIX ?= /usr/local
LV2DIR ?= $(PREFIX)/lib/lv2
DESTDIR ?=
OPT ?= -O2
PYTHON ?= python3
PKG_CONFIG ?= pkg-config
SAN ?= 0
BUILD_DIR ?= build
ifeq ($(SAN),1)
override BUILD_DIR := $(BUILD_DIR)/san
SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g
endif
ifeq ($(SAN),thread)
override BUILD_DIR := $(BUILD_DIR)/tsan
SAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer -g
endif

URI := https://github.com/ABustos2606/chamber\#chamber
BUNDLE := $(BUILD_DIR)/chamber.lv2
VENDOR := third_party/eurorack
SOURCES := src/lv2_entry.cpp src/chamber.cpp src/controls.cpp
VENDOR_SOURCES := $(addprefix $(VENDOR)/,elements/dsp/exciter.cc elements/dsp/multistage_envelope.cc \
	elements/dsp/ominous_voice.cc elements/dsp/part.cc elements/dsp/resonator.cc elements/dsp/string.cc \
	elements/dsp/tube.cc elements/dsp/voice.cc elements/resources.cc stmlib/dsp/units.cc stmlib/utils/random.cc)
OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/obj/%.o,$(SOURCES))
VENDOR_OBJECTS := $(patsubst $(VENDOR)/%.cc,$(BUILD_DIR)/obj/vendor/%.o,$(VENDOR_SOURCES))
METADATA := manifest.ttl chamber.ttl
TTL_TARGETS := $(addprefix $(BUNDLE)/,$(METADATA))
LV2_CFLAGS = $(shell $(PKG_CONFIG) --cflags lv2)
# TEST selects stmlib's portable C++ instead of Cortex-M4 assembly, as
# upstream's own desktop test build does. -fno-fast-math comes after OPT: the
# non-finite guards need IEEE arithmetic even if a host build passes
# -ffast-math in OPT. initial-exec puts stmlib::Random's thread_local state
# (third_party/eurorack/README.md) in the static TLS block set up at dlopen,
# so the first run() on a new host thread does not allocate it.
BASE_FLAGS = $(CPPFLAGS) $(CXXFLAGS) $(OPT) -std=c++17 -fPIC -fvisibility=hidden -DTEST \
	-ftls-model=initial-exec -fno-fast-math -fno-math-errno -fno-trapping-math $(SAN_FLAGS)
COMPILE_FLAGS = $(BASE_FLAGS) $(LV2_CFLAGS) -isystem $(VENDOR) -Wall -Wextra
# Vendored code is kept as upstream wrote it; its warnings are not ours.
VENDOR_FLAGS = $(BASE_FLAGS) -I$(VENDOR) -w
TEST_LIBS = -ldl -pthread

.PHONY: all install test test-timing test-hosts render lint clean
all: $(BUNDLE)/chamber.so $(TTL_TARGETS)

$(BUILD_DIR)/obj/%.o: src/%.cpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/obj/vendor/%.o: $(VENDOR)/%.cc
	@mkdir -p "$(@D)"
	$(CXX) $(VENDOR_FLAGS) -MMD -MP -c $< -o $@

$(BUNDLE)/chamber.so: $(OBJECTS) $(VENDOR_OBJECTS) src/exports.map
	@mkdir -p "$(@D)"
	$(CXX) $(OBJECTS) $(VENDOR_OBJECTS) $(LDFLAGS) $(SAN_FLAGS) -shared -Wl,--no-undefined \
		-Wl,--version-script=src/exports.map -o $@

$(BUNDLE)/%.ttl: chamber.lv2/%.ttl
	@mkdir -p "$(@D)"
	install -m 644 $< $@

$(BUILD_DIR)/test_scaffold: tests/test_scaffold.cpp tests/host.cpp tests/host.hpp src/ports.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) tests/test_scaffold.cpp tests/host.cpp $(LDFLAGS) $(TEST_LIBS) -o $@

$(BUILD_DIR)/test_resampler: tests/test_resampler.cpp src/rate_bridge.hpp src/resampler.hpp src/block_buffer.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) tests/test_resampler.cpp $(LDFLAGS) -o $@

$(BUILD_DIR)/test_voice: tests/test_voice.cpp tests/host.cpp tests/host.hpp $(BUILD_DIR)/obj/controls.o
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) tests/test_voice.cpp tests/host.cpp $(BUILD_DIR)/obj/controls.o $(LDFLAGS) $(TEST_LIBS) -o $@

$(BUILD_DIR)/render: tools/render.cpp tests/host.cpp tests/host.hpp src/ports.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) tools/render.cpp tests/host.cpp $(LDFLAGS) $(TEST_LIBS) -o $@

render: all $(BUILD_DIR)/render

# Drives the vendored Part directly, beside the plugin, so it links them too.
$(BUILD_DIR)/test_match: tests/test_match.cpp tests/host.cpp tests/host.hpp $(BUILD_DIR)/obj/controls.o $(VENDOR_OBJECTS)
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) tests/test_match.cpp tests/host.cpp $(BUILD_DIR)/obj/controls.o $(VENDOR_OBJECTS) \
		$(LDFLAGS) $(TEST_LIBS) -o $@

test: all $(BUILD_DIR)/test_scaffold $(BUILD_DIR)/test_match $(BUILD_DIR)/test_resampler $(BUILD_DIR)/test_voice $(BUILD_DIR)/render
	$(PYTHON) tests/ttl_check.py $(BUNDLE)
	$(BUILD_DIR)/test_scaffold $(BUNDLE)/chamber.so
	$(BUILD_DIR)/test_match $(BUNDLE)/chamber.so
	$(BUILD_DIR)/test_resampler
	$(BUILD_DIR)/test_voice $(BUNDLE)/chamber.so
	$(BUILD_DIR)/render $(BUNDLE)/chamber.so $(BUILD_DIR)/render-check.wav --seconds 1 model=1

test-timing: all $(BUILD_DIR)/test_scaffold
	$(BUILD_DIR)/test_scaffold $(BUNDLE)/chamber.so --timing

# jalv and Ingen with a private dummy JACK server; Ingen must be on PATH
# (Peepo's tools/ingen-spike/env.sh sets it up).
test-hosts: all
	$(PYTHON) tests/host_smoke.py $(BUNDLE)

install: all
	install -d "$(DESTDIR)$(LV2DIR)/chamber.lv2"
	install -m 755 $(BUNDLE)/chamber.so "$(DESTDIR)$(LV2DIR)/chamber.lv2/"
	install -m 644 $(TTL_TARGETS) LICENSE THIRD_PARTY_NOTICES "$(DESTDIR)$(LV2DIR)/chamber.lv2/"

lint:
	@if command -v lv2lint >/dev/null 2>&1; then \
		LV2_PATH="$(DESTDIR)$(LV2DIR):$${LV2_PATH:-/usr/local/lib/lv2:/usr/lib/lv2}" lv2lint '$(URI)'; \
	else echo 'SKIP lv2lint: not installed (install the bundle with make install first)'; fi

clean:
	$(RM) -r $(BUILD_DIR)/obj $(BUNDLE) $(BUILD_DIR)/test_scaffold $(BUILD_DIR)/test_match $(BUILD_DIR)/test_resampler $(BUILD_DIR)/test_voice $(BUILD_DIR)/render $(BUILD_DIR)/render-check.wav

-include $(OBJECTS:.o=.d) $(VENDOR_OBJECTS:.o=.d)
