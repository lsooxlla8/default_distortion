#include "PluginEditor.h"
#include "DefaultDistortionFonts.h"
#include "UILayout.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace dd
{
namespace
{
const auto lightPalette = juce::Colour (0xfff6f6f6);
const auto darkPalette = juce::Colour (0xff050505);
constexpr std::array<float, MultibandParameters::maximumCrossovers>
    defaultCrossoverFrequencies { 100.0f, 500.0f, 2000.0f };
const std::array<juce::String, DistortionEngine::modeCount>
    prototypeModeNamesByDisplay {
        "SOFT CLIP", "HARD CLIP", "DIODE", "TRIODE", "TRANSISTOR",
        "TAPE", "ODD / EVEN", "PHASE DISTORTION", "SPECTRAL CLIP",
        "SINE EROSION", "SIGN / SQUARE", "ZERO-SQUARE",
        "FULL-WAVE RECTIFIER", "SOFT FULL-WAVE", "TRANSFORMER CORE",
        "CLASS-B SATURATION", "TOPOLOGY FOLD", "RECURSIVE FOLDBACK",
        "SINE FOLD", "CHEBYSHEV FOLD", "MODULO WRAP", "DOWNSAMPLE",
        "BIT CRUSHER", "BIT ROTATION", "DELTA CRUSHER", "SLEW LIMITER",
        "SCHMITT HYSTERESIS", "FEEDBACK SATURATOR",
        "RESONANT FEEDBACK CLIP", "DYNAMIC SAG"
    };

juce::PropertiesFile& themeProperties()
{
    static juce::PropertiesFile properties ([]
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "default_distortion-ui";
        options.filenameSuffix = "settings";
        options.folderName = "icanseesounds";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = 0;
        return options;
    }());
    return properties;
}

bool loadLightTheme()
{
    return themeProperties().getBoolValue ("lightTheme", true);
}

void saveLightTheme (bool light)
{
    auto& properties = themeProperties();
    properties.setValue ("lightTheme", light);
    properties.saveIfNeeded();
}

juce::Colour foregroundOf (const juce::Component& component)
{
    return component.findColour (
        GeometricLookAndFeel::foregroundColourId);
}

juce::Colour backgroundOf (const juce::Component& component)
{
    return component.findColour (
        GeometricLookAndFeel::backgroundColourId);
}

juce::Colour mutedOf (const juce::Component& component)
{
    return component.findColour (
        GeometricLookAndFeel::mutedColourId);
}

float scaleOf (const juce::Component& component) noexcept
{
    if (const auto* look = dynamic_cast<const GeometricLookAndFeel*> (
            &component.getLookAndFeel()))
        return look->getUiScale();
    return 1.0f;
}

juce::Font monoFont (float height, bool bold = false)
{
    static const auto medium = juce::Typeface::createSystemTypefaceFor (
        DefaultDistortionFonts::JetBrainsMonoMedium_ttf,
        DefaultDistortionFonts::JetBrainsMonoMedium_ttfSize);
    static const auto extraBold = juce::Typeface::createSystemTypefaceFor (
        DefaultDistortionFonts::JetBrainsMonoExtraBold_ttf,
        DefaultDistortionFonts::JetBrainsMonoExtraBold_ttfSize);
    return juce::Font (
        juce::FontOptions (bold ? extraBold : medium)
            .withPointHeight (height)
            .withFallbackEnabled (false));
}

juce::Font trackedMonoFont (float fontSize,
                            bool bold,
                            float letterSpacingEm,
                            float scale)
{
    auto font = monoFont (fontSize * scale, bold);
    if (letterSpacingEm == 0.0f || font.getHeight() <= 0.0f)
        return font;
    const auto cssTracking = letterSpacingEm * fontSize * scale;
    return font.withExtraKerningFactor (cssTracking / font.getHeight());
}

float prototypeTextWidth (const juce::String& text,
                          float fontSize,
                          bool bold,
                          float letterSpacingEm,
                          float scale)
{
    return juce::GlyphArrangement::getStringWidth (
        trackedMonoFont (fontSize, bold, letterSpacingEm, scale), text)
        + letterSpacingEm * fontSize * scale;
}

void drawPrototypeText (juce::Graphics& graphics,
                        const juce::String& text,
                        juce::Rectangle<float> lineBox,
                        float fontSize,
                        bool bold,
                        float letterSpacingEm,
                        juce::Colour colour,
                        juce::Justification justification,
                        float scale)
{
    juce::GlyphArrangement glyphs;
    const auto font = trackedMonoFont (
        fontSize, bold, letterSpacingEm, scale);
    glyphs.addLineOfText (font, text, 0.0f, font.getAscent());
    auto glyphBounds = glyphs.getBoundingBox (
        0, glyphs.getNumGlyphs(), true);
    if (glyphBounds.getWidth() > lineBox.getWidth()
        && lineBox.getWidth() > 0.0f)
    {
        glyphs.stretchRangeOfGlyphs (
            0,
            glyphs.getNumGlyphs(),
            lineBox.getWidth() / glyphBounds.getWidth());
        glyphBounds = glyphs.getBoundingBox (
            0, glyphs.getNumGlyphs(), true);
    }
    auto targetX = lineBox.getX();
    if (justification.testFlags (juce::Justification::horizontallyCentred))
        targetX = lineBox.getCentreX() - glyphBounds.getWidth() * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        targetX = lineBox.getRight() - glyphBounds.getWidth();
    glyphs.moveRangeOfGlyphs (
        0,
        glyphs.getNumGlyphs(),
        targetX - glyphBounds.getX(),
        lineBox.getCentreY() - glyphBounds.getCentreY());
    graphics.setColour (colour);
    glyphs.draw (graphics);
}

bool differs (float first, float second) noexcept
{
    return std::bit_cast<std::uint32_t> (first)
        != std::bit_cast<std::uint32_t> (second);
}

} // namespace

class PrototypeSimpleMenuWindow final : public juce::Component
{
public:
    enum class Style { plain, phase };

    PrototypeSimpleMenuWindow (juce::StringArray newItems,
                               int selectedIndex,
                               Style newStyle,
                               juce::Component* newAnchor,
                               juce::Rectangle<int> target,
                               juce::Component& shell,
                               float newScale,
                               std::function<void (int)> newOnChoose)
        : items (std::move (newItems)),
          selected (selectedIndex),
          style (newStyle),
          anchor (newAnchor),
          onChoose (std::move (newOnChoose)),
          uiScale (newScale),
          outsideListener (*this)
    {
        setLookAndFeel (&shell.getLookAndFeel());
        setOpaque (true);
        setAlwaysOnTop (true);
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (false);

        const auto border = juce::jmax (1, juce::roundToInt (uiScale));
        const auto padding = juce::roundToInt (3.0f * uiScale);
        const auto rowHeight = juce::roundToInt (26.0f * uiScale);
        const auto menuWidth = target.getWidth();
        const auto menuHeight = items.size() * rowHeight
            + 2 * (border + padding);
        const auto shellBounds = shell.getScreenBounds();
        const auto inset = juce::roundToInt (4.0f * uiScale);
        const auto left = juce::jlimit (
            shellBounds.getX() + inset,
            shellBounds.getRight() - inset - menuWidth,
            target.getX());
        auto top = target.getBottom();
        // The Phase menu in the reference deliberately drops below the plugin
        // shell. It is a desktop popup, so the editor's bottom edge must not
        // make it flip upward.
        if (style != Style::phase
            && top + menuHeight > shellBounds.getBottom() - inset)
            top = target.getY() - menuHeight;
        if (style != Style::phase)
            top = juce::jmax (shellBounds.getY() + inset, top);

        if (newAnchor != nullptr)
        {
            newAnchor->getProperties().set ("pickerOpen", true);
            newAnchor->repaint();
        }
        setBounds (left, top, menuWidth, menuHeight);
        addToDesktop (juce::ComponentPeer::windowIsTemporary);
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();

        auto safeThis = juce::Component::SafePointer<PrototypeSimpleMenuWindow> (this);
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr || safeThis->listeningGlobally)
                return;
            juce::Desktop::getInstance().addGlobalMouseListener (
                &safeThis->outsideListener);
            safeThis->listeningGlobally = true;
        });
    }

    ~PrototypeSimpleMenuWindow() override
    {
        close();
        setLookAndFeel (nullptr);
    }

    bool isShowingFor (const juce::Component* component) const noexcept
    {
        return isVisible() && anchor.getComponent() == component;
    }

    void close()
    {
        if (anchor != nullptr)
        {
            anchor->getProperties().set ("pickerOpen", false);
            anchor->repaint();
        }
        anchor = nullptr;
        setVisible (false);
        if (listeningGlobally)
        {
            juce::Desktop::getInstance().removeGlobalMouseListener (
                &outsideListener);
            listeningGlobally = false;
        }
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto foreground = foregroundOf (*this);
        const auto background = backgroundOf (*this);
        const auto border = juce::jmax (1, juce::roundToInt (uiScale));
        const auto padding = juce::roundToInt (3.0f * uiScale);
        const auto inset = border + padding;
        const auto rowHeight = juce::roundToInt (26.0f * uiScale);
        graphics.fillAll (background);
        graphics.setColour (foreground);
        graphics.drawRect (getLocalBounds(), border);

        for (int row = 0; row < items.size(); ++row)
        {
            const auto area = juce::Rectangle<int> {
                inset, inset + row * rowHeight,
                getWidth() - 2 * inset, rowHeight };
            const auto active = row == selected || row == hovered;
            graphics.setColour (active ? foreground : background);
            graphics.fillRect (area);
            const auto textColour = active ? background : foreground;
            if (style == Style::phase)
            {
                const auto divider = items[row].indexOfChar (' ');
                const auto prefix = items[row].substring (0, divider);
                const auto value = items[row].substring (divider + 1);
                auto x = static_cast<float> (area.getX()) + 7.0f * uiScale;
                const auto line = area.toFloat();
                const auto prefixWidth = prototypeTextWidth (
                    prefix, 9.0f, true, 0.04f, uiScale);
                drawPrototypeText (
                    graphics, prefix,
                    line.withX (x).withWidth (prefixWidth),
                    9.0f, true, 0.04f, textColour,
                    juce::Justification::centredLeft, uiScale);
                x += prefixWidth + 7.0f * uiScale;
                drawPrototypeText (
                    graphics, value,
                    line.withX (x).withRight (
                        static_cast<float> (area.getRight()) - 7.0f * uiScale),
                    9.0f, true, 0.04f, textColour,
                    juce::Justification::centredLeft, uiScale);
            }
            else
            {
                drawPrototypeText (
                    graphics, items[row],
                    area.toFloat().reduced (4.0f * uiScale, 0.0f),
                    9.0f, true, 0.0f, textColour,
                    juce::Justification::centredLeft, uiScale);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        const auto next = rowAt (event.getPosition());
        if (next == hovered)
            return;
        hovered = next;
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered == -1)
            return;
        hovered = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! event.mods.isLeftButtonDown())
            return;
        const auto choice = rowAt (event.getPosition());
        if (choice < 0)
            return;
        if (onChoose)
            onChoose (choice);
        close();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() != juce::KeyPress::escapeKey)
            return false;
        close();
        return true;
    }

private:
    int rowAt (juce::Point<int> point) const noexcept
    {
        const auto border = juce::jmax (1, juce::roundToInt (uiScale));
        const auto padding = juce::roundToInt (3.0f * uiScale);
        const auto inset = border + padding;
        const auto rowHeight = juce::jmax (
            1, juce::roundToInt (26.0f * uiScale));
        const auto row = (point.y - inset) / rowHeight;
        return point.x >= inset && point.x < getWidth() - inset
            && point.y >= inset && row >= 0 && row < items.size()
            ? row : -1;
    }

    void handleOutsideMouseDown (const juce::MouseEvent& event)
    {
        const auto* original = event.originalComponent;
        if (original == this
            || (original != nullptr && isParentOf (original)))
            return;
        if (anchor != nullptr
            && (original == anchor.getComponent()
                || (original != nullptr && anchor->isParentOf (original))))
            return;
        close();
    }

    class OutsideListener final : public juce::MouseListener
    {
    public:
        explicit OutsideListener (PrototypeSimpleMenuWindow& newOwner)
            : owner (newOwner) {}
        void mouseDown (const juce::MouseEvent& event) override
        {
            owner.handleOutsideMouseDown (event);
        }
    private:
        PrototypeSimpleMenuWindow& owner;
    };

    juce::StringArray items;
    int selected = 0;
    int hovered = -1;
    Style style = Style::plain;
    juce::Component::SafePointer<juce::Component> anchor;
    std::function<void (int)> onChoose;
    float uiScale = 1.0f;
    OutsideListener outsideListener;
    bool listeningGlobally = false;
};

class PrototypeModeMenuWindow final : public juce::Component
{
public:
    PrototypeModeMenuWindow (int selectedMode,
                             double sampleRate,
                             juce::Component& newAnchor,
                             juce::Rectangle<int> target,
                             juce::Component& shell,
                             float newScale,
                             std::function<void (int)> newOnChoose)
        : selected (DistortionEngine::getDisplayPositionForMode (selectedMode)),
          anchor (&newAnchor),
          onChoose (std::move (newOnChoose)),
          uiScale (newScale),
          outsideListener (*this)
    {
        for (int position = 0; position < DistortionEngine::modeCount; ++position)
        {
            Parameters parameters;
            parameters.mode = DistortionEngine::getModeForDisplayPosition (position);
            parameters.driveDb = 18.0f;
            parameters.character = DistortionEngine::isCharacterBipolar (
                parameters.mode)
                ? 0.35f
                : juce::jmax (
                    0.58f,
                    DistortionEngine::getDefaultCharacter (parameters.mode));
            parameters.stages = 1;
            parameters.autoGainMode = 0;
            DistortionEngine::makeVisualization (
                parameters, sampleRate,
                visualizations[static_cast<size_t> (position)]);
        }

        setLookAndFeel (&shell.getLookAndFeel());
        setOpaque (true);
        setAlwaysOnTop (true);
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (false);
        newAnchor.getProperties().set ("pickerOpen", true);
        newAnchor.repaint();
        setBounds (target.withSize (
            juce::roundToInt (640.0f * uiScale),
            juce::roundToInt (182.0f * uiScale)));
        addToDesktop (juce::ComponentPeer::windowIsTemporary);
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();

        auto safeThis = juce::Component::SafePointer<PrototypeModeMenuWindow> (this);
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr || safeThis->listeningGlobally)
                return;
            juce::Desktop::getInstance().addGlobalMouseListener (
                &safeThis->outsideListener);
            safeThis->listeningGlobally = true;
        });
    }

    ~PrototypeModeMenuWindow() override
    {
        close();
        setLookAndFeel (nullptr);
    }

    bool isShowingFor (const juce::Component* component) const noexcept
    {
        return isVisible() && anchor.getComponent() == component;
    }

    void close()
    {
        if (anchor != nullptr)
        {
            anchor->getProperties().set ("pickerOpen", false);
            anchor->repaint();
        }
        anchor = nullptr;
        setVisible (false);
        if (listeningGlobally)
        {
            juce::Desktop::getInstance().removeGlobalMouseListener (
                &outsideListener);
            listeningGlobally = false;
        }
    }

    void paint (juce::Graphics& graphics) override
    {
        const auto normalForeground = foregroundOf (*this);
        const auto normalBackground = backgroundOf (*this);
        graphics.fillAll (normalBackground);
        for (int position = 0; position < DistortionEngine::modeCount; ++position)
        {
            const auto cell = cellBounds (position);
            const auto active = position == selected || position == hovered;
            const auto foreground = active ? normalBackground : normalForeground;
            const auto background = active ? normalForeground : normalBackground;
            graphics.setColour (background);
            graphics.fillRect (cell);
            graphics.setColour (foreground.withAlpha (0.18f));
            if (position % 10 != 9)
                graphics.fillRect (cell.withY (cell.getBottom() - uiScale)
                    .withHeight (juce::jmax (1.0f, uiScale)));
            if (position / 10 != 2)
                graphics.fillRect (cell.withX (cell.getRight() - uiScale)
                    .withWidth (juce::jmax (1.0f, uiScale)));

            auto content = cell.reduced (7.0f * uiScale, 1.0f * uiScale);
            auto number = content.removeFromLeft (18.0f * uiScale);
            content.removeFromLeft (7.0f * uiScale);
            auto icon = content.removeFromRight (35.0f * uiScale);
            content.removeFromRight (7.0f * uiScale);
            drawPrototypeText (
                graphics,
                juce::String (position + 1).paddedLeft ('0', 2),
                number, 9.0f, true, 0.0f,
                foreground.withAlpha (0.72f),
                juce::Justification::centredLeft, uiScale);
            drawPrototypeText (
                graphics,
                prototypeModeNamesByDisplay[static_cast<size_t> (position)],
                content, 9.0f, true, 0.0f, foreground,
                juce::Justification::centredLeft, uiScale);

            icon = icon.withSizeKeepingCentre (
                35.0f * uiScale, 12.0f * uiScale);
            juce::Path path;
            const auto& values = visualizations[
                static_cast<size_t> (position)].output;
            for (int point = 0;
                 point < DistortionEngine::Visualization::pointCount;
                 ++point)
            {
                const auto t = static_cast<float> (point)
                    / static_cast<float> (
                        DistortionEngine::Visualization::pointCount - 1);
                const auto x = icon.getX() + t * icon.getWidth();
                const auto y = icon.getCentreY()
                    - juce::jlimit (-1.0f, 1.0f,
                        values[static_cast<size_t> (point)] / 1.25f)
                        * icon.getHeight() * 0.46f;
                if (point == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }
            graphics.setColour (foreground);
            graphics.strokePath (
                path,
                juce::PathStrokeType (
                    1.1f * uiScale,
                    juce::PathStrokeType::curved,
                    juce::PathStrokeType::rounded));
        }
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        const auto next = positionAt (event.getPosition());
        if (next == hovered)
            return;
        hovered = next;
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered == -1)
            return;
        hovered = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! event.mods.isLeftButtonDown())
            return;
        const auto choice = positionAt (event.getPosition());
        if (choice < 0)
            return;
        if (onChoose)
            onChoose (choice);
        close();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() != juce::KeyPress::escapeKey)
            return false;
        close();
        return true;
    }

