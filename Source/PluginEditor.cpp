#include "PluginEditor.h"
#include "BinaryData.h"

namespace swell::bass
{
static constexpr int W = 1000, H = 660;
static juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 4); }

// ===== Look ======================================================================================
Look::Look()
{
    bebas = juce::Typeface::createSystemTypefaceFor (BinaryData::BebasNeueRegular_ttf, BinaryData::BebasNeueRegular_ttfSize);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, col::line);
    setColour (juce::TextButton::textColourOffId, col::muted);
    setColour (juce::TextButton::textColourOnId, col::ink);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (5).withSizeKeepingCentre (juce::jmin (w, h) - 10.f, juce::jmin (w, h) - 10.f);
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;
    // anneau de valeur
    juce::Path track, val;
    track.addCentredArc (c.x, c.y, rad + 3, rad + 3, 0, a0, a1, true);
    g.setColour (col::line);
    g.strokePath (track, juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float ang = a0 + pos * (a1 - a0);
    val.addCentredArc (c.x, c.y, rad + 3, rad + 3, 0, a0, ang, true);
    g.setColour (accent);
    g.strokePath (val, juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // corps
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2a44), c.x - rad * 0.3f, c.y - rad * 0.4f, juce::Colour (0xff100e1a), c.x + rad, c.y + rad, true));
    g.fillEllipse (r.reduced (2));
    g.setColour (col::line);
    g.drawEllipse (r.reduced (2), 1.f);
    // aiguille lumineuse
    juce::Path needle;
    needle.addRoundedRectangle (-1.5f, -rad + 6, 3.f, rad * 0.5f, 1.5f);
    needle.applyTransform (juce::AffineTransform::rotation (ang).translated (c));
    g.setColour (accent.withAlpha (0.35f));
    g.strokePath (needle, juce::PathStrokeType (4.f));
    g.setColour (accent);
    g.fillPath (needle);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (down ? accent.withAlpha (0.25f) : on ? accent.withAlpha (0.14f) : juce::Colour (0x66000000));
    g.fillRoundedRectangle (r, 6.f);
    g.setColour (on || over ? accent : col::line);
    g.drawRoundedRectangle (r, 6.f, 1.f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool)
{
    g.setColour (b.getToggleState() || over ? accent : col::muted);
    g.setFont (juce::FontOptions (b.getHeight() > 30 ? 17.f : 12.f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

// ===== Knob ======================================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& l)
    : label (l), attach (s, id, slider), param (s.getParameter (id))
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (-135.f), juce::degreesToRadians (135.f), true);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Knob::resized() { slider.setBounds (getLocalBounds().removeFromTop (getHeight() - 30)); }

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().removeFromBottom (30);
    g.setColour (col::ink);
    g.setFont (juce::FontOptions (12.f, juce::Font::bold));
    g.drawText (param->getCurrentValueAsText(), r.removeFromTop (15), juce::Justification::centred);
    g.setColour (col::muted);
    g.setFont (juce::FontOptions (11.f));
    g.drawText (label, r, juce::Justification::centred);
}

// ===== Liste des samples =========================================================================
SampleList::SampleList (Processor& p) : proc (p)
{
    list.setModel (this);
    list.setRowHeight (28);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);
}

void SampleList::refresh()
{
    list.updateContent();
    list.selectRow (proc.sampleIndex(), true, true);
    list.repaint();
}

int SampleList::getNumRows() { return proc.bank.entries (proc.category()).size(); }

void SampleList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto& l = proc.bank.entries (proc.category());
    if (! juce::isPositiveAndBelow (row, l.size())) return;
    const auto& e = l.getReference (row);
    const auto acc = col::accent (proc.category());
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (2, 2).toFloat();
    if (selected) { g.setColour (acc.withAlpha (0.18f)); g.fillRoundedRectangle (r, 6.f); }
    g.setColour (selected ? acc : col::ink);
    g.setFont (juce::FontOptions (13.f));
    g.drawText (e.name, r.reduced (9, 0).toNearestInt(), juce::Justification::centredLeft);
    g.setColour (col::muted);
    g.setFont (juce::FontOptions (10.f));
    g.drawText ((e.user ? juce::String ("PERSO  ") : juce::String()) + noteName (e.rootNote), r.reduced (9, 0).toNearestInt(), juce::Justification::centredRight);
}

