extern "C" { int _fltused=0; }
#include "BandRanges.h"
extern "C" void* original_path(void*,void*,const void*,double) noexcept;
extern "C" double original_cos(double) noexcept;
extern "C" double original_pow(double,double) noexcept;
extern "C" void original_prepare(void*,double,const double*) noexcept;
extern "C" void original_meter_prepare(void*,double) noexcept;
extern "C" void* native_get_module(const unsigned short*) noexcept;
extern "C" void* native_get_proc(void*,const char*) noexcept;

static double maximum(double a,double b) noexcept{return a>b?a:b;}
static double minimum(double a,double b) noexcept{return a<b?a:b;}
static double clamp(double x,double a,double b) noexcept{return minimum(maximum(x,a),b);}
static double scalar(void*p,unsigned offset) noexcept{return *reinterpret_cast<double*>(static_cast<unsigned char*>(p)+offset);}
struct Frame {double full[2],sub[2],bass[2],remainder[2],subSolo,bassSolo,generated;};
struct Routed {double audio[2],sub[2],bass[2],generated;};
struct State {
    double bands[2][4],analysis[4],subWeight,bassWeight,pathWeight,wet;
    double raw[4];bool available;unsigned char padding[7];double rate;
    onyx_update::FourthOrder subLowpass;double bassNorm,bassGain;
};
static_assert(__builtin_offsetof(State,raw)==128,"Input capture offsets");
static_assert(__builtin_offsetof(State,available)==160,"Input availability offset");
static_assert(sizeof(State)==304,"Audited retired-history footprint");
static State& state(void*p) noexcept{return *reinterpret_cast<State*>(static_cast<unsigned char*>(p)+0x36538);}

static void prepare_ranges(void*engine,double rate) noexcept {
    auto&s=state(engine);s.rate=rate;
    for(unsigned c=0;c<2;++c){
        reinterpret_cast<onyx_update::SourceBands*>(static_cast<unsigned char*>(engine)+0xd68+c*0x77a0)->setup(rate);
    }
    s.bassNorm=-1.;s.bassGain=1.;
    s.subLowpass.setup(rate,onyx_update::crossoverHz);
    *reinterpret_cast<onyx_update::FourthOrder*>(static_cast<unsigned char*>(engine)+0x49be0)=s.subLowpass;
}
extern "C" double source_update(void*engine,unsigned channel,double,double coloration) noexcept {
    auto&s=state(engine);const double rate=scalar(engine,0x49db0);
    if(s.rate!=rate)prepare_ranges(engine,rate);
    auto*subLowpass=reinterpret_cast<onyx_update::FourthOrder*>(static_cast<unsigned char*>(engine)+0x49be0);
    if(subLowpass->sections[0].b0!=s.subLowpass.sections[0].b0)*subLowpass=s.subLowpass;
    const unsigned c=channel&1u;
    auto clean=[](double x)noexcept{return __builtin_isfinite(x)?clamp(x,-1.e6,1.e6):0.;};
    double selected=clean(s.raw[c]);
    const double external=s.available?clean(s.raw[2+c]):0.,mix=scalar(engine,0x49c98);
    if(mix==1.)selected=external;else if(mix>0.)selected+=mix*(external-selected);
    // The new BASS gain belongs to the 60-150 Hz lane. Retain the native tube
    // residual, but remove the old pre-split BASS EQ so it cannot also change SUB.
    return selected+coloration;
}

