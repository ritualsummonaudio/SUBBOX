
static inline double magnitude(double x) noexcept { return __builtin_fabs(x); }
static bool repeats(void* object,unsigned pos) noexcept {
    const auto* raw=static_cast<const unsigned char*>(object);
    const double* l=reinterpret_cast<const double*>(raw+0x12000)+pos+1;
    const double* r=reinterpret_cast<const double*>(raw+0x14000)+pos+1;
    constexpr double tolerance=1.e-8;
    for(unsigned lag=16;lag<=128;++lag){
        if(magnitude(l[511]-l[511-lag])>tolerance||magnitude(l[510]-l[510-lag])>tolerance||magnitude(r[511]-r[511-lag])>tolerance||magnitude(r[510]-r[510-lag])>tolerance)continue;
        bool same=true;
        for(unsigned n=lag;n<512;++n){if(magnitude(l[n]-l[n-lag])>tolerance||magnitude(r[n]-r[n-lag])>tolerance){same=false;break;}}
        if(same){
            // Recover periodic headroom only for a simple low-band waveform.
            // Repeating broadband/noisy or rapid-edge patterns keep the full guard.
            bool simple=true;
            for(unsigned channel=0;channel<2&&simple;++channel){
                const double* p=channel?r:l;unsigned turns=0;int first=0,last=0;
                for(unsigned j=0;j<lag;++j){
                    const double delta=p[512-lag+j]-p[512-lag+(j+lag-1)%lag];
                    if(magnitude(delta)>.4){simple=false;break;}
                    const int sign=delta>1.e-8?1:(delta< -1.e-8?-1:0);
                    if(sign){if(!first)first=sign;if(last&&last!=sign)++turns;last=sign;}
                }
                if(first&&last&&first!=last)++turns;
                if(turns>2)simple=false;
            }
            if(simple)return true;
        }
    }
    return false;
}
static double target(void* object, unsigned positionOffset, double safe, double detectedPeak) noexcept {
    if (!(detectedPeak>safe)) return safe;
    const auto* raw=static_cast<const unsigned char*>(object);
    const unsigned pos=*reinterpret_cast<const unsigned*>(raw+positionOffset)&511u;
    using V2 = double __attribute__((vector_size(16)));
    constexpr double nominal=0.98627943826364084;
    V2 maximum={0.,0.};
    for(unsigned channel=0;channel<2;++channel){
        const double* p=reinterpret_cast<const double*>(raw+0x12000+channel*0x2000)+pos+1;
        for(unsigned n=2;n<512;n+=2){
            V2 a,b,c;__builtin_memcpy(&a,p+n,16);__builtin_memcpy(&b,p+n-1,16);__builtin_memcpy(&c,p+n-2,16);
            V2 d=(a-b)-(b-c);
            d=__builtin_ia32_maxpd(d,-d);
            if(__builtin_ia32_movmskpd(d>=(V2){.020,.020})) return repeats(object,pos)?nominal:safe;
            maximum=__builtin_ia32_maxpd(maximum,d);
        }
    }
    const double rough=maximum[0]>maximum[1]?maximum[0]:maximum[1];
    // A completely constant stereo plateau has no inter-sample crest. Let a
    // sufficiently driven plateau reach the float-safe requested hard limit.
    if(rough<1.e-12){
        const double* l=reinterpret_cast<const double*>(raw+0x12000)+pos+1;
        const double* r=reinterpret_cast<const double*>(raw+0x14000)+pos+1;
        if(magnitude(l[511]-l[0])<1.e-12&&magnitude(r[511]-r[0])<1.e-12)
            return .9885530471801758;
    }
    // Retain the original conservative guard for abrupt/noisy/high-frequency
    // material. Recover unused margin only after a complete smooth history.
    const double low=.0020,high=.020;
    if(rough>low && repeats(object,pos)) return nominal;
    double w=(high-rough)/(high-low);
    w=w<0.?0.:(w>1.?1.:w);
    w=w*w*(3.-2.*w);
    // Retain a small 0.02 dB reconstruction reserve at the recovered end.
    return safe+(nominal-safe)*w;
}
extern "C" double sbo_target_a(void* object,double peak) noexcept {return target(object,0x22288,0.96050577225698053,peak);}
extern "C" double sbo_target_b(void* object,double peak) noexcept {return target(object,0x1e288,0.96605083259375335,peak);}
