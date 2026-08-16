#include "desktop/App/theme.h"

namespace namfx {
namespace desktop {

NAMTheme::NAMTheme()
{
    juce::LookAndFeel_V4::ColourScheme s = getCurrentColourScheme();
    using U = juce::LookAndFeel_V4::ColourScheme::UIColour;
    s.setUIColour(U::windowBackground, bg());
    s.setUIColour(U::widgetBackground, panel());
    s.setUIColour(U::menuBackground, panelHi());
    s.setUIColour(U::outline, panelBorder());
    s.setUIColour(U::defaultText, text());
    s.setUIColour(U::defaultFill, panel());
    s.setUIColour(U::highlightedText, textBright());
    s.setUIColour(U::highlightedFill, accentDim());
    s.setUIColour(U::menuText, text());
    setColourScheme(s);

    setColour(juce::TextButton::buttonColourId, panelHi());
    setColour(juce::TextButton::textColourOffId, text());
    setColour(juce::TextButton::textColourOnId, accent());
    setColour(juce::ComboBox::backgroundColourId, panel());
    setColour(juce::ComboBox::outlineColourId, panelBorder());
    setColour(juce::ComboBox::textColourId, text());
    setColour(juce::ComboBox::arrowColourId, textDim());
    setColour(juce::PopupMenu::backgroundColourId, panelHi());
    setColour(juce::PopupMenu::textColourId, text());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentDim());
    setColour(juce::Label::textColourId, text());
    setColour(juce::Label::textWhenEditingColourId, text());
    setColour(juce::Slider::trackColourId, panelHi());
    setColour(juce::Slider::backgroundColourId, panel());
    setColour(juce::Slider::thumbColourId, accent());
    setColour(juce::ToggleButton::textColourId, text());
    setColour(juce::TextEditor::backgroundColourId, panel());
    setColour(juce::TextEditor::textColourId, text());
    setColour(juce::TextEditor::outlineColourId, panelBorder());
    setColour(juce::TextEditor::focusedOutlineColourId, accentDim());
    setColour(juce::ScrollBar::thumbColourId, panelHi());
    setColour(juce::ListBox::backgroundColourId, panel());
    setColour(juce::ListBox::outlineColourId, panelBorder());
}

void NAMTheme::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                    const juce::Colour& /*backgroundColour*/,
                                    bool isHighlighted, bool isDown)
{
    auto bounds = b.getLocalBounds().toFloat().reduced(1.0f);
    juce::Colour base = isDown ? accentDim() : (isHighlighted ? panelHi() : panel());
    if (b.getToggleState() && b.isEnabled()) {
        base = isDown ? accentDim() : (isHighlighted ? accentHover() : accent());
    }
    g.setColour(base);
    g.fillRoundedRectangle(bounds, 7.0f);
    if (isHighlighted && !b.getToggleState()) {
        g.setColour(panelBorder());
        g.drawRoundedRectangle(bounds, 7.0f, 1.0f);
    }
}

void NAMTheme::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/, int,
                            int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width - 1),
                                         static_cast<float>(height - 1));
    g.setColour(panel());
    g.fillRoundedRectangle(bounds, 7.0f);
    g.setColour(box.hasKeyboardFocus(true) ? accentDim() : panelBorder());
    g.drawRoundedRectangle(bounds, 7.0f, 1.0f);
    g.setColour(textDim());
    juce::Path p;
    p.startNewSubPath(static_cast<float>(width - 16), static_cast<float>(height) / 2.0f - 2.0f);
    p.lineTo(static_cast<float>(width - 11), static_cast<float>(height) / 2.0f + 2.0f);
    p.lineTo(static_cast<float>(width - 6), static_cast<float>(height) / 2.0f - 2.0f);
    g.strokePath(p, juce::PathStrokeType(1.5f));
}

void NAMTheme::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                float sliderPos, float /*minPos*/, float /*maxPos*/,
                                const juce::Slider::SliderStyle /*style*/, juce::Slider&)
{
    const float cy = static_cast<float>(y) + static_cast<float>(height) / 2.0f;
    g.setColour(panelHi());
    g.fillRoundedRectangle(static_cast<float>(x), cy - 3.0f, static_cast<float>(width), 6.0f, 3.0f);
    g.setColour(accentDim());
    g.fillRoundedRectangle(static_cast<float>(x), cy - 3.0f, sliderPos - static_cast<float>(x),
                           6.0f, 3.0f);
    g.setColour(accent());
    g.fillRoundedRectangle(sliderPos - 5.0f, cy - 8.0f, 10.0f, 16.0f, 4.0f);
}

void NAMTheme::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
                                bool /*shouldDrawButtonAsHighlighted*/,
                                bool /*shouldDrawButtonAsDown*/)
{
    const bool on = b.getToggleState();
    const float h = static_cast<float>(b.getHeight());
    const float cy = h / 2.0f;
    const bool highlighted = b.isMouseOver();
    const float trackW = 32.0f;
    const float trackH = 18.0f;
    if (on) {
        g.setColour(highlighted ? accentHover() : accent());
        g.fillRoundedRectangle(1.0f, cy - trackH / 2.0f, trackW, trackH, trackH / 2.0f);
        g.setColour(textBright());
        g.fillEllipse(19.0f, cy - 6.0f, 12.0f, 12.0f);
    } else {
        g.setColour(highlighted ? panelBorder() : panelHi());
        g.fillRoundedRectangle(1.0f, cy - trackH / 2.0f, trackW, trackH, trackH / 2.0f);
        g.setColour(textDim());
        g.fillEllipse(3.0f, cy - 6.0f, 12.0f, 12.0f);
    }
    g.setColour(text());
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    g.drawText(b.getButtonText(), 42.0f, 0.0f, static_cast<float>(b.getWidth()) - 44.0f,
               h, juce::Justification::centredLeft);
}

void NAMTheme::drawTextEditorOutline(juce::Graphics& g, int width, int height,
                                     juce::TextEditor& editor)
{
    if (editor.isEnabled()) {
        g.setColour(editor.hasKeyboardFocus(true) ? accentDim() : panelBorder());
        g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(width - 1),
                               static_cast<float>(height - 1), 6.0f, 1.0f);
    }
}

void NAMTheme::drawScrollBar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width,
                             int height, bool /*isScrollbarVertical*/, int thumbStartPosition,
                             int thumbSize, bool isMouseOver, bool /*isMouseDown*/)
{
    g.setColour(panelBorder());
    g.fillRect(x, y, width, height);
    juce::Colour thumb = isMouseOver ? accentDim() : panelHi();
    g.setColour(thumb);
    g.fillRoundedRectangle(static_cast<float>(x + 2), static_cast<float>(thumbStartPosition + 2),
                           static_cast<float>(width - 4), static_cast<float>(std::max(thumbSize - 4, 4)),
                           3.0f);
}

juce::Font NAMTheme::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions().withHeight(13.0f));
}

juce::Font NAMTheme::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions().withHeight(12.0f));
}

} // namespace desktop
} // namespace namfx
