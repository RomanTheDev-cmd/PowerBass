// Tests of the plug-in wrapper (processor, state, presets through the host path, editor).
// The DSP itself is covered by PowerBassTests.cpp.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

namespace
{
int failures = 0, checks = 0;
void expect (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
}

float rawValue (PowerBassProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); }
void setPlain (PowerBassProcessor& p, const char* id, float v)
{
    auto* param = p.state.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

// A mouse event on component c (click or drag), for the mouse handlers of the waveform.
juce::MouseEvent mouseAt (juce::Component* c, bool dragged)
{
    const auto now = juce::Time::getCurrentTime();
    const juce::Point<float> pt (20.0f, 20.0f);
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pt, {}, juce::MouseInputSource::defaultPressure,
                             juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                             juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, c, c, now, pt, now, 1, dragged);
}


struct Run { double peak = 0; bool finite = true; };

// A rolling 16th bass line at 150 BPM (C2 / D#2), `between` runs before each block.
Run play (PowerBassProcessor& p, int blocks, std::function<void (int)> between = {}, int channels = 2)
{
    Run r;
    juce::AudioBuffer<float> buf (channels, 512);
    const int step = 4800; // 16th at 150 BPM, 48 kHz
    for (int b = 0; b < blocks; ++b)
    {
        if (between) between (b);
        juce::MidiBuffer midi;
        for (int s = 0; s < 512; ++s)
        {
            const int t = b * 512 + s;
            const int note = (t / (step * 8)) % 2 == 0 ? 36 : 39;
            if (t % step == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), s);
            if (t % step == step * 3 / 4) midi.addEvent (juce::MidiMessage::noteOff (1, note), s);
        }
        buf.clear();
        p.processBlock (buf, midi);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) r.finite = false;
                r.peak = std::max (r.peak, (double) std::abs (v));
            }
    }
    return r;
}

void testDefaultsMatchDefaultPreset()
{
    // A fresh instance and every double-click reset must sound like the default preset.
    PowerBassProcessor p;
    const pb::Preset* def = nullptr;
    for (const auto& pr : pb::factoryPresets())
        if (pr.name == "Reese Core") def = &pr;
    expect (def != nullptr, "default preset Reese Core exists");
    if (def == nullptr) return;
    expect (p.getPresetName() == "Reese Core", "a fresh instance shows Reese Core");
    for (const auto& pid : pb::allParamIds())
    {
        auto* prm = p.state.getParameter (pid);
        const auto it = def->values.find (pid);
        if (it == def->values.end()) continue; // unlisted: the default by definition
        const float want = prm->convertTo0to1 (it->second), have = prm->getDefaultValue(); // double-click reset target
        expect (std::abs (want - have) < 0.002f, "default " + pid + " = Reese Core (" + juce::String (prm->convertFrom0to1 (have), 3)
                                                 + " vs " + juce::String (it->second, 3) + ")");
    }
}

// Every factory preset loads in the plug-in exactly as the tests, the level grid and the fitting tools
// render it (paramsFromSnapshot). A key missing from a preset must not fall back to a different value
// in the plug-in than in those tools (found by review: five Deep Sub presets played with detune).
void testFactoryPresetsLoadAsMeasured()
{
    PowerBassProcessor p;
    const auto& factory = pb::factoryPresets();
    int bad = 0;
    juce::String first;
    for (int i = 0; i < (int) factory.size(); ++i)
    {
        p.loadPreset (i);
        const auto got = pb::paramsFromSnapshot (pb::captureSnapshot (p.state));
        const auto want = pb::paramsFromSnapshot (factory[(size_t) i].values);
        juce::String diff;
        auto near = [&diff] (const char* what, double a, double b)
        {
            if (std::abs (a - b) > 0.051 + 0.01 * std::abs (b))
                diff << what << " " << juce::String (a, 3) << " vs " << juce::String (b, 3) << "; ";
        };
        near ("shape", got.shape, want.shape); near ("sub", got.sub, want.sub); near ("oscDrive", got.oscDrive, want.oscDrive);
        near ("fmAmount", got.fmAmount, want.fmAmount); near ("fmRatio", got.fmRatio, want.fmRatio);
        near ("fmDecay", got.fmDecayMs, want.fmDecayMs); near ("oscMix", got.oscMix, want.oscMix);
        near ("cutoff", got.cutoffHz, want.cutoffHz); near ("resonance", got.resonance, want.resonance);
        near ("envAmount", got.envAmount, want.envAmount); near ("filterMode", (int) got.filterMode, (int) want.filterMode);
        near ("distType", (int) got.distType, (int) want.distType); near ("distDrive", got.distDrive, want.distDrive);
        near ("distTone", got.distTone, want.distTone); near ("distMix", got.distMix, want.distMix);
        near ("dark", got.dark, want.dark); near ("bite", got.bite, want.bite); near ("punch", got.punch, want.punch);
        near ("width", got.width, want.width); near ("split", got.splitHz, want.splitHz);
        near ("attack", got.attackMs, want.attackMs); near ("decay", got.decayMs, want.decayMs);
        near ("sustain", got.sustain, want.sustain); near ("release", got.releaseMs, want.releaseMs);
        near ("glide", got.glideMs, want.glideMs); near ("glideMode", (int) got.glideMode, (int) want.glideMode);
        near ("master", got.masterDb, want.masterDb); near ("filterPos", got.filterPostDist, want.filterPostDist);
        near ("oversample", got.oversampleIndex, want.oversampleIndex); near ("fltDecay", got.fltDecayMs, want.fltDecayMs);
        near ("velSens", got.velSens, want.velSens); near ("bendRange", got.bendRange, want.bendRange);
        near ("stereoDelay", got.stereoDelayMs, want.stereoDelayMs); near ("detune", got.detune, want.detune);
        if (diff.isNotEmpty())
        {
            if (bad++ < 3) first << factory[(size_t) i].name << ": " << diff;
        }
    }
    expect (bad == 0, "factory presets load in the plug-in as measured (" + juce::String (bad) + " differ; " + first + ")");
}

void testStateRoundTrip()
{
    PowerBassProcessor a;
    a.prepareToPlay (48000, 512);
    setPlain (a, pb::id::cutoff, 1234.0f);
    setPlain (a, pb::id::distType, 2.0f);
    setPlain (a, pb::id::detune, 0.4f);
    setPlain (a, pb::id::stereoDelay, 6.0f);
    a.setPresetName ("My Saved Bass");
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    PowerBassProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true;
    for (const auto& id : pb::allParamIds())
        same = same && std::abs (rawValue (a, id.toRawUTF8()) - rawValue (b, id.toRawUTF8())) < 1.0e-3f;
    expect (same, "project state restores every parameter");
    expect (b.getPresetName() == "My Saved Bass", "project state restores the preset name");
    const float before = rawValue (b, pb::id::cutoff);
    const char junk[] = "not a PowerBass state";
    b.setStateInformation (junk, (int) sizeof (junk));
    expect (std::abs (rawValue (b, pb::id::cutoff) - before) < 1.0e-3f, "garbage state is ignored");
}

