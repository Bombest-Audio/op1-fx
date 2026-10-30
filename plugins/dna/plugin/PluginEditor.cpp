#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f, kKnobH = 250.0f;
constexpr float kMiniW = 80.0f, kMiniH = 100.0f;

const juce::Colour kBlue(0xff2f8bff);
const juce::Colour kGreen(0xff1de9a0);
const juce::Colour kOrange(0xffff8a1f);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);

constexpr float kHelixX = 490.0f, kHelixW = 300.0f;  // the helix column inside the 1280x630 faceplate
constexpr int kTimerHz = 60;

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

juce::String percentText(double v) { return juce::String(juce::roundToInt(v * 100.0)); }

juce::String signedPercentText(double v)
{
    const int p = juce::roundToInt(v * 100.0);
    return (p > 0 ? "+" : "") + juce::String(p);
}

juce::String timeText(double norm)
{
    const double s = dna_time_seconds(static_cast<float>(norm));
    return s < 1.0 ? juce::String(juce::roundToInt(s * 1000.0)) + "ms" : juce::String(s, 2) + "s";
}
} // namespace

DnaKnob::DnaKnob(juce::String label, juce::String caption, juce::Colour tone, bool mini, bool bipolar,
                 std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), mini_(mini), bipolar_(bipolar),
      format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void DnaKnob::paint(juce::Graphics& g)
{
    if (mini_)
        paintMini(g);
    else
        paintBig(g);
}

