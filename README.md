# DumbDock Amp

## Download (latest)
| System | Download |
|---|---|
| **macOS** (Apple Silicon + Intel) | [DumbDockAmp-macOS.zip](https://github.com/dorukdikmeer2/Plugin-Dumbo-Amp/releases/download/latest/DumbDockAmp-macOS.zip) |
| **Windows** | [DumbDockAmp-Windows-MSVC.zip](https://github.com/dorukdikmeer2/Plugin-Dumbo-Amp/releases/download/latest/DumbDockAmp-Windows-MSVC.zip) |

Both are built automatically by GitHub from the newest `main` and passed the strict pluginval check on their own system
([all builds on the Releases page](https://github.com/dorukdikmeer2/Plugin-Dumbo-Amp/releases/tag/latest)).
Unzip, then follow `README-FIRST.txt` inside (Windows: double-click `Install-DumbDockAmp.exe`; macOS: open `Install-DumbDockAmp.pkg`).
Then rescan plug-ins in your DAW and add `VST3: DumbDock Amp`.
The macOS installer is not notarised (no Apple developer account), so macOS asks you to confirm opening it once; the steps are in the README-FIRST file.
Not yet tried by the author on a real Mac or Windows machine, only by GitHub's test machines.

A two-amp guitar VST3 plugin built from hand-drawn circuit schematics, with two cabinets and a microphone model.
Personal project (JUCE 7, C++17). Mono guitar in, stereo out.

| | |
|---|---|
| **AMP-001 "Dumb 1"** | Four-channel boutique head: Clean / Crunch / Lead / Ultra. Every channel has its own GAIN, BASS, MID, TREBLE. Shared: CHAR, PRES, MASTER, VERB, noise GATE, 6-band EQ, power tube (6L6GC / EL34 / KT88). |
| **AMP-002 "Dock"** | 50 W EL34 combo, two channels: A Pristine (huge clean headroom) and B Chime (up to edge-of-breakup). Own GAIN / BASS / MID / TREBLE per channel, PRES, MASTER, spring VERB, standby, MIDI CC control. |
| **Cabinets** | CAB A 4x12 closed back, CAB B 1x12 open back. Each is miked with one or two microphones (SM57, SM58, Royer 121, AKG 414, MD 421 *clones*) with position, distance, level, pan and polarity per mic. |
| **Safety** | Look-ahead limiter at -1 dBFS instead of a clipper: no setting can clip the output. |

The circuit models (triode stages, long-tailed-pair phase inverter, tone stacks, power stage) are solved from the schematic values.
Voicing choices that are **not** on the schematics are listed in [VOICING_NOTES.md](VOICING_NOTES.md).

## Documentation
* [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) - build, install, tests
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - code layout and signal chain
* [docs/GAIN_STAGING.md](docs/GAIN_STAGING.md) - levels, limiter, what was measured
* [docs/CAB_AND_MICS.md](docs/CAB_AND_MICS.md) - the cabinet / microphone model
* [docs/UI_INTEGRATION.md](docs/UI_INTEGRATION.md) - parameter IDs and how to replace the UI
* [VOICING_NOTES.md](VOICING_NOTES.md) - what was studied and tuned

## Quality gates
`tests/` contains headless audits of the JUCE-free engine (`make check`: 95 + 402 + 797 + 134 + 101 checks) and a JUCE host
test (395 checks). They measure; they are not a substitute for listening.

## Licensing / credits
* The plugin code is by Doruk (with substantial help from Claude, Anthropic's AI assistant).
* JUCE is dual licensed (AGPLv3 / commercial); a distributed plugin built with JUCE has to comply with one of them.
  Choose a licence for this repository with that in mind.
* Microphone and amplifier names are trademarks of their owners and are used only to describe the modelled character;
  this project is not affiliated with or endorsed by them. The microphone models are approximations, not measurements.

## Revision 3 (what changed)
* Cab MIC PAN is now a pure balance control (it no longer changes loudness); overall cab loudness is the CAB LVL knob (-12 .. +18 dB).
* Dumb: EQ BYPASS switch for the six-band EQ.
* Dumb Crunch re-voiced from fuzzy to edge-of-breakup -> blues-rock crunch (less top end, less fizz above GAIN 6).
* Dock level raised by about 6 dB and re-balanced against the Dumb amp.
* BASS / MID / TREBLE on both amps have a much wider range (an extra +-9 dB contour on top of the schematic stack; knob 5 is flat).
* Presets: save / load / delete your own settings (amp, mics, cabs, EQ); a built-in DEFAULT returns to the opening sound.
* Cab selector sits directly above the cab panels; the window is freely resizable (aspect ratio kept).

## Revision 4 (what changed)
* Output: every voice is 3 dB lower, the safety limiter ceiling is -3 dBFS and inter-sample aware, the OUT meter is green / amber / red at -8 / -4 dBFS.
  No OUT TRIM needed at default settings.
* Mic level: one mic, two mics, mic polarity and pan do not change the loudness any more (each output side = its loudest mic).
  CAB LVL is the master level; MIC PAN only moves the image.
* PHASE INVERT on MIC 1 as well (mic 2 had it already).
* GAIN knobs of Crunch, Lead, Ultra and Chime now use the whole sweep (no dead first half). Clean and Pristine are unchanged.
  Crunch: edge of breakup at the bottom, steady climb to a rock crunch at 10. Chime: edge of breakup from about 4-5, light overdrive at 10.
  Ultra and Lead: a bit more gain.
* Presets: < > buttons next to the drop-down step through Default and your presets.
* Removed: FX LOOP (both amps; it was a unity pass-through) and HEATER VOLT (Dock; always 20 V now). MIDI CC64 is no longer used.