void SampleList::listBoxItemClicked (int row, const juce::MouseEvent&) { proc.selectSample (proc.category(), row); }

// ===== Forme d'onde ==============================================================================
void WaveView::update()
{
    auto s = proc.displayedSample();
    if (s == shown && shownWidth == getWidth()) return;
    shown = s;
    shownWidth = getWidth();
    wave.clear();
    if (s == nullptr || getWidth() <= 0) return;
    const int n = s->audio.getNumSamples(), w = getWidth();
    const float mid = getHeight() * 0.5f, amp = getHeight() * 0.45f;
    const auto* d = s->audio.getReadPointer (0);
    float peak = 1.0e-3f;
    for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (d[i]));
    juce::Array<float> hi, lo;
    for (int x = 0; x < w; ++x)
    {
        const int a = (int) ((juce::int64) x * n / w), b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * n / w));
        float mn = 0, mx = 0;
        for (int i = a; i < b && i < n; ++i) { mn = juce::jmin (mn, d[i]); mx = juce::jmax (mx, d[i]); }
        hi.add (mid - mx / peak * amp);
        lo.add (mid - mn / peak * amp);
    }
    wave.startNewSubPath (0, hi[0]);
    for (int x = 1; x < w; ++x) wave.lineTo ((float) x, hi[x]);
    for (int x = w - 1; x >= 0; --x) wave.lineTo ((float) x, lo[x]);
    wave.closeSubPath();
    repaint();
}

void WaveView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff06050a));
    g.fillRoundedRectangle (r, 8.f);
    g.setColour (juce::Colour (0xff1c1930));
    for (float x = 0; x < r.getWidth(); x += 40) g.drawVerticalLine ((int) x, 0, r.getHeight());
    g.drawHorizontalLine ((int) r.getCentreY(), 0, r.getWidth());
    const auto acc = col::accent (proc.category());
    if (wave.isEmpty())
    {
        g.setColour (col::muted);
        g.drawText ("Aucun sample", getLocalBounds(), juce::Justification::centred);
        return;
    }
    g.setColour (acc.withAlpha (0.18f));
    g.fillPath (wave);
    g.setColour (acc);
    g.strokePath (wave, juce::PathStrokeType (1.2f));
    if (shown != nullptr)
    {
        g.setColour (col::muted);
        g.setFont (juce::FontOptions (10.f));
        g.drawText (juce::String (shown->audio.getNumSamples() / shown->sampleRate, 2) + " s", getLocalBounds().reduced (8, 6), juce::Justification::topRight);
    }
}

// ===== Réglages ==================================================================================
Settings::Settings (Processor& p) : proc (p)
{
    for (auto* b : { &close, &deactivate, &change, &open, &rescan }) addAndMakeVisible (b);
    for (auto z : { 75, 100, 125, 150 })
    {
        auto* b = zooms.add (new juce::TextButton (juce::String (z) + " %"));
        b->onClick = [this, z] { zoom = z / 100.f; refresh(); if (onZoom) onZoom (zoom); };
        addAndMakeVisible (b);
    }
    close.onClick = [this] { setVisible (false); };
    deactivate.onClick = [this] {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, juce::String::fromUTF8 ("Désactiver la licence"),
            juce::String::fromUTF8 ("Swell Bass sera désactivé sur cet ordinateur et sa place libérée sur ton compte."), juce::String::fromUTF8 ("Désactiver"), "Annuler", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) licence->deactivate(); }));
    };
    change.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("Dossier « Mes samples »"), Bank::userFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().isDirectory()) { Bank::setUserFolder (fc.getResult()); proc.rescanBank(); if (onBankChanged) onBankChanged(); repaint(); }
        });
    };
    open.onClick = [] { Bank::userFolder().createDirectory(); Bank::userFolder().startAsProcess(); };
    rescan.onClick = [this] { proc.rescanBank(); if (onBankChanged) onBankChanged(); repaint(); };
    licence->addChangeListener (this);
    refresh();
}

Settings::~Settings() { licence->removeChangeListener (this); }

