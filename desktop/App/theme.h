#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace namfx {
namespace desktop {

class NAMTheme : public juce::LookAndFeel_V4 {
public:
    NAMTheme();

    static juce::Colour bg() { return juce::Colour(0xff0d1116); }
    static juce::Colour panel() { return juce::Colour(0xff151b23); }
    static juce::Colour panelHi() { return juce::Colour(0xff222b36); }
    static juce::Colour panelBorder() { return juce::Colour(0xff303b49); }
    static juce::Colour accent() { return juce::Colour(0xffb9ed4b); }
    static juce::Colour accentDim() { return juce::Colour(0xff6f9f32); }
    static juce::Colour accentHover() { return juce::Colour(0xffd3ff78); }
    static juce::Colour text() { return juce::Colour(0xffe7edf4); }
    static juce::Colour textDim() { return juce::Colour(0xff8290a0); }
    static juce::Colour textBright() { return juce::Colour(0xffffffff); }
    static juce::Colour danger() { return juce::Colour(0xffe74c3c); }

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
    void drawScrollBar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown);

    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getLabelFont(juce::Label&) override;
};

} // namespace desktop
} // namespace namfx
