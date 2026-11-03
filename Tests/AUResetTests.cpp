// Native CoreAudio regression for MIDI queued at a transport reset. No audio device or DAW edits.
// No argument: installed AU. Optional argument: directly load a candidate .component in this process.
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void check (OSStatus s, const char* where)
{
    if (s != noErr) throw std::runtime_error (std::string (where) + ": " + std::to_string (s));
}

static void setSwitch (AudioUnit unit, const std::string& name, int value)
{
    UInt32 bytes = 0; Boolean writable = false;
    check (AudioUnitGetPropertyInfo (unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &bytes, &writable), "parameter size");
    std::vector<AudioUnitParameterID> ids (bytes / sizeof (AudioUnitParameterID));
    check (AudioUnitGetProperty (unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &bytes), "parameter list");
    for (auto id : ids)
    {
        AudioUnitParameterInfo info {}; bytes = sizeof (info);
        check (AudioUnitGetProperty (unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &bytes), "parameter info");
        char text[512] {};
        if (info.cfNameString) CFStringGetCString (info.cfNameString, text, sizeof (text), kCFStringEncodingUTF8);
        else std::strncpy (text, info.name, sizeof (text) - 1);
        if (info.cfNameString && (info.flags & kAudioUnitParameterFlag_CFNameRelease)) CFRelease (info.cfNameString);
        if (name != text) continue;
        check (AudioUnitSetParameter (unit, id, kAudioUnitScope_Global, 0, (float) value, 0), "set switch");
        AudioUnitParameterValue got = -1;
        check (AudioUnitGetParameter (unit, id, kAudioUnitScope_Global, 0, &got), "read switch");
        if (got != (float) value) throw std::runtime_error ("switch readback " + name);
        return;
    }
    throw std::runtime_error ("missing switch " + name);
}

int main (int argc, char** argv)
{
    try
    {
        AudioComponentDescription desc { kAudioUnitType_MusicDevice, 'Pwbs', 'Mddy', 0, 0 };
        auto component = AudioComponentFindNext (nullptr, &desc);
        CFBundleRef bundle = nullptr;
        if (argc > 1)
        {
            auto url = CFURLCreateFromFileSystemRepresentation (nullptr, reinterpret_cast<const UInt8*> (argv[1]),
                                                                std::strlen (argv[1]), true);
            bundle = CFBundleCreate (nullptr, url); CFRelease (url);
            if (bundle == nullptr || ! CFBundleLoadExecutable (bundle)) throw std::runtime_error ("load candidate");
            auto factory = reinterpret_cast<AudioComponentFactoryFunction> (
                CFBundleGetFunctionPointerForName (bundle, CFSTR ("PowerBassAUFactory")));
            if (factory == nullptr) throw std::runtime_error ("candidate factory");
            component = AudioComponentRegister (&desc, CFSTR ("PowerBass reset regression"), 0x10100, factory);
        }
        if (component == nullptr) throw std::runtime_error ("PowerBass component not found");

        int failures = 0, checks = 0;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
          for (int smooth : { 0, 1 })
           for (int analog : { 0, 1 })
            for (int scenario = 0; scenario < 5; ++scenario)
            {
                AudioUnit unit = nullptr;
                check (AudioComponentInstanceNew (component, &unit), "instantiate");
                AudioStreamBasicDescription fmt {};
                fmt.mSampleRate = sr; fmt.mFormatID = kAudioFormatLinearPCM;
                fmt.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
                fmt.mBytesPerPacket = fmt.mBytesPerFrame = 4; fmt.mFramesPerPacket = 1;
                fmt.mChannelsPerFrame = 2; fmt.mBitsPerChannel = 32;
                check (AudioUnitSetProperty (unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
                                             0, &fmt, sizeof (fmt)), "format");
                UInt32 maxFrames = 512;
                check (AudioUnitSetProperty (unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global,
                                             0, &maxFrames, sizeof (maxFrames)), "frames");
                check (AudioUnitInitialize (unit), "initialize");
                setSwitch (unit, "Smooth Start", smooth);
                setSwitch (unit, "Digital/Analogue", analog);

                // 0: queued on -> reset, 1: played on -> reset, 2: reset -> new on,
                // 3: paired on/off, 4: on -> reset -> a different new on/off.
                if (scenario != 2)
                    check (MusicDeviceMIDIEvent (unit, 0x90, 36, 100, 0), "initial on");
                if (scenario == 0 || scenario == 2 || scenario == 4)
                    check (AudioUnitReset (unit, kAudioUnitScope_Global, 0), "queued reset");
                if (scenario == 2 || scenario == 4)
                    check (MusicDeviceMIDIEvent (unit, 0x90, 43, 100, 0), "new on after reset");

                constexpr int block = 173;
                std::vector<float> left (block), right (block);
                double body = 0.0, tail = 0.0;
                bool finite = true;
                for (int pos = 0; pos < (int) (4 * sr); pos += block)
                {
                    const int n = std::min (block, (int) (4 * sr) - pos);
                    const int offAt = (int) (.4 * sr);
                    if (pos <= offAt && pos + n > offAt)
                    {
                        if (scenario == 1) check (AudioUnitReset (unit, kAudioUnitScope_Global, 0), "played reset");
                        if (scenario == 3 || scenario == 4)
                            check (MusicDeviceMIDIEvent (unit, 0x80, scenario == 3 ? 36 : 43, 0, offAt - pos), "off");
                    }
                    struct { UInt32 count; AudioBuffer buffers[2]; } audio;
                    audio.count = 2;
                    audio.buffers[0] = { 1, (UInt32) n * 4, left.data() };
                    audio.buffers[1] = { 1, (UInt32) n * 4, right.data() };
                    AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = pos;
                    AudioUnitRenderActionFlags flags = 0;
                    check (AudioUnitRender (unit, &flags, &ts, 0, n, reinterpret_cast<AudioBufferList*> (&audio)), "render");
                    for (int i = 0; i < n; ++i)
                    {
                        const double v = std::max (std::abs (left[i]), std::abs (right[i]));
                        finite = finite && std::isfinite (v);
                        if (pos + i < (int) (.3 * sr)) body = std::max (body, v);
                        if (pos + i > (int) (3 * sr)) tail = std::max (tail, v);
                    }
                }
                const bool shouldHold = scenario == 2;
                const bool ok = finite && (shouldHold ? tail > .01 : tail < 1e-7)
                                && (scenario == 0 ? body < 1e-7 : body > .01);
                ++checks; if (! ok) ++failures;
                std::cout << "{\"sr\":" << sr << ",\"smooth\":" << smooth << ",\"analogue\":" << analog << ",\"case\":" << scenario << ",\"body\":" << body
                          << ",\"tail\":" << tail << ",\"pass\":" << (ok ? "true" : "false") << "}\n";
                check (AudioUnitUninitialize (unit), "uninitialize");
                check (AudioComponentInstanceDispose (unit), "dispose");
            }
        std::cout << (failures == 0 ? "PASS " : "FAILED ") << checks - failures << "/" << checks << " AU reset checks\n";
        if (bundle != nullptr) CFRelease (bundle);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 2; }
}
