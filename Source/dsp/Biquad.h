// Pure C++ (no JUCE) biquad section used by the dynamic EQ.
// RBJ cookbook coefficients, transposed direct form II.
#pragma once

#include <cmath>
#include <complex>

namespace dyneq
{
    enum class FilterType
    {
        bell = 0,
        lowShelf,
        highShelf,
        highPass,
        lowPass,
        notch,
        bandPass
    };

    struct Coeffs
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

        bool isFinite() const noexcept
        {
            return std::isfinite (b0) && std::isfinite (b1) && std::isfinite (b2)
                && std::isfinite (a1) && std::isfinite (a2);
        }
    };

    constexpr float kMaxQ = 18.0f;
    constexpr float kMinQ = 0.1f;

    inline Coeffs makeBypass() noexcept { return {}; }

    inline Coeffs makeBell (double fs, double f, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
        const double cw = std::cos (w0);
        const double sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);

        const double a0 = 1.0 + alpha / A;
        Coeffs c;
        c.b0 = (float) ((1.0 + alpha * A) / a0);
        c.b1 = (float) ((-2.0 * cw) / a0);
        c.b2 = (float) ((1.0 - alpha * A) / a0);
        c.a1 = (float) ((-2.0 * cw) / a0);
        c.a2 = (float) ((1.0 - alpha / A) / a0);
        return c;
    }

    inline Coeffs makeShelf (double fs, double f, double q, double gainDb, bool high) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
        const double cw = std::cos (w0);
        const double sw = std::sin (w0);
        // shelf slope S = 1; q only widens the transition a little
        const double S = 1.0;
        const double alpha = sw / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double beta = 2.0 * std::sqrt (A) * alpha;
        (void) q;

        const double a0 = (A + 1.0) + (A - 1.0) * cw + beta;
        Coeffs c;
        if (high)
        {
            c.b0 = (float) ((A * ((A + 1.0) + (A - 1.0) * cw + beta)) / a0);
            c.b1 = (float) ((-2.0 * A * ((A - 1.0) + (A + 1.0) * cw)) / a0);
            c.b2 = (float) ((A * ((A + 1.0) + (A - 1.0) * cw - beta)) / a0);
            c.a1 = (float) ((2.0 * ((A - 1.0) - (A + 1.0) * cw)) / a0);
            c.a2 = (float) (((A + 1.0) - (A - 1.0) * cw - beta) / a0);
        }
        else
        {
            c.b0 = (float) ((A * ((A + 1.0) - (A - 1.0) * cw + beta)) / a0);
            c.b1 = (float) ((2.0 * A * ((A - 1.0) - (A + 1.0) * cw)) / a0);
            c.b2 = (float) ((A * ((A + 1.0) - (A - 1.0) * cw - beta)) / a0);
            c.a1 = (float) ((-2.0 * ((A - 1.0) + (A + 1.0) * cw)) / a0);
            c.a2 = (float) (((A + 1.0) + (A - 1.0) * cw - beta) / a0);
        }
        return c;
    }

    inline Coeffs makePass (double fs, double f, double q, FilterType type) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
        const double cw = std::cos (w0);
        const double sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Coeffs c;
        c.a1 = (float) ((-2.0 * cw) / a0);
        c.a2 = (float) ((1.0 - alpha) / a0);

        switch (type)
        {
            case FilterType::highPass:
                c.b0 = (float) ((1.0 + cw) / 2.0 / a0);
                c.b1 = (float) (-(1.0 + cw) / a0);
                c.b2 = (float) ((1.0 + cw) / 2.0 / a0);
                break;
            case FilterType::lowPass:
                c.b0 = (float) ((1.0 - cw) / 2.0 / a0);
                c.b1 = (float) ((1.0 - cw) / a0);
                c.b2 = (float) ((1.0 - cw) / 2.0 / a0);
                break;
            case FilterType::notch:
                c.b0 = 1.0f / (float) a0;
                c.b1 = (float) ((-2.0 * cw) / a0);
                c.b2 = 1.0f / (float) a0;
                break;
            case FilterType::bandPass: // constant 0 dB peak gain
                c.b0 = (float) (alpha / a0);
                c.b1 = 0.0f;
                c.b2 = (float) (-alpha / a0);
                break;
            default:
                return makeBypass();
        }
        return c;
    }

    inline Coeffs makeCoeffs (FilterType type, double fs, double f, double q, double gainDb) noexcept
    {
        f = std::min (std::max (f, 10.0), std::min (20000.0, fs * 0.49));
        q = std::min (std::max (q, (double) kMinQ), (double) kMaxQ);

        switch (type)
        {
            case FilterType::bell:      return makeBell (fs, f, q, gainDb);
            case FilterType::lowShelf:  return makeShelf (fs, f, q, gainDb, false);
            case FilterType::highShelf: return makeShelf (fs, f, q, gainDb, true);
            case FilterType::highPass:
            case FilterType::lowPass:
            case FilterType::notch:
            case FilterType::bandPass:  return makePass (fs, f, q, type);
        }
        return makeBypass();
    }

    // Complex magnitude response of one section at frequency f (Hz), fs (Hz).
    inline std::complex<double> response (const Coeffs& c, double f, double fs) noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * f / fs;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
    }

    struct BiquadState
    {
        float z1 = 0.0f, z2 = 0.0f;

        inline float process (float x, const Coeffs& c) noexcept
        {
            const float y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }
    };
}
