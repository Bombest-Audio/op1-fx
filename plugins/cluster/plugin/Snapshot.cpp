// Dev tool: render the plugin editor to a PNG without a host.
// Usage: ClusterSnapshot out.png [width] [level 0..1]
// With a level argument the dots light in a fixed pattern (strongest on the first), so the animated look can be checked.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String out = argc > 1 ? argv[1] : "cluster-editor.png";
    const int width = argc > 2 ? juce::String(argv[2]).getIntValue() : 1280;

    ClusterAudioProcessor processor;
    std::unique_ptr<ClusterAudioProcessorEditor> editor(static_cast<ClusterAudioProcessorEditor*>(processor.createEditor()));
    if (argc > 3)
    {
        const float level = juce::String(argv[3]).getFloatValue();
        std::array<float, CLUSTER_OSCS> act{};
        for (size_t i = 0; i < act.size(); ++i)
            act[i] = level * (1.0f - 0.11f * static_cast<float>(i));
        editor->showActivity(act, level);
    }
    editor->setSize(width, width * 630 / 1280);
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);

    juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(out));
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream(image, stream) ? 0 : 1;
}
