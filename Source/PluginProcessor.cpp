#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <iterator>

namespace
{
    struct Preset
    {
        const char* name;
        float rise, damp;
        bool inst;
        float sibil;
        bool dry;
        bool phone;  float phoneMix;
        bool cho;    float choDepth, choMix;
        bool drv;    float drive, drvMix;
        bool trem;   float tremRate, tremDepth;
    };

    const Preset kPresets[] =
    {
        { "Default",          3.0f, 0.50f, false, 0.70f, false, false, 1.0f, false, 0.5f, 0.50f, false, 0.47f, 0.5f, false, 5.0f, 0.5f },
        { "Classic Swell",    2.5f, 0.45f, false, 0.80f, false, false, 1.0f, false, 0.5f, 0.50f, false, 0.47f, 0.5f, false, 5.0f, 0.5f },
        { "Long Vocal Build", 6.0f, 0.60f, false, 0.85f, false, false, 1.0f, true,  0.4f, 0.35f, false, 0.47f, 0.5f, false, 5.0f, 0.5f },
        { "Telephone Rise",   3.0f, 0.50f, false, 0.70f, false, true,  1.0f, false, 0.5f, 0.50f, false, 0.47f, 0.5f, false, 5.0f, 0.5f },
        { "Wide Chorus Wash", 4.0f, 0.40f, false, 0.70f, false, false, 1.0f, true,  0.7f, 0.60f, false, 0.47f, 0.5f, false, 5.0f, 0.5f },
        { "Gritty Rise",      2.0f, 0.50f, false, 0.60f, false, false, 1.0f, false, 0.5f, 0.50f, true,  0.65f, 0.6f, false, 5.0f, 0.5f },
        { "Pulse Gate",       4.0f, 0.50f, false, 0.70f, false, false, 1.0f, false, 0.5f, 0.50f, false, 0.47f, 0.5f, true,  8.0f, 0.7f },
        { "Instrument Bloom", 5.0f, 0.30f, true,  0.00f, false, false, 1.0f, true,  0.6f, 0.50f, false, 0.47f, 0.5f, false, 5.0f, 0.5f },
    };

    const char* const kParamIds[] =
    {
        "rise", "damp", "inst", "sibil", "dry",
        "phoneOn", "phoneMix",
        "choOn", "choDepth", "choMix",
        "drvOn", "drive", "drvMix",
        "tremOn", "tremRate", "tremDepth"
    };
}

// -------------------------------------------------------------------------------------------------
class BackswellProcessor::RenderThread : public juce::Thread
{
public:
    explicit RenderThread (BackswellProcessor& o) : juce::Thread ("Backswell render"), owner (o) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (-1);
            if (threadShouldExit())
                break;

            sleep (30); // coalesce bursts of parameter changes

            if (owner.renderDirty.exchange (false))
                owner.renderNow();
        }
    }

private:
    BackswellProcessor& owner;
};

