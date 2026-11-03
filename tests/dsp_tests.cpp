// On-device DSP regression tests for the DynEQ core (no JUCE needed).
// Build:  c++ -std=c++17 -O2 -I../Source tests/dsp_tests.cpp -o dsp_tests
#include "dsp/DynamicEQ.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace dyneq;

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kFs = 48000.0;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf ("FAIL: %s\n", msg); } } while (0)

static void runSine (DynamicEQ& eq, double freq, double amp, double seconds, double& rmsOut)
{
    const int total = (int) (seconds * kFs);
    const int block = 256;
    std::vector<float> inL (block), inR (block), outL (block), outR (block);
    double acc = 0.0;
    int count = 0;
    double phase = 0.0;

    for (int n = 0; n < total; n += block)
    {
        const int m = std::min (block, total - n);
        for (int i = 0; i < m; ++i)
        {
            const float s = (float) (amp * std::sin (phase));
            inL[i] = inR[i] = s;
            phase += 2.0 * kPi * freq / kFs;
        }
        eq.process (inL.data(), inR.data(), outL.data(), outR.data(), m);

        if (n > total * 3 / 4)   // steady-state tail only
            for (int i = 0; i < m; ++i) { acc += (double) outL[i] * outL[i]; ++count; }
    }
    rmsOut = std::sqrt (acc / count);
}

static double measureGainDb (double freq, double amp, double seconds,
                             void (*cfg)(DynamicEQ&))
{
    DynamicEQ eq;
    eq.prepare (kFs);
    BandParams p[4];
    if (cfg) cfg (eq);
    eq.setBand (0, p[0]); eq.setBand (1, p[1]); eq.setBand (2, p[2]); eq.setBand (3, p[3]);

    double rmsOut = 0.0;
    runSine (eq, freq, amp, seconds, rmsOut);
    const double rmsIn = amp / std::sqrt (2.0);
    return 20.0 * std::log10 (rmsOut / rmsIn);
}

// --- configurations ---------------------------------------------------------
static void cfgBellP6 (DynamicEQ&) { /* helper sets band 0 */ }

static double closeTo (double a, double b, double tol) { return std::fabs (a - b) <= tol; }

