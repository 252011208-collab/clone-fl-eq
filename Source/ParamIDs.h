#pragma once
#include <JuceHeader.h>

namespace ParamIDs
{
    static constexpr const char* outGain  = "outGain";
    static constexpr const char* bypass   = "bypass";
    static constexpr const char* analyzer = "analyzer";

    inline juce::String b (int i, const char* suffix) { return "b" + juce::String (i) + "_" + suffix; }
}
