// Level calibration: iterates the make-up tables in Source/Amps/Calibration.h until every mode / channel
// follows its loudness target over GAIN 1..10, then rewrites that header.   Usage: calibrate [a1|a2|all]
#include "metrics.h"
#include "Amps/Calibration.h"
#include <thread>
#include <fstream>
using namespace mx;

// target RMS (dBFS) of the final output for the 2 s test chord (peak 0.25 FS) at MASTER 5
static double targetA1(int mode, int g) {
    const double L1[4] = { -32.5, -31.5, -29.5, -28.5 };         // level at GAIN 1 (3 dB lower again in revision 4: kAmpOutScale = -3 dB, see docs/GAIN_STAGING.md)
    const double rise[4] = { 9.0, 9.0, 7.0, 7.0 };               // total rise 1 -> 10
    const double tc[4] = { 0, 0, 3.0, 3.0 };                     // >0: fast-saturating curve (high-gain modes compress)
    double s = (g - 1) / 9.0;
    if (tc[mode] > 0) s = (1 - exp(-(g - 1) / tc[mode])) / (1 - exp(-9.0 / tc[mode]));
    return L1[mode] + rise[mode] * s;
}
static double targetA2(int ch, int g) {
    const double L1[2] = { -31.5, -28.5 }, rise[2] = { 14.0, 12.0 };       // Dock +6 dB vs the first rebuild: it was too quiet next to the Dumb amp
    return L1[ch] + rise[ch] * (g - 1) / 9.0;
}
static double measure(EngineParams p, const vector<float>& chordSig) { return level(process(p, chordSig)).rmsDb; }
// loudness target, capped so that the -12 dBFS test chord never peaks above PEAK_CAP (the heavily compressed high-gain modes
// have a low crest factor, so the same RMS would otherwise mean hotter peaks)
static const double PEAK_CAP = -12.5, PEAK_CAP_DOCK = -11.5;     // the Dock has a much higher crest factor, it may peak a little hotter
static double err(EngineParams p, const vector<float>& sig, double targetRms, double cap = PEAK_CAP) {
    R r = level(process(p, sig));
    double e = targetRms - r.rmsDb;
    if (r.pkDb + e > cap) e = cap - r.pkDb;
    return e;
}

int main(int argc, char** argv) {
    std::string which = argc > 1 ? argv[1] : "all";
    auto sig = chord(0.25f);
    const int ITER = 12;
    vector<std::thread> th;
    if (which == "a1" || which == "all")
        for (int m = 0; m < 4; ++m) th.emplace_back([&, m] {
            double* T = calib::a1Comp[m];
            for (int it = 0; it < ITER; ++it) {
                double err = 0;
                for (int g = 1; g <= 10; ++g) {
                    EngineParams p = dumb(m, (float) g);
                    double e = ::err(p, sig, targetA1(m, g)); err = std::max(err, std::fabs(e));
                    T[g] += 0.9 * e;
                }
                T[0] = T[1];
                if (err < 0.15) break;
            }
        });
#ifdef CALIB_A2
    if (which == "a2" || which == "all")
        for (int ch = 0; ch < 2; ++ch) th.emplace_back([&, ch] {
            double* T = ch ? calib::a2CompB : calib::a2CompA;
            for (int it = 0; it < ITER; ++it) {
                double err = 0;
                for (int g = 1; g <= 10; ++g) {
                    EngineParams p = dock(ch, (float) g);
                    double e = ::err(p, sig, targetA2(ch, g), PEAK_CAP_DOCK); err = std::max(err, std::fabs(e));
                    T[g] += 0.9 * e;
                }
                T[0] = T[1];
                if (err < 0.15) break;
            }
        });
#endif
    for (auto& t : th) t.join();

    std::ofstream f(argc > 2 ? argv[2] : "../Source/Amps/Calibration.h");
    f << "#pragma once\n//==============================================================================\n"
         "// Level make-up tables (dB, per GAIN knob position 0..10) measured by tests/calibrate.cpp so that the\n"
         "// loudness curve of every mode/channel follows its voicing target.  Generated: run tests/calibrate\n"
         "// after changing a model, never edit by hand (the plugin only reads these tables).\n"
         "//==============================================================================\nnamespace calib {\n";
    auto row = [&](const double* T) { char b[400]; std::string s = "{ "; for (int i = 0; i < 11; ++i) { snprintf(b, 400, "%.2f%s", T[i], i < 10 ? ", " : " }"); s += b; } return s; };
    f << "// AMP-001, rows = SW1 position 0..3\ninline double a1Comp[4][11] = {\n";
    for (int m = 0; m < 4; ++m) f << "    " << row(calib::a1Comp[m]) << (m < 3 ? ",\n" : " };\n");
    f << "// AMP-002, channel A / channel B\ninline double a2CompA[11] = " << row(calib::a2CompA) << ";\n";
    f << "inline double a2CompB[11] = " << row(calib::a2CompB) << ";\n}\n";
    for (int m = 0; m < 4; ++m) { printf("a1 mode %d:", m + 1); for (int g = 1; g <= 10; ++g) { EngineParams p = dumb(m, (float) g); printf(" %.1f", measure(p, sig)); } printf("\n"); }
    return 0;
}
