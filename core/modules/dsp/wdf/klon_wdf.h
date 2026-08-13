#pragma once

#include "halfband.h"

#include <chowdsp_wdf/chowdsp_wdf.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace namfx {

// Klon-style diode pair: reverse-parallel pair with Is = 15uA (vs ~25nA for a
// 1N914) giving a much higher, softer clipping knee - the "transparent" sound.
// For quiet signals the Wright Omega approximation has errors near zero, so a
// small LUT is used there (built on first construction, which is the load path
// off the audio thread; never built inside the audio callback).
template <typename T, typename Next>
class KlonDiodePair final : public chowdsp::wdft::RootWDF {
public:
    explicit KlonDiodePair(Next& next) : next_(next)
    {
        next_.connectToParent(this);
        calcImpedance();
        (void)makeLut();
    }

    void calcImpedance() override
    {
        R_Is_ = next_.wdf.R * is_;
        logR_Is_overVt_ = std::log(R_Is_ / vt_);
    }

    inline void incident(T x) noexcept
    {
        a_ = x;
    }

    inline T reflected() noexcept
    {
        const T lambda = a_ < (T)0.0 ? (T)-1.0 : (T)1.0;
        const T u = logR_Is_overVt_ + lambda * a_ / vt_ + R_Is_ / vt_;
        b_ = a_ + (T)2.0 * lambda * (R_Is_ - vt_ * wrightOmega(u));
        return b_;
    }

    chowdsp::wdft::WDFMembers<T> wdf;

private:
    static T wrightOmega(T x)
    {
        if (std::fabs(x) > 0.5f) {
            return chowdsp::Omega::omega4(x);
        }
        return lutLookup(x);
    }

    static T lutLookup(T x)
    {
        const auto& lut = makeLut();
        constexpr int kSize = 4096;
        constexpr T kSpan = (T)1.0;
        const T t = (x + kSpan) * (T)0.5 * static_cast<T>(kSize - 1);
        int idx = static_cast<int>(t);
        if (idx < 0) {
            return lut[0];
        }
        if (idx >= kSize - 1) {
            return lut[kSize - 1];
        }
        const T frac = t - static_cast<T>(idx);
        return static_cast<T>(lut[static_cast<std::size_t>(idx)]) * ((T)1.0 - frac) +
               static_cast<T>(lut[static_cast<std::size_t>(idx) + 1]) * frac;
    }

    // wrightOmega(x) = W(e^x): solves w + ln(w) = x by Newton iteration
    static T solveWrightOmega(T x)
    {
        T w = std::max<T>(x, (T)0.0);
        for (int i = 0; i < 24; ++i) {
            const T f = w + std::log(w) - x;
            const T fp = (T)1.0 + (T)1.0 / w;
            const T next = w - f / fp;
            if (std::fabs(next - w) < (T)1.0e-9) {
                return next;
            }
            w = next;
        }
        return w;
    }

    static const std::array<double, 4096>& makeLut()
    {
        static const std::array<double, 4096> lut = [] {
            std::array<double, 4096> table{};
            for (std::size_t i = 0; i < table.size(); ++i) {
                const double x = -1.0 + 2.0 * static_cast<double>(i) / static_cast<double>(table.size() - 1);
                table[i] = solveWrightOmega(static_cast<T>(x));
            }
            return table;
        }();
        return lut;
    }

    static constexpr T is_ = (T)15.0e-6;
    static constexpr T vt_ = (T)0.02585;

    Next& next_;
    T R_Is_ = (T)0.0;
    T logR_Is_overVt_ = (T)0.0;
    T a_ = (T)0.0;
    T b_ = (T)0.0;
};

// Klon clipping stage: feedback network with the diode pair, runs at 2x
// oversampling. Topology (from ChowCentaur ClippingStage):
//   D23(root) -> P1 = S2 || S3, S2 = (Vin -> I1 invert -> C9) + R13,
//   S3 = C10 + Vbias(47k). Output = current through C10.
class KlonClipping {
public:
    void prepare(float sampleRate)
    {
        osRate_ = static_cast<float>(sampleRate) * 2.0f;
        C9.prepare(osRate_);
        C10.prepare(osRate_);
        reset();
    }