void testLegacyProjectMigration()
{
    PowerBassProcessor a;
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    for (auto* id : { pb::id::detune, pb::id::stereoDelay })
        if (auto* e = xml->getChildByAttribute ("id", id)) xml->removeChildElement (e, true);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    PowerBassProcessor b;
    b.setStateInformation (old.getData(), (int) old.getSize());
    expect (rawValue (b, pb::id::detune) < 1.0e-4f, "old project: DETUNE off");
    expect (rawValue (b, pb::id::stereoDelay) < 1.0e-4f, "old project: stereo DELAY off");
}

void testPresetSwitchingWhilePlaying()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    const int n = (int) p.library.all().size();
    double worst = 0;
    bool finite = true;
    for (int i = 0; i < n; i += std::max (1, n / 60))
    {
        const auto r = play (p, 10, [&p, i] (int b) { if (b == 5) p.loadPreset (i); });
        worst = std::max (worst, r.peak);
        finite = finite && r.finite;
    }
    expect (finite, "switching presets while playing stays finite");
    expect (worst <= 1.0, "switching presets while playing stays under 0 dBFS (" + juce::String (worst, 3) + ")");
}

void testReprepareMatchesFresh()
{
    for (double startRate : { 44100.0, 48000.0, 96000.0 })
        for (float width : { 0.0f, 1.0f })
        {
            const double finalRate = startRate == 96000.0 ? 44100.0 : 96000.0;
            PowerBassProcessor reused, fresh;
            setPlain (reused, pb::id::width, width);
            setPlain (fresh, pb::id::width, width);
            reused.prepareToPlay (startRate, 512);
            play (reused, 8);
            reused.prepareToPlay (finalRate, 512);
            fresh.prepareToPlay (finalRate, 512);
            double error = 0.0;
            for (int b = 0; b < 32; ++b)
            {
                juce::AudioBuffer<float> a (2, 512), c (2, 512);
                a.clear(); c.clear();
                juce::MidiBuffer ma, mc;
                if (b == 0)
                {
                    ma.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 37);
                    mc = ma;
                }
                reused.processBlock (a, ma);
                fresh.processBlock (c, mc);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        error = std::max (error, (double) std::abs (a.getSample (ch, i) - c.getSample (ch, i)));
            }
            expect (error < 1.0e-6, "reprepare " + juce::String (startRate) + " -> " + juce::String (finalRate)
                    + " / WIDTH " + juce::String (width) + " matches fresh (error " + juce::String (error, 9) + ")");
        }
}

void testLatencyAvailableBeforeFirstBlock()
{
    // The host configures PDC before it asks for audio, including on a restored project.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int os = 0; os < 3; ++os)
        {
            PowerBassProcessor p;
            setPlain (p, pb::id::oversample, (float) os);
            p.prepareToPlay (sr, 512);
            const int before = p.getLatencySamples();
            juce::dsp::Oversampling<float> reference (1, (size_t) (os + 1),
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
            reference.initProcessing (512);
            const int expected = (int) std::lround (reference.getLatencyInSamples());
            play (p, 2);
            expect (before == expected && before == p.getLatencySamples(),
                    "PDC available before first audio at " + juce::String (sr) + " / OS " + juce::String (os)
                    + " (before " + juce::String (before) + ", after " + juce::String (p.getLatencySamples()) + ")");
        }
}

void testStateRestoredAfterPrepareStartsExact()
{
    // Some hosts restore the project after prepareToPlay. The first block must still start on the
    // restored values, not glide to them from the defaults that were current at prepare time.
    PowerBassProcessor source;
    setPlain (source, pb::id::master, -18.0f);
    setPlain (source, pb::id::cutoff, 300.0f);
    setPlain (source, pb::id::width, 1.0f);
    setPlain (source, pb::id::splitFreq, 150.3f); // within 0.5 Hz of the default: must still take effect
    setPlain (source, pb::id::oversample, 0.0f); // 2x: its latency differs from the default 4x
    juce::MemoryBlock saved;
    source.getStateInformation (saved);

    PowerBassProcessor late, early;
    late.prepareToPlay (48000, 512);
    late.setStateInformation (saved.getData(), (int) saved.getSize());
    early.setStateInformation (saved.getData(), (int) saved.getSize());
    early.prepareToPlay (48000, 512);
    PowerBassProcessor defaults;
    defaults.prepareToPlay (48000, 512);
    expect (early.getLatencySamples() != defaults.getLatencySamples(),
            "the restored oversampling has a latency different from the default (else the next check is empty)");
    expect (late.getLatencySamples() == early.getLatencySamples(),
            "state restored after prepare reports its oversampling latency before the first block ("
            + juce::String (late.getLatencySamples()) + " vs " + juce::String (early.getLatencySamples()) + ")");
    double error = 0.0;
    for (int b = 0; b < 8; ++b)
    {
        juce::AudioBuffer<float> a (2, 512), c (2, 512);
        a.clear(); c.clear();
        juce::MidiBuffer ma, mc;
        if (b == 0) { ma.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0); mc = ma; }
        late.processBlock (a, ma);
        early.processBlock (c, mc);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                error = std::max (error, (double) std::abs (a.getSample (ch, i) - c.getSample (ch, i)));
    }
    expect (error < 1.0e-6, "state restored after prepare starts exactly on the restored values (error "
            + juce::String (error, 9) + ")");
    expect (late.getLatencySamples() == early.getLatencySamples(),
            "state restored after prepare reports the restored oversampling latency");
}

void testOversamplingLatency()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    int lat[3];
    for (int os = 0; os < 3; ++os)
    {
        setPlain (p, pb::id::oversample, (float) os);
        play (p, 6); // the switch happens inside a short fade, then latency is re-reported
        lat[os] = p.getLatencySamples();
    }
    expect (lat[0] >= 0 && lat[1] >= 0 && lat[2] >= 0, "latency reported for 2x/4x/8x");
    expect (! (lat[0] == lat[1] && lat[1] == lat[2]) || lat[0] == 0, "latency follows the oversampling setting");
}

