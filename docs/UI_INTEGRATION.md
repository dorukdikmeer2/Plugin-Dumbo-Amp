# Replacing the user interface

The UI is a thin layer: **nothing in `Source/Engine`, `Source/Amps`, `Source/Cab` or `Source/Parameters` knows the UI exists.**
A new design can replace `Source/PluginEditor.*` and `Source/UI/` without touching anything else.

## What a UI needs
* The processor object (`DumbDockAmpAudioProcessor`) with its `apvts` (a `juce::AudioProcessorValueTreeState`).
* Parameter IDs from `Source/Parameters/ParamIDs.h`. Every control is an attachment to one ID:

```cpp
knob.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, ids::a1Master, knob);
```

## Parameter map
| Group | IDs |
|---|---|
| global | `activeAmp` (0 Dumb, 1 Dock), `activeCab` (0 A, 1 B), `cab_off`, `input_trim`, `output_trim` |
| Dumb 1 | `a1_mode` (0..3), `a1_powertube` (0..2), `a1_character`, `a1_presence`, `a1_master`, `a1_reverb`, `a1_gate`, `a1_eq_bypass`, `a1_eq0..a1_eq5` (-1..+1 = +-15 dB) |
| Dumb 1 per channel `n` = 1..4 | `a1_c{n}_gain`, `a1_c{n}_bass`, `a1_c{n}_mid`, `a1_c{n}_treble` (0..10) |
| Dock | `a2_channel` (0 A, 1 B), `a2_master`, `a2_presence`, `a2_reverb`, `a2_standby` |
| Dock per channel | `a2_a_{gain,bass,mid,treble}`, `a2_b_{gain,bass,mid,treble}` |
| cab `X` = A or B | `cab{X}_level` (dB), `cab{X}_m1_{type,pos,dist,level,pan,phase}`, `cab{X}_m2_{on,type,pos,dist,level,pan,phase}` |

Mic type: 0 SM57, 1 SM58, 2 Royer 121, 3 AKG 414, 4 MD 421. Position and distance 0..1, level in dB (-24..+6), pan -1..+1.

Build IDs in code with the helpers `ids::a1Ch(mode, ids::gain)`, `ids::a2Ch(channel, ids::bass)`, `ids::cabMic(cab, mic, "pos")`.

## Read-only values for meters
`processor.rawInputPeak`, `processor.outputPeak`, `processor.limiterReductionDb`, `processor.gateReductionDb`,
`processor.faultCount` (atomics, linear / dB, safe to poll from a `juce::Timer`). `VUMeter` shows how.

## Behaviour the UI can rely on
* Switching `a1_mode` / `a2_channel` / `activeAmp` is click-free (the engine ducks while it switches); each channel keeps its own
  gain and tone values, so a "channel strip" per channel is the natural layout.
* The welcome popup is separate (`UI/WelcomePopup.h`, stored per user in `DorukAudio/DumbDockAmp.settings`).
* The current `PluginEditor` is a functional placeholder: green (Dumb) / amber (Dock) panels with every control exposed.
  It lays out each channel strip as 2 x 2 knobs, the global controls, six EQ sliders and two cab panels.

## Checklist for a new editor
1. Attach every ID above (the host test `tests/plugin_host_test.cpp` fails if a parameter disappears).
2. Keep `setResizable`/size handling in the editor only.
3. Run `DumbDockHostTest` (`-DDUMBDOCK_BUILD_TESTS=ON`): it opens the editor and renders it to PNG.
