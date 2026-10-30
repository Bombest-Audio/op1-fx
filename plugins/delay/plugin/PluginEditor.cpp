#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f;
constexpr float kKnobH = 250.0f;

const juce::Colour kBlue(0xff698eff);
const juce::Colour kGreen(0xff00ed95);
const juce::Colour kLavender(0xffdfd9ff);
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

// Where the ripple rings in the artwork are centred, in design pixels. The editor draws its own rings from here.
constexpr float kRippleX = 640.0f, kRippleY = 200.0f;
constexpr float kRippleMaxRadius = 300.0f;
constexpr int kTimerHz = 60;
// A ring lives about 1.6 s at full tape speed and longer when the tape is slow.
constexpr float kRingSecondsFast = 1.6f;
// Echo level that reads as a full-strength echo. The tape saturates at 1.0 and a typical echo sits well under it.
constexpr float kFullEchoLevel = 0.5f;

juce::String formatTime(double seconds)
{
    if (seconds >= 1.0)
        return juce::String(seconds, 2) + "s";
    return juce::String(juce::roundToInt(seconds * 1000.0));
}
} // namespace

DelayKnob::DelayKnob(juce::String label, juce::String caption, juce::Colour tone, std::function<juce::String()> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void DelayKnob::refreshReadout()
{
    if (format_() != shown_)
        repaint();
}

void DelayKnob::paint(juce::Graphics& g)
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

    shown_ = format_();
    g.setColour(kInk);
    g.setFont(condensed(76.0f, false));
    g.drawText(shown_, 0, 92, static_cast<int>(kKnobW), 76, juce::Justification::centred);

    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText(caption_, 0, 222, static_cast<int>(kKnobW), 18, juce::Justification::centred);
}

float DelayAudioProcessorEditor::valueOf(const char* id) const
{
    return processor_.apvts.getRawParameterValue(id)->load();
}

DelayAudioProcessorEditor::DelayAudioProcessorEditor(DelayAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      // The TIME readout is the real delay (loop length divided by tape speed), so it moves when SPEED moves.
      time_("TIME", "DELAY TIME", kBlue,
            [this] { return formatTime(static_cast<double>(delay_time_seconds(valueOf("time"), valueOf("speed")))); }),
      speed_("SPEED", "TAPE SPEED %", kGreen,
             [this] {
                 return juce::String(juce::roundToInt(delay_speed_ratio(valueOf("speed"), valueOf("time")) * 100.0f));
             }),
      fdb_("FDB", "FEEDBACK %", kLavender, [this] { return juce::String(juce::roundToInt(valueOf("fdb") * 100.0f)); }),
      input_("INPUT", "SEND TO TAPE %", kRed,
             [this] { return juce::String(juce::roundToInt(valueOf("input") * 100.0f)); })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&time_, &speed_, &fdb_, &input_})
    {
        addAndMakeVisible(*k);
        k->setRange(0.0, 1.0);
    }

    time_.setDoubleClickReturnValue(true, DELAY_DEFAULT_TIME);
    speed_.setDoubleClickReturnValue(true, DELAY_DEFAULT_SPEED);
    fdb_.setDoubleClickReturnValue(true, DELAY_DEFAULT_FDB);
    input_.setDoubleClickReturnValue(true, DELAY_DEFAULT_INPUT);

    timeA_ = std::make_unique<Attachment>(processor_.apvts, "time", time_);
    speedA_ = std::make_unique<Attachment>(processor_.apvts, "speed", speed_);
    fdbA_ = std::make_unique<Attachment>(processor_.apvts, "fdb", fdb_);
    inputA_ = std::make_unique<Attachment>(processor_.apvts, "input", input_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

DelayAudioProcessorEditor::~DelayAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> DelayAudioProcessorEditor::rippleRegion()
{
    return {kRippleX - kRippleMaxRadius, 0.0f, 2.0f * kRippleMaxRadius, kRippleY + kRippleMaxRadius};
}

void DelayAudioProcessorEditor::showEcho(float level)
{
    previewHold_ = true;
    level_ = juce::jlimit(0.0f, 1.0f, level);
    const float ages[] = {0.12f, 0.38f, 0.68f};
    for (size_t i = 0; i < 3; ++i)
        rings_[i] = {ages[i], level_};
    repaint();
}

void DelayAudioProcessorEditor::timerCallback()
{
    time_.refreshReadout();
    speed_.refreshReadout();

    if (!previewHold_)
    {
        const float echo = juce::jlimit(0.0f, 1.0f, processor_.takeActivity() / kFullEchoLevel);
        level_ = echo > level_ ? echo : level_ * 0.92f;
        if (level_ < 0.002f)
            level_ = 0.0f;

        // A new ring when the echo level jumps above its recent average, or every 0.7 s while it stays up.
        ++sinceSpawn_;
        const bool jump = echo > 0.08f && echo > slowLevel_ * 1.6f + 0.05f;
        const bool steady = echo > 0.08f && sinceSpawn_ > static_cast<int>(0.7f * kTimerHz);
        slowLevel_ += 0.06f * (echo - slowLevel_);
        if ((jump && sinceSpawn_ > 6) || steady)
        {
            sinceSpawn_ = 0;
            auto it = std::find_if(rings_.begin(), rings_.end(), [](const Ring& r) { return r.age >= 1.0f; });
            if (it == rings_.end())
                it = std::max_element(rings_.begin(), rings_.end(),
                                      [](const Ring& a, const Ring& b) { return a.age < b.age; });
            *it = {0.0f, echo};
        }

        // Slower tape, slower ripples.
        const float speed = delay_speed_ratio(valueOf("speed"), valueOf("time"));
        const float seconds = kRingSecondsFast * (1.0f + 1.5f * (1.0f - speed));
        for (auto& r : rings_)
            if (r.age < 1.0f)
                r.age = juce::jmin(1.0f, r.age + 1.0f / (seconds * kTimerHz));
    }

    repaint(rippleRegion().transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW))
                .getSmallestIntegerContainer());
}

void DelayAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    {
        // The artwork already carries a few static ripples. These are the live ones: a soft glow in the middle that
        // follows the echo level, and rings that expand from it, one per echo.
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(rippleRegion().toNearestInt());
        const juce::Point<float> c(kRippleX, kRippleY);
        if (level_ > 0.01f)
        {
            juce::ColourGradient glow(kBlue.withAlpha(0.20f * level_), c, kBlue.withAlpha(0.0f),
                                      c.translated(110.0f, 0.0f), true);
            g.setGradientFill(glow);
            g.fillEllipse(c.x - 110.0f, c.y - 110.0f, 220.0f, 220.0f);
        }
        for (const auto& r : rings_)
        {
            if (r.age >= 1.0f)
                continue;
            const float radius = 14.0f + (kRippleMaxRadius - 14.0f) * (1.0f - std::pow(1.0f - r.age, 2.0f));
            const float fade = (1.0f - r.age) * (1.0f - r.age);
            g.setColour(kBlue.withAlpha(0.45f * juce::jlimit(0.15f, 1.0f, r.level) * fade));
            g.drawEllipse(c.x - radius, c.y - radius, 2.0f * radius, 2.0f * radius, 2.0f);
        }
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("DELAY", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("TAPE ECHO", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void DelayAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](DelayKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kKnobW * s, kKnobH * s).toNearestInt());
    };
    place(time_, 60.0f, 110.0f);
    place(speed_, 60.0f, 370.0f);
    place(fdb_, 980.0f, 110.0f);
    place(input_, 980.0f, 370.0f);
}
