#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace subbox {
static_assert(std::atomic<double>::is_always_lock_free,"Lock-free scalar atomics required");
constexpr double pi=3.14159265358979323846;
constexpr unsigned maxSidechainBuses=1,pluginLatency=0;
constexpr double outputWarningLevel=.9885530946569389; // -0.1 dBFS metering warning
constexpr double subClipCeiling=0.25118863582611084,bassClipCeiling=0.5011872053146362,masterClipCeiling=0.9988493323326111; // float-safe -12 / -6 / -0.01 dBFS sample ceilings
// Retired IDs remain reserved. Existing projects cannot turn old roll/mono/OS
// automation into phase, SOLO or clipping commands after this architecture change.
enum Param:unsigned {Bass,LegacySub,LegacyRoll,BassSteep,SubSteep,LegacyMono,Bpm,HostSync,LegacyOS,Sidechain,Bypass,Phase,Clip,SubSolo,BassSolo,Mono,Oversample,Sub,LegacySynthesis,Pge,ParamCount};
constexpr std::array<double,ParamCount> defaults{.5,0.,0.,0.,0.,0.,100./280.,1.,0.,.5,0.,0.,0.,0.,0.,0.,.4,.5,0.,0.};
constexpr bool activeParam(unsigned id) noexcept{return id<ParamCount&&id!=LegacyRoll&&id!=LegacyMono&&id!=LegacyOS&&id!=LegacySynthesis;}
constexpr bool toggleParam(unsigned id) noexcept{return id==HostSync||id==Bypass||id==Pge||id==SubSolo||id==BassSolo||id==Mono;}
inline double clean(double x) noexcept{return std::isfinite(x)?std::clamp(x,-1.e6,1.e6):0.;}
// [7/6] odd Pade form on the normal tube operating range. Its maximum
// measured absolute error versus std::tanh over [-1.5,1.5] is <3.4e-10.
// Outside that range use the original library function, including overloads.
inline double tubeTanh(double x)noexcept{if(std::abs(x)>1.5)return std::tanh(x);const double s=x*x;return x*(135135.+s*(17325.+s*(378.+s)))/(135135.+s*(62370.+s*(3150.+28.*s)));}
inline double db(double x) noexcept{return x>1.e-12?20.*std::log10(x):-240.;}
inline double clampNorm(double x) noexcept{return std::clamp(clean(x),0.,1.);}
inline unsigned clipMode(double x) noexcept{return static_cast<unsigned>(std::lround(2.*clampNorm(x)));}
#include "Onyx.h"
inline double bassDb(double x) noexcept{return 60.*clampNorm(x)-30.;}
inline double bassNorm(double decibels) noexcept{return clampNorm((decibels+30.)/60.);}
inline double subDb(double x) noexcept{return bassDb(x);}
inline double subNorm(double decibels) noexcept{return bassNorm(decibels);}
inline unsigned oversampleIndex(double x) noexcept{return static_cast<unsigned>(std::clamp(std::lround(clampNorm(x)*5.),0l,5l));}
inline unsigned oversampleFactor(double x) noexcept{return 1u<<oversampleIndex(x);}
inline unsigned oversampleDelay(unsigned factor) noexcept{return 32u-32u/factor;}
inline unsigned requestedLatency(const std::array<double,ParamCount>&p) noexcept{(void)p;return 0u;}
inline double phaseMs(double x) noexcept{return 20.*clampNorm(x);}
inline double resetValue(Param id) noexcept{return id==Bass||id==Sub?bassNorm(0.):0.;}
inline double bpmValue(double x) noexcept{return 20.+280.*clampNorm(x);}
inline double foldSub(double hz) noexcept {
    if(!(hz>0.)||!std::isfinite(hz))return 0.;
    constexpr double top=56.5685424949238,bottom=28.2842712474619;
    while(hz>top)hz*=.5;
    while(hz<bottom)hz*=2.;
    return hz; // Nearest octave to 40 Hz; preserve edge-note pitch classes.
}
struct Biquad {
    double b0=1.,b1=0.,b2=0.,a1=0.,a2=0.,z1=0.,z2=0.;
    void clear() noexcept{z1=z2=0.;}
    void setup(double sr,double hz,bool high=false,double q=.7071067811865475) noexcept{
        const double k=std::tan(pi*std::min(hz,sr*.45)/sr),d=1.+k/q+k*k;
        b0=(high?1.:k*k)/d;b1=(high?-2.:2.*k*k)/d;b2=b0;a1=2.*(k*k-1.)/d;a2=(1.-k/q+k*k)/d;clear();
    }
    void peak(double sr,double hz,double q,double decibels) noexcept{
        const double a=std::pow(10.,decibels/40.),w=2.*pi*hz/sr,alpha=std::sin(w)/(2.*q),d=1.+alpha/a;
        b0=(1.+alpha*a)/d;b1=-2.*std::cos(w)/d;b2=(1.-alpha*a)/d;a1=b1;a2=(1.-alpha/a)/d;
    }
    double tick(double x) noexcept {
        const double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;
        if(std::abs(z1)<1.e-25)z1=0.;
        if(std::abs(z2)<1.e-25)z2=0.;
        return y;
    }
};
template<unsigned Order=4> struct Butterworth {
    static_assert(Order%2==0);std::array<Biquad,Order/2> sections;
    void setup(double sr,double hz,bool high=false) noexcept{for(unsigned i=0;i<Order/2;++i)sections[i].setup(sr,hz,high,1./(2.*std::cos(pi*(2.*i+1.)/(2.*Order))));}
    double tick(double x) noexcept{for(auto&s:sections)x=s.tick(x);return x;}
};
// MONO is specified by its STOP band, not by a -3 dB corner. The former
// 80 Hz Butterworth left almost all stereo width near 80 Hz. This elliptic
// side-only high-pass rejects 0-80 Hz by at least 120 dB, with <=0.01 dB
// passband ripple above 120 Hz. The original mid is never filtered/delayed.
// Analog 13th-order prototype, bilinear prewarped at 120 Hz on preparation.
struct MonoSideFilter {
    std::array<Biquad,7> sections;
    void setup(double sr)noexcept{
        static constexpr double prototype[7][6]{
            {0.,.99999999999999989,0.,0.,1.,2.675810805810737},
            {1.,0.,.054575714221984262,1.,3.0517097665354922,4.4300145728999265},
            {1.,0.,.19008745256974297,1.,1.2582464576095649,2.3046163924812011},
            {1.,0.,.3479935483026505,1.,.57252786949782253,1.4992204406166163},
            {1.,0.,.4819127571621929,1.,.27883147857917201,1.1666487704337438},
            {1.,0.,.57215868299182615,1.,.13016741634818127,1.0187706320346981},
            {1.,0.,.61624500332169085,1.,.038268099741305121,.96033281712986018}
        };
        const double w=std::tan(pi*120./sr),w2=w*w;
        for(unsigned i=0;i<sections.size();++i){const auto&r=prototype[i];auto&b=sections[i];
            if(r[3]==0.){const double d=r[4]+r[5]*w;b.b0=(r[1]+r[2]*w)/d;b.b1=(r[2]*w-r[1])/d;b.b2=b.a2=0.;b.a1=(r[5]*w-r[4])/d;b.clear();continue;}
            const double d=r[3]+r[4]*w+r[5]*w2;
            b.b0=(r[0]+r[1]*w+r[2]*w2)/d;b.b1=2.*(r[2]*w2-r[0])/d;b.b2=(r[0]-r[1]*w+r[2]*w2)/d;
            b.a1=2.*(r[5]*w2-r[3])/d;b.a2=(r[3]-r[4]*w+r[5]*w2)/d;b.clear();}
    }
    double tick(double x)noexcept{for(auto&s:sections)x=s.tick(x);return x;}
};
struct Band {
    Butterworth<4> high,low;
    void setup(double sr,double bottom,double top) noexcept{high.setup(sr,bottom,true);low.setup(sr,top);}
    double tick(double x) noexcept{return low.tick(high.tick(x));}
};
// Linkwitz-Riley fourth-order split. Its low and high outputs have matching
// phase; their sum is a second-order all-pass, not the original dry waveform.
struct Crossover {
    std::array<Biquad,2>low,high;
    void setup(double sr,double hz)noexcept{for(auto&b:low)b.setup(sr,hz);for(auto&b:high)b.setup(sr,hz,true);}
    std::array<double,2>tick(double x)noexcept{double l=x,h=x;for(auto&b:low)l=b.tick(l);for(auto&b:high)h=b.tick(h);return {l,h};}
};
struct CrossoverPhase {
    Biquad filter;
    void setup(double sr,double hz)noexcept{filter.setup(sr,hz);filter.b0=filter.a2;filter.b1=filter.a1;filter.b2=1.;}
    double tick(double x)noexcept{return filter.tick(x);}
};
// Three coherent source lanes plus the untouched-frequency remainder. Every
// branch has the SAME crossover phase, including the subsonic/upper remainder.
// Attenuating a branch can therefore never expose a phase-cancelling copy of
// that branch in the remainder. Neutral, CLIP-off playback still uses dry audio.
struct SourceBands {
    Crossover top,split,bottom;CrossoverPhase bassPhase,upperPhase,upperBottomPhase;
    void setup(double sr)noexcept{top.setup(sr,120.);split.setup(sr,80.);bottom.setup(sr,20.);bassPhase.setup(sr,20.);upperPhase.setup(sr,80.);upperBottomPhase.setup(sr,20.);}
    std::array<double,3>tick(double x)noexcept{const auto t=top.tick(x),s=split.tick(t[0]),b=bottom.tick(s[0]);return {b[1],bassPhase.tick(s[1]),b[0]+upperBottomPhase.tick(upperPhase.tick(t[1]))};}
};
template<unsigned N> struct Delay {
    std::array<double,N> memory{};unsigned pos=0;
    void clear() noexcept{memory.fill(0.);pos=0;}
    double tick(double x,double delay) noexcept{
        memory[pos]=x;delay=std::clamp(delay,0.,static_cast<double>(N-2));const auto whole=static_cast<unsigned>(delay);const double t=delay-whole;
        const double y=(1.-t)*memory[(pos+N-whole)%N]+t*memory[(pos+N-whole-1)%N];pos=(pos+1)%N;return y;
    }
    double read(double delay)const noexcept{delay=std::clamp(delay,0.,static_cast<double>(N-2));const auto whole=static_cast<unsigned>(delay);const double t=delay-whole;return (1.-t)*memory[(pos+N-1-whole)%N]+t*memory[(pos+N-2-whole)%N];}
};
// YIN cumulative-mean normalized difference, anti-aliased downsampling, finite
// fixed buffers. Three consistent candidates are required before a note locks.
class PitchTracker {
    static constexpr unsigned capacity=512,window=192,maxLag=160;
    std::array<double,capacity> ring{};std::array<double,maxLag+1>difference{},rawDifference{};
    Butterworth<8> low;Biquad high;unsigned pos=0,count=0,decimator=0,divide=16,hop=0,consistent=0;
    double rate=3000.,candidate=0.,energy=0.;
public:
    double hz=0.,confidence=0.,rms=0.;
    void prepare(double sr) noexcept{divide=std::max(1u,static_cast<unsigned>(std::lround(sr/3000.)));rate=sr/divide;low.setup(sr,200.);high.setup(sr,22.,true);ring.fill(0.);difference.fill(0.);pos=count=decimator=hop=consistent=0;hz=confidence=rms=candidate=energy=0.;}
    void tick(double input,bool transient) noexcept{
        const double x=low.tick(high.tick(input));if(++decimator<divide)return;decimator=0;ring[pos]=x;pos=(pos+1)%capacity;if(count<capacity)++count;
        if(++hop<32||count<window+maxLag)return;
        hop=0;
        auto sample=[&](unsigned age){return ring[(pos+capacity-1-age)%capacity];};
        energy=0.;for(unsigned i=0;i<window;++i)energy+=sample(i)*sample(i);rms=std::sqrt(energy/window);
        if(rms<.0002||transient){confidence*=.5;consistent=0;return;}
        double accumulated=0.;difference[0]=1.;for(unsigned lag=1;lag<=maxLag;++lag){double v=0.;for(unsigned i=0;i<window;++i){const double d=sample(i)-sample(i+lag);v+=d*d;}rawDifference[lag]=v;accumulated+=v;difference[lag]=accumulated>1.e-25?v*lag/accumulated:1.;}
        const unsigned lower=std::max(2u,static_cast<unsigned>(rate/200.)),upper=std::min(maxLag-1,static_cast<unsigned>(std::ceil(rate/25.))+1);unsigned best=0;
        for(unsigned lag=2;lag<upper;++lag)if(difference[lag]<.16&&difference[lag]<=difference[lag-1]&&difference[lag]<difference[lag+1]){best=lag;break;}
        // Find the earliest real periodicity before range selection. Otherwise
        // a 250 Hz voice can appear perfectly periodic at 125 Hz, and create
        // an unrelated 31.25 Hz sub from a multiple of its true period.
        if(!best||best<lower){confidence=0.;consistent=0;return;}
        const double a=rawDifference[best-1],b=rawDifference[best],c=rawDifference[best+1],d=a-2.*b+c;
        const double offset=d>1.e-15?std::clamp(.5*(a-c)/d,-.5,.5):0.;const double note=rate/(best+offset);
        if(note<24.8||note>200.5){confidence=0.;consistent=0;return;}
        if(candidate>0.&&std::abs(std::log(note/candidate))<.035)++consistent;else consistent=1;candidate=note;
        confidence=1.-difference[best];
        if(consistent>=3&&confidence>=.84){hz=note;
            // Integrate over exactly two detected cycles. A fixed RMS window
            // would amplitude-modulate the generated sine as notes move off-bin.
            const double duration=std::min(2.*rate/note,static_cast<double>(capacity-2));const unsigned n=static_cast<unsigned>(duration);const double t=duration-n;double power=0.;
            for(unsigned i=0;i<n;++i)power+=.5*(sample(i)*sample(i)+sample(i+1)*sample(i+1));
            const double endPower=sample(n)*sample(n),nextPower=sample(n+1)*sample(n+1);power+=endPower*t+.5*(nextPower-endPower)*t*t;rms=std::sqrt(std::max(0.,power/duration));
        }
    }
};
struct AnalysisFrame {float subL=0.,subR=0.,bassL=0.,bassR=0.;};
// One audio producer and one UI consumer. A generation prevents a late audio
// block from restoring a transient cleared by RESET. No locks or allocation.
class PeakMailbox {
    std::atomic<std::uint64_t> value{0};
    static std::uint64_t pack(unsigned epoch,double peak)noexcept{const float f=static_cast<float>(peak);std::uint32_t bits=0;std::memcpy(&bits,&f,sizeof(bits));return (std::uint64_t(epoch)<<32)|bits;}
    static double unpack(std::uint64_t state)noexcept{const auto bits=static_cast<std::uint32_t>(state);float f=0.;std::memcpy(&f,&bits,sizeof(f));return f;}
public:
    void clear(unsigned epoch)noexcept{
        auto previous=value.load(std::memory_order_relaxed);
        while(static_cast<std::int32_t>(static_cast<unsigned>(previous>>32)-epoch)<0)
            if(value.compare_exchange_weak(previous,pack(epoch,0.),std::memory_order_release,std::memory_order_relaxed))return;
    }
    void publish(unsigned epoch,double peak)noexcept{
        auto previous=value.load(std::memory_order_relaxed);
        for(;;){const auto oldEpoch=static_cast<unsigned>(previous>>32);if(static_cast<std::int32_t>(oldEpoch-epoch)>0)return;
            if(oldEpoch==epoch&&unpack(previous)>=peak)return;
            if(value.compare_exchange_weak(previous,pack(epoch,peak),std::memory_order_release,std::memory_order_relaxed))return;}
    }
    double consume(unsigned epoch)noexcept{
        auto previous=value.load(std::memory_order_acquire);
        for(;;){const auto oldEpoch=static_cast<unsigned>(previous>>32);if(static_cast<std::int32_t>(oldEpoch-epoch)>0)return 0.;
            if(value.compare_exchange_weak(previous,pack(epoch,0.),std::memory_order_acq_rel,std::memory_order_acquire))return oldEpoch==epoch?unpack(previous):0.;}
    }
};
class AnalysisQueue {
    static constexpr unsigned capacity=65536,mask=capacity-1;
    std::array<AnalysisFrame,capacity> data{};
    std::atomic<unsigned> head{0},tail{0},overflows{0};bool overflowing=false;
public:
    void push(AnalysisFrame v) noexcept{const auto h=head.load(std::memory_order_relaxed),next=(h+1)&mask;if(next==tail.load(std::memory_order_acquire)){if(!overflowing){overflows.fetch_add(1,std::memory_order_relaxed);overflowing=true;}return;}overflowing=false;data[h]=v;head.store(next,std::memory_order_release);}
    unsigned overflowEpoch()const noexcept{return overflows.load(std::memory_order_relaxed);}
    bool pop(AnalysisFrame&v) noexcept{const auto t=tail.load(std::memory_order_relaxed);if(t==head.load(std::memory_order_acquire))return false;v=data[t];tail.store((t+1)&mask,std::memory_order_release);return true;}
};

