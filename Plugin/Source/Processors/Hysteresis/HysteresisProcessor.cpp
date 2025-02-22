#include "HysteresisProcessor.h"

namespace
{
enum
{
    numSteps = 500,
};

constexpr double v1Norm = 1.414 / 10000.0;

template <typename T>
static void interleaveSamples (const T** source, T* dest, int numSamples, int numChannels)
{
    for (int chan = 0; chan < numChannels; ++chan)
    {
        auto i = chan;
        const auto* src = source[chan];

        for (int j = 0; j < numSamples; ++j)
        {
            dest[i] = src[j];
            i += numChannels;
        }
    }
}

template <typename T>
static void deinterleaveSamples (const T* source, T** dest, int numSamples, int numChannels)
{
    for (int chan = 0; chan < numChannels; ++chan)
    {
        auto i = chan;
        auto* dst = dest[chan];

        for (int j = 0; j < numSamples; ++j)
        {
            dst[j] = source[i];
            i += numChannels;
        }
    }
}
} // namespace

HysteresisProcessor::HysteresisProcessor (AudioProcessorValueTreeState& vts) : osManager (vts)
{
    using namespace chowdsp::ParamUtils;
    loadParameterPointer (driveParam, vts, "drive");
    loadParameterPointer (satParam, vts, "sat");
    loadParameterPointer (widthParam, vts, "width");
    modeParam = vts.getRawParameterValue ("mode");
    onOffParam = vts.getRawParameterValue ("hyst_onoff");
}

void HysteresisProcessor::createParameterLayout (chowdsp::Parameters& params)
{
    using namespace chowdsp::ParamUtils;
    emplace_param<chowdsp::BoolParameter> (params, "hyst_onoff", "Tape On/Off", true);
    emplace_param<chowdsp::FloatParameter> (params, "drive", "Tape Drive", NormalisableRange { 0.0f, 1.0f }, 0.5f, &floatValToString, &stringToFloatVal);
    emplace_param<chowdsp::FloatParameter> (params, "sat", "Tape Saturation", NormalisableRange { 0.0f, 1.0f }, 0.5f, &floatValToString, &stringToFloatVal);
    emplace_param<chowdsp::FloatParameter> (params, "width", "Tape Bias", NormalisableRange { 0.0f, 1.0f }, 0.5f, &floatValToString, &stringToFloatVal);

    emplace_param<chowdsp::ChoiceParameter> (params, "mode", "Tape Mode", StringArray ({ "RK2", "RK4", "NR4", "NR8", "STN", "V1" }), 0);

    using OSManager = decltype (osManager);
    OSManager::createParameterLayout (params, OSManager::OSFactor::TwoX, OSManager::OSMode::MinPhase);
}

void HysteresisProcessor::setSolver (int newSolver)
{
    // Hack for V1 solver mode
    useV1 = newSolver == SolverType::NUM_SOLVERS;
    solver = useV1 ? RK4 : static_cast<SolverType> (newSolver);

    // set clip level for solver
    switch (solver)
    {
        case RK2:
        case RK4:
            clipLevel = 10.0f;
            return;

        case NR4:
        case NR8:
            clipLevel = 12.5f;
            return;

        default:
            clipLevel = 20.0f;
    };
}

double HysteresisProcessor::calcMakeup()
{
    return (1.0 + 0.6 * width[0].getTargetValue()) / (0.5 + 1.5 * (1.0 - sat[0].getTargetValue()));
}

void HysteresisProcessor::setDrive (float newDrive)
{
    for (auto& driveVal : drive)
        driveVal.setTargetValue ((double) newDrive);
}

void HysteresisProcessor::setWidth (float newWidth)
{
    for (auto& widthVal : width)
        widthVal.setTargetValue ((double) newWidth);
}

void HysteresisProcessor::setSaturation (float newSaturation)
{
    for (auto& satVal : sat)
        satVal.setTargetValue ((double) newSaturation);
}

void HysteresisProcessor::setOversampling()
{
    if (osManager.updateOSFactor())
    {
        for (size_t ch = 0; ch < hProcs.size(); ++ch)
        {
            hProcs[ch].setSampleRate (fs * osManager.getOSFactor());
            hProcs[ch].cook (drive[ch].getCurrentValue(), width[ch].getCurrentValue(), sat[ch].getCurrentValue(), wasV1);
            hProcs[ch].reset();
        }

        calcBiasFreq();
    }
}

void HysteresisProcessor::calcBiasFreq()
{
    biasFreq = fs * osManager.getOSFactor() / 2.0;
}

