#include "PluginEditor.h"

#include <cmath>

// =================================================================================================
// WaveformView
// =================================================================================================
void WaveformView::rebuildPeaks()
{
    peaks.clear();
    if (buffer == nullptr || buffer->getNumSamples() == 0 || getWidth() < 60)
        return;

    const int bars = juce::jmax (10, (getWidth() - 28) / 5);
    const int n = buffer->getNumSamples();
    peaks.assign ((size_t) bars, 0.0f);

    float maxPeak = 1.0e-6f;
    for (int i = 0; i < bars; ++i)
    {
        const int s0 = (int) ((juce::int64) i * n / bars);
        const int s1 = juce::jmin (n, juce::jmax (s0 + 1, (int) ((juce::int64) (i + 1) * n / bars)));

        float m = 0.0f;
        for (int ch = 0; ch < buffer->getNumChannels(); ++ch)
        {
            const float* d = buffer->getReadPointer (ch);
            for (int s = s0; s < s1; ++s)
                m = juce::jmax (m, std::abs (d[s]));
        }

        peaks[(size_t) i] = m;
        maxPeak = juce::jmax (maxPeak, m);
    }

    for (auto& p : peaks)
        p = std::pow (p / maxPeak, 0.7f);
}

void WaveformView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (theme::display);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (juce::Colour (0xff222836));
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    if (peaks.empty())
    {
        g.setColour (theme::muted);
        g.setFont (14.0f);
        g.drawText (hint, getLocalBounds().reduced (16), juce::Justification::centred, true);
        return;
    }

    auto area = b.reduced (14.0f, 18.0f);
    const int n = (int) peaks.size();
    const float step = area.getWidth() / (float) n;

    g.setGradientFill (juce::ColourGradient (theme::accent2, area.getX(), 0.0f,
                                             theme::accent, area.getRight(), 0.0f, false));
    for (int i = 0; i < n; ++i)
    {
        const float h = juce::jmax (2.0f, peaks[(size_t) i] * area.getHeight());
        g.fillRoundedRectangle (area.getX() + (float) i * step, area.getCentreY() - h * 0.5f,
                                juce::jmax (1.0f, step - 1.5f), h, 1.0f);
    }

    if (playhead >= 0.0f)
    {
        const float px = area.getX() + playhead * area.getWidth();
        g.setColour (theme::text);
        g.fillRect (px - 1.0f, b.getY() + 8.0f, 2.0f, b.getHeight() - 16.0f);
    }

    if (draggable)
    {
        auto lb = getLocalBounds();
        g.setColour (theme::muted);
        g.setFont (11.0f);
        g.drawText ("drag this into your DAW", lb.removeFromBottom (20).reduced (14, 0),
                    juce::Justification::centredRight, true);
    }
}

// =================================================================================================
// Editor
// =================================================================================================
BackswellEditor::BackswellEditor (BackswellProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    setSize (600, 700);

    // waveform
    addAndMakeVisible (waveform);
    waveform.onDragOut = [this]
    {
        auto file = proc.writeRiserToTempFile();
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles (juce::StringArray (file.getFullPathName()),
                                                                        false, &waveform);
    };

    // transport row
    initButton (captureButton, juce::String::fromUTF8 ("\xe2\x97\x8f  CAPTURE"), theme::record);
    initButton (listenButton,  juce::String::fromUTF8 ("\xe2\x96\xb6  LISTEN"), theme::accent);
    initButton (loopButton,    "LOOP", theme::accent);
    initButton (viewRawButton, "VIEW RAW", theme::accent2);
    initButton (exportButton,  "EXPORT", theme::accent);
    initButton (saveButton,    "SAVE", theme::accent);
    initButton (resetButton,   "RESET", theme::accent);
    exportButton.setClickingTogglesState (false);
    saveButton.setClickingTogglesState (false);
    resetButton.setClickingTogglesState (false);
    loopButton.setToggleState (proc.isLooping(), juce::dontSendNotification);

    captureButton.onClick = [this]
    {
        if (captureButton.getToggleState())
            proc.startCapture();
        else
            proc.stopCapture();
    };
    listenButton.onClick = [this] { proc.setListening (listenButton.getToggleState()); };
    loopButton.onClick   = [this] { proc.setLooping (loopButton.getToggleState()); };
    viewRawButton.onClick = [this] { refreshWaveform(); };
    exportButton.onClick = [this] { exportDialog(); };
    saveButton.onClick   = [this] { savePresetDialog(); };
    resetButton.onClick  = [this]
    {
        proc.setCurrentProgram (0);
        presetBox.setSelectedId (1, juce::dontSendNotification);
    };

    // presets
    addAndMakeVisible (presetBox);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 100)
        {
            const int idx = id - 100;
            if (idx < userPresetFiles.size())
                proc.loadUserPreset (userPresetFiles[idx]);
        }
        else if (id >= 1)
        {
            proc.setCurrentProgram (id - 1);
        }
    };
    refreshPresetList (1);

    // main controls
    initSlider (riseS,  "rise",  "RISE", true, Fmt::Seconds);
    initSlider (dampS,  "damp",  "DAMPING", true, Fmt::Percent);
    initSlider (sibilS, "sibil", "DE-ESS", true, Fmt::Percent);
    riseS.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    dampS.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    sibilS.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);

    initAttachedButton (instB, "inst", "INSTRUMENT MODE", theme::accent2);
    initAttachedButton (dryB,  "dry",  "APPEND DRY CLIP", theme::accent2);

    // effects
    initAttachedButton (phoneB,   "phoneOn", "PHONE",   theme::accent2);
    initAttachedButton (chorusB,  "choOn",   "CHORUS",  theme::accent2);
    initAttachedButton (driveB,   "drvOn",   "DRIVE",   theme::accent2);
    initAttachedButton (tremoloB, "tremOn",  "TREMOLO", theme::accent2);

    initSlider (phoneMixS,  "phoneMix", "MIX", false, Fmt::Percent);
    initSlider (choDepthS,  "choDepth", "DEPTH", false, Fmt::Percent);
    initSlider (choMixS,    "choMix",   "MIX", false, Fmt::Percent);
    initSlider (driveS,     "drive",    "DRIVE", false, Fmt::Percent);
    initSlider (drvMixS,    "drvMix",   "MIX", false, Fmt::Percent);
    initSlider (tremRateS,  "tremRate", "RATE", false, Fmt::Hertz);
    initSlider (tremDepthS, "tremDepth","DEPTH", false, Fmt::Percent);

    refreshWaveform();
    startTimerHz (30);
}

