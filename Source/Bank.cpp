#include "Bank.h"
#include "BinaryData.h"

namespace swell::bass
{
static juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Swell Audio").getChildFile ("Swell Bass").getChildFile ("settings.xml");
}

juce::File Bank::userFolder()
{
    if (auto xml = juce::parseXML (settingsFile()))
        if (auto p = xml->getStringAttribute ("userFolder"); p.isNotEmpty())
            return juce::File (p);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
        .getChildFile ("Swell Audio").getChildFile ("Swell Bass").getChildFile ("User");
}

void Bank::setUserFolder (const juce::File& f)
{
    juce::XmlElement xml ("SwellBass");
    if (auto old = juce::parseXML (settingsFile())) xml = *old;
    xml.setAttribute ("userFolder", f.getFullPathName());
    settingsFile().getParentDirectory().createDirectory();
    xml.writeTo (settingsFile());
}

int Bank::parseRootNote (const juce::String& stem, int fallback)
{
    // dernier « _Note » du nom : _C1, _F#2, _Bb0 ; sans note, le sample est en C
    auto tag = stem.fromLastOccurrenceOf ("_", false, false).trim().toUpperCase();
    if (tag.isEmpty() || tag == stem.toUpperCase()) return fallback;
    static const int base[] { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const auto letter = tag[0];
    if (letter < 'A' || letter > 'G') return fallback;
    int n = base[letter - 'A'], i = 1;
    if (tag[i] == '#') { ++n; ++i; }
    else if (tag[i] == 'B' && tag.length() > 2) { --n; ++i; }
    auto oct = tag.substring (i);
    if (! oct.containsOnly ("-0123456789") || oct.isEmpty()) return fallback;
    // Tout se joue depuis C5 (note MIDI 60) : la touche C5 donne le sample à sa hauteur d'origine.
    // Seule la note compte, pas l'octave : un sample en D se cale sur D5 (62), donc C5 le joue un ton plus bas.
    return kRootKey + ((n % 12) + 12) % 12;
}

static juce::String displayName (const juce::String& stem)
{
    return stem.containsChar ('_') ? stem.upToLastOccurrenceOf ("_", false, false) : stem;
}

void Bank::toggleFavorite (int category, const juce::String& name)
{
    const auto k = favKey (category, name);
    if (favorites.contains (k)) favorites.removeString (k); else favorites.add (k);
    juce::XmlElement xml ("SwellBass");
    if (auto old = juce::parseXML (settingsFile())) xml = *old;
    xml.setAttribute ("favorites", favorites.joinIntoString ("|"));
    settingsFile().getParentDirectory().createDirectory();
    xml.writeTo (settingsFile());
}

Bank::Bank()
{
    if (auto xml = juce::parseXML (settingsFile()))
        favorites.addTokens (xml->getStringAttribute ("favorites"), "|", "");
    favorites.removeEmptyStrings();
    formats.registerBasicFormats();
    rescan();
}

void Bank::addFactory()
{
    static const std::pair<const char*, int> tags[] { { "SUB__", Sub }, { "REESE__", Reese }, { "808__", Eight08 }, { "PLUCK__", Pluck } };
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String file = BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i]);
        if (! file.endsWithIgnoreCase (".flac")) continue;
        for (auto& [tag, cat] : tags)
            if (file.startsWith (tag))
            {
                const auto stem = file.fromFirstOccurrenceOf ("__", false, false).upToLastOccurrenceOf (".", false, false);
                Entry e;
                e.name = displayName (stem);
                e.category = cat;
                e.rootNote = parseRootNote (stem);
                e.resource = BinaryData::namedResourceList[i];
                lists[(size_t) cat].add (e);
            }
    }
}

void Bank::rescan()
{
    for (auto& l : lists) l.clear();
    addFactory();

    const auto root = userFolder();
    for (int c = 0; c < numCategories; ++c)
    {
        auto dir = root.getChildFile (categoryName (c));
        dir.createDirectory(); // le client voit tout de suite où déposer ses sons
        juce::Array<juce::File> files;
        for (const auto& f : juce::RangedDirectoryIterator (dir, false, "*.wav;*.aif;*.aiff;*.flac"))
            if (! f.getFile().getFileName().startsWith ("._")) files.add (f.getFile());
        files.sort();
        for (auto& f : files)
        {
            Entry e;
            e.name = displayName (f.getFileNameWithoutExtension());
            e.category = c;
            e.rootNote = parseRootNote (f.getFileNameWithoutExtension());
            e.user = true;
            e.file = f;
            lists[(size_t) c].add (e);
        }
    }
    for (auto& l : lists)
        std::stable_sort (l.begin(), l.end(), [] (const Entry& a, const Entry& b) { return ! a.user && b.user; });
}

std::shared_ptr<Sample> Bank::load (int category, int index) const
{
    const auto& l = entries (category);
    if (! juce::isPositiveAndBelow (index, l.size())) return {};
    const auto& e = l.getReference (index);

    std::unique_ptr<juce::AudioFormatReader> reader;
    if (e.user)
        reader.reset (formats.createReaderFor (e.file));
    else
    {
        int size = 0;
        if (auto* data = BinaryData::getNamedResource (e.resource.toRawUTF8(), size))
            reader.reset (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false)));
    }
    if (reader == nullptr || reader->lengthInSamples <= 0) return {};

    auto s = std::make_shared<Sample>();
    const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) reader->sampleRate * 60);
    s->audio.setSize (2, len);
    reader->read (&s->audio, 0, len, 0, true, true);
    if (reader->numChannels == 1) s->audio.copyFrom (1, 0, s->audio, 0, 0, len);
    s->sampleRate = reader->sampleRate;
    s->rootNote = e.rootNote;
    buildSustainLoop (*s);
    return s;
}