// OVERSAMPLE is a quality setting of the instance, not part of a sound: loading a preset never
// changes it (factory presets do not store it, so it used to fall back to 4x, and the plug-in
// latency changed while the host was playing).
void testPresetsKeepOversampling()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    // Latency differs between 2x and 4x (4x and 8x round to the same samples), so the latency
    // check runs at 2x; the setting itself is checked at 2x and 8x.
    setPlain (p, pb::id::oversample, 1.0f);
    play (p, 6);
    const int latency4x = p.getLatencySamples();
    const auto& all = p.library.all();
    for (const int choice : { 0, 2 })
    {
        setPlain (p, pb::id::oversample, (float) choice);
        play (p, 6);
        const int latencyChosen = p.getLatencySamples();
        if (choice == 0)
            expect (latencyChosen != latency4x, "2x and 4x report different latencies, else the latency check is empty (2x "
                    + juce::String (latencyChosen) + ", 4x " + juce::String (latency4x) + ")");
        const juce::String name = juce::String (2 << choice) + "x";
        int kept = 0, loaded = 0, withControl = 0;
        for (int i = 0; i < (int) all.size(); ++i)
        {
            p.loadPreset (i);
            if (juce::roundToInt (rawValue (p, pb::id::oversample)) == choice) ++kept;
            // Control: the preset itself was applied.
            if (const auto it = all[(size_t) i].values.find (pb::id::cutoff); it != all[(size_t) i].values.end())
            {
                ++withControl;
                if (std::abs (rawValue (p, pb::id::cutoff) - it->second) <= 0.01f * it->second) ++loaded;
            }
        }
        expect (kept == (int) all.size(), "every preset keeps OVERSAMPLE " + name + " (" + juce::String (kept) + "/" + juce::String ((int) all.size()) + ")");
        expect (withControl > 0 && loaded == withControl, "the presets themselves were applied (CUTOFF " + juce::String (loaded) + "/" + juce::String (withControl) + ")");
        play (p, 6);
        expect (p.getLatencySamples() == latencyChosen, "latency at " + name + " unchanged by preset switches ("
                + juce::String (p.getLatencySamples()) + " vs " + juce::String (latencyChosen) + ")");
    }

    // A saved preset that does contain OVERSAMPLE does not change it either.
    pb::Snapshot saved { { pb::id::oversample, 0.0f }, { pb::id::cutoff, 500.0f } };
    pb::applySnapshot (p.state, saved);
    expect (juce::roundToInt (rawValue (p, pb::id::oversample)) == 2 && std::abs (rawValue (p, pb::id::cutoff) - 500.0f) < 5.0f,
            "a snapshot with OVERSAMPLE 2x keeps 8x but applies its CUTOFF");
}

// AMP: two plain on/off buttons instead of the MONO / LEGATO pair (Roman found it confusing, 2026-10-23):
// LEGATO ties overlapping notes, SMOOTH lets back-to-back notes continue without a new attack.
void testNoteButtons()
{
#if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    PowerBassProcessor p;
    auto button = [] (juce::AudioProcessorEditor& e, const char* id)
    {
        auto* c = e.findChildWithID (id);
        return c != nullptr && c->getNumChildComponents() == 1 ? dynamic_cast<juce::Button*> (c->getChildComponent (0)) : nullptr;
    };
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setBoundsConstrained ({ 0, 0, 1080, 720 });
        auto* legato = button (*ed, "legato");
        auto* smooth = button (*ed, "smooth");
        expect (legato != nullptr && legato->getButtonText() == "LEGATO" && smooth != nullptr && smooth->getButtonText() == "SMOOTH",
                "AMP has single LEGATO and SMOOTH buttons");
        if (legato == nullptr || smooth == nullptr)
            return;
        expect (legato->getToggleState() && ! smooth->getToggleState(), "defaults: LEGATO on, SMOOTH off (every note with its attack)");
        legato->onClick();
        smooth->onClick();
        expect (juce::roundToInt (rawValue (p, pb::id::glideMode)) == 0 && rawValue (p, pb::id::smoothStart) > 0.5f,
                "clicking switches LEGATO off and SMOOTH on");
        smooth->onClick();
        expect (rawValue (p, pb::id::smoothStart) < 0.5f, "a second click switches SMOOTH off again");
        auto* l = ed->findChildWithID ("legato");
        auto* m = ed->findChildWithID ("smooth");
        bool clear = ! l->getBounds().intersects (m->getBounds());
        for (auto* b : { l, m })
        {
            clear = clear && ed->getLocalBounds().contains (b->getBounds());
            for (const char* id : { "attack", "decay", "sustain", "release", "punch", "glide" })
                clear = clear && ! b->getBounds().intersects (ed->findChildWithID (id)->getBounds());
        }
        expect (clear, "the buttons sit apart, inside the window, off the AMP knobs");
    }
    setPlain (p, pb::id::glideMode, 1.0f);
    setPlain (p, pb::id::smoothStart, 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* legato = button (*ed, "legato");
    auto* smooth = button (*ed, "smooth");
    expect (legato != nullptr && smooth != nullptr && legato->getToggleState() && smooth->getToggleState(),
            "the buttons show their parameters (both on)");
#endif
}

// The current sound as a waveform, like PowerKick's (Roman, 2026-10-23): it shows the note PREVIEW plays,
// and a click on it plays that note - no MIDI keyboard needed.
void testWaveformView()
{
#if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    PowerBassProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setBoundsConstrained ({ 0, 0, 1080, 720 });
    auto* w = dynamic_cast<pb::ui::WaveformView*> (ed->findChildWithID ("waveform"));
    expect (w != nullptr && w->getWidth() >= ed->getWidth() * 3 / 4, "the waveform spans the top of the window");
    if (w == nullptr)
        return;
    expect (w->hasAudio(), "it shows the current sound at once");
    p.previewRequested = false;
    w->mouseDown (mouseAt (w, false));
    w->mouseUp (mouseAt (w, false));
    expect (p.previewRequested.exchange (false), "a click on the waveform plays the note");
#endif
}

// The preset browser's filter: category, any of the selected groups, and words of a search text.
void testPresetFilter()
{
    const auto& all = pb::factoryPresets();
    expect (pb::filterPresets (all, {}, {}, {}).size() == all.size(), "no filter shows every preset");
    expect (pb::filterPresets (all, "Rolling Techno Bass", {}, {}).size() == 100, "category Rolling Techno Bass: 100 presets");
    expect (pb::filterPresets (all, "Rolling Techno Bass", { "Reese" }, {}).size() == 39, "Rolling + Reese: 39 presets");
    const auto two = pb::filterPresets (all, "Dark Underground Bass", { "Reese", "FM" }, {});
    expect (two.size() == 31, "Dark Underground + Reese or FM: 23 + 8 presets (" + juce::String ((int) two.size()) + ")");
    juce::StringArray found;
    for (int i : pb::filterPresets (all, {}, {}, "  reese CORE ")) found.add (all[(size_t) i].name);
    expect (found.contains ("Reese Core"), "search finds a preset by words in any case");
    bool allMatch = ! found.isEmpty();
    for (int i : pb::filterPresets (all, {}, {}, "reese core"))
    {
        const auto& p = all[(size_t) i];
        auto has = [&p] (const char* w) { return p.name.containsIgnoreCase (w) || p.category.containsIgnoreCase (w) || p.group.containsIgnoreCase (w); };
        allMatch = allMatch && has ("reese") && has ("core");
    }
    expect (allMatch, "every search result contains every word in its name, genre or type");
    // Words also match the genre and the type: "acid fm" covers both Acid-Inspired presets of the FM type.
    const auto af = pb::filterPresets (all, {}, {}, "acid fm");
    bool covers = true;
    for (int i : pb::filterPresets (all, "Acid-Inspired Bass", { "FM" }, {}))
        covers = covers && std::find (af.begin(), af.end(), i) != af.end();
    expect (covers && ! af.empty(), "search matches the genre and the type too (" + juce::String ((int) af.size()) + " results)");
    expect (pb::filterPresets (all, {}, {}, "zzzz").empty(), "a search without matches is empty");
    expect (pb::filterPresets (all, "User", {}, {}).empty(), "User shows only the user's own presets (none among factory presets)");
    // Sorted for reading: genres in library order, names alphabetically (natural order) within a genre.
    const auto order = pb::filterPresets (all, {}, {}, {});
    bool sorted = order.size() == all.size();
    for (size_t k = 1; k < order.size(); ++k)
    {
        const auto& a = all[(size_t) order[k - 1]];
        const auto& b = all[(size_t) order[k]];
        if (a.category == b.category)
            sorted = sorted && a.name.compareNatural (b.name) <= 0;
    }
    juce::StringArray genres, libraryGenres;
    for (int i : order) if (! genres.contains (all[(size_t) i].category)) genres.add (all[(size_t) i].category);
    for (const auto& p : all) libraryGenres.addIfNotAlreadyThere (p.category);
    expect (sorted && genres == libraryGenres, "results are sorted by genre, then by name (" + genres.joinIntoString (", ") + ")");
}

