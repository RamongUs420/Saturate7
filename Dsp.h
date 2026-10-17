#pragma once

#include <algorithm>
#include <cmath>

namespace s7
{
constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// Biquad en forma transpuesta directa II (doble precision, 2 canales)
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1[2] = { 0.0, 0.0 };
    double z2[2] = { 0.0, 0.0 };

    void reset() noexcept
    {
        z1[0] = z1[1] = z2[0] = z2[1] = 0.0;
    }

    inline double process (int c, double x) noexcept
    {
        const double y = b0 * x + z1[c];
        z1[c] = b1 * x - a1 * y + z2[c];
        z2[c] = b2 * x - a2 * y;
        return y;
    }

    void setLowpass (double f, double fs, double q = 0.70710678118654752) noexcept
    {
        f = std::clamp (f, 1.0, 0.49 * fs);
        const double w0 = 2.0 * kPi * f / fs;
        const double cw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - cw) * 0.5 / a0;
        b1 = (1.0 - cw) / a0;
        b2 = b0;
        a1 = -2.0 * cw / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void setHighpass (double f, double fs, double q = 0.70710678118654752) noexcept
    {
        f = std::clamp (f, 1.0, 0.49 * fs);
        const double w0 = 2.0 * kPi * f / fs;
        const double cw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 + cw) * 0.5 / a0;
        b1 = -(1.0 + cw) / a0;
        b2 = b0;
        a1 = -2.0 * cw / a0;
        a2 = (1.0 - alpha) / a0;
    }
};

//==============================================================================
// Crossover Linkwitz-Riley de 4o orden (24 dB/oct). low + high = all-pass.
struct Crossover
{
    Biquad lp1, lp2, hp1, hp2;

    void setup (double f, double fs) noexcept
    {
        lp1.setLowpass (f, fs);
        lp2 = lp1;
        hp1.setHighpass (f, fs);
        hp2 = hp1;
        reset();
    }

    void reset() noexcept
    {
        lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset();
    }

    inline void process (int c, double x, double& lo, double& hi) noexcept
    {
        lo = lp2.process (c, lp1.process (c, x));
        hi = hp2.process (c, hp1.process (c, x));
    }

    // Misma respuesta de fase que el crossover, magnitud plana.
    inline double allpass (int c, double x) noexcept
    {
        double lo, hi;
        process (c, x, lo, hi);
        return lo + hi;
    }
};

//==============================================================================
// Curvas de saturacion. Todas tienen pendiente ~1 en el origen y salida ~[-1, 1].
inline double shape (int curve, double x) noexcept
{
    switch (curve)
    {
        case 0:  // Tanh Soft
            return std::tanh (x);

        case 1:  // Hard Clip
            return x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x);

        case 2:  // Tube (asimetrica, armonicos pares)
            return x >= 0.0 ? 1.0 - std::exp (-x)
                            : -0.8 * (1.0 - std::exp (x / 0.8));

        case 3:  // Tape (sigmoide algebraica, compresion suave)
        {
            const double a = std::abs (x);
            return x / std::pow (1.0 + std::pow (a, 1.5), 1.0 / 1.5);
        }

        case 4:  // Asymmetric
            return x >= 0.0 ? std::tanh (x) : 0.5 * std::tanh (2.0 * x);

        case 5:  // Foldback (wavefolder)
            return std::abs (std::abs (std::fmod (x - 1.0, 4.0)) - 2.0) - 1.0;

        case 6:  // Arctan
            return 0.63661977236758134 * std::atan (1.5707963267948966 * x);

        case 7:  // Sine
        {
            const double h = 1.5707963267948966;
            if (x >= h)  return 1.0;
            if (x <= -h) return -1.0;
            return std::sin (x);
        }

        default:
            return x;
    }
}
} // namespace s7