    void reset()
    {
        C9.reset();
        C10.reset();
        upFilter_.reset();
        downFilter_.reset();
    }

    inline float processSample(float x)
    {
        const float up0 = upFilter_.process(x) * 2.0f;
        const float up1 = upFilter_.process(0.0f) * 2.0f;
        downFilter_.process(processClipped(up0));
        return downFilter_.process(processClipped(up1));
    }

private:
    inline float processClipped(float x)
    {
        Vin.setVoltage(x);
        D23.incident(P1.reflected());
        P1.incident(D23.reflected());
        return chowdsp::wdft::current<float>(C10);
    }

    using ResVs = chowdsp::wdft::ResistiveVoltageSourceT<double>;
    using Capacitor = chowdsp::wdft::CapacitorT<double>;
    using Resistor = chowdsp::wdft::ResistorT<double>;

    ResVs Vin;
    Capacitor C9{1.0e-6};
    Resistor R13{1000.0};
    Capacitor C10{1.0e-6};
    ResVs Vbias{47000.0};

    chowdsp::wdft::PolarityInverterT<double, ResVs> I1{Vin};
    chowdsp::wdft::WDFSeriesT<double, decltype(I1), Capacitor> S1{I1, C9};
    chowdsp::wdft::WDFSeriesT<double, decltype(S1), Resistor> S2{S1, R13};
    chowdsp::wdft::WDFSeriesT<double, Capacitor, ResVs> S3{C10, Vbias};
    chowdsp::wdft::WDFParallelT<double, decltype(S2), decltype(S3)> P1{S2, S3};
    KlonDiodePair<double, decltype(P1)> D23{P1};

    HalfbandLowpass upFilter_;
    HalfbandLowpass downFilter_;
    float osRate_ = 96000.0f;
};

// Klon preamp: input buffer with bias network and high-pass. FF1 side chain
// takes the current through Vbias2 (15k).
class KlonPreAmp {
public:
    void prepare(float sampleRate)
    {
        C3.prepare(sampleRate);
        C5.prepare(sampleRate);
        C16.prepare(sampleRate);
        reset();
    }

    void reset()
    {
        C3.reset();
        C5.reset();
        C16.reset();
    }

    void setGain(float gain)
    {
        Vbias.setResistanceValue(static_cast<double>(gain) * 100.0e3);
    }

    inline float processSample(float x)
    {
        Vin.setVoltage(x);
        Vin.incident(I1.reflected());
        const float y = static_cast<float>(
            chowdsp::wdft::voltage<double>(Vbias) + chowdsp::wdft::voltage<double>(R6));
        I1.incident(Vin.reflected());
        ff1_ = static_cast<float>(chowdsp::wdft::current<double>(Vbias2));
        return y;
    }

    inline float getFF1() const { return ff1_; }

private:
    using Capacitor = chowdsp::wdft::CapacitorT<double>;
    using Resistor = chowdsp::wdft::ResistorT<double>;
    using ResVs = chowdsp::wdft::ResistiveVoltageSourceT<double>;

    Capacitor C3{0.1e-6};
    Capacitor C5{68.0e-9};
    Capacitor C16{1.0e-6};
    Resistor R6{10000.0};
    Resistor R7{1500.0};
    ResVs Vbias2{15000.0};
    ResVs Vbias{50000.0};