void Settings::refresh()
{
    for (int i = 0; i < zooms.size(); ++i) zooms[i]->setToggleState (std::abs ((75 + 25 * i) / 100.f - zoom) < 0.01f, juce::dontSendNotification);
    deactivate.setVisible (licence->info().key.isNotEmpty());
    repaint();
}

juce::Rectangle<int> Settings::card() const { return getLocalBounds().withSizeKeepingCentre (560, 520); }

void Settings::mouseDown (const juce::MouseEvent& e) { if (! card().contains (e.getPosition())) setVisible (false); }

static void row (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& a, const juce::String& b, juce::Colour bc = col::muted, int buttons = 110)
{
    g.setColour (col::box);
    g.fillRoundedRectangle (r.toFloat(), 8.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.f, 1.f);
    r.reduce (12, 0);
    g.setColour (col::ink);
    g.setFont (juce::FontOptions (13.f));
    g.drawText (a, r, juce::Justification::centredLeft);
    g.setColour (bc);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (b, r.withTrimmedLeft (130).withTrimmedRight (buttons), juce::Justification::centredLeft, true);
}

void Settings::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xd005040a));
    const auto c = card();
    g.setColour (col::panel);
    g.fillRoundedRectangle (c.toFloat(), 14.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 14.f, 1.f);
    auto& lk = dynamic_cast<Look&> (getLookAndFeel());
    auto r = c.reduced (24, 20);
    g.setColour (col::ink);
    g.setFont (lk.display (30.f));
    g.drawText (juce::String::fromUTF8 ("RÉGLAGES"), r.removeFromTop (34), juce::Justification::centredLeft);

    auto section = [&] (const juce::String& t) {
        r.removeFromTop (14);
        g.setColour (col::muted);
        g.setFont (juce::FontOptions (10.f, juce::Font::bold));
        g.drawText (t, r.removeFromTop (18), juce::Justification::centredLeft);
    };
    const auto info = licence->info();
    section ("LICENCE");
    if (licence->isUnlocked())
    {
        row (g, r.removeFromTop (34), "Statut", info.isTrial() ? "Essai : " + juce::String (licence->trialDaysLeft()) + " jours restants" : juce::String::fromUTF8 ("\xe2\x97\x8f Activée"), juce::Colour (0xff4ade80));
        r.removeFromTop (6);
        auto owner = r.removeFromTop (34);
        r.removeFromTop (6);
        auto keyRow = r.removeFromTop (34);
        if (info.key.isNotEmpty())
        {
            row (g, owner, juce::String::fromUTF8 ("Propriétaire"), info.name.isNotEmpty() ? info.name + "  " + info.email : info.email);
            row (g, keyRow, juce::String::fromUTF8 ("Clé"), info.key.substring (0, 11) + "-XXXX-XXXX");
        }
    }
    else
    {
        row (g, r.removeFromTop (34), "Statut", juce::String::fromUTF8 ("Non activée"), juce::Colour (0xffff4d6d));
        r.removeFromTop (80);
    }
    int factory = 0, user = 0;
    for (int i = 0; i < numCategories; ++i)
        for (auto& e : proc.bank.entries (i)) (e.user ? user : factory)++;
    section ("BANQUES DE SONS");
    row (g, r.removeFromTop (34), "Banque d'usine", juce::String (factory) + juce::String::fromUTF8 (" sons intégrés \xc2\xb7 4 catégories"));
    r.removeFromTop (6);
    row (g, r.removeFromTop (34), "Mes samples", juce::String (user) + juce::String::fromUTF8 (" sons \xc2\xb7 ") + Bank::userFolder().getFullPathName().replace (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName(), "~"), col::muted, 170);
    r.removeFromTop (6);
    row (g, r.removeFromTop (34), "Dossiers", "SUB BASS, REESE BASS, 808, PLUCK BASS");
    section ("TAILLE DE L'INTERFACE");
    r.removeFromTop (36);
    section (juce::String::fromUTF8 ("À PROPOS"));
    row (g, r.removeFromTop (34), "Swell Bass v" JucePlugin_VersionString, juce::String::fromUTF8 ("Swell Audio \xc2\xb7 swell-audio.fr"));
}

