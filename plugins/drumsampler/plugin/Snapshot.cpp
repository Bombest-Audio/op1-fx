// Dev tool: render the plugin editor to a PNG without a host.
// Usage: DrumSamplerSnapshot out.png [width] [glow 0..1] [flash slot 0..23] [playhead 0..1] [select slot 0..23]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String out = argc > 1 ? argv[1] : "drumsampler-editor.png";
    const int width = argc > 2 ? juce::String(argv[2]).getIntValue() : 1280;

    DrumSamplerAudioProcessor processor;
    if (argc > 6)
        processor.setSelectedSlot(juce::String(argv[6]).getIntValue());
    std::unique_ptr<DrumSamplerAudioProcessorEditor> editor(static_cast<DrumSamplerAudioProcessorEditor*>(processor.createEditor()));
    editor->setSize(width, width * 630 / 1280);
    if (argc > 3)
        editor->showPreview(juce::String(argv[3]).getFloatValue(), argc > 4 ? juce::String(argv[4]).getIntValue() : -1,
                            argc > 5 ? juce::String(argv[5]).getFloatValue() : -1.0f);
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);

    juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(out));
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream(image, stream) ? 0 : 1;
}
