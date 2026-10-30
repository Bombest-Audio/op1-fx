#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f;
constexpr float kKnobH = 250.0f;

const juce::Colour kBlue(0xff698eff);
const juce::Colour kMint(0xff00ed95);
const juce::Colour kLilac(0xffdfd9ff);
const juce::Colour kRed(0xffff3a5d);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

constexpr float kArtX = 330.0f, kArtW = 620.0f; // the bottle panel inside the 1280x630 faceplate
constexpr juce::Point<float> kValve{610.0f, 108.0f}; // where the brass valve sits in the faceplate art
constexpr juce::Point<float> kMist{610.0f, 540.0f};  // the mist at the foot of the bottle
constexpr int kTimerHz = 60;
} // namespace

NitroKnob::NitroKnob(juce::String label, juce::String caption, juce::Colour tone, float readoutSize,
                     std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), readoutSize_(readoutSize),
      format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void NitroKnob::paint(juce::Graphics& g)
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

    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.0f, a0, a1, true);
    g.setColour(kTrack);
    g.strokePath(track, juce::PathStrokeType(6.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (prop > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0.0f, a0, aV, true);
        g.setColour(tone_);
        g.strokePath(arc, juce::PathStrokeType(6.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float tx = cx + std::sin(aV) * r;
    const float ty = cy - std::cos(aV) * r;
    g.setColour(tone_);
    g.fillEllipse(tx - 7.0f, ty - 7.0f, 14.0f, 14.0f);

    g.setColour(kInk);
    g.setFont(condensed(readoutSize_, false));
    const int ry = juce::roundToInt(130.0f - readoutSize_ * 0.5f);
    g.drawText(format_(getValue()), 0, ry, static_cast<int>(kKnobW), juce::roundToInt(readoutSize_),
               juce::Justification::centred);

    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText(caption_, 0, 222, static_cast<int>(kKnobW), 18, juce::Justification::centred);
}

NitroAudioProcessorEditor::NitroAudioProcessorEditor(NitroAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      freq_("FREQ", "CUTOFF HZ", kBlue, 76.0f,
            [](double v) {
                const float hz = nitro_freq_hz(static_cast<float>(v));
                return hz >= 1000.0f ? juce::String(hz / 1000.0f, 1) + "k" : juce::String(juce::roundToInt(hz));
            }),
      res_("RES", "FEEDBACK", kMint, 76.0f, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      shape_("SHAPE", "LOW PASS TO HIGH PASS", kLilac, 44.0f,
             [](double v) { return juce::String(nitro_shape_name(static_cast<float>(v))); }),
      drive_("DRIVE", "GAIN INTO THE FILTER", kRed, 76.0f,
             [](double v) { return juce::String(juce::roundToInt(v * 100.0)); })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&freq_, &res_, &shape_, &drive_})
    {
        k->setRange(0.0, 1.0);
        addAndMakeVisible(*k);
    }

    freq_.setDoubleClickReturnValue(true, NITRO_DEFAULT_FREQ);
    res_.setDoubleClickReturnValue(true, NITRO_DEFAULT_RES);
    shape_.setDoubleClickReturnValue(true, NITRO_DEFAULT_SHAPE);
    drive_.setDoubleClickReturnValue(true, NITRO_DEFAULT_DRIVE);

    freqA_ = std::make_unique<Attachment>(processor_.apvts, "freq", freq_);
    resA_ = std::make_unique<Attachment>(processor_.apvts, "res", res_);
    shapeA_ = std::make_unique<Attachment>(processor_.apvts, "shape", shape_);
    driveA_ = std::make_unique<Attachment>(processor_.apvts, "drive", drive_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

NitroAudioProcessorEditor::~NitroAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> NitroAudioProcessorEditor::artRegion()
{
    return {kArtX, 0.0f, kArtW, kDesignH};
}

void NitroAudioProcessorEditor::showLevel(float level)
{
    hold_ = true;
    level_ = juce::jlimit(0.0f, 1.0f, level);
    repaint();
}

void NitroAudioProcessorEditor::timerCallback()
{
    if (!hold_)
    {
        const float in = juce::jlimit(0.0f, 1.0f, processor_.takeActivity());
        // Rise at once, fall exponentially: 0.95 per frame is about 330 ms to 1/e at 60 Hz.
        level_ = in > level_ ? in : level_ * 0.95f;
        if (level_ < 0.002f)
            level_ = 0.0f;
        phase_ += juce::MathConstants<double>::twoPi / (4.0 * kTimerHz);
    }
    repaint(artRegion().transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW))
                .getSmallestIntegerContainer());
}

void NitroAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;

    {
        // Restrained signal animation: the valve glows red-pink and the mist at the foot warms toward violet as the
        // input gets louder. A very slow drift keeps it from looking frozen when idle. Everything stays under 0.4 alpha.
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(artRegion().transformedBy(juce::AffineTransform::scale(s)).toNearestInt());
        const float drift = 0.5f + 0.5f * static_cast<float>(std::sin(phase_));
        const float valve = 0.04f * drift + 0.34f * level_;
        const auto vc = juce::Point<float>(kValve.x * s, kValve.y * s);
        g.setGradientFill(juce::ColourGradient(kRed.withAlpha(valve), vc, kRed.withAlpha(0.0f),
                                               vc.translated(110.0f * s, 0.0f), true));
        g.fillEllipse(vc.x - 110.0f * s, vc.y - 110.0f * s, 220.0f * s, 220.0f * s);

        const float mist = 0.03f * (1.0f - drift) + 0.22f * level_;
        const auto mc = juce::Point<float>(kMist.x * s, kMist.y * s);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffa77bff).withAlpha(mist), mc,
                                               juce::Colour(0xffa77bff).withAlpha(0.0f),
                                               mc.translated(170.0f * s, 0.0f), true));
        g.fillEllipse(mc.x - 170.0f * s, mc.y - 170.0f * s, 340.0f * s, 340.0f * s);
    }

    g.addTransform(juce::AffineTransform::scale(s));

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("NITRO", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("DRIVE FILTER", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void NitroAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](NitroKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kKnobW * s, kKnobH * s).toNearestInt());
    };
    place(freq_, 60.0f, 110.0f);
    place(res_, 60.0f, 370.0f);
    place(shape_, 980.0f, 110.0f);
    place(drive_, 980.0f, 370.0f);
}
