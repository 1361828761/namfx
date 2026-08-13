#pragma once

#include <chowdsp_wdf/chowdsp_wdf.h>

#include <cmath>

namespace namfx {

// 4th-order Butterworth lowpass (two cascaded biquads), fc = sampleRate / 4
// in the oversampled domain. Coefficients are fixed: with fc = fs_os / 4 the
// normalized cutoff is always pi/2, independent of the base sample rate.
class TsHalfbandLowpass {
public:
    void reset()
    {
        z1a_ = z2a_ = z1b_ = z2b_ = 0.0f;
    }

    inline float process(float x)
    {
        float y = b0A_ * x + z1a_;
        z1a_ = b1A_ * x - a1A_ * y + z2a_;
        z2a_ = b2A_ * x - a2A_ * y;

        y = b0B_ * y + z1b_;
        z1b_ = b1B_ * y - a1B_ * y + z2b_;
        z2b_ = b2B_ * y - a2B_ * y;
        return y;
    }

private:
    // Section A: Q = 1.3066, Section B: Q = 0.5412 (4th-order Butterworth split)
    static constexpr float b0A_ = 0.361607f;
    static constexpr float b1A_ = 0.723214f;
    static constexpr float b2A_ = 0.361607f;
    static constexpr float a1A_ = 0.0f;
    static constexpr float a2A_ = 0.446437f;

    static constexpr float b0B_ = 0.259884f;
    static constexpr float b1B_ = 0.519768f;
    static constexpr float b2B_ = 0.259884f;
    static constexpr float a1B_ = 0.0f;
    static constexpr float a2B_ = 0.0395505f;

    float z1a_ = 0.0f;
    float z2a_ = 0.0f;
    float z1b_ = 0.0f;
    float z2b_ = 0.0f;
};

// TS808 clipping amplifier, modeled as three cascaded WDF sub-networks
// (Duarte & Chowdhury decomposition, as used by TS-808-Ultra):
//   a: input high-pass network (1uF + 220R + 10k)  -> voltage in, voltage out
//   b: inverting input branch (4.7k + 47nF)        -> voltage in, current out
//   c: feedback network (current source || 51pF || 1N914 diode pair)
//                                                   -> current in, voltage out
// Runs at 2x oversampling to suppress aliasing from the diode nonlinearity.
class TsClippingStage {
public:
    void prepare(double sampleRate)
    {
        osRate_ = static_cast<float>(sampleRate) * 2.0f;
        wdfA_.prepare(osRate_);
        wdfB_.prepare(osRate_);
        wdfC_.prepare(osRate_);
        driveSmoothK_ = 1.0f - std::exp(-1.0f / (0.01f * osRate_));
        reset();
    }

    void reset()
    {
        wdfA_.reset();
        wdfB_.reset();
        wdfC_.reset();
        upFilter_.reset();
        downFilter_.reset();
        driveR_ = 0.0f;
    }

    void setDrive(float drive01)
    {
        // inverse-log audio taper, drive01 in [0, 1] -> pot resistance in [0, 500k]
        float taper = drive01 <= 0.5f ? 1.8f * drive01 : 0.2f * drive01 + 0.8f;
        driveTarget_ = taper * 500000.0f;
    }

    inline float processSample(float x)
    {
        driveR_ += driveSmoothK_ * (driveTarget_ - driveR_);
        wdfC_.setPotResistance(driveR_);

        const float up0 = upFilter_.process(x) * 2.0f;
        const float up1 = upFilter_.process(0.0f) * 2.0f;
        const float clipped0 = wdfC_.processSample(wdfB_.processSample(wdfA_.processSample(up0)));
        const float clipped1 = wdfC_.processSample(wdfB_.processSample(wdfA_.processSample(up1)));
        const float down0 = downFilter_.process(clipped0);
        const float down1 = downFilter_.process(clipped1);

        if (sampleIndex_) {
            sampleIndex_ = 0;
            return down1;
        }
        sampleIndex_ = 1;
        return down0;
    }

private:
    class WdfA {
    public:
        void prepare(float fs)
        {
            C2.prepare(fs);
        }

        void reset()
        {
            C2.reset();
        }

        inline float processSample(float x)
        {
            Vs.setVoltage(x);
            Vs.incident(S3.reflected());
            const float y = chowdsp::wdft::voltage<float>(R5);
            S3.incident(Vs.reflected());
            return y;
        }

    private:
        chowdsp::wdft::ResistorT<float> Rin{1.0f};
        chowdsp::wdft::ResistorT<float> RA{220.0f};
        chowdsp::wdft::ResistorT<float> R5{10000.0f};
        chowdsp::wdft::CapacitorT<float> C2{1.0e-6f};
        chowdsp::wdft::WDFSeriesT<float, decltype(RA), decltype(R5)> S1{RA, R5};
        chowdsp::wdft::WDFSeriesT<float, decltype(C2), decltype(S1)> S2{C2, S1};
        chowdsp::wdft::WDFSeriesT<float, decltype(Rin), decltype(S2)> S3{Rin, S2};
        chowdsp::wdft::IdealVoltageSourceT<float, decltype(S3)> Vs{S3};
    };

    class WdfB {
    public:
        void prepare(float fs)
        {
            C3.prepare(fs);
        }

        void reset()
        {
            C3.reset();
        }

        inline float processSample(float x)
        {
            Vs.setVoltage(x);
            Vs.incident(S1.reflected());
            const float y = chowdsp::wdft::current<float>(R4);
            S1.incident(Vs.reflected());
            return y;
        }

    private:
        chowdsp::wdft::ResistorT<float> R4{4700.0f};
        chowdsp::wdft::CapacitorT<float> C3{47.0e-9f};
        chowdsp::wdft::WDFSeriesT<float, decltype(C3), decltype(R4)> S1{C3, R4};
        chowdsp::wdft::IdealVoltageSourceT<float, decltype(S1)> Vs{S1};
    };

    class WdfC {
    public:
        void prepare(float fs)
        {
            C4.prepare(fs);
        }

        void reset()
        {
            C4.reset();
        }

        void setPotResistance(float potR)
        {
            Is.setResistanceValue(51000.0f + potR);
        }

        inline float processSample(float x)
        {
            Is.setCurrent(x);
            dp.incident(P1.reflected());
            const float y = chowdsp::wdft::voltage<float>(C4);
            P1.incident(dp.reflected());
            return y;
        }

    private:
        chowdsp::wdft::ResistiveCurrentSourceT<float> Is;
        chowdsp::wdft::CapacitorT<float> C4{51.0e-12f};
        chowdsp::wdft::WDFParallelT<float, decltype(Is), decltype(C4)> P1{Is, C4};
        chowdsp::wdft::DiodePairT<float, decltype(P1)> dp{P1, 25.0e-9f};
    };

    WdfA wdfA_;
    WdfB wdfB_;
    WdfC wdfC_;
    TsHalfbandLowpass upFilter_;
    TsHalfbandLowpass downFilter_;

    float osRate_ = 96000.0f;
    float driveSmoothK_ = 0.0f;
    float driveR_ = 0.0f;
    float driveTarget_ = 0.0f;
    int sampleIndex_ = 0;
};

} // namespace namfx