private:
    juce::Rectangle<float> cellBounds (int position) const noexcept
    {
        const auto column = position / 10;
        const auto row = position % 10;
        const auto width = static_cast<float> (getWidth());
        const auto height = static_cast<float> (getHeight());
        const auto left = width * static_cast<float> (column) / 3.0f;
        const auto right = width * static_cast<float> (column + 1) / 3.0f;
        const auto top = height * static_cast<float> (row) / 10.0f;
        const auto bottom = height * static_cast<float> (row + 1) / 10.0f;
        return { left, top, right - left, bottom - top };
    }

    int positionAt (juce::Point<int> point) const noexcept
    {
        if (! getLocalBounds().contains (point))
            return -1;
        const auto column = juce::jlimit (
            0, 2, point.x * 3 / juce::jmax (1, getWidth()));
        const auto row = juce::jlimit (
            0, 9, point.y * 10 / juce::jmax (1, getHeight()));
        return column * 10 + row;
    }

    void handleOutsideMouseDown (const juce::MouseEvent& event)
    {
        const auto* original = event.originalComponent;
        if (original == this
            || (original != nullptr && isParentOf (original)))
            return;
        if (anchor != nullptr
            && (original == anchor.getComponent()
                || (original != nullptr && anchor->isParentOf (original))))
            return;
        close();
    }

    class OutsideListener final : public juce::MouseListener
    {
    public:
        explicit OutsideListener (PrototypeModeMenuWindow& newOwner)
            : owner (newOwner) {}
        void mouseDown (const juce::MouseEvent& event) override
        {
            owner.handleOutsideMouseDown (event);
        }
    private:
        PrototypeModeMenuWindow& owner;
    };

    int selected = 0;
    int hovered = -1;
    juce::Component::SafePointer<juce::Component> anchor;
    std::function<void (int)> onChoose;
    float uiScale = 1.0f;
    std::array<DistortionEngine::Visualization,
               DistortionEngine::modeCount> visualizations {};
    OutsideListener outsideListener;
    bool listeningGlobally = false;
};

GeometricLookAndFeel::GeometricLookAndFeel()
{
    applyPalette();
}

void GeometricLookAndFeel::setInverted (bool shouldBeInverted)
{
    if (inverted == shouldBeInverted)
        return;
    inverted = shouldBeInverted;
    applyPalette();
}

void GeometricLookAndFeel::setUiScale (float newScale) noexcept
{
    uiScale = juce::jlimit (0.5f, 3.0f, newScale);
}

void GeometricLookAndFeel::applyPalette()
{
    const auto foreground = inverted ? darkPalette : lightPalette;
    const auto background = inverted ? lightPalette : darkPalette;
    const auto muted = foreground.interpolatedWith (background, 0.28f);

    setColour (foregroundColourId, foreground);
    setColour (backgroundColourId, background);
    setColour (mutedColourId, muted);
    setColour (juce::Label::textColourId, foreground);
    setColour (juce::Slider::textBoxTextColourId, foreground);
    setColour (juce::Slider::textBoxBackgroundColourId, background);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, background);
    setColour (juce::ComboBox::textColourId, foreground);
    setColour (juce::ComboBox::outlineColourId, foreground);
    setColour (juce::ComboBox::arrowColourId, foreground);
    setColour (juce::PopupMenu::backgroundColourId, background);
    setColour (juce::PopupMenu::textColourId, foreground);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, foreground);
    setColour (juce::PopupMenu::highlightedTextColourId, background);
}

void GeometricLookAndFeel::drawRotarySlider (
    juce::Graphics& graphics,
    int x,
    int y,
    int width,
    int height,
    float sliderPosition,
    float,
    float,
    juce::Slider& slider)
{
    const auto scale = uiScale;
    auto bounds = juce::Rectangle<float> (
        static_cast<float> (x),
        static_cast<float> (y),
        static_cast<float> (width),
        static_cast<float> (height));
    const auto foreground = findColour (foregroundColourId);
    const auto background = findColour (backgroundColourId);
    const auto progress = juce::jlimit (0.0f, 1.0f, sliderPosition);
    if (slider.getName() == "STAGES")
    {
        const auto blockWidth = 7.0f * scale;
        const auto blockHeight = 4.0f * scale;
        const auto gap = 1.0f * scale;
        const auto totalHeight = 8.0f * blockHeight + 7.0f * gap;
        auto stage = juce::Rectangle<float> {
            bounds.getRight() - 12.0f * scale - blockWidth,
            bounds.getCentreY() - 0.5f * totalHeight,
            blockWidth,
            blockHeight
        };
        const auto activeStages = juce::jlimit (
            1, 8, juce::roundToInt (slider.getValue()));
        for (int index = 0; index < 8; ++index)
        {
            graphics.setColour (foreground.withAlpha (
                index >= 8 - activeStages ? 1.0f : 0.42f));
            graphics.fillRect (stage);
            stage.translate (0.0f, blockHeight + gap);
        }
        return;
    }

    auto rail = juce::Rectangle<float> {
        bounds.getRight() - 7.0f * scale - 18.0f * scale,
        bounds.getCentreY() - 22.0f * scale,
        18.0f * scale,
        44.0f * scale
    };
    graphics.setColour (background);
    graphics.fillRect (rail);
    graphics.setColour (foreground);
    graphics.drawRect (rail, juce::jmax (1.0f, scale));
    // CSS absolute children are positioned inside the parent's border box:
    // border 1 px + inset 2 px leaves a 3 px outer inset for the fill.
    const auto interior = rail.reduced (3.0f * scale);
    graphics.setColour (foreground);
    graphics.fillRect (interior.withTop (
        interior.getBottom() - progress * interior.getHeight()));
}

void GeometricLookAndFeel::drawLinearSlider (
    juce::Graphics& graphics,
    int x,
    int y,
    int width,
    int height,
    float sliderPos,
    float minSliderPos,
    float maxSliderPos,
    juce::Slider::SliderStyle style,
    juce::Slider& slider)
{
    if (style == juce::Slider::LinearBar
        && (slider.getName() == "MIX" || slider.getName() == "OUT"))
    {
        graphics.fillAll (findColour (backgroundColourId));
        return;
    }
    juce::LookAndFeel_V4::drawLinearSlider (
        graphics, x, y, width, height, sliderPos,
        minSliderPos, maxSliderPos, style, slider);
}

juce::Slider::SliderLayout GeometricLookAndFeel::getSliderLayout (
    juce::Slider& slider)
{
    if (slider.getName() == "MIX" || slider.getName() == "OUT")
    {
        juce::Slider::SliderLayout layout;
        layout.sliderBounds = slider.getLocalBounds();
        layout.textBoxBounds = slider.getLocalBounds();
        return layout;
    }
    if (slider.getSliderStyle()
        != juce::Slider::RotaryHorizontalVerticalDrag)
        return juce::LookAndFeel_V4::getSliderLayout (slider);

    juce::Slider::SliderLayout layout;
    layout.sliderBounds = slider.getLocalBounds();
    layout.textBoxBounds = juce::Rectangle<int> {
        juce::roundToInt (11.0f * uiScale),
        juce::roundToInt (33.5f * uiScale),
        juce::roundToInt (58.0f * uiScale),
        juce::roundToInt (12.0f * uiScale)
    }.getIntersection (slider.getLocalBounds());
    return layout;
}

juce::Label* GeometricLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setBorderSize ({ 0, 0, 0, 0 });
    label->setMinimumHorizontalScale (1.0f);
    label->setJustificationType (juce::Justification::centredLeft);
    label->setInterceptsMouseClicks (false, false);
    label->getProperties().set ("prototypeSliderValue", true);
    label->setFont (monoFont (
        ((slider.getName() == "MIX" || slider.getName() == "OUT")
             ? 10.0f : 12.0f) * uiScale,
        true));
    return label;
}

void GeometricLookAndFeel::drawLabel (juce::Graphics& graphics,
                                      juce::Label& label)
{
    const auto sliderValue = static_cast<bool> (
        label.getProperties().getWithDefault (
            "prototypeSliderValue", false));
    const auto controlTitle = static_cast<bool> (
        label.getProperties().getWithDefault (
            "prototypeControlTitle", false));
    if (! sliderValue && ! controlTitle)
    {
        juce::LookAndFeel_V4::drawLabel (graphics, label);
        return;
    }

    const auto* slider = sliderValue
        ? dynamic_cast<const juce::Slider*> (label.getParentComponent())
        : nullptr;
    const auto compact = slider != nullptr
        && (slider->getName() == "MIX" || slider->getName() == "OUT");
    drawPrototypeText (
        graphics,
        label.getText(),
        label.getLocalBounds().toFloat(),
        controlTitle ? 9.0f : (compact ? 10.0f : 12.0f),
        true,
        controlTitle ? 0.02f : 0.0f,
        label.findColour (juce::Label::textColourId),
        juce::Justification::centredLeft,
        uiScale);
}

void GeometricLookAndFeel::drawComboBox (
    juce::Graphics& graphics,
    int width,
    int height,
    bool,
    int,
    int,
    int,
    int,
    juce::ComboBox&)
{
    const auto scale = uiScale;
    auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (
        juce::jmax (1, juce::roundToInt (scale)));
    const auto foreground = findColour (foregroundColourId);
    const auto background = findColour (backgroundColourId);
    graphics.setColour (background);
    graphics.fillRect (bounds);
    graphics.setColour (foreground);
    graphics.drawRect (
        bounds, juce::jmax (1, juce::roundToInt (2.0f * scale)));

    const auto marker = juce::jmax (
        juce::roundToInt (8.0f * scale), height / 5);
    graphics.fillRect (
        width - marker - juce::roundToInt (10.0f * scale),
        (height - marker) / 2,
        marker,
        marker);
}

void GeometricLookAndFeel::positionComboBoxText (
    juce::ComboBox& box,
    juce::Label& label)
{
    const auto left = juce::roundToInt (10.0f * uiScale);
    const auto top = juce::jmax (1, juce::roundToInt (uiScale));
    label.setBounds (
        left,
        top,
        box.getWidth() - juce::roundToInt (42.0f * uiScale),
        box.getHeight() - 2 * top);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font GeometricLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return monoFont (15.0f * uiScale, true);
}

juce::Font GeometricLookAndFeel::getLabelFont (juce::Label& label)
{
    if (const auto* slider = dynamic_cast<const juce::Slider*> (
            label.getParentComponent()))
        if (slider->getName() == "MIX" || slider->getName() == "OUT")
            return monoFont (10.0f * uiScale, true);
    return monoFont (12.0f * uiScale, true);
}

juce::Font GeometricLookAndFeel::getPopupMenuFont()
{
    return monoFont (12.0f * uiScale);
}

void GeometricLookAndFeel::drawButtonBackground (
    juce::Graphics& graphics,
    juce::Button& button,
    const juce::Colour&,
    bool isHighlighted,
    bool isDown)
{
    const auto active = button.getToggleState();
    const auto foreground = findColour (foregroundColourId);
    const auto background = findColour (backgroundColourId);
    const auto bounds = button.getLocalBounds();
    graphics.setColour (active || isDown ? foreground : background);
    graphics.fillRect (bounds);

    juce::ignoreUnused (isHighlighted);
}

void GeometricLookAndFeel::drawButtonText (
    juce::Graphics& graphics,
    juce::TextButton& button,
    bool,
    bool isDown)
{
    const auto textColour = button.getToggleState() || isDown
        ? findColour (backgroundColourId)
        : findColour (foregroundColourId);
    drawPrototypeText (
        graphics,
        button.getButtonText(),
        button.getLocalBounds().toFloat().reduced (2.0f * uiScale),
        9.0f,
        true,
        0.0f,
        textColour,
        juce::Justification::centred,
        uiScale);
}

BrandButton::BrandButton()
    : juce::TextButton ("default_distortion")
{
    setWantsKeyboardFocus (false);
}

void BrandButton::paintButton (
    juce::Graphics& graphics,
    bool isHighlighted,
    bool isDown)
{
    juce::ignoreUnused (isHighlighted);
    const auto scale = scaleOf (*this);
    drawPrototypeText (
        graphics,
        getButtonText(),
        getLocalBounds().toFloat().reduced (12.0f * scale, 0.0f),
        16.0f,
        true,
        -0.06f,
        isDown ? mutedOf (*this) : foregroundOf (*this),
        juce::Justification::centred,
        scale);
}

void ResettableSlider::mouseDown (const juce::MouseEvent& event)
{
    if ((event.mods.isPopupMenu() || event.mods.isRightButtonDown())
        && isDoubleClickReturnEnabled())
    {
        setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);
        return;
    }
    juce::Slider::mouseDown (event);
}

void ResettableSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    showTextBox();
}

void VerticalDragSlider::mouseDown (const juce::MouseEvent& event)
{
    dragStartProportion = valueToProportionOfLength (getValue());
    ResettableSlider::mouseDown (event);
}

void VerticalDragSlider::mouseDrag (const juce::MouseEvent& event)
{
    if (! isEnabled())
        return;
    const auto fine = event.mods.isShiftDown() ? 0.2 : 1.0;
    const auto nextProportion = juce::jlimit (
        0.0,
        1.0,
        dragStartProportion
            - static_cast<double> (event.getDistanceFromDragStartY())
                * fine
                / static_cast<double> (
                    juce::jmax (1, getMouseDragSensitivity())));
    setValue (
        proportionOfLengthToValue (nextProportion),
        juce::sendNotificationSync);
}