void testAutomation()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    const auto r = play (p, 160, [&p] (int b)
    {
        const float t = (float) (b % 40) / 40.0f;
        setPlain (p, pb::id::cutoff, 60.0f + 8000.0f * t);
        setPlain (p, pb::id::resonance, t);
        setPlain (p, pb::id::distDrive, t);
        setPlain (p, pb::id::fmAmount, 1.0f - t);
        setPlain (p, pb::id::width, t);
        setPlain (p, pb::id::stereoDelay, 15.0f * t);
        setPlain (p, pb::id::splitFreq, 100.0f + 900.0f * t);
        setPlain (p, pb::id::master, -30.0f + 30.0f * t);
        setPlain (p, pb::id::distType, (float) (b % 5));
        setPlain (p, pb::id::filterMode, (float) (b % 4));
    });
    expect (r.finite && r.peak <= 1.0, "heavy automation stays finite and under 0 dBFS (" + juce::String (r.peak, 3) + ")");
}

void testMonoBus()
{
    PowerBassProcessor p;
    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add (juce::AudioChannelSet::stereo());   // 1.3: the sidechain input is always part of the layout
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    expect (p.setBusesLayout (mono), "mono output bus accepted (with a stereo sidechain)");
    {
        PowerBassProcessor q;
        juce::AudioProcessor::BusesLayout off;
        off.inputBuses.add (juce::AudioChannelSet::disabled());
        off.outputBuses.add (juce::AudioChannelSet::stereo());
        expect (q.setBusesLayout (off), "a disabled sidechain input is accepted");
    }
    p.prepareToPlay (48000, 512);
    const auto r = play (p, 30, {}, 1);
    expect (r.finite && r.peak > 0.05 && r.peak <= 1.0, "mono bus plays the bass (" + juce::String (r.peak, 3) + ")");
}

void testPreviewOwnership()
{
    for (bool smooth : { false, true })
        for (int scenario = 0; scenario < 7; ++scenario)
        {
            PowerBassProcessor p;
            setPlain (p, pb::id::smoothStart, smooth ? 1.0f : 0.0f);
            p.prepareToPlay (48000, 512);
            juce::AudioBuffer<float> audio (2, 512);
            double held = 0.0, tail = 0.0;
            for (int b = 0; b < 350; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0 || ((scenario == 0 || scenario == 1) && b == 10)) p.previewRequested = true;
                if (scenario == 1 && b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0);
                if (scenario >= 2 && b == (scenario >= 5 ? 37 : 5))
                {
                    if (scenario >= 3) p.previewRequested = true;
                    // At block 37 the original Preview expires at sample 255: test Panic at
                    // that exact sample, then one sample later, including a real C1 at the boundary.
                    const int panic = scenario == 5 ? 255 : scenario == 6 ? 256 : 17;
                    midi.addEvent (juce::MidiMessage::controllerEvent (1, scenario == 4 ? 123 : 120, 0), panic);
                    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), scenario == 5 ? panic : panic + 1);
                }
                if (scenario != 0 && b == 90) midi.addEvent (juce::MidiMessage::noteOff (1, 36), 33);
                p.processBlock (audio, midi);
                const double peak = std::max (audio.getMagnitude (0, 0, 512), audio.getMagnitude (1, 0, 512));
                if (b >= 75 && b < 85) held = std::max (held, peak);
                if (b >= 300) tail = std::max (tail, peak);
            }
            const auto label = juce::String ("preview ownership case ") + juce::String (scenario) + (smooth ? " SMOOTH" : " retrigger");
            if (scenario != 0) expect (held > 0.01, label + ": real C1 survives preview/panic timer");
            expect (tail < 1e-7, label + ": no ghost note after MIDI (" + juce::String (tail, 6) + ")");
        }
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    p.previewRequested = true;
    juce::AudioBuffer<float> empty (2, 0);
    juce::MidiBuffer none;
    p.processBlock (empty, none);
    expect (p.previewRequested.load(), "a zero-length block does not lose an unrendered Preview");
}

void testPreview()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    p.previewRequested = true;
    juce::AudioBuffer<float> buf (2, 512);
    double peak = 0, tail = 0;
    for (int b = 0; b < 200; ++b)
    {
        juce::MidiBuffer none;
        buf.clear();
        p.processBlock (buf, none);
        const double m = std::max (buf.getMagnitude (0, 0, 512), buf.getMagnitude (1, 0, 512));
        if (b < 40) peak = std::max (peak, m);
        if (b > 180) tail = std::max (tail, m);
    }
    expect (peak > 0.05, "PREVIEW plays a note");
    expect (tail < 1.0e-3, "PREVIEW note is released (no hanging note)");
}

// A host can flush a voice without delivering the last note-off (AU Reset / deactivation).
// This must clear the whole signal path and pending preview, independently of SMOOTH.
void testHostFlushClearsVoice()
{
    for (bool smooth : { false, true })
        for (bool deactivate : { false, true })
        {
            PowerBassProcessor p;
            setPlain (p, pb::id::smoothStart, smooth ? 1.0f : 0.0f);
            p.prepareToPlay (48000, 512);
            juce::AudioBuffer<float> buf (2, 512);
            double body = 0.0;
            for (int b = 0; b < 30; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0);
                p.processBlock (buf, midi);
                body = std::max (body, (double) buf.getMagnitude (0, 0, 512));
            }
            expect (body > 0.05, "host flush control: a held note is sounding");
            p.previewRequested = true;
            if (deactivate) p.releaseResources(); else p.reset();
            double tail = 0.0;
            for (int b = 0; b < 200; ++b)
            {
                juce::MidiBuffer midi;
                p.processBlock (buf, midi);
                tail = std::max ({ tail, (double) buf.getMagnitude (0, 0, 512),
                                       (double) buf.getMagnitude (1, 0, 512) });
            }
            expect (tail == 0.0, juce::String (deactivate ? "releaseResources" : "reset")
                                        + (smooth ? " SMOOTH" : " retrigger")
                                        + " clears voice and pending preview (peak " + juce::String (tail, 6) + ")");
            juce::MidiBuffer next;
            next.addEvent (juce::MidiMessage::noteOn (1, 43, 0.9f), 0);
            p.processBlock (buf, next);
            expect (buf.getMagnitude (0, 0, 512) > 0.01f, "a new note plays after host flush");
        }
}

