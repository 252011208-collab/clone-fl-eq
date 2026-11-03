#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

namespace
{
    constexpr int kNumBands = 4;

    const float defaultFreq[kNumBands]  = { 100.0f, 300.0f, 1000.0f, 4000.0f };
    const float defaultGain[kNumBands]  = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float defaultQ[kNumBands]     = { 1.0f, 1.0f, 1.0f, 1.0f };
    const juce::StringArray kTypes { "Bell", "Low Shelf", "High Shelf" };

    inline juce::NormalisableRange<float> r (float lo, float hi, float skew = 0.0f)
    { return juce::NormalisableRange<float> (lo, hi, 0.0f, skew); }
}

DynEQAudioProcessor::DynEQAudioProcessor()
    : apvts (*this, nullptr, "DynEQ", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DynEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < kNumBands; ++i)
    {
        const auto n = juce::String (i + 1);
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::b (i, "freq"),
            "B" + n + " Freq", r (20.0f, 20000.0f, 0.25f), defaultFreq[i]));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::b (i, "gain"),
            "B" + n + " Gain", r (-18.0f, 18.0f), defaultGain[i]));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::b (i, "q"),
            "B" + n + " Q", r (0.1f, 18.0f, 0.5f), defaultQ[i]));
        layout.add (std::make_unique<juce::AudioParameterChoice> (ParamIDs::b (i, "type"),
            "B" + n + " Type", kTypes, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::b (i, "mix"),
            "B" + n + " Mix", r (0.0f, 1.0f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterBool> (ParamIDs::b (i, "dyn"),
            "B" + n + " Dyn", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::b (i, "thresh"),
            "B" + n + " Thresh", r (-60.0f, 0.0f), -30.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::b (i, "ratio"),
            "B" + n + " Ratio", r (1.0f, 10.0f), 3.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::b (i, "range"),
            "B" + n + " Range", r (0.0f, 24.0f), 12.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::b (i, "attack"),
            "B" + n + " Attack", r (0.1f, 200.0f, 0.4f), 5.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::b (i, "release"),
            "B" + n + " Release", r (5.0f, 2000.0f, 0.4f), 120.0f));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (ParamIDs::outGain, "Output", r (-24.0f, 24.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParamIDs::bypass, "Bypass", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParamIDs::analyzer, "Analyzer", true));

    return layout;
}

void DynEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    eq.prepare (sampleRate);
    eq.reset();
    analyzerFifo.reset();
}

void DynEQAudioProcessor::releaseResources()
{
}

bool DynEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}

void DynEQAudioProcessor::readParamsIntoEngine()
{
    for (int i = 0; i < kNumBands; ++i)
    {
        dyneq::BandParams p;
        p.freq        = *apvts.getRawParameterValue (ParamIDs::b (i, "freq"));
        p.gainDb      = *apvts.getRawParameterValue (ParamIDs::b (i, "gain"));
        p.q           = *apvts.getRawParameterValue (ParamIDs::b (i, "q"));
        p.mix         = *apvts.getRawParameterValue (ParamIDs::b (i, "mix"));
        p.type        = static_cast<dyneq::FilterType> ((int) *apvts.getRawParameterValue (ParamIDs::b (i, "type")));
        p.dynamic     = *apvts.getRawParameterValue (ParamIDs::b (i, "dyn")) > 0.5f;
        p.thresholdDb = *apvts.getRawParameterValue (ParamIDs::b (i, "thresh"));
        p.ratio       = *apvts.getRawParameterValue (ParamIDs::b (i, "ratio"));
        p.rangeDb     = *apvts.getRawParameterValue (ParamIDs::b (i, "range"));
        p.attackMs    = *apvts.getRawParameterValue (ParamIDs::b (i, "attack"));
        p.releaseMs   = *apvts.getRawParameterValue (ParamIDs::b (i, "release"));
        eq.setBand (i, p);
    }
    eq.setOutputGainDb (*apvts.getRawParameterValue (ParamIDs::outGain));
}

void DynEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (*apvts.getRawParameterValue (ParamIDs::bypass) > 0.5f)
    {
        pushAnalyzer (buffer, buffer.getNumSamples());
        return;
    }

    readParamsIntoEngine();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels >= 2)
    {
        for (int c = 0; c + 1 < numChannels; c += 2)
            eq.process (buffer.getWritePointer (c), buffer.getWritePointer (c + 1),
                        buffer.getWritePointer (c), buffer.getWritePointer (c + 1), numSamples);
        if ((numChannels & 1) != 0)
        {
            float* p = buffer.getWritePointer (numChannels - 1);
            eq.process (p, p, p, p, numSamples);
        }
    }
    else
    {
        float* p = buffer.getWritePointer (0);
        eq.process (p, p, p, p, numSamples);
    }

    pushAnalyzer (buffer, numSamples);
}

void DynEQAudioProcessor::pushAnalyzer (const juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (numSamples <= 0) return;
    const int ch = buffer.getNumChannels();
    const int space = analyzerFifo.getFreeSpace();
    if (space < numSamples) return;

    int start1, size1, start2, size2;
    analyzerFifo.prepareToWrite (numSamples, start1, size1, start2, size2);
    auto push = [&] (int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            float s = 0.0f;
            if (ch >= 2) s = 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i));
            else         s = buffer.getSample (0, i);
            analyzerBuf[(size_t) start + (size_t) i] = s;
        }
    };
    push (start1, size1);
    push (start2, size2);
    analyzerFifo.finishedWrite (numSamples);
}

juce::AudioProcessorEditor* DynEQAudioProcessor::createEditor()
{
    return new DynEQAudioProcessorEditor (*this);
}

void DynEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DynEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