ParameterControl::ParameterControl (juce::String title)
{
    slider.setName (title);
    slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    titleLabel.setText (std::move (title), juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setBorderSize ({ 0, 0, 0, 0 });
    titleLabel.setInterceptsMouseClicks (false, false);
    titleLabel.getProperties().set ("prototypeControlTitle", true);
    addAndMakeVisible (titleLabel);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    addAndMakeVisible (slider);
    setUiScale (1.0f);
}

void ParameterControl::applyPaletteColours()
{
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    titleLabel.setColour (
        juce::Label::textColourId, foreground.withAlpha (0.72f));
    slider.setColour (juce::Slider::textBoxTextColourId, foreground);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, background);
    slider.setColour (
        juce::Slider::textBoxOutlineColourId,
        juce::Colours::transparentBlack);
    slider.updateText();
    repaint();
}

void ParameterControl::lookAndFeelChanged()
{
    juce::Component::lookAndFeelChanged();
    applyPaletteColours();
}

void ParameterControl::setTitle (const juce::String& title)
{
    titleLabel.setText (title, juce::dontSendNotification);
}

void ParameterControl::setUiScale (float newScale)
{
    uiScale = juce::jlimit (0.5f, 2.0f, newScale);
    titleLabel.setFont (monoFont (9.0f * uiScale, true));
    if (compactLayout)
    {
        slider.setSliderStyle (juce::Slider::LinearBar);
        slider.setTextBoxStyle (
            juce::Slider::TextBoxLeft,
            false,
            juce::roundToInt (92.0f * uiScale),
            juce::roundToInt (22.0f * uiScale));
    }
    else
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (
            juce::Slider::TextBoxLeft,
            false,
            juce::roundToInt (69.0f * uiScale),
            juce::roundToInt (18.0f * uiScale));
    }
    applyPaletteColours();
    resized();
    repaint();
}

void ParameterControl::setCompactLayout (bool shouldBeCompact)
{
    compactLayout = shouldBeCompact;
    setUiScale (uiScale);
}

void ParameterControl::resized()
{
    if (compactLayout)
    {
        auto bounds = getLocalBounds();
        const auto isMix = slider.getName() == "MIX";
        bounds.removeFromLeft (juce::roundToInt (
            (isMix ? 5.0f : 8.0f) * uiScale));
        bounds.removeFromRight (juce::roundToInt (
            (isMix ? 5.0f : 8.0f) * uiScale));
        titleLabel.setBounds (bounds.removeFromLeft (
            juce::roundToInt (18.0f * uiScale)));
        bounds.removeFromLeft (juce::roundToInt (
            (isMix ? 4.0f : 7.0f) * uiScale));
        slider.setBounds (bounds);
        return;
    }
    auto bounds = getLocalBounds();
    slider.setBounds (bounds);
    titleLabel.setBounds (
        juce::roundToInt (11.0f * uiScale),
        juce::roundToInt (15.5f * uiScale),
        juce::jmax (1, getWidth() - juce::roundToInt (38.0f * uiScale)),
        juce::roundToInt (9.0f * uiScale));
    titleLabel.toFront (false);
}

void ParameterControl::paint (juce::Graphics& graphics)
{
    graphics.fillAll (backgroundOf (*this));
}

TriangleButton::TriangleButton (bool pointsRight)
    : juce::Button (pointsRight ? "Next algorithm" : "Previous algorithm"),
      right (pointsRight)
{
    setWantsKeyboardFocus (false);
}

void TriangleButton::paintButton (
    juce::Graphics& graphics,
    bool isHighlighted,
    bool isDown)
{
    const auto scale = scaleOf (*this);
    const auto size = 6.0f * scale;
    const auto centre = getLocalBounds().toFloat().getCentre();
    auto bounds = juce::Rectangle<float> {
        centre.x - 0.5f * size,
        centre.y - 0.5f * size,
        size,
        size
    };
    juce::Path triangle;
    if (right)
    {
        triangle.startNewSubPath (bounds.getX(), bounds.getY());
        triangle.lineTo (bounds.getRight(), bounds.getCentreY());
        triangle.lineTo (bounds.getX(), bounds.getBottom());
    }
    else
    {
        triangle.startNewSubPath (bounds.getRight(), bounds.getY());
        triangle.lineTo (bounds.getX(), bounds.getCentreY());
        triangle.lineTo (bounds.getRight(), bounds.getBottom());
    }
    triangle.closeSubPath();
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    if (isHighlighted || isDown)
    {
        graphics.setColour (foreground);
        graphics.fillAll();
    }
    graphics.setColour (isHighlighted || isDown ? background : foreground);
    graphics.fillPath (triangle);
}

AlgorithmButton::AlgorithmButton()
    : juce::TextButton ("01  SOFT CLIP")
{
    setWantsKeyboardFocus (false);
}

void AlgorithmButton::setMode (int displayPosition, juce::String name)
{
    number = juce::jlimit (1, DistortionEngine::modeCount, displayPosition + 1);
    modeName = std::move (name);
    setButtonText (
        juce::String (number).paddedLeft ('0', 2) + "  " + modeName);
    repaint();
}

void AlgorithmButton::paintButton (juce::Graphics& graphics,
                                   bool,
                                   bool)
{
    const auto scale = scaleOf (*this);
    const auto ink = foregroundOf (*this);
    const auto numberText = juce::String (number).paddedLeft ('0', 2);
    const auto numberWidth = prototypeTextWidth (
        numberText, 10.0f, true, 0.0f, scale);
    const auto nameWidth = prototypeTextWidth (
        modeName, 10.0f, true, -0.03f, scale);
    const auto gap = 6.0f * scale;
    const auto totalWidth = numberWidth + gap + nameWidth;
    const auto left = getLocalBounds().toFloat().getCentreX()
        - totalWidth * 0.5f;
    const auto line = juce::Rectangle<float> {
        left,
        0.0f,
        totalWidth,
        static_cast<float> (getHeight())
    };
    drawPrototypeText (
        graphics, numberText,
        line.withWidth (numberWidth),
        10.0f, true, 0.0f, ink,
        juce::Justification::centredLeft, scale);
    drawPrototypeText (
        graphics, modeName,
        line.withTrimmedLeft (numberWidth + gap),
        10.0f, true, -0.03f, ink,
        juce::Justification::centredLeft, scale);
    if ((bool) getProperties().getWithDefault ("pickerOpen", false))
    {
        graphics.setColour (ink);
        graphics.drawRect (
            getLocalBounds(), juce::jmax (1, juce::roundToInt (scale)));
    }
}

HeaderActionButton::HeaderActionButton (juce::String label,
                                        juce::String value)
    : juce::TextButton (label + " " + value),
      headerLabel (std::move (label)),
      valueText (std::move (value))
{
    setWantsKeyboardFocus (false);
}

void HeaderActionButton::setValueText (juce::String value)
{
    valueText = std::move (value);
    setButtonText (headerLabel + " " + valueText);
    repaint();
}

void HeaderActionButton::paintButton (juce::Graphics& graphics,
                                      bool isHighlighted,
                                      bool isDown)
{
    const auto routeHover = headerLabel == "ROUTE" && isHighlighted;
    const auto active = getToggleState() || isDown || routeHover;
    const auto paper = backgroundOf (*this);
    const auto ink = foregroundOf (*this);
    graphics.fillAll (active ? ink : paper);
    const auto foreground = active ? paper : ink;
    const auto scale = scaleOf (*this);
    auto labelBounds = juce::Rectangle<float> {
        13.0f * scale, 15.0f * scale,
        static_cast<float> (getWidth()) - 26.0f * scale, 9.0f * scale
    };
    auto valueBounds = juce::Rectangle<float> {
        13.0f * scale, 33.0f * scale,
        static_cast<float> (getWidth()) - 26.0f * scale, 13.0f * scale
    };
    const auto justification = (headerLabel == "ROUTE" || headerLabel == "OS")
        ? juce::Justification::centredLeft
        : juce::Justification::centred;
    drawPrototypeText (
        graphics, headerLabel, labelBounds,
        9.0f, true, 0.13f,
        foreground.withAlpha (0.72f), justification, scale);
    const auto dimValue =
        (headerLabel == "AUTO GAIN" || headerLabel == "POWER")
        && valueText == "OFF";
    drawPrototypeText (
        graphics, valueText, valueBounds,
        headerLabel == "OS" ? 12.0f : 13.0f,
        true, 0.0f,
        foreground.withAlpha (dimValue ? 0.42f : 1.0f),
        justification, scale);
    if ((bool) getProperties().getWithDefault ("pickerOpen", false))
    {
        graphics.setColour (ink);
        graphics.drawRect (
            getLocalBounds(), juce::jmax (1, juce::roundToInt (scale)));
    }
}

SmartGainButton::SmartGainButton()
    : HeaderActionButton ("AUTO GAIN", "REGULAR")
{
}

StripButton::StripButton (juce::String text)
    : juce::TextButton (std::move (text))
{
    setWantsKeyboardFocus (false);
}

void StripButton::paintButton (juce::Graphics& graphics,
                               bool isHighlighted,
                               bool isDown)
{
    juce::ignoreUnused (isHighlighted);
    const auto active = getToggleState() || isDown;
    const auto ink = foregroundOf (*this);
    const auto paper = backgroundOf (*this);
    graphics.fillAll (active ? ink : paper);
    const auto scale = scaleOf (*this);
    const auto colour = active ? paper : ink;
    auto line = getLocalBounds().toFloat().reduced (11.0f * scale, 0.0f);
    const auto divider = getButtonText().indexOf ("  ");
    if (divider > 0)
    {
        const auto prefix = getButtonText().substring (0, divider);
        const auto value = getButtonText().substring (divider + 2);
        const auto prefixWidth = prototypeTextWidth (
            prefix, 9.0f, true, 0.04f, scale);
        drawPrototypeText (
            graphics, prefix, line.withWidth (prefixWidth),
            9.0f, true, 0.04f, colour,
            juce::Justification::centredLeft, scale);
        line.removeFromLeft (prefixWidth + 7.0f * scale);
        drawPrototypeText (
            graphics, value, line,
            9.0f, true, 0.04f, colour,
            juce::Justification::centredLeft, scale);
    }
    else
    {
        drawPrototypeText (
            graphics, getButtonText(), line,
            9.0f, true, 0.04f, colour,
            juce::Justification::centredLeft, scale);
    }
    if ((bool) getProperties().getWithDefault ("pickerOpen", false))
    {
        graphics.setColour (ink);
        graphics.drawRect (
            getLocalBounds(), juce::jmax (1, juce::roundToInt (scale)));
    }
}

RtaBandButton::RtaBandButton (juce::String text)
    : juce::TextButton (std::move (text))
{
    setClickingTogglesState (false);
    setWantsKeyboardFocus (true);
}

void RtaBandButton::paintButton (juce::Graphics& graphics,
                                 bool isHighlighted,
                                 bool isDown)
{
    const auto active = getToggleState() || isDown;
    const auto ink = foregroundOf (*this);
    const auto paper = backgroundOf (*this);
    graphics.fillAll (active ? ink : paper);
    graphics.setColour (ink);
    graphics.drawRect (
        getLocalBounds(),
        juce::jmax (1, juce::roundToInt (scaleOf (*this))));
    graphics.setColour (active ? paper : ink);
    juce::ignoreUnused (isHighlighted);
    drawPrototypeText (
        graphics, getButtonText(), getLocalBounds().toFloat(),
        9.0f, true, 0.0f,
        active ? paper : ink,
        juce::Justification::centred, scaleOf (*this));
}

VerticalTextButton::VerticalTextButton()
    : juce::TextButton ("STEREO")
{
    setClickingTogglesState (true);
    setWantsKeyboardFocus (false);
}

void VerticalTextButton::paintButton (
    juce::Graphics& graphics,
    bool isHighlighted,
    bool isDown)
{
    const auto active = getToggleState() || isDown;
    const auto scale = scaleOf (*this);
    auto body = getLocalBounds().toFloat();
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    const auto border = juce::jmax (1.0f, 1.0f * scale);
    graphics.setColour (active ? foreground : background);
    graphics.fillRect (body);
    graphics.setColour (foreground);
    graphics.fillRect (body.removeFromTop (border));
    graphics.fillRect (body.removeFromBottom (border));
    graphics.fillRect (body.removeFromLeft (border));
    graphics.fillRect (body.removeFromRight (border));

    juce::ignoreUnused (isHighlighted);

    juce::Graphics::ScopedSaveState saved (graphics);
    graphics.addTransform (
        juce::AffineTransform::rotation (
            -juce::MathConstants<float>::halfPi,
            body.getCentreX(), body.getCentreY()));
    const auto rotatedBounds = juce::Rectangle<float> (
        body.getCentreX() - body.getHeight() * 0.5f,
        body.getCentreY() - body.getWidth() * 0.5f,
        body.getHeight(), body.getWidth());
    drawPrototypeText (
        graphics, "STEREO",
        rotatedBounds.reduced (2.0f * scale, 1.0f * scale),
        9.0f, true, 0.0f,
        active ? background : foreground,
        juce::Justification::centred, scale);
}

VerticalTextSlider::VerticalTextSlider (
    juce::String parameterName,
    juce::String verticalText,
    double defaultValue)
    : text (std::move (verticalText))
{
    setSliderStyle (juce::Slider::LinearVertical);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRange (0.0, 1.0, 0.001);
    setMouseDragSensitivity (140);
    setDescriptor (
        std::move (parameterName), std::move (verticalText), defaultValue);
}

void VerticalTextSlider::setDescriptor (
    juce::String parameterName,
    juce::String verticalText,
    double defaultValue)
{
    setName (parameterName);
    setTitle (parameterName);
    text = std::move (verticalText);
    setDoubleClickReturnValue (true, defaultValue);
    repaint();
}

void VerticalTextSlider::paint (juce::Graphics& graphics)
{
    const auto scale = scaleOf (*this);
    const auto connectorWidth = juce::jmax (3.0f, 7.0f * scale);
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromLeft (connectorWidth);
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    const auto border = juce::jmax (1.0f, 2.0f * scale);

    graphics.setColour (foreground);
    graphics.fillRect (0.0f,
                       bounds.getCentreY() - 1.5f * scale,
                       bounds.getX() + border,
                       3.0f * scale);
    graphics.setColour (background);
    graphics.fillRect (bounds);
    graphics.setColour (foreground);
    auto framed = bounds;
    graphics.fillRect (framed.removeFromTop (border));
    graphics.fillRect (framed.removeFromBottom (border));
    graphics.fillRect (framed.removeFromLeft (border));
    graphics.fillRect (framed.removeFromRight (border));

    const auto progress = static_cast<float> (
        getNormalisableRange().convertTo0to1 (getValue()));
    const auto interior = bounds.reduced (border);
    const auto fill = interior.withTop (
        interior.getBottom() - progress * interior.getHeight());
    graphics.setColour (foreground);
    graphics.fillRect (fill);

    graphics.setFont (monoFont (10.0f * scale, true));
    const auto drawText = [&] (juce::Colour colour,
                               juce::Rectangle<int> clip)
    {
        juce::Graphics::ScopedSaveState clipped (graphics);
        graphics.reduceClipRegion (clip);
        graphics.setColour (colour);
        juce::Graphics::ScopedSaveState rotated (graphics);
        graphics.addTransform (
            juce::AffineTransform::rotation (
                -juce::MathConstants<float>::halfPi,
                bounds.getCentreX(), bounds.getCentreY()));
        const auto rotatedBounds = juce::Rectangle<float> (
            bounds.getCentreX() - bounds.getHeight() * 0.5f,
            bounds.getCentreY() - bounds.getWidth() * 0.5f,
            bounds.getHeight(), bounds.getWidth());
        graphics.drawFittedText (
            text,
            rotatedBounds.reduced (
                5.0f * scale, 2.0f * scale).toNearestInt(),
            juce::Justification::centred,
            1);
    };
    drawText (foreground, interior.toNearestInt());
    drawText (background, fill.toNearestInt());
}