BackswellEditor::~BackswellEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// -------------------------------------------------------------------------------------------------
void BackswellEditor::initButton (juce::TextButton& b, const juce::String& text, juce::Colour onColour)
{
    b.setButtonText (text);
    b.setClickingTogglesState (true);
    b.setColour (juce::TextButton::buttonColourId, theme::panel);
    b.setColour (juce::TextButton::buttonOnColourId, onColour);
    b.setColour (juce::TextButton::textColourOffId, theme::text);
    b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (b);
}

void BackswellEditor::initAttachedButton (AttachedButton& b, const juce::String& paramId,
                                          const juce::String& text, juce::Colour onColour)
{
    initButton (b.button, text, onColour);
    b.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, paramId, b.button);
}

void BackswellEditor::initSlider (LabeledSlider& s, const juce::String& paramId, const juce::String& text,
                                  bool rotary, Fmt fmt)
{
    s.slider.setSliderStyle (rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    s.slider.setTextBoxStyle (rotary ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight, false,
                              rotary ? 80 : 46, 18);
    s.slider.setColour (juce::Slider::trackColourId, theme::accent);
    s.slider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff262c38));
    s.slider.setColour (juce::Slider::thumbColourId, theme::text);
    s.slider.setColour (juce::Slider::textBoxTextColourId, theme::text);
    s.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    s.label.setText (text, juce::dontSendNotification);
    s.label.setJustificationType (juce::Justification::centred);
    s.label.setColour (juce::Label::textColourId, theme::muted);
    s.label.setFont (juce::FontOptions (11.0f));

    addAndMakeVisible (s.slider);
    addAndMakeVisible (s.label);

    s.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, s.slider);

    // The attachment installs its own text converters, so ours are set afterwards
    switch (fmt)
    {
        case Fmt::Percent:
            s.slider.textFromValueFunction = [] (double v) { return juce::String ((int) std::lround (v * 100.0)) + "%"; };
            s.slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };
            break;
        case Fmt::Seconds:
            s.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " s"; };
            s.slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
            break;
        case Fmt::Hertz:
            s.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " Hz"; };
            s.slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
            break;
    }
    s.slider.updateText();
}

// -------------------------------------------------------------------------------------------------
void BackswellEditor::refreshPresetList (int selectId)
{
    presetBox.clear (juce::dontSendNotification);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);

    userPresetFiles = proc.getUserPresets();
    if (userPresetFiles.size() > 0)
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("User presets");
        for (int i = 0; i < userPresetFiles.size(); ++i)
            presetBox.addItem (userPresetFiles[i].getFileNameWithoutExtension(), 100 + i);
    }

    presetBox.setSelectedId (selectId, juce::dontSendNotification);
}

void BackswellEditor::savePresetDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Save preset", proc.getUserPresetDir().getChildFile ("My Preset.bsw"), "*.bsw");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto result = fc.getResult();
                              if (result == juce::File())
                                  return;

                              auto target = proc.getUserPresetDir()
                                                .getChildFile (result.getFileNameWithoutExtension())
                                                .withFileExtension ("bsw");
                              if (proc.saveUserPreset (target))
                              {
                                  refreshPresetList (1);
                                  for (int i = 0; i < userPresetFiles.size(); ++i)
                                      if (userPresetFiles[i] == target)
                                          presetBox.setSelectedId (100 + i, juce::dontSendNotification);
                              }
                          });
}

void BackswellEditor::exportDialog()
{
    if (proc.getRiser() == nullptr)
        return;

    chooser = std::make_unique<juce::FileChooser> ("Export riser",
                                                   juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                       .getChildFile ("Backswell Riser.wav"),
                                                   "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto result = fc.getResult();
                              if (result != juce::File())
                                  proc.writeRiserToFile (result.withFileExtension ("wav"));
                          });
}

