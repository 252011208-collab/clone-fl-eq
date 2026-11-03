#pragma once
#include <JuceHeader.h>
#include "dsp/DynamicEQ.h"

class DynEQAudioProcessor : public juce::AudioProcessor
{
public:
    DynEQAudioProcessor();
    ~DynEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DynEQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    dyneq::DynamicEQ eq;

    // Mono-mix ring buffer consumed by the editor's spectrum analyser.
    juce::AbstractFifo analyzerFifo { 1 << 16 };
    std::vector<float> analyzerBuf { size_t (1) << 16, 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushAnalyzer (const juce::AudioBuffer<float>& buffer, int numSamples);
    void readParamsIntoEngine();

    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynEQAudioProcessor)
};