void SmartGainButton::setLoadingState (float progress, bool isLoading)
{
    loadingProgress = juce::jlimit (0.0f, 1.0f, progress);
    loading = isLoading;
    repaint();
}

void SmartGainButton::paintButton (
    juce::Graphics& graphics,
    bool isHighlighted,
    bool isDown)
{
    HeaderActionButton::paintButton (graphics, isHighlighted, isDown);

    if (! loading)
        return;

    const auto scale = scaleOf (*this);
    auto track = getLocalBounds().toFloat().reduced (5.0f * scale);
    track = track.removeFromBottom (5.0f * scale);
    const auto background = backgroundOf (*this);
    graphics.setColour (background.withAlpha (0.22f));
    graphics.fillRect (track);
    graphics.setColour (background);
    graphics.fillRect (track.withWidth (
        track.getWidth() * loadingProgress));

    const auto scan = static_cast<float> (std::fmod (
        juce::Time::getMillisecondCounterHiRes() * 0.0014, 1.0));
    const auto scannerWidth = juce::jmax (
        3.0f * scale, track.getWidth() * 0.045f);
    graphics.fillRect (
        track.getX() + scan * juce::jmax (
            0.0f, track.getWidth() - scannerWidth),
        track.getY(),
        scannerWidth,
        track.getHeight());
}

ResponseDisplay::ResponseDisplay (DefaultDistortionAudioProcessor& owner)
    : processor (owner)
{
    startTimerHz (30);
}

ResponseDisplay::~ResponseDisplay()
{
    stopTimer();
}

LevelMeterPanel::LevelMeterPanel (DefaultDistortionAudioProcessor& owner)
    : processor (owner)
{
    startTimerHz (30);
}

LevelMeterPanel::~LevelMeterPanel()
{
    stopTimer();
}

void LevelMeterPanel::timerCallback()
{
    repaint();
}

void LevelMeterPanel::paint (juce::Graphics& graphics)
{
    const auto scale = scaleOf (*this);
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    graphics.fillAll (background);
    const auto railWidth = 8.0f * scale;
    const auto railHeight = 148.0f * scale;
    auto first = juce::Rectangle<float> {
        6.0f * scale, 10.0f * scale, railWidth, railHeight };
    auto second = first.translated (10.0f * scale, 0.0f);
    auto third = first.translated (30.0f * scale, 0.0f);
    auto fourth = second.translated (30.0f * scale, 0.0f);
    const auto drawRail = [&] (juce::Rectangle<float> rail,
                               float level,
                               float alpha)
    {
        graphics.setColour (foreground.withAlpha (0.11f));
        graphics.fillRect (rail);
        graphics.setColour (foreground.withAlpha (alpha));
        const auto amount = juce::jlimit (0.0f, 1.0f, level);
        graphics.fillRect (rail.withTop (
            rail.getBottom() - amount * rail.getHeight()));
    };
    drawRail (first, processor.getInputPeak (0), 0.34f);
    drawRail (second, processor.getInputPeak (1), 0.34f);
    drawRail (third, processor.getOutputPeak (0), 1.0f);
    drawRail (fourth, processor.getOutputPeak (1), 1.0f);
    drawPrototypeText (
        graphics, "IN",
        juce::Rectangle<float> {
            0.0f, 165.0f * scale, 30.0f * scale, 9.0f * scale },
        9.0f, true, 0.0f, foreground,
        juce::Justification::centred, scale);
    drawPrototypeText (
        graphics, "OUT",
        juce::Rectangle<float> {
            30.0f * scale, 165.0f * scale, 30.0f * scale, 9.0f * scale },
        9.0f, true, 0.0f, foreground,
        juce::Justification::centred, scale);
}

BandTrimControl::BandTrimControl()
{
    setName ("Band Trim");
    setTitle ("Band Trim");
    setSliderStyle (juce::Slider::LinearVertical);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRange (-12.0, 12.0, 0.01);
    setMouseDragSensitivity (180);
    setSliderSnapsToMousePosition (false);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setEditable (false, true, false);
    valueLabel.setInterceptsMouseClicks (false, false);
    valueLabel.onTextChange = [this]
    {
        if (updatingText)
            return;
        auto text = valueLabel.getText().trim();
        text = text.upToFirstOccurrenceOf ("dB", false, true).trim();
        const auto parsed = getNormalisableRange().snapToLegalValue (
            juce::jlimit (getMinimum(), getMaximum(), text.getDoubleValue()));
        setValue (parsed, juce::sendNotificationSync);
        updateValueText();
    };
    addAndMakeVisible (valueLabel);
    onValueChange = [this] { updateValueText(); };
    updateValueText();
}

void BandTrimControl::updateValueText()
{
    const auto clean = std::abs (getValue()) < 0.005 ? 0.0 : getValue();
    juce::ScopedValueSetter<bool> guard (updatingText, true);
    valueLabel.setText (
        juce::String (clean, 2) + " dB",
        juce::dontSendNotification);
}

void BandTrimControl::paint (juce::Graphics& graphics)
{
    const auto scale = scaleOf (*this);
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    graphics.setColour (background);
    graphics.fillRect (getLocalBounds());
    graphics.setColour (foreground);
    graphics.drawRect (
        getLocalBounds(),
        juce::jmax (1, juce::roundToInt (2.0f * scale)));
}

void BandTrimControl::resized()
{
    valueLabel.setBounds (getLocalBounds().reduced (
        juce::jmax (2, juce::roundToInt (2.0f * scaleOf (*this)))));
}

void BandTrimControl::mouseDoubleClick (const juce::MouseEvent&)
{
    valueLabel.showEditor();
    if (auto* editor = valueLabel.getCurrentTextEditor())
        editor->selectAll();
}

void BandTrimControl::lookAndFeelChanged()
{
    juce::Slider::lookAndFeelChanged();
    valueLabel.setColour (juce::Label::textColourId, foregroundOf (*this));
    valueLabel.setColour (
        juce::Label::backgroundColourId,
        juce::Colours::transparentBlack);
    valueLabel.setColour (
        juce::Label::outlineColourId,
        juce::Colours::transparentBlack);
    valueLabel.setFont (monoFont (12.0f * scaleOf (*this), true));
    updateValueText();
    repaint();
}

MultibandPanel::MultibandPanel (DefaultDistortionAudioProcessor& owner)
    : processor (owner)
{
    inputSpectrum.fill (analyzerFloorDb);
    outputSpectrum.fill (analyzerFloorDb);
    for (auto* button : {
             &linkButton, &bandCountButton, &phaseButton,
             &soloButton, &bypassButton })
        addAndMakeVisible (*button);

    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
    {
        auto& solo = soloButtons[static_cast<size_t> (band)];
        auto& bypass = bypassButtons[static_cast<size_t> (band)];
        solo.setName ("Band " + juce::String (band + 1) + " Solo");
        bypass.setName ("Band " + juce::String (band + 1) + " Bypass");
        addAndMakeVisible (solo);
        addAndMakeVisible (bypass);
        solo.onClick = [this, band]
        {
            processor.setSelectedBand (band);
            processor.setSoloBand (
                processor.getSoloBand() == band ? -1 : band);
            repaint();
        };
        bypass.onClick = [this, band]
        {
            const auto parameters = processor.getCurrentMultibandParameters();
            processor.setSelectedBand (band);
            setParameter (
                ParamIDs::band (band, "Bypass"),
                parameters.bands[static_cast<size_t> (band)].bypass
                    ? 0.0f : 1.0f);
            repaint();
        };
    }

    addAndMakeVisible (trimControl);

    linkButton.onClick = [this]
    {
        processor.setMultibandLinkedFromUi (
            ! processor.getCurrentMultibandParameters().linked);
    };
    bandCountButton.onClick = [this]
    {
        showBandCountMenu();
    };
    phaseButton.onClick = [this]
    {
        const auto phase = processor.getCurrentMultibandParameters().phaseMode;
        setParameter (ParamIDs::multibandPhase, phase == 0 ? 1.0f : 0.0f);
    };
    soloButton.onClick = [this]
    {
        const auto selected = processor.getSelectedBand();
        processor.setSoloBand (
            processor.getSoloBand() == selected ? -1 : selected);
    };
    bypassButton.onClick = [this]
    {
        const auto selected = processor.getSelectedBand();
        const auto multiband = processor.getCurrentMultibandParameters();
        setParameter (
            ParamIDs::band (selected, "Bypass"),
            multiband.bands[static_cast<size_t> (selected)].bypass ? 0.0f : 1.0f);
    };
    bindTrimControl (processor.getSelectedBand());
    setMouseCursor (juce::MouseCursor::NormalCursor);
    updateControls();
    startTimerHz (30);
}

MultibandPanel::~MultibandPanel()
{
    stopTimer();
    endTrimDrag();
    trimAttachment.reset();
    processor.setSoloBand (-1);
}

void MultibandPanel::setParameter (const juce::String& id, float plainValue)
{
    if (auto* parameter = processor.parameters.getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (plainValue));
        parameter->endChangeGesture();
    }
}

juce::Rectangle<float> MultibandPanel::analyzerBounds() const
{
    return getLocalBounds().toFloat().reduced (4.0f * scaleOf (*this));
}

float MultibandPanel::frequencyToX (float frequency) const
{
    const auto bounds = analyzerBounds();
    const auto maximum = static_cast<float> (
        juce::jmin (20000.0, 0.45 * juce::jmax (1.0, processor.getSampleRate())));
    const auto normalised = std::log (juce::jlimit (20.0f, maximum, frequency) / 20.0f)
        / std::log (maximum / 20.0f);
    return bounds.getX() + normalised * bounds.getWidth();
}

float MultibandPanel::xToFrequency (float x) const
{
    const auto bounds = analyzerBounds();
    const auto maximum = static_cast<float> (
        juce::jmin (20000.0, 0.45 * juce::jmax (1.0, processor.getSampleRate())));
    const auto normalised = juce::jlimit (
        0.0f, 1.0f, (x - bounds.getX()) / bounds.getWidth());
    return 20.0f * std::pow (maximum / 20.0f, normalised);
}

juce::Rectangle<float> MultibandPanel::slopeBadgeBounds (int crossover) const
{
    const auto parameters = processor.getCurrentMultibandParameters();
    if (crossover < 0 || crossover >= parameters.bandCount - 1)
        return {};
    const auto scale = scaleOf (*this);
    const auto x = frequencyToX (
        parameters.crossoverHz[static_cast<size_t> (crossover)]);
    const auto width = 64.0f * scale;
    return { x - 0.5f * width,
             analyzerBounds().getY() + 4.0f * scale,
             width,
             20.0f * scale };
}

juce::Rectangle<float> MultibandPanel::frequencyTooltipBounds (
    int crossover) const
{
    const auto parameters = processor.getCurrentMultibandParameters();
    if (crossover < 0 || crossover >= parameters.bandCount - 1)
        return {};
    const auto scale = scaleOf (*this);
    const auto x = frequencyToX (
        parameters.crossoverHz[static_cast<size_t> (crossover)]);
    return { x - 32.0f * scale,
             analyzerBounds().getBottom() - 35.0f * scale,
             64.0f * scale,
             17.0f * scale };
}

int MultibandPanel::crossoverAt (juce::Point<float> position,
                                 bool badgeOnly) const
{
    const auto parameters = processor.getCurrentMultibandParameters();
    const auto bounds = analyzerBounds();
    for (int crossover = 0; crossover < parameters.bandCount - 1; ++crossover)
    {
        const auto x = frequencyToX (
            parameters.crossoverHz[static_cast<size_t> (crossover)]);
        const auto hit = badgeOnly
            ? slopeBadgeBounds (crossover)
            : juce::Rectangle<float> {
                x - 8.0f * scaleOf (*this), bounds.getY(),
                16.0f * scaleOf (*this), bounds.getHeight() };
        if (hit.contains (position))
            return crossover;
    }
    return -1;
}

int MultibandPanel::crossoverForResetAt (juce::Point<float> position) const
{
    const auto line = crossoverAt (position, false);
    return line >= 0 ? line : crossoverAt (position, true);
}

int MultibandPanel::bandAt (float x) const
{
    const auto parameters = processor.getCurrentMultibandParameters();
    for (int crossover = 0; crossover < parameters.bandCount - 1; ++crossover)
        if (x < frequencyToX (
                parameters.crossoverHz[static_cast<size_t> (crossover)]))
            return crossover;
    return parameters.bandCount - 1;
}

juce::Rectangle<float> MultibandPanel::bandBounds (int band) const
{
    const auto parameters = processor.getCurrentMultibandParameters();
    const auto bounds = analyzerBounds();
    if (band < 0 || band >= parameters.bandCount)
        return {};
    const auto left = band == 0
        ? bounds.getX()
        : frequencyToX (
            parameters.crossoverHz[static_cast<size_t> (band - 1)]);
    const auto right = band == parameters.bandCount - 1
        ? bounds.getRight()
        : frequencyToX (
            parameters.crossoverHz[static_cast<size_t> (band)]);
    return { left, bounds.getY(), right - left, bounds.getHeight() };
}

float MultibandPanel::trimToY (float trimDb) const
{
    const auto bounds = analyzerBounds();
    return bounds.getCentreY()
        - juce::jlimit (-12.0f, 12.0f, trimDb)
            / 12.0f * bounds.getHeight() * 0.33f;
}

float MultibandPanel::yToTrim (float y) const
{
    const auto bounds = analyzerBounds();
    return juce::jlimit (
        -12.0f, 12.0f,
        (bounds.getCentreY() - y)
            / (bounds.getHeight() * 0.33f) * 12.0f);
}

int MultibandPanel::trimAt (juce::Point<float> position) const
{
    const auto parameters = processor.getCurrentMultibandParameters();
    const auto tolerance = 9.0f * scaleOf (*this);
    for (int band = 0; band < parameters.bandCount; ++band)
    {
        const auto bounds = bandBounds (band);
        const auto trimY = trimToY (
            parameters.bands[static_cast<size_t> (band)].trimDb);
        if (position.x >= bounds.getX() + 3.0f * scaleOf (*this)
            && position.x <= bounds.getRight() - 3.0f * scaleOf (*this)
            && std::abs (position.y - trimY) <= tolerance)
            return band;
    }
    return -1;
}

void MultibandPanel::resetTrim (int band)
{
    if (band < 0)
        return;
    processor.setSelectedBand (band);
    bindTrimControl (band);
    setParameter (ParamIDs::band (band, "Trim"), 0.0f);
    repaint();
}

void MultibandPanel::resetCrossover (int crossover)
{
    if (crossover < 0
        || crossover >= MultibandParameters::maximumCrossovers)
        return;
    setParameter (
        ParamIDs::crossoverFrequency (crossover),
        defaultCrossoverFrequencies[static_cast<size_t> (crossover)]);
    repaint();
}

void MultibandPanel::writeBandParameters (
    int band, const BandParameters& values)
{
    const auto write = [this, band] (const char* suffix, float value)
    {
        setParameter (ParamIDs::band (band, suffix), value);
    };
    const auto& saturation = values.saturation;
    write ("Mode", static_cast<float> (saturation.mode));
    write ("Drive", saturation.driveDb);
    write ("Character", saturation.character);
    write ("Secondary", saturation.secondary);
    write ("Asym", saturation.asymmetry);
    write ("AsymStereo", saturation.asymmetryStereo ? 1.0f : 0.0f);
    write ("Tone", saturation.tone);
    write ("Stages", static_cast<float> (saturation.stages));
    write ("Mix", saturation.mix);
    write ("Route", static_cast<float> (saturation.route));
    write ("Placement", saturation.placementPercent);
    write ("Dynamic", saturation.dynamicPercent);
    write ("Speed", saturation.speedPercent);
    write ("InputHp", saturation.inputHpHz);
    write ("OutputLp", saturation.outputLpHz);
    write ("Bypass", values.bypass ? 1.0f : 0.0f);
    write ("Trim", values.trimDb);
}