extern "C" void frame_update(void*engine,Frame*f,const double*) noexcept{
    auto&s=state(engine);auto*base=static_cast<unsigned char*>(engine);
    const double rate=scalar(engine,0x49db0);
    if(s.rate!=rate)prepare_ranges(engine,rate);
    const double norm=scalar(engine,0x49c50);
    if(norm!=s.bassNorm){s.bassGain=norm==.5?1.:original_pow(10.,(60.*norm-30.)/20.);s.bassNorm=norm;}
    for(unsigned c=0;c<2;++c){
        f->bass[c]*=s.bassGain;
        // One permanent, coherent crossover sum. No alternate all-pass path
        // is introduced when a knob or SOLO switch changes.
        f->full[c]=f->sub[c]+f->bass[c]+f->remainder[c];
    }
    const double normal=1.-maximum(f->subSolo,f->bassSolo);
    s.subWeight=normal+f->subSolo;s.bassWeight=normal+f->bassSolo;
    const unsigned remaining=*reinterpret_cast<unsigned*>(base+0x41540),total=*reinterpret_cast<unsigned*>(base+0x41544);
    s.pathWeight=remaining&&total?.5+.5*original_cos(3.14159265358979323846*double(remaining)/double(total)):1.;
    double fade=clamp((double(*reinterpret_cast<unsigned*>(base+0x49e80))/rate-1.75)*4.,0.,1.);
    fade=fade==1.?0.:.5+.5*original_cos(3.14159265358979323846*fade);
    s.wet=(1.-scalar(engine,0x49ca0))*fade;
}
extern "C" void* path_update(void*path,Routed*out,const Frame*f,double mix,void*engine) noexcept{
    Frame ordinary=*f;ordinary.subSolo=ordinary.bassSolo=0.;
    original_path(path,out,&ordinary,mix);
    auto&s=state(engine);
    const unsigned index=unsigned((reinterpret_cast<unsigned long long>(path)-reinterpret_cast<unsigned long long>(engine)-0x36478ull)/0x60ull)&1u;
    const double normal=1.-maximum(f->subSolo,f->bassSolo);
    for(unsigned c=0;c<2;++c){
        s.bands[index][c]=out->sub[c];s.bands[index][2+c]=out->bass[c];
        if(f->subSolo!=0.||f->bassSolo!=0.)
            out->audio[c]=normal*out->audio[c]+f->subSolo*out->sub[c]+f->bassSolo*out->bass[c];
        out->sub[c]*=s.subWeight;out->bass[c]*=s.bassWeight;
    }
    out->generated*=s.subWeight;
    return out;
}
extern "C" void capture_analysis(void*engine,const double*output) noexcept{
    auto&s=state(engine);const unsigned index=*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(engine)+0x41538)&1u;
    for(unsigned j=0;j<4;++j){
        const double weight=j<2?s.subWeight:s.bassWeight;
        if(weight>1.e-8)s.analysis[j]=output[2+j]/weight;
        else s.analysis[j]=s.wet*(s.bands[1-index][j]+s.pathWeight*(s.bands[index][j]-s.bands[1-index][j]));
    }
}
extern "C" void* retired_rms(void*meter,double*out,double*sub,double*bass) noexcept{
    auto&s=state(static_cast<unsigned char*>(meter)-0x10bd0);
    for(unsigned c=0;c<2;++c){sub[c]=s.analysis[c];bass[c]=s.analysis[2+c];}
    out[0]=out[1]=0.;return out;
}
using Discard=unsigned long(*)(void*,unsigned long long);
static Discard get_discard() noexcept{
    static const unsigned short kernel[]={107,101,114,110,101,108,51,50,46,100,108,108,0};
    void*module=native_get_module(kernel);if(!module)return nullptr;
    return reinterpret_cast<Discard>(native_get_proc(module,"DiscardVirtualMemory"));
}
static void discard_interior(Discard discard,unsigned long long first,unsigned long long last) noexcept{
    first=(first+4095ull)&~4095ull;last&=~4095ull;
    if(last>first)discard(reinterpret_cast<void*>(first),last-first);
}
extern "C" void prepare_compact(void*engine,double rate,const double*parameters) noexcept{
    original_prepare(engine,rate,parameters);
    prepare_ranges(engine,scalar(engine,0x49db0));
    auto&s=state(engine);for(unsigned i=0;i<2;++i)for(unsigned j=0;j<4;++j)s.bands[i][j]=0.;
    for(unsigned j=0;j<4;++j)s.analysis[j]=0.;s.subWeight=s.bassWeight=s.pathWeight=s.wet=1.;
    if(auto discard=get_discard()){
        const auto base=reinterpret_cast<unsigned long long>(engine);
        discard_interior(discard,base+0x10bd0,base+0x363d0);
        discard_interior(discard,base+0x36538+sizeof(State),base+0x41538);
    }
}
extern "C" void meter_prepare_compact(void*meter,double rate) noexcept{
    original_meter_prepare(meter,rate);
    if(auto discard=get_discard()){
        const auto base=reinterpret_cast<unsigned long long>(meter);
        const unsigned length=*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(meter)+0x2581c);
        for(unsigned lane=0;lane<2;++lane){
            const auto start=base+lane*0x12c00;
            discard_interior(discard,start+8ull*length,start+0x12c00);
        }
    }
}
