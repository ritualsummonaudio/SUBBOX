#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
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
#ifndef _WIN32
static void crash(int n,siginfo_t*info,void*v){auto*u=(ucontext_t*)v;char s[256];int z=snprintf(s,sizeof(s),"FAULT signal=%d RIP=%llx addr=%llx isolated engine probe\n",n,(unsigned long long)u->uc_mcontext.gregs[REG_RIP],(unsigned long long)info->si_addr);write(2,s,z);_exit(100+n);}
#endif
static double db(double x){return x>1.e-150?20*std::log10(x):-300.;}
static uintptr_t hostMeterAddress=0; static unsigned hostMeterLength=0; static uintptr_t memoryEngines[2]{};static unsigned discardCalls=0;static unsigned long long discardedBytes=0;
static unsigned __attribute__((ms_abi)) discardUnused(void*address,unsigned long long size){
 const auto a=(uintptr_t)address;bool valid=false;
 for(auto e:memoryEngines)if(e){
   if(a>=e+0x10bd0&&a+size<=e+0x363d0)valid=true;
   if(a>=e+0x36638&&a+size<=e+0x41538)valid=true;
 }
 if(hostMeterAddress)for(unsigned lane=0;lane<2;++lane){auto b=hostMeterAddress+lane*0x12c00;if(a>=b+8ull*hostMeterLength&&a+size<=b+0x12c00)valid=true;}
 if(!valid||a%4096||size%4096||!size)_exit(31);
 ++discardCalls;discardedBytes+=size;
 if(std::getenv("SBO_DISCARD_POISON"))std::memset(address,0xa5,size);else {
#ifdef _WIN32
    (void)DiscardVirtualMemory(address,size);
#else
    if(madvise(address,size,MADV_DONTNEED))_exit(32);
#endif
 }
 return 0;
}
static void* __attribute__((ms_abi)) moduleStub(const unsigned short*){return reinterpret_cast<void*>(1);}
static void* __attribute__((ms_abi)) procStub(void*,const char*name){if(std::strcmp(name,"DiscardVirtualMemory"))_exit(33);return std::getenv("SBO_DISCARD_UNAVAILABLE")?nullptr:reinterpret_cast<void*>(&discardUnused);}
template<class T>T at(const unsigned char*p){T v;memcpy(&v,p,sizeof(v));return v;}
int main(int argc,char**argv){
 if(argc<10)return 2;
#ifdef _WIN32
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#else
 struct sigaction act{};act.sa_sigaction=crash;act.sa_flags=SA_SIGINFO;for(int n:{SIGSEGV,SIGILL,SIGFPE,SIGABRT})sigaction(n,&act,nullptr);
#endif
 std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char>b((std::istreambuf_iterator<char>(f)),{});auto nt=b.data()+at<uint32_t>(b.data()+0x3c);auto op=nt+24;auto preferredBase=at<uint64_t>(op+24);auto imageBase=std::getenv("SBO_PROBE_BASE")?std::strtoull(std::getenv("SBO_PROBE_BASE"),nullptr,0):preferredBase;auto sz=at<uint32_t>(op+56);
#ifdef _WIN32
 auto base=reinterpret_cast<unsigned char*>(LoadLibraryA(argv[1]));
 if(!base){std::cerr<<"Native Windows LoadLibrary failed "<<GetLastError()<<"\n";return 3;}
 if(!GetProcAddress(reinterpret_cast<HMODULE>(base),"GetPluginFactory"))return 18;
 DWORD oldProtect=0;VirtualProtect(base+0x7c230,16,PAGE_READWRITE,&oldProtect);
#else
 auto base=(unsigned char*)mmap((void*)imageBase,sz,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);if(base==MAP_FAILED){perror("mmap");return 3;}
 memcpy(base,b.data(),at<uint32_t>(op+60));auto sec=op+at<uint16_t>(nt+20);for(unsigned i=0;i<at<uint16_t>(nt+6);++i){auto s=sec+i*40;memcpy(base+at<uint32_t>(s+12),b.data()+at<uint32_t>(s+20),at<uint32_t>(s+16));}
 if(imageBase!=preferredBase){unsigned start=at<uint32_t>(op+152),length=at<uint32_t>(op+156);for(unsigned cursor=start;cursor<start+length;){unsigned page=at<uint32_t>(base+cursor),block=at<uint32_t>(base+cursor+4);if(block<8)return 12;for(unsigned pos=cursor+8;pos<cursor+block;pos+=2){unsigned item=at<uint16_t>(base+pos);if((item>>12)==10){auto target=base+page+(item&4095);uint64_t value=at<uint64_t>(target)+imageBase-preferredBase;memcpy(target,&value,8);}else if(item>>12)return 13;}cursor+=block;}}
#endif
 *reinterpret_cast<void**>(base+0x7c238)=reinterpret_cast<void*>(&moduleStub);*reinterpret_cast<void**>(base+0x7c230)=reinterpret_cast<void*>(&procStub);
#ifdef _WIN32
 DWORD ignoredProtect=0;VirtualProtect(base+0x7c230,16,oldProtect,&ignoredProtect);
 std::cerr<<"Native Windows VST3 DLL loader and factory export passed\n";
#endif
 double hz=std::stod(argv[2]),sub=std::stod(argv[3]),bass=std::stod(argv[4]),amp=std::stod(argv[7]),rate=argc>10?std::stod(argv[10]):48000.,seconds=std::stod(argv[8]);unsigned clip=std::stoul(argv[5]),solo=std::stoul(argv[6]);
 std::array<double,20>p{.5,0.,0.,0.,0.,0.,100./280.,1.,0.,.5,0.,0.,0.,0.,0.,0.,.4,.5,0.,0.};if(std::getenv("SBO_PROBE_NATIVE_DEFAULTS"))std::memcpy(p.data(),base+0x8fa40,160);p[0]=(bass+30.)/60.;p[17]=(sub+30.)/60.;p[12]=clip*.5;p[13]=solo&1?1.:0.;p[14]=solo&2?1.:0.;
 using Prep=void(__attribute__((ms_abi))*)(void*,double,const void*);
 using Tick=void*(__attribute__((ms_abi))*)(void*,void*,double,double,double,double,bool,const void*,double);
 auto prep=(Prep)(base+0x5020);auto tick=(Tick)(base+0x5ee0);
 const unsigned expectedLatency=std::getenv("SBO_EXPECT_DELAY")?std::stoul(std::getenv("SBO_EXPECT_DELAY")):384;
 using Latency=unsigned(__attribute__((ms_abi))*)(void*);if(((Latency)(base+0x9b10))(nullptr)!=expectedLatency)return 16;
 std::vector<unsigned char>tailObject(0x1c0000);std::memcpy(tailObject.data()+0x1b20b8,&rate,8);if(((Latency)(base+0x9b20))(tailObject.data())!=static_cast<unsigned>(std::ceil(rate*2.))+expectedLatency)return 17;
 std::vector<unsigned char>state(0x100000),guardState(0x80000);memoryEngines[0]=(uintptr_t)state.data();
 using GuardPrep=void(__attribute__((ms_abi))*)(void*,double);using GuardTick=void*(__attribute__((ms_abi))*)(void*,void*,const void*,bool);
 bool useGuard=std::getenv("SBO_ENGINE_FINAL_GUARD");if(useGuard){((GuardPrep)(base+0x12900))(guardState.data(),rate);((GuardPrep)(base+0x11bd0))(guardState.data()+0x222a8,rate);}
 const unsigned automation=std::getenv("SBO_ENGINE_AUTOMATION")?std::stoul(std::getenv("SBO_ENGINE_AUTOMATION")):0;
 for(auto setting:std::array<std::pair<const char*,unsigned>,9>{{{"SBO_ENGINE_BASS_STEEP",3},{"SBO_ENGINE_SUB_STEEP",4},{"SBO_ENGINE_PGE",19},{"SBO_ENGINE_MONO",15},{"SBO_ENGINE_BYPASS",10},{"SBO_ENGINE_SIDECHAIN",9},{"SBO_ENGINE_PHASE",11},{"SBO_ENGINE_SYNC",7},{"SBO_ENGINE_BPM",6}}})if(auto value=std::getenv(setting.first))p[setting.second]=std::stod(value);
 if(auto value=std::getenv("SBO_ENGINE_OS"))p[16]=std::stod(value);
 auto fullSettings=p;if(automation){p[0]=p[17]=.5;}
 double allPeak=0.,inputPeak=0.;
double license=std::getenv("SBO_TEST_LICENSE")?std::stod(std::getenv("SBO_TEST_LICENSE")):1.;std::memcpy(state.data()+0xafc20,&license,8);
std::cerr<<"Engine preparing\n";prep(state.data(),rate,p.data());std::cerr<<"Engine prepared\n";
 std::vector<unsigned char>other;auto otherP=p;bool multi=std::getenv("SBO_ENGINE_MULTI_INSTANCE");if(multi){other.resize(0x100000);memoryEngines[1]=(uintptr_t)other.data();std::memcpy(other.data()+0xafc20,&license,8);otherP[0]=.7;otherP[17]=.2;otherP[12]=.5;prep(other.data(),rate,otherP.data());}
 std::vector<unsigned char>hostMeter(0x26000);hostMeterAddress=(uintptr_t)hostMeter.data();hostMeterLength=std::min(9600u,std::max(1u,(unsigned)std::lround(rate*.05)));bool meter=std::getenv("SBO_TEST_POST_METER");if(meter)((GuardPrep)(base+0x4c30))(hostMeter.data(),rate);
 std::vector<double>fileData;if(auto name=std::getenv("SBO_DIAG_INPUT_F64LE")){std::ifstream inputFile(name,std::ios::binary);std::vector<char>raw((std::istreambuf_iterator<char>(inputFile)),{});if(raw.size()%16)return 14;fileData.resize(raw.size()/8);std::memcpy(fileData.data(),raw.data(),raw.size());}
 const auto count=fileData.empty()?static_cast<unsigned>(rate*seconds):static_cast<unsigned>(fileData.size()/2);double sum=0.,input=0.,peak=0.,ss=0.,cc=0.,sc=0.,sy=0.,cy=0.;unsigned measured=0;std::ofstream out(argv[9],std::ios::binary);
 for(unsigned n=0;n<count;++n){constexpr double pi=3.14159265358979323846;double x=amp*std::sin(2*pi*hz*n/rate+.25);if(std::getenv("SBO_ENGINE_MULTITONE")){x=0.;for(unsigned j=1;j<=16;++j)x+=amp/16.*std::sin(2*pi*(20.+j*23.)*n/rate-pi*j*(j-1)/16.);}
 unsigned cycleSample=n;if(std::getenv("SBO_PROBE_REPREPARE")&&n>=count/2){cycleSample=n-count/2;if(n==count/2){prep(state.data(),rate,p.data());if(useGuard){((GuardPrep)(base+0x12900))(guardState.data(),rate);((GuardPrep)(base+0x11bd0))(guardState.data()+0x222a8,rate);}}x=amp*std::sin(2*pi*hz*cycleSample/rate+.25);}
 if(std::getenv("SBO_DIAG_FLAT_THIRD"))x=amp*(std::sin(2*pi*(hz/3.)*n/rate)+std::sin(2*pi*hz*n/rate)/6.);
 if(!fileData.empty())x=fileData[2*n];
 if(std::getenv("SBO_PROBE_PAD")&&(n<1024||n>=count-std::min(count/2,std::max(2048u,static_cast<unsigned>(rate*.3)))))x=0.;
 if(std::getenv("SBO_ENGINE_SILENT_TAIL")&&n>=count/2)x=0.;if(std::getenv("SBO_ENGINE_SILENT_GAP")&&double(n)/rate>=1.&&double(n)/rate<3.5)x=0.;
 if(automation){p=fullSettings;if(n<count/3){p[0]=p[17]=.5;}else if(automation==2&&n>=2*count/3){p[0]=p[17]=.5;}else if(automation==3){double w=(double(n)-count/3.)/(count*2./3.);p[0]=.5+w*(fullSettings[0]-.5);p[17]=.5+w*(fullSettings[17]-.5);}}
 if(automation==6){p=fullSettings;if(n<count/3){if(fullSettings[0]!=.5)p[0]=1.;if(fullSettings[17]!=.5)p[17]=1.;}else if(n>=2*count/3){p[0]=p[17]=.5;}}
 if(automation==4)p[12]=(n>=count/3&&n<2*count/3)?fullSettings[12]:0.;if(automation==5){p[13]=(n>=count/3&&n<2*count/3)?fullSettings[13]:0.;p[14]=(n>=count/3&&n<2*count/3)?fullSettings[14]:0.;}
 if(automation==8)p[12]=n<count/3?1.:.5;
 if(automation==9)p[10]=(n>=count/3&&n<2*count/3)?1.:0.;
 if(automation==10){license=n<count/3?0.:n<2*count/3?double(n-count/3)/(count/3.):1.;std::memcpy(state.data()+0xafc20,&license,8);}
 if(automation==12)p[16]=std::min(5u,static_cast<unsigned>(double(n)*6/count))*.2;
 double scLeft=0.,scRight=0.;bool scAvailable=std::getenv("SBO_PROBE_SC_AVAILABLE");if(scAvailable){scLeft=.13*std::sin(2*pi*73.48469228349535*n/rate+.7);scRight=.09*std::sin(2*pi*40*n/rate+1.1);}
 alignas(16)std::array<double,18>y{};tick(state.data(),y.data(),x,fileData.empty()?.7*x:fileData[2*n+1],scLeft,scRight,scAvailable,p.data(),120.);if(multi){alignas(16)std::array<double,18>otherY{};tick(other.data(),otherY.data(),-.4*x,x,0.,0.,false,otherP.data(),127.);}
 if(std::getenv("SBO_TEST_HOST_AUTH")){for(unsigned c=0;c<2;++c){double main=c?(fileData.empty()?.7*x:fileData[2*n+1]):x;y[c]=main+license*(y[c]-main);y[2+c]*=license;y[4+c]*=license;}if(p[12]>=.75&&p[10]<.5){double pk=std::max(std::abs(y[0]),std::abs(y[1]));if(pk>.9885530471801758){double g=.9885530471801758/pk;y[0]*=g;y[1]*=g;}}}
 unsigned context=cycleSample+1;std::memcpy((unsigned char*)y.data()+0x84,&context,4);
 if(useGuard){alignas(16)std::array<double,18>delayed{};((GuardTick)(base+0x4ec0))(guardState.data(),delayed.data(),y.data(),p[12]>=.75&&p[10]<.5);y=delayed;unsigned got=at<unsigned>((const unsigned char*)y.data()+0x84),expected=cycleSample>=expectedLatency?cycleSample-expectedLatency+1:0;if(got!=expected){std::cerr<<"Analysis context delay mismatch "<<n<<" "<<got<<" != "<<expected<<"\n";return 15;}}
 if(meter){using Meter=void*(__attribute__((ms_abi))*)(void*,double*,const double*,const double*);((Meter)(base+0x4cf0))(hostMeter.data(),y.data()+6,y.data()+2,y.data()+4);}
 allPeak=std::max(allPeak,std::abs(y[0]));inputPeak=std::max(inputPeak,std::abs(x));if(n>=count/2){sum+=y[0]*y[0];input+=x*x;double sn=std::sin(2*pi*hz*n/rate+.25),cs=std::cos(2*pi*hz*n/rate+.25);ss+=sn*sn;cc+=cs*cs;sc+=sn*cs;sy+=sn*y[0];cy+=cs*y[0];peak=std::max(peak,std::abs(y[0]));++measured;}if(!std::isfinite(y[0]))return 4;out.write((char*)y.data(),sizeof(y));}
 double det=ss*cc-sc*sc,a=(sy*cc-cy*sc)/det,bfit=(cy*ss-sy*sc)/det;std::cout.precision(13);std::cout<<"{\"all_peak_dbfs\":"<<db(allPeak)<<",\"input_peak_dbfs\":"<<db(inputPeak)<<",\"frequency\":"<<hz<<",\"sub_db\":"<<sub<<",\"bass_db\":"<<bass<<",\"clip\":"<<clip<<",\"solo\":"<<solo<<",\"tone_gain_db\":"<<db(std::hypot(a,bfit)/amp)<<",\"tone_phase_deg\":"<<std::atan2(bfit,a)*180./3.14159265358979323846<<",\"gain_db\":"<<db(input>0.?std::sqrt(sum/input):0.)<<",\"peak_dbfs\":"<<db(peak)<<",\"frames\":"<<count<<"}\n";
 std::cerr<<"Memory discard calls="<<discardCalls<<" eligible_bytes="<<discardedBytes<<"\n";
 
#ifdef _WIN32
 FreeLibrary(reinterpret_cast<HMODULE>(base));
#else
 munmap(base,sz);
#endif

}
