#include "MyLNF.h"
#include "ModulatableSlider.h"

MyLNF::MyLNF()
{
    roboto = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedRegular_ttf,
                                                BinaryData::RobotoCondensedRegular_ttfSize);

    robotoBold = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf,
                                                    BinaryData::RobotoCondensedBold_ttfSize);

    setColour (TabbedButtonBar::tabOutlineColourId, Colour (0xFF595C6B));
}

Typeface::Ptr MyLNF::getTypefaceForFont (const Font& font)
{
    return font.isBold() ? robotoBold : roboto;
}

void MyLNF::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& s)
{
    int diameter = (width > height) ? height : width;
    if (diameter < 16)
        return;

    juce::Point<float> centre (x + std::floor (width * 0.5f + 0.5f), y + std::floor (height * 0.5f + 0.5f));
    diameter -= (diameter % 2 == 1) ? 9 : 8;
    float radius = (float) diameter * 0.5f;
    x = int (centre.x - radius);
    y = int (centre.y - radius);

    const auto bounds = juce::Rectangle<int> (x, y, diameter, diameter).toFloat();

    auto b = pointer->getBounds().toFloat();
    pointer->setTransform (AffineTransform::rotation (MathConstants<float>::twoPi * ((sliderPos - 0.5f) * 300.0f / 360.0f),
                                                      b.getCentreX(),
                                                      b.getCentreY()));

    const auto alpha = s.isEnabled() ? 1.0f : 0.4f;

    auto knobBounds = (bounds * 0.75f).withCentre (centre);
    knob->drawWithin (g, knobBounds, RectanglePlacement::stretchToFit, alpha);
    pointer->drawWithin (g, knobBounds, RectanglePlacement::stretchToFit, alpha);

    auto modSliderPos = sliderPos;
    if (auto* modSlider = dynamic_cast<ModulatableSlider*> (&s))
        modSliderPos = (float) modSlider->getModulatedPosition();

    const auto toAngle = rotaryStartAngle + modSliderPos * (rotaryEndAngle - rotaryStartAngle);
    constexpr float arcFactor = 0.9f;

    Path valueArc;
    valueArc.addPieSegment (bounds, rotaryStartAngle, rotaryEndAngle, arcFactor);
    g.setColour (Colour (0xff595c6b).withAlpha (alpha));
    g.fillPath (valueArc);
    valueArc.clear();

    valueArc.addPieSegment (bounds, rotaryStartAngle, toAngle, arcFactor);
    g.setColour (Colour (0xff9cbcbd).withAlpha (alpha));
    g.fillPath (valueArc);
}

void MyLNF::drawToggleButton (Graphics& g, ToggleButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto fontSize = jmin (15.0f, (float) button.getHeight() * 0.75f);
    auto tickWidth = fontSize * 1.1f;

    drawTickBox (g, button, 4.0f, ((float) button.getHeight() - tickWidth) * 0.5f, tickWidth, tickWidth, button.getToggleState(), button.isEnabled(), shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    g.setColour (button.findColour (ToggleButton::textColourId));
    g.setFont (Font (fontSize).boldened());

    if (! button.isEnabled())
        g.setOpacity (0.5f);

    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().withTrimmedLeft (roundToInt (tickWidth) + 10).withTrimmedRight (2),
                      Justification::centredLeft,
                      10);
}

void MyLNF::createTabTextLayout (const TabBarButton& button, float length, float depth, Colour colour, TextLayout& textLayout)
{
    Font font (depth * 0.45f, Font::bold);
    font.setUnderline (button.hasKeyboardFocus (false));

    AttributedString s;
    s.setJustification (Justification::centred);
    s.append (button.getButtonText().trim(), font, colour);

    textLayout.createLayout (s, length);
}