// -------------------------------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout BackswellProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using NR = juce::NormalisableRange<float>;

    auto id = [] (const char* s) { return juce::ParameterID { s, 1 }; };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<F> (id ("rise"),  "Rise",      NR (0.5f, 10.0f, 0.01f, 0.5f), 3.0f));
    layout.add (std::make_unique<F> (id ("damp"),  "Damping",   NR (0.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<B> (id ("inst"),  "Instrument Mode", false));
    layout.add (std::make_unique<F> (id ("sibil"), "Sibilance Control", NR (0.0f, 1.0f, 0.001f), 0.7f));
    layout.add (std::make_unique<B> (id ("dry"),   "Append Dry", false));

    layout.add (std::make_unique<B> (id ("phoneOn"),  "Phone On", false));
    layout.add (std::make_unique<F> (id ("phoneMix"), "Phone Mix", NR (0.0f, 1.0f, 0.001f), 1.0f));

    layout.add (std::make_unique<B> (id ("choOn"),    "Chorus On", false));
    layout.add (std::make_unique<F> (id ("choDepth"), "Chorus Depth", NR (0.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<F> (id ("choMix"),   "Chorus Mix", NR (0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<B> (id ("drvOn"),  "Drive On", false));
    layout.add (std::make_unique<F> (id ("drive"),  "Drive", NR (0.0f, 1.0f, 0.001f), 0.47f));
    layout.add (std::make_unique<F> (id ("drvMix"), "Drive Mix", NR (0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<B> (id ("tremOn"),    "Tremolo On", false));
    layout.add (std::make_unique<F> (id ("tremRate"),  "Tremolo Rate", NR (0.5f, 16.0f, 0.01f, 0.6f), 5.0f));
    layout.add (std::make_unique<F> (id ("tremDepth"), "Tremolo Depth", NR (0.0f, 1.0f, 0.001f), 0.5f));

    return layout;
}

BackswellProcessor::BackswellProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    formatManager.registerBasicFormats();

    for (auto* paramId : kParamIds)
        apvts.addParameterListener (paramId, this);

    renderThread = std::make_unique<RenderThread> (*this);
    renderThread->startThread();
}

BackswellProcessor::~BackswellProcessor()
{
    for (auto* paramId : kParamIds)
        apvts.removeParameterListener (paramId, this);

    renderThread->stopThread (3000);
}

// -------------------------------------------------------------------------------------------------
void BackswellProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    juce::SpinLock::ScopedLockType sl (captureLock);
    captureBuffer.setSize (2, (int) (sampleRate * 30.0), false, true, false);
    capturePos = 0;
}

bool BackswellProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return out == layouts.getMainInputChannelSet();
}

void BackswellProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();

    // Record the incoming audio while capture is armed
    if (capturing.load())
    {
        juce::SpinLock::ScopedTryLockType tl (captureLock);
        if (tl.isLocked() && capturing.load() && captureBuffer.getNumSamples() > 0)
        {
            const int room = captureBuffer.getNumSamples() - capturePos;
            const int num = juce::jmin (room, n);
            if (num > 0)
            {
                for (int ch = 0; ch < 2; ++ch)
                    captureBuffer.copyFrom (ch, capturePos, buffer, juce::jmin (ch, nch - 1), 0, num);
                capturePos += num;
            }

            if (capturePos >= captureBuffer.getNumSamples())
            {
                capturing = false;
                captureFinished = true;
            }
        }
    }

    // Audition the riser: replaces the input while "Listen" is on
    if (listening.load())
    {
        juce::SpinLock::ScopedTryLockType tl (riserLock);
        buffer.clear();

        if (tl.isLocked() && riser != nullptr)
        {
            const int len = riser->getNumSamples();
            int written = 0;

            while (len > 0 && written < n)
            {
                if (playPos >= len)
                {
                    if (looping.load())
                        playPos = 0;
                    else
                    {
                        listening = false;
                        break;
                    }
                }

                const int chunk = juce::jmin (n - written, len - playPos);
                for (int ch = 0; ch < nch; ++ch)
                    buffer.copyFrom (ch, written, *riser, juce::jmin (ch, riser->getNumChannels() - 1), playPos, chunk);

                playPos += chunk;
                written += chunk;
            }

            uiPlayPos = playPos;
            uiPlayLen = len;
        }
    }
    else
    {
        playPos = 0;
    }
}

float BackswellProcessor::getPlayFraction() const noexcept
{
    const int len = uiPlayLen.load();
    return len > 0 ? (float) uiPlayPos.load() / (float) len : 0.0f;
}

// -------------------------------------------------------------------------------------------------
void BackswellProcessor::startCapture()
{
    {
        juce::SpinLock::ScopedLockType sl (captureLock);
        capturePos = 0;
    }
    captureFinished = false;
    capturing = true;
}

void BackswellProcessor::stopCapture()
{
    capturing = false;

    juce::AudioBuffer<float> clip;
    {
        juce::SpinLock::ScopedLockType sl (captureLock);
        if (capturePos > 0)
        {
            clip.setSize (2, capturePos);
            for (int ch = 0; ch < 2; ++ch)
                clip.copyFrom (ch, 0, captureBuffer, ch, 0, capturePos);
        }
        capturePos = 0;
    }

    if (clip.getNumSamples() > 0)
        setClip (std::move (clip));
}

bool BackswellProcessor::consumeCaptureFinished()
{
    if (captureFinished.exchange (false))
    {
        stopCapture();
        return true;
    }
    return false;
}

bool BackswellProcessor::loadClipFromFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const juce::int64 maxSamples = (juce::int64) (reader->sampleRate * 30.0);
    const int numSamples = (int) juce::jmin (reader->lengthInSamples, maxSamples);
    if (numSamples < 16)
        return false;

    const int channels = (int) juce::jmin (2u, reader->numChannels);
    juce::AudioBuffer<float> buf (juce::jmax (1, channels), numSamples);
    buf.clear();
    reader->read (&buf, 0, numSamples, 0, true, true);

    buf = backswell::resampleBuffer (buf, reader->sampleRate, currentSampleRate);
    setClip (std::move (buf));
    return true;
}

void BackswellProcessor::setClip (juce::AudioBuffer<float> clip)
{
    auto trimmed = backswell::trimSilence (clip);
    if (trimmed.getNumSamples() < 16)
        return;

    clipSampleRate = currentSampleRate;
    {
        juce::SpinLock::ScopedLockType sl (clipLock);
        rawClip = std::make_shared<juce::AudioBuffer<float>> (std::move (trimmed));
    }

    ++version;
    triggerRender();
}

std::shared_ptr<const juce::AudioBuffer<float>> BackswellProcessor::getRawClip() const
{
    juce::SpinLock::ScopedLockType sl (clipLock);
    return rawClip;
}

std::shared_ptr<const juce::AudioBuffer<float>> BackswellProcessor::getRiser() const
{
    juce::SpinLock::ScopedLockType sl (riserLock);
    return riser;
}

// -------------------------------------------------------------------------------------------------
void BackswellProcessor::parameterChanged (const juce::String&, float)
{
    triggerRender();
}

void BackswellProcessor::triggerRender()
{
    renderDirty = true;
    if (renderThread != nullptr)
        renderThread->notify();
}

void BackswellProcessor::renderNow()
{
    std::shared_ptr<const juce::AudioBuffer<float>> clip = getRawClip();
    if (clip == nullptr)
        return;

    backswell::RenderSettings s;
    s.sampleRate  = clipSampleRate.load();
    s.riseSeconds = pv ("rise");
    s.damping     = pv ("damp");
    s.voxMode     = pv ("inst") < 0.5f;
    s.sibilance   = pv ("sibil");
    s.includeDry  = pv ("dry") > 0.5f;

    s.phoneOn     = pv ("phoneOn") > 0.5f;   s.phoneMix = pv ("phoneMix");
    s.chorusOn    = pv ("choOn") > 0.5f;     s.chorusDepth = pv ("choDepth");  s.chorusMix = pv ("choMix");
    s.driveOn     = pv ("drvOn") > 0.5f;     s.drive = pv ("drive");           s.driveMix = pv ("drvMix");
    s.tremoloOn   = pv ("tremOn") > 0.5f;    s.tremRate = pv ("tremRate");     s.tremDepth = pv ("tremDepth");

    auto result = std::make_shared<juce::AudioBuffer<float>> (backswell::renderRiser (*clip, s));
    if (result->getNumSamples() == 0)
        return;

    riserRate = s.sampleRate;

    std::shared_ptr<const juce::AudioBuffer<float>> incoming = result;
    {
        juce::SpinLock::ScopedLockType sl (riserLock);
        riser.swap (incoming);
    }
    // 'incoming' now holds the previous riser and is released here, on the render thread

    ++version;
}

// -------------------------------------------------------------------------------------------------
bool BackswellProcessor::writeRiserToFile (const juce::File& file) const
{
    auto r = getRiser();
    if (r == nullptr)
        return false;

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::FileOutputStream> out (file.createOutputStream());
    if (out == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.get(), riserRate.load(), (unsigned int) r->getNumChannels(), 24, {}, 0));
    if (writer == nullptr)
        return false;

    out.release(); // the writer owns the stream now
    return writer->writeFromAudioSampleBuffer (*r, 0, r->getNumSamples());
}

juce::File BackswellProcessor::writeRiserToTempFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Backswell");
    auto name = "Backswell Riser " + juce::String (pv ("rise"), 1) + "s " + juce::String (version.load()) + ".wav";
    auto file = dir.getChildFile (name);

    if (! file.existsAsFile() && ! writeRiserToFile (file))
        return {};

    return file;
}