class Halfband {
    std::array<double,16> odd{};
    std::array<double,34> upMemory{};
    std::array<double,66> downMemory{};
    unsigned upPos=0,downPos=0;
public:
    void clear() noexcept{upMemory.fill(0.);downMemory.fill(0.);upPos=downPos=0;}
    void setup() noexcept{
        double sum=0.;for(unsigned j=0;j<16;++j){const unsigned i=2*j+1;const double t=static_cast<double>(i)-16.;const double w=.42-.5*std::cos(2.*pi*i/32.)+.08*std::cos(4.*pi*i/32.);odd[j]=std::sin(pi*t*.5)/(pi*t)*w;sum+=odd[j];}
        for(auto&c:odd)c*=.5/sum;
        upMemory.fill(0.);downMemory.fill(0.);upPos=downPos=0;
    }
    std::array<double,2> up(double x) noexcept{
        upMemory[upPos]=upMemory[upPos+17]=x;const auto*history=upMemory.data()+upPos+17;double second=0.;
        for(unsigned j=0;j<16;++j)second+=2.*odd[j]*history[-static_cast<int>(j)];
        const double first=history[-8];if(++upPos==17)upPos=0;return {first,second};
    }
    void downPush(double x) noexcept{downMemory[downPos]=downMemory[downPos+33]=x;if(++downPos==33)downPos=0;}
    double downTick(double x) noexcept{
        downMemory[downPos]=downMemory[downPos+33]=x;const auto*history=downMemory.data()+downPos+33;
        double y=.5*history[-16];for(unsigned j=0;j<16;++j)y+=odd[j]*history[-static_cast<int>(2*j+1)];
        if(++downPos==33)downPos=0;
        return y;
    }
};
class OversampledLane {
    std::array<Halfband,5> upStages,downStages,referenceStages;
public:
    void setup() noexcept{for(auto&s:upStages)s.setup();for(auto&s:downStages)s.setup();for(auto&s:referenceStages)s.setup();}
    void clear() noexcept{for(auto*group:{&upStages,&downStages,&referenceStages})for(auto&s:*group)s.clear();}
    template<class Shape> std::array<double,2> process(double x,unsigned f,Shape&&shape) noexcept{
        if(f==1){if constexpr(std::is_invocable_v<Shape,double,double>)return {shape(x,0.),x};else return {shape(x),x};}
        std::array<double,32>a{},b{},reference{};a[0]=x;unsigned count=1,levels=0;
        for(;count<f;++levels){for(unsigned j=0;j<count;++j){const auto pair=upStages[levels].up(a[j]);b[2*j]=pair[0];b[2*j+1]=pair[1];}count*=2;std::copy_n(b.begin(),count,a.begin());}
        for(unsigned i=0;i<count;++i){reference[i]=a[i];if constexpr(std::is_invocable_v<Shape,double,double>)a[i]=shape(a[i],static_cast<double>(i)/f);else a[i]=shape(a[i]);}
        while(levels){--levels;for(unsigned i=0;i<count;i+=2){b[i/2]=downStages[levels].downTick(a[i]);downStages[levels].downPush(a[i+1]);const double r=referenceStages[levels].downTick(reference[i]);referenceStages[levels].downPush(reference[i+1]);reference[i/2]=r;}count/=2;std::copy_n(b.begin(),count,a.begin());}
        return {a[0],reference[0]};
    }
    // Linear reconstruction commutes with subtraction. Filter only the
    // nonlinear residual instead of filtering two nearly equal waveforms.
    // This retains the same anti-alias transfer and eliminates cancellation
    // roundoff and a redundant reference reconstruction at high OS factors.
    template<class Residual> double processResidual(double x,unsigned f,Residual&&residual) noexcept{
        if(f==1)return residual(x);
        std::array<double,32>a{},b{};a[0]=x;unsigned count=1,levels=0;
        for(;count<f;++levels){for(unsigned j=0;j<count;++j){const auto pair=upStages[levels].up(a[j]);b[2*j]=pair[0];b[2*j+1]=pair[1];}count*=2;std::copy_n(b.begin(),count,a.begin());}
        for(unsigned i=0;i<count;++i)a[i]=residual(a[i]);
        while(levels){--levels;for(unsigned i=0;i<count;i+=2){b[i/2]=downStages[levels].downTick(a[i]);downStages[levels].downPush(a[i+1]);}count/=2;std::copy_n(b.begin(),count,a.begin());}
        return a[0];
    }
};
// Linked C3 output knee. At OS > 1, integrate the nonlinear correction
// along the previous-to-current stereo segment with endpoint-inclusive quadrature.
// Keeping the linear term at the current sample avoids delaying the dry path;
// the short causal correction replaces a long FIR residual whose phase can
// increase folded energy when recombined with an undelayed original.
// The final common gain bounds SAMPLE peaks, not reconstructed true peaks.
class AdaptiveMasterClipper {
    std::array<double,2> previous{};
    double envelope=0.,release=0.;unsigned peakHold=0,peakHoldSamples=3840;bool active=false;
    static std::array<double,2> shape(const std::array<double,2>&x,double start)noexcept{
        const double peak=std::max(std::abs(x[0]),std::abs(x[1]));
        const double gain=peak>start?OnyxClipper::withStart(peak,start,masterClipCeiling)/peak:1.;
        return {x[0]*gain,x[1]*gain};
    }
public:
    static constexpr double softStartDb=-.99,softStartLevel=0.8922776195878269,releaseMs=80.;
    double start=softStartLevel,reduction=0.;
    void prepare(double sr)noexcept{release=std::exp(-1./(.001*releaseMs*sr));peakHoldSamples=std::max(1u,static_cast<unsigned>(std::lround(.001*releaseMs*sr)));peakHold=0;envelope=0.;active=false;previous={};start=softStartLevel;reduction=0.;}
    std::array<double,2> tick(const std::array<double,2>&input,double mix,unsigned requested,double quality=1.)noexcept{
        const double wet=clampNorm(mix);if(wet==0.){active=false;envelope=reduction=0.;peakHold=0;start=softStartLevel;return input;}
        const std::array<double,2>x{clean(input[0]),clean(input[1])};const double peak=std::max(std::abs(x[0]),std::abs(x[1]));
        // Hold an observed crest through successive waveform cycles. Without
        // this hold a 0 dBFS sine reset the knee between every crest and never
        // reached the requested -0.01 dBFS output target, unlike a DC fixture.
        if(peak>=envelope){envelope=peak;peakHold=peakHoldSamples;}else if(peakHold)--peakHold;else envelope=std::max(peak,envelope*release);
        const double floor=softStartLevel;
        // The C3 knee becomes flat at 2*ceiling-start. Solve that edge for
        // the observed input peak, then recover upward over 80 ms. Loud hits
        // lower the knee immediately. The first enabled sample starts at
        // -0.99 dBFS; no makeup gain or future audio is used.
        // Silence/headroom must restore the -0.99 dBFS soft entry. Previously
        // silence moved it almost to the hard ceiling, disabling the soft knee
        // on ordinary mastered peaks. Only sustained ceiling-level input can
        // raise the knee to recover usable output headroom.
        const double target=envelope>=masterClipCeiling?std::clamp(2.*masterClipCeiling-envelope,floor,masterClipCeiling-1.e-5):floor;
        if(target<start)start=target;
        auto shaped=shape(x,start);unsigned level=0;while((1u<<level)<std::clamp(requested,1u,32u))++level;const unsigned factor=1u<<level;
        if(active&&factor>1){
            std::array<double,2> correction{};
            // Right-endpoint Gauss-Radau quadrature retains the current soft
            // attack while integrating the inter-sample nonlinear residual.
            struct Node {double t,weight;};
            static constexpr std::array<std::array<Node,32>,5>radau{{
        {{{0.33333333333333337,0.75000000000000011},{1,0.25}}},
        {{{0.088587959512703929,0.22046221117676798},{0.40946686444073477,0.3881934688431718},{0.787659461760847,0.32884431998005981},{1,0.0625}}},
        {{{0.022479386438712501,0.057254407372128252},{0.11467905316090415,0.12482395066492961},{0.26578982278458951,0.17350739781725119},{0.45284637366944464,0.19578608372624656},{0.64737528288683033,0.18825877269455918},{0.81975930826310761,0.15206531032339249},{0.94373743946307787,0.092679077401489632},{1,0.015625}}},
        {{{0.0056406889725116316,0.014448569508395839},{0.02948229864794244,0.033094754305063118},{0.071412953115158828,0.050497839810890843},{0.12983102555359105,0.065962849966534301},{0.202492755050104,0.07889346090210049},{0.28660608625752715,0.088792396376370666},{0.37893868864697794,0.0952791471276771},{0.47594230846323488,0.098104394119515451},{0.57388916090668585,0.097159544855783694},{0.66901519502995987,0.092480890744332661},{0.75766473903134268,0.084248198924961354},{0.83643096060561017,0.072777772610101196},{0.902286700679378,0.05851017655193011},{0.95270040990583338,0.041992640722482592},{0.98573054526317416,0.023851113473842871},{1,0.00390625}}},
        {{{0.0014114759654438314,0.0036205982124616},{0.0074220518264557223,0.0083942260798286813},{0.018174769095265775,0.013094150677865537},{0.033568488693880627,0.017668593628765963},{0.053455257851262594,0.022073003920110822},{0.077643627492414047,0.026264876971879222},{0.10590067435078981,0.0302038193968001},{0.13795427748555528,0.033851889039683856},{0.17349574784677541,0.037173949759661908},{0.21218280408880408,0.04013800681154002},{0.25364287008646658,0.042715513955870116},{0.29747666355540758,0.04488164798002011},{0.34326204160227936,0.046615547588758667},{0.3905580663160596,0.047900514229571547},{0.43890925130751679,0.048724172867781766},{0.48784994832762429,0.049078591141999556},{0.53690883173210957,0.04896035574263826},{0.58561343760999263,0.048370605272906778},{0.63349471386349487,0.047315019272462268},{0.68009153741807127,0.045803763506207087},{0.72495515505406605,0.043851392040448321},{0.76765350508070218,0.041476707040857479},{0.80777537820794154,0.038702577624243777},{0.84493437749210631,0.035555719466690332},{0.8787726390975521,0.032066437184492889},{0.90896427773652599,0.028268331672098142},{0.93521852274783446,0.024197974270816915},{0.95728251181491264,0.019894547349781803},{0.974943704254424,0.015399437880515995},{0.98803183049517229,0.010755674375458737},{0.99641981547134661,0.0060057925377774045},{1,0.0009765625}}},
            }};
            const auto&nodes=radau[level-1];
            for(unsigned i=0;i<factor;++i){const double t=nodes[i].t;std::array<double,2>at{};
                for(unsigned c=0;c<2;++c)at[c]=previous[c]+t*(x[c]-previous[c]);
                const auto clipped=shape(at,start);for(unsigned c=0;c<2;++c)correction[c]+=nodes[i].weight*(clipped[c]-at[c]);
            }
            for(unsigned c=0;c<2;++c)shaped[c]+=clampNorm(quality)*(x[c]+correction[c]-shaped[c]);
        }
        previous=x;active=true;
        const double reconstructedPeak=std::max(std::abs(shaped[0]),std::abs(shaped[1]));
        const double guard=reconstructedPeak>masterClipCeiling?masterClipCeiling/reconstructedPeak:1.;
        for(unsigned c=0;c<2;++c){shaped[c]=std::clamp(shaped[c]*guard,-masterClipCeiling,masterClipCeiling);if(wet!=1.)shaped[c]=x[c]+wet*(shaped[c]-x[c]);}
        const double outPeak=std::max(std::abs(shaped[0]),std::abs(shaped[1]));reduction=peak>1.e-12?std::max(0.,1.-outPeak/peak):0.;start=target+(start-target)*release;return shaped;
    }
};
// Eight-phase windowed-sinc peak estimate. Its 32-sample observation delay is
// a metering delay; audio lookahead is accounted for separately.
class TruePeakMeter {
    std::array<std::array<double,65>,8> coefficients{};
    std::array<std::array<double,256>,2> memory{};unsigned pos=0;
public:
    void prepare() noexcept {
        for(unsigned phase=0;phase<8;++phase){double sum=0.;const double fraction=phase/8.;for(unsigned j=0;j<65;++j){const double t=static_cast<double>(j)-32.-fraction,w=.42+.5*std::cos(pi*t/33.)+.08*std::cos(2.*pi*t/33.);const double v=std::abs(t)<1.e-12?1.:std::sin(pi*t)/(pi*t);coefficients[phase][j]=v*std::max(0.,w);sum+=coefficients[phase][j];}for(auto&v:coefficients[phase])v/=sum;}
        clear();
    }
    void clear() noexcept{for(auto&m:memory)m.fill(0.);pos=0;}
    double tick(const std::array<double,2>&x) noexcept {
        double peak=0.;for(unsigned c=0;c<2;++c){memory[c][pos]=memory[c][pos+128]=x[c];const auto*history=memory[c].data()+pos+64;peak=std::max(peak,std::abs(history[32]));
            for(unsigned phase=1;phase<8;++phase){const auto*taps=coefficients[phase].data();double a=0.,b=0.,d=0.,e=0.;
                for(unsigned j=0;j<64;j+=4){a+=taps[j]*history[j];b+=taps[j+1]*history[j+1];d+=taps[j+2]*history[j+2];e+=taps[j+3]*history[j+3];}
                peak=std::max(peak,std::abs(a+b+d+e+taps[64]*history[64]));}}
        pos=(pos+1)%128;return peak;
    }
};
class PeakProtection {
    std::array<std::array<double,256>,2> audio{};std::array<double,256> desired{};
    struct Candidate {double gain=1.;std::uint64_t time=0;};
    std::array<Candidate,256> minimum{};unsigned minHead=0,minTail=0;std::uint64_t windowClock=0;
    unsigned pos=0;double gain=1.,release=0.,ceiling=.5;
public:
    void clear() noexcept{for(auto&a:audio)a.fill(0.);desired.fill(1.);pos=minHead=minTail=0;windowClock=0;gain=1.;reduction=0.;}
    static constexpr unsigned lookahead=128;
    double reduction=0.;
    void prepare(double sr,double limit) noexcept{ceiling=limit;clear();release=std::exp(-1./(.08*sr));}
    std::array<double,2> tick(const std::array<double,2>&x,double enable) noexcept {
        const double peak=std::max(std::abs(x[0]),std::abs(x[1]));
        desired[pos]=std::min(1.,ceiling/std::max(peak,1.e-30));
        const auto center=(pos+256-lookahead)%256;
        // Exact 129-sample sliding minimum, bounded monotonic queue: no repeated
        // full-window scan and no allocation. Future detector gain is queued.
        const double incoming=desired[pos];
        while(minHead!=minTail&&windowClock>=129&&minimum[minHead].time<windowClock-128)minHead=(minHead+1)%256;
        while(minHead!=minTail&&minimum[(minTail+255)%256].gain>=incoming)minTail=(minTail+255)%256;
        minimum[minTail]={incoming,windowClock++};minTail=(minTail+1)%256;const double request=minimum[minHead].gain;
        gain=request<gain?request:request+(gain-request)*release;reduction=1.-gain;
        std::array<double,2>out{};for(unsigned c=0;c<2;++c){audio[c][pos]=x[c];out[c]=audio[c][center]*(1.-enable+enable*gain);}pos=(pos+1)%256;return out;
    }
};
// Protect only the measured low-band contributors. The rest of the selected
// song is delay-matched and recombined without a full-mix limiter or clipper.
struct RoutingFrame {
    std::array<double,2>full{},sub{},bass{},remainder{};double subSolo=0.,bassSolo=0.,generated=0.;
};
struct RoutedOutput {std::array<double,2>audio{},sub{},bass{};double generated=0.;};
class ProcessingPath {
    AdaptiveBandClipper subClip,bassClip;
public:
    unsigned factor=4;bool enabled=false;double reduction=0.;
    unsigned latency()const noexcept{return 0;}
    void prepare(double sr,unsigned f,bool active)noexcept{factor=f;enabled=active;subClip.prepare(sr,subClipCeiling);bassClip.prepare(sr,bassClipCeiling);reduction=0.;}
    void clear()noexcept{subClip.clear();bassClip.clear();reduction=0.;}
    RoutedOutput tick(const RoutingFrame&x,double mix)noexcept{
        RoutedOutput y;const double normal=1.-std::max(x.subSolo,x.bassSolo);
        const double wet=std::clamp(2.*mix,0.,1.);
        const auto sub=enabled?subClip.tick(x.sub,wet):x.sub;
        const auto bass=enabled?bassClip.tick(x.bass,wet):x.bass;
        for(unsigned c=0;c<2;++c){const double full=enabled?x.remainder[c]+sub[c]+bass[c]:x.full[c];
            y.audio[c]=normal*full+x.subSolo*sub[c]+x.bassSolo*bass[c];
            y.sub[c]=(normal+x.subSolo)*sub[c];y.bass[c]=(normal+x.bassSolo)*bass[c];
        }
        const double inputSub=std::max(std::abs(x.sub[0]),std::abs(x.sub[1]));
        const double inputBass=std::max(std::abs(x.bass[0]),std::abs(x.bass[1]));
        const double subGain=inputSub>1.e-15?std::max(std::abs(sub[0]),std::abs(sub[1]))/inputSub:1.;
        const double bassGain=inputBass>1.e-15?std::max(std::abs(bass[0]),std::abs(bass[1]))/inputBass:1.;
        y.generated=(normal+x.subSolo)*x.generated*subGain;
        reduction=enabled?1.-std::min(subGain,bassGain):0.;return y;
    }
    void prime(const RoutingFrame&)noexcept{}
};
// Unbiased stereo-power RMS over an exact trailing 50 ms interval. Fixed
// storage covers the supported 192 kHz ceiling; no GUI timing or decimation
// can alter the reading. A full-scale sine reads -3.0103 dBFS RMS.
class BandRmsMeter {
    static constexpr unsigned capacity=9600;
    std::array<std::array<double,capacity>,2> history{};
    std::array<double,2> sum{};std::array<unsigned,2> active{};unsigned pos=0,length=2400;
public:
    void prepare(double sr)noexcept{length=std::clamp(static_cast<unsigned>(std::lround(sr*.05)),1u,capacity);clear();}
    void clear()noexcept{for(auto&lane:history)lane.fill(0.);sum.fill(0.);active.fill(0);pos=0;}
    std::array<double,2> tick(const std::array<double,2>&sub,const std::array<double,2>&bass)noexcept{
        std::array<double,2> rms{};
        for(unsigned lane=0;lane<2;++lane){const auto&x=lane==0?sub:bass;const double power=.5*(x[0]*x[0]+x[1]*x[1]);if(history[lane][pos]>0.)--active[lane];if(power>0.)++active[lane];sum[lane]+=power-history[lane][pos];history[lane][pos]=power;if(!active[lane]||sum[lane]<0.)sum[lane]=0.;rms[lane]=std::sqrt(sum[lane]/length);}
        if(++pos==length)pos=0;
        return rms;
    }
};
struct Output {
    std::array<double,2>audio{},sub{},bass{},rms{};double generated=0.,glideSemitones=0.,oscillatorHz=0.,guard=0.,trackedHz=0.,confidence=0.,reduction=0.,masking=0.;bool unstable=false;
};
class Engine {
    struct Channel {Biquad eq,dc;Butterworth<4> subsonic;Band bass,dryStem;SourceBands source;OversampledLane tube;Delay<512> tubeScale,bypass,meterBypass;};
    std::array<Channel,2> channels;PitchTracker pitch;PitchGlide glide;Band kickBand;BandRmsMeter bandMeter;AdaptiveMasterClipper master;
    std::array<ProcessingPath,2> paths;std::array<RoutingFrame,512>qualityHistory{};unsigned pathCurrent=0,qualityPos=0,transition=0,transitionLength=384;
    Delay<4096> phaseDelay;MonoSideFilter monoHighpass,monoBassHighpass,monoSubHighpass,monoRemainderHighpass;Butterworth<4> subHighpass,subLowpass;
    std::array<double,ParamCount> smooth=defaults,controlStage=defaults;
    std::array<double,3> synthesisGain{};double synthesisSmoothing=0.;
    double sr=48000.,smoothing=0.,kickFast=0.,kickSlow=0.,guard=0.,subEnvelope=0.,frequency=40.,oscillator=0.,beat=0.,rollGain=1.;
    double kickRelease=0.,slowRelease=0.,guardRelease=0.,subAttack=0.,freqSmooth=0.,bassScale=0.,bassRelease=0.;
    double leftPower=0.,rightPower=0.,midPower=0.,selectLeft=0.,powerSmooth=0.,qualityGain=1.,subLimitGain=1.,sourceGain=1.,sourcePhaseMix=0.;
    unsigned idle=0,clock=0,badHold=0,goodCount=0,silenceLength=96000,factor=4,latencySamples=0;bool statusBad=false,settledSilence=false,sidechainSeen=false,externalAnalysis=false,sourceChanging=false;
public:
    void prepare(double sampleRate,const std::array<double,ParamCount>&p=defaults) noexcept {
        sr=std::clamp(clean(sampleRate),1000.,192000.);smooth=p;for(auto&v:smooth)v=clampNorm(v);smooth[Sidechain]=p[Sidechain]>.75?1.:0.;controlStage=smooth;synthesisGain.fill(0.);synthesisSmoothing=std::exp(-1./(.0015*sr));sidechainSeen=sourceChanging=false;externalAnalysis=p[Sidechain]>.75;factor=oversampleFactor(p[Oversample]);latencySamples=requestedLatency(p);pathCurrent=qualityPos=transition=0;transitionLength=static_cast<unsigned>(std::ceil(sr*.008));qualityHistory.fill({});for(auto&path:paths)path.prepare(sr,factor,p[Clip]>=.5);
        smoothing=std::exp(-1./(.008*sr));kickRelease=std::exp(-1./(.008*sr));slowRelease=std::exp(-1./(.08*sr));guardRelease=std::exp(-1./(.045*sr));subAttack=std::exp(-1./(.012*sr));freqSmooth=std::exp(-1./(.025*sr));bassRelease=std::exp(-1./(.09*sr));powerSmooth=std::exp(-1./(.04*sr));
        for(auto&c:channels){c.eq.clear();c.eq.peak(sr,std::sqrt(60.*90.),3.5,bassDb(p[Bass]));c.dc.setup(sr,5.,true);c.subsonic.setup(sr,20.,true);c.bass.setup(sr,20.,80.);c.dryStem.setup(sr,20.,120.);c.source.setup(sr);c.tube.setup();c.tubeScale.clear();c.bypass.clear();c.meterBypass.clear();}
        pitch.prepare(sr);glide.prepare(sr);master.prepare(sr);kickBand.setup(sr,35.,140.);monoHighpass.setup(sr);monoBassHighpass.setup(sr);monoSubHighpass.setup(sr);monoRemainderHighpass.setup(sr);subHighpass.setup(sr,20.,true);subLowpass.setup(sr,80.);phaseDelay.clear();bandMeter.prepare(sr);subLimitGain=1.;
        kickFast=kickSlow=guard=subEnvelope=oscillator=beat=bassScale=leftPower=rightPower=midPower=selectLeft=0.;frequency=40.;rollGain=qualityGain=sourceGain=1.;sourcePhaseMix=p[Sub]==.5?0.:1.;idle=clock=badHold=goodCount=0;silenceLength=static_cast<unsigned>(std::ceil(sr*2.));statusBad=settledSilence=false;
    }
    unsigned latency()const noexcept{return latencySamples;}
    unsigned oversampling()const noexcept{return factor;}
    bool analyzesSidechain()const noexcept{return externalAnalysis;}
    void sync(double ppq) noexcept{if(std::isfinite(ppq))beat=ppq-std::floor(ppq);}
    double noteHz()const noexcept{return pitch.hz;}
    double noteConfidence()const noexcept{return pitch.confidence;}
    Output tick(double left,double right,double scL,double scR,bool scAvailable,const std::array<double,ParamCount>&p,double tempo) noexcept {
        left=clean(left);right=clean(right);scL=clean(scL);scR=clean(scR);
        if(!scAvailable)sidechainSeen=false;else if(std::abs(scL)>1.e-12||std::abs(scR)>1.e-12)sidechainSeen=true;
        const bool selectedSidechain=p[Sidechain]>.75||(p[Sidechain]>.25&&scAvailable&&sidechainSeen);
        if(selectedSidechain!=externalAnalysis){pitch.prepare(sr);glide.prepare(sr);sourceChanging=true;externalAnalysis=selectedSidechain;}
        for(unsigned i=0;i<ParamCount;++i){const double target=i==Sidechain?(externalAnalysis?1.:0.):clampNorm(p[i]);controlStage[i]=target+(controlStage[i]-target)*smoothing;if(std::abs(controlStage[i]-target)<1.e-10)controlStage[i]=target;
            // Two continuous poles remove the slope step when audition/PGE
            // switches change, including sample-offset host automation.
            smooth[i]=(i==SubSolo||i==BassSolo||i==Pge)?controlStage[i]+(smooth[i]-controlStage[i])*smoothing:controlStage[i];if(std::abs(smooth[i]-target)<1.e-10)smooth[i]=target;}
        // Select an audio source before ALL processing. Do not add auxiliary
        // audio to main (double routing) or show an unrelated analysis source.
        // Explicit SC ON with no mapped bus is silence; AUTO falls back to main.
        if(smooth[Sidechain]==1.){left=scAvailable?scL:0.;right=scAvailable?scR:0.;}
        else if(smooth[Sidechain]>0.){left=left+smooth[Sidechain]*((scAvailable?scL:0.)-left);right=right+smooth[Sidechain]*((scAvailable?scR:0.)-right);}
        // Only harmonic residuals are oversampled in the bass path. Fade those
        // residuals around a bounded state reset, leaving dry/EQ timing intact.
        {const auto requested=oversampleFactor(p[Oversample]);const double target=requested==factor?1.:0.;qualityGain=target+(qualityGain-target)*smoothing;if(requested!=factor&&qualityGain<1.e-4){factor=requested;qualityGain=0.;for(auto&c:channels){c.tube.clear();c.tubeScale.clear();}}}
        const auto requestedFactor=oversampleFactor(p[Oversample]);const bool clipRequested=p[Clip]>=.5;
        if(!transition&&(paths[pathCurrent].enabled!=clipRequested||(clipRequested&&paths[pathCurrent].factor!=requestedFactor))){pathCurrent=1-pathCurrent;auto&path=paths[pathCurrent];path.prepare(sr,requestedFactor,clipRequested);for(unsigned i=0;i<512;++i)path.prime(qualityHistory[(qualityPos+i)%512]);latencySamples=path.latency();transition=transitionLength;}
        if(left==0.&&right==0.){if(idle<silenceLength)++idle;}else{idle=0;settledSilence=false;}
        const std::array<double,2>original{left,right},x{channels[0].subsonic.tick(left),channels[1].subsonic.tick(right)};const double mid=.5*(x[0]+x[1]);
        leftPower=powerSmooth*leftPower+(1.-powerSmooth)*x[0]*x[0];rightPower=powerSmooth*rightPower+(1.-powerSmooth)*x[1]*x[1];midPower=powerSmooth*midPower+(1.-powerSmooth)*mid*mid;
        const double selection=midPower<.15*std::max(leftPower,rightPower)?1.:0.;selectLeft=selection+(selectLeft-selection)*smoothing;
        const double trackInput=mid+selectLeft*((leftPower>=rightPower?x[0]:x[1])-mid);
        const double kick=std::abs(kickBand.tick(mid));kickFast=std::max(kick,kickFast*kickRelease);kickSlow=slowRelease*kickSlow+(1.-slowRelease)*kick;
        const double detected=std::clamp((kickFast/(kickSlow+.002)-2.2)/1.8,0.,1.);
        guard=std::max(detected,guard*guardRelease);pitch.tick(trackInput,guard>.5);
        const bool locked=pitch.hz>=25.&&pitch.confidence>=.84&&idle<static_cast<unsigned>(sr*.08);
        double desiredHz=locked?foldSub(pitch.hz):frequency;
        // Small octave hysteresis prevents estimator noise or pitch bends at
        // the folding boundary from alternating between two octave choices.
        if(locked&&frequency>52.&&desiredHz<30.&&desiredHz*2.<=57.)desiredHz*=2.;
        else if(locked&&frequency<30.&&desiredHz>52.&&desiredHz*.5>=28.)desiredHz*=.5;
        frequency=desiredHz+(frequency-desiredHz)*freqSmooth;
        const double targetAmplitude=locked?std::min(.85,std::sqrt(2.)*pitch.rms*1.35):0.;
        if(locked)sourceChanging=false;
        const double sourceTarget=sourceChanging?0.:1.;sourceGain=sourceTarget+(sourceGain-sourceTarget)*smoothing;if(std::abs(sourceGain-sourceTarget)<1.e-10)sourceGain=sourceTarget;
        const double subSteep=smooth[SubSteep]*smooth[SubSteep]*(3.-2.*smooth[SubSteep]);
        const double release=std::exp(-1./((sourceChanging?.008:.055+1.1*subSteep)*sr));
        subEnvelope=targetAmplitude+(subEnvelope-targetAmplitude)*(targetAmplitude>subEnvelope?subAttack:release);
        if(idle>=silenceLength&&!settledSilence){
            // Commit the bounded tail to state, not just the final output.
            // Otherwise resuming unrelated audio can expose a hidden old sine
            // or samples still in the clip/lookahead delay buffers.
            subEnvelope=0.;synthesisGain.fill(0.);phaseDelay.clear();subHighpass.setup(sr,20.,true);subLowpass.setup(sr,80.);for(auto&path:paths)path.clear();qualityHistory.fill({});
            settledSilence=true;
        }
        const double glideSemitones=glide.tick(trackInput,locked,p[Pge]>=.5)*smooth[Pge];
        oscillator+=2.*pi*frequency*std::exp2(glideSemitones/12.)/sr;if(oscillator>=2.*pi)oscillator-=2.*pi;
        tempo=std::clamp(clean(tempo),20.,300.);const double rhythm=1.-smooth[HostSync]*.23*smooth[SubSteep]*smooth[SubSteep]*(.5-.5*std::cos(2.*pi*beat));beat+=tempo/(60.*sr);if(beat>=1.)beat-=1.;
        rollGain=rhythm+(rollGain-rhythm)*smoothing;
        const double rawSub=std::sin(oscillator)*subEnvelope*rollGain;
        // Causal 20 Hz subsonic protection and a real 24 dB/oct 80 Hz LPF.
        // Apply gain/ducking after filtering and phase alignment so a delayed
        // sine or a filter tail cannot evade a new mono kick transient.
        const double filteredSub=subLowpass.tick(subHighpass.tick(phaseDelay.tick(rawSub,phaseMs(smooth[Phase])*sr/1000.)));
        // New SUB is signed lane gain. Rolling synthesis belongs to SUB STEEP;
        // hidden legacy wet automation retains its original meaning on recall.
        const double subGain=std::pow(10.,subDb(smooth[Sub])/20.);
        const double newRoll=std::max({smooth[LegacySub],.65*subSteep,.65*smooth[Pge]});
        const double rollAmount=newRoll+smooth[LegacySynthesis]*(smooth[LegacySub]-newRoll);
        const double referenceRatio=std::pow(10.,(-2.+2.*std::min(1.,rollAmount))/20.);
        const double limit=guard>.15?std::min(1.,kickFast*referenceRatio/std::max(subEnvelope*rollAmount*subGain,1.e-8)):1.;
        const double limitSmooth=limit<subLimitGain?std::exp(-1./(.0005*sr)):slowRelease;
        subLimitGain=limit+(subLimitGain-limit)*limitSmooth;
        // Protection is applied after the oscillator's filters and delay, so
        // old tails cannot evade a new kick. Smooth the gain itself: an instant
        // post-filter gain step otherwise turns a clean sine into a click.
        double gain=rollAmount*subGain*(1.-guard)*(1.-guard)*subLimitGain*sourceGain;
        for(auto&stage:synthesisGain){stage=gain+(stage-gain)*synthesisSmoothing;if(std::abs(stage)<1.e-25)stage=0.;gain=stage;}
        const double generated=rollAmount==0.?0.:filteredSub*gain;
        const double phaseTarget=p[Sub]==.5?0.:1.;sourcePhaseMix=phaseTarget+(sourcePhaseMix-phaseTarget)*smoothing;if(std::abs(sourcePhaseMix-phaseTarget)<1.e-10)sourcePhaseMix=phaseTarget;
        std::array<double,2>sourceSub{},bassLane{},remainder{},candidate{};const bool coefficients=clock++%32==0;
        std::array<double,2>band{};for(unsigned c=0;c<2;++c){if(coefficients)channels[c].eq.peak(sr,std::sqrt(60.*90.),3.5,bassDb(smooth[Bass]));band[c]=channels[c].bass.tick(x[c]);}
        bassScale=std::max({std::abs(band[0]),std::abs(band[1]),bassScale*bassRelease});
        const double boost=std::max(0.,bassDb(smooth[Bass]))/30.;
        const double bassSteep=smooth[BassSteep]*smooth[BassSteep]*(3.-2.*smooth[BassSteep]);
        const double tubeAmount=.015*boost+.14*bassSteep*(.25+.75*boost);
        for(unsigned c=0;c<2;++c){auto&ch=channels[c];const double scale=std::max(1.e-8,ch.tubeScale.tick(bassScale,oversampleDelay(factor)*.5));
            const double residual=ch.tube.processResidual(band[c],factor,[&](double v)noexcept{if(tubeAmount==0.)return 0.;const double u=v/scale;const double even=u*u/(1.+.5*u*u),odd=.12*(tubeTanh(1.5*u)/1.5-u);return tubeAmount*scale*(even+odd);});
            const double dc=ch.dc.tick(residual),eq=ch.eq.tick(original[c]);
            const double coloration=tubeAmount==0.?0.:qualityGain*dc*std::pow(10.,std::min(0.,bassDb(smooth[Bass]))/20.);
            const double equalized=smooth[Bass]==.5?original[c]:eq;
            const auto split=ch.source.tick(equalized+coloration);sourceSub[c]=split[0]*subGain;bassLane[c]=split[1];remainder[c]=split[2];candidate[c]=equalized+coloration+generated;
        }
        // MONO changes only side energy. The original mid (including its kick)
        // remains at its original sample position. The side stopband includes
        // 80 Hz; the 80-120 Hz transition restores the upper stereo image.
        const double candidateMid=.5*(candidate[0]+candidate[1]),side=.5*(candidate[0]-candidate[1]);
        const double highSide=monoHighpass.tick(side),monoSide=side+smooth[Mono]*(highSide-side);
        if(smooth[Mono]!=0.)candidate={candidateMid+monoSide,candidateMid-monoSide};
        const double bassMid=.5*(bassLane[0]+bassLane[1]),bassSide=.5*(bassLane[0]-bassLane[1]);
        const double highBassSide=monoBassHighpass.tick(bassSide),meterSide=bassSide+smooth[Mono]*(highBassSide-bassSide);
        const std::array<double,2>meterBass=smooth[Mono]==0.?bassLane:std::array<double,2>{bassMid+meterSide,bassMid-meterSide};
        const double subMid=.5*(sourceSub[0]+sourceSub[1]),subSide=.5*(sourceSub[0]-sourceSub[1]);
        const double highSubSide=monoSubHighpass.tick(subSide),selectedSubSide=subSide+smooth[Mono]*(highSubSide-subSide);
        const std::array<double,2>subLane=smooth[Mono]==0.?std::array<double,2>{sourceSub[0]+generated,sourceSub[1]+generated}:std::array<double,2>{subMid+selectedSubSide+generated,subMid-selectedSubSide+generated};
        const double remainderMid=.5*(remainder[0]+remainder[1]),remainderSide=.5*(remainder[0]-remainder[1]);
        const double highRemainderSide=monoRemainderHighpass.tick(remainderSide),selectedRemainderSide=remainderSide+smooth[Mono]*(highRemainderSide-remainderSide);
        if(smooth[Mono]!=0.)remainder={remainderMid+selectedRemainderSide,remainderMid-selectedRemainderSide};
        // Active signed SUB gain uses the same coherent lanes as CLIP/SOLO.
        // At neutral gain the original dry path is retained bit-exactly.
        if(sourcePhaseMix!=0.)for(unsigned c=0;c<2;++c){const double matched=remainder[c]+subLane[c]+meterBass[c];candidate[c]=sourcePhaseMix==1.?matched:candidate[c]+sourcePhaseMix*(matched-candidate[c]);}
        const RoutingFrame frame{candidate,subLane,meterBass,remainder,smooth[SubSolo],smooth[BassSolo],generated};
        qualityHistory[qualityPos]=frame;qualityPos=(qualityPos+1)%512;
        auto protectedAudio=paths[pathCurrent].tick(frame,clipRequested?1.:smooth[Clip]);const unsigned oldPath=1-pathCurrent;
        const double qualityMix=transition?.5+.5*std::cos(pi*transition/transitionLength):1.;
        if(transition){auto old=paths[oldPath].tick(frame,paths[oldPath].enabled?1.:0.);protectedAudio.generated=old.generated+qualityMix*(protectedAudio.generated-old.generated);for(unsigned c=0;c<2;++c){protectedAudio.audio[c]=old.audio[c]+qualityMix*(protectedAudio.audio[c]-old.audio[c]);protectedAudio.sub[c]=old.sub[c]+qualityMix*(protectedAudio.sub[c]-old.sub[c]);protectedAudio.bass[c]=old.bass[c]+qualityMix*(protectedAudio.bass[c]-old.bass[c]);}}
        // Master stage is after recombination and audition selection. Bypass
        // remains the untouched selected input. The master ceiling engages on
        // the requested sample; fading its attack would let hot peaks escape.
        const double masterWet=clipMode(p[Clip])==2?1.:std::clamp(2.*smooth[Clip]-1.,0.,1.);
        const auto beforeMaster=protectedAudio.audio;
        // Fade toward untouched dry when bypass is requested. On re-entry,
        // apply the master stage after that blend so hot dry cannot escape.
        if(p[Bypass]>=.5)protectedAudio.audio=master.tick(beforeMaster,masterWet,factor,qualityGain);
        Output out;out.glideSemitones=glideSemitones;out.oscillatorHz=frequency*std::exp2(glideSemitones/12.);out.guard=guard;out.trackedHz=locked?frequency:0.;out.confidence=pitch.confidence;out.reduction=paths[pathCurrent].reduction;
        const double fadePosition=std::clamp((idle/sr-1.75)/.25,0.,1.),fade=fadePosition==1.?0.:.5+.5*std::cos(pi*fadePosition);
        for(unsigned c=0;c<2;++c){auto&ch=channels[c];double dry=ch.bypass.tick(original[c],latencySamples);if(transition){const double old=ch.bypass.read(paths[oldPath].latency());dry=old+qualityMix*(dry-old);}
            out.audio[c]=(smooth[Bypass]==1.?dry:protectedAudio.audio[c]+smooth[Bypass]*(dry-protectedAudio.audio[c]))*fade;
            const double dryBass=ch.meterBypass.tick(ch.dryStem.tick(original[c]),latencySamples);
            out.sub[c]=protectedAudio.sub[c]*(1.-smooth[Bypass])*fade;out.bass[c]=(protectedAudio.bass[c]+smooth[Bypass]*(dryBass-protectedAudio.bass[c]))*fade;
            if(fade==0.)out.audio[c]=out.sub[c]=out.bass[c]=0.;
        }
        if(clipRequested&&p[Bypass]<.5){
            // Final band safety belongs AFTER path/solo/bypass crossfades.
            // A crossfade containing an old unprotected path can otherwise
            // exceed both ceilings for the first samples of SB/SBO engagement.
            const auto sub=OnyxClipper::hardStereo(out.sub,subClipCeiling),bass=OnyxClipper::hardStereo(out.bass,bassClipCeiling);
            const double subPeak=std::max(std::abs(out.sub[0]),std::abs(out.sub[1])),subGain=subPeak>0.?std::max(std::abs(sub[0]),std::abs(sub[1]))/subPeak:1.;
            const double bassPeak=std::max(std::abs(out.bass[0]),std::abs(out.bass[1])),bassGain=bassPeak>0.?std::max(std::abs(bass[0]),std::abs(bass[1]))/bassPeak:1.;out.reduction=std::max(out.reduction,1.-std::min(subGain,bassGain));
            for(unsigned c=0;c<2;++c){out.audio[c]+=(sub[c]-out.sub[c])+(bass[c]-out.bass[c]);out.sub[c]=sub[c];out.bass[c]=bass[c];}
            protectedAudio.generated*=subGain;
        }
        if(p[Bypass]<.5)out.audio=master.tick(out.audio,masterWet,factor,qualityGain);
        out.reduction=std::max(out.reduction,master.reduction);
        out.generated=protectedAudio.generated*(1.-smooth[Bypass])*fade;out.rms=bandMeter.tick(out.sub,out.bass);
        out.masking=guard>.15?std::abs(out.generated)/std::max(kickFast,.002):0.;
        // Kick status describes conflicting mono sub energy. Stereo peaks and
        // output limiting are metered separately and never imply a bad kick.
        const bool bad=out.masking>.4;
        if(bad){statusBad=true;badHold=static_cast<unsigned>(sr*.35);goodCount=0;}else if(badHold)--badHold;else if(statusBad&&++goodCount>=static_cast<unsigned>(sr*.4)){statusBad=false;goodCount=0;}
        out.unstable=statusBad;if(transition)--transition;return out;
    }
};

