#pragma once
#include <cmath>
#include <algorithm>

namespace cab {

//==============================================================================
// Microphone models.
//
// Every model is described by a handful of numbers that the IR designer
// (CabIrDesigner) combines with the speaker, the mic position and the distance:
//
//   * farField   on-axis, bass-compensated response at ~1 m in dB re 1 kHz.
//                These control points are an approximation of the manufacturers'
//                published frequency-response plots, re-drawn by hand (NOT
//                measured by me); they carry the character of each mic.
//   * alpha      polar pattern as  alpha + (1-alpha)*cos(theta):
//                0.5 = cardioid, 0 = figure-8 (pure pressure gradient)
//   * capsule    distance between the front of the grille/basket and the
//                diaphragm or ribbon (m): decides how close the element
//                really gets, which sets the proximity effect
//   * offAxisDb  extra high-frequency loss at 90 degrees, 8 kHz (dB)
//
// The five models are labelled "clone" in the plugin: they are *inspired by*
// the named microphones, they are not measurements of them.
//==============================================================================
struct MicSpec {
    const char* name;
    const char* character;
    double alpha;
    double capsule;
    double offAxisDb;
    int    numPoints;
    const float (*farField)[2];        // { Hz, dB }
};

// ---- control points ---------------------------------------------------------
// Shure SM57 - dynamic cardioid: bass roll-off, broad presence peak 3-7 kHz, steep top above 10 kHz.
static const float kSm57[][2] = {
    {  30, -22 }, {  50, -14 }, {  80, -8.5f }, { 120, -5.0f }, { 200, -2.5f }, { 350, -1.0f }, { 600, -0.2f }, { 1000, 0 },
    { 1600, 1.5f }, { 2500, 3.8f }, { 3500, 5.6f }, { 5000, 6.8f }, { 6500, 5.6f }, { 8000, 3.4f }, { 10000, 0.2f },
    { 12500, -4.5f }, { 16000, -11 }, { 20000, -20 }, { 24000, -30 } };
// Shure SM58 - ball-grille vocal capsule: fuller low mids, softer upper-mid bite, a little more air than the 57.
static const float kSm58[][2] = {
    {  30, -18 }, {  50, -10.5f }, {  80, -5.5f }, { 120, -3.0f }, { 200, -0.8f }, { 350, -0.8f }, { 600, -0.6f }, { 1000, 0 },
    { 1600, 0.8f }, { 2500, 2.0f }, { 3500, 3.6f }, { 5000, 5.2f }, { 6500, 5.0f }, { 8000, 4.0f }, { 10000, 1.6f },
    { 12500, -2.0f }, { 16000, -9 }, { 20000, -18 }, { 24000, -28 } };
// Royer R-121 - passive ribbon, figure-8: very flat and smooth, gentle top roll-off, no presence peak, strong proximity effect.
static const float kRoyer121[][2] = {
    {  30, -3.5f }, {  50, -1.0f }, {  80, 0.2f }, { 150, 0.8f }, { 300, 0.3f }, { 600, 0 }, { 1000, 0 }, { 2000, 0.4f },
    { 3500, 0.9f }, { 5000, 0.2f }, { 7000, -1.6f }, { 9000, -3.2f }, { 12000, -5.2f }, { 15000, -8.0f }, { 18000, -12 },
    { 20000, -16 }, { 24000, -26 } };
// AKG C 414 - large-diaphragm condenser (cardioid): extended and open, mild 5-6 kHz lift, 'air' peak around 10-12 kHz.
static const float kC414[][2] = {
    {  30, -4.0f }, {  50, -1.8f }, {  80, -0.4f }, { 150, 0.2f }, { 400, 0.2f }, { 1000, 0 }, { 2000, -1.0f }, { 3200, -0.4f },
    { 5000, 3.2f }, { 8000, 7.0f }, { 11000, 8.5f }, { 14000, 6.5f }, { 17000, 1.6f }, { 20000, -2.5f }, { 24000, -12 } };
// Sennheiser MD 421 - dynamic cardioid: very extended bass, a little low-mid scoop, punchy 4-6 kHz presence, smooth top to 17 kHz.
static const float kMd421[][2] = {
    {  30, -5.0f }, {  50, -2.0f }, {  80, -0.4f }, { 150, 0.6f }, { 300, -0.4f }, { 500, -3.0f }, { 800, -1.8f }, { 1000, 0 },
    { 2000, 1.4f }, { 3000, 3.2f }, { 4500, 4.8f }, { 6000, 4.6f }, { 8000, 3.4f }, { 10000, 1.6f }, { 12500, -1.2f },
    { 15000, -4.6f }, { 17500, -9.5f }, { 20000, -16 }, { 24000, -26 } };

inline const MicSpec& micSpec(int type)
{
    static const MicSpec specs[5] = {
        { "SM57 clone",        "dynamic cardioid - tight, midrange bite, the studio standard for guitar cabs",
          0.50, 0.008, 8.0, (int) (sizeof(kSm57) / sizeof(kSm57[0])), kSm57 },
        { "SM58 clone",        "ball-grille dynamic - rounder and fuller than the 57, a little less bite",
          0.50, 0.016, 6.0, (int) (sizeof(kSm58) / sizeof(kSm58[0])), kSm58 },
        { "Royer 121 clone",   "ribbon figure-8 - dark, smooth and big, strong proximity effect",
          0.00, 0.012, 5.0, (int) (sizeof(kRoyer121) / sizeof(kRoyer121[0])), kRoyer121 },
        { "AKG 414 clone",     "large-diaphragm condenser - open, detailed, extended highs",
          0.50, 0.022, 4.0, (int) (sizeof(kC414) / sizeof(kC414[0])), kC414 },
        { "MD 421 clone",      "dynamic cardioid - full low end, scooped low mids, punchy presence",
          0.50, 0.014, 8.0, (int) (sizeof(kMd421) / sizeof(kMd421[0])), kMd421 } };
    return specs[type < 0 ? 0 : (type > 4 ? 4 : type)];
}

// Far-field response of a model in dB re 1 kHz at f (Hz): cubic Hermite through the control points in log-frequency.
inline double micFarFieldDb(const MicSpec& m, double f)
{
    const int n = m.numPoints;
    const double lf = std::log(std::max(f, 1.0));
    if (f <= m.farField[0][0]) return m.farField[0][1] + 6.0 * std::log2(std::max(f, 1.0) / m.farField[0][0]);   // 6 dB/oct below the first point
    if (f >= m.farField[n - 1][0]) return m.farField[n - 1][1];
    int i = 0;
    while (i < n - 2 && f > m.farField[i + 1][0]) ++i;
    auto X = [&](int j) { return std::log((double) m.farField[j][0]); };
    auto Y = [&](int j) { return (double) m.farField[j][1]; };
    auto slope = [&](int j) {                                 // Catmull-Rom tangent, clamped to avoid overshoot
        if (j <= 0) return (Y(1) - Y(0)) / (X(1) - X(0));
        if (j >= n - 1) return (Y(n - 1) - Y(n - 2)) / (X(n - 1) - X(n - 2));
        const double a = (Y(j) - Y(j - 1)) / (X(j) - X(j - 1)), b = (Y(j + 1) - Y(j)) / (X(j + 1) - X(j));
        return (a * b <= 0.0) ? 0.0 : 2.0 * a * b / (a + b);  // harmonic mean: monotone between points
    };
    const double h = X(i + 1) - X(i), t = (lf - X(i)) / h, t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * Y(i) + (t3 - 2 * t2 + t) * h * slope(i) + (-2 * t3 + 3 * t2) * Y(i + 1) + (t3 - t2) * h * slope(i + 1);
}

} // namespace cab