void MultibandPanel::insertCrossover (int band, float frequency)
{
    const auto parameters = processor.getCurrentMultibandParameters();
    if (parameters.bandCount >= MultibandParameters::maximumBands)
        return;
    band = juce::jlimit (0, parameters.bandCount - 1, band);
    for (int destination = parameters.bandCount;
         destination > band + 1;
         --destination)
        writeBandParameters (
            destination,
            parameters.bands[static_cast<size_t> (destination - 1)]);
    writeBandParameters (
        band + 1, parameters.bands[static_cast<size_t> (band)]);

    for (int destination = parameters.bandCount - 1;
         destination > band;
         --destination)
    {
        setParameter (
            ParamIDs::crossoverFrequency (destination),
            parameters.crossoverHz[static_cast<size_t> (destination - 1)]);
        setParameter (
            ParamIDs::crossoverSlope (destination),
            static_cast<float> (parameters.crossoverSlope[
                static_cast<size_t> (destination - 1)]));
    }
    setParameter (ParamIDs::crossoverFrequency (band), frequency);
    setParameter (ParamIDs::crossoverSlope (band), 2.0f);
    setParameter (
        ParamIDs::multibandBandCount,
        static_cast<float> (parameters.bandCount - 1));
    processor.setSelectedBand (band + 1);
    ghostCrossoverX = -1.0f;
    repaint();
}

void MultibandPanel::removeCrossover (int crossover)
{
    const auto parameters = processor.getCurrentMultibandParameters();
    if (parameters.bandCount <= 2
        || crossover < 0
        || crossover >= parameters.bandCount - 1)
        return;
    const auto removedBand = crossover + 1;
    for (int destination = removedBand;
         destination < parameters.bandCount - 1;
         ++destination)
        writeBandParameters (
            destination,
            parameters.bands[static_cast<size_t> (destination + 1)]);
    for (int destination = crossover;
         destination < parameters.bandCount - 2;
         ++destination)
    {
        setParameter (
            ParamIDs::crossoverFrequency (destination),
            parameters.crossoverHz[static_cast<size_t> (destination + 1)]);
        setParameter (
            ParamIDs::crossoverSlope (destination),
            static_cast<float> (parameters.crossoverSlope[
                static_cast<size_t> (destination + 1)]));
    }
    setParameter (
        ParamIDs::multibandBandCount,
        static_cast<float> (parameters.bandCount - 3));
    processor.setSelectedBand (juce::jlimit (
        0, parameters.bandCount - 2,
        processor.getSelectedBand() > removedBand
            ? processor.getSelectedBand() - 1
            : juce::jmin (processor.getSelectedBand(), removedBand - 1)));
    processor.setSoloBand (-1);
    repaint();
}

void MultibandPanel::bindTrimControl (int band)
{
    const auto parameters = processor.getCurrentMultibandParameters();
    band = juce::jlimit (0, parameters.bandCount - 1, band);
    if (band == trimBoundBand && trimAttachment != nullptr)
        return;
    trimAttachment.reset();
    trimBoundBand = band;
    trimAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameters,
            ParamIDs::band (band, "Trim"),
            trimControl);
}

void MultibandPanel::beginTrimDrag (int band, float y)
{
    endTrimDrag();
    processor.setSelectedBand (band);
    bindTrimControl (band);
    draggedTrimBand = band;
    draggedTrimParameter = processor.parameters.getParameter (
        ParamIDs::band (band, "Trim"));
    if (draggedTrimParameter != nullptr)
        draggedTrimParameter->beginChangeGesture();
    updateTrimDrag (y);
}

void MultibandPanel::updateTrimDrag (float y)
{
    if (draggedTrimBand < 0 || draggedTrimParameter == nullptr)
        return;
    const auto plainValue = draggedTrimParameter->getNormalisableRange()
        .snapToLegalValue (yToTrim (y));
    draggedTrimParameter->setValueNotifyingHost (
        draggedTrimParameter->convertTo0to1 (plainValue));
    trimControl.setValue (plainValue, juce::dontSendNotification);
    repaint();
}

void MultibandPanel::endTrimDrag()
{
    if (draggedTrimParameter != nullptr)
        draggedTrimParameter->endChangeGesture();
    draggedTrimParameter = nullptr;
    draggedTrimBand = -1;
}

void MultibandPanel::showBandCountMenu()
{
    if (simpleMenu != nullptr
        && simpleMenu->isShowingFor (&bandCountButton))
    {
        simpleMenu->close();
        return;
    }
    const auto selected = processor.getCurrentMultibandParameters().bandCount;
    const auto safeThis = juce::Component::SafePointer<MultibandPanel> (this);
    simpleMenu.reset();
    simpleMenu = std::make_unique<PrototypeSimpleMenuWindow> (
        juce::StringArray { "2 BANDS", "3 BANDS", "4 BANDS" },
        selected - 2,
        PrototypeSimpleMenuWindow::Style::plain,
        &bandCountButton,
        bandCountButton.getScreenBounds(),
        *getParentComponent(),
        scaleOf (*this),
        [safeThis] (int choice)
        {
            if (safeThis != nullptr && choice >= 0 && choice <= 2)
                safeThis->setParameter (
                    ParamIDs::multibandBandCount,
                    static_cast<float> (choice));
        });
}

void MultibandPanel::showSlopeMenu (int crossover)
{
    const auto selected = processor.getCurrentMultibandParameters()
        .crossoverSlope[static_cast<size_t> (crossover)];
    const auto target = localAreaToGlobal (
        slopeBadgeBounds (crossover).toNearestInt());
    const auto safeThis = juce::Component::SafePointer<MultibandPanel> (this);
    simpleMenu.reset();
    simpleMenu = std::make_unique<PrototypeSimpleMenuWindow> (
        juce::StringArray { "6 dB/oct", "12 dB/oct", "24 dB/oct",
                            "36 dB/oct", "48 dB/oct" },
        selected,
        PrototypeSimpleMenuWindow::Style::plain,
        nullptr,
        target,
        *getParentComponent(),
        scaleOf (*this),
        [safeThis, crossover] (int choice)
        {
            if (safeThis != nullptr && choice >= 0 && choice < 5)
                safeThis->setParameter (
                    ParamIDs::crossoverSlope (crossover),
                    static_cast<float> (choice));
        });
}

void MultibandPanel::timerCallback()
{
    if (processor.pullAnalyzerFrames (
        incomingInput.data(), incomingOutput.data(),
        static_cast<int> (incomingInput.size())) != 0)
        updateSpectrum();
    updateControls();
    repaint();
}

void MultibandPanel::updateSpectrum()
{
    const auto rate = processor.getSampleRate() > 0.0
        ? processor.getSampleRate() : 48000.0;
    const auto frameSeconds = static_cast<float> (spectrumPublishHop / rate);
    const auto averaging = 1.0f - std::exp (
        -frameSeconds / analyzerAveragingSeconds);
    const auto decay = analyzerDecayDb * frameSeconds * 30.0f;
    const auto smoothSpectrum = [] (const auto& source,
                                    auto& spectrum,
                                    float averagingAmount,
                                    float decayAmount)
    {
        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const auto target = source[static_cast<size_t> (bin)];
            auto& current = spectrum[static_cast<size_t> (bin)];
            current += averagingAmount * (target - current);
            current = juce::jmax (target, current - decayAmount);
        }
    };
    smoothSpectrum (incomingInput, inputSpectrum, averaging, decay);
    smoothSpectrum (incomingOutput, outputSpectrum, averaging, decay);
}

void MultibandPanel::updateControls()
{
    const auto parameters = processor.getCurrentMultibandParameters();
    if (laidOutBandCount != parameters.bandCount)
    {
        laidOutBandCount = parameters.bandCount;
        resized();
    }
    const auto selected = juce::jlimit (
        0, parameters.bandCount - 1, processor.getSelectedBand());
    if (selected != processor.getSelectedBand())
        processor.setSelectedBand (selected);
    linkButton.setToggleState (parameters.linked, juce::dontSendNotification);
    bandCountButton.setButtonText (
        juce::String (parameters.bandCount) + " BANDS");
    phaseButton.setButtonText (
        parameters.phaseMode == 0 ? "MIN PHASE" : "LINEAR PHASE");
    phaseButton.setToggleState (
        parameters.phaseMode == 1, juce::dontSendNotification);
    soloButton.setToggleState (
        processor.getSoloBand() == selected, juce::dontSendNotification);
    bypassButton.setToggleState (
        parameters.bands[static_cast<size_t> (selected)].bypass,
        juce::dontSendNotification);
    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
    {
        const auto visible = band < parameters.bandCount;
        soloButtons[static_cast<size_t> (band)].setVisible (visible);
        bypassButtons[static_cast<size_t> (band)].setVisible (visible);
        soloButtons[static_cast<size_t> (band)].setToggleState (
            processor.getSoloBand() == band, juce::dontSendNotification);
        bypassButtons[static_cast<size_t> (band)].setToggleState (
            visible
                && parameters.bands[static_cast<size_t> (band)].bypass,
            juce::dontSendNotification);
    }
    if (draggedTrimBand < 0)
        bindTrimControl (selected);
}

void MultibandPanel::paint (juce::Graphics& graphics)
{
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    graphics.fillAll (foreground);
    const auto bounds = analyzerBounds();
    graphics.setColour (background);
    graphics.fillRect (bounds);

    const auto parameters = processor.getCurrentMultibandParameters();
    const auto selected = processor.getSelectedBand();
    auto left = bounds.getX();
    for (int band = 0; band < parameters.bandCount; ++band)
    {
        const auto right = band < parameters.bandCount - 1
            ? frequencyToX (parameters.crossoverHz[static_cast<size_t> (band)])
            : bounds.getRight();
        if (band == selected)
        {
            graphics.setColour (foreground.withAlpha (0.12f));
            graphics.fillRect (juce::Rectangle<float> {
                left, bounds.getY(), right - left, bounds.getHeight() });
        }
        left = right;
    }

    graphics.setColour (foreground.withAlpha (0.11f));
    for (const auto ratio : { 0.164f, 0.5f, 0.836f })
    {
        const auto y = bounds.getY() + ratio * bounds.getHeight();
        graphics.drawHorizontalLine (
            juce::roundToInt (y), bounds.getX(), bounds.getRight());
    }
    for (const auto frequency : {
             50.0f, 100.0f, 500.0f, 1000.0f,
             2000.0f, 5000.0f, 10000.0f })
    {
        const auto x = frequencyToX (frequency);
        graphics.drawVerticalLine (
            juce::roundToInt (x), bounds.getY(), bounds.getBottom());
        graphics.setColour (foreground.withAlpha (0.72f));
        const auto label = frequency >= 1000.0f
            ? juce::String (juce::roundToInt (frequency / 1000.0f)) + "k"
            : juce::String (juce::roundToInt (frequency));
        drawPrototypeText (
            graphics, label,
            juce::Rectangle<float> {
                x + 4.0f * scaleOf (*this),
                bounds.getBottom() - 16.0f * scaleOf (*this),
                34.0f * scaleOf (*this),
                12.0f * scaleOf (*this) },
            9.0f, true, 0.0f,
            foreground.withAlpha (0.72f),
            juce::Justification::centredLeft, scaleOf (*this));
        graphics.setColour (foreground.withAlpha (0.11f));
    }

    const auto scale = scaleOf (*this);

    const auto makePath = [&] (const auto& spectrum)
    {
        juce::Path path;
        const auto rate = processor.getSampleRate() > 0.0
            ? processor.getSampleRate() : 48000.0;
        auto started = false;
        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const auto frequency = static_cast<float> (bin * rate / fftSize);
            if (frequency < 20.0f || frequency > 20000.0f)
                continue;
            const auto x = frequencyToX (frequency);
            const auto tilted = spectrum[static_cast<size_t> (bin)]
                + analyzerTiltDbPerOctave * std::log2 (frequency / 1000.0f);
            const auto db = juce::jlimit (
                analyzerFloorDb, analyzerCeilingDb, tilted);
            const auto y = bounds.getY()
                + ((analyzerCeilingDb - db)
                   / (analyzerCeilingDb - analyzerFloorDb)) * bounds.getHeight();
            if (! started)
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
                path.lineTo (x, y);
        }
        return path;
    };
    const auto hasSpectrum = std::any_of (
        inputSpectrum.begin() + 1, inputSpectrum.end(),
        [] (float value) { return value > analyzerFloorDb + 0.1f; });
    const auto fallbackPath = [&] (float offset, float strength)
    {
        juce::Path path;
        for (int index = 0; index <= 130; ++index)
        {
            const auto indexFloat = static_cast<float> (index);
            const auto x = bounds.getX()
                + indexFloat / 130.0f * bounds.getWidth();
            const auto shape = std::sin (indexFloat * 0.29f + offset)
                + 0.52f * std::sin (indexFloat * 0.83f + offset * 1.7f)
                + 0.26f * std::sin (indexFloat * 1.91f);
            const auto envelope = 0.55f
                + 0.45f * std::sin (juce::MathConstants<float>::pi
                                    * indexFloat / 130.0f);
            const auto y = bounds.getY() + 0.798f * bounds.getHeight()
                - strength * scale * envelope * (2.2f + shape);
            if (index == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        return path;
    };
    const auto drawSpectrum = [&] (const auto& spectrum,
                                   float lineAlpha,
                                   float strokeWidth,
                                   float fallbackOffset,
                                   float fallbackStrength)
    {
        const auto path = hasSpectrum
            ? makePath (spectrum)
            : fallbackPath (fallbackOffset, fallbackStrength);
        graphics.setColour (foreground.withAlpha (lineAlpha));
        graphics.strokePath (
            path,
            juce::PathStrokeType (strokeWidth * scaleOf (*this)));
    };
    drawSpectrum (inputSpectrum, 0.20f, 1.0f, 0.3f, 12.0f);
    drawSpectrum (outputSpectrum, 0.48f, 1.5f, 1.1f, 15.0f);

    for (int band = 0; band < parameters.bandCount; ++band)
    {
        auto bandArea = bandBounds (band);
        const auto trimY = trimToY (
            parameters.bands[static_cast<size_t> (band)].trimDb);
        graphics.setColour (foreground.withAlpha (0.76f));
        graphics.drawLine (
            bandArea.getX() + 6.0f * scale,
            trimY,
            bandArea.getRight() - 6.0f * scale,
            trimY,
            2.0f * scale);
        const auto handle = juce::Rectangle<float> {
            bandArea.getCentreX() - 5.0f * scale,
            trimY - 5.0f * scale,
            10.0f * scale,
            10.0f * scale };
        graphics.setColour (foreground);
        graphics.fillRect (handle);
        graphics.setColour (background);
        graphics.drawRect (handle, juce::jmax (1.0f, scale));
    }

    if (parameters.bandCount < MultibandParameters::maximumBands
        && ghostCrossoverX >= bounds.getX())
    {
        graphics.setColour (foreground.withAlpha (0.25f));
        graphics.drawLine (
            ghostCrossoverX, bounds.getY(),
            ghostCrossoverX, bounds.getBottom(),
            1.25f * scale);
    }

    constexpr std::array<int, 5> slopes { 6, 12, 24, 36, 48 };
    for (int crossover = 0; crossover < parameters.bandCount - 1; ++crossover)
    {
        const auto x = frequencyToX (
            parameters.crossoverHz[static_cast<size_t> (crossover)]);
        graphics.setColour (foreground);
        graphics.drawLine (
            x, bounds.getY(), x, bounds.getBottom(), 1.25f * scale);
        if (hoveredCrossover == crossover || draggedCrossover == crossover)
        {
            const auto frequencyBadge = frequencyTooltipBounds (crossover);
            const auto slopeBadge = slopeBadgeBounds (crossover);
            graphics.setColour (foreground);
            graphics.fillRect (frequencyBadge);
            graphics.fillRect (slopeBadge);
            graphics.setColour (background);
            const auto frequency = parameters.crossoverHz[
                static_cast<size_t> (crossover)];
            drawPrototypeText (
                graphics,
                frequency >= 1000.0f
                    ? juce::String (
                        frequency / 1000.0f,
                        frequency >= 10000.0f ? 1 : 2) + " kHz"
                    : juce::String (juce::roundToInt (frequency)) + " Hz",
                frequencyBadge,
                9.0f, true, 0.0f, background,
                juce::Justification::centred, scaleOf (*this));
            drawPrototypeText (
                graphics,
                juce::String (slopes[static_cast<size_t> (
                    parameters.crossoverSlope[static_cast<size_t> (crossover)])])
                    + " dB/oct",
                slopeBadge,
                9.0f, true, 0.0f, background,
                juce::Justification::centred, scaleOf (*this));
        }
    }

}

void MultibandPanel::resized()
{
    if (simpleMenu != nullptr)
        simpleMenu->close();
    for (auto* component : {
             static_cast<juce::Component*> (&linkButton),
             static_cast<juce::Component*> (&bandCountButton),
             static_cast<juce::Component*> (&phaseButton),
             static_cast<juce::Component*> (&soloButton),
             static_cast<juce::Component*> (&bypassButton),
             static_cast<juce::Component*> (&trimControl) })
        component->setVisible (false);

    const auto parameters = processor.getCurrentMultibandParameters();
    const auto scale = scaleOf (*this);
    const auto analyzer = analyzerBounds();
    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
    {
        const auto visible = band < parameters.bandCount;
        auto& solo = soloButtons[static_cast<size_t> (band)];
        auto& bypass = bypassButtons[static_cast<size_t> (band)];
        solo.setVisible (visible);
        bypass.setVisible (visible);
        if (! visible)
            continue;
        const auto x = bandBounds (band).getX() + 7.0f * scale;
        solo.setBounds (juce::Rectangle<float> {
            x, analyzer.getY() + 0.164f * 160.0f * scale,
            18.0f * scale, 16.0f * scale }.toNearestInt());
        bypass.setBounds (juce::Rectangle<float> {
            x, analyzer.getY() + 52.0f * scale,
            18.0f * scale, 16.0f * scale }.toNearestInt());
    }
}

void MultibandPanel::mouseMove (const juce::MouseEvent& event)
{
    const auto hoveredBadge = crossoverAt (event.position, true);
    hoveredCrossover = hoveredBadge >= 0
        ? hoveredBadge : crossoverAt (event.position, false);
    hoveredTrimBand = hoveredCrossover < 0 ? trimAt (event.position) : -1;
    const auto parameters = processor.getCurrentMultibandParameters();
    ghostCrossoverX = -1.0f;
    if (parameters.bandCount < MultibandParameters::maximumBands
        && hoveredCrossover < 0
        && analyzerBounds().contains (event.position)
        && event.position.y < analyzerBounds().getCentreY())
    {
        const auto band = bandAt (event.position.x);
        constexpr auto spacing = 1.2599210498948732f;
        const auto lower = band == 0
            ? 20.0f
            : parameters.crossoverHz[static_cast<size_t> (band - 1)] * spacing;
        const auto upper = band >= parameters.bandCount - 1
            ? 20000.0f
            : parameters.crossoverHz[static_cast<size_t> (band)] / spacing;
        const auto frequency = xToFrequency (event.position.x);
        if (frequency >= lower && frequency <= upper)
            ghostCrossoverX = event.position.x;
    }
    setMouseCursor (
        hoveredBadge >= 0
            ? juce::MouseCursor::PointingHandCursor
            : (hoveredCrossover >= 0
            ? juce::MouseCursor::LeftRightResizeCursor
            : (hoveredTrimBand >= 0
                ? juce::MouseCursor::UpDownResizeCursor
                : juce::MouseCursor::NormalCursor)));
    repaint();
}

void MultibandPanel::mouseExit (const juce::MouseEvent&)
{
    if (draggedCrossover < 0)
        hoveredCrossover = -1;
    if (draggedTrimBand < 0)
        hoveredTrimBand = -1;
    ghostCrossoverX = -1.0f;
    repaint();
}

void MultibandPanel::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        const auto crossover = crossoverForResetAt (event.position);
        if (crossover >= 0)
            resetCrossover (crossover);
        else
            resetTrim (trimAt (event.position));
        return;
    }
    const auto parameters = processor.getCurrentMultibandParameters();
    const auto scale = scaleOf (*this);
    const auto analyzer = analyzerBounds();
    for (int band = 0; band < parameters.bandCount; ++band)
    {
        const auto area = bandBounds (band);
        const auto buttonX = area.getX() + 7.0f * scale;
        const auto hit = [&] (float y)
        {
            return juce::Rectangle<float> {
                buttonX - 3.0f * scale,
                analyzer.getY() + y * scale - 4.0f * scale,
                24.0f * scale,
                24.0f * scale };
        };
        if (hit (0.164f * 160.0f).contains (event.position))
        {
            processor.setSelectedBand (band);
            processor.setSoloBand (
                processor.getSoloBand() == band ? -1 : band);
            repaint();
            return;
        }
        if (hit (52.0f).contains (event.position))
        {
            processor.setSelectedBand (band);
            setParameter (
                ParamIDs::band (band, "Bypass"),
                parameters.bands[static_cast<size_t> (band)].bypass
                    ? 0.0f : 1.0f);
            repaint();
            return;
        }
    }
    const auto badge = crossoverAt (event.position, true);
    if (badge >= 0 && badge == hoveredCrossover)
    {
        showSlopeMenu (badge);
        return;
    }
    draggedCrossover = crossoverAt (event.position, false);
    if (draggedCrossover >= 0)
        return;
    if (ghostCrossoverX >= analyzerBounds().getX()
        && parameters.bandCount < MultibandParameters::maximumBands)
    {
        insertCrossover (
            bandAt (event.position.x), xToFrequency (event.position.x));
        return;
    }
    const auto trimBand = trimAt (event.position);
    if (trimBand >= 0)
    {
        beginTrimDrag (trimBand, event.position.y);
        return;
    }
    if (analyzerBounds().contains (event.position))
    {
        const auto band = bandAt (event.position.x);
        processor.setSelectedBand (band);
        bindTrimControl (band);
    }
}

