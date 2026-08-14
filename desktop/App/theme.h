#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace namfx {
namespace desktop {

// Dark editor theme (web-UI-like): near-black background, raised panels,
// one accent colour, muted section labels. Applied app-wide.
class NAMTheme : public juce::LookAndFeel_V4 {
public:
    NAMTheme();

    static juce::Colour bg() { return juce::Colour(0xff14161a); }
    static juce::Colour panel() { return juce::Colour(0xff1e2128); }
    static juce::Colour panelHi() { return juce::Colour(0xff2a2e38); }
    static juce::Colour accent() { return juce::Colour(0xff4cc9f0); }
    static juce::Colour accentDim() { return juce::Colour(0xff2a6f8a); }
    static juce::Colour text() { return juce::Colour(0xffd7dde6); }
    static juce::Colour textDim() { return juce::Colour(0xff7a8494); }

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool isHighlighted, bool isDown) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int, int, int,
                      int, juce::ComboBox&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float minPos, float maxPos, const juce::Slider::SliderStyle,
                          juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height,
                               juce::TextEditor&) override;
};

} // namespace desktop
} // namespace namfx
