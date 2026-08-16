#include "desktop/App/tuner_meter.h"
#include "desktop/App/theme.h"

#include <algorithm>

namespace namfx {
namespace desktop {

TunerMeter::TunerMeter()
{
    setOpaque(true);
}

void TunerMeter::setDeviation(float cents, bool hasSignal, bool inTune)
{
    dev_ = cents;
    hasSignal_ = hasSignal;
    inTune_ = inTune;
    repaint();
}

void TunerMeter::paint(juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();
    g.fillAll(NAMTheme::panelHi());
    const int centreX = w / 2;
    const float scale = (static_cast<float>(w) / 2.0f - 8.0f) / 60.0f; // +/-60 ct
    const int zoneW = static_cast<int>(5.0f * scale);

    // in-tune zone (+/-5 ct): faint green band behind the scale
    g.setColour(NAMTheme::accent().withAlpha(0.14f));
    g.fillRect(centreX - zoneW, 2, zoneW * 2, h - 4);

    // tick marks every 10 ct
    g.setColour(NAMTheme::textDim());
    for (int c = -50; c <= 50; c += 10) {
        const int x = centreX + static_cast<int>(static_cast<float>(c) * scale);
        const int half = (c % 50 == 0) ? h / 3 : h / 6;
        g.drawVerticalLine(x, h / 2 - half, h / 2 + half);
    }

    // center line
    g.setColour(NAMTheme::text());
    g.drawVerticalLine(centreX, 2, h - 2);

    if (!hasSignal_) {
        // no signal: centred amber bar
        g.setColour(juce::Colour(0xffd59b45));
        g.fillRect(centreX - 2, 4, 4, h - 8);
        return;
    }

    const float clamped = std::clamp(dev_, -60.0f, 60.0f);
    const int x = centreX + static_cast<int>(clamped * scale);
    g.setColour(inTune_ ? NAMTheme::accent()
                        : (dev_ < 0.0f ? juce::Colour(0xffd59b45) : NAMTheme::danger()));
    g.fillRect(x - 4, 2, 8, h - 4);
    if (inTune_) {
        g.setColour(NAMTheme::accent());
        g.setFont(13.0f);
        g.drawText("TUNED", centreX + 16, h / 2 - 10, 70, 20, juce::Justification::centredLeft);
    }
}

} // namespace desktop
} // namespace namfx
