#ifndef FLUTTERPROCESS_H_INCLUDED
#define FLUTTERPROCESS_H_INCLUDED

#include <JuceHeader.h>

class FlutterProcess
{
public:
    FlutterProcess() = default;

    void prepare (double sampleRate, int samplesPerBlock, int numChannels);
    void prepareBlock (float curDepth, float flutterFreq, int numSamples, int numChannels);
    void plotBuffer (foleys::MagicPlotSource* plot);

    inline bool shouldTurnOff() const noexcept { return depthSlew[0].getTargetValue() == depthSlewMin; }
    inline void updatePhase (size_t ch) noexcept
    {
        phase1[ch] += angleDelta1;
        phase2[ch] += angleDelta2;
        phase3[ch] += angleDelta3;
    }

    inline std::pair<float, float> getLFO (int n, size_t ch) noexcept
    {
        updatePhase (ch);
        flutterPtrs[ch][n] = depthSlew[ch].getNextValue()
                             * (amp1 * std::cos (phase1[ch] + phaseOff1)
                                + amp2 * std::cos (phase2[ch] + phaseOff2)
                                + amp3 * std::cos (phase3[ch] + phaseOff3));
        return std::make_pair (flutterPtrs[ch][n], dcOffset);
    }

    inline void boundPhase (size_t ch) noexcept
    {
        while (phase1[ch] >= MathConstants<float>::twoPi)
            phase1[ch] -= MathConstants<float>::twoPi;
        while (phase2[ch] >= MathConstants<float>::twoPi)
            phase2[ch] -= MathConstants<float>::twoPi;
        while (phase2[ch] >= MathConstants<float>::twoPi)
            phase2[ch] -= MathConstants<float>::twoPi;
    }

private:
    std::vector<float> phase1;
    std::vector<float> phase2;
    std::vector<float> phase3;

    float amp1 = 0.0f;
    float amp2 = 0.0f;
    float amp3 = 0.0f;
    std::vector<SmoothedValue<float, ValueSmoothingTypes::Multiplicative>> depthSlew;

    float angleDelta1 = 0.0f;
    float angleDelta2 = 0.0f;
    float angleDelta3 = 0.0f;

    float dcOffset = 0.0f;
    static constexpr float phaseOff1 = 0.0f;
    static constexpr float phaseOff2 = 13.0f * MathConstants<float>::pi / 4.0f;
    static constexpr float phaseOff3 = -MathConstants<float>::pi / 10.0f;

    AudioBuffer<float> flutterBuffer;
    float** flutterPtrs;
    float fs = 48000.0f;

    static constexpr float depthSlewMin = 0.001f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlutterProcess)
};

#endif // FLUTTERPROCESS_H_INCLUDED
