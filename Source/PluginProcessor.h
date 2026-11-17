#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

#include "RiserEngine.h"

class BackswellProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
    BackswellProcessor();
    ~BackswellProcessor() override;

    // AudioProcessor ------------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Backswell"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Parameters ----------------------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

    // Clip capture / loading ----------------------------------------------------------------
    void startCapture();
    void stopCapture();                       // finishes the capture and builds the riser
    bool isCapturing() const noexcept { return capturing.load(); }
    bool consumeCaptureFinished();            // true once if the capture buffer filled up on its own
    bool loadClipFromFile (const juce::File& file);

    std::shared_ptr<const juce::AudioBuffer<float>> getRawClip() const;
    std::shared_ptr<const juce::AudioBuffer<float>> getRiser() const;
    int getVersion() const noexcept { return version.load(); }

    // Listen / loop -------------------------------------------------------------------------
    void setListening (bool shouldListen) noexcept { listening = shouldListen; }
    bool isListening() const noexcept { return listening.load(); }
    void setLooping (bool shouldLoop) noexcept { looping = shouldLoop; }
    bool isLooping() const noexcept { return looping.load(); }
    float getPlayFraction() const noexcept;

    // Export --------------------------------------------------------------------------------
    juce::File writeRiserToTempFile();
    bool writeRiserToFile (const juce::File& file) const;

    // Presets -------------------------------------------------------------------------------
    juce::File getUserPresetDir() const;
    juce::Array<juce::File> getUserPresets() const;
    bool saveUserPreset (const juce::File& file);
    bool loadUserPreset (const juce::File& file);

private:
    class RenderThread;

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void setParam (const juce::String& id, float plainValue);
    float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    void setClip (juce::AudioBuffer<float> clip);
    void triggerRender();
    void renderNow();

    // state
    juce::AudioFormatManager formatManager;
    double currentSampleRate = 44100.0;
    int currentProgram = 0;

    // capture
    juce::SpinLock captureLock;
    juce::AudioBuffer<float> captureBuffer;
    int capturePos = 0;
    std::atomic<bool> capturing { false };
    std::atomic<bool> captureFinished { false };

    // clip + riser
    mutable juce::SpinLock clipLock;
    std::shared_ptr<const juce::AudioBuffer<float>> rawClip;
    std::atomic<double> clipSampleRate { 44100.0 };

    mutable juce::SpinLock riserLock;
    std::shared_ptr<const juce::AudioBuffer<float>> riser;
    std::atomic<double> riserRate { 44100.0 };
    std::atomic<int> version { 0 };

    // playback
    std::atomic<bool> listening { false };
    std::atomic<bool> looping { true };
    int playPos = 0;
    std::atomic<int> uiPlayPos { 0 };
    std::atomic<int> uiPlayLen { 0 };

    // background rendering
    std::atomic<bool> renderDirty { false };
    std::unique_ptr<RenderThread> renderThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackswellProcessor)
};
