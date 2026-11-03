#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class DynEQAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit DynEQAudioProcessorEditor (DynEQAudioProcessor&);
    ~DynEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Graph;
    void updateBandControls();
    void setParamNormalised (const juce::String& id, float normalised);

    DynEQAudioProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;

    std::unique_ptr<Graph> graph;
    int selectedBand = 0;

    juce::Label titleLabel;
    juce::Slider outGainSlider;
    juce::TextButton bypassButton, analyzerButton;

    juce::ComboBox typeCombo;
    juce::ToggleButton dynToggle;
    juce::Slider freqSlider, gainSlider, qSlider, mixSlider,
                 threshSlider, ratioSlider, rangeSlider, attackSlider, releaseSlider;

    juce::ComboBox presetCombo;
    juce::Label typeLabel, dynLabel, freqLabel, gainLabel, qLabel, mixLabel,
               threshLabel, ratioLabel, rangeLabel, attackLabel, releaseLabel, presetLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynEQAudioProcessorEditor)
};
