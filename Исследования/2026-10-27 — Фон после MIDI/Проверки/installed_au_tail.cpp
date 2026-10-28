// Offline CoreAudio probe of the installed PowerBass. No audio device, project or preset writes.
#include <AudioToolbox/AudioToolbox.h>
static AudioComponent candidate = nullptr;
static AudioComponent findComponent (AudioComponent previous, const AudioComponentDescription* desc)
{
    return candidate != nullptr && desc->componentSubType == 'Pwbs' ? candidate : AudioComponentFindNext(previous,desc);
}
#define AudioComponentFindNext findComponent
#define main old_probe_main
#include "../../2026-10-22 — Проверка задержки/Host/au_probe.cpp"
#undef main
#undef AudioComponentFindNext
#include <cstring>

static void set (AU& a, const std::string& name, float value)
{
    UInt32 size=0; Boolean writable=false;
    check(AudioUnitGetPropertyInfo(a.u,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,&size,&writable),"list size");
    std::vector<AudioUnitParameterID> ids(size/sizeof(AudioUnitParameterID));
    check(AudioUnitGetProperty(a.u,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,ids.data(),&size),"list");
    for(auto id:ids) {
        AudioUnitParameterInfo info{}; size=sizeof(info);
        check(AudioUnitGetProperty(a.u,kAudioUnitProperty_ParameterInfo,kAudioUnitScope_Global,id,&info,&size),"info");
        char text[512]{};
        if(info.cfNameString) CFStringGetCString(info.cfNameString,text,sizeof(text),kCFStringEncodingUTF8);
        else std::strncpy(text,info.name,sizeof(text)-1);
        if(info.cfNameString && (info.flags&kAudioUnitParameterFlag_CFNameRelease)) CFRelease(info.cfNameString);
        if(name==text) {
            check(AudioUnitSetParameter(a.u,id,kAudioUnitScope_Global,0,value,0),"set");
            AudioUnitParameterValue got=0;
            check(AudioUnitGetParameter(a.u,id,kAudioUnitScope_Global,0,&got),"get");
            if(std::abs(got-value)>1e-5) throw std::runtime_error("parameter readback "+name);
            return;
        }
    }
    throw std::runtime_error("missing parameter "+name);
}

int main(int argc,char** argv)
{
    try {
        // Register only inside this probe process; no installed component or system cache changes.
        if(argc>1) {
            auto url=CFURLCreateFromFileSystemRepresentation(nullptr,reinterpret_cast<const UInt8*>(argv[1]),std::strlen(argv[1]),true);
            auto bundle=CFBundleCreate(nullptr,url);CFRelease(url);
            if(bundle==nullptr || !CFBundleLoadExecutable(bundle)) throw std::runtime_error("cannot load candidate bundle");
            auto factory=reinterpret_cast<AudioComponentFactoryFunction>(CFBundleGetFunctionPointerForName(bundle,CFSTR("PowerBassAUFactory")));
            if(factory==nullptr) throw std::runtime_error("candidate factory not found");
            AudioComponentDescription desc{kAudioUnitType_MusicDevice,'Pwbs','Mddy',0,0};
            candidate=AudioComponentRegister(&desc,CFSTR("PowerBass candidate offline"),65536,factory);
            if(candidate==nullptr) throw std::runtime_error("cannot register candidate in probe process");
            // Keep the factory's bundle loaded until all AudioUnit instances have been disposed.
        }
        for(double sr:{44100.,48000.,96000.})
            for(int smooth:{0,1})
                for(int scenario:{0,1,2}) {
                    AU a('Pwbs',sr);
                    set(a,"Smooth Start",(float)smooth);
                    struct Event { double t; int status,key; };
                    const std::vector<Event> phrase = scenario==1
                        ? std::vector<Event>{{0,0x90,36},{.2,0x90,36},{.201,0x80,36},{.4,0x90,43},
                                             {.401,0x80,36},{.6,0x90,39},{.601,0x80,43},{.801,0x80,39}}
                        : std::vector<Event>{{0,0x90,36},{.5,0x80,36}};
                    double tail=0,peak=0,initial=0;
                    bool finite=true;
                    const int total=(int)(sr*8),block=173;
                    std::vector<float> left(block),right(block);
                    struct {UInt32 count;AudioBuffer b[2];} abl;
                    for(int pos=0;pos<total;pos+=block) {
                        const int n=std::min(block,total-pos);
                        for(auto e:phrase) {
                            const int at=(int)(e.t*sr);
                            if(at>=pos && at<pos+n && !(scenario==2 && e.status==0x80))
                                check(MusicDeviceMIDIEvent(a.u,e.status,e.key,e.status==0x90?100:0,at-pos),"MIDI");
                        }
                        // A host panic/reset without note-off must clear a previously sounding voice.
                        if(scenario==2 && pos<=(int)(.5*sr) && pos+n>(int)(.5*sr))
                            check(AudioUnitReset(a.u,kAudioUnitScope_Global,0),"reset");
                        abl.count=2;abl.b[0]={1,(UInt32)n*4,left.data()};abl.b[1]={1,(UInt32)n*4,right.data()};
                        AudioTimeStamp ts{};ts.mFlags=kAudioTimeStampSampleTimeValid;ts.mSampleTime=pos;
                        AudioUnitRenderActionFlags flags=0;
                        check(AudioUnitRender(a.u,&flags,&ts,0,n,reinterpret_cast<AudioBufferList*>(&abl)),"render");
                        for(int i=0;i<n;++i) {
                            const double v=std::max(std::abs(left[i]),std::abs(right[i]));
                            finite &= std::isfinite(v);peak=std::max(peak,v);
                            if(pos+i<(int)(sr*.4))initial=std::max(initial,v);
                            if(pos+i>(int)(sr*6))tail=std::max(tail,v);
                        }
                    }
                    std::cout<<std::setprecision(12)<<"{\"sr\":"<<sr<<",\"smooth\":"<<smooth<<",\"scenario\":"<<scenario
                             <<",\"body_peak\":"<<initial<<",\"tail_peak\":"<<tail<<",\"finite\":"<<(finite?"true":"false")<<"}\n";
                }
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 2;}
}