    chowdsp::wdft::WDFParallelT<double, Capacitor, Resistor> P1{C5, R6};
    chowdsp::wdft::WDFSeriesT<double, decltype(P1), ResVs> S1{P1, Vbias};
    chowdsp::wdft::WDFParallelT<double, ResVs, Capacitor> P2{Vbias2, C16};
    chowdsp::wdft::WDFSeriesT<double, decltype(P2), Resistor> S2{P2, R7};
    chowdsp::wdft::WDFParallelT<double, decltype(S1), decltype(S2)> P3{S1, S2};
    chowdsp::wdft::WDFSeriesT<double, decltype(P3), Capacitor> S3{P3, C3};
    chowdsp::wdft::PolarityInverterT<double, decltype(S3)> I1{S3};
    chowdsp::wdft::IdealVoltageSourceT<double, decltype(I1)> Vin{I1};

    float ff1_ = 0.0f;
};

// Klon feed-forward network 2 (from ChowCentaur FeedForward2). Outputs the
// current through R16; the pot RV is split top/bottom by the gain control.
class KlonFeedForward2 {
public:
    void prepare(float sampleRate)
    {
        C4.prepare(sampleRate);
        C6.prepare(sampleRate);
        C11.prepare(sampleRate);
        C12.prepare(sampleRate);
        reset();
    }

    void reset()
    {
        C4.reset();
        C6.reset();
        C11.reset();
        C12.reset();
    }

    void setGain(float gain)
    {
        RVTop.setResistanceValue(std::max(static_cast<double>(gain) * 100.0e3, 1.0));
        RVBot.setResistanceValue(std::max((1.0 - static_cast<double>(gain)) * 100.0e3, 1.0));
    }

    inline float processSample(float x)
    {
        Vin.setVoltage(x);
        Vin.incident(I1.reflected());
        const float y = static_cast<float>(chowdsp::wdft::current<double>(R16));
        I1.incident(Vin.reflected());
        return y;
    }

private:
    using Capacitor = chowdsp::wdft::CapacitorT<double>;
    using Resistor = chowdsp::wdft::ResistorT<double>;
    using ResVs = chowdsp::wdft::ResistiveVoltageSourceT<double>;

    Resistor R5{5100.0};
    Resistor R8{1500.0};
    Resistor R9{1000.0};
    Resistor RVTop{50000.0};
    Resistor RVBot{50000.0};
    Resistor R15{22000.0};
    Resistor R16{47000.0};
    Resistor R17{27000.0};
    Resistor R18{12000.0};
    ResVs Vbias;
    Capacitor C4{68.0e-9};
    Capacitor C6{390.0e-9};
    Capacitor C11{2.2e-9};
    Capacitor C12{27.0e-9};

    chowdsp::wdft::WDFSeriesT<double, Capacitor, Resistor> S1{C12, R18};
    chowdsp::wdft::WDFParallelT<double, decltype(S1), Resistor> P1{S1, R17};
    chowdsp::wdft::WDFSeriesT<double, Capacitor, Resistor> S2{C11, R15};
    chowdsp::wdft::WDFSeriesT<double, decltype(S2), Resistor> S3{S2, R16};
    chowdsp::wdft::WDFParallelT<double, decltype(S3), decltype(P1)> P2{S3, P1};
    chowdsp::wdft::WDFParallelT<double, decltype(P2), Resistor> P3{P2, RVBot};
    chowdsp::wdft::WDFSeriesT<double, decltype(P3), Resistor> S4{P3, RVTop};
    chowdsp::wdft::WDFSeriesT<double, Capacitor, Resistor> S5{C6, R9};
    chowdsp::wdft::WDFParallelT<double, decltype(S4), decltype(S5)> P4{S4, S5};
    chowdsp::wdft::WDFParallelT<double, decltype(P4), Resistor> P5{P4, R8};
    chowdsp::wdft::WDFSeriesT<double, decltype(P5), ResVs> S6{P5, Vbias};
    chowdsp::wdft::WDFParallelT<double, Resistor, Capacitor> P6{R5, C4};
    chowdsp::wdft::WDFSeriesT<double, decltype(P6), decltype(S6)> S7{P6, S6};
    chowdsp::wdft::PolarityInverterT<double, decltype(S7)> I1{S7};
    chowdsp::wdft::IdealVoltageSourceT<double, decltype(I1)> Vin{I1};
};

} // namespace namfx
