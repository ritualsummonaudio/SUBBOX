#pragma once

namespace onyx_update {
constexpr double subLowHz=20.,crossoverHz=60.,bassHighHz=150.;
constexpr double pi=3.1415926535897932384626433832795;
inline double sine(double x) noexcept {
    const double z=x*x;
    return x*(1.+z*(-1./6.+z*(1./120.+z*(-1./5040.+z*(1./362880.+z*(-1./39916800.+z*(1./6227020800.+z*(-1./1307674368000.))))))));
}
inline double cosine(double x) noexcept {
    const double z=x*x;
    return 1.+z*(-1./2.+z*(1./24.+z*(-1./720.+z*(1./40320.+z*(-1./3628800.+z*(1./479001600.+z*(-1./87178291200.)))))));
}
struct Biquad {
    double b0,b1,b2,a1,a2,z1,z2;
    void setup(double rate,double hz,bool high=false,double q=.7071067811865475) noexcept {
        const double w=pi*hz/rate,k=sine(w)/cosine(w),d=1.+k/q+k*k;
        b0=(high?1.:k*k)/d;b1=(high?-2.:2.*k*k)/d;b2=b0;
        a1=2.*(k*k-1.)/d;a2=(1.-k/q+k*k)/d;z1=z2=0.;
    }
    double tick(double x) noexcept {
        const double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;
        if(__builtin_fabs(z1)<1.e-25)z1=0.;if(__builtin_fabs(z2)<1.e-25)z2=0.;return y;
    }
};
static_assert(sizeof(Biquad)==56,"Native biquad layout");
struct FourthOrder {
    Biquad sections[2];
    void setup(double rate,double hz,bool high=false) noexcept {
        sections[0].setup(rate,hz,high,.541196100146197);
        sections[1].setup(rate,hz,high,1.3065629648763766);
    }
    double tick(double x) noexcept {return sections[1].tick(sections[0].tick(x));}
};
struct BassWindow {
    FourthOrder high,low;
    void setup(double rate) noexcept{high.setup(rate,crossoverHz,true);low.setup(rate,bassHighHz);}
    double tick(double x) noexcept{return low.tick(high.tick(x));}
};
struct Crossover {
    Biquad low[2],high[2];
    void setup(double rate,double hz) noexcept {
        for(unsigned i=0;i<2;++i){low[i].setup(rate,hz);high[i].setup(rate,hz,true);}
    }
};
struct Phase {
    Biquad filter;
    void setup(double rate,double hz) noexcept {
        filter.setup(rate,hz);filter.b0=filter.a2;filter.b1=filter.a1;filter.b2=1.;
    }
};
struct SourceBands {
    Crossover top,split,bottom;Phase bassPhase,upperPhase,upperBottomPhase;
    void setup(double rate) noexcept {
        top.setup(rate,bassHighHz);split.setup(rate,crossoverHz);bottom.setup(rate,subLowHz);
        bassPhase.setup(rate,subLowHz);upperPhase.setup(rate,crossoverHz);upperBottomPhase.setup(rate,subLowHz);
    }
};
static_assert(sizeof(SourceBands)==840,"Native source filter-bank layout");
}
