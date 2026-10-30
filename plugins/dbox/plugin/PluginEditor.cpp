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

// Pad grid under the drum box, 6 columns by 4 rows, key 0 top left. Same shape as the key grid on the OP-1 screen.
constexpr float kPadX = 488.0f, kPadY = 285.0f, kPad = 44.0f, kGap = 8.0f;
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

juce::String decayReadout(double v)
{
    const auto a = dbox_attack_seconds(static_cast<float>(v));
    return a > 0.0f ? juce::String(juce::roundToInt(a * 1000.0f)) : juce::String(juce::roundToInt(dbox_decay_seconds(static_cast<float>(v)) * 1000.0f));
}

juce::String decayCaption(double v)
{
    return dbox_attack_seconds(static_cast<float>(v)) > 0.0f ? "SLOW ATTACK, MS" : "DECAY, MS";
}
} // namespace

DBoxKnob::DBoxKnob(juce::String label, juce::Colour tone, bool small, std::function<juce::String(double)> format,
                   std::function<juce::String(double)> caption)
    : label_(std::move(label)), tone_(tone), small_(small), format_(std::move(format)), caption_(std::move(caption))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void DBoxKnob::paint(juce::Graphics& g)
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

    // Bipolar knobs (PITCH) draw the arc from the centre; the rest from the minimum.
    const float aStart = getMinimum() < 0.0 ? 0.0f : a0;
    if (std::abs(aV - aStart) > 0.004f)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0.0f, juce::jmin(aStart, aV), juce::jmax(aStart, aV), true);
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
        g.drawText(caption_ ? caption_(getValue()) : juce::String(), 0, 222, static_cast<int>(dw), 18, juce::Justification::centred);
    }
}

DBoxAudioProcessorEditor::DBoxAudioProcessorEditor(DBoxAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      pitch_("PITCH", kBlue, false,
             [](double v) { return (v >= 0 ? "+" : "") + juce::String(dbox_pitch_semitones(static_cast<float>(v)), 1); },
             [](double) { return juce::String("SEMITONES"); }),
      wave_("WAVE", kGreen, false, [](double v) { return pct(v); }, [](double) { return juce::String("SINE > SQUARE > SAW"); }),
      decay_("DECAY", kWhite, false, decayReadout, decayCaption),
      mode_("MODE", kOrange, false, [](double v) { return juce::String(juce::roundToInt(v)); },
            [](double v) { return juce::String(dbox_mode_name(juce::roundToInt(v))); }),
      mix2_("2 MIX", kMuted, true, [](double v) { return pct(v); }),
      wave2_("2 WAVE", kMuted, true, [](double v) { return pct(v); }),
      decay2_("2 DECAY", kMuted, true, decayReadout),
      filter_("FILTER", kMuted, true,
              [](double v) {
                  const double hz = dbox_filter_hz(static_cast<float>(v));
                  return hz >= 1000.0 ? juce::String(hz / 1000.0, 1) + "k" : juce::String(juce::roundToInt(hz));
              }),
      atk_("ATK", kMuted, true, secs),
      dec_("DEC", kMuted, true, secs),
      sus_("SUS", kMuted, true, pct),
      rel_("REL", kMuted, true, secs),
      level_("LEVEL", kMuted, true, pct),
      bend_("BEND", kMuted, true, [](double v) { return juce::String(juce::roundToInt(v)) + " st"; })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    DBoxKnob* knobs[14] = {&pitch_, &wave_, &decay_, &mode_, &mix2_, &wave2_, &decay2_, &filter_, &atk_, &dec_, &sus_, &rel_, &level_, &bend_};
    const char* ids[14] = {"pitch", "wave", "decay", "mode", "mix2", "wave2", "decay2", "filter", "env_atk", "env_dec", "env_sus", "env_rel", "level", "bend"};
    for (int i = 0; i < 14; ++i)
    {
        addAndMakeVisible(*knobs[i]);
        a_[i] = std::make_unique<Attachment>(processor_.apvts, ids[i], *knobs[i]);
    }
    pitch_.setDoubleClickReturnValue(true, 0.0);
    wave_.setDoubleClickReturnValue(true, DBOX_DEFAULT_WAVE);
    decay_.setDoubleClickReturnValue(true, DBOX_DEFAULT_DECAY);
    mode_.setDoubleClickReturnValue(true, DBOX_DEFAULT_MODE);
    mix2_.setDoubleClickReturnValue(true, DBOX_DEFAULT_MIX2);
    wave2_.setDoubleClickReturnValue(true, DBOX_DEFAULT_WAVE2);
    decay2_.setDoubleClickReturnValue(true, DBOX_DEFAULT_DECAY2);
    filter_.setDoubleClickReturnValue(true, DBOX_DEFAULT_FILTER);
    atk_.setDoubleClickReturnValue(true, DBOX_DEFAULT_ATTACK);
    dec_.setDoubleClickReturnValue(true, DBOX_DEFAULT_ENV_DECAY);
    sus_.setDoubleClickReturnValue(true, DBOX_DEFAULT_SUSTAIN);
    rel_.setDoubleClickReturnValue(true, DBOX_DEFAULT_RELEASE);
    level_.setDoubleClickReturnValue(true, DBOX_DEFAULT_LEVEL);
    bend_.setDoubleClickReturnValue(true, 2.0);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

DBoxAudioProcessorEditor::~DBoxAudioProcessorEditor() { stopTimer(); }

void DBoxAudioProcessorEditor::showPads(unsigned mask)
{
    previewHold_ = true;
    for (int k = 0; k < DBOX_KEYS; ++k)
        lit_[k] = (mask >> k) & 1u ? 1.0f : 0.0f;
    energy_ = mask ? 0.6f : 0.0f;
    repaint();
}

void DBoxAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        const auto a = processor_.takeActivity();
        for (int k = 0; k < DBOX_KEYS; ++k)
        {
            // Rise at once on a hit, fall exponentially: 0.93 per frame is about 230 ms to 1/e.
            lit_[k] = (a.keys >> k) & 1u ? 1.0f : lit_[k] * 0.93f;
            if (lit_[k] < 0.004f)
                lit_[k] = 0.0f;
        }
        const float level = juce::jlimit(0.0f, 1.0f, a.peak * 1.5f);
        energy_ = level > energy_ ? level : energy_ * 0.92f;
        if (energy_ < 0.002f)
            energy_ = 0.0f;
    }
    const float s = static_cast<float>(getWidth()) / kDesignW;
    repaint(juce::Rectangle<float>(330.0f, 0.0f, 620.0f, 500.0f).transformedBy(juce::AffineTransform::scale(s)).getSmallestIntegerContainer());
}

void DBoxAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    // A soft glow on the drum head follows the output level. Kept faint.
    if (energy_ > 0.01f)
    {
        const juce::Point<float> c(640.0f, 62.0f);
        juce::ColourGradient grad(kOrange.withAlpha(0.20f * energy_), c, kOrange.withAlpha(0.0f), c.translated(110.0f, 0.0f), true);
        g.setGradientFill(grad);
        g.fillEllipse(c.x - 110.0f, c.y - 110.0f, 220.0f, 220.0f);
    }

    for (int k = 0; k < DBOX_KEYS; ++k)
    {
        const int col = k % 6, row = k / 6;
        const juce::Rectangle<float> r(kPadX + col * (kPad + kGap), kPadY + row * (kPad + kGap), kPad, kPad);
        g.setColour(kTrack.interpolatedWith(kOrange, lit_[k] * 0.9f));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(kMuted.withAlpha(0.5f + 0.5f * lit_[k]));
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("DBOX", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("24 KEY DRUM KIT", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void DBoxAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto big = [s](DBoxKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kBigW * s, kBigH * s).toNearestInt());
    };
    auto small = [s](DBoxKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kSmallW * s, kSmallH * s).toNearestInt());
    };
    big(pitch_, 60.0f, 110.0f);
    big(wave_, 60.0f, 370.0f);
    big(decay_, 980.0f, 110.0f);
    big(mode_, 980.0f, 370.0f);
    small(mix2_, 335.0f, 100.0f);
    small(wave2_, 335.0f, 220.0f);
    small(decay2_, 335.0f, 340.0f);
    small(filter_, 795.0f, 100.0f);
    small(level_, 795.0f, 220.0f);
    small(bend_, 795.0f, 340.0f);
    small(atk_, 335.0f, 495.0f);
    small(dec_, 495.0f, 495.0f);
    small(sus_, 655.0f, 495.0f);
    small(rel_, 815.0f, 495.0f);
}