void MultibandPanel::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto crossover = crossoverForResetAt (event.position);
    if (crossover >= 0
        && processor.getCurrentMultibandParameters().bandCount > 2)
        removeCrossover (crossover);
    else
        resetTrim (trimAt (event.position));
}

void MultibandPanel::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedTrimBand >= 0)
    {
        updateTrimDrag (event.position.y);
        return;
    }
    if (draggedCrossover < 0)
        return;
    auto frequency = xToFrequency (event.position.x);
    const auto parameters = processor.getCurrentMultibandParameters();
    constexpr auto ratio = 1.2599210498948732f;
    const auto lower = draggedCrossover == 0
        ? 20.0f
        : parameters.crossoverHz[static_cast<size_t> (draggedCrossover - 1)]
            * ratio;
    const auto maximum = static_cast<float> (
        juce::jmin (20000.0, 0.45 * juce::jmax (1.0, processor.getSampleRate())));
    const auto upper = draggedCrossover >= parameters.bandCount - 2
        ? maximum
        : parameters.crossoverHz[static_cast<size_t> (draggedCrossover + 1)]
            / ratio;
    frequency = juce::jlimit (lower, upper, frequency);
    setParameter (ParamIDs::crossoverFrequency (draggedCrossover), frequency);
}

void MultibandPanel::mouseUp (const juce::MouseEvent&)
{
    draggedCrossover = -1;
    endTrimDrag();
}

void ResponseDisplay::timerCallback()
{
    repaint();
}

void ResponseDisplay::paint (juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat();
    const auto scale = scaleOf (*this);
    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    graphics.setColour (background);
    graphics.fillRect (bounds);

    auto parameters = processor.getCurrentParameters();
    const auto multiband = processor.getCurrentMultibandParameters();
    if (multiband.enabled && ! multiband.linked)
        parameters = multiband.bands[static_cast<size_t> (
            juce::jlimit (0, multiband.bandCount - 1,
                          processor.getSelectedBand()))].saturation;
    const auto displaySampleRate =
        processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const auto visualizationChanged =
        ! visualizationValid
        || parameters.mode != visualizedParameters.mode
        || differs (parameters.driveDb, visualizedParameters.driveDb)
        || differs (parameters.character, visualizedParameters.character)
        || differs (parameters.secondary, visualizedParameters.secondary)
        || differs (parameters.asymmetry, visualizedParameters.asymmetry)
        || parameters.asymmetryStereo
            != visualizedParameters.asymmetryStereo
        || differs (parameters.tone, visualizedParameters.tone)
        || parameters.stages != visualizedParameters.stages
        || parameters.quality != visualizedParameters.quality
        || std::abs (displaySampleRate - visualizedSampleRate) > 0.5;
    if (visualizationChanged)
    {
        DistortionEngine::makeVisualization (
            parameters, displaySampleRate, visualization);
        visualizedParameters = parameters;
        visualizedSampleRate = displaySampleRate;
        visualizationValid = true;
    }

    auto graph = juce::Rectangle<float> {
        8.0f * scale,
        10.0f * scale,
        bounds.getWidth() - 16.0f * scale,
        bounds.getHeight() - 20.0f * scale
    };
    for (const auto ratio : { 0.25f, 0.5f, 0.75f })
    {
        graphics.setColour (foreground.withAlpha (
            ratio == 0.5f ? 0.56f : 0.10f));
        const auto y = graph.getY() + ratio * graph.getHeight();
        const auto x = graph.getX() + ratio * graph.getWidth();
        graphics.drawHorizontalLine (
            juce::roundToInt (y), graph.getX(), graph.getRight());
        graphics.drawVerticalLine (
            juce::roundToInt (x), graph.getY(), graph.getBottom());
    }

    auto makePath = [&] (const std::array<float, DistortionEngine::Visualization::pointCount>& values)
    {
        juce::Path path;
        for (int point = 0;
             point < DistortionEngine::Visualization::pointCount;
             ++point)
        {
            const auto position = static_cast<float> (point)
                / static_cast<float> (
                    DistortionEngine::Visualization::pointCount - 1);
            const auto value = values[static_cast<size_t> (point)];
            const auto px = graph.getX() + position * graph.getWidth();
            const auto normalisedValue = visualization.spectralDomain
                ? 2.0f * value / 1.5f - 1.0f
                : value / 1.25f;
            const auto py = graph.getCentreY()
                - juce::jlimit (-1.0f, 1.0f, normalisedValue)
                    * graph.getHeight() * 0.46f;
            if (point == 0)
                path.startNewSubPath (px, py);
            else
                path.lineTo (px, py);
        }
        return path;
    };

    graphics.setColour (foreground.withAlpha (0.24f));
    graphics.strokePath (
        makePath (visualization.input),
        juce::PathStrokeType (1.25f * scale));
    graphics.setColour (foreground);
    graphics.strokePath (
        makePath (visualization.output),
        juce::PathStrokeType (
            3.0f * scale,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

}

DefaultDistortionAudioProcessorEditor::DefaultDistortionAudioProcessorEditor (
    DefaultDistortionAudioProcessor& owner)
    : AudioProcessorEditor (&owner),
      ownerProcessor (owner),
      responseDisplay (owner),
      levelMeters (owner),
      multibandPanel (owner)
{
    ownerProcessor.setAnalyzerEnabled (true);
    lookAndFeel.setInverted (loadLightTheme());
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setResizable (true, false);
    const auto initiallyExpanded =
        ownerProcessor.getCurrentMultibandParameters().enabled;
    multibandVisible = initiallyExpanded;
    getConstrainer()->setFixedAspectRatio (
        static_cast<double> (ui::designWidth)
            / (initiallyExpanded ? ui::expandedHeight : ui::compactHeight));
    setResizeLimits (
        ui::designWidth,
        initiallyExpanded ? ui::expandedHeight : ui::compactHeight,
        3 * ui::designWidth,
        3 * (initiallyExpanded ? ui::expandedHeight : ui::compactHeight));
    setSize (
        ui::designWidth,
        initiallyExpanded ? ui::expandedHeight : ui::compactHeight);

    brandLabel.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    brandLabel.onClick = [this] { togglePalette(); };
    addAndMakeVisible (brandLabel);

    modeButton.onClick = [this] { showModeMenu(); };
    addAndMakeVisible (modeButton);
    previousModeButton.onClick = [this] { stepMode (-1); };
    nextModeButton.onClick = [this] { stepMode (1); };
    addAndMakeVisible (previousModeButton);
    addAndMakeVisible (nextModeButton);

    qualityButton.onClick = [this] { showQualityMenu(); };
    addAndMakeVisible (qualityButton);

    autoGainButton.setClickingTogglesState (false);
    autoGainButton.onClick = [this] { cycleAutoGain(); };
    addAndMakeVisible (autoGainButton);
    pluginPowerButton.setClickingTogglesState (true);
    pluginPowerButton.setWantsKeyboardFocus (false);
    pluginPowerButton.onStateChange = [this]
    {
        pluginPowerButton.setValueText (
            pluginPowerButton.getToggleState() ? "ON" : "OFF");
    };
    addAndMakeVisible (pluginPowerButton);
    addAndMakeVisible (asymStereoButton);
    routeButton.onClick = [this] { cycleRoute(); };
    addAndMakeVisible (routeButton);
    linkStripButton.onClick = [this]
    {
        ownerProcessor.setMultibandLinkedFromUi (
            ! ownerProcessor.getCurrentMultibandParameters().linked);
    };
    phaseStripButton.onClick = [this] { showPhaseMenu(); };
    addAndMakeVisible (linkStripButton);
    addAndMakeVisible (phaseStripButton);

    for (auto* control : {
             &drive, &character, &secondary, &asym, &tone,
             &stages, &placement, &dynamic, &speed,
             &inputHp, &outputLp, &mix, &output })
    {
        configureKnob (*control);
        addAndMakeVisible (*control);
    }
    mix.setCompactLayout (true);
    output.setCompactLayout (true);
    addAndMakeVisible (levelMeters);
    addAndMakeVisible (responseDisplay);
    multibandButton.setClickingTogglesState (true);
    multibandButton.onStateChange = [this]
    {
        multibandButton.setButtonText (
            multibandButton.getToggleState()
                ? "MULTIBAND  ON" : "MULTIBAND  OFF");
    };
    addAndMakeVisible (multibandButton);
    addAndMakeVisible (multibandPanel);
    multibandPanel.setVisible (initiallyExpanded);

    // ParameterControl paints an opaque background. Keep the linked vertical
    // controls above their neighbouring knobs so neither the connector nor
    // the left frame edge can be covered at larger editor scales.
    asymStereoButton.toFront (false);

    drive.slider.setRange (0.0, 36.0, 0.01);
    secondary.slider.setRange (0.0, 1.0, 0.001);
    asym.slider.setRange (-1.0, 1.0, 0.001);
    tone.slider.setRange (-1.0, 1.0, 0.001);
    stages.slider.setRange (1.0, 8.0, 1.0);
    placement.slider.setRange (-100.0, 100.0, 0.1);
    dynamic.slider.setRange (-100.0, 100.0, 0.1);
    speed.slider.setRange (0.0, 100.0, 0.1);
    inputHp.slider.setRange (0.0, 200.0, 0.1);
    inputHp.slider.setSkewFactorFromMidPoint (20.0);
    outputLp.slider.setRange (2000.0, 20000.0, 1.0);
    outputLp.slider.setSkewFactorFromMidPoint (6324.555);
    mix.slider.setRange (0.0, 1.0, 0.001);
    output.slider.setRange (-24.0, 12.0, 0.01);

    drive.slider.textFromValueFunction = [] (double value)
    {
        const auto clean = std::abs (value) < 0.005 ? 0.0 : value;
        return juce::String (clean, 1) + " dB";
    };
    for (auto* slider : { &asym.slider, &tone.slider })
        slider->textFromValueFunction = [] (double value)
        {
            return juce::String (juce::roundToInt (value * 100.0)) + "%";
        };
    secondary.slider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value * 100.0)) + "%";
    };
    placement.slider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value)) + "%";
    };
    dynamic.slider.textFromValueFunction = [] (double value)
    {
        const auto rounded = juce::roundToInt (value);
        return juce::String (rounded > 0 ? "+" : "")
            + juce::String (rounded) + "%";
    };
    speed.slider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value)) + "%";
    };
    inputHp.slider.textFromValueFunction = [] (double value)
    {
        return value <= 0.5 ? juce::String { "OFF" }
                            : juce::String (juce::roundToInt (value)) + " Hz";
    };
    outputLp.slider.textFromValueFunction = [] (double value)
    {
        if (value >= 19999.5)
            return juce::String { "OFF" };
        return value >= 10000.0
            ? juce::String (value / 1000.0, 1) + " kHz"
            : juce::String (value / 1000.0, 2) + " kHz";
    };
    stages.slider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value)) + " STAGE";
    };
    mix.slider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value * 100.0)) + "%";
    };
    output.slider.textFromValueFunction = [] (double value)
    {
        const auto clean = std::abs (value) < 0.005 ? 0.0 : value;
        return juce::String (clean, 1) + " dB";
    };

    drive.slider.setDoubleClickReturnValue (true, 0.0);
    character.slider.setDoubleClickReturnValue (true, 0.0);
    secondary.slider.setDoubleClickReturnValue (true, 0.0);
    asym.slider.setDoubleClickReturnValue (true, 0.0);
    tone.slider.setDoubleClickReturnValue (true, 0.0);
    stages.slider.setDoubleClickReturnValue (true, 1.0);
    placement.slider.setDoubleClickReturnValue (true, 0.0);
    dynamic.slider.setDoubleClickReturnValue (true, 0.0);
    speed.slider.setDoubleClickReturnValue (true, 50.0);
    inputHp.slider.setDoubleClickReturnValue (true, 0.0);
    outputLp.slider.setDoubleClickReturnValue (true, 20000.0);
    mix.slider.setDoubleClickReturnValue (true, 1.0);
    output.slider.setDoubleClickReturnValue (true, 0.0);

    auto& state = ownerProcessor.parameters;
    outputAttachment = std::make_unique<SliderAttachment> (
        state, ParamIDs::output, output.slider);
    if (auto* parameter = state.getParameter (ParamIDs::quality))
    {
        qualityAttachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float value)
            {
                static const std::array<juce::String, 4> labels {
                    "OFF", "2×", "4×", "8×"
                };
                qualityButton.setValueText (labels[static_cast<size_t> (
                    juce::jlimit (0, 3, juce::roundToInt (value)))]);
            });
        qualityAttachment->sendInitialUpdate();
    }
    multibandAttachment = std::make_unique<ButtonAttachment> (
        state, ParamIDs::multibandEnabled, multibandButton);
    multibandButton.setButtonText (
        multibandButton.getToggleState()
            ? "MULTIBAND  ON" : "MULTIBAND  OFF");
    pluginPowerAttachment = std::make_unique<ButtonAttachment> (
        state, ParamIDs::pluginEnabled, pluginPowerButton);
    pluginPowerButton.setValueText (
        pluginPowerButton.getToggleState() ? "ON" : "OFF");
    if (auto* parameter = state.getParameter (ParamIDs::autoGain))
    {
        autoGainAttachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float value)
            {
                updateAutoGainButton (juce::roundToInt (value));
            });
        autoGainAttachment->sendInitialUpdate();
    }
    character.slider.onDragStart = [this]
    {
        if (characterAttachment != nullptr)
            characterAttachment->beginGesture();
        beginBandGroupDrag (
            "Character",
            static_cast<float> (character.slider.getValue() / 100.0));
    };
    character.slider.onValueChange = [this]
    {
        if (! updatingCharacter && characterAttachment != nullptr)
            characterAttachment->setValueAsPartOfGesture (
                static_cast<float> (character.slider.getValue() / 100.0));
        if (! updatingCharacter)
            updateBandGroupDrag (
                static_cast<float> (character.slider.getValue() / 100.0));
    };
    character.slider.onDragEnd = [this]
    {
        if (characterAttachment != nullptr)
            characterAttachment->endGesture();
        endBandGroupDrag();
    };
    const auto installGroupDrag = [this] (
        juce::Slider& slider, juce::String suffix)
    {
        slider.onDragStart = [this, &slider, suffix]
        {
            beginBandGroupDrag (suffix, static_cast<float> (slider.getValue()));
        };
        slider.onValueChange = [this, &slider]
        {
            updateBandGroupDrag (static_cast<float> (slider.getValue()));
        };
        slider.onDragEnd = [this]
        {
            endBandGroupDrag();
        };
    };
    installGroupDrag (drive.slider, "Drive");
    installGroupDrag (secondary.slider, "Secondary");
    installGroupDrag (asym.slider, "Asym");
    installGroupDrag (tone.slider, "Tone");
    installGroupDrag (stages.slider, "Stages");
    installGroupDrag (placement.slider, "Placement");
    installGroupDrag (dynamic.slider, "Dynamic");
    installGroupDrag (speed.slider, "Speed");
    installGroupDrag (inputHp.slider, "InputHp");
    installGroupDrag (outputLp.slider, "OutputLp");
    installGroupDrag (mix.slider, "Mix");
    rebindContextualControls();
    stages.slider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value)) + " STAGE";
    };
    stages.slider.updateText();
    timerCallback();
    sendLookAndFeelChange();
    for (auto* control : {
             &drive, &character, &secondary, &asym, &tone,
             &stages, &placement, &dynamic, &speed,
             &inputHp, &outputLp, &mix, &output })
        control->applyPaletteColours();
    startTimerHz (12);
}

