# Architecture

```
Source/
  Amps/         circuit models of the two amps (no JUCE)
    TubeStage    Koren triode + LTP phase inverter, table driven
    ToneStack    TMB stack / EQ filters
    PowerStage   behavioural power amp + output transformer
    Reverb       room (FDN) and spring
    Amp001Model  "Dumb 1": 4 modes, per-mode voicing tables
    Amp002Model  "Dock": 2 channels
    Calibration.h  generated loudness tables (tests/calibrate.cpp)
  Cab/          cabinets and microphones (no JUCE)
    MicModels    5 mic specs + response curves
    CabIrDesigner  physics -> minimum-phase impulse response
    CabinetSim   convolution, 1 or 2 mics, IR morphing
    Fft.h
  Engine/       everything between host in and host out (no JUCE)
    AmpEngine    the chain, smoothing, structural switching, fault handling
    EngineParams plain-data description of every control
    NoiseGate    downward expander (Dumb only)
    Limiter      look-ahead safety limiter
  Common/DspUtil.h   oversamplers, filters, DC blocker, loudness laws
  Parameters/   host parameters <-> EngineParams
    ParamIDs.h   all IDs in one place
    ParameterLayout.cpp   ranges, names, defaults
    EngineBinding.cpp     real-time read of the parameters into EngineParams
  UI/ + PluginEditor.*   front panel (replaceable, see UI_INTEGRATION.md)
  PluginProcessor.*      the JUCE shell: buses, MIDI, state, latency
tests/        headless audits + the JUCE host test
installer/    Windows installer sources, mac / windows readmes
```

Dependencies only point downwards: UI -> processor -> Parameters -> Engine -> Amps/Cab -> Common. `Engine/Amps/Cab/Common`
build into the static library `DumbDockEngine` (see CMakeLists.txt) and are also compiled by `tests/Makefile` without JUCE.

## Processing chain
```
mono in (stereo is summed) -> input trim -> [noise gate, Dumb only] -> oversampling (4x <= 52 kHz, 2x <= 100 kHz)
 -> amp model -> decimate -> DC block -> duck envelope -> cabinet (1-2 mics, stereo) -> cab level -> output trim
 -> DC block -> look-ahead limiter -> DC block -> clamp -> stereo out
```
Control smoothing: every continuous control is smoothed per 32-sample chunk. Structural switches (mode, channel,
power tube, active amp, standby) are applied while the output is ducked. Per-channel controls follow whichever channel
is active (they snap on a channel change so a channel always sounds like itself).
Safety: NaN/Inf/runaway detection resets the amp and mutes for a short fade-in; no allocation, locks or string lookups in `process()`.

## State and migration
`PluginProcessor::setStateInformation` restores parameters through the APVTS. State version 3 introduced per-channel
and per-cab parameters; older projects (shared gain/tone, one cab mic) are migrated by copying the shared values to every
channel / cab. IDs are part of the file format: never rename without adding a migration.

## MIDI (Dock): CC1 channel, CC7 master, CC11 reverb, CC80 standby.

## Tests (tests/Makefile, no JUCE)
`make check` builds and runs: `engine_audit` (95 checks), `voicing` (396), `gainstage` (789), `cabmic` (123), `features` (55).
`calibrate` regenerates `Source/Amps/Calibration.h`. The JUCE host test (`DumbDockHostTest`, 367 checks) needs
`-DDUMBDOCK_BUILD_TESTS=ON`.
