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

// The preset browser's filter: category, any of the selected groups, and words of a search text.
void testPresetFilter()
{
    const auto& all = pb::factoryPresets();
    expect (pb::filterPresets (all, {}, {}, {}).size() == all.size(), "no filter shows every preset");
    expect (pb::filterPresets (all, "Rolling Techno Bass", {}, {}).size() == 100, "category Rolling Techno Bass: 100 presets");
    expect (pb::filterPresets (all, "Rolling Techno Bass", { "Reese" }, {}).size() == 39, "Rolling + Reese: 39 presets");
    const auto two = pb::filterPresets (all, "Dark Underground Bass", { "Reese", "FM" }, {});
    expect (two.size() == 29, "Dark Underground + Reese or FM: 21 + 8 presets (" + juce::String ((int) two.size()) + ")");
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
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    expect (p.setBusesLayout (mono), "mono output bus accepted");
    p.prepareToPlay (48000, 512);
    const auto r = play (p, 30, {}, 1);
    expect (r.finite && r.peak > 0.05 && r.peak <= 1.0, "mono bus plays the bass (" + juce::String (r.peak, 3) + ")");
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

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return snapshot (juce::File (juce::String::fromUTF8 (argv[2])));
    testDefaultsMatchDefaultPreset();
    testFactoryPresetsLoadAsMeasured();
    testStateRoundTrip();
    testLegacyProjectMigration();
    testPresetSwitchingWhilePlaying();
    testReprepareMatchesFresh();
    testLatencyAvailableBeforeFirstBlock();
    testStateRestoredAfterPrepareStartsExact();
    testOversamplingLatency();
    testPresetFilter();
    testAutomation();
    testMonoBus();
    testPreview();
    testEditor();
    testPresetBrowser();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " wrapper checks\n";
    return failures == 0 ? 0 : 1;
}