// The preset browser replaces the nested preset menus: one panel, one list, filters on top.
void testPresetBrowser()
{
#if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    PowerBassProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* button = dynamic_cast<juce::Button*> (ed->findChildWithID ("presetButton"));
    auto* advanced = dynamic_cast<juce::Button*> (ed->findChildWithID ("advancedButton"));
    auto* next = dynamic_cast<juce::Button*> (ed->findChildWithID ("nextButton"));
    auto* browser = dynamic_cast<pb::ui::PresetBrowser*> (ed->findChildWithID ("presetBrowser"));
    expect (button != nullptr && advanced != nullptr && next != nullptr && browser != nullptr && ! browser->isVisible(),
            "the preset browser exists and starts closed");
    if (button == nullptr || advanced == nullptr || next == nullptr || browser == nullptr)
        return;
    button->onClick();
    expect (browser->isVisible(), "clicking the preset name opens the browser");
    expect (browser->numShown() == (int) pb::factoryPresets().size(), "the browser lists every preset at first ("
            + juce::String (browser->numShown()) + ")");
    // The list opens scrolled to the current preset; a new filter without it starts at the top.
    browser->setCategory ("Rolling Techno Bass");
    expect (browser->firstVisibleRow() == 0, "a filter without the current preset starts at the top of the list (row "
            + juce::String (browser->firstVisibleRow()) + ")");
    browser->setCategory ({});
    browser->setCategory ("Rolling Techno Bass");
    browser->toggleGroup ("Reese");
    expect (browser->numShown() == 39, "category and sound type filter the list (" + juce::String (browser->numShown()) + ")");
    const auto rows = pb::filterPresets (pb::factoryPresets(), "Rolling Techno Bass", { "Reese" }, {});
    auto nameAt = [&rows] (int r) { return pb::factoryPresets()[(size_t) rows[(size_t) r]].name; };
    p.previewRequested = false;
    browser->choose (2, false);
    expect (p.getPresetName() == nameAt (2) && browser->isVisible(), "choosing a row loads that preset and keeps the browser open");
    expect (p.previewRequested.exchange (false), "with AUDITION on, a chosen preset plays at once");
    browser->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    expect (p.getPresetName() == nameAt (3), "the down arrow loads the next preset of the list");
    browser->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    browser->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    expect (p.getPresetName() == nameAt (1), "the up arrow loads the previous preset of the list");
    browser->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    expect (! browser->isVisible(), "Escape closes the browser");
    button->onClick();
    expect (browser->numShown() == 39, "the browser remembers its filters while the window is open");
    browser->setSearch ("zzzz");
    expect (browser->numShown() == 0, "a search without matches shows an empty list");
    browser->setSearch ({});
    browser->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    expect (! browser->isVisible(), "Enter closes the browser");
    button->onClick();
    advanced->setToggleState (true, juce::sendNotification);
    expect (! browser->isVisible(), "opening ADVANCED closes the browser");
    button->onClick();
    expect (browser->isVisible() && ! advanced->getToggleState(), "opening the browser closes ADVANCED");
    browser->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    // With the browser closed, < > step through every preset in the browser's order (genre, then name).
    const auto order = pb::filterPresets (pb::factoryPresets(), {}, {}, {});
    size_t k = 0; // a place where the browser's order differs from the library order
    while (k + 1 < order.size() && order[k + 1] == order[k] + 1)
        ++k;
    p.loadPreset (order[k]);
    next->onClick();
    expect (p.getPresetName() == pb::factoryPresets()[(size_t) order[k + 1]].name, "> loads the next preset in the browser's order");
    ed.reset();
#endif
}

// Main window as a chain of blocks along the signal path: SOURCE -> FM -> FILTER on the first row,
// DRIVE -> AMP -> OUTPUT on the second, a full-width scope on top. One large primary knob per block
// carries the accent; the others are smaller and neutral. Checked at the minimum, default and maximum size.
void testBlockLayout()
{
#if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    PowerBassProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    // One primary knob per block; MASTER sits beside the waveform, as OUTPUT in PowerKick (Roman, 2026-10-23).
    const juce::StringArray primary { "shape", "fmAmount", "cutoff", "distDrive", "width" };
    const juce::StringArray secondary { "sub", "oscDrive", "oscMix", "fmRatio", "fmDecay", "resonance", "envAmount", "distTone",
                                        "distMix", "dark", "bite", "attack", "decay", "sustain", "release", "punch", "glide",
                                        "splitFreq", "stereoDelay" };
    for (auto size : { juce::Rectangle<int> (0, 0, 864, 576), juce::Rectangle<int> (0, 0, 1080, 720), juce::Rectangle<int> (0, 0, 1620, 1080) })
    {
        ed->setBoundsConstrained (size);
        const juce::String at = " at " + juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight());
        std::vector<juce::Component*> knobs;
        bool found = true;
        for (const auto& ids : { primary, secondary, juce::StringArray { "master" } })
            for (const auto& id : ids)
            {
                auto* k = ed->findChildWithID (id);
                found = found && k != nullptr;
                if (k != nullptr) knobs.push_back (k);
            }
        expect (found, "every knob is on the main window" + at);
        bool inside = true, apart = true;
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            inside = inside && ed->getLocalBounds().contains (knobs[i]->getBounds());
            for (size_t j = i + 1; j < knobs.size(); ++j)
                apart = apart && ! knobs[i]->getBounds().intersects (knobs[j]->getBounds());
        }
        expect (inside && apart, "knobs stay inside the window and never overlap" + at);
        auto dial = [&ed] (const juce::String& id)
        {
            auto* k = dynamic_cast<pb::ui::Knob*> (ed->findChildWithID (id));
            return k != nullptr ? std::min (k->slider.getWidth(), k->slider.getHeight()) : 0;
        };
        int smallest = 1 << 20, largest = 0;
        for (const auto& id : primary) smallest = std::min (smallest, dial (id));
        for (const auto& id : secondary) largest = std::max (largest, dial (id));
        expect (smallest >= largest * 3 / 2, "primary knobs are at least 1.5x the others (" + juce::String (smallest) + " vs "
                + juce::String (largest) + ")" + at);
        bool accents = true;
        for (const auto& id : primary)
            if (auto* k = dynamic_cast<pb::ui::Knob*> (ed->findChildWithID (id))) accents = accents && (bool) k->slider.getProperties()["primary"];
        for (const auto& id : secondary)
            if (auto* k = dynamic_cast<pb::ui::Knob*> (ed->findChildWithID (id))) accents = accents && ! (bool) k->slider.getProperties()["primary"];
        expect (accents, "only the primary knobs carry the accent" + at);
        auto* waveform = ed->findChildWithID ("waveform");
        auto* masterKnob = ed->findChildWithID ("master");
        expect (waveform != nullptr && waveform->getWidth() >= ed->getWidth() * 3 / 4 && waveform->getHeight() >= ed->getHeight() * 17 / 100,
                "a large waveform across the top (" + juce::String (waveform != nullptr ? waveform->getHeight() : 0) + " px high)" + at);
        expect (waveform != nullptr && masterKnob != nullptr && masterKnob->getX() >= waveform->getRight()
                && masterKnob->getBounds().getCentreY() > waveform->getY() && masterKnob->getBounds().getCentreY() < waveform->getBottom(),
                "MASTER sits right of the waveform, as OUTPUT in PowerKick" + at);
        auto centre = [&ed] (const juce::String& id) { auto* k = ed->findChildWithID (id); return k != nullptr ? k->getBounds().getCentre() : juce::Point<int>(); };
        const bool chain = centre ("shape").x < centre ("fmAmount").x && centre ("fmAmount").x < centre ("cutoff").x
                           && centre ("distDrive").x < centre ("attack").x && centre ("attack").x < centre ("width").x
                           && centre ("shape").y < centre ("distDrive").y && centre ("cutoff").y < centre ("width").y;
        expect (chain, "blocks follow the signal path: SOURCE, FM, FILTER, then DRIVE, AMP, STEREO" + at);
    }
    ed.reset();
