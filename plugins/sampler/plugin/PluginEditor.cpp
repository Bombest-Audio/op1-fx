#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kBigW = 240.0f, kBigH = 250.0f;
constexpr float kSmallW = 150.0f, kSmallH = 130.0f;

const juce::Colour kBlue(0xff698eff);
const juce::Colour kGreen(0xff2ef0a0);
const juce::Colour kWhite(0xffe9ece8);
const juce::Colour kOrange(0xffff9a3d);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);

// The waveform art is centred at x 640 and runs the full height. The sample maps onto y 20..610 inside it.
constexpr float kArtX = 640.0f;
constexpr float kArtHalfW = 176.0f;
constexpr float kTraceTop = 20.0f, kTraceBottom = 610.0f;
constexpr int kTimerHz = 60;

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

juce::String pct(double v) { return juce::String(juce::roundToInt(v * 100.0)); }

juce::String secs(double v)
{
    return v < 1.0 ? juce::String(juce::roundToInt(v * 1000.0)) + " ms" : juce::String(v, 2) + " s";
}

float yFor(float frac) { return kTraceTop + juce::jlimit(0.0f, 1.0f, frac) * (kTraceBottom - kTraceTop); }
} // namespace

SamplerKnob::SamplerKnob(juce::String label, juce::String caption, juce::Colour tone, bool small,
                         std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), small_(small), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void SamplerKnob::paint(juce::Graphics& g)
{
    const float dw = small_ ? kSmallW : kBigW, dh = small_ ? kSmallH : kBigH;
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / dw, static_cast<float>(getHeight()) / dh));

    const float cx = dw * 0.5f;
    const float cy = small_ ? 56.0f : 130.0f;
    const float r = small_ ? 26.0f : 74.0f;
    const float stroke = small_ ? 4.0f : 6.2f;
    const float a0 = juce::degreesToRadians(-135.0f), a1 = juce::degreesToRadians(135.0f);
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float aV = a0 + (a1 - a0) * prop;

    g.setColour(small_ ? kMuted : tone_);
    g.setFont(condensed(small_ ? 20.0f : 36.0f, true).withExtraKerningFactor(0.2f));
    g.drawText(label_, 0, 0, static_cast<int>(dw), small_ ? 22 : 36, juce::Justification::centred);

    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.0f, a0, a1, true);
    g.setColour(kTrack);
    g.strokePath(track, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (prop > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0.0f, a0, aV, true);
        g.setColour(tone_);
        g.strokePath(arc, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float dot = small_ ? 4.5f : 7.0f;
    g.setColour(tone_);
    g.fillEllipse(cx + std::sin(aV) * r - dot, cy - std::cos(aV) * r - dot, dot * 2.0f, dot * 2.0f);

    g.setColour(kInk);
    if (small_)
    {
        g.setFont(condensed(26.0f, false));
        g.drawText(format_(getValue()), 0, 90, static_cast<int>(dw), 30, juce::Justification::centred);
    }
    else
    {
        g.setFont(condensed(76.0f, false));
        g.drawText(format_(getValue()), 0, 92, static_cast<int>(dw), 76, juce::Justification::centred);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
        g.drawText(caption_, 0, 222, static_cast<int>(dw), 18, juce::Justification::centred);
    }
}

SamplerAudioProcessorEditor::SamplerAudioProcessorEditor(SamplerAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      start_("START", "PLAY FROM", kBlue, false, pct),
      end_("END", "PLAY TO", kGreen, false, pct),
      tune_("TUNE", "SEMITONES", kWhite, false,
            [](double v) { return (v >= 0.0 ? juce::String("+") : juce::String()) + juce::String(v, 1); }),
      loop_("LOOP", "CROSSFADE", kOrange, false, [](double v) { return v <= 0.0 ? juce::String("OFF") : pct(v); }),
      attack_("ATK", {}, kBlue, true, secs),
      decay_("DEC", {}, kBlue, true, secs),
      sustain_("SUS", {}, kBlue, true, pct),
      release_("REL", {}, kBlue, true, secs),
      level_("LEVEL", {}, kBlue, true, pct),
      root_("ROOT", {}, kBlue, true, [](double v) { return samplerNoteName(juce::roundToInt(v)); })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&start_, &end_, &tune_, &loop_, &attack_, &decay_, &sustain_, &release_, &level_, &root_})
        addAndMakeVisible(*k);

    startA_ = std::make_unique<Attachment>(processor_.apvts, "start", start_);
    endA_ = std::make_unique<Attachment>(processor_.apvts, "end", end_);
    tuneA_ = std::make_unique<Attachment>(processor_.apvts, "tune", tune_);
    loopA_ = std::make_unique<Attachment>(processor_.apvts, "loop", loop_);
    attackA_ = std::make_unique<Attachment>(processor_.apvts, "attack", attack_);
    decayA_ = std::make_unique<Attachment>(processor_.apvts, "decay", decay_);
    sustainA_ = std::make_unique<Attachment>(processor_.apvts, "sustain", sustain_);
    releaseA_ = std::make_unique<Attachment>(processor_.apvts, "release", release_);
    levelA_ = std::make_unique<Attachment>(processor_.apvts, "level", level_);
    rootA_ = std::make_unique<Attachment>(processor_.apvts, "root", root_);

    start_.setDoubleClickReturnValue(true, SAMPLER_DEFAULT_START);
    end_.setDoubleClickReturnValue(true, SAMPLER_DEFAULT_END);
    tune_.setDoubleClickReturnValue(true, SAMPLER_DEFAULT_TUNE);
    loop_.setDoubleClickReturnValue(true, SAMPLER_DEFAULT_LOOP);
    attack_.setDoubleClickReturnValue(true, SAMPLER_DEFAULT_ATTACK);
    decay_.setDoubleClickReturnValue(true, SAMPLER_DEFAULT_DECAY);
    sustain_.setDoubleClickReturnValue(true, SAMPLER_DEFAULT_SUSTAIN);
    release_.setDoubleClickReturnValue(true, SAMPLER_DEFAULT_RELEASE);
    level_.setDoubleClickReturnValue(true, SAMPLER_DEFAULT_LEVEL);
    root_.setDoubleClickReturnValue(true, SAMPLER_DEFAULT_ROOT);
    root_.setMouseDragSensitivity(400);

    info_ = processor_.getSampleInfo();

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