void Settings::resized()
{
    const auto c = card();
    close.setBounds (c.getRight() - 50, c.getY() + 20, 30, 30);
    auto rowBtn = [&] (juce::Button& b, int y, int w = 90) { b.setBounds (c.getRight() - 24 - 6 - w, y + 5, w, 24); };
    const int y0 = c.getY() + 20 + 34 + 14 + 18;          // 1re ligne licence
    rowBtn (deactivate, y0 + 80);
    const int yb = y0 + 3 * 34 + 12 + 14 + 18;             // 1re ligne banques
    rowBtn (change, yb + 40, 70);
    open.setBounds (change.getX() - 76, yb + 45, 70, 24);
    rowBtn (rescan, yb + 80);
    const int yz = yb + 3 * 34 + 12 + 14 + 18;
    for (int i = 0; i < zooms.size(); ++i) zooms[i]->setBounds (c.getX() + 24 + i * 76, yz, 70, 28);
}

// ===== Éditeur ===================================================================================
enum { kAttack, kDecay, kRelease, kDrive, kTone, kGlide, kDetune, kWidth, kPunch, kSnap, kVolume };

Editor::Editor (Processor& p)
    : AudioProcessorEditor (p), proc (p), samples (p), wave (p),
      keys (p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard), settings (p)
{
    setLookAndFeel (&look);
    logo = juce::ImageCache::getFromMemory (BinaryData::swellmark_png, BinaryData::swellmark_pngSize);

    for (auto* b : { &prev, &next, &gear }) addAndMakeVisible (b);
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    gear.onClick = [this] { settings.setVisible (true); settings.toFront (true); };
    addAndMakeVisible (samples);
    addAndMakeVisible (wave);

    const std::pair<const char*, const char*> defs[] { { "attack", "Attack" }, { "decay", "Decay" }, { "release", "Release" },
        { "drive", "Drive" }, { "tone", "Tone" }, { "glide", "Glide" }, { "detune", "Detune" }, { "width", "Width" },
        { "punch", "Punch" }, { "snap", "Snap" }, { "volume", "Volume" } };
    for (auto& [id, name] : defs) addAndMakeVisible (knobs.add (new Knob (p.params, id, name)));

    for (int i = 0; i < 3; ++i)
    {
        modes[i].setClickingTogglesState (false);
        modes[i].onClick = [this, i] {
            if (auto* prm = proc.params.getParameter ("mode")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
        };
        addAndMakeVisible (modes[i]);
    }

    keys.setAvailableRange (24, 59);
    keys.setOctaveForMiddleC (4);
    keys.setKeyWidth (27.f);
    keys.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffd9d4ee));
    keys.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff15121f));
    keys.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff6b6588));
    keys.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keys.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, col::panel);
    keys.setWantsKeyboardFocus (true);
    addAndMakeVisible (keys);

    settings.onZoom = [this] (float z) {
        if (auto* top = findParentComponentOfClass<ResizableEditor>()) top->setSize (juce::roundToInt (W * z), juce::roundToInt (H * z));
    };
    settings.onBankChanged = [this] { shownCount = -1; };
    addChildComponent (settings);

    setSize (W, H);
    syncCategory();
    startTimerHz (30);
}

Editor::~Editor() { setLookAndFeel (nullptr); }

juce::Rectangle<int> Editor::tabBounds (int i) const
{
    const int x0 = 24, w = (W - 48 - 30) / 4;
    return { x0 + i * (w + 10), 78, w, 58 };
}

void Editor::step (int d)
{
    const int n = proc.bank.entries (proc.category()).size();
    if (n > 0) proc.selectSample (proc.category(), (proc.sampleIndex() + d + n) % n);
}

void Editor::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < numCategories; ++i)
        if (tabBounds (i).contains (e.getPosition()) && i != proc.category())
            proc.selectSample (i, 0);
}

void Editor::syncCategory()
{
    const int c = proc.category();
    look.accent = col::accent (c);
    // ligne 3 : réglages propres à la catégorie, puis le volume
    knobs[kDetune]->setVisible (c == Reese);
    knobs[kWidth]->setVisible (c == Reese);
    knobs[kPunch]->setVisible (c == Eight08);
    knobs[kSnap]->setVisible (c == Pluck);
    keys.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, look.accent.withAlpha (0.85f));
    keys.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, look.accent.withAlpha (0.25f));
    resized();
    samples.refresh();
    repaint();
}