#endif
}

void testEditor()
{
#if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    PowerBassProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    expect (ed != nullptr && ed->getWidth() == 1080 && ed->getHeight() == 720, "editor opens at 1080x720");
    ed->setBoundsConstrained ({ 0, 0, 100, 100 });
    expect (ed->getWidth() >= 864 && ed->getHeight() >= 576, "editor keeps its minimum size when the host shrinks it");
    ed.reset();
#endif
}
}

// Window snapshots for a visual check: powerbass_wrapper_tests --snapshot <dir>
int snapshot (const juce::File& dir)
{
    juce::ScopedJuceInitialiser_GUI gui;
    PowerBassProcessor p;
    p.prepareToPlay (48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    dir.createDirectory();
    auto save = [&] (const juce::String& name)
    {
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = dir.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    };
    save ("main.png");
    ed->setBoundsConstrained ({ 0, 0, 864, 576 });
    save ("main_small.png");
    ed->setBoundsConstrained ({ 0, 0, 1620, 1080 });
    save ("main_large.png");
    ed->setBoundsConstrained ({ 0, 0, 1080, 720 });
    if (auto* adv = dynamic_cast<juce::Button*> (ed->findChildWithID ("advancedButton")))
    {
        adv->setToggleState (true, juce::sendNotification);
        save ("advanced.png");
        adv->setToggleState (false, juce::sendNotification);
    }
    auto* button = dynamic_cast<juce::Button*> (ed->findChildWithID ("presetButton"));
    auto* browser = dynamic_cast<pb::ui::PresetBrowser*> (ed->findChildWithID ("presetBrowser"));
    if (button != nullptr && browser != nullptr)
    {
        button->onClick();
        save ("browser.png");
        browser->setCategory ("Rolling Techno Bass");
        browser->toggleGroup ("Reese");
        save ("browser_filtered.png");
        browser->setSearch ("nothing like this");
        save ("browser_empty.png");
        browser->setSearch ({});
        browser->setCategory ("User");
        browser->toggleGroup ("Reese");
        save ("browser_user_empty.png");
        browser->setCategory ("Dark Underground Bass");
        ed->setBoundsConstrained ({ 0, 0, 864, 576 });
        save ("browser_small.png");
    }
    ed.reset();
    return 0;
}


// DIGITAL | ANALOGUE (2026-10-28): a parameter in Digital by default, kept over preset changes like OVERSAMPLE,
// a two-button switch in the header that overlaps nothing.
// SIDECHAIN through the processor: the host's song position drives the dip, the plug-in latency is accounted for,
// presets leave it alone and an old project comes back with it off.
struct TestHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i; i.setIsPlaying (playing); i.setBpm (bpm); i.setPpqPosition (ppq); return i;
    }
};
void testSidechain()
{
    auto render = [] (float amount, bool playing, int os)
    {
        PowerBassProcessor p;
        p.prepareToPlay (48000.0, 512);
        setPlain (p, pb::id::oversample, (float) os);
        setPlain (p, pb::id::sustain, 1.0f); setPlain (p, pb::id::release, 5.0f);
        setPlain (p, pb::id::scAmount, amount);
        TestHead head; head.playing = playing;
        p.setPlayHead (&head);
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, 512);
        for (int b = 0; b < 300; ++b)   // 3.2 s at 120 BPM
        {
            head.ppq = b * 512 / 48000.0 * 2.0;
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0);
            buf.clear();
            p.processBlock (buf, midi);
            for (int i = 0; i < 512; ++i) out.push_back (std::abs (buf.getSample (0, i)) + std::abs (buf.getSample (1, i)));
        }
        p.setPlayHead (nullptr);
        return std::make_pair (out, p.getLatencySamples());
    };
    auto level = [] (const std::vector<float>& x, int from, int len)
    {
        double e = 0.0;
        for (int i = from; i < from + len; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
        return 10.0 * std::log10 (e / len + 1.0e-20);
    };
    for (int os : { 0, 2 })
    {
        const auto [dry, lat] = render (0.0f, true, os);
        const auto [wet, lat2] = render (1.0f, true, os);
        juce::ignoreUnused (lat2);
        // beat 3 of the output (ppq 2): the listener hears it at sample 48000 + latency in the host's time,
        // which is sample 48000 + latency of the plug-in's output
        const int beat = 48000 + lat;
        const double dip = level (wet, beat + 96, 384) - level (dry, beat + 96, 384);
        const double back = level (wet, beat + 19000, 2000) - level (dry, beat + 19000, 2000);
        const double before = level (wet, beat - 600, 400) - level (dry, beat - 600, 400);
        expect (dip < -20.0, "SIDECHAIN in the plug-in ducks right after the beat (" + juce::String (dip, 1) + " dB, oversampling " + juce::String (os) + ")");
        expect (std::abs (back) < 1.0 && std::abs (before) < 1.0, "SIDECHAIN in the plug-in is back before the next beat ("
                + juce::String (back, 2) + " / " + juce::String (before, 2) + " dB, oversampling " + juce::String (os) + ")");
    }
    const auto [dry, l0] = render (0.0f, false, 1);
    const auto [stopped, l1] = render (1.0f, false, 1);
    juce::ignoreUnused (l0, l1);
    double diff = 0.0;
    for (size_t i = 0; i < dry.size(); ++i) diff = std::max (diff, (double) std::abs (dry[i] - stopped[i]));
    expect (diff < 1.0e-6, "SIDECHAIN does nothing while the host is stopped (" + juce::String (diff, 7) + ")");
    // a preset keeps the sidechain; an old project brings it back off
    PowerBassProcessor p;
    setPlain (p, pb::id::scAmount, 0.7f); setPlain (p, pb::id::scShape, 2.0f);
    p.loadPreset (3);
    expect (std::abs (rawValue (p, pb::id::scAmount) - 0.7f) < 1.0e-3 && juce::roundToInt (rawValue (p, pb::id::scShape)) == 2,
            "loading a preset keeps SIDECHAIN and its curve");
    juce::MemoryBlock mb; p.getStateInformation (mb);
    auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    for (const char* pid : { pb::id::scAmount, pb::id::scRate, pb::id::scShape })
        if (auto* e = xml->getChildByAttribute ("id", pid)) xml->removeChildElement (e, true);
    juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
    PowerBassProcessor q;
    setPlain (q, pb::id::scAmount, 0.5f);
    q.setStateInformation (old.getData(), (int) old.getSize());
    expect (rawValue (q, pb::id::scAmount) < 1.0e-6, "a project from before SIDECHAIN opens with it off");
}

