#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace namfx {
namespace desktop {

// Graphical tuner needle: deviation in cents is drawn as a bar that moves
// left (flat) / right (sharp) of center; inside +/-5 cents the bar turns
// green and a TUNED marker appears. No signal shows a centred amber bar.
class TunerMeter : public juce::Component {
public:
    TunerMeter();

    void setDeviation(float cents, bool hasSignal, bool inTune);

    void paint(juce::Graphics&) override;

private:
    float dev_ = 0.0f;
    bool hasSignal_ = false;
    bool inTune_ = false;
};

} // namespace desktop
} // namespace namfx
