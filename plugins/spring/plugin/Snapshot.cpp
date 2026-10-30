// Dev tool: render the plugin editor to a PNG without a host.
// Usage: SpringSnapshot out.png [width] [energy 0..1] [pulse position 0..1]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String out = argc > 1 ? argv[1] : "spring-editor.png";
    const int width = argc > 2 ? juce::String(argv[2]).getIntValue() : 1280;

    SpringAudioProcessor processor;
    std::unique_ptr<SpringAudioProcessorEditor> editor(static_cast<SpringAudioProcessorEditor*>(processor.createEditor()));
    if (argc > 3)
        editor->showEnergy(juce::String(argv[3]).getFloatValue(), argc > 4 ? juce::String(argv[4]).getFloatValue() : 0.5f);
    editor->setSize(width, width * 630 / 1280);
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);

    juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(out));
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream(image, stream) ? 0 : 1;
}