void HysteresisProcessor::prepareToPlay (double sampleRate, int samplesPerBlock, int numChannels)
{
    fs = sampleRate;
    wasV1 = useV1;

    osManager.prepareToPlay (sampleRate, samplesPerBlock, numChannels);
    calcBiasFreq();

    drive.resize ((size_t) numChannels);
    for (auto& val : drive)
        val.reset (numSteps);

    width.resize ((size_t) numChannels);
    for (auto& val : width)
        val.reset (numSteps);

    sat.resize ((size_t) numChannels);
    for (auto& val : sat)
        val.reset (numSteps);

    hProcs.resize ((size_t) numChannels);
    for (size_t ch = 0; ch < (size_t) numChannels; ++ch)
    {
        hProcs[ch].setSampleRate (sampleRate * osManager.getOSFactor());
        hProcs[ch].cook (drive[ch].getCurrentValue(), width[ch].getCurrentValue(), sat[ch].getCurrentValue(), wasV1);
        hProcs[ch].reset();
    }

    biasAngle.resize ((size_t) numChannels, 0.0);
    makeup.reset (numSteps);

    dcBlocker.resize ((size_t) numChannels);
    for (auto& filt : dcBlocker)
        filt.prepare (sampleRate, dcFreq);

    doubleBuffer.setSize (numChannels, samplesPerBlock);
    bypass.prepare (samplesPerBlock, numChannels, bypass.toBool (onOffParam));

#if HYSTERESIS_USE_SIMD
    const auto maxOSBlockSize = (uint32) samplesPerBlock * 16;
    const auto numVecChannels = chowdsp::Math::ceiling_divide ((size_t) numChannels, Vec2::size);
    interleavedBlock = chowdsp::AudioBlock<Vec2> (interleavedBlockData, numVecChannels, maxOSBlockSize);
    zeroBlock = chowdsp::AudioBlock<double> (zeroData, Vec2::size, maxOSBlockSize);
    zeroBlock.clear();

    channelPointers.resize (numVecChannels * Vec2::size);
#endif
}

void HysteresisProcessor::releaseResources()
{
    osManager.reset();
}

float HysteresisProcessor::getLatencySamples() const noexcept
{
    // latency of oversampling + fudge factor for hysteresis
    return onOffParam->load() == 1.0f ? osManager.getLatencySamples() + 1.4f // on
                                      : 0.0f; // off
}

void HysteresisProcessor::processBlock (AudioBuffer<float>& buffer)
{
    const auto numChannels = buffer.getNumChannels();

    if (! bypass.processBlockIn (buffer, bypass.toBool (onOffParam)))
        return;

    setSolver ((int) *modeParam);
    setDrive (*driveParam);
    setSaturation (*satParam);
    setWidth (1.0f - *widthParam);
    makeup.setTargetValue (calcMakeup());
    setOversampling();

    bool needsSmoothing = drive[0].isSmoothing() || width[0].isSmoothing() || sat[0].isSmoothing() || wasV1 != useV1;

    if (useV1 != wasV1)
    {
        for (auto& hProc : hProcs)
            hProc.reset();
    }

    wasV1 = useV1;

    // clip input to avoid unstable hysteresis
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* bufferPtr = buffer.getWritePointer (ch);
        FloatVectorOperations::clip (bufferPtr,
                                     bufferPtr,
                                     -clipLevel,
                                     clipLevel,
                                     buffer.getNumSamples());
    }

    doubleBuffer.makeCopyOf (buffer, true);

    dsp::AudioBlock<double> block (doubleBuffer);
    dsp::AudioBlock<double> osBlock = osManager.processSamplesUp (block);

#if HYSTERESIS_USE_SIMD
    const auto n = osBlock.getNumSamples();
    auto* inout = channelPointers.data();
    const auto numChannelsPadded = channelPointers.size();
    for (size_t ch = 0; ch < numChannelsPadded; ++ch)
        inout[ch] = (ch < osBlock.getNumChannels() ? const_cast<double*> (osBlock.getChannelPointer (ch)) : zeroBlock.getChannelPointer (ch % Vec2::size));

    // interleave channels
    for (size_t ch = 0; ch < numChannelsPadded; ch += Vec2::size)
    {
        auto* simdBlockData = reinterpret_cast<double*> (interleavedBlock.getChannelPointer (ch / Vec2::size));
        interleaveSamples (&inout[ch], simdBlockData, static_cast<int> (n), static_cast<int> (Vec2::size));
    }

    auto&& processBlock = interleavedBlock.getSubBlock (0, n);

    using ProcessType = Vec2;
#else
    auto&& processBlock = osBlock;

    using ProcessType = double;
#endif

    if (useV1)
    {
        if (needsSmoothing)
            processSmoothV1<ProcessType> (processBlock);
        else
            processV1<ProcessType> (processBlock);
    }
    else
    {
        switch (solver)
        {
            case RK2:
                if (needsSmoothing)
                    processSmooth<RK2, ProcessType> (processBlock);
                else
                    process<RK2, ProcessType> (processBlock);
                break;
            case RK4:
                if (needsSmoothing)
                    processSmooth<RK4, ProcessType> (processBlock);
                else
                    process<RK4, ProcessType> (processBlock);
                break;
            case NR4:
                if (needsSmoothing)
                    processSmooth<NR4, ProcessType> (processBlock);
                else
                    process<NR4, ProcessType> (processBlock);
                break;
            case NR8:
                if (needsSmoothing)
                    processSmooth<NR8, ProcessType> (processBlock);
                else
                    process<NR8, ProcessType> (processBlock);
                break;
            case STN:
                if (needsSmoothing)
                    processSmooth<STN, ProcessType> (processBlock);
                else
                    process<STN, ProcessType> (processBlock);
                break;
            default:
                jassertfalse; // unknown solver!
        };
    }

