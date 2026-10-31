// Factory presets for the JUCE plugins. The same presets.json the MPC port compiles into VST programs
// ({"presets":[{"name":..., "values":{paramID: value}}]}, values in each parameter's own units) is embedded
// with the plugin and exposed through the host's program list.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

#ifdef OP1FX_HAS_PRESETS
#include "BinaryData.h"
#define OP1FX_PRESET_ARGS BinaryData::presets_json, BinaryData::presets_jsonSize
#else
#define OP1FX_PRESET_ARGS nullptr, 0
#endif

namespace op1fx
{
class Presets
{
public:
    // json: the embedded presets.json, or null/0 when a plugin ships none (one empty program, as before).
    Presets(const char* json, int size)
    {
        if (json == nullptr || size <= 0)
            return;
        const auto root = juce::JSON::parse(juce::String::fromUTF8(json, size));
        const auto* list = root.getProperty("presets", {}).getArray();
        if (list == nullptr)
            return;
        for (const auto& item : *list)
        {
            Entry e;
            e.name = item.getProperty("name", {}).toString();
            if (const auto* obj = item.getProperty("values", {}).getDynamicObject())
                for (const auto& nv : obj->getProperties())
                    e.values.emplace_back(nv.name.toString(), static_cast<float>(double(nv.value)));
            entries_.push_back(std::move(e));
        }
    }

    int count() const { return entries_.empty() ? 1 : static_cast<int>(entries_.size()); }
    int current() const { return current_; }

    juce::String name(int index) const
    {
        return index >= 0 && index < static_cast<int>(entries_.size()) ? entries_[(size_t) index].name : juce::String();
    }

    // A host re-selecting the current program (some do at load) must not overwrite a restored state.
    void select(juce::AudioProcessorValueTreeState& apvts, int index)
    {
        if (index < 0 || index >= static_cast<int>(entries_.size()) || index == current_)
            return;
        for (const auto& [id, value] : entries_[(size_t) index].values)
            if (auto* p = apvts.getParameter(id))
                p->setValueNotifyingHost(p->convertTo0to1(value));
        current_ = index;
    }

private:
    struct Entry
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };
    std::vector<Entry> entries_;
    int current_ = 0;
};
} // namespace op1fx
