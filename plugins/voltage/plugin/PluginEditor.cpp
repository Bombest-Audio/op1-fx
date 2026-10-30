#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f, kKnobH = 250.0f;
constexpr float kSmallW = 110.0f, kSmallH = 120.0f;

const juce::Colour kBlue(0xff698eff);
const juce::Colour kGreen(0xff00ed95);
const juce::Colour kWhite(0xffe9ece8);
const juce::Colour kOrange(0xffff9a3a);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);
constexpr int kTimerHz = 60;
// The two copper terminals in the 1280x630 faceplate; the arc jumps between them.
constexpr float kTermLX = 500.0f, kTermRX = 780.0f, kTermY = 122.0f;

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

juce::String envText(double v)
{
    return v < 1.0 ? juce::String(juce::roundToInt(v * 1000.0)) + "ms" : juce::String(v, 1) + "s";
}
} // namespace

VoltageKnob::VoltageKnob(juce::String label, juce::String caption, juce::Colour tone,
                         std::function<juce::String(double)> format, bool small)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), format_(std::move(format)), small_(small)
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void VoltageKnob::paint(juce::Graphics& g)
{
    const float w = small_ ? kSmallW : kKnobW, h = small_ ? kSmallH : kKnobH;
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / w, static_cast<float>(getHeight()) / h));
    const float k = small_ ? 0.42f : 1.0f;

    g.setColour(tone_);
    g.setFont(condensed((small_ ? 18.0f : 36.0f), true).withExtraKerningFactor(0.2f));
    g.drawText(label_, 0, 0, static_cast<int>(w), small_ ? 20 : 36, juce::Justification::centred);

    const float cx = w * 0.5f, cy = small_ ? 56.0f : 130.0f, r = 74.0f * k;
    const float a0 = juce::degreesToRadians(-135.0f), a1 = juce::degreesToRadians(135.0f);
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float aV = a0 + (a1 - a0) * prop;
    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.0f, a0, a1, true);
    g.setColour(kTrack);
    g.strokePath(track, juce::PathStrokeType(6.2f * (small_ ? 0.7f : 1.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (prop > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0.0f, a0, aV, true);
        g.setColour(tone_);
        g.strokePath(arc, juce::PathStrokeType(6.2f * (small_ ? 0.7f : 1.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float tx = cx + std::sin(aV) * r, ty = cy - std::cos(aV) * r, dr = 7.0f * (small_ ? 0.7f : 1.0f);
    g.setColour(tone_);
    g.fillEllipse(tx - dr, ty - dr, dr * 2, dr * 2);

    g.setColour(kInk);
    g.setFont(condensed(small_ ? 26.0f : 76.0f, false));
    g.drawText(format_(getValue()), 0, small_ ? 40 : 92, static_cast<int>(w), small_ ? 32 : 76, juce::Justification::centred);
    if (!small_)
    {
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
        g.drawText(caption_, 0, 222, static_cast<int>(w), 18, juce::Justification::centred);
    }
}

VoltageAudioProcessorEditor::VoltageAudioProcessorEditor(VoltageAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      xor_("XOR", "XORED RECTIFIED SINES", kBlue, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      fb_("FEEDBACK", "SINE INTO ITSELF", kGreen, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      filter_("FILTER", "LOW-PASS HZ", kWhite,
              [](double v) {
                  const double hz = opvl_filter_hz(static_cast<float>(v));
                  return hz >= 1000.0 ? juce::String(hz / 1000.0, 1) + "k" : juce::String(juce::roundToInt(hz));
              }),
      detune_("DETUNE", "OUTER OSCILLATORS, % OF PITCH", kOrange,
              [](double v) { return juce::String(opvl_detune_percent(static_cast<float>(v)), 1); }),
      attack_("A", "", kMuted, [](double v) { return envText(voltagetime::toSeconds(static_cast<float>(v), voltagetime::kAttackLo, voltagetime::kAttackHi)); }, true),
      decay_("D", "", kMuted, [](double v) { return envText(voltagetime::toSeconds(static_cast<float>(v), voltagetime::kLongLo, voltagetime::kLongHi)); }, true),
      sustain_("S", "", kMuted, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }, true),
      release_("R", "", kMuted, [](double v) { return envText(voltagetime::toSeconds(static_cast<float>(v), voltagetime::kLongLo, voltagetime::kLongHi)); }, true),
      level_("LEVEL", "", kInk, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }, true)
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png, BinaryData::faceplatebg1280x630_pngSize);
    VoltageKnob* knobs[9] = {&xor_, &fb_, &filter_, &detune_, &attack_, &decay_, &sustain_, &release_, &level_};
    const char* ids[9] = {"xor", "feedback", "filter", "detune", "attack", "decay", "sustain", "release", "level"};
    const double defs[9] = {OPVL_DEFAULT_XOR, OPVL_DEFAULT_FEEDBACK, OPVL_DEFAULT_FILTER, OPVL_DEFAULT_DETUNE,
                            voltagetime::toNorm(OPVL_DEFAULT_ATTACK, voltagetime::kAttackLo, voltagetime::kAttackHi),
                            voltagetime::toNorm(OPVL_DEFAULT_ENV_DECAY, voltagetime::kLongLo, voltagetime::kLongHi),
                            OPVL_DEFAULT_SUSTAIN,
                            voltagetime::toNorm(OPVL_DEFAULT_RELEASE, voltagetime::kLongLo, voltagetime::kLongHi),
                            OPVL_DEFAULT_LEVEL};
    for (int i = 0; i < 9; i++)
    {
        addAndMakeVisible(*knobs[i]);
        if (auto* param = processor_.apvts.getParameter(ids[i]))
            knobs[i]->setNormalisableRange(juce::NormalisableRange<double>(
                static_cast<double>(param->getNormalisableRange().start), static_cast<double>(param->getNormalisableRange().end), 0.0,
                static_cast<double>(param->getNormalisableRange().skew)));
        knobs[i]->setDoubleClickReturnValue(true, defs[i]);
        a_[i] = std::make_unique<Attachment>(processor_.apvts, ids[i], *knobs[i]);
    }
    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);
    startTimerHz(kTimerHz);
}

VoltageAudioProcessorEditor::~VoltageAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> VoltageAudioProcessorEditor::sparkRegion()
{
    return {kTermLX - 60.0f, 20.0f, kTermRX - kTermLX + 120.0f, 180.0f};
}

void VoltageAudioProcessorEditor::showLevel(float level)
{
    hold_ = true;
    glow_ = juce::jlimit(0.0f, 1.0f, level);
    repaint();
}

void VoltageAudioProcessorEditor::timerCallback()
{
    if (!hold_)
    {
        // The arc follows the output level: rises at once, falls over about 200 ms.
        const float hit = juce::jlimit(0.0f, 1.0f, processor_.takeLevel() * 2.0f);
        glow_ = hit > glow_ ? hit : glow_ * 0.92f;
        if (glow_ < 0.002f)
            glow_ = 0.0f;
        phase_ += 1.0;
        seed_ = static_cast<int>(phase_ / 3.0);
    }
    repaint(sparkRegion().transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW)).getSmallestIntegerContainer());
}

void VoltageAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    if (glow_ > 0.01f)
    {
        // A restrained blue halo and a jagged arc between the terminals, redrawn every third frame.
        const juce::Point<float> mid((kTermLX + kTermRX) * 0.5f, kTermY - 15.0f);
        juce::ColourGradient halo(kBlue.withAlpha(0.28f * glow_), mid, kBlue.withAlpha(0.0f), mid.translated(190.0f, 0.0f), true);
        g.setGradientFill(halo);
        g.fillEllipse(mid.x - 190.0f, mid.y - 110.0f, 380.0f, 220.0f);
        juce::Random rnd(seed_ * 7919 + 13);
        juce::Path arc;
        arc.startNewSubPath(kTermLX + 8.0f, kTermY);
        const int segs = 14;
        for (int i = 1; i < segs; i++)
        {
            const float t = static_cast<float>(i) / segs;
            const float x = kTermLX + 8.0f + (kTermRX - kTermLX - 16.0f) * t;
            const float bow = std::sin(t * juce::MathConstants<float>::pi) * -22.0f * glow_;
            arc.lineTo(x, kTermY + bow + (rnd.nextFloat() - 0.5f) * 26.0f * glow_);
        }
        arc.lineTo(kTermRX - 8.0f, kTermY);
        g.setColour(kBlue.withAlpha(0.35f * glow_));
        g.strokePath(arc, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved));
        g.setColour(juce::Colour(0xffcfdcff).withAlpha(0.85f * glow_));
        g.strokePath(arc, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved));
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("VOLTAGE", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("XOR OSCILLATOR", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void VoltageAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](VoltageKnob& k, float x, float y, float w, float h) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, w * s, h * s).toNearestInt());
    };
    place(xor_, 60.0f, 110.0f, kKnobW, kKnobH);
    place(fb_, 60.0f, 370.0f, kKnobW, kKnobH);
    place(filter_, 980.0f, 110.0f, kKnobW, kKnobH);
    place(detune_, 980.0f, 370.0f, kKnobW, kKnobH);
    const float y = 500.0f, x0 = 360.0f, dx = 110.0f;
    place(attack_, x0, y, kSmallW, kSmallH);
    place(decay_, x0 + dx, y, kSmallW, kSmallH);
    place(sustain_, x0 + 2 * dx, y, kSmallW, kSmallH);
    place(release_, x0 + 3 * dx, y, kSmallW, kSmallH);
    place(level_, x0 + 4 * dx + 20.0f, y, kSmallW, kSmallH);
}