void MyLNF::drawTabButton (TabBarButton& button, Graphics& g, bool /*isMouseOver*/, bool /*isMouseDown*/)
{
    button.setViewportIgnoreDragFlag (true);
    const Rectangle<int> activeArea (button.getActiveArea());

    const TabbedButtonBar::Orientation o = button.getTabbedButtonBar().getOrientation();

    const Colour bkg (button.getTabBackgroundColour());

    if (button.getToggleState())
    {
        g.setColour (bkg);
    }
    else
    {
        Point<int> p1, p2;

        switch (o)
        {
            case TabbedButtonBar::TabsAtBottom:
                p1 = activeArea.getBottomLeft();
                p2 = activeArea.getTopLeft();
                break;
            case TabbedButtonBar::TabsAtTop:
                p1 = activeArea.getTopLeft();
                p2 = activeArea.getBottomLeft();
                break;
            case TabbedButtonBar::TabsAtRight:
                p1 = activeArea.getTopRight();
                p2 = activeArea.getTopLeft();
                break;
            case TabbedButtonBar::TabsAtLeft:
                p1 = activeArea.getTopLeft();
                p2 = activeArea.getTopRight();
                break;
            default:
                jassertfalse;
                break;
        }

        g.setGradientFill (ColourGradient (bkg.brighter (0.2f), p1.toFloat(), bkg.darker (0.1f), p2.toFloat(), false));
    }

    g.fillRect (activeArea);

    g.setColour (button.findColour (TabbedButtonBar::tabOutlineColourId));

    Rectangle<int> r (activeArea);
    g.fillRect (r.removeFromBottom (1));

    const float alpha = 0.6f;
    Colour col (bkg.contrasting().withMultipliedAlpha (alpha));

    if (button.isFrontTab())
        col = Colours::white;

    const Rectangle<float> area (button.getTextArea().toFloat());

    float length = area.getWidth();
    float depth = area.getHeight();

    if (button.getTabbedButtonBar().isVertical())
        std::swap (length, depth);

    TextLayout textLayout;
    createTabTextLayout (button, length, depth, col, textLayout);

    AffineTransform t;

    switch (o)
    {
        case TabbedButtonBar::TabsAtLeft:
            t = t.rotated (MathConstants<float>::pi * -0.5f).translated (area.getX(), area.getBottom());
            break;
        case TabbedButtonBar::TabsAtRight:
            t = t.rotated (MathConstants<float>::pi * 0.5f).translated (area.getRight(), area.getY());
            break;
        case TabbedButtonBar::TabsAtTop:
        case TabbedButtonBar::TabsAtBottom:
            t = t.translated (area.getX(), area.getY());
            break;
        default:
            jassertfalse;
            break;
    }

    g.addTransform (t);
    textLayout.draw (g, Rectangle<float> (length, depth));
}

Button* MyLNF::createTabBarExtrasButton()
{
    Rectangle<float> arrowZone (-10.0f, -10.0f, 120.0f, 120.0f);
    constexpr auto xPad = 25.0f;
    const auto arrowHeight = arrowZone.getHeight() / 3.0f;
    const auto yStart = arrowZone.getCentreY() - arrowHeight / 2.0f;
    const auto yEnd = arrowZone.getCentreY() + arrowHeight / 2.0f;

    Path path;
    path.startNewSubPath (arrowZone.getX() + xPad, yStart);
    path.lineTo (arrowZone.getCentreX(), yEnd);
    path.lineTo (arrowZone.getRight() - xPad, yStart);

    DrawablePath arrow;
    arrow.setPath (path);
    arrow.setFill (Colours::white);

    path.clear();
    path.addEllipse (arrowZone);

    DrawablePath background;
    background.setPath (path);
    background.setFill (findColour (TabbedComponent::ColourIds::backgroundColourId));

    DrawableComposite normalImage;
    normalImage.addAndMakeVisible (background.createCopy().release());
    normalImage.addAndMakeVisible (arrow.createCopy().release());

    arrow.setFill (Colours::white.darker());

    DrawableComposite overImage;
    overImage.addAndMakeVisible (background.createCopy().release());
    overImage.addAndMakeVisible (arrow.createCopy().release());

    auto db = new DrawableButton ("tabs", DrawableButton::ImageFitted);
    db->setImages (&normalImage, &overImage, nullptr);
    return db;
}