// -------------------------------------------------------------------------------------------------
void BackswellEditor::refreshWaveform()
{
    const bool raw = viewRawButton.getToggleState();
    lastViewRaw = raw;
    lastVersion = proc.getVersion();

    auto buf = raw ? proc.getRawClip() : proc.getRiser();
    waveform.setBuffer (buf);
    waveform.setDraggable (! raw && buf != nullptr);
}

void BackswellEditor::timerCallback()
{
    if (proc.consumeCaptureFinished())
        captureButton.setToggleState (false, juce::dontSendNotification);

    if (proc.getVersion() != lastVersion || viewRawButton.getToggleState() != lastViewRaw)
        refreshWaveform();

    listenButton.setToggleState (proc.isListening(), juce::dontSendNotification);

    if (proc.isListening() && ! viewRawButton.getToggleState())
        waveform.setPlayhead (proc.getPlayFraction());
    else
        waveform.setPlayhead (-1.0f);
}

// -------------------------------------------------------------------------------------------------
bool BackswellEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3"
            || ext == ".ogg" || ext == ".m4a" || ext == ".caf")
            return true;
    }
    return false;
}

void BackswellEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty())
        return;

    if (proc.loadClipFromFile (juce::File (files[0])))
        viewRawButton.setToggleState (false, juce::dontSendNotification);
}

// -------------------------------------------------------------------------------------------------
void BackswellEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (theme::bgTop, 0.0f, 0.0f, theme::bg, 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (theme::text);
    g.setFont (g.getCurrentFont().withHeight (22.0f).boldened());
    g.drawText ("BACKSWELL", titleBounds.removeFromTop (24), juce::Justification::centredLeft, false);

    g.setColour (theme::muted);
    g.setFont (11.0f);
    g.drawText ("reverse reverb riser", titleBounds, juce::Justification::centredLeft, false);

    g.setColour (theme::panel);
    for (auto& fb : fxBounds)
        g.fillRoundedRectangle (fb.toFloat(), 10.0f);
}

void BackswellEditor::layoutFx (juce::Rectangle<int> column, juce::TextButton& toggle,
                                std::initializer_list<LabeledSlider*> sliders)
{
    auto c = column.reduced (10, 10);
    toggle.setBounds (c.removeFromTop (26));
    c.removeFromTop (8);
    for (auto* s : sliders)
    {
        s->label.setBounds (c.removeFromTop (16));
        s->slider.setBounds (c.removeFromTop (28));
        c.removeFromTop (6);
    }
}

void BackswellEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    // header
    auto header = r.removeFromTop (38);
    titleBounds = header.removeFromLeft (170);
    resetButton.setBounds (header.removeFromRight (64).reduced (0, 4));
    header.removeFromRight (6);
    saveButton.setBounds (header.removeFromRight (64).reduced (0, 4));
    header.removeFromRight (6);
    presetBox.setBounds (header.reduced (0, 4));
    r.removeFromTop (8);

    // waveform
    waveform.setBounds (r.removeFromTop (170));
    r.removeFromTop (8);

    // transport row
    auto tr = r.removeFromTop (30);
    captureButton.setBounds (tr.removeFromLeft (104));
    tr.removeFromLeft (6);
    listenButton.setBounds (tr.removeFromLeft (92));
    tr.removeFromLeft (6);
    loopButton.setBounds (tr.removeFromLeft (64));
    exportButton.setBounds (tr.removeFromRight (80));
    tr.removeFromRight (6);
    viewRawButton.setBounds (tr.removeFromRight (90));
    r.removeFromTop (8);

    // effects block at the bottom
    auto fx = r.removeFromBottom (150);
    r.removeFromBottom (8);
    const int colW = fx.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        fxBounds[i] = fx.removeFromLeft (colW).reduced (4, 0);

    layoutFx (fxBounds[0], phoneB.button,   { &phoneMixS });
    layoutFx (fxBounds[1], chorusB.button,  { &choDepthS, &choMixS });
    layoutFx (fxBounds[2], driveB.button,   { &driveS, &drvMixS });
    layoutFx (fxBounds[3], tremoloB.button, { &tremRateS, &tremDepthS });

    // mode toggles
    auto tg = r.removeFromBottom (30);
    instB.button.setBounds (tg.removeFromLeft (tg.getWidth() / 2).reduced (30, 0));
    dryB.button.setBounds (tg.reduced (30, 0));
    r.removeFromBottom (6);

    // knobs
    auto left = r.removeFromLeft (130);
    auto right = r.removeFromRight (130);

    auto lc = left.withSizeKeepingCentre (110, 130);
    dampS.label.setBounds (lc.removeFromTop (16));
    dampS.slider.setBounds (lc);

    auto rc = right.withSizeKeepingCentre (110, 130);
    sibilS.label.setBounds (rc.removeFromTop (16));
    sibilS.slider.setBounds (rc);

    riseS.label.setBounds (r.removeFromTop (16));
    riseS.slider.setBounds (r);
}