// -------------------------------------------------------------------------------------------------
int BackswellProcessor::getNumPrograms()
{
    return (int) std::size (kPresets);
}

const juce::String BackswellProcessor::getProgramName (int index)
{
    if (index >= 0 && index < (int) std::size (kPresets))
        return kPresets[index].name;
    return {};
}

void BackswellProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

void BackswellProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= (int) std::size (kPresets))
        return;

    currentProgram = index;
    const auto& p = kPresets[index];

    setParam ("rise", p.rise);
    setParam ("damp", p.damp);
    setParam ("inst", p.inst ? 1.0f : 0.0f);
    setParam ("sibil", p.sibil);
    setParam ("dry", p.dry ? 1.0f : 0.0f);

    setParam ("phoneOn", p.phone ? 1.0f : 0.0f);
    setParam ("phoneMix", p.phoneMix);

    setParam ("choOn", p.cho ? 1.0f : 0.0f);
    setParam ("choDepth", p.choDepth);
    setParam ("choMix", p.choMix);

    setParam ("drvOn", p.drv ? 1.0f : 0.0f);
    setParam ("drive", p.drive);
    setParam ("drvMix", p.drvMix);

    setParam ("tremOn", p.trem ? 1.0f : 0.0f);
    setParam ("tremRate", p.tremRate);
    setParam ("tremDepth", p.tremDepth);
}

juce::File BackswellProcessor::getUserPresetDir() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Audio").getChildFile ("Presets").getChildFile ("Backswell");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> BackswellProcessor::getUserPresets() const
{
    return getUserPresetDir().findChildFiles (juce::File::findFiles, false, "*.bsw");
}

bool BackswellProcessor::saveUserPreset (const juce::File& file)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        return xml->writeTo (file);
    return false;
}

bool BackswellProcessor::loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            return true;
        }
    return false;
}

void BackswellProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BackswellProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BackswellProcessor::createEditor()
{
    return new BackswellEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BackswellProcessor();
}