DefaultDistortionAudioProcessorEditor::~DefaultDistortionAudioProcessorEditor()
{
    stopTimer();
    ownerProcessor.setAnalyzerEnabled (false);
    setLookAndFeel (nullptr);
}

void DefaultDistortionAudioProcessorEditor::configureKnob (
    ParameterControl& control)
{
    control.slider.setMouseDragSensitivity (180);
}

void DefaultDistortionAudioProcessorEditor::rebindContextualControls()
{
    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    linkStripButton.setToggleState (
        multiband.linked, juce::dontSendNotification);
    phaseStripButton.setButtonText (
        multiband.phaseMode == 0 ? "PHASE  MINIMUM" : "PHASE  LINEAR");
    // Phase is a selector, not an active/inactive toggle. LINEAR must not
    // invert the closed cell.
    phaseStripButton.setToggleState (false, juce::dontSendNotification);
    const auto targetBand = multiband.enabled && ! multiband.linked
        ? juce::jlimit (0, multiband.bandCount - 1,
                        ownerProcessor.getSelectedBand())
        : -1;
    if (targetBand == boundBand)
        return;
    boundBand = targetBand;

    driveAttachment.reset();
    secondaryAttachment.reset();
    asymAttachment.reset();
    asymStereoAttachment.reset();
    toneAttachment.reset();
    stagesAttachment.reset();
    placementAttachment.reset();
    dynamicAttachment.reset();
    speedAttachment.reset();
    inputHpAttachment.reset();
    outputLpAttachment.reset();
    mixAttachment.reset();
    modeAttachment.reset();
    characterAttachment.reset();
    routeAttachment.reset();

    const auto id = [targetBand] (const char* master, const char* bandSuffix)
    {
        return targetBand < 0
            ? juce::String (master)
            : ParamIDs::band (targetBand, bandSuffix);
    };
    auto& state = ownerProcessor.parameters;
    driveAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::drive, "Drive"), drive.slider);
    secondaryAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::secondary, "Secondary"), secondary.slider);
    asymAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::asym, "Asym"), asym.slider);
    asymStereoAttachment = std::make_unique<ButtonAttachment> (
        state, id (ParamIDs::asymStereo, "AsymStereo"), asymStereoButton);
    toneAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::tone, "Tone"), tone.slider);
    stagesAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::stages, "Stages"), stages.slider);
    stages.slider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::roundToInt (value)) + " STAGE";
    };
    stages.slider.updateText();
    placementAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::placement, "Placement"), placement.slider);
    dynamicAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::dynamic, "Dynamic"), dynamic.slider);
    speedAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::speed, "Speed"), speed.slider);
    inputHpAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::inputHp, "InputHp"), inputHp.slider);
    outputLpAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::outputLp, "OutputLp"), outputLp.slider);
    mixAttachment = std::make_unique<SliderAttachment> (
        state, id (ParamIDs::mix, "Mix"), mix.slider);

    const auto routeId = id (ParamIDs::route, "Route");
    if (auto* parameter = state.getParameter (routeId))
    {
        routeAttachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float value)
            {
                routeButton.setValueText (
                    juce::roundToInt (value) == 0 ? "M/S" : "T/S");
            });
        routeAttachment->sendInitialUpdate();
    }

    const auto modeId = id (ParamIDs::mode, "Mode");
    if (auto* parameter = state.getParameter (modeId))
    {
        modeAttachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float value)
            {
                const auto mode = juce::jlimit (
                    0,
                    DistortionEngine::modeCount - 1,
                    juce::roundToInt (value));
                updateCharacterControl (mode);
                const auto displayPosition =
                    DistortionEngine::getDisplayPositionForMode (mode);
                modeButton.setMode (
                    displayPosition,
                    prototypeModeNamesByDisplay[
                        static_cast<size_t> (displayPosition)]);
                if (characterAttachment != nullptr)
                    characterAttachment->sendInitialUpdate();
            });
        modeAttachment->sendInitialUpdate();
    }

    const auto characterId = id (ParamIDs::character, "Character");
    if (auto* parameter = state.getParameter (characterId))
    {
        characterAttachment = std::make_unique<juce::ParameterAttachment> (
            *parameter,
            [this] (float rawValue)
            {
                const auto shownValue = DistortionEngine::isCharacterBipolar (
                    juce::jmax (0, displayedMode))
                    ? rawValue
                    : juce::jmax (0.0f, rawValue);
                juce::ScopedValueSetter<bool> guard (updatingCharacter, true);
                character.slider.setValue (
                    100.0 * shownValue, juce::dontSendNotification);
            });
        characterAttachment->sendInitialUpdate();
    }
}

void DefaultDistortionAudioProcessorEditor::beginBandGroupDrag (
    const juce::String& parameterSuffix,
    float displayedValue)
{
    endBandGroupDrag();
    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    if (! multiband.enabled
        || multiband.linked
        || boundBand < 0
        || ! juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
        return;

    bandGroupDrag.displayedStart = displayedValue;
    bandGroupDrag.active = true;
    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
    {
        auto* parameter = ownerProcessor.parameters.getParameter (
            ParamIDs::band (band, parameterSuffix.toRawUTF8()));
        if (parameter == nullptr)
        {
            endBandGroupDrag();
            return;
        }
        bandGroupDrag.parameters[static_cast<size_t> (band)] = parameter;
        bandGroupDrag.bandStarts[static_cast<size_t> (band)] =
            parameter->convertFrom0to1 (parameter->getValue());
        if (band != boundBand)
            parameter->beginChangeGesture();
    }
}

void DefaultDistortionAudioProcessorEditor::updateBandGroupDrag (
    float displayedValue)
{
    if (! bandGroupDrag.active)
        return;
    const auto delta = displayedValue - bandGroupDrag.displayedStart;
    for (int band = 0; band < MultibandParameters::maximumBands; ++band)
    {
        if (band == boundBand)
            continue;
        auto* parameter = bandGroupDrag.parameters[static_cast<size_t> (band)];
        if (parameter == nullptr)
            continue;
        const auto plainValue = parameter->getNormalisableRange().snapToLegalValue (
            bandGroupDrag.bandStarts[static_cast<size_t> (band)] + delta);
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (plainValue));
    }
}

void DefaultDistortionAudioProcessorEditor::endBandGroupDrag()
{
    if (bandGroupDrag.active)
        for (int band = 0; band < MultibandParameters::maximumBands; ++band)
            if (band != boundBand)
                if (auto* parameter = bandGroupDrag.parameters[
                        static_cast<size_t> (band)])
                    parameter->endChangeGesture();
    bandGroupDrag = {};
}

void DefaultDistortionAudioProcessorEditor::updateMultibandVisibility (
    bool enabled,
    bool resizeEditor)
{
    if (enabled == multibandVisible && ! resizeEditor)
        return;
    multibandVisible = enabled;
    multibandPanel.setVisible (enabled);
    const auto targetHeight = static_cast<double> (
        enabled ? ui::expandedHeight : ui::compactHeight);
    if (resizeEditor)
    {
        const auto currentWidth = getWidth();
        const auto scale = static_cast<double> (currentWidth)
            / static_cast<double> (ui::designWidth);
        setResizeLimits (1, 1, 10000, 10000);
        getConstrainer()->setFixedAspectRatio (
            static_cast<double> (ui::designWidth) / targetHeight);
        setSize (
            currentWidth,
            juce::roundToInt (targetHeight * scale));
    }
    else
        getConstrainer()->setFixedAspectRatio (
            static_cast<double> (ui::designWidth) / targetHeight);
    setResizeLimits (
        ui::designWidth,
        enabled ? ui::expandedHeight : ui::compactHeight,
        3 * ui::designWidth,
        3 * (enabled ? ui::expandedHeight : ui::compactHeight));
    resized();
    repaint();
}

void DefaultDistortionAudioProcessorEditor::showModeMenu()
{
    if (modeMenu != nullptr && modeMenu->isShowingFor (&modeButton))
    {
        modeMenu->close();
        return;
    }
    if (simpleMenu != nullptr)
        simpleMenu->close();
    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    const auto contextBand = multiband.enabled && ! multiband.linked
        ? juce::jlimit (0, multiband.bandCount - 1,
                        ownerProcessor.getSelectedBand())
        : -1;
    const auto currentMode = contextBand < 0
        ? ownerProcessor.getCurrentParameters().mode
        : multiband.bands[static_cast<size_t> (contextBand)].saturation.mode;
    const auto displaySampleRate =
        ownerProcessor.getSampleRate() > 0.0
            ? ownerProcessor.getSampleRate()
            : 48000.0;
    const auto safeThis =
        juce::Component::SafePointer<DefaultDistortionAudioProcessorEditor> (this);
    const auto scale = lookAndFeel.getUiScale();
    const auto menuBounds = localAreaToGlobal (juce::Rectangle<int> {
        juce::roundToInt (ui::main.x * scale),
        juce::roundToInt (ui::main.y * scale),
        juce::roundToInt (ui::main.width * scale),
        juce::roundToInt (ui::main.height * scale)
    });
    modeMenu.reset();
    modeMenu = std::make_unique<PrototypeModeMenuWindow> (
        currentMode,
        displaySampleRate,
        modeButton,
        menuBounds,
        *this,
        scale,
        [safeThis] (int position)
        {
            if (safeThis == nullptr)
                return;
            if (position >= 0 && position < DistortionEngine::modeCount)
                safeThis->selectMode (
                    DistortionEngine::getModeForDisplayPosition (position));
        });
}

void DefaultDistortionAudioProcessorEditor::showQualityMenu()
{
    if (simpleMenu != nullptr
        && simpleMenu->isShowingFor (&qualityButton))
    {
        simpleMenu->close();
        return;
    }
    if (modeMenu != nullptr)
        modeMenu->close();
    const auto multiplicationSign = juce::String::charToString (0x00d7);
    const auto current = ownerProcessor.getCurrentParameters().quality;
    const auto safeThis = juce::Component::SafePointer<
        DefaultDistortionAudioProcessorEditor> (this);
    simpleMenu.reset();
    simpleMenu = std::make_unique<PrototypeSimpleMenuWindow> (
        juce::StringArray { "OFF", "2" + multiplicationSign,
                            "4" + multiplicationSign,
                            "8" + multiplicationSign },
        current,
        PrototypeSimpleMenuWindow::Style::plain,
        &qualityButton,
        qualityButton.getScreenBounds(),
        *this,
        lookAndFeel.getUiScale(),
        [safeThis] (int choice)
        {
            if (safeThis == nullptr || choice < 0 || choice > 3)
                return;
            if (auto* parameter = safeThis->ownerProcessor.parameters.getParameter (
                    ParamIDs::quality))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (
                    parameter->convertTo0to1 (static_cast<float> (choice)));
                parameter->endChangeGesture();
            }
        });
}

