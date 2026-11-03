#pragma once
#include <JuceHeader.h>
#include <vector>
#include "ParamIDs.h"

// Factory presets. Values are in engineering units (Hz / dB / ms); the helper
// maps them through each parameter's NormalisableRange so skew is respected.
namespace Presets
{
    struct Band
    {
        float freq, gain, q, mix;
        int   type;      // 0 bell, 1 lowShelf, 2 highShelf
        bool  dyn;
        float thresh, ratio, range, attack, release;
    };

    struct Preset
    {
        const char* name;
        Band bands[4];
        float outGainDb;
    };

    inline void setReal (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float real)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (real));
    }

    inline void setNorm (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float normalised)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (normalised);
    }

    inline void applyBand (juce::AudioProcessorValueTreeState& apvts, int b, const Band& x)
    {
        setReal (apvts, ParamIDs::b (b, "freq"),    x.freq);
        setReal (apvts, ParamIDs::b (b, "gain"),    x.gain);
        setReal (apvts, ParamIDs::b (b, "q"),       x.q);
        setReal (apvts, ParamIDs::b (b, "mix"),     x.mix);
        setNorm (apvts, ParamIDs::b (b, "type"),    x.type * 0.5f);
        setNorm (apvts, ParamIDs::b (b, "dyn"),     x.dyn ? 1.0f : 0.0f);
        setReal (apvts, ParamIDs::b (b, "thresh"),  x.thresh);
        setReal (apvts, ParamIDs::b (b, "ratio"),   x.ratio);
        setReal (apvts, ParamIDs::b (b, "range"),   x.range);
        setReal (apvts, ParamIDs::b (b, "attack"),  x.attack);
        setReal (apvts, ParamIDs::b (b, "release"), x.release);
    }

    inline const std::vector<Preset>& all()
    {
        static const std::vector<Preset> presets =
        {
            { "Flat",
              { {100,0,1,1,0,false,-30,3,12,5,120}, {300,0,1,1,0,false,-30,3,12,5,120},
                {1000,0,1,1,0,false,-30,3,12,5,120}, {4000,0,1,1,0,false,-30,3,12,5,120} }, 0.0f },

            { "Vocal De-Esser",
              { {100,0,1,1,0,false,-30,3,12,5,120}, {300,0,1,1,0,false,-30,3,12,5,120},
                {1000,0,1,1,0,false,-30,3,12,5,120},
                {6500,0,2.2f,1,0,true,-35,4,8,1,60} }, 0.0f },

            { "Low-End Tightener",
              { {90,0,1,1,1,true,-25,3,6,5,120}, {300,0,1,1,0,false,-30,3,12,5,120},
                {1000,0,1,1,0,false,-30,3,12,5,120}, {4000,0,1,1,0,false,-30,3,12,5,120} }, 0.0f },

            { "Air & Body",
              { {150,2,0.7f,1,1,false,-30,3,12,5,120}, {300,0,1,1,0,false,-30,3,12,5,120},
                {1000,0,1,1,0,false,-30,3,12,5,120}, {10000,3,0.7f,1,2,false,-30,3,12,5,120} }, 0.0f },

            { "Smiley",
              { {120,3,0.7f,1,1,false,-30,3,12,5,120}, {300,0,1,1,0,false,-30,3,12,5,120},
                {1000,-2.5f,1.2f,1,0,false,-30,3,12,5,120}, {8000,3,0.7f,1,2,false,-30,3,12,5,120} }, 0.0f }
        };
        return presets;
    }

    inline juce::StringArray names()
    {
        juce::StringArray s;
        for (const auto& p : all()) s.add (p.name);
        return s;
    }

    inline void apply (juce::AudioProcessorValueTreeState& apvts, int index)
    {
        if (index < 0 || index >= (int) all().size()) return;
        const auto& p = all()[(size_t) index];
        for (int b = 0; b < 4; ++b) applyBand (apvts, b, p.bands[b]);
        setReal (apvts, ParamIDs::outGain, p.outGainDb);
    }
}
