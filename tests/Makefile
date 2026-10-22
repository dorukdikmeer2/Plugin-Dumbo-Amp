# Headless test / tooling build for the JUCE-free engine.
#   make -j8            build everything into tests/build/
#   make check          build + run the full audit (engine audit, voicing, gain staging, cab/mic, features)
#   make calibrate      re-measure the loudness tables and rewrite Source/Amps/Calibration.h (see docs/GAIN_STAGING.md)
SRC      := ../Source
CXX      ?= g++
CXXFLAGS := -std=c++17 -O3 -pthread -I$(SRC) -I. -MMD -MP
ENGINE   := Amps/TubeStage Amps/ToneStack Amps/PowerStage Amps/Reverb Amps/Amp001Model Amps/Amp002Model \
            Cab/CabinetSim Cab/CabIrDesigner Engine/AmpEngine
OBJ      := $(patsubst %,build/%.o,$(ENGINE))
TESTS    := engine_audit voicing gainstage cabmic features
BIN      := $(patsubst %,build/%,$(TESTS)) build/calibrate

all: $(BIN)

build/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%: %.cpp $(OBJ) metrics.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< $(OBJ) -o $@

build/calibrate: calibrate.cpp $(OBJ) metrics.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -DCALIB_A2 $< $(OBJ) -o $@

check: all
	@set -e; for t in $(TESTS); do echo "################ $$t"; build/$$t; done

calibrate: build/calibrate
	build/calibrate all ../Source/Amps/Calibration.h
	$(MAKE) all

clean:
	rm -rf build

-include $(OBJ:.o=.d)
.PHONY: all check calibrate clean