void DefaultDistortionAudioProcessorEditor::showPhaseMenu()
{
    if (simpleMenu != nullptr
        && simpleMenu->isShowingFor (&phaseStripButton))
    {
        simpleMenu->close();
        return;
    }
    if (modeMenu != nullptr)
        modeMenu->close();
    const auto current = ownerProcessor.getCurrentMultibandParameters().phaseMode;
    const auto safeThis = juce::Component::SafePointer<
        DefaultDistortionAudioProcessorEditor> (this);
    simpleMenu.reset();
    simpleMenu = std::make_unique<PrototypeSimpleMenuWindow> (
        juce::StringArray { "PHASE MINIMUM", "PHASE LINEAR" },
        current,
        PrototypeSimpleMenuWindow::Style::phase,
        &phaseStripButton,
        phaseStripButton.getScreenBounds(),
        *this,
        lookAndFeel.getUiScale(),
        [safeThis] (int choice)
        {
            if (safeThis == nullptr || choice < 0 || choice > 1)
                return;
            if (auto* parameter = safeThis->ownerProcessor.parameters.getParameter (
                    ParamIDs::multibandPhase))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (
                    parameter->convertTo0to1 (static_cast<float> (choice)));
                parameter->endChangeGesture();
            }
        });
}

void DefaultDistortionAudioProcessorEditor::selectMode (int mode)
{
    const auto selected = juce::jlimit (
        0, DistortionEngine::modeCount - 1, mode);
    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    const auto contextBand = multiband.enabled && ! multiband.linked
        ? juce::jlimit (0, multiband.bandCount - 1,
                        ownerProcessor.getSelectedBand())
        : -1;
    const auto modeId = contextBand < 0
        ? juce::String (ParamIDs::mode)
        : ParamIDs::band (contextBand, "Mode");
    if (auto* parameter = ownerProcessor.parameters.getParameter (modeId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (
                static_cast<float> (selected)));
        parameter->endChangeGesture();
    }

    updateCharacterControl (selected);
    if (characterAttachment != nullptr)
        characterAttachment->setValueAsCompleteGesture (
            DistortionEngine::getDefaultCharacter (selected));
    const auto secondaryId = contextBand < 0
        ? juce::String (ParamIDs::secondary)
        : ParamIDs::band (contextBand, "Secondary");
    if (auto* parameter = ownerProcessor.parameters.getParameter (secondaryId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (
                DistortionEngine::getDefaultSecondary (selected)));
        parameter->endChangeGesture();
    }
}

void DefaultDistortionAudioProcessorEditor::stepMode (int delta)
{
    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    const auto contextBand = multiband.enabled && ! multiband.linked
        ? juce::jlimit (0, multiband.bandCount - 1,
                        ownerProcessor.getSelectedBand())
        : -1;
    const auto current = juce::jlimit (
        0, DistortionEngine::modeCount - 1,
        contextBand < 0
            ? ownerProcessor.getCurrentParameters().mode
            : multiband.bands[static_cast<size_t> (contextBand)].saturation.mode);
    const auto currentPosition =
        DistortionEngine::getDisplayPositionForMode (current);
    const auto nextPosition =
        (currentPosition + delta + DistortionEngine::modeCount)
        % DistortionEngine::modeCount;
    selectMode (
        DistortionEngine::getModeForDisplayPosition (nextPosition));
}

void DefaultDistortionAudioProcessorEditor::cycleAutoGain()
{
    if (autoGainAttachment == nullptr)
        return;
    autoGainAttachment->setValueAsCompleteGesture (
        static_cast<float> ((displayedAutoGainMode + 1) % 3));
}

void DefaultDistortionAudioProcessorEditor::cycleRoute()
{
    if (routeAttachment == nullptr)
        return;
    const auto current = routeButton.getButtonText().contains ("T/S")
        ? 1.0f : 0.0f;
    routeAttachment->setValueAsCompleteGesture (current < 0.5f ? 1.0f : 0.0f);
}

void DefaultDistortionAudioProcessorEditor::updateAutoGainButton (int mode)
{
    displayedAutoGainMode = juce::jlimit (0, 2, mode);
    static const std::array<juce::String, 3> labels {
        "OFF", "REGULAR", "SMART"
    };
    autoGainButton.setValueText (
        labels[static_cast<size_t> (displayedAutoGainMode)]);
    autoGainButton.setToggleState (false, juce::dontSendNotification);
}

void DefaultDistortionAudioProcessorEditor::updateCharacterControl (int mode)
{
    displayedMode = juce::jlimit (0, DistortionEngine::modeCount - 1, mode);
    drive.slider.textFromValueFunction = [this] (double value)
    {
        return DistortionEngine::formatDriveValue (
            displayedMode,
            static_cast<float> (value),
            ownerProcessor.getSampleRate() > 0.0
                ? ownerProcessor.getSampleRate()
                : 48000.0);
    };
    character.setTitle (
        DistortionEngine::getCharacterNames()[static_cast<size_t> (displayedMode)]);
    character.slider.setRange (
        DistortionEngine::isCharacterBipolar (displayedMode) ? -100.0 : 0.0,
        100.0,
        DistortionEngine::isCharacterStepped (displayedMode) ? 50.0 : 0.1);
    character.slider.textFromValueFunction = [this] (double value)
    {
        auto formatted = DistortionEngine::formatCharacterValue (
            displayedMode,
            static_cast<float> (value / 100.0),
            ownerProcessor.getSampleRate() > 0.0
                ? ownerProcessor.getSampleRate()
                : 48000.0);
        return formatted;
    };
    drive.slider.updateText();
    character.slider.updateText();
    const auto secondaryName = DistortionEngine::getSecondaryName (displayedMode);
    secondary.setTitle (secondaryName.isNotEmpty() ? secondaryName : "DETAIL");
    secondary.slider.setDoubleClickReturnValue (
        true, DistortionEngine::getDefaultSecondary (displayedMode));
    const auto hasSecondary = DistortionEngine::hasSecondaryControl (displayedMode);
    secondary.setVisible (true);
    secondary.setEnabled (hasSecondary);
    secondary.setAlpha (hasSecondary ? 1.0f : 0.34f);
    drive.repaint();
    character.repaint();
}

void DefaultDistortionAudioProcessorEditor::togglePalette()
{
    lookAndFeel.setInverted (! lookAndFeel.isInverted());
    saveLightTheme (lookAndFeel.isInverted());
    sendLookAndFeelChange();
    repaint();
}

void DefaultDistortionAudioProcessorEditor::timerCallback()
{
    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    if (multiband.enabled != multibandVisible)
        updateMultibandVisibility (multiband.enabled, true);
    rebindContextualControls();
    const auto contextBand = multiband.enabled && ! multiband.linked
        ? juce::jlimit (0, multiband.bandCount - 1,
                        ownerProcessor.getSelectedBand())
        : -1;
    const auto mode = juce::jlimit (
        0,
        DistortionEngine::modeCount - 1,
        contextBand < 0
            ? ownerProcessor.getCurrentParameters().mode
            : multiband.bands[static_cast<size_t> (contextBand)].saturation.mode);
    const auto contextualParameters = contextBand < 0
        ? ownerProcessor.getCurrentParameters()
        : multiband.bands[static_cast<size_t> (contextBand)].saturation;
    if (std::abs (contextualParameters.placementPercent) < 0.05f)
        placement.setTitle (contextualParameters.route == 0 ? "CENTER" : "SUM");
    else if (contextualParameters.route == 0)
        placement.setTitle (
            contextualParameters.placementPercent < 0.0f ? "MID" : "SIDE");
    else
        placement.setTitle (
            contextualParameters.placementPercent < 0.0f
                ? "TRNSNT" : "SUSTAIN");
    if (mode != displayedMode)
    {
        updateCharacterControl (mode);
        if (characterAttachment != nullptr)
            characterAttachment->sendInitialUpdate();
    }

    const auto displayPosition =
        DistortionEngine::getDisplayPositionForMode (mode);
    modeButton.setMode (
        displayPosition,
        prototypeModeNamesByDisplay[static_cast<size_t> (displayPosition)]);

    autoGainButton.setLoadingState (
        ownerProcessor.getSmartAutoGainProgress(),
        displayedAutoGainMode == 2
            && ! ownerProcessor.isSmartAutoGainLocked());
}

void DefaultDistortionAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    const auto designHeight = static_cast<float> (
        multibandVisible ? ui::expandedHeight : ui::compactHeight);
    const auto scale = juce::jmin (
        static_cast<float> (getWidth())
            / static_cast<float> (ui::designWidth),
        static_cast<float> (getHeight()) / designHeight);
    const auto offsetX =
        0.5f * (static_cast<float> (getWidth())
                - static_cast<float> (ui::designWidth) * scale);
    const auto offsetY =
        0.5f * (static_cast<float> (getHeight()) - designHeight * scale);
    auto rect = [scale, offsetX, offsetY] (
                    float x, float y, float width, float height)
    {
        return juce::Rectangle<float> (
            offsetX + x * scale,
            offsetY + y * scale,
            width * scale,
            height * scale);
    };

    const auto foreground = foregroundOf (*this);
    const auto background = backgroundOf (*this);
    graphics.fillAll (foreground);
    graphics.setColour (background);

    const auto drawBounds = [&] (ui::Bounds bounds)
    {
        graphics.fillRect (rect (
            static_cast<float> (bounds.x),
            static_cast<float> (bounds.y),
            static_cast<float> (bounds.width),
            static_cast<float> (bounds.height)));
    };
    drawBounds (ui::header);
    drawBounds (ui::main);
    drawBounds (ui::utility);
    if (multibandVisible)
        drawBounds (ui::rta);
}

void DefaultDistortionAudioProcessorEditor::paintOverChildren (
    juce::Graphics& graphics)
{
    const auto designHeight = static_cast<float> (
        multibandVisible ? ui::expandedHeight : ui::compactHeight);
    const auto scale = juce::jmin (
        static_cast<float> (getWidth()) / static_cast<float> (ui::designWidth),
        static_cast<float> (getHeight()) / designHeight);
    const auto offsetX = 0.5f * (
        static_cast<float> (getWidth()) - ui::designWidth * scale);
    const auto offsetY = 0.5f * (
        static_cast<float> (getHeight()) - designHeight * scale);
    const auto ink = foregroundOf (*this);
    const auto paper = backgroundOf (*this);
    const auto vertical = [&] (float x, float y, float height,
                               juce::Colour colour = juce::Colour {})
    {
        graphics.setColour (colour.isTransparent() ? ink : colour);
        graphics.fillRect (offsetX + x * scale,
                           offsetY + y * scale,
                           juce::jmax (1.0f, scale),
                           height * scale);
    };
    const auto horizontal = [&] (float x, float y, float width)
    {
        graphics.setColour (ink);
        graphics.fillRect (offsetX + x * scale,
                           offsetY + y * scale,
                           width * scale,
                           juce::jmax (1.0f, scale));
    };

    vertical (404.0f, 4.0f, 60.0f);
    vertical (463.0f, 4.0f, 60.0f);
    vertical (573.0f, 4.0f, 60.0f);
    vertical (204.0f, 13.0f, 42.0f);
    graphics.setColour (ink);
    graphics.fillRect (offsetX + 200.0f * scale,
                       offsetY + 4.0f * scale,
                       9.0f * scale,
                       9.0f * scale);
    graphics.fillRect (offsetX + 200.0f * scale,
                       offsetY + 55.0f * scale,
                       9.0f * scale,
                       9.0f * scale);

    for (const auto x : { 103.0f, 203.0f, 303.0f, 403.0f, 463.0f })
        vertical (x, 68.0f, 182.0f);
    horizontal (4.0f, 128.0f, 400.0f);
    horizontal (4.0f, 189.0f, 400.0f);

    const auto multiband = ownerProcessor.getCurrentMultibandParameters();
    vertical (203.0f, 254.0f, 28.0f,
              multiband.enabled && multiband.linked ? paper : ink);
    for (const auto x : { 303.0f, 403.0f, 463.0f })
        vertical (x, 254.0f, 28.0f);
}

void DefaultDistortionAudioProcessorEditor::resized()
{
    if (simpleMenu != nullptr)
        simpleMenu->close();
    if (modeMenu != nullptr)
        modeMenu->close();
    const auto designHeight = static_cast<float> (
        multibandVisible ? ui::expandedHeight : ui::compactHeight);
    const auto scale = juce::jmin (
        static_cast<float> (getWidth())
            / static_cast<float> (ui::designWidth),
        static_cast<float> (getHeight()) / designHeight);
    const auto offsetX = juce::roundToInt (
        0.5f * (static_cast<float> (getWidth())
                - static_cast<float> (ui::designWidth) * scale));
    const auto offsetY = juce::roundToInt (
        0.5f * (static_cast<float> (getHeight()) - designHeight * scale));
    lookAndFeel.setUiScale (scale);
    for (auto* control : {
             &drive, &character, &secondary, &asym, &tone,
             &stages, &placement, &dynamic, &speed,
             &inputHp, &outputLp, &mix, &output })
        control->setUiScale (scale);

    auto scaled = [scale, offsetX, offsetY] (
                      int x, int y, int width, int height)
    {
        return juce::Rectangle<int> (
            offsetX + juce::roundToInt (static_cast<float> (x) * scale),
            offsetY + juce::roundToInt (static_cast<float> (y) * scale),
            juce::roundToInt (static_cast<float> (width) * scale),
            juce::roundToInt (static_cast<float> (height) * scale));
    };
    const auto place = [&] (ui::Bounds bounds)
    {
        return scaled (bounds.x, bounds.y, bounds.width, bounds.height);
    };

    brandLabel.setBounds (place (ui::brand));
    previousModeButton.setBounds (place (ui::algorithmPrevious));
    modeButton.setBounds (place (ui::algorithm));
    nextModeButton.setBounds (place (ui::algorithmNext));
    qualityButton.setBounds (place (ui::oversampling));
    autoGainButton.setBounds (place (ui::autoGain));
    pluginPowerButton.setBounds (place (ui::power));

    drive.setBounds (place (ui::controls[0]));
    character.setBounds (place (ui::controls[1]));
    secondary.setBounds (place (ui::controls[2]));
    asym.setBounds (place (ui::controls[3]));
    asymStereoButton.setBounds (place (ui::stereoToggle));
    routeButton.setBounds (place (ui::controls[4]));
    placement.setBounds (place (ui::controls[5]));
    dynamic.setBounds (place (ui::controls[6]));
    speed.setBounds (place (ui::controls[7]));
    inputHp.setBounds (place (ui::controls[8]));
    tone.setBounds (place (ui::controls[9]));
    stages.setBounds (place (ui::controls[10]));
    outputLp.setBounds (place (ui::controls[11]));
    levelMeters.setBounds (place (ui::meters));
    responseDisplay.setBounds (place (ui::response));
    multibandButton.setBounds (place (ui::utilityCells[0]));
    linkStripButton.setBounds (place (ui::utilityCells[1]));
    phaseStripButton.setBounds (place (ui::utilityCells[2]));
    mix.setBounds (place (ui::utilityCells[3]));
    output.setBounds (place (ui::utilityCells[4]));
    multibandPanel.setBounds (place (ui::multibandPanel));
    sendLookAndFeelChange();
}
} // namespace dd
