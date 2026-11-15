#include <string>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#endif
#include <cstdlib>
#ifndef _WIN32
static void crash(int n,siginfo_t*,void* v){auto*u=(ucontext_t*)v;char s[160];int z=snprintf(s,sizeof(s),"FAULT signal=%d RIP=%llx address-only isolated guard probe\n",n,(unsigned long long)u->uc_mcontext.gregs[REG_RIP]);write(2,s,z);_exit(100+n);}
#endif
static uint16_t u16(const unsigned char*p){uint16_t v;memcpy(&v,p,2);return v;}
static uint32_t u32(const unsigned char*p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t u64(const unsigned char*p){uint64_t v;memcpy(&v,p,8);return v;}
int main(int argc,char**argv){
 if(argc<4)return 2;
#ifdef _WIN32
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#else
 struct sigaction act{};act.sa_sigaction=crash;act.sa_flags=SA_SIGINFO;sigaction(SIGSEGV,&act,nullptr);sigaction(SIGILL,&act,nullptr);sigaction(SIGFPE,&act,nullptr);
#endif
 std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char>b((std::istreambuf_iterator<char>(f)),{});auto nt=b.data()+u32(b.data()+0x3c);auto op=nt+24;auto preferredBase=u64(op+24);auto imageBase=std::getenv("SBO_PROBE_BASE")?std::strtoull(std::getenv("SBO_PROBE_BASE"),nullptr,0):preferredBase;auto sz=u32(op+56);
#ifdef _WIN32
 auto base=reinterpret_cast<unsigned char*>(LoadLibraryA(argv[1]));
 if(!base){std::cerr<<"Native Windows LoadLibrary failed "<<GetLastError()<<"\n";return 3;}
#else
auto base=(unsigned char*)mmap((void*)imageBase,sz,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);if(base==MAP_FAILED){perror("mmap");return 3;}memcpy(base,b.data(),u32(op+60));auto sec=op+u16(nt+20);for(unsigned i=0;i<u16(nt+6);++i){auto s=sec+i*40;memcpy(base+u32(s+12),b.data()+u32(s+20),u32(s+16));}if(imageBase!=preferredBase){auto directory=u32(op+152);auto total=u32(op+156);for(unsigned cursor=directory;cursor<directory+total;){auto page=u32(base+cursor),size=u32(base+cursor+4);if(size<8)return 11;for(unsigned at=cursor+8;at<cursor+size;at+=2){auto item=u16(base+at);if((item>>12)==10){auto target=base+page+(item&4095);auto value=u64(target)+imageBase-preferredBase;memcpy(target,&value,8);}else if((item>>12)!=0)return 12;}cursor+=size;}}
#endif

 if(argc>4&&std::string(argv[4])!="file"){double r=std::stod(argv[4]);auto lim=.9988493323326111*std::pow(10.,-r/20.);memcpy(base+0x971c8,&lim,8);lim=.9988493323326111*std::pow(10.,-std::max(0.,r-.05)/20.);memcpy(base+0x971d8,&lim,8);}
 using Prep=void(__attribute__((ms_abi))*)(void*,double);using Tick=void*(__attribute__((ms_abi))*)(void*,void*,void*,bool);
 auto prepA=(Prep)(base+0x12900),prepB=(Prep)(base+0x11bd0);auto tick=(Tick)(base+0x4ec0);std::vector<unsigned char>state(0x50000);std::cerr<<"preparing\n";const double rate=argc>5?std::stod(argv[5]):48000.;prepA(state.data(),rate);prepB(state.data()+0x222a8,rate);std::cerr<<"prepared\n";
 using Master=void*(__attribute__((ms_abi))*)(void*,void*,const void*,double,unsigned,double);auto master=(Master)(base+0x4250);alignas(16)std::array<unsigned char,64> masterState{};double release=std::exp(-1./(.08*rate)),soft=.8922776195878269;unsigned hold=static_cast<unsigned>(std::lround(.08*rate));std::memcpy(masterState.data()+0x18,&release,8);std::memcpy(masterState.data()+0x24,&hold,4);std::memcpy(masterState.data()+0x30,&soft,8);const unsigned os=argc>7?std::stoul(argv[7]):1;constexpr double pi=3.14159265358979323846;const double lim=*reinterpret_cast<double*>(base+0x971f0);unsigned mode=std::stoul(argv[2]);std::mt19937 rng(argc>6?std::stoul(argv[6]):340);std::uniform_real_distribution<double>noise(-1.,1.);std::ofstream out(argv[3],std::ios::binary);double peak=0.;std::vector<double>patternL(os),patternR(os);if(mode>=40)for(unsigned j=0;j<os;++j){patternL[j]=noise(rng);patternR[j]=noise(rng);}
 const unsigned delay=std::getenv("SBO_EXPECT_DELAY")?std::stoul(std::getenv("SBO_EXPECT_DELAY")):384;const unsigned count=mode>=20&&mode<40?static_cast<unsigned>(rate*6)+4096:48512;for(unsigned n=0;n<count;++n){double x=0.,r=0.,i=double(n)-4096.;if(n>=4096&&n<44096){switch(mode){case 0:x=2.*sin(pi*.5*i+pi*.25);break;case 1:x=2.*sin(2*pi*.499*i+.7);break;case 2:x=noise(rng);r=noise(rng);break;case 3:case 4:{double t=i-20000.-.5;unsigned width=mode==3?128:2048;if(std::abs(t)<=width)x=std::copysign(1.,sin(pi*t)/(pi*t));break;}case 5:x=(n%2?1.:-1.)*(n<24000?1.:-1.);break;case 6:x=(.7+exp(-fmod(i,2000.)*.02))*sin(2*pi*.333*i);break;case 7:x=2.*sin(2*pi*(.001*i+.000006*i*i));break;case 8:x=1.;break;case 9:x=n%997==0?1.:0.;break;case 10:x=4*noise(rng);break;case 11:x=2.*fmod(i/127.,1.)-1.;break;case 12:x=sin(2*pi*.41*i)+sin(2*pi*.333*i+.8)+sin(2*pi*.171*i+1.1);break;case 13:x=(n%7<3?1.:-1.);break;case 14:x=n%512<16?2.*sin(pi*.5*i+pi*.25):0.;break;default:x=2.*sin(pi*.5*i+.25*(n/256));break;}}
 x=std::clamp(x,-lim,lim);if(mode!=2)r=-.7*x;if(mode>=40){double t=(double(n)-4096.)/rate;bool present=n>=4096&&n<44096;const unsigned j=(n+os-4096%os)%os;x=present?lim*(mode==41?(patternL[j]>0?1.:-1.):patternL[j]):0.;r=present?lim*(mode==41?(patternR[j]>0?1.:-1.):patternR[j]):0.;if(mode==42){x=present?lim*(1.-4.*std::abs(double(j)/os-.5)):0.;r=-.7*x;}if(mode==43){x=present?lim*std::pow(.5+.5*std::cos(2*pi*j/os),4):0.;r=-.7*x;}}else if(mode>=30){double t=(double(n)-4096.)/rate;bool present=t>=0.&&t<5.8;x=present?(mode==30?4.:(mode==32?2.:mode==33?.5:2.)*sin(2*pi*(mode==32?40.:1000.)*t+.25)):0.;r=mode==34?.25*x:x;std::array<double,2> input{x,r},shaped{};master(masterState.data(),shaped.data(),input.data(),1.,os,1.);x=shaped[0];r=shaped[1];}else if(mode>=20){const double t=(double(n)-4096.)/rate;const double duration=5.8;const bool present=t>=0.&&t<duration;double hz=mode==21?40.:(mode==22?100.:(mode==23?250.:1000.));x=present?(mode==20?lim:lim*sin(2*pi*hz*t+.25)):0.;r=-.7*x;}else if(mode>=16){x=(mode==16?1.7:.01)*sin(2*pi*.37*n);r=(mode==16?-.7:-.007)*cos(2*pi*.23*n);}alignas(16)std::array<double,18>in{},y{};in[0]=x;in[1]=r;in[8]=.001*n;tick(state.data(),y.data(),in.data(),mode!=16);if(mode>=16&&mode<20){double a=n>=delay?(mode==16?1.7:.01)*sin(2*pi*.37*(n-delay)):0.;double b=n>=delay?(mode==16?-.7:-.007)*cos(2*pi*.23*(n-delay)):0.;double metadata=n>=delay?.001*(n-delay):0.;if(y[0]!=a||y[1]!=b||y[8]!=metadata){std::cerr<<"DELAY/DATA MISMATCH at "<<n<<"\n";return 7;}}if(mode==33){double t=(double(n)-delay-4096.)/rate;double expect=t>=0.&&t<5.8?.5*sin(2*pi*1000.*t+.25):0.;if(y[0]!=expect||y[1]!=expect){std::cerr<<"SUB-SOFT TRANSPARENCY MISMATCH at "<<n<<"\n";return 13;}}if(mode==34&&y[0]!=4*y[1]){std::cerr<<"STEREO LINK MISMATCH at "<<n<<"\n";return 14;}if(std::getenv("SBO_DUMP_DSP")&&mode>=30&&n==static_cast<unsigned>(rate*4)+4096){std::cerr<<"MASTER current="<<x<<" out="<<y[0]<<" knee="<<*reinterpret_cast<double*>(masterState.data()+0x30)<<"\n";for(unsigned stage=0;stage<2;++stage){auto*mem=state.data()+(stage?0x222a8:0);auto pos=*reinterpret_cast<unsigned*>(mem+(stage?0x1e288:0x22288))&511u;auto*l=reinterpret_cast<double*>(mem+0x12000)+pos;auto*r=reinterpret_cast<double*>(mem+0x14000)+pos;double rough=0.,top=0.,best=100.;unsigned bestLag=0;for(unsigned j=2;j<512;++j){rough=std::max({rough,std::abs((l[j]-l[j-1])-(l[j-1]-l[j-2])),std::abs((r[j]-r[j-1])-(r[j-1]-r[j-2]))});top=std::max({top,std::abs(l[j]),std::abs(r[j])});}for(unsigned lag=2;lag<=128;++lag){double error=0.;for(unsigned j=lag;j<512;++j)error=std::max({error,std::abs(l[j]-l[j-lag]),std::abs(r[j]-r[j-lag])});if(error<best){best=error;bestLag=lag;}}std::cerr<<"STAGE "<<stage<<" rough="<<rough<<" topDB="<<20*log10(top)<<" bestLag="<<bestLag<<" repeatError="<<best<<"\n";}}peak=std::max({peak,std::abs(y[0]),std::abs(y[1])});out.write((char*)y.data(),16);
 }
 std::cout<<"MODE "<<mode<<" SAMPLE_DBFS "<<20*log10(peak)<<'\n';
#ifdef _WIN32
 FreeLibrary(reinterpret_cast<HMODULE>(base));
#else
 munmap(base,sz);
#endif

}