SamplerAudioProcessorEditor::~SamplerAudioProcessorEditor() { stopTimer(); }

void SamplerAudioProcessorEditor::showGlow(float energy, float head)
{
    previewHold_ = true;
    energy_ = juce::jlimit(0.0f, 1.0f, energy);
    head_ = head;
    repaint();
}

juce::Rectangle<int> SamplerAudioProcessorEditor::sampleBarBounds() const
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    return juce::Rectangle<float>(490.0f * s, 566.0f * s, 300.0f * s, 40.0f * s).toNearestInt();
}

void SamplerAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        const auto a = processor_.takeActivity();
        const float level = juce::jlimit(0.0f, 1.0f, a.peak * 1.5f);
        energy_ = level > energy_ ? level : energy_ * 0.92f; // 0.92 per frame is about 200 ms to 1/e
        if (energy_ < 0.002f)
            energy_ = 0.0f;
        head_ = a.head;
        info_ = processor_.getSampleInfo();
    }
    repaint(juce::Rectangle<float>(kArtX - kArtHalfW - 4.0f, 0.0f, kArtHalfW * 2.0f + 8.0f, kDesignH)
                .transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW))
                .getSmallestIntegerContainer()
                .getUnion(sampleBarBounds()));
}

void SamplerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    // Output level lights the waveform softly behind the trace. Kept faint on purpose.
    if (energy_ > 0.01f)
    {
        const juce::Point<float> c(kArtX, kDesignH * 0.5f);
        juce::ColourGradient grad(kBlue.withAlpha(0.16f * energy_), c, kBlue.withAlpha(0.0f), c.translated(0.0f, 330.0f), true);
        g.setGradientFill(grad);
        g.fillRect(juce::Rectangle<float>(kArtX - kArtHalfW, 0.0f, kArtHalfW * 2.0f, kDesignH));
    }

    // The loaded sample, drawn as a thin trace down the middle of the art, with the played region lit and the rest dimmed.
    const float yStart = yFor(*processor_.apvts.getRawParameterValue("start"));
    const float yEnd = yFor(*processor_.apvts.getRawParameterValue("end"));
    const float yLo = juce::jmin(yStart, yEnd), yHi = juce::jmax(yStart, yEnd);
    const auto& peaks = info_.peaks;
    {
        juce::Path outline;
        const float step = (kTraceBottom - kTraceTop) / static_cast<float>(peaks.size());
        for (size_t i = 0; i < peaks.size(); ++i)
            outline.addRectangle(kArtX - 1.0f - peaks[i] * 62.0f, kTraceTop + static_cast<float>(i) * step, 2.0f + peaks[i] * 124.0f,
                                 step * 0.7f);
        g.saveState();
        g.reduceClipRegion(juce::Rectangle<float>(kArtX - 80.0f, yLo, 160.0f, yHi - yLo).toNearestInt());
        g.setColour(kInk.withAlpha(0.32f));
        g.fillPath(outline);
        g.restoreState();
    }
    g.setColour(kPlate.withAlpha(0.55f));
    g.fillRect(juce::Rectangle<float>(kArtX - kArtHalfW, 0.0f, kArtHalfW * 2.0f, yLo));
    g.fillRect(juce::Rectangle<float>(kArtX - kArtHalfW, yHi, kArtHalfW * 2.0f, kDesignH - yHi));

    // Start and end markers.
    g.setColour(kBlue);
    g.fillRect(kArtX - 100.0f, yStart - 0.75f, 200.0f, 1.5f);
    g.setColour(kGreen);
    g.fillRect(kArtX - 100.0f, yEnd - 0.75f, 200.0f, 1.5f);

    // Playhead of the newest voice.
    if (head_ >= 0.0f)
    {
        const float y = yFor(head_);
        g.setColour(kInk.withAlpha(0.85f));
        g.fillRect(kArtX - 60.0f, y - 0.75f, 120.0f, 1.5f);
        juce::ColourGradient grad(kInk.withAlpha(0.25f), kArtX, y, kInk.withAlpha(0.0f), kArtX + 60.0f, y, true);
        g.setGradientFill(grad);
        g.fillEllipse(kArtX - 60.0f, y - 14.0f, 120.0f, 28.0f);
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("SAMPLER", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("ONE SAMPLE, EIGHT VOICES", 50, 62, 300, 16, juce::Justification::centredLeft);

    // Sample name bar: click for the menu, or drop a WAV anywhere on the window.
    const juce::Rectangle<float> bar(490.0f, 566.0f, 300.0f, 40.0f);
    g.setColour(juce::Colour(0xff0d0e10));
    g.fillRoundedRectangle(bar, 4.0f);
    g.setColour(dragOver_ ? kBlue : kTrack);
    g.drawRoundedRectangle(bar, 4.0f, dragOver_ ? 2.0f : 1.0f);
    g.setColour(info_.missing ? kOrange : kInk);
    g.setFont(condensed(22.0f, false).withExtraKerningFactor(0.1f));
    const auto name = info_.missing ? "NOT FOUND: " + info_.name : info_.name;
    g.drawText(name.toUpperCase(), bar.reduced(10.0f, 0.0f), juce::Justification::centred, true);
}

void SamplerAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto big = [s](SamplerKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kBigW * s, kBigH * s).toNearestInt());
    };
    auto small = [s](SamplerKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kSmallW * s, kSmallH * s).toNearestInt());
    };
    big(start_, 60.0f, 110.0f);
    big(end_, 60.0f, 370.0f);
    big(tune_, 980.0f, 110.0f);
    big(loop_, 980.0f, 370.0f);
    small(attack_, 335.0f, 110.0f);
    small(decay_, 335.0f, 250.0f);
    small(level_, 335.0f, 390.0f);
    small(sustain_, 795.0f, 110.0f);
    small(release_, 795.0f, 250.0f);
    small(root_, 795.0f, 390.0f);
}

void SamplerAudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && sampleBarBounds().contains(e.getPosition()))
        showSampleMenu();
}

void SamplerAudioProcessorEditor::loadFile(const juce::File& f)
{
    if (!processor_.loadSampleFile(f))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Sampler",
                                               "I could not read " + f.getFileName() + ". WAV and AIFF files work.");
    info_ = processor_.getSampleInfo();
    repaint();
}

void SamplerAudioProcessorEditor::showSampleMenu()
{
    juce::PopupMenu m;
    m.addItem(1, "Load a WAV or AIFF file...");
    m.addSeparator();
    for (int k = 0; k < SAMPLER_DEMO_KINDS; ++k)
        m.addItem(10 + k, juce::String("Built in: ") + sampler_demo_name(k));
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(
                        localAreaToGlobal(sampleBarBounds())),
                    [safe = juce::Component::SafePointer<SamplerAudioProcessorEditor>(this)](int result) {
                        if (safe == nullptr || result == 0)
                            return;
                        if (result >= 10)
                        {
                            safe->processor_.loadDemoSample(result - 10);
                            safe->info_ = safe->processor_.getSampleInfo();
                            safe->repaint();
                            return;
                        }
                        safe->chooser_ = std::make_unique<juce::FileChooser>("Choose a sample", juce::File(), "*.wav;*.aif;*.aiff");
                        safe->chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                    [safe](const juce::FileChooser& fc) {
                                                        if (safe != nullptr && fc.getResult().existsAsFile())
                                                            safe->loadFile(fc.getResult());
                                                    });
                    });
}

bool SamplerAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff")
            return true;
    }
    return false;
}

void SamplerAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver_ = false;
    for (const auto& f : files)
    {
        const auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff")
        {
            loadFile(juce::File(f));
            return;
        }
    }
    repaint();
}