void Editor::timerCallback()
{
    const int c = proc.category(), i = proc.sampleIndex(), n = proc.bank.entries (c).size();
    if (c != shownCat) { shownCat = c; shownIdx = i; shownCount = n; syncCategory(); }
    else if (i != shownIdx || n != shownCount) { shownIdx = i; shownCount = n; samples.refresh(); repaint (0, 0, W, 76); }
    wave.update();
    const int m = (int) proc.params.getRawParameterValue ("mode")->load();
    for (int k = 0; k < 3; ++k) modes[k].setToggleState (k == m, juce::dontSendNotification);
    const float lv = proc.level.load();
    const int note = proc.playingNote.load();
    if (std::abs (lv - shownLevel) > 0.005f || note != shownNote) { shownLevel = lv; shownNote = note; repaint (wave.getX(), wave.getBottom(), wave.getWidth(), 60); }
}

void Editor::paint (juce::Graphics& g)
{
    const int c = proc.category();
    const auto acc = col::accent (c);
    g.fillAll (col::bg);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1530), W * 0.5f, 0, col::bg, W * 0.5f, H * 0.7f, true));
    g.fillRect (getLocalBounds());

    // en-tête : vrai logo Swell + nom
    g.drawImageWithin (logo, 24, 16, 40, 36, juce::RectanglePlacement::centred);
    g.setFont (look.display (36.f));
    g.setColour (col::ink);
    g.drawText ("SWELL", 74, 14, 100, 40, juce::Justification::centredLeft);
    const int sw = juce::GlyphArrangement::getStringWidthInt (look.display (36.f), "SWELL ");
    g.setColour (acc);
    g.drawText ("BASS", 74 + sw, 14, 100, 40, juce::Justification::centredLeft);
    const int bw = juce::GlyphArrangement::getStringWidthInt (look.display (36.f), "BASS");
    g.setColour (col::muted);
    g.setFont (juce::FontOptions (11.f));
    g.drawText ("SWELL AUDIO", 74 + sw + bw + 12, 24, 120, 20, juce::Justification::centredLeft);
    // nom du sample entre les flèches
    auto nameBox = juce::Rectangle<int> (prev.getRight() + 4, prev.getY(), next.getX() - prev.getRight() - 8, prev.getHeight());
    g.setColour (juce::Colour (0x66000000));
    g.fillRoundedRectangle (nameBox.toFloat(), 6.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (nameBox.toFloat().reduced (0.5f), 6.f, 1.f);
    g.setColour (col::ink);
    g.setFont (juce::FontOptions (14.f));
    g.drawText (proc.sampleName(), nameBox, juce::Justification::centred, true);
    g.setColour (col::line);
    g.drawHorizontalLine (66, 0, (float) W);

    // onglets
    static const char* sub[] { "sinus profond, propre", "scies désaccordées, large", "kick tonal, glide, drive", "court, pincé, rebondi" };
    for (int i = 0; i < numCategories; ++i)
    {
        const auto r = tabBounds (i).toFloat();
        const bool on = i == c;
        const auto tc = col::accent (i);
        juce::Path p;
        p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight() + 1, 12.f, 12.f, true, true, false, false);
        g.setColour (on ? col::panel : juce::Colour (0xff0d0b16));
        g.fillPath (p);
        g.setColour (col::line);
        g.strokePath (p, juce::PathStrokeType (1.f));
        if (on) { g.setColour (tc); g.fillRoundedRectangle (r.getX() + 8, r.getY(), r.getWidth() - 16, 3.f, 1.5f); }
        g.setColour (on ? tc : col::muted.withAlpha (0.7f));
        g.setFont (look.display (25.f));
        g.drawText (categoryName (i), r.withTrimmedBottom (20).toNearestInt(), juce::Justification::centredBottom);
        g.setFont (juce::FontOptions (10.f));
        g.setColour (on ? col::muted : col::muted.withAlpha (0.5f));
        g.drawText (juce::String::fromUTF8 (sub[i]), r.withTrimmedTop (36).toNearestInt(), juce::Justification::centredTop);
    }

    // panneau central et boîtes
    const juce::Rectangle<float> main (24, 136, W - 48, 334);
    g.setColour (col::panel);
    g.fillRoundedRectangle (main, 12.f);
    g.setColour (col::line);
    g.drawRoundedRectangle (main.reduced (0.5f), 12.f, 1.f);
    auto boxAt = [&] (juce::Rectangle<int> r, const char* title) {
        g.setColour (col::box);
        g.fillRoundedRectangle (r.toFloat(), 10.f);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.f, 1.f);
        g.setColour (col::muted);
        g.setFont (juce::FontOptions (10.f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 (title), r.reduced (12, 10).removeFromTop (12), juce::Justification::centredLeft);
    };
    boxAt ({ 38, 150, 210, 306 }, "SAMPLES");
    boxAt ({ 260, 150, 466, 306 }, "FORME D'ONDE");
    boxAt ({ 738, 150, 224, 306 }, "RÉGLAGES");

    // dossier sous la liste
    g.setColour (col::muted);
    g.setFont (juce::FontOptions (10.f));
    const juce::Rectangle<float> fold (50, 420, 186, 24);
    g.drawRoundedRectangle (fold, 6.f, 1.f);
    g.drawText ("+ tes sons : Mes samples/" + juce::String (categoryName (c)), fold.toNearestInt(), juce::Justification::centred, true);

    // note jouée + niveau
    const int ny = wave.getBottom() + 8;
    g.setColour (acc);
    g.setFont (look.display (40.f));
    g.drawText (shownNote >= 0 ? noteName (shownNote) : juce::String::fromUTF8 ("\xe2\x80\x94"), wave.getX(), ny, 120, 36, juce::Justification::centredLeft);
    g.setColour (col::muted);
    g.setFont (juce::FontOptions (10.f));
    g.drawText (juce::String::fromUTF8 ("note jouée"), wave.getX(), ny + 34, 120, 12, juce::Justification::centredLeft);
    const juce::Rectangle<float> meter ((float) wave.getRight() - 170, (float) ny + 22, 170, 6);
    g.drawText ("niveau", meter.withY (meter.getY() - 16).withHeight (12).toNearestInt(), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff1a1728));
    g.fillRoundedRectangle (meter, 3.f);
    g.setGradientFill (juce::ColourGradient (acc, meter.getX(), 0, juce::Colours::white, meter.getRight(), 0, false));
    g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jlimit (0.f, 1.f, shownLevel)), 3.f);

    // mode
    g.setColour (col::muted);
    g.setFont (juce::FontOptions (10.f, juce::Font::bold));
    g.drawText ("MODE", 750, modes[0].getY() - 16, 100, 12, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (11.f));
    g.drawText ("Joue en MIDI, clique les touches, ou utilise le clavier de l'ordinateur", 0, 478, W, 16, juce::Justification::centred);
}

