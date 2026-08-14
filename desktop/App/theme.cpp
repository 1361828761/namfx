#include "desktop/App/theme.h"

namespace namfx {
namespace desktop {

NAMTheme::NAMTheme()
{
    // V4 colour scheme drives most default widgets; the overrides below
    // polish the common ones
    juce::LookAndFeel_V4::ColourScheme s = getCurrentColourScheme();
    using U = juce::LookAndFeel_V4::ColourScheme::UIColour;
    s.setUIColour(U::windowBackground, bg());
    s.setUIColour(U::widgetBackground, panel());
    s.setUIColour(U::menuBackground, panel());
    s.setUIColour(U::outline, juce::Colour(0xff3a4150));
    s.setUIColour(U::defaultText, text());
    s.setUIColour(U::defaultFill, panel());
    s.setUIColour(U::highlightedText, text());
    s.setUIColour(U::highlightedFill, accentDim());
    s.setUIColour(U::menuText, text());
    setColourScheme(s);

    setColour(juce::TextButton::buttonColourId, panelHi());
    setColour(juce::TextButton::textColourOffId, text());
    setColour(juce::TextButton::textColourOnId, accent());
    setColour(juce::ComboBox::backgroundColourId, panel());
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a4150));
    setColour(juce::ComboBox::textColourId, text());
    setColour(juce::ComboBox::arrowColourId, textDim());
    setColour(juce::PopupMenu::backgroundColourId, panel());
    setColour(juce::PopupMenu::textColourId, text());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentDim());
    setColour(juce::Label::textColourId, text());
    setColour(juce::Label::textWhenEditingColourId, text());
    setColour(juce::Slider::trackColourId, accentDim());
    setColour(juce::Slider::thumbColourId, accent());
    setColour(juce::ToggleButton::textColourId, text());
    setColour(juce::TextEditor::backgroundColourId, panel());
    setColour(juce::TextEditor::textColourId, text());
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff3a4150));
    setColour(juce::TextEditor::focusedOutlineColourId, accentDim());
    setColour(juce::ScrollBar::thumbColourId, panelHi());
}

void NAMTheme::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                    const juce::Colour& /*backgroundColour*/,
                                    bool isHighlighted, bool isDown)
{
    const juce::Colour base = isDown ? accentDim() : (isHighlighted ? panelHi() : panel());
    g.setColour(base);
    g.fillRoundedRectangle(b.getLocalBounds().toFloat().reduced(1.0f), 4.0f);
}

void NAMTheme::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/, int,
                            int, int, int, juce::ComboBox& box)
{
    g.setColour(panel());
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width),
                           static_cast<float>(height), 4.0f);
    g.setColour(juce::Colour(0xff3a4150));
    g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(width - 1),
                           static_cast<float>(height - 1), 4.0f, 1.0f);
    // arrow
    g.setColour(textDim());
    juce::Path p;
    p.startNewSubPath(static_cast<float>(width - 14), static_cast<float>(height) / 2.0f - 2.0f);
    p.lineTo(static_cast<float>(width - 9), static_cast<float>(height) / 2.0f + 2.0f);
    p.lineTo(static_cast<float>(width - 4), static_cast<float>(height) / 2.0f - 2.0f);
    g.strokePath(p, juce::PathStrokeType(1.5f));
}

void NAMTheme::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                float sliderPos, float /*minPos*/, float /*maxPos*/,
                                const juce::Slider::SliderStyle /*style*/, juce::Slider&)
{
    const float cy = static_cast<float>(y) + static_cast<float>(height) / 2.0f;
    // track
    g.setColour(juce::Colour(0xff2a2e38));
    g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, static_cast<float>(width), 4.0f,
                           2.0f);
    // filled portion up to the thumb
    g.setColour(accentDim());
    g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, sliderPos - static_cast<float>(x),
                           4.0f, 2.0f);
    // thumb
    g.setColour(accent());
    g.fillRoundedRectangle(sliderPos - 4.0f, cy - 6.0f, 8.0f, 12.0f, 3.0f);
}

void NAMTheme::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
                                bool /*shouldDrawButtonAsHighlighted*/,
                                bool /*shouldDrawButtonAsDown*/)
{
    const bool on = b.getToggleState();
    const juce::Colour base = on ? accent() : juce::Colour(0xff2a2e38);
    g.setColour(base);
    g.fillRoundedRectangle(1.0f, 3.0f, 10.0f, 10.0f, 3.0f);
    g.setColour(on ? juce::Colour(0xff14161a) : textDim());
    g.fillEllipse(on ? 6.0f : 2.5f, 5.5f, 5.0f, 5.0f);
    g.setColour(text());
    g.setFont(13.0f);
    g.drawText(b.getButtonText(), 16.0f, 0.0f, static_cast<float>(b.getWidth()) - 18.0f,
               static_cast<float>(b.getHeight()), juce::Justification::centredLeft);
}

void NAMTheme::drawTextEditorOutline(juce::Graphics& g, int width, int height,
                                     juce::TextEditor& editor)
{
    if (editor.isEnabled()) {
        g.setColour(editor.hasKeyboardFocus(true) ? accentDim()
                                                  : juce::Colour(0xff3a4150));
        g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(width - 1),
                               static_cast<float>(height - 1), 4.0f, 1.0f);
    }
}

} // namespace desktop
} // namespace namfx