#if HYSTERESIS_USE_SIMD
    // de-interleave channels
    for (size_t ch = 0; ch < numChannelsPadded; ch += Vec2::size)
    {
        auto* simdBlockData = reinterpret_cast<double*> (interleavedBlock.getChannelPointer (ch / Vec2::size));
        deinterleaveSamples (simdBlockData,
                             const_cast<double**> (&inout[ch]),
                             static_cast<int> (n),
                             static_cast<int> (Vec2::size));
    }
#endif

    osManager.processSamplesDown (block);

    buffer.makeCopyOf (doubleBuffer, true);
    applyDCBlockers (buffer);

    bypass.processBlockOut (buffer, bypass.toBool (onOffParam));
}

template <typename T, typename SmoothType>
void applyMakeup (chowdsp::AudioBlock<T>& block, SmoothType& makeup)
{
#if HYSTERESIS_USE_SIMD
    const auto numSamples = block.getNumSamples();
    const auto numChannels = block.getNumChannels();

    if (makeup.isSmoothing())
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            const auto scaler = makeup.getNextValue();

            for (size_t ch = 0; ch < numChannels; ++ch)
                block.getChannelPointer (ch)[i] *= scaler;
        }
    }
    else
    {
        const auto scaler = makeup.getTargetValue();
        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* x = block.getChannelPointer (ch);
            for (size_t i = 0; i < numSamples; ++i)
                x[i] *= scaler;
        }
    }
#else
    block *= makeup;
#endif
}

template <SolverType solverType, typename T>
void HysteresisProcessor::process (chowdsp::AudioBlock<T>& block)
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        auto* x = block.getChannelPointer (channel);
        auto& hProc = hProcs[channel];
        for (size_t samp = 0; samp < numSamples; samp++)
            x[samp] = hProc.process<solverType> (x[samp]);
    }

    applyMakeup<T> (block, makeup);
}

template <SolverType solverType, typename T>
void HysteresisProcessor::processSmooth (chowdsp::AudioBlock<T>& block)
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        auto* x = block.getChannelPointer (channel);
        auto& hProc = hProcs[channel];
        for (size_t samp = 0; samp < numSamples; samp++)
        {
            hProc.cook (drive[channel].getNextValue(), width[channel].getNextValue(), sat[channel].getNextValue(), false);
            x[samp] = hProc.process<solverType> (x[samp]);
        }
    }

    applyMakeup<T> (block, makeup);
}

template <typename T>
void HysteresisProcessor::processV1 (chowdsp::AudioBlock<T>& block)
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();
    const auto angleDelta = MathConstants<double>::twoPi * biasFreq / (fs * osManager.getOSFactor());

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        auto* x = block.getChannelPointer (channel);
        auto& hProc = hProcs[channel];
        auto& bAngle = biasAngle[channel];
        const auto bAngleMult = biasGain * (1.0 - width[channel].getCurrentValue());
        for (size_t samp = 0; samp < numSamples; samp++)
        {
            auto bias = bAngleMult * std::sin (bAngle);
            bAngle += angleDelta;
            bAngle -= MathConstants<double>::twoPi * (bAngle >= MathConstants<double>::twoPi);

            x[samp] = hProc.process<RK4> ((x[samp] + bias) * 10000.0) * v1Norm;
        }
    }
}

template <typename T>
void HysteresisProcessor::processSmoothV1 (chowdsp::AudioBlock<T>& block)
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();
    const auto angleDelta = MathConstants<double>::twoPi * biasFreq / (fs * osManager.getOSFactor());

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        auto* x = block.getChannelPointer (channel);
        auto& hProc = hProcs[channel];
        auto& bAngle = biasAngle[channel];
        for (size_t samp = 0; samp < numSamples; samp++)
        {
            hProc.cook (drive[channel].getNextValue(), width[channel].getNextValue(), sat[channel].getNextValue(), true);

            auto bias = biasGain * (1.0 - width[channel].getCurrentValue()) * std::sin (bAngle);
            bAngle += angleDelta;
            bAngle -= MathConstants<double>::twoPi * (bAngle >= MathConstants<double>::twoPi);

            x[samp] = hProc.process<RK4> ((x[samp] + bias) * 10000.0) * v1Norm;
        }
    }
}

void HysteresisProcessor::applyDCBlockers (AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dcBlocker[(size_t) ch].processBlock (buffer.getWritePointer (ch), buffer.getNumSamples());
}