// Calibrated stereo-energy, sine-RMS dBFS FFT. Both measured lanes use the
// complete logarithmic 20–400 Hz view; there are no artificial band masks.
// The trailing observation window never buffers or delays the audio path.
class Spectrum {
public:
    static constexpr unsigned size=512,fftSize=size*8,points=401;
    static double frequencyAt(unsigned point) noexcept{return 20.*std::pow(20.,static_cast<double>(point)/(points-1));}
    static double positionOf(double hz) noexcept{return std::clamp(std::log(std::max(20.,hz)/20.)/std::log(20.),0.,1.);}
private:
    std::array<AnalysisFrame,size>ring{};
    std::array<std::complex<double>,fftSize>work{},twiddle{};
    std::array<unsigned,fftSize>reverse{};
    std::array<std::array<double,fftSize/2+1>,2>power{};
    std::array<double,size>window{};std::array<Butterworth<8>,4>antialias;
    unsigned pos=0,count=0,stride=16,decimation=0;double sumWindow=0.,inputRate=48000.;
    void fft() noexcept{
        for(unsigned i=0;i<fftSize;++i)if(i<reverse[i])std::swap(work[i],work[reverse[i]]);
        for(unsigned len=2;len<=fftSize;len<<=1){const unsigned step=fftSize/len;for(unsigned i=0;i<fftSize;i+=len)for(unsigned j=0;j<len/2;++j){const auto u=work[i+j],v=work[i+j+len/2]*twiddle[j*step];work[i+j]=u+v;work[i+j+len/2]=u-v;}}
    }
    double filterCompensation(double hz)const noexcept {
        const double ratio=std::tan(pi*hz/inputRate)/std::tan(pi*std::min(600.,inputRate*.45)/inputRate);
        return 1.+std::pow(ratio,16.); // compensate analysis-only Butterworth power response
    }
public:
    std::array<double,points>sub{},bass{};std::array<double,2>peakHz{},peakDb{-240.,-240.};
    Spectrum(){for(unsigned i=0;i<size;++i){const double a=2.*pi*i/(size-1);window[i]=.42-.5*std::cos(a)+.08*std::cos(2.*a);sumWindow+=window[i];}for(unsigned i=0;i<fftSize;++i){twiddle[i]=std::polar(1.,-2.*pi*i/fftSize);unsigned x=i,r=0;for(unsigned b=0;b<12;++b){r=(r<<1)|(x&1);x>>=1;}reverse[i]=r;}setSampleRate(inputRate);}
    void setSampleRate(double sr) noexcept {if(!std::isfinite(sr)||sr<=0.)return;inputRate=sr;stride=std::max(1u,static_cast<unsigned>(std::floor(sr/3000.)));clear();}
    void clear() noexcept{ring.fill({});for(auto&p:power)p.fill(0.);pos=count=decimation=0;sub.fill(-240.);bass.fill(-240.);peakHz.fill(0.);peakDb.fill(-240.);for(auto&f:antialias)f.setup(inputRate,600.);}
    unsigned inputWindowSamples()const noexcept{return size*stride;}
    double windowSeconds()const noexcept{return inputWindowSamples()/inputRate;}
    void add(AnalysisFrame v) noexcept{
        const std::array<double,4>in{v.subL,v.subR,v.bassL,v.bassR};std::array<float,4>filtered{};
        for(unsigned c=0;c<4;++c)filtered[c]=static_cast<float>(antialias[c].tick(clean(in[c])));
        if(++decimation<stride)return;
        decimation=0;ring[pos]={filtered[0],filtered[1],filtered[2],filtered[3]};pos=(pos+1)%size;if(count<size)++count;
    }
    bool ready()const noexcept{return count==size;}
    double binDb(unsigned lane,unsigned bin)const noexcept{return lane<2&&bin*8<fftSize/2?10.*std::log10(std::max(power[lane][bin*8],1.e-24)):-240.;}
    void calculate(double sampleRate) noexcept{
        if(!std::isfinite(sampleRate)||sampleRate<=0.)return;
        if(sampleRate!=inputRate){setSampleRate(sampleRate);return;}if(!ready())return;
        const double rate=inputRate/stride,normalization=1./(sumWindow*sumWindow);
        for(unsigned lane=0;lane<2;++lane){work.fill({});for(unsigned i=0;i<size;++i){const auto&f=ring[(pos+i)%size];work[i]={window[i]*(lane==0?f.subL:f.bassL),window[i]*(lane==0?f.subR:f.bassR)};}fft();
            for(unsigned k=1;k<fftSize/2;++k){const auto mirror=std::conj(work[fftSize-k]),l=(work[k]+mirror)*.5,r=(work[k]-mirror)*std::complex<double>(0.,-.5);power[lane][k]=(std::norm(l)+std::norm(r))*normalization*filterCompensation(k*rate/fftSize);}
            const double bottom=20.,top=400.;
            const unsigned lo=std::max(1u,static_cast<unsigned>(std::ceil(bottom*fftSize/rate))),hi=std::min(fftSize/2-2,static_cast<unsigned>(std::floor(top*fftSize/rate)));
            unsigned best=lo;for(unsigned k=lo;k<=hi;++k)if(power[lane][k]>power[lane][best])best=k;
            if(lo<=hi&&power[lane][best]>1.e-16){const double a=std::log(std::max(power[lane][best-1],1.e-24)),b=std::log(power[lane][best]),c=std::log(std::max(power[lane][best+1],1.e-24)),d=a-2.*b+c;const double offset=d<0.?std::clamp(.5*(a-c)/d,-.5,.5):0.;peakHz[lane]=std::clamp((best+offset)*rate/fftSize,bottom,top);peakDb[lane]=(b-.25*(a-c)*offset)*10./std::log(10.);}else{peakHz[lane]=0.;peakDb[lane]=-240.;}
        }
        for(unsigned i=0;i<points;++i){const double hz=frequencyAt(i),bin=hz*fftSize/rate;const auto lo=static_cast<unsigned>(bin);const double t=bin-lo;for(unsigned lane=0;lane<2;++lane){double value=-240.;if(lo+1<fftSize/2){const double p=(1.-t)*power[lane][lo]+t*power[lane][lo+1];value=10.*std::log10(std::max(p,1.e-24));}(lane==0?sub:bass)[i]=value;}}
    }
};
// Fixed left/right bars warn only ABOVE SUB -12 / BASS -6 dBFS sample peak.
// These are separate from the RMS spectrum. Only
// new audio blocks retrigger the 2.5 s hold, so stopped transport can decay.
struct PeakHighlights {
    static constexpr std::array<double,2>referenceDb{-12.,-6.};
    static constexpr double sampleToleranceDb=1.e-6; // float-safe ceiling quantization
    std::array<double,2>held{-240.,-240.},remaining{},redRemaining{};
    std::array<unsigned,2>severity{};
    void clear()noexcept{held.fill(-240.);remaining.fill(0.);redRemaining.fill(0.);severity.fill(0);}
    void update(const std::array<double,2>&levels,double seconds,bool newAudio=true)noexcept{
        for(unsigned lane=0;lane<2;++lane){remaining[lane]=std::max(0.,remaining[lane]-std::max(0.,seconds));redRemaining[lane]=std::max(0.,redRemaining[lane]-std::max(0.,seconds));const double level=levels[lane];
            if(newAudio&&std::isfinite(level)){if(level>referenceDb[lane]+sampleToleranceDb)remaining[lane]=2.5;if(level>-.01+sampleToleranceDb)redRemaining[lane]=2.5;held[lane]=std::max(held[lane],level);}
            severity[lane]=remaining[lane]>0.?(redRemaining[lane]>0.?2u:1u):0u;
        }
    }
};
}
