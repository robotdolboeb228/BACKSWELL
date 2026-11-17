#include "RiserEngine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace backswell
{
namespace
{
    constexpr double kPi = juce::MathConstants<double>::pi;

    // Small RBJ biquad, no dependency on juce_dsp
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void setHighPass (double sr, double freq, double q = 0.7071)
        {
            const double w = 2.0 * kPi * freq / sr;
            const double c = std::cos (w), s = std::sin (w);
            const double al = s / (2.0 * q);
            const double a0 = 1.0 + al;
            b0 = ((1.0 + c) * 0.5) / a0;
            b1 = (-(1.0 + c)) / a0;
            b2 = ((1.0 + c) * 0.5) / a0;
            a1 = (-2.0 * c) / a0;
            a2 = (1.0 - al) / a0;
        }

        void setLowPass (double sr, double freq, double q = 0.7071)
        {
            const double w = 2.0 * kPi * freq / sr;
            const double c = std::cos (w), s = std::sin (w);
            const double al = s / (2.0 * q);
            const double a0 = 1.0 + al;
            b0 = ((1.0 - c) * 0.5) / a0;
            b1 = (1.0 - c) / a0;
            b2 = ((1.0 - c) * 0.5) / a0;
            a1 = (-2.0 * c) / a0;
            a2 = (1.0 - al) / a0;
        }

        float process (float x)
        {
            const double y = b0 * (double) x + z1;
            z1 = b1 * (double) x - a1 * y + z2;
            z2 = b2 * (double) x - a2 * y;
            return (float) y;
        }
    };

    float peakOf (const juce::AudioBuffer<float>& b)
    {
        return b.getNumSamples() > 0 ? b.getMagnitude (0, b.getNumSamples()) : 0.0f;
    }

    // ------------------------------------------------------------------------------------------
    // Sibilance control: measures how much of the signal energy sits above ~5.5 kHz and pulls the
    // high band down while that ratio is high, so harsh "S" sounds don't smear backwards.
    void applyDeEss (juce::AudioBuffer<float>& buf, double sr, float amount)
    {
        const int n = buf.getNumSamples();
        const int nch = buf.getNumChannels();
        if (n == 0 || nch == 0 || amount <= 0.0f)
            return;

        std::vector<float> mono ((size_t) n, 0.0f);
        for (int ch = 0; ch < nch; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] += d[i] / (float) nch;
        }

        Biquad h1, h2;
        h1.setHighPass (sr, 5500.0);
        h2.setHighPass (sr, 5500.0);

        const double envAtk = std::exp (-1.0 / (0.001 * sr));
        const double envRel = std::exp (-1.0 / (0.030 * sr));
        const double gAtk   = std::exp (-1.0 / (0.002 * sr));
        const double gRel   = std::exp (-1.0 / (0.020 * sr));

        std::vector<float> gain ((size_t) n, 1.0f);
        double eHp = 0.0, eAll = 0.0, g = 1.0;

        for (int i = 0; i < n; ++i)
        {
            const float x = mono[(size_t) i];
            const float hp = h2.process (h1.process (x));
            const double ah = std::abs ((double) hp);
            const double aa = std::abs ((double) x);

            eHp  = ah > eHp  ? envAtk * eHp  + (1.0 - envAtk) * ah : envRel * eHp  + (1.0 - envRel) * ah;
            eAll = aa > eAll ? envAtk * eAll + (1.0 - envAtk) * aa : envRel * eAll + (1.0 - envRel) * aa;

            double over = 0.0;
            if (eAll > 1.0e-3)
                over = juce::jlimit (0.0, 1.0, (eHp / (eAll + 1.0e-9) - 0.35) / 0.4);

            const double target = 1.0 - (double) amount * 0.85 * over;
            g = target < g ? gAtk * g + (1.0 - gAtk) * target
                           : gRel * g + (1.0 - gRel) * target;
            gain[(size_t) i] = (float) g;
        }

        for (int ch = 0; ch < nch; ++ch)
        {
            Biquad a, b;
            a.setHighPass (sr, 5500.0);
            b.setHighPass (sr, 5500.0);
            float* d = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i];
                const float hp = b.process (a.process (x));
                d[i] = x - hp * (1.0f - gain[(size_t) i]);
            }
        }
    }

    // ------------------------------------------------------------------------------------------
    void applyPhone (juce::AudioBuffer<float>& buf, double sr, float mix)
    {
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            Biquad hp1, hp2, lp1, lp2;
            hp1.setHighPass (sr, 500.0, 0.8);
            hp2.setHighPass (sr, 500.0, 0.8);
            lp1.setLowPass  (sr, 3200.0, 0.8);
            lp2.setLowPass  (sr, 3200.0, 0.8);

            float* d = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i];
                const float w = lp2.process (lp1.process (hp2.process (hp1.process (x)))) * 1.6f;
                d[i] = x * (1.0f - mix) + w * mix;
            }
        }
    }

    void applyChorus (juce::AudioBuffer<float>& buf, double sr, float depth, float mix)
    {
        const int n = buf.getNumSamples();
        const int size = (int) (0.06 * sr) + 8;
        const double depthMs = 1.0 + 7.0 * (double) depth;

        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            std::vector<float> line ((size_t) size, 0.0f);
            int w = 0;
            const double phase0 = ch == 0 ? 0.0 : 0.5;
            float* d = buf.getWritePointer (ch);

            for (int i = 0; i < n; ++i)
            {
                const float x = d[i];
                line[(size_t) w] = x;

                float acc = 0.0f;
                for (int v = 0; v < 2; ++v)
                {
                    const double rate = v == 0 ? 0.70 : 1.13;
                    const double ph = phase0 + 0.33 * (double) v;
                    const double lfo = std::sin (2.0 * kPi * (rate * (double) i / sr + ph));
                    const double delay = (14.0 + 4.0 * (double) v + depthMs * lfo) * 0.001 * sr;

                    double rp = (double) w - delay;
                    while (rp < 0.0)
                        rp += (double) size;

                    const int i0 = (int) rp % size;
                    const int i1 = (i0 + 1) % size;
                    const float fr = (float) (rp - std::floor (rp));
                    acc += line[(size_t) i0] * (1.0f - fr) + line[(size_t) i1] * fr;
                }

                acc *= 0.5f;
                d[i] = x * (1.0f - mix) + acc * mix;
                w = (w + 1) % size;
            }
        }
    }

    void applyDrive (juce::AudioBuffer<float>& buf, float drive, float mix)
    {
        const float pre = 1.0f + drive * 30.0f;
        const float comp = 1.0f / std::sqrt (pre);
        const int n = buf.getNumSamples();

        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            float* d = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i];
                const float w = std::tanh (x * pre) * comp * 1.4f;
                d[i] = x * (1.0f - mix) + w * mix;
            }
        }
    }

    void applyTremolo (juce::AudioBuffer<float>& buf, double sr, float rate, float depth)
    {
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            float* d = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const double lfo = 0.5 + 0.5 * std::sin (2.0 * kPi * (double) rate * (double) i / sr);
                d[i] *= (float) (1.0 - (double) depth * lfo);
            }
        }
    }
} // namespace