int main()
{
    // 1) Bell boost +6 dB at 1 kHz.
    {
        auto run = []() {
            double g = 0;
            DynamicEQ eq; eq.prepare (kFs);
            BandParams p; p.type = FilterType::bell; p.freq = 1000; p.gainDb = 6; p.q = 1; p.mix = 1;
            eq.setBand (0, p);
            runSine (eq, 1000, 0.1, 1.5, g);
            return 20.0 * std::log10 (g / (0.1 / std::sqrt (2.0)));
        };
        const double g = run();
        CHECK (closeTo (g, 6.0, 0.4), ("bell +6 -> got " + std::to_string (g) + " dB").c_str());
    }

    // 2) Bell cut -12 dB at 1 kHz.
    {
        auto run = []() {
            double g = 0;
            DynamicEQ eq; eq.prepare (kFs);
            BandParams p; p.type = FilterType::bell; p.freq = 1000; p.gainDb = -12; p.q = 1; p.mix = 1;
            eq.setBand (0, p);
            runSine (eq, 1000, 0.1, 1.5, g);
            return 20.0 * std::log10 (g / (0.1 / std::sqrt (2.0)));
        };
        const double g = run();
        CHECK (closeTo (g, -12.0, 0.6), ("bell -12 -> got " + std::to_string (g) + " dB").c_str());
    }

    // 3) High shelf +6 dB measured well above the corner.
    {
        auto run = []() {
            double g = 0;
            DynamicEQ eq; eq.prepare (kFs);
            BandParams p; p.type = FilterType::highShelf; p.freq = 2000; p.gainDb = 6; p.mix = 1;
            eq.setBand (0, p);
            runSine (eq, 8000, 0.1, 1.5, g);
            return 20.0 * std::log10 (g / (0.1 / std::sqrt (2.0)));
        };
        const double g = run();
        CHECK (closeTo (g, 6.0, 0.5), ("high shelf +6 -> got " + std::to_string (g) + " dB").c_str());
    }

    // 4) Parallel mix 0.5 halves the bell's deviation (~+3.5 dB at centre).
    {
        auto run = []() {
            double g = 0;
            DynamicEQ eq; eq.prepare (kFs);
            BandParams p; p.type = FilterType::bell; p.freq = 1000; p.gainDb = 6; p.q = 1; p.mix = 0.5f;
            eq.setBand (0, p);
            runSine (eq, 1000, 0.1, 1.5, g);
            return 20.0 * std::log10 (g / (0.1 / std::sqrt (2.0)));
        };
        const double g = run();
        CHECK (closeTo (g, 3.5, 0.4), ("bell +6 mix .5 -> got " + std::to_string (g) + " dB").c_str());
    }

    // 5) Dynamics: loud tone collapses the +6 dB boost (range 6 -> back to ~0 dB).
    {
        auto run = []() {
            double g = 0;
            DynamicEQ eq; eq.prepare (kFs);
            BandParams p; p.type = FilterType::bell; p.freq = 1000; p.gainDb = 6; p.q = 1; p.mix = 1;
            p.dynamic = true; p.thresholdDb = -30; p.ratio = 4; p.rangeDb = 6; p.attackMs = 5; p.releaseMs = 120;
            eq.setBand (0, p);
            runSine (eq, 1000, 0.5, 2.0, g);   // -6 dBFS tone
            return 20.0 * std::log10 (g / (0.5 / std::sqrt (2.0)));
        };
        const double g = run();
        CHECK (closeTo (g, 0.0, 1.0), ("dynamic collapse -> got " + std::to_string (g) + " dB").c_str());
    }

    // 6) Dynamics: quiet tone leaves the +6 dB boost intact.
    {
        auto run = []() {
            double g = 0;
            DynamicEQ eq; eq.prepare (kFs);
            BandParams p; p.type = FilterType::bell; p.freq = 1000; p.gainDb = 6; p.q = 1; p.mix = 1;
            p.dynamic = true; p.thresholdDb = -30; p.ratio = 4; p.rangeDb = 6; p.attackMs = 5; p.releaseMs = 120;
            eq.setBand (0, p);
            runSine (eq, 1000, 0.00316, 2.0, g);  // -50 dBFS tone
            return 20.0 * std::log10 (g / (0.00316 / std::sqrt (2.0)));
        };
        const double g = run();
        CHECK (closeTo (g, 6.0, 1.0), ("dynamic quiet -> got " + std::to_string (g) + " dB").c_str());
    }

    // 7) Stability fuzz: random params must stay finite and bounded.
    {
        std::mt19937 rng (12345);
        std::uniform_real_distribution<float> uf (20, 20000);
        std::uniform_real_distribution<float> ug (-18, 18);
        std::uniform_real_distribution<float> uq (0.1f, 18);
        std::uniform_real_distribution<float> um (0, 1);
        bool ok = true;
        float maxAbs = 0.0f;

        for (int trial = 0; trial < 200 && ok; ++trial)
        {
            DynamicEQ eq; eq.prepare (kFs);
            for (int b = 0; b < kNumBands; ++b)
            {
                BandParams p;
                p.type = (FilterType) (rng() % 3);
                p.freq = uf (rng); p.gainDb = ug (rng); p.q = uq (rng); p.mix = um (rng);
                p.dynamic = (rng() % 2);
                p.thresholdDb = -60 + 60 * um (rng); p.ratio = 1 + 9 * um (rng);
                p.rangeDb = 24 * um (rng); p.attackMs = 0.1f + 200 * um (rng); p.releaseMs = 5 + 2000 * um (rng);
                eq.setBand (b, p);
            }
            std::vector<float> L (256), R (256), oL (256), oR (256);
            for (int i = 0; i < 256; ++i) { L[i] = (rng() % 1000) / 500.0f - 1.0f; R[i] = L[i]; }
            eq.process (L.data(), R.data(), oL.data(), oR.data(), 256);
            for (int i = 0; i < 256; ++i)
            {
                if (!std::isfinite (oL[i]) || !std::isfinite (oR[i])) { ok = false; break; }
                maxAbs = std::max (maxAbs, std::fabs (oL[i]));
            }
        }
        CHECK (ok, "fuzz produced non-finite output");
        CHECK (maxAbs < 100.0f, ("fuzz output unbounded (" + std::to_string (maxAbs) + ")").c_str());
    }

    // 8) CPU benchmark (stereo, 4 active bands, 10 s @ 48 kHz).
    {
        DynamicEQ eq; eq.prepare (kFs);
        for (int b = 0; b < kNumBands; ++b)
        {
            BandParams p; p.type = FilterType::bell; p.freq = 120.f * (b + 1) * (b + 1);
            p.gainDb = 3; p.q = 1; p.mix = 1; p.dynamic = true;
            eq.setBand (b, p);
        }
        const int block = 512;
        std::vector<float> L (block, 0.1f), R (block, 0.1f), oL (block), oR (block);
        const int loops = (int) (kFs * 10 / block);

        const auto t0 = std::chrono::steady_clock::now();
        for (int n = 0; n < loops; ++n) eq.process (L.data(), R.data(), oL.data(), oR.data(), block);
        const auto t1 = std::chrono::steady_clock::now();

        const double us = std::chrono::duration<double, std::micro> (t1 - t0).count();
        const double nsPerSample = us * 1000.0 / (double) (loops * block);
        std::printf ("CPU: %.2f ns/sample  (%.3f%% of one 48 kHz core)\n",
                     nsPerSample, nsPerSample * 48000.0 / 1e7);
    }

    std::printf ("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