void Editor::resized()
{
    gear.setBounds (W - 24 - 38, 15, 38, 36);
    next.setBounds (gear.getX() - 10 - 30, 15, 30, 36);
    prev.setBounds (next.getX() - 200, 15, 30, 36);
    samples.setBounds (44, 172, 198, 242);
    wave.setBounds (272, 172, 442, 210);

    // grille 3 x 3 des boutons rotatifs ; ligne 3 selon la catégorie
    const int c = proc.category();
    const int gx = 744, gy = 170, cw = 70, ch = 76;
    auto place = [&] (int k, int col_, int row_) { knobs[k]->setBounds (gx + col_ * cw, gy + row_ * ch, cw, ch); };
    place (kAttack, 0, 0); place (kDecay, 1, 0); place (kRelease, 2, 0);
    place (kDrive, 0, 1);  place (kTone, 1, 1);  place (kGlide, 2, 1);
    if (c == Reese)        { place (kDetune, 0, 2); place (kWidth, 1, 2); }
    else if (c == Eight08) place (kPunch, 0, 2);
    else if (c == Pluck)   place (kSnap, 0, 2);
    place (kVolume, 2, 2);
    for (int i = 0; i < 3; ++i) modes[i].setBounds (750 + i * 68, 418, 62, 26);

    keys.setBounds (24, 500, W - 48, 136);
    keys.setKeyWidth ((W - 48) / 21.f);
    settings.setBounds (getLocalBounds());
}
} // namespace swell::bass
