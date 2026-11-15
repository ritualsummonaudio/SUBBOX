#pragma once

// Included inside namespace subbox after the shared numeric helpers.
// C3-continuous geometric knee: integrate a quintic smoothstep slope.
// Unity below 92% of ceiling; exactly flat above 108%. No history, lookahead,
// allocation, oversampling filter, or sample delay. This is a sample-peak
// clipper; nonlinear aliasing and reconstructed intersample peaks remain.
struct OnyxClipper {
    static std::array<double,2> hardStereo(const std::array<double,2>&x,double ceiling)noexcept{
        const double peak=std::max(std::abs(x[0]),std::abs(x[1]));if(peak<=ceiling)return x;
        const double gain=ceiling/peak;return {std::clamp(x[0]*gain,-ceiling,ceiling),std::clamp(x[1]*gain,-ceiling,ceiling)};
    }
    static double withStart(double x,double start,double ceiling) noexcept {
        x=std::abs(clean(x));const double knee=ceiling-start;
        if(x<=start)return x;
        if(knee<1.e-9||x>=ceiling+knee)return ceiling;
        const double t=(x-start)/(2.*knee),t2=t*t,t4=t2*t2;
        return std::min(ceiling,start+2.*knee*(t+t4*(-2.5+t*(3.-t))));
    }
    static double magnitude(double x,double ceiling) noexcept {
        x=std::abs(clean(x));
        const double knee=.08*ceiling,start=ceiling-knee;
        if(x<=start)return x;
        if(x>=ceiling+knee)return ceiling;
        const double t=(x-start)/(2.*knee),t2=t*t,t4=t2*t2;
        return std::min(ceiling,start+2.*knee*(t+t4*(-2.5+t*(3.-t))));
    }
    static double sample(double x,double ceiling) noexcept {
        return std::copysign(magnitude(x,ceiling),x);
    }
    static std::array<double,2> stereo(const std::array<double,2>&x,double ceiling,double mix=1.) noexcept {
        const double peak=std::max(std::abs(x[0]),std::abs(x[1]));
        if(peak<=.92*ceiling)return x;
        const double gain=1.+clampNorm(mix)*(magnitude(peak,ceiling)/peak-1.);
        return {x[0]*gain,x[1]*gain};
    }
};

// Band knees react to the current sample and recover with headroom. The
// envelope moves the curve's knee only; it does not add automatic makeup gain
// or hold a gain-reduction value over the next note.
class AdaptiveBandClipper {
    double ceiling=.5,floor=.46,envelope=0.,release=0.;
public:
    double start=.46;
    void prepare(double sr,double limit)noexcept{ceiling=limit;floor=.92*limit;release=std::exp(-1./(.08*sr));clear();}
    void clear()noexcept{envelope=0.;start=floor;}
    std::array<double,2> tick(const std::array<double,2>&x,double mix=1.)noexcept{
        const double peak=std::max(std::abs(x[0]),std::abs(x[1]));envelope=std::max(peak,envelope*release);
        start=std::clamp(ceiling+floor-envelope,floor,ceiling-1.e-6);
        if(peak<=start||mix<=0.)return x;
        const double gain=OnyxClipper::withStart(peak,start,ceiling)/peak,wet=clampNorm(mix);
        std::array<double,2>y{x[0]*(1.+wet*(gain-1.)),x[1]*(1.+wet*(gain-1.))};
        if(wet==1.)for(auto&value:y)value=std::clamp(value,-ceiling,ceiling);
        return y;
    }
};

// One-octave, 75 ms pitch fall. The oscillator stays phase-continuous. A
// Schmitt onset detector and 90 ms refractory interval reject cycle retriggers.
// First unknown notes wait for the causal pitch tracker; main audio never waits.
class PitchGlide {
    double sr=48000.,fast=0.,slow=0.,fastDecay=0.,slowDecay=0.;
    unsigned age=0,quiet=0,refractory=0,pendingAge=0;
    bool armed=true,pending=false,active=false,wasEnabled=false;
public:
    unsigned triggers=0;
    void prepare(double rate) noexcept {
        sr=rate;fast=slow=0.;fastDecay=std::exp(-1./(.006*sr));slowDecay=std::exp(-1./(.060*sr));
        age=quiet=refractory=pendingAge=triggers=0;armed=true;pending=active=wasEnabled=false;
    }
    double tick(double input,bool locked,bool enabled) noexcept {
        const double a=std::abs(clean(input));fast=std::max(a,fast*fastDecay);slow=a+(slow-a)*slowDecay;
        if(refractory)--refractory;
        if(fast<.0005||fast<slow*.65){if(++quiet>=static_cast<unsigned>(.020*sr))armed=true;}else quiet=0;
        if(!enabled){pending=false;wasEnabled=false;}
        else {
            if((armed&&!refractory&&fast>std::max(.0015,slow*2.2))||(!wasEnabled&&locked&&fast>.0015)) {
                pending=true;pendingAge=0;armed=false;refractory=static_cast<unsigned>(.090*sr);
            }
            wasEnabled=true;
            if(pending&&++pendingAge>static_cast<unsigned>(.5*sr))pending=false;
            if(pending&&locked){age=0;active=true;pending=false;++triggers;}
        }
        if(!active)return 0.;
        const double t=std::min(1.,age++/(.075*sr)),t2=t*t,t3=t2*t;
        if(t>=1.){active=false;return 0.;}
        return 12.*(1.-t3*(10.+t*(-15.+6.*t)));
    }
};
