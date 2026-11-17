#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>
#include <vector>

#include "PluginProcessor.h"

namespace theme
{
    const juce::Colour bg        { 0xff0d1016 };
    const juce::Colour bgTop     { 0xff171c26 };
    const juce::Colour panel     { 0xff1a1f2a };
    const juce::Colour display   { 0xff090b10 };
    const juce::Colour accent    { 0xff3fe0c5 };
    const juce::Colour accent2   { 0xffffb347 };
    const juce::Colour record    { 0xffff5470 };
    const juce::Colour text      { 0xffe8edf5 };
    const juce::Colour muted     { 0xff8792a6 };
}

// -------------------------------------------------------------------------------------------------
class BackswellLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const float arcR = radius - 3.0f;
        const float thick = juce::jmax (3.0f, radius * 0.07f);
        const float body = radius * 0.76f;

        // knob body
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c3340), c.x, c.y - body,
                                                 juce::Colour (0xff11141a), c.x, c.y + body, false));
        g.fillEllipse (c.x - body, c.y - body, body * 2.0f, body * 2.0f);
        g.setColour (juce::Colour (0xff3a4252));
        g.drawEllipse (c.x - body, c.y - body, body * 2.0f, body * 2.0f, 1.2f);

        // track + value arc
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff262c38));
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setGradientFill (juce::ColourGradient (theme::accent2, c.x - arcR, c.y, theme::accent, c.x + arcR, c.y, false));
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // pointer
        juce::Path ptr;
        ptr.addRoundedRectangle (-1.8f, -body + 6.0f, 3.6f, body * 0.45f, 1.5f);
        g.setColour (theme::text);
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }
};

// -------------------------------------------------------------------------------------------------
class WaveformView : public juce::Component
{
public:
    std::function<void()> onDragOut;

    void setBuffer (std::shared_ptr<const juce::AudioBuffer<float>> b)
    {
        buffer = std::move (b);
        rebuildPeaks();
        repaint();
    }

    void setPlayhead (float fraction)
    {
        if (std::abs (fraction - playhead) > 0.0005f)
        {
            playhead = fraction;
            repaint();
        }
    }

    void setHint (const juce::String& h) { hint = h; repaint(); }
    void setDraggable (bool d) { draggable = d; }

    void resized() override { rebuildPeaks(); }
    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent&) override { dragStarted = false; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggable && ! dragStarted && e.getDistanceFromDragStart() > 6 && onDragOut != nullptr)
        {
            dragStarted = true;
            onDragOut();
        }
    }

private:
    void rebuildPeaks();

    std::shared_ptr<const juce::AudioBuffer<float>> buffer;
    std::vector<float> peaks;
    float playhead = -1.0f;
    bool draggable = false;
    bool dragStarted = false;
    juce::String hint { "Press Capture and play your audio, or drop an audio file here" };
};

// -------------------------------------------------------------------------------------------------
class BackswellEditor : public juce::AudioProcessorEditor,
                        public juce::DragAndDropContainer,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit BackswellEditor (BackswellProcessor&);
    ~BackswellEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    enum class Fmt { Percent, Seconds, Hertz };

    struct LabeledSlider
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct AttachedButton
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void timerCallback() override;

    void initSlider (LabeledSlider& s, const juce::String& paramId, const juce::String& text, bool rotary, Fmt fmt);
    void initButton (juce::TextButton& b, const juce::String& text, juce::Colour onColour);
    void initAttachedButton (AttachedButton& b, const juce::String& paramId, const juce::String& text, juce::Colour onColour);
    void layoutFx (juce::Rectangle<int> column, juce::TextButton& toggle, std::initializer_list<LabeledSlider*> sliders);

    void refreshWaveform();
    void refreshPresetList (int selectId);
    void savePresetDialog();
    void exportDialog();

    BackswellProcessor& proc;
    BackswellLookAndFeel laf;

    WaveformView waveform;

    juce::TextButton captureButton, listenButton, loopButton, viewRawButton, exportButton, saveButton, resetButton;
    juce::ComboBox presetBox;
    juce::Array<juce::File> userPresetFiles;

    LabeledSlider riseS, dampS, sibilS;
    LabeledSlider phoneMixS, choDepthS, choMixS, driveS, drvMixS, tremRateS, tremDepthS;
    AttachedButton instB, dryB, phoneB, chorusB, driveB, tremoloB;

    juce::Rectangle<int> titleBounds;
    juce::Rectangle<int> fxBounds[4];

    std::unique_ptr<juce::FileChooser> chooser;
    int lastVersion = -1;
    bool lastViewRaw = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackswellEditor)
};
