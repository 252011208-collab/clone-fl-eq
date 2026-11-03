// DynEQ DSP core — pure C++17, no JUCE, no external deps.
//
// Topology (TDR Nova-inspired "parallel" dynamic EQ):
//   Each band extracts its region with one biquad (band-pass for a bell,
//   low/high-pass for shelves) from the DRY input, runs a band-linked
//   envelope detector on that component, computes a time-varying gain
//   (downward compression of the band), and adds the weighted component
//   back into the dry signal:
//
//     out = dry + sum_k  component_k * ((Gk * dynGain_k) - 1) * mix_k
//
//   => one biquad + one envelope per band, zero extra latency, no oversampling.
#pragma once

#include "Biquad.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace dyneq
{
    constexpr int kNumBands = 4;

    // Everything the host can set for one band.
    struct BandParams
    {
        FilterType type = FilterType::bell;   // bell | lowShelf | highShelf
        float freq       = 1000.0f;           // Hz
        float gainDb     = 0.0f;              // static boost/cut, -18..+18 dB
        float q          = 1.0f;              // bell width (ignored for shelves)
        float mix        = 1.0f;              // 0..1 parallel amount
        bool  dynamic    = false;
        float thresholdDb = -30.0f;           // detector threshold, -60..0 dB
        float ratio      = 3.0f;              // 1..10
        float rangeDb    = 12.0f;             // max gain reduction, 0..24 dB
        float attackMs   = 5.0f;              // detector attack, 0.1..200 ms
        float releaseMs  = 120.0f;            // detector release, 5..2000 ms
    };

    inline Coeffs makeComponentFilter (FilterType type, double fs, double f, double q) noexcept
    {
        switch (type)
        {
            case FilterType::lowShelf:  return makePass (fs, f, 0.7071067811865476, FilterType::lowPass);
            case FilterType::highShelf: return makePass (fs, f, 0.7071067811865476, FilterType::highPass);
            case FilterType::bell:
            default:                    return makePass (fs, f, q, FilterType::bandPass);
        }
    }

    class DynamicBand
    {
    public:
        void prepare (double fs) noexcept
        {
            fs_ = fs;
            freqSmooth_ = 1.0f - std::exp (-1.0 / (0.010 * fs));  // 10 ms
            valSmooth_  = 1.0f - std::exp (-1.0 / (0.005 * fs));  // 5 ms
            reset();
        }

        void reset() noexcept
        {
            stL_.reset(); stR_.reset();
            env_ = 0.0f;
            freq_ = target_.freq;
            q_    = target_.q;
            gainDb_ = target_.gainDb;
            mix_ = target_.mix;
            grDb_ = 0.0f;
            counter_ = 0;
            updateCoeffs();
        }

        void setParams (const BandParams& p) noexcept { target_ = p; }

        inline void processAdd (float xL, float xR, float& yL, float& yR) noexcept
        {
            const float cL = stL_.process (xL, coeffs_);
            const float cR = stR_.process (xR, coeffs_);

            // Band-linked detector on the component (stereo max).
            const float det = std::max (std::fabs (cL), std::fabs (cR));

            float g = 1.0f;
            if (target_.dynamic)
            {
                const float coef = (det > env_) ? attackCoef_ : releaseCoef_;
                env_ += (det - env_) * coef;
                if (env_ < 1e-15f) env_ = 0.0f;

                const float detDb = 20.0f * std::log10 (env_ + 1e-9f);
                g = computeDynGain (detDb);
            }

            // Smooth the static shaping parameters (zipper-free).
            gainDb_ += (target_.gainDb - gainDb_) * valSmooth_;
            mix_    += (target_.mix - mix_) * valSmooth_;

            const float staticGain = std::pow (10.0f, gainDb_ * 0.05f);
            const float k = (staticGain * g - 1.0f) * mix_;
            yL += cL * k;
            yR += cR * k;

            // Coefficient update at a 1/16-of-block control rate.
            if ((++counter_ & 15) == 0)
            {
                freq_ += (target_.freq - freq_) * freqSmooth_;
                q_    += (target_.q - q_) * freqSmooth_;
                updateCoeffs();
            }
        }

        float currentGrDb() const noexcept { return grDb_; }
        const BandParams& params() const noexcept { return target_; }
        float currentEffGainDb() const noexcept { return gainDb_ - grDb_; }

    private:
        float computeDynGain (float detDb) noexcept
        {
            constexpr float knee = 6.0f;                 // dB
            const float over = detDb - target_.thresholdDb;
            float gr;
            if (over <= -knee * 0.5f)
                gr = 0.0f;
            else if (over >= knee * 0.5f)
                gr = over * (1.0f - 1.0f / target_.ratio);
            else
            {
                const float t = over + knee * 0.5f;
                gr = (1.0f - 1.0f / target_.ratio) * t * t / (2.0f * knee);
            }
            gr = std::min (gr, target_.rangeDb);
            if (gr < 0.0f) gr = 0.0f;
            grDb_ = gr;
            return std::pow (10.0f, -gr * 0.05f);
        }

        void updateCoeffs() noexcept
        {
            coeffs_ = makeComponentFilter (target_.type, fs_, freq_, q_);
            if (!coeffs_.isFinite())
                coeffs_ = makeBypass();

            const float atk = 1.0f - std::exp (-1.0 / (target_.attackMs * 0.001 * fs_));
            const float rel = 1.0f - std::exp (-1.0 / (target_.releaseMs * 0.001 * fs_));
            attackCoef_  = std::min (std::max (atk, 1e-5f), 1.0f);
            releaseCoef_ = std::min (std::max (rel, 1e-5f), 1.0f);
        }

        double fs_ = 48000.0;
        float freqSmooth_ = 0.01f, valSmooth_ = 0.01f;
        float attackCoef_ = 0.001f, releaseCoef_ = 0.001f;

        BandParams target_;
        Coeffs coeffs_{};
        BiquadState stL_, stR_;
        float env_ = 0.0f;
        float freq_ = 1000.0f, q_ = 1.0f, gainDb_ = 0.0f, mix_ = 1.0f;
        float grDb_ = 0.0f;
        unsigned counter_ = 0;
    };

    // Complex response of one band in the parallel sum (static, dynamic off):
    //   H_k(f) = 1 + (G_k - 1) * mix_k * H_component,k(f)
    inline std::complex<double> bandResponse (const BandParams& p, double f, double fs) noexcept
    {
        const double g = std::pow (10.0, p.gainDb / 20.0);
        const Coeffs c = makeComponentFilter (p.type, fs, p.freq, p.q);
        return 1.0 + (g - 1.0) * p.mix * response (c, f, fs);
    }

    // Total parallel EQ response (product-free sum).
    inline std::complex<double> totalResponse (const BandParams (&bands)[kNumBands], double f, double fs) noexcept
    {
        std::complex<double> h = 1.0;
        for (int b = 0; b < kNumBands; ++b)
            h += bandResponse (bands[b], f, fs) - 1.0;
        return h;
    }

    class DynamicEQ
    {
    public:
        void prepare (double fs) noexcept
        {
            fs_ = fs;
            for (auto& b : bands_) b.prepare (fs);
            outGain_ = 1.0f;
            targetOutGain_ = 1.0f;
            reset();
        }

        void reset() noexcept
        {
            for (auto& b : bands_) b.reset();
            outGain_ = targetOutGain_ = 1.0f;
        }

        void setBand (int i, const BandParams& p) noexcept
        {
            if (i >= 0 && i < kNumBands) bands_[i].setParams (p);
        }

        void setOutputGainDb (float db) noexcept { targetOutGain_ = std::pow (10.0f, db * 0.05f); }

        void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const float xL = inL[i], xR = inR[i];
                float yL = xL, yR = xR;

                for (int b = 0; b < kNumBands; ++b)
                    bands_[b].processAdd (xL, xR, yL, yR);

                outGain_ += (targetOutGain_ - outGain_) * outGainSmooth_;
                outL[i] = yL * outGain_;
                outR[i] = yR * outGain_;
            }
        }

        float currentGrDb (int i) const noexcept { return bands_[i].currentGrDb(); }
        const BandParams& bandParams (int i) const noexcept { return bands_[i].params(); }
        double sampleRate() const noexcept { return fs_; }

    private:
        double fs_ = 48000.0;
        float outGain_ = 1.0f, targetOutGain_ = 1.0f;
        float outGainSmooth_ = 0.01f;
        DynamicBand bands_[kNumBands];
    };
}