// ===== Tenue en boucle ===========================================================================
void buildSustainLoop (Sample& s)
{
    const int n = s.audio.getNumSamples();
    const double sr = s.sampleRate;
    if (n < (int) (0.05 * sr)) return;
    const auto* chL = s.audio.getReadPointer (0);
    const auto* chR = s.audio.getReadPointer (1);
    auto mono = [&] (int i) { return 0.5f * (chL[i] + chR[i]); };

    // enveloppe RMS (fenêtre 40 ms, pas de 5 ms)
    const int hop = juce::jmax (1, (int) (0.005 * sr)), win = (int) (0.04 * sr);
    std::vector<float> env;
    for (int c = 0; c + 1 < n; c += hop)
    {
        double acc = 0; int cnt = 0;
        for (int i = juce::jmax (0, c - win / 2); i < juce::jmin (n, c + win / 2); ++i) { const float v = mono (i); acc += v * v; ++cnt; }
        env.push_back ((float) std::sqrt (acc / juce::jmax (1, cnt)));
    }
    auto E = [&] (int i) {
        const float f = (float) i / hop;
        const int a = juce::jlimit (0, (int) env.size() - 1, (int) f), b = juce::jmin ((int) env.size() - 1, a + 1);
        return env[(size_t) a] + (env[(size_t) b] - env[(size_t) a]) * (f - (float) a);
    };
    const int peakFrame = (int) (std::max_element (env.begin(), env.end()) - env.begin());
    const float peakEnv = env[(size_t) peakFrame];
    if (peakEnv < 1.0e-4f) return;
    const int peakIdx = peakFrame * hop;

    // période : autocorrélation autour de la zone choisie (20 à 400 Hz)
    auto periodAt = [&] (int at) {
        const int minLag = (int) (sr / 400), maxLag = (int) (sr / 20), len = juce::jmin ((int) (0.12 * sr), n - at - maxLag - 1);
        if (len < maxLag) return (int) (sr / 65.4);
        int best = minLag; double bestV = -1e30;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double acc = 0;
            for (int i = 0; i < len; i += 2) acc += mono (at + i) * mono (at + i + lag);
            acc /= (double) (len - lag * 0.0);
            if (acc > bestV * 1.02) { bestV = acc; best = lag; } // la plus petite période forte
        }
        return best;
    };

    // début de boucle : quand le son est retombé à -10 dB (la partie tenue), ou vers la fin s'il tient tout seul
    const int earliest = juce::jmin (n - 1, peakIdx + (int) (0.12 * sr));
    int ls = -1;
    const float thr = peakEnv * 0.316f;
    for (int i = earliest; i < n; i += hop) if (E (i) < thr) { ls = i; break; }
    int L = (int) (0.25 * sr);
    if (ls < 0) ls = n - L - (int) (0.05 * sr);
    // échantillon trop court : boucle plus courte, placée dans ce qui reste après l'attaque
    const int avail = n - juce::jmin (n - 1, peakIdx + (int) (0.03 * sr)) - 2;
    if (avail < 64) return;
    L = juce::jmin (L, (int) (avail * 0.6));
    const int T = periodAt (juce::jlimit (0, juce::jmax (0, n - (int) (0.2 * sr)), ls));
    if (T > 0 && L >= T) L = juce::jmax (T, (L / T) * T);
    // la boucle doit rester dans le son : si le fichier finit par du silence (ou une chute brutale),
    // on la recule avant la fin de la partie encore forte
    int lastLoud = -1;
    for (int i = n - 1; i > peakIdx; i -= hop) if (E (i) > peakEnv * 0.2f) { lastLoud = i; break; }
    const int minStart = juce::jmin (n - 1, peakIdx + (int) (0.03 * sr));
    if (lastLoud > 0 && lastLoud - minStart < L + 16)
    {   // sample très court : boucle raccourcie pour tenir entre l'attaque et la fin du son
        L = juce::jmax (64, lastLoud - minStart - 16);
        if (T > 0 && L >= T) L = (L / T) * T;
    }
    if (lastLoud > 0 && ls + L > lastLoud) ls = lastLoud - L;
    ls = juce::jlimit (juce::jmin (n - 1, peakIdx + (int) (0.03 * sr)), juce::jmax (0, n - L - 2), ls);
    int X = juce::jmax (8, juce::jmin ((int) (0.06 * sr), L / 2));
    X = juce::jmin (X, ls);                       // le fondu ne peut pas commencer avant le début du fichier
    if (X < 8 || ls + L > n) return;

    const float ref = juce::jmax (E (ls), 1.0e-5f), floorE = ref * 0.05f;
    auto flat = [&] (int i) { return ref / juce::jmax (floorE, E (i)); };
    s.loop.setSize (2, L);
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* d = s.audio.getReadPointer (ch);
        auto* o = s.loop.getWritePointer (ch);
        for (int i = 0; i < L; ++i)
        {
            float v = d[ls + i] * flat (ls + i);
            if (i >= L - X)
            {   // raccord : la fin de la boucle se fond dans ce qui précède son début
                const int j = i - (L - X);
                const float w = (float) j / (float) X * juce::MathConstants<float>::halfPi;
                const float pre = d[ls - X + j] * flat (ls - X + j);
                v = v * std::cos (w) + pre * std::sin (w);
            }
            o[i] = v;
        }
    }
    s.loopStart = ls;
    s.loopLength = L;
}
} // namespace swell::bass