// SIDECHAIN KICK through the processor: the kick arrives on the sidechain input in the same buffer the bass is written
// to; the bass ducks after each hit (latency accounted), the kick never leaks into the output.
void testSidechainKickInput()
{
    PowerBassProcessor p;
    expect (p.getBusCount (true) == 1 && p.getChannelCountOfBus (true, 0) == 2, "PowerBass has a stereo sidechain input");
    p.prepareToPlay (48000.0, 512);
    setPlain (p, pb::id::sustain, 1.0f); setPlain (p, pb::id::release, 5.0f);
    setPlain (p, pb::id::scAmount, 1.0f); setPlain (p, pb::id::scSource, 1.0f);
    const int lat = p.getLatencySamples();
    std::vector<float> out, kickOnly;
    juce::AudioBuffer<float> buf (2, 512);
    for (int b = 0; b < 200; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0);
        for (int i = 0; i < 512; ++i)
        {
            const int t = b * 512 + i, since = t - 48000;   // one kick at 1.0 s
            const float k = since >= 0 && since < 12000 ? (float) (0.9 * std::exp (-since / 2880.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * since / 48000.0)) : 0.0f;
            buf.setSample (0, i, k); buf.setSample (1, i, k);
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < 512; ++i) out.push_back (std::abs (buf.getSample (0, i)) + std::abs (buf.getSample (1, i)));
    }
    auto level = [&] (int from, int len) { double e = 0; for (int i = from; i < from + len; ++i) e += (double) out[(size_t) i] * out[(size_t) i]; return 10.0 * std::log10 (e / len + 1e-20); };
    const double before = level (48000 + lat - 4800, 2400), dip = level (48000 + lat + 480, 480), after = level (48000 + lat + 24000, 2400);
    expect (dip < before - 20.0, "SIDECHAIN KICK in the plug-in: the bass ducks after the kick on the input (" + juce::String (dip - before, 1) + " dB)");
    expect (std::abs (after - before) < 1.0, "SIDECHAIN KICK in the plug-in: back half a second later (" + juce::String (after - before, 2) + " dB)");
    // the kick alone, no note: nothing comes out (the input is not passed through)
    PowerBassProcessor q; q.prepareToPlay (48000.0, 512);
    double leak = 0.0;
    for (int b = 0; b < 20; ++b)
    {
        for (int i = 0; i < 512; ++i) { buf.setSample (0, i, 0.5f); buf.setSample (1, i, -0.5f); }
        juce::MidiBuffer none; q.processBlock (buf, none);
        leak = std::max (leak, (double) buf.getMagnitude (0, 512));
    }
    expect (leak < 1.0e-6, "the sidechain input never reaches the output (" + juce::String (leak, 7) + ")");
}

void testAnalogueSwitch()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000.0, 512);
    auto* prm = p.state.getParameter (pb::id::analog);
    expect (prm != nullptr && prm->getValue() == 0.0f, "DIGITAL | ANALOGUE exists and starts in DIGITAL");
    if (prm == nullptr) return;
    setPlain (p, pb::id::analog, 1.0f);
    const auto& all = p.library.all();
    int kept = 0, tried = 0;
    for (int i = 0; i < (int) all.size(); i += 29, ++tried)
    {
        p.loadPreset (i);
        kept += p.state.getRawParameterValue (pb::id::analog)->load() > 0.5f ? 1 : 0;
    }
    expect (kept == tried, "a preset change keeps ANALOGUE (" + juce::String (kept) + " of " + juce::String (tried) + ")");
    setPlain (p, pb::id::analog, 0.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (1080, 720);
    auto* seg = ed->findChildWithID ("analog");
    expect (seg != nullptr && seg->getNumChildComponents() == 2 && ed->getLocalBounds().contains (seg->getBounds()),
            "the header shows DIGITAL | ANALOGUE");
    if (seg == nullptr || seg->getNumChildComponents() != 2) return;
    bool clear = true;
    for (auto* c : ed->getChildren())
        if (c != seg && c->isVisible() && c->getBounds().intersects (seg->getBounds()))
        {
            clear = false;
            std::cout << "  overlaps " << c->getComponentID() << "\n";
        }
    expect (clear, "DIGITAL | ANALOGUE overlaps no other control");
    auto* analogueButton = dynamic_cast<juce::Button*> (seg->getChildComponent (1));
    if (analogueButton != nullptr && analogueButton->onClick != nullptr) analogueButton->onClick();
    expect (p.state.getRawParameterValue (pb::id::analog)->load() > 0.5f, "the ANALOGUE button switches the sound to ANALOGUE");
}

// Favourites (2026-10-29). A temporary preset folder, never the user's own; left in the system temp folder.
juce::File tempPresetDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("PowerBassFavouritesTest-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
    dir.createDirectory();
    return dir;
}
juce::StringArray starredNames (const pb::PresetLibrary& l)
{
    juce::StringArray out;
    for (int i : pb::filterPresets (l.all(), pb::favouritesCategory, {}, {}))
        out.add (l.all()[(size_t) i].name + (l.all()[(size_t) i].isFactory() ? "" : " (user)"));
    return out;
}
int userIndex (const pb::PresetLibrary& l, const juce::String& name)
{
    for (int i = 0; i < (int) l.all().size(); ++i)
        if (! l.all()[(size_t) i].isFactory() && l.all()[(size_t) i].name == name)
            return i;
    return -1;
}

