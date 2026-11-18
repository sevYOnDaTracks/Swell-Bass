#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace swell::bass
{
enum P { pCat, pSample, pAttack, pDecay, pRelease, pDrive, pTone, pGlide, pDetune, pWidth, pPunch, pSnap, pMode, pVolume, numP };
static const char* ids[numP] { "category", "sample", "attack", "decay", "release", "drive", "tone", "glide",
                               "detune", "width", "punch", "snap", "mode", "volume" };

// ===== Voix ======================================================================================
void BassVoice::reset()
{
    held.clear();
    stage = Idle;
    env = 0;
    std::memset (lp, 0, sizeof (lp));
}

void BassVoice::noteOn (int note, float velocity, const Settings& s)
{
    const bool overlap = ! held.isEmpty() && stage != Idle && stage != Release;
    const bool hadNote = stage != Idle;
    held.removeFirstMatchingValue (note);
    held.add (note);
    vel = 0.35f + 0.65f * velocity;
    if (s.mode == 1)      trigger (note, ! overlap, overlap);   // Legato : pas de nouvelle attaque si on lie les notes
    else if (s.mode == 2) trigger (note, true, hadNote);        // Glide : nouvelle attaque, glisse depuis la note d'avant
    else                  trigger (note, true, false);          // Mono
    glideT = (overlap || s.mode == 2) && s.glide > 0.001f ? 0.f : 1.f;
}

void BassVoice::noteOff (int note, const Settings& s)
{
    const bool wasTop = ! held.isEmpty() && held.getLast() == note;
    held.removeFirstMatchingValue (note);
    if (! wasTop) return;
    if (held.isEmpty()) { if (stage != Idle) stage = Release; return; }
    trigger (held.getLast(), false, true);                      // retour à la note encore tenue, lié
    glideT = s.glide > 0.001f ? 0.f : 1.f;
}

void BassVoice::trigger (int note, bool retrigger, bool slide)
{
    glideFrom = slide ? pitch : (float) note;
    target = (float) note;
    if (! slide) pitch = target;
    if (retrigger)
    {
        posA = posB = 0;
        since = 0;
        stage = Attack;
        if (! slide) env = 0;
    }
}

static inline float readAt (const juce::AudioBuffer<float>& b, int ch, double pos)
{
    const int i = (int) pos;
    const float f = (float) (pos - i);
    const auto* d = b.getReadPointer (ch);
    const int n = b.getNumSamples();
    if (i + 1 >= n) return i < n ? d[i] * (1 - f) : 0.f;
    // Hermite 4 points
    const float xm1 = i > 0 ? d[i - 1] : d[i], x0 = d[i], x1 = d[i + 1], x2 = i + 2 < n ? d[i + 2] : x1;
    const float c = (x1 - xm1) * 0.5f, v = x0 - x1, w = c + v, a = w + v + (x2 - x0) * 0.5f, bn = w + a;
    return ((a * f - bn) * f + c) * f + x0;
}

void BassVoice::render (juce::AudioBuffer<float>& out, int start, int num, const Sample* smp, const Settings& s)
{
    if (stage == Idle || smp == nullptr) return;
    const auto& audio = smp->audio;
    const double len = audio.getNumSamples();
    const float dt = (float) (1.0 / hostRate);
    const float attackStep = s.attack > 0.0005f ? dt / s.attack : 1.f;
    const float decayCoef = s.decay > 0 ? std::exp (-dt / (s.decay * 0.3f)) : 1.f;
    const float releaseCoef = std::exp (-dt / juce::jmax (0.002f, s.release * 0.3f));
    const float glideStep = s.glide > 0.001f ? dt / s.glide : 1.f;
    const double rateBase = smp->sampleRate / hostRate;
    const bool reese = s.category == Reese, eight = s.category == Eight08, pluck = s.category == Pluck;
    const float detune = reese ? s.detune : 0.f;   // demi-tons
    const float width = reese ? s.width : 0.f;
    const float drive = 1.f + s.drive * 11.f, driveNorm = 1.f / std::tanh (drive);
    const float baseCut = 40.f * std::pow (500.f, s.tone);   // 40 Hz … 20 kHz
    auto* L = out.getWritePointer (0);
    auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
    float coef = 1.f;

    for (int n = 0; n < num; ++n)
    {
        // hauteur : glide puis punch (808 : la note part plus haut et retombe)
        if (glideT < 1.f) { glideT = juce::jmin (1.f, glideT + glideStep); pitch = glideFrom + (target - glideFrom) * (1 - (1 - glideT) * (1 - glideT)); }
        else pitch = target;
        float semis = pitch - (float) smp->rootNote;
        if (eight && s.punch > 0) semis += s.punch * 24.f * std::exp ((float) -since / 0.035f);

        // filtre : Tone + Snap (pluck : le filtre s'ouvre à l'attaque puis se referme)
        if ((n & 15) == 0)
        {
            float cut = baseCut;
            if (pluck && s.snap > 0) cut *= 1.f + s.snap * 30.f * std::exp ((float) -since / 0.06f);
            cut = juce::jmin (cut, (float) hostRate * 0.45f);
            coef = 1.f - std::exp (-juce::MathConstants<float>::twoPi * cut * dt);
        }

        // enveloppe
        switch (stage)
        {
            case Attack:  env += attackStep; if (env >= 1) { env = 1; stage = Decay; } break;
            case Decay:   env *= decayCoef; break;
            case Release: env *= releaseCoef; if (env < 1.0e-4f) { stage = Idle; env = 0; } break;
            case Idle:    break;
        }
        if (stage == Idle || posA >= len) { stage = Idle; break; }

        const double rate = rateBase * std::pow (2.0, semis / 12.0);
        float l, r;
        if (detune > 0)
        {
            const double ra = rate * std::pow (2.0, detune / 12.0), rb = rate * std::pow (2.0, -detune / 12.0);
            const float aL = readAt (audio, 0, posA), aR = readAt (audio, 1, posA);
            const float bL = readAt (audio, 0, posB), bR = readAt (audio, 1, posB);
            const float wa = 0.5f + 0.5f * width, wb = 1 - wa;
            l = (aL * wa + bL * wb);
            r = (bR * wa + aR * wb);
            posA += ra; posB += rb;
        }
        else
        {
            l = readAt (audio, 0, posA);
            r = readAt (audio, 1, posA);
            posA += rate; posB = posA;
        }

        float x[2] { l, r };
        for (int c = 0; c < 2; ++c)
        {
            float v = x[c];
            if (s.drive > 0.001f) v = std::tanh (v * drive) * driveNorm * (1.f / (1.f + s.drive * 0.8f));
            lp[c][0] += coef * (v - lp[c][0]);
            lp[c][1] += coef * (lp[c][0] - lp[c][1]);
            x[c] = lp[c][1];
        }
        const float g = env * vel * s.gain;
        L[start + n] += x[0] * g;
        if (R) R[start + n] += x[1] * g;
        since += dt;
    }
}

// ===== Processeur =================================================================================
static juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto ms = [] (float v, int) { return v < 1 ? String (roundToInt (v * 1000)) + " ms" : String (v, 2) + " s"; };
    auto pct = [] (float v, int) { return String (roundToInt (v * 100)) + " %"; };
    auto range = [] (float a, float b, float centre) { NormalisableRange<float> r (a, b); r.setSkewForCentre (centre); return r; };

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids[pCat], 1 }, juce::String::fromUTF8 ("Catégorie"), StringArray { "Sub Bass", "Reese Bass", "808", "Pluck Bass" }, 2));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { ids[pSample], 1 }, "Sample", 0, 255, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pAttack], 1 }, "Attack", range (0.f, 0.5f, 0.05f), 0.002f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pDecay], 1 }, "Decay", range (0.05f, 10.f, 1.5f), 10.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([ms] (float v, int n) { return v >= 9.99f ? String ("Naturel") : ms (v, n); })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pRelease], 1 }, "Release", range (0.005f, 3.f, 0.3f), 0.12f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pDrive], 1 }, "Drive", NormalisableRange<float> (0, 1), 0.f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pTone], 1 }, "Tone", NormalisableRange<float> (0, 1), 1.f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pGlide], 1 }, "Glide", range (0.f, 1.f, 0.15f), 0.08f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pDetune], 1 }, "Detune", NormalisableRange<float> (0, 50), 12.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ct"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pWidth], 1 }, "Width", NormalisableRange<float> (0, 1), 0.6f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pPunch], 1 }, "Punch", NormalisableRange<float> (0, 1), 0.f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pSnap], 1 }, "Snap", NormalisableRange<float> (0, 1), 0.3f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids[pMode], 1 }, "Mode", StringArray { "Mono", "Legato", "Glide" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[pVolume], 1 }, "Volume", NormalisableRange<float> (-24.f, 6.f), 0.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return l;
}

