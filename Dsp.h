#pragma once
#include <cmath>

// Kis, JUCE-mentes DSP építőkövek. Nincs allokáció, nincs heap: audio szálon biztonságos.
namespace a2b
{
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void reset() { z1 = z2 = 0.0f; }

        inline float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void setLowPass (double fs, double f, double q = 0.70710678)
        {
            const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
            const double c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
            set ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + al, -2.0 * c, 1.0 - al);
        }

        void setHighPass (double fs, double f, double q = 0.70710678)
        {
            const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
            const double c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
            set ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + al, -2.0 * c, 1.0 - al);
        }

        void setBandPass (double fs, double f, double q)
        {
            const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
            const double c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
            set (al, 0.0, -al, 1.0 + al, -2.0 * c, 1.0 - al);
        }

        void setPeak (double fs, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
            const double c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
            set (1.0 + al * A, -2.0 * c, 1.0 - al * A, 1.0 + al / A, -2.0 * c, 1.0 - al / A);
        }

        void setLowShelf (double fs, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
            const double c = std::cos (w0), s = std::sin (w0);
            const double beta = 2.0 * std::sqrt (A) * (s / (2.0 * q));
            set (A * ((A + 1.0) - (A - 1.0) * c + beta),
                 2.0 * A * ((A - 1.0) - (A + 1.0) * c),
                 A * ((A + 1.0) - (A - 1.0) * c - beta),
                 (A + 1.0) + (A - 1.0) * c + beta,
                 -2.0 * ((A - 1.0) + (A + 1.0) * c),
                 (A + 1.0) + (A - 1.0) * c - beta);
        }

    private:
        void set (double B0, double B1, double B2, double A0, double A1, double A2)
        {
            b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
            a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
        }
    };

    // Egypólusú DC-blokkoló (kb. 8 Hz)
    struct DcBlock
    {
        float x1 = 0.0f, y1 = 0.0f, r = 0.999f;
        void prepare (double fs, double fc = 8.0) { r = (float) std::exp (-2.0 * 3.14159265358979323846 * fc / fs); x1 = y1 = 0.0f; }
        inline float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    };
}