// ----------------------------------------------------------------------------------------------
juce::AudioBuffer<float> trimSilence (const juce::AudioBuffer<float>& in, float threshold)
{
    const int n = in.getNumSamples();
    const int nch = in.getNumChannels();
    int first = n, last = -1;

    for (int ch = 0; ch < nch; ++ch)
    {
        const float* d = in.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            if (std::abs (d[i]) > threshold) { first = std::min (first, i); break; }
        for (int i = n - 1; i >= 0; --i)
            if (std::abs (d[i]) > threshold) { last = std::max (last, i); break; }
    }

    if (last < first)
    {
        juce::AudioBuffer<float> empty (std::max (1, nch), 0);
        return empty;
    }

    juce::AudioBuffer<float> out (nch, last - first + 1);
    out.clear();
    for (int ch = 0; ch < nch; ++ch)
        out.copyFrom (ch, 0, in, ch, first, last - first + 1);
    return out;
}

juce::AudioBuffer<float> resampleBuffer (const juce::AudioBuffer<float>& in, double srcRate, double dstRate)
{
    if (std::abs (srcRate - dstRate) < 1.0 || srcRate <= 0.0 || dstRate <= 0.0)
        return in;

    const double ratio = srcRate / dstRate;
    const int outN = std::max (0, (int) std::floor ((double) in.getNumSamples() / ratio) - 4);

    juce::AudioBuffer<float> out (in.getNumChannels(), outN);
    out.clear();

    for (int ch = 0; ch < in.getNumChannels(); ++ch)
    {
        juce::LagrangeInterpolator interp;
        interp.process (ratio, in.getReadPointer (ch), out.getWritePointer (ch), outN);
    }
    return out;
}