void DnaKnob::paintBig(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kKnobW,
                                                static_cast<float>(getHeight()) / kKnobH));

    g.setColour(tone_);
    g.setFont(condensed(36.0f, true).withExtraKerningFactor(0.2f));
    g.drawText(label_, 0, 0, static_cast<int>(kKnobW), 36, juce::Justification::centred);

    const float cx = kKnobW * 0.5f, cy = 130.0f, r = 74.0f;
    const float a0 = juce::degreesToRadians(-135.0f), a1 = juce::degreesToRadians(135.0f);
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float aV = a0 + (a1 - a0) * prop;
    const float aFrom = bipolar_ ? 0.0f : a0;

    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.0f, a0, a1, true);
    g.setColour(kTrack);
    g.strokePath(track, juce::PathStrokeType(6.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (std::abs(aV - aFrom) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0.0f, juce::jmin(aFrom, aV), juce::jmax(aFrom, aV), true);
        g.setColour(tone_);
        g.strokePath(arc, juce::PathStrokeType(6.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (bipolar_)
    {
        // a small tick at the centre detent, straight up
        g.setColour(kMuted);
        g.fillRect(cx - 1.0f, cy - r - 10.0f, 2.0f, 6.0f);
    }

    const float tx = cx + std::sin(aV) * r;
    const float ty = cy - std::cos(aV) * r;
    g.setColour(tone_);
    g.fillEllipse(tx - 7.0f, ty - 7.0f, 14.0f, 14.0f);

    g.setColour(kInk);
    g.setFont(condensed(76.0f, false));
    g.drawText(format_(getValue()), 0, 92, static_cast<int>(kKnobW), 76, juce::Justification::centred);

    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText(caption_, 0, 222, static_cast<int>(kKnobW), 18, juce::Justification::centred);
}

void DnaKnob::paintMini(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kMiniW,
                                                static_cast<float>(getHeight()) / kMiniH));

    g.setColour(tone_);
    g.setFont(condensed(15.0f, true).withExtraKerningFactor(0.16f));
    g.drawText(label_, 0, 0, static_cast<int>(kMiniW), 16, juce::Justification::centred);

    const float cx = kMiniW * 0.5f, cy = 54.0f, r = 24.0f;
    const float a0 = juce::degreesToRadians(-135.0f), a1 = juce::degreesToRadians(135.0f);
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float aV = a0 + (a1 - a0) * prop;

    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.0f, a0, a1, true);
    g.setColour(kTrack);
    g.strokePath(track, juce::PathStrokeType(3.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (prop > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0.0f, a0, aV, true);
        g.setColour(tone_);
        g.strokePath(arc, juce::PathStrokeType(3.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(tone_);
    g.fillEllipse(cx + std::sin(aV) * r - 3.5f, cy - std::cos(aV) * r - 3.5f, 7.0f, 7.0f);

    g.setColour(kInk);
    g.setFont(condensed(16.0f, false));
    g.drawText(format_(getValue()), 0, 40, static_cast<int>(kMiniW), 28, juce::Justification::centred);
}

DnaAudioProcessorEditor::DnaAudioProcessorEditor(DnaAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      mutate_("MUTATE", "TOWARD THE MUTANTS", kBlue, false, true, signedPercentText),
      bits_("BITS", "GENES PER CYCLE", kGreen, false, false,
            [](double v) { return juce::String(juce::roundToInt(dna_bits_value(static_cast<float>(v)))); }),
      sub_("SUB", "SECOND CYCLE DRIFT", kOrange, false, false, percentText),
      grit_("GRIT", "RANDOM GENE FLIPS", kOrange, false, false, percentText),
      attack_("ATK", "", kBlue, true, false, timeText),
      decay_("DEC", "", kBlue, true, false, timeText),
      sustain_("SUS", "", kGreen, true, false, percentText),
      release_("REL", "", kGreen, true, false, timeText),
      level_("LVL", "", kOrange, true, false, percentText)
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&mutate_, &bits_, &sub_, &grit_, &attack_, &decay_, &sustain_, &release_, &level_})
        addAndMakeVisible(*k);

    mutate_.setDoubleClickReturnValue(true, DNA_DEFAULT_MUTATE);
    bits_.setDoubleClickReturnValue(true, DNA_DEFAULT_BITS);
    sub_.setDoubleClickReturnValue(true, DNA_DEFAULT_SUB);
    grit_.setDoubleClickReturnValue(true, DNA_DEFAULT_GRIT);
    attack_.setDoubleClickReturnValue(true, dna_time_norm(DNA_DEFAULT_ATTACK));
    decay_.setDoubleClickReturnValue(true, dna_time_norm(DNA_DEFAULT_DECAY));
    sustain_.setDoubleClickReturnValue(true, DNA_DEFAULT_SUSTAIN);
    release_.setDoubleClickReturnValue(true, dna_time_norm(DNA_DEFAULT_RELEASE));
    level_.setDoubleClickReturnValue(true, DNA_DEFAULT_LEVEL);

    auto& s = processor_.apvts;
    mutateA_ = std::make_unique<Attachment>(s, "mutate", mutate_);
    bitsA_ = std::make_unique<Attachment>(s, "bits", bits_);
    subA_ = std::make_unique<Attachment>(s, "sub", sub_);
    gritA_ = std::make_unique<Attachment>(s, "grit", grit_);
    attackA_ = std::make_unique<Attachment>(s, "attack", attack_);
    decayA_ = std::make_unique<Attachment>(s, "decay", decay_);
    sustainA_ = std::make_unique<Attachment>(s, "sustain", sustain_);
    releaseA_ = std::make_unique<Attachment>(s, "release", release_);
    levelA_ = std::make_unique<Attachment>(s, "level", level_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

DnaAudioProcessorEditor::~DnaAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> DnaAudioProcessorEditor::helixRegion()
{
    return {kHelixX, 0.0f, kHelixW, kDesignH};
}

void DnaAudioProcessorEditor::showGlow(float level, int voices)
{
    hold_ = true;
    glow_ = juce::jlimit(0.0f, 1.0f, level);
    pulse_ = glow_;
    voices_ = voices;
    repaint();
}

void DnaAudioProcessorEditor::timerCallback()
{
    if (!hold_)
    {
        const auto act = processor_.takeActivity();
        const float hit = juce::jlimit(0.0f, 1.0f, act.peak * 1.6f);
        glow_ = hit > glow_ ? hit : glow_ * 0.94f;   // 0.94 per frame is about 270 ms to 1/e
        if (glow_ < 0.002f)
            glow_ = 0.0f;
        if (act.notes != lastNotes_)
        {
            lastNotes_ = act.notes;
            pulse_ = 1.0f;
        }
        pulse_ *= 0.945f;
        voices_ = act.voices;
        phase_ += juce::MathConstants<double>::twoPi / (3.1 * kTimerHz);
    }
    const float s = static_cast<float>(getWidth()) / kDesignW;
    repaint(helixRegion().transformedBy(juce::AffineTransform::scale(s)).getSmallestIntegerContainer());
}

void DnaAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;

    // The helix lights up with the output level, flashes on each new note and shimmers slowly. One glow blob per
    // sounding voice (up to 8), spread down the helix.
    if (glow_ > 0.004f || pulse_ > 0.004f)
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(helixRegion().transformedBy(juce::AffineTransform::scale(s)).toNearestInt());
        const int n = juce::jlimit(1, DNA_VOICES, voices_);
        for (int i = 0; i < n; ++i)
        {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
            const float y = (40.0f + 550.0f * t + 14.0f * static_cast<float>(std::sin(phase_ + i * 1.3))) * s;
            const float amount = 0.20f * glow_ + 0.18f * pulse_;
            const juce::Colour c = (i % 2 == 0 ? kBlue : kGreen).withAlpha(juce::jlimit(0.0f, 0.5f, amount));
            const juce::Point<float> centre(640.0f * s, y);
            const float rad = (70.0f + 55.0f * glow_) * s;
            juce::ColourGradient grad(c, centre, c.withAlpha(0.0f), centre.translated(rad, 0.0f), true);
            g.setGradientFill(grad);
            g.fillEllipse(centre.x - rad, centre.y - rad, rad * 2.0f, rad * 2.0f);
        }
    }

    g.addTransform(juce::AffineTransform::scale(s));

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("DNA", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("GENOME OSCILLATOR", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void DnaAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](DnaKnob& k, float x, float y, float w, float h) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, w * s, h * s).toNearestInt());
    };
    place(mutate_, 60.0f, 110.0f, kKnobW, kKnobH);
    place(bits_, 60.0f, 370.0f, kKnobW, kKnobH);
    place(sub_, 980.0f, 110.0f, kKnobW, kKnobH);
    place(grit_, 980.0f, 370.0f, kKnobW, kKnobH);

    // Envelope and level: a row either side of the helix, below the free middle of the plate.
    place(attack_, 340.0f, 500.0f, kMiniW, kMiniH);
    place(decay_, 430.0f, 500.0f, kMiniW, kMiniH);
    place(sustain_, 710.0f, 500.0f, kMiniW, kMiniH);
    place(release_, 800.0f, 500.0f, kMiniW, kMiniH);
    place(level_, 890.0f, 500.0f, kMiniW, kMiniH);
}