void MyLNF::drawLinearSlider (Graphics& g, int x, int y, int width, int height, float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/, const Slider::SliderStyle, Slider& slider)
{
    auto trackWidth = jmin (10.0f, slider.isHorizontal() ? (float) height * 0.25f : (float) width * 0.25f);

    Point<float> startPoint (slider.isHorizontal() ? (float) x : (float) x + (float) width * 0.5f,
                             slider.isHorizontal() ? (float) y + (float) height * 0.5f : (float) (height + y));

    Point<float> endPoint (slider.isHorizontal() ? (float) (width + x) : startPoint.x,
                           slider.isHorizontal() ? startPoint.y : (float) y);

    const auto alpha = slider.isEnabled() ? 1.0f : 0.4f;

    Path backgroundTrack;
    backgroundTrack.startNewSubPath (startPoint);
    backgroundTrack.lineTo (endPoint);
    g.setColour (slider.findColour (Slider::backgroundColourId).withAlpha (alpha));
    g.strokePath (backgroundTrack, { trackWidth, PathStrokeType::curved, PathStrokeType::rounded });

    Path valueTrack;
    Point<float> minPoint, maxPoint, modPoint;

    {
        auto kx = slider.isHorizontal() ? sliderPos : ((float) x + (float) width * 0.5f);
        auto ky = slider.isHorizontal() ? ((float) y + (float) height * 0.5f) : sliderPos;

        minPoint = startPoint;
        maxPoint = { kx, ky };

        modPoint = maxPoint;
        if (auto* modSlider = dynamic_cast<ModulatableSlider*> (&slider))
        {
            const auto modSliderPos = (float) modSlider->getModulatedPosition();
            auto kmx = slider.isHorizontal() ? (startPoint.x + (endPoint.x - startPoint.x) * modSliderPos) : ((float) x + (float) width * 0.5f);
            auto kmy = slider.isHorizontal() ? ((float) y + (float) height * 0.5f) : (startPoint.y + (endPoint.y - startPoint.y) * modSliderPos);

            modPoint = { kmx, kmy };
        }
    }

    auto thumbWidth = juce::jmax (trackWidth * 2.5f, (float) getSliderThumbRadius (slider));

    valueTrack.startNewSubPath (minPoint);
    valueTrack.lineTo (modPoint);
    g.setColour (slider.findColour (Slider::trackColourId).withAlpha (alpha));
    g.strokePath (valueTrack, { trackWidth, PathStrokeType::curved, PathStrokeType::rounded });

    auto thumbRect = Rectangle<float> (thumbWidth, thumbWidth)
                         .withCentre (maxPoint);
    knob->drawWithin (g, thumbRect, RectanglePlacement::stretchToFit, alpha);
}

Slider::SliderLayout MyLNF::getSliderLayout (Slider& slider)
{
    auto layout = LookAndFeel_V4::getSliderLayout (slider);

    auto style = slider.getSliderStyle();
    if (style == Slider::LinearHorizontal)
        layout.textBoxBounds = layout.textBoxBounds.withX (layout.sliderBounds.getX());

    return layout;
}