juce::AudioBuffer<float> renderRiser (const juce::AudioBuffer<float>& clipIn, const RenderSettings& s)
{
    auto clip = trimSilence (clipIn);
    const int N = clip.getNumSamples();
    if (N < 16)
        return {};

    const double sr = s.sampleRate;
    const float clipPeak = peakOf (clip);

    // Stereo copy of the dry clip
    juce::AudioBuffer<float> dry (2, N);
    dry.clear();
    for (int ch = 0; ch < 2; ++ch)
        dry.copyFrom (ch, 0, clip, std::min (ch, clip.getNumChannels() - 1), 0, N);

    // Reverb send (optionally cleaned of sibilance)
    juce::AudioBuffer<float> sendClip;
    sendClip.makeCopyOf (dry);
    if (s.voxMode && s.sibilance > 0.0f)
        applyDeEss (sendClip, sr, s.sibilance);

    const float rise = juce::jlimit (0.5f, 10.0f, s.riseSeconds);
    const int T = std::max (1, (int) std::lround ((double) rise * sr));
    const int M = N + T;

    juce::AudioBuffer<float> wet (2, M);
    wet.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        wet.copyFrom (ch, 0, sendClip, ch, 0, N);
        wet.reverse (ch, 0, N);          // 1) reverse the clip
    }

    // 2) run it through the reverb, with silence appended so the tail can ring out
    juce::Reverb reverb;
    juce::Reverb::Parameters p;
    const double fb = std::exp (std::log (0.001) * 0.0253 / (double) rise);
    p.roomSize = (float) juce::jlimit (0.0, 1.0, (fb - 0.7) / 0.28);
    p.damping = s.voxMode ? juce::jmax (s.damping, 0.3f) : s.damping;
    p.wetLevel = 1.0f;
    p.dryLevel = 0.0f;
    p.width = 1.0f;
    p.freezeMode = 0.0f;
    reverb.setParameters (p);
    reverb.setSampleRate (sr);
    reverb.processStereo (wet.getWritePointer (0), wet.getWritePointer (1), M);

    // 3) reverse again: the tail now swells up to the point where the original clip would start
    wet.reverse (0, M);

    if (s.phoneOn)   applyPhone   (wet, sr, s.phoneMix);
    if (s.chorusOn)  applyChorus  (wet, sr, s.chorusDepth, s.chorusMix);
    if (s.driveOn)   applyDrive   (wet, s.drive, s.driveMix);
    if (s.tremoloOn) applyTremolo (wet, sr, s.tremRate, s.tremDepth);

    // Match the level of the source clip
    const float wetPeak = peakOf (wet);
    if (wetPeak > 1.0e-9f)
        wet.applyGain (juce::jlimit (0.2f, 0.95f, clipPeak) / wetPeak);

    const int fadeIn = std::min (M, std::max (1, (int) (0.005 * sr)));
    for (int ch = 0; ch < 2; ++ch)
        wet.applyGainRamp (ch, 0, fadeIn, 0.0f, 1.0f);

    if (! s.includeDry)
    {
        // Riser only: stop exactly where the original clip would begin
        juce::AudioBuffer<float> out (2, T);
        out.clear();
        const int fadeOut = std::min (T, std::max (1, (int) (0.004 * sr)));
        for (int ch = 0; ch < 2; ++ch)
        {
            out.copyFrom (ch, 0, wet, ch, 0, T);
            out.applyGainRamp (ch, T - fadeOut, fadeOut, 1.0f, 0.0f);
        }
        return out;
    }

    // Riser followed by the original clip
    for (int ch = 0; ch < 2; ++ch)
        wet.addFrom (ch, T, dry, ch, 0, N);

    const float pk = peakOf (wet);
    if (pk > 0.98f)
        wet.applyGain (0.98f / pk);

    return wet;
}

} // namespace backswell