Processor::Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "SwellBass", layout())
{
    for (int i = 0; i < numP; ++i) p[i] = params.getRawParameterValue (ids[i]);
    loadIfNeeded();
    startTimerHz (20);
}

Processor::~Processor() { stopTimer(); }

bool Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void Processor::prepareToPlay (double sr, int) { voice.prepare (sr); keyboard.reset(); }

int Processor::category() const { return juce::jlimit (0, numCategories - 1, (int) p[pCat]->load()); }
int Processor::sampleIndex() const { return juce::jlimit (0, juce::jmax (0, bank.entries (category()).size() - 1), (int) p[pSample]->load()); }

juce::String Processor::sampleName() const
{
    const auto& l = bank.entries (category());
    return juce::isPositiveAndBelow (sampleIndex(), l.size()) ? l[sampleIndex()].name : juce::String ("Aucun sample");
}

static void setParam (juce::AudioProcessorValueTreeState& t, const char* id, float plain)
{
    if (auto* prm = t.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

void Processor::selectSample (int cat, int index)
{
    setParam (params, ids[pCat], (float) cat);
    setParam (params, ids[pSample], (float) index);
    loadIfNeeded();
}

void Processor::rescanBank()
{
    const auto name = sampleName();
    const int cat = category();
    bank.rescan();
    const auto& l = bank.entries (cat);
    for (int i = 0; i < l.size(); ++i)
        if (l[i].name == name) { setParam (params, ids[pSample], (float) i); break; }
    loadedCat = loadedIdx = -1;
    loadIfNeeded();
}

void Processor::timerCallback() { loadIfNeeded(); }

void Processor::loadIfNeeded()
{
    if (restoreName.isNotEmpty())
    {
        const auto& l = bank.entries (category());
        for (int i = 0; i < l.size(); ++i)
            if (l[i].name == restoreName) { setParam (params, ids[pSample], (float) i); break; }
        restoreName.clear();
    }
    const int c = category(), i = sampleIndex();
    if (c == loadedCat && i == loadedIdx)
    {
        // l'ancien sample rendu par le thread audio est libéré ici, jamais sur le thread audio
        std::shared_ptr<Sample> old;
        const juce::SpinLock::ScopedTryLockType tl (swapLock);
        if (tl.isLocked() && ! fresh) old = std::move (pending);
        return;
    }
    loadedCat = c; loadedIdx = i;
    auto s = bank.load (c, i);
    uiSample = s;
    std::shared_ptr<Sample> old;
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        old = std::move (pending);
        pending = std::move (s);
        fresh = true;
    }
    // 'old' est libéré ici, sur le thread des messages
}

BassVoice::Settings Processor::readSettings() const
{
    BassVoice::Settings s;
    s.attack = p[pAttack]->load();
    s.decay = p[pDecay]->load() >= 9.99f ? -1.f : p[pDecay]->load();
    s.release = p[pRelease]->load();
    s.drive = p[pDrive]->load();
    s.tone = p[pTone]->load();
    s.glide = p[pGlide]->load();
    s.detune = p[pDetune]->load() / 100.f;
    s.width = p[pWidth]->load();
    s.punch = p[pPunch]->load();
    s.snap = p[pSnap]->load();
    s.mode = (int) p[pMode]->load();
    s.category = category();
    s.gain = juce::Decibels::decibelsToGain (p[pVolume]->load());
    return s;
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    keyboard.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    {   // nouveau sample prêt ? (l'ancien repart vers le thread des messages via 'pending')
        const juce::SpinLock::ScopedTryLockType tl (swapLock);
        if (tl.isLocked() && fresh)
        {
            fresh = false;
            current.swap (pending);   // l'ancien reste dans 'pending' jusqu'à ce que le thread des messages le libère
            voice.allOff();
        }
    }

    const auto s = readSettings();
    int pos = 0;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int at = juce::jlimit (0, buffer.getNumSamples(), m.samplePosition);
        voice.render (buffer, pos, at - pos, current.get(), s);
        pos = at;
        if (msg.isNoteOn()) voice.noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), s);
        else if (msg.isNoteOff()) voice.noteOff (msg.getNoteNumber(), s);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) voice.allOff();
    }
    voice.render (buffer, pos, buffer.getNumSamples() - pos, current.get(), s);
    midi.clear();

    if (! licence->isUnlocked()) { buffer.clear(); level.store (0); return; }

    playingNote.store (voice.currentNote());
    const float peak = buffer.getMagnitude (0, buffer.getNumSamples());
    level.store (juce::jmax (peak, level.load() * 0.85f));
}

void Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.setProperty ("sampleName", sampleName(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            restoreName = state.getProperty ("sampleName").toString();
            params.replaceState (state);   // le timer recharge le sample au prochain tour
        }
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return swell::LicenceGuard::attach (new ResizableEditor (*this, new Editor (*this)));
}
} // namespace swell::bass

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new swell::bass::Processor(); }