Label* MyLNF::createSliderTextBox (Slider& slider)
{
    struct ReturnFocusSliderLabel : public juce::Label
    {
    public:
        explicit ReturnFocusSliderLabel (Slider& baseSlider) : juce::Label ({}, {}),
                                                               slider (baseSlider)
        {
        }

        ~ReturnFocusSliderLabel() override = default;

        void textEditorReturnKeyPressed (TextEditor& editor) override
        {
            juce::Label::textEditorReturnKeyPressed (editor);
            slider.grabKeyboardFocus();
        }

        void textEditorEscapeKeyPressed (TextEditor& editor) override
        {
            juce::Label::textEditorEscapeKeyPressed (editor);
            slider.grabKeyboardFocus();
        }

        std::unique_ptr<AccessibilityHandler> createAccessibilityHandler() override
        {
            return createIgnoredAccessibilityHandler (*this);
        }

        Slider& slider;
    };

    auto* label = new ReturnFocusSliderLabel (slider);

    label->setJustificationType (Justification::centred);
    label->setKeyboardType (TextInputTarget::decimalKeyboard);

    label->setColour (Label::textColourId, slider.findColour (Slider::textBoxTextColourId));
    label->setColour (Label::backgroundColourId,
                      (slider.getSliderStyle() == Slider::LinearBar || slider.getSliderStyle() == Slider::LinearBarVertical)
                          ? Colours::transparentBlack
                          : slider.findColour (Slider::textBoxBackgroundColourId));
    label->setColour (Label::outlineColourId, slider.findColour (Slider::textBoxOutlineColourId));
    label->setColour (TextEditor::textColourId, slider.findColour (Slider::textBoxTextColourId));
    label->setColour (TextEditor::backgroundColourId,
                      slider.findColour (Slider::textBoxBackgroundColourId)
                          .withAlpha ((slider.getSliderStyle() == Slider::LinearBar || slider.getSliderStyle() == Slider::LinearBarVertical)
                                          ? 0.7f
                                          : 1.0f));
    label->setColour (TextEditor::outlineColourId, slider.findColour (Slider::textBoxOutlineColourId));
    label->setColour (TextEditor::highlightColourId, slider.findColour (Slider::textBoxHighlightColourId));

    auto style = slider.getSliderStyle();
    if (style == Slider::LinearHorizontal)
        label->setJustificationType (Justification::left);

    label->setFont ((float) slider.getTextBoxHeight());

    return label;
}

Component* MyLNF::getParentComponentForMenuOptions (const PopupMenu::Options& options)
{
#if JUCE_IOS
    if (PluginHostType::getPluginLoadedAs() == AudioProcessor::wrapperType_AudioUnitv3)
    {
        if (options.getParentComponent() == nullptr && options.getTargetComponent() != nullptr)
            return options.getTargetComponent()->getTopLevelComponent();
    }
#endif
    return LookAndFeel_V2::getParentComponentForMenuOptions (options);
}

PopupMenu::Options MyLNF::getOptionsForComboBoxPopupMenu (ComboBox& comboBox, Label& label)
{
    auto&& baseOptions = LookAndFeel_V4::getOptionsForComboBoxPopupMenu (comboBox, label);

#if JUCE_IOS
    return baseOptions.withParentComponent (comboBox.getTopLevelComponent());
#else
    return baseOptions;
#endif
}

//==============================================================
Font ComboBoxLNF::getComboBoxFont (ComboBox& box)
{
    return { juce::jmin (28.0f, (float) box.proportionOfHeight (0.48f)) };
}

void ComboBoxLNF::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox& box)
{
    auto cornerSize = 5.0f;
    Rectangle<int> boxBounds (0, 0, width, height);

    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (boxBounds.toFloat(), cornerSize);

    if (box.getName().isNotEmpty())
    {
        g.setColour (Colours::white);
        g.setFont (getComboBoxFont (box).boldened());
        auto nameBox = boxBounds.withWidth (boxBounds.proportionOfWidth (0.7f));
        g.drawFittedText (box.getName() + ": ", nameBox, Justification::right, 1);
    }
}

void ComboBoxLNF::positionComboBoxText (ComboBox& box, Label& label)
{
    auto b = box.getBounds().withPosition ({});

    if (box.getName().isNotEmpty())
    {
        auto width = b.proportionOfWidth (0.3f);
        auto x = b.proportionOfWidth (0.7f);
        b = b.withX (x).withWidth (width);
    }

    label.setBounds (b);
    label.setFont (getComboBoxFont (box).boldened());
}
