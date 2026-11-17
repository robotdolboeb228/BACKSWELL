#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace backswell
{
    struct RenderSettings
    {
        double sampleRate = 44100.0;

        float riseSeconds = 3.0f;   // length of the swell, 0.5 - 10 s
        float damping     = 0.5f;   // reverb high-frequency damping, 0 - 1
        bool  voxMode     = true;   // true: vocal mode (sibilance control), false: instrument mode
        float sibilance   = 0.7f;   // amount of sibilance reduction in the reverb send, 0 - 1
        bool  includeDry  = false;  // append the original clip after the riser

        bool  phoneOn   = false;  float phoneMix   = 1.0f;
        bool  chorusOn  = false;  float chorusDepth = 0.5f;  float chorusMix = 0.5f;
        bool  driveOn   = false;  float drive = 0.47f;       float driveMix  = 0.5f;
        bool  tremoloOn = false;  float tremRate = 5.0f;     float tremDepth = 0.5f;
    };

    /** Removes leading and trailing silence. Returns an empty buffer if everything is silent. */
    juce::AudioBuffer<float> trimSilence (const juce::AudioBuffer<float>& in, float threshold = 0.001f);

    /** Linear-interpolating (Lagrange) sample rate conversion. */
    juce::AudioBuffer<float> resampleBuffer (const juce::AudioBuffer<float>& in, double srcRate, double dstRate);

    /** Builds the reverse-reverb riser from a clip. Always returns a stereo buffer (or an empty one on failure). */
    juce::AudioBuffer<float> renderRiser (const juce::AudioBuffer<float>& clip, const RenderSettings& settings);
}
