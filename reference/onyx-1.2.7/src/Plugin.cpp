#include "Plugin.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include <cstring>
#include <type_traits>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace subbox {
static double canonical(unsigned id,double value) noexcept{value=clampNorm(value);return id==Oversample?oversampleIndex(value)/5.:(id==Sidechain||id==Clip)?std::round(value*2.)/2.:toggleParam(id)?(value>=.5?1.:0.):value;}
Plugin::Plugin(){for(unsigned i=0;i<ParamCount;++i)values[i].store(defaults[i]);for(auto&a:sidechainActive)a.store(true);for(auto&c:sidechainName)c.store(0);processContextRequirements.needTempo().needProjectTimeMusic().needTransportState();}
std::array<double,ParamCount> Plugin::snapshot()const noexcept{std::array<double,ParamCount>p{};for(unsigned i=0;i<ParamCount;++i)p[i]=values[i].load(std::memory_order_relaxed);return p;}
tresult PLUGIN_API Plugin::initialize(FUnknown* context){
    const auto result=SingleComponentEffect::initialize(context);if(result!=kResultOk)return result;licensing.initialize();licenseMix=licensing.authorized.load()?1.:0.;
    addAudioInput(STR16("Stereo In"),SpeakerArr::kStereo);
    addAudioInput(STR16("Sidechain"),SpeakerArr::kStereo,kAux,BusInfo::kDefaultActive);
    addAudioOutput(STR16("Stereo Out"),SpeakerArr::kStereo);
    parameters.addParameter(new RangeParameter(STR16("BASS"),Bass,STR16("dB"),-30.,30.,0.));
    parameters.addParameter(new RangeParameter(STR16("SUB"),Sub,STR16("dB"),-30.,30.,0.));
    parameters.addParameter(new RangeParameter(STR16("Legacy Generated Sub"),LegacySub,STR16("%"),0.,100.,0.,0,ParameterInfo::kCanAutomate|ParameterInfo::kIsHidden));
    parameters.addParameter(new RangeParameter(STR16("PHASE ALIGN"),Phase,STR16("ms"),0.,20.,0.));
    parameters.addParameter(new RangeParameter(STR16("BASS STEEP"),BassSteep,STR16("%"),0.,100.,0.));
    parameters.addParameter(new RangeParameter(STR16("SUB STEEP"),SubSteep,STR16("%"),0.,100.,0.));
    parameters.addParameter(new RangeParameter(STR16("Manual BPM"),Bpm,STR16("BPM"),20.,300.,120.));
    parameters.addParameter(STR16("Rhythm Sync"),nullptr,1,1.,ParameterInfo::kCanAutomate,HostSync);
    // Keep stable parameter IDs and expose descriptive host information.
    // Oversampling is a persistent quality choice with zero reported PDC.
    auto*clip=new StringListParameter(STR16("CLIP"),Clip,nullptr,ParameterInfo::kCanAutomate|ParameterInfo::kIsList);for(auto name:{STR16("OFF"),STR16("SUB BASS"),STR16("SUB BASS OUTPUT")})clip->appendString(name);parameters.addParameter(clip);
    parameters.addParameter(STR16("Pitch Glide Envelope"),nullptr,1,0.,ParameterInfo::kCanAutomate,Pge);
    parameters.addParameter(STR16("MONO below 80 Hz"),nullptr,1,0.,ParameterInfo::kCanAutomate,Mono);
    auto*os=new StringListParameter(STR16("OS Oversample"),Oversample,nullptr,ParameterInfo::kIsList);for(auto name:{STR16("1x"),STR16("2x"),STR16("4x"),STR16("8x"),STR16("16x"),STR16("32x")})os->appendString(name);os->getInfo().defaultNormalizedValue=defaults[Oversample];os->setNormalized(defaults[Oversample]);parameters.addParameter(os);
    parameters.addParameter(STR16("SUB SOLO"),nullptr,1,0.,ParameterInfo::kCanAutomate,SubSolo);
    parameters.addParameter(STR16("BASS SOLO"),nullptr,1,0.,ParameterInfo::kCanAutomate,BassSolo);
    auto*source=new StringListParameter(STR16("Audio Source"),Sidechain,nullptr,ParameterInfo::kCanAutomate|ParameterInfo::kIsList);for(auto name:{STR16("MAIN"),STR16("AUTO"),STR16("SIDECHAIN")})source->appendString(name);source->getInfo().defaultNormalizedValue=.5;source->setNormalized(.5);parameters.addParameter(source);
    parameters.addParameter(STR16("Bypass"),nullptr,1,0.,ParameterInfo::kCanAutomate|ParameterInfo::kIsBypass,Bypass);
    engine.prepare(sampleRate.load(),snapshot());return kResultOk;
}
tresult PLUGIN_API Plugin::setBusArrangements(SpeakerArrangement* ins,int32 ni,SpeakerArrangement* outs,int32 no){
    if(!ins||!outs||ni<1||ni>2||no!=1)return kResultFalse;
    if((ins[0]!=SpeakerArr::kMono&&ins[0]!=SpeakerArr::kStereo)||outs[0]!=ins[0])return kResultFalse;
    if(ni==2&&ins[1]!=SpeakerArr::kMono&&ins[1]!=SpeakerArr::kStereo)return kResultFalse;
    return SingleComponentEffect::setBusArrangements(ins,ni,outs,no);
}
tresult PLUGIN_API Plugin::activateBus(MediaType t,BusDirection d,int32 index,TBool active){
    if(index<0)return kInvalidArgument;
    const auto r=SingleComponentEffect::activateBus(t,d,index,active);
    if(r==kResultOk&&t==kAudio&&d==kInput&&index==1)sidechainActive[0].store(active!=0);
    return r;
}
tresult PLUGIN_API Plugin::canProcessSampleSize(int32 s){return s==kSample32||s==kSample64?kResultTrue:kResultFalse;}
tresult PLUGIN_API Plugin::setupProcessing(ProcessSetup& setup){
    if(!std::isfinite(setup.sampleRate)||setup.sampleRate<1000.||setup.sampleRate>192000.||setup.maxSamplesPerBlock<1)return kInvalidArgument;
    const auto r=SingleComponentEffect::setupProcessing(setup);if(r!=kResultOk)return r;
    licenseMix=licensing.authorized.load()?1.:0.;for(auto&d:unlicensedDelay)d.clear();sampleRate.store(setup.sampleRate);engine.prepare(setup.sampleRate,snapshot());configuredLatency.store(engine.latency());configuredOS.store(engine.oversampling());qualityPending.store(false);invalidateAnalysis();return kResultOk;
}
tresult PLUGIN_API Plugin::setActive(TBool state){licenseMix=licensing.authorized.load()?1.:0.;for(auto&d:unlicensedDelay)d.clear();engine.prepare(sampleRate.load(),snapshot());configuredLatency.store(engine.latency());configuredOS.store(engine.oversampling());qualityPending.store(false);invalidateAnalysis();if(!state)playing.store(false);return kResultOk;}
tresult PLUGIN_API Plugin::setProcessing(TBool state){licenseMix=licensing.authorized.load()?1.:0.;for(auto&d:unlicensedDelay)d.clear();engine.prepare(sampleRate.load(),snapshot());configuredLatency.store(engine.latency());configuredOS.store(engine.oversampling());qualityPending.store(false);invalidateAnalysis();if(!state)playing.store(false);return kResultOk;}
tresult PLUGIN_API Plugin::setParamNormalized(ParamID id,ParamValue value){
    if(!activeParam(id)||!std::isfinite(value))return kInvalidArgument;
    value=canonical(id,value);const auto previous=values[id].exchange(value);const auto r=SingleComponentEffect::setParamNormalized(id,value);
    if(!restoringState&&id==Sub){values[LegacySynthesis].store(0.);values[LegacySub].store(0.);SingleComponentEffect::setParamNormalized(LegacySub,0.);}
    if(!restoringState&&id==LegacySub)values[LegacySynthesis].store(1.);
    if(previous!=value&&(id==Sidechain||id==SubSolo||id==BassSolo||id==Clip||id==Bypass))invalidateAnalysis();
    if(previous!=value&&(id==Clip||id==Oversample)){qualityPending.store(true);if(!restoringState&&requestedLatency(snapshot())!=configuredLatency.load())if(auto*h=getComponentHandler())h->restartComponent(kLatencyChanged);}
    return r;
}
ParamValue PLUGIN_API Plugin::getParamNormalized(ParamID id){return activeParam(id)?values[id].load():0.;}
tresult PLUGIN_API Plugin::getParamValueByString(ParamID id,TChar* text,ParamValue& value){
    if(!text||!activeParam(id))return kInvalidArgument;
    if(toggleParam(id)||id==Sidechain){auto upper=[](TChar c){return c>=u'a'&&c<=u'z'?static_cast<TChar>(c-u'a'+u'A'):c;};if(upper(text[0])==u'O'&&upper(text[1])==u'N'&&text[2]==0){value=1.;return kResultOk;}if(upper(text[0])==u'O'&&upper(text[1])==u'F'&&upper(text[2])==u'F'&&text[3]==0){value=0.;return kResultOk;}}
    return SingleComponentEffect::getParamValueByString(id,text,value);
}
void Plugin::edit(Param id,double value){if(id==SubSteep){values[LegacySynthesis].store(0.);values[LegacySub].store(0.);SingleComponentEffect::setParamNormalized(LegacySub,0.);}beginEdit(id);setParamNormalized(id,value);performEdit(id,getParamNormalized(id));endEdit(id);}
tresult PLUGIN_API Plugin::getState(IBStream* stream){
    if(!stream)return kInvalidArgument;
    IBStreamer io(stream,kLittleEndian);if(!io.writeInt32(0x4F4E5958)||!io.writeInt32(1))return kResultFalse;for(double v:snapshot())if(!io.writeDouble(v))return kResultFalse;return kResultOk;
}
tresult PLUGIN_API Plugin::setState(IBStream* stream){
    if(!stream)return kInvalidArgument;
    IBStreamer io(stream,kLittleEndian);int32 magic=0,version=0;
    if(!io.readInt32(magic)||!io.readInt32(version)||magic!=0x4F4E5958||version!=1)return kResultFalse;
    auto p=defaults;for(unsigned i=0;i<ParamCount;++i)if(!io.readDouble(p[i])||!std::isfinite(p[i])||p[i]<0.||p[i]>1.)return kResultFalse;
    restoringState=true;for(unsigned i=0;i<ParamCount;++i)if(activeParam(i))setParamNormalized(i,p[i]);values[LegacySynthesis].store(p[LegacySynthesis]);restoringState=false;
    if(requestedLatency(snapshot())!=configuredLatency.load())if(auto*h=getComponentHandler())h->restartComponent(kLatencyChanged);
    return kResultOk;
}
// Optional host extension. Standard VST3 channel-context metadata identifies the
// insert, not its sidechain source; never mislabel an insert as a sidechain track.
tresult PLUGIN_API Plugin::notify(IMessage* message){
    if(!message)return kInvalidArgument;
    auto*a=message->getAttributes();
    if(a&&message->getMessageID()&&std::strcmp(message->getMessageID(),"SUBBOX.SidechainContext")==0){String128 name{};int64 id=-1;
        if(a->getString("subbox.sidechain.name",name,sizeof(name))==kResultOk)for(unsigned i=0;i<64;++i)sidechainName[i].store(i<63?name[i]:0);
        if(a->getInt("subbox.sidechain.id",id)==kResultOk)sidechainId.store(id);
        return kResultOk;}
    return SingleComponentEffect::notify(message);
}
IPlugView* PLUGIN_API Plugin::createView(FIDString name){return name&&std::strcmp(name,ViewType::kEditor)==0?createNativeEditor(*this):nullptr;}
template<class Sample> tresult Plugin::processAudio(ProcessData& data) noexcept {
    struct Point {IParamValueQueue*q=nullptr;int32 index=0,offset=0;ParamValue value=0.;bool valid=false;};
    std::array<Point,ParamCount> points{};unsigned livePublicationEpoch=livePeakEpoch.load();double liveSub=0.,liveBass=0.;auto p=snapshot();
    if(data.inputParameterChanges){const int32 count=data.inputParameterChanges->getParameterCount();for(int32 k=0;k<count;++k){auto*q=data.inputParameterChanges->getParameterData(k);if(!q||!activeParam(q->getParameterId()))continue;auto&pt=points[q->getParameterId()];pt.q=q;pt.valid=q->getPoint(0,pt.offset,pt.value)==kResultOk;}}
    auto advance=[&](int32 sample){for(unsigned id=0;id<ParamCount;++id){auto&pt=points[id];while(pt.valid&&(pt.offset<=sample||data.numSamples==0)){const auto previous=p[id];p[id]=canonical(id,pt.value);values[id].store(p[id],std::memory_order_relaxed);if(id==Sub){p[LegacySynthesis]=p[LegacySub]=0.;values[LegacySynthesis].store(0.,std::memory_order_relaxed);values[LegacySub].store(0.,std::memory_order_relaxed);}else if(id==LegacySub){p[LegacySynthesis]=1.;values[LegacySynthesis].store(1.,std::memory_order_relaxed);}if(previous!=p[id]&&(id==Sidechain||id==SubSolo||id==BassSolo||id==Clip||id==Bypass)){invalidateAnalysis();livePublicationEpoch=livePeakEpoch.load();liveSub=liveBass=0.;}pt.valid=pt.q->getPoint(++pt.index,pt.offset,pt.value)==kResultOk;}}};
    const auto* context=data.processContext;
    const bool hostTempo=context&&(context->state&ProcessContext::kTempoValid)&&std::isfinite(context->tempo)&&context->tempo>=20.&&context->tempo<=300.;
    playing.store(context&&(context->state&ProcessContext::kPlaying),std::memory_order_relaxed);
    if(context&&p[HostSync]>=.5&&(context->state&ProcessContext::kProjectTimeMusicValid))engine.sync(context->projectTimeMusic);
    if(data.numSamples<=0){advance(0);const bool fromHost=p[HostSync]>=.5&&hostTempo;tempoDisplay.store(fromHost?context->tempo:bpmValue(p[Bpm]));tempoFromHost.store(fromHost);return kResultOk;}
    if(data.numOutputs<1||!data.outputs)return kResultOk;
    AudioBusBuffers empty{};empty.numChannels=std::min(data.outputs[0].numChannels,2);
    auto& input=data.numInputs>0&&data.inputs?data.inputs[0]:empty;auto& output=data.outputs[0];
    const int32 n=std::min(output.numChannels,2);if(n<=0)return kResultOk;
    Sample**in=nullptr;Sample**out=nullptr;
    if constexpr(std::is_same_v<Sample,float>){in=input.channelBuffers32;out=output.channelBuffers32;}else{in=input.channelBuffers64;out=output.channelBuffers64;}
    if(!out)return kInvalidArgument;
    for(int32 c=0;c<n;++c)if(!out[c])return kInvalidArgument;
    struct SideInput {Sample** buffers=nullptr;int32 channels=0;uint64 silence=0;};
    std::array<SideInput,maxSidechainBuses> sides{};unsigned activeSides=0;
    if(data.inputs)for(unsigned k=0;k<maxSidechainBuses&&static_cast<int32>(k+1)<data.numInputs;++k){if(!sidechainActive[k].load(std::memory_order_relaxed))continue;auto&bus=data.inputs[k+1];if(bus.numChannels<1)continue;auto&side=sides[k];side.channels=std::min(bus.numChannels,2);side.silence=bus.silenceFlags;if constexpr(std::is_same_v<Sample,float>)side.buffers=bus.channelBuffers32;else side.buffers=bus.channelBuffers64;++activeSides;}
    scPresent.store(activeSides>0,std::memory_order_relaxed);scBusCount.store(activeSides,std::memory_order_relaxed);
    const bool analyze=analysisEnabled.load(std::memory_order_relaxed);
    const double licenseSmoothing=std::exp(-1./(.008*sampleRate.load(std::memory_order_relaxed)));
    const unsigned epoch=resetEpoch.load();const bool reset=epoch!=observedReset;observedReset=epoch;
    double sp=reset?0.:subPeak.load(),bp=reset?0.:bassPeak.load(),blockSubRms=0.,blockBassRms=0.,protection=0.;bool unstable=false,previousSource=engine.analyzesSidechain();double reduction=0.,note=0.,confidence=0.;uint64 silence=(1ull<<n)-1;
    for(int32 i=0;i<data.numSamples;++i){
        advance(i);const double tempo=p[HostSync]>=.5&&hostTempo?context->tempo:bpmValue(p[Bpm]);tempoDisplay.store(tempo,std::memory_order_relaxed);tempoFromHost.store(p[HostSync]>=.5&&hostTempo,std::memory_order_relaxed);
        const double l=input.numChannels>0&&in&&in[0]&&!(input.silenceFlags&1)?in[0][i]:0.;
        const double r=input.numChannels==1?l:(input.numChannels>1&&in&&in[1]&&!(input.silenceFlags&2)?in[1][i]:0.);
        double kl=0.,kr=0.;for(const auto&side:sides){if(!side.buffers||side.channels<1)continue;const double sl=side.buffers[0]&&!(side.silence&1)?clean(side.buffers[0][i]):0.;const double sr=side.channels==1?sl:(side.buffers[1]&&!(side.silence&2)?clean(side.buffers[1][i]):0.);kl+=sl;kr+=sr;}
        if(n==1){const double mid=.5*(kl+kr);kl=kr=mid;}
        const double authorized=licensing.authorized.load(std::memory_order_acquire)?1.:0.;
        if(licenseMix==0.&&authorized>0.){engine.prepare(sampleRate.load(std::memory_order_relaxed),p);invalidateAnalysis();}
        const double previousLicenseMix=licenseMix;licenseMix=authorized+(licenseMix-authorized)*licenseSmoothing;if(std::abs(licenseMix-authorized)<1.e-10)licenseMix=authorized;
        if(previousLicenseMix>0.&&licenseMix==0.){engine.prepare(sampleRate.load(std::memory_order_relaxed),p);invalidateAnalysis();}
        auto y=licenseMix>0.?engine.tick(l,r,kl,kr,activeSides>0,p,tempo):Output{};
        for(unsigned c=0;c<2;++c){const double dry=unlicensedDelay[c].tick(clean(c==0?l:r),licenseMix>0.?engine.latency():requestedLatency(p));y.audio[c]=licenseMix==0.?dry:licenseMix==1.?y.audio[c]:dry+licenseMix*(y.audio[c]-dry);y.sub[c]*=licenseMix;y.bass[c]*=licenseMix;}
        // Activation blends dry audio AFTER Engine's master stage. Keep the
        // actual authorized host output bounded throughout that ramp too.
        if(authorized>0.&&p[Bypass]<.5&&clipMode(p[Clip])==2)y.audio=OnyxClipper::hardStereo(y.audio,masterClipCeiling);
        if(licenseMix==0.){y.generated=y.guard=y.trackedHz=y.confidence=y.reduction=0.;y.rms={};}
        if(previousSource!=engine.analyzesSidechain()){previousSource=engine.analyzesSidechain();invalidateAnalysis();livePublicationEpoch=livePeakEpoch.load();liveSub=liveBass=0.;}
        for(int32 c=0;c<n;++c){out[c][i]=static_cast<Sample>(y.audio[c]);if(out[c][i]!=Sample(0))silence&=~(1ull<<c);}
        liveSub=std::max({liveSub,std::abs(y.sub[0]),std::abs(y.sub[1])});liveBass=std::max({liveBass,std::abs(y.bass[0]),std::abs(y.bass[1])});sp=std::max(sp,liveSub);bp=std::max(bp,liveBass);blockSubRms=y.rms[0];blockBassRms=y.rms[1];protection=std::max(protection,y.guard);unstable=unstable||y.unstable;reduction=std::max(reduction,y.reduction);note=y.trackedHz;confidence=y.confidence;
        if(analyze)analysis.push({static_cast<float>(y.sub[0]),static_cast<float>(y.sub[1]),static_cast<float>(y.bass[0]),static_cast<float>(y.bass[1])});
    }
    output.silenceFlags=silence;if(resetEpoch.load()==epoch){subPeak.store(sp);bassPeak.store(bp);peakEpoch.store(epoch);}subRms.store(blockSubRms);bassRms.store(blockBassRms);subLivePeak.store(liveSub);bassLivePeak.store(liveBass);publishPeak(livePublicationEpoch,0,liveSub);publishPeak(livePublicationEpoch,1,liveBass);meterEpoch.fetch_add(1);guardDisplay.store(protection);unstableDisplay.store(unstable);clipReduction.store(reduction);trackedHz.store(note);confidenceDisplay.store(confidence);analysisExternal.store(engine.analyzesSidechain());configuredLatency.store(licenseMix==0.?requestedLatency(p):engine.latency());configuredOS.store(engine.oversampling());qualityPending.store((licenseMix!=0.&&engine.latency()!=requestedLatency(p))||engine.oversampling()!=oversampleFactor(p[Oversample]));return kResultOk;
}
tresult PLUGIN_API Plugin::process(ProcessData& data){
    if(data.numSamples<0||data.numSamples>processSetup.maxSamplesPerBlock)return kInvalidArgument;
    if(data.symbolicSampleSize==kSample32)return processAudio<float>(data);
    if(data.symbolicSampleSize==kSample64)return processAudio<double>(data);
    return kResultFalse;
}
}
