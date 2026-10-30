// Dev tool: render the plugin editor to a PNG without a host. Usage: DBoxSnapshot out.png [width] [lit key mask]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String out = argc > 1 ? argv[1] : "dbox-editor.png";
    const int width = argc > 2 ? juce::String(argv[2]).getIntValue() : 1280;

    DBoxAudioProcessor processor;
    std::unique_ptr<DBoxAudioProcessorEditor> editor(static_cast<DBoxAudioProcessorEditor*>(processor.createEditor()));
    if (argc > 3)
        editor->showPads(static_cast<unsigned>(juce::String(argv[3]).getIntValue()));
    editor->setSize(width, width * 630 / 1280);
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);

    juce::File file(juce::File::getCurrentWorkingDirectory().getChildFile(out));
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream(image, stream) ? 0 : 1;
}