void testFavourites()
{
    const auto dir = tempPresetDir();
    auto trashed = dir.getChildFile ("trashed");
    trashed.createDirectory();
    auto fakeTrash = [trashed] (const juce::File& f) { return f.moveFileTo (trashed.getChildFile (f.getFileName())); };
    juce::String e;
    pb::PresetLibrary a (dir);
    a.moveToTrash = fakeTrash;
    expect (starredNames (a).isEmpty(), "no favourites in a new preset folder");
    const juce::String shared = a.all()[3].name; // a factory name a user preset will share
    a.saveUser (shared, a.all()[3].values, e);
    a.saveUser ("My Lead", a.all()[0].values, e);
    const bool on = a.toggleFavourite (3);
    expect (on && a.all()[3].favourite && ! a.all()[(size_t) userIndex (a, shared)].favourite,
            "a star on a factory preset leaves the user preset of the same name unstarred");

    pb::PresetLibrary b (dir); // another instance, or another project
    b.moveToTrash = fakeTrash;
    expect (b.all()[3].favourite, "the star shows in another instance");
    b.toggleFavourite (userIndex (b, "My Lead"));
    a.rescan();
    expect (starredNames (a) == juce::StringArray (shared, "My Lead (user)"),
            "Favourites lists the starred presets in library order, both instances' stars kept (" + starredNames (a).joinIntoString (", ") + ")");
    expect (pb::filterPresets (a.all(), pb::favouritesCategory, {}, "my lead").size() == 1, "search works inside Favourites");

    expect (a.rename (userIndex (a, "My Lead"), "Lead X", e) && a.all()[(size_t) userIndex (a, "Lead X")].favourite,
            "a renamed user preset keeps its star");
    b.rescan();
    expect (starredNames (b).contains ("Lead X (user)") && ! starredNames (b).contains ("My Lead (user)"), "the rename reaches other instances");
    expect (a.remove (userIndex (a, "Lead X"), e), "a starred user preset can be deleted: " + e);
    a.saveUser ("Lead X", a.all()[0].values, e); // a new preset of that name starts without a star
    expect (! a.all()[(size_t) userIndex (a, "Lead X")].favourite && starredNames (a) == juce::StringArray (shared),
            "a deleted preset loses its star (" + starredNames (a).joinIntoString (", ") + ")");
    expect (! a.toggleFavourite (3) && ! a.all()[3].favourite && starredNames (a).isEmpty(), "a second click removes the star");

    a.toggleFavourite (5);
    a.favouritesFile().replaceWithText ("{ not json at all");
    pb::PresetLibrary c (dir);
    expect (starredNames (c).isEmpty() && c.all().size() == a.all().size(), "a damaged favourites file loads as no stars, every preset still there");
    c.toggleFavourite (7);
    pb::PresetLibrary d (dir);
    expect (d.all()[7].favourite && starredNames (d).size() == 1, "after a damaged file the next star writes a good one");
}

// The browser: the star at the left of a row stars that preset without loading it; the rest of the row loads it.
// Favourites is a category of the browser; the header star stars the current preset.
void testFavouritesInEditor()
{
    PowerBassProcessor p;
    p.prepareToPlay (48000.0, 512);
    p.library = pb::PresetLibrary (tempPresetDir());
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (1080, 720);
    auto* star = dynamic_cast<pb::ui::StarButton*> (ed->findChildWithID ("favouriteButton"));
    expect (star != nullptr, "the header has a star for the current preset");
    if (star == nullptr)
        return;
    const auto current = p.getPresetName();
    star->onClick();
    const int ci = p.library.indexOf (current);
    expect (ci >= 0 && p.library.all()[(size_t) ci].favourite && star->isStarred(), "the header star stars the current preset");

    auto* presetButton = dynamic_cast<juce::Button*> (ed->findChildWithID ("presetButton"));
    presetButton->onClick();
    auto* browser = dynamic_cast<pb::ui::PresetBrowser*> (ed->findChildWithID ("presetBrowser"));
    auto* list = browser != nullptr ? dynamic_cast<juce::ListBox*> (browser->findChildWithID ("presetList")) : nullptr;
    expect (list != nullptr && browser->isVisible(), "the browser opens with its list");
    if (list == nullptr)
        return;
    browser->setCategory ({});
    // A row on screen (the list scrolls to the current preset) that is not the current preset.
    const auto order = pb::filterPresets (p.library.all(), {}, {}, {});
    int row = browser->firstVisibleRow();
    while (row < browser->numShown() - 1 && p.library.all()[(size_t) order[(size_t) row]].name == current)
        ++row;
    auto* spot = list->getComponentForRowNumber (row); // the row's own component: the star's click target
    auto* rowComp = spot != nullptr ? spot->getParentComponent() : nullptr;
    expect (rowComp != nullptr, "row " + juce::String (row) + " is on screen with its star (list " + list->getBounds().toString() + ", "
            + juce::String (list->getNumRowsOnScreen()) + " rows on screen)");
    if (rowComp == nullptr)
        return;
    const int h = rowComp->getHeight();
    auto* atStar = rowComp->getComponentAt (juce::roundToInt (12 * ed->getWidth() / 1080.0f), h / 2);
    auto* atName = rowComp->getComponentAt (rowComp->getWidth() / 2, h / 2);
    expect (atStar != nullptr && atStar != rowComp && atName == rowComp, "the star is a click target of its own; the name belongs to the row");
    const int preset = order[(size_t) row];
    if (atStar != nullptr && atStar != rowComp)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const juce::Point<float> at (12.0f, (float) h / 2);
        const juce::MouseEvent click (src, at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, atStar, atStar, juce::Time::getCurrentTime(), at,
                                      juce::Time::getCurrentTime(), 1, false);
        atStar->mouseDown (click);
    }
    expect (p.library.all()[(size_t) preset].favourite, "clicking the star of a row stars that preset");
    expect (p.getPresetName() == current, "starring a row does not load it (still " + p.getPresetName() + ")");
    browser->setCategory (pb::favouritesCategory);
    expect (browser->numShown() == 2, "the Favourites category shows the two starred presets (" + juce::String (browser->numShown()) + ")");
}

int main (int argc, char** argv)
{
    if (argc > 1 && juce::String (argv[1]) == "--preview-ownership-check")
    {
        testPreviewOwnership();
        std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " preview ownership checks\n";
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && juce::String (argv[1]) == "--host-flush-check")
    {
        testHostFlushClearsVoice();
        std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " host flush checks\n";
        return failures == 0 ? 0 : 1;
    }
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return snapshot (juce::File (juce::String::fromUTF8 (argv[2])));
    testFavourites();
    testDefaultsMatchDefaultPreset();
    testFactoryPresetsLoadAsMeasured();
    testStateRoundTrip();
    testLegacyProjectMigration();
    testPresetSwitchingWhilePlaying();
    testReprepareMatchesFresh();
    testLatencyAvailableBeforeFirstBlock();
    testStateRestoredAfterPrepareStartsExact();
    testOversamplingLatency();
    testPresetsKeepOversampling();
    testAnalogueSwitch();
    testSidechain();
    testSidechainKickInput();
    testPresetFilter();
    testAutomation();
    testMonoBus();
    testPreview();
    testPreviewOwnership();
    testHostFlushClearsVoice();
    testEditor();
    testFavouritesInEditor();
    testBlockLayout();
    testWaveformView();
    testNoteButtons();
    testPresetBrowser();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " wrapper checks\n";
    return failures == 0 ? 0 : 1;
}
