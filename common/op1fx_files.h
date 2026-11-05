// Files picked in a juce::FileChooser, as local files the samplers can read.
//
// On the desktop this is just getResults(). On iOS the picker hands back security-scoped URLs into the Files app, and a
// plain juce::File for that path cannot be read once the picker closes, so each pick is copied into this app's own
// Documents/Samples folder first. The copy stays there so a saved session can find the sample again.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

inline juce::Array<juce::File> op1fxChosenFiles(const juce::FileChooser& fc)
{
#if JUCE_IOS
    juce::Array<juce::File> files;
    const auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Samples");
    dir.createDirectory();
    for (const auto& url : fc.getURLResults())
    {
        auto in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
        if (in == nullptr)
        {
            DBG("op1fxChosenFiles: cannot open " << url.toString(false));
            continue;
        }
        auto dest = dir.getChildFile(url.getFileName());
        dest.deleteFile();
        juce::FileOutputStream out(dest);
        if (!out.openedOk() || out.writeFromInputStream(*in, -1) < 0)
        {
            DBG("op1fxChosenFiles: cannot copy to " << dest.getFullPathName());
            continue;
        }
        out.flush();
        files.add(dest);
    }
    return files;
#else
    return fc.getResults();
#endif
}
