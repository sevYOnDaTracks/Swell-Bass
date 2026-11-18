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

float Bank::parseRootNote (const juce::String& stem, float fallback)
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
    // hauteur exacte : « E1+15 » = mi, octave 1, +15 cents (l'octave ne compte pas, seule la note et les cents)
    auto rest = tag.substring (i);
    float cents = 0;
    const int sign = juce::jmax (rest.lastIndexOfChar ('+'), rest.lastIndexOfChar ('-'));
    if (sign > 0) { cents = rest.substring (sign).getFloatValue(); rest = rest.substring (0, sign); }
    auto oct = rest;
    if (! oct.containsOnly ("-0123456789") || oct.isEmpty()) return fallback;
    // Tout se joue depuis C5 (note MIDI 60) : la touche C5 donne le sample à sa hauteur d'origine.
    // Seule la note compte, pas l'octave.
    // Hauteur absolue du son (C1 = 24). Tout ce qui est à moins de 8,5 demi-tons de C1 se joue en C1 sur la
    // touche C5 (Impact en la#0 monte un peu, Young Metro en sol#1 descend) : toutes les 808 à la même octave.
    // Les sons nettement plus aigus (subs en C2...) gardent leur octave : do le plus proche.
    const float m = (float) (n + 12 * (oct.getIntValue() + 1)) + cents / 100.f;
    const float target = std::abs (m - 24.f) <= 8.5f ? 24.f : 12.f * std::ceil (m / 12.f - 0.5f);
    return (float) kRootKey + (m - target);
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
    return s;
}

} // namespace swell::bass
