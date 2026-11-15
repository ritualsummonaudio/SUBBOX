#pragma once
#include "Dsp.h"
#include "licensing/License.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <atomic>

namespace subbox {
class Plugin final : public Steinberg::Vst::SingleComponentEffect {
    std::array<std::atomic<double>,ParamCount> values;
    Engine engine;
    bool restoringState=false;
    double licenseMix=0.;
    std::array<Delay<512>,2> unlicensedDelay;
    std::array<std::atomic<bool>,maxSidechainBuses> sidechainActive{};
    template<class Sample> Steinberg::tresult processAudio(Steinberg::Vst::ProcessData& data) noexcept;
public:
    rsa::licensing::Monitor licensing;
    AnalysisQueue analysis;
    std::array<std::atomic<char16_t>,64> sidechainName{};
    std::atomic<Steinberg::int64> sidechainId{-1};
    std::atomic<double> sampleRate{48000.},tempoDisplay{120.},subPeak{0.},bassPeak{0.},subRms{0.},bassRms{0.},guardDisplay{0.},trackedHz{0.},confidenceDisplay{0.},clipReduction{0.};
    std::atomic<bool> playing{false},scPresent{false},analysisExternal{false},unstableDisplay{false},tempoFromHost{false};
    std::atomic<bool> analysisEnabled{false},editorAttached{false};
    std::atomic<unsigned> analysisEpoch{0},meterEpoch{0},scBusCount{0},configuredLatency{0},configuredOS{4};
    std::atomic<double> subLivePeak{0.},bassLivePeak{0.};
    // Audio-side maxima survive short blocks until the UI consumes them.
    // A single last-block value can lose a transient between 60 Hz frames.
    std::array<PeakMailbox,2> pendingPeak;
    std::atomic<unsigned> livePeakEpoch{0};
    void publishPeak(unsigned epoch,unsigned lane,double peak) noexcept {pendingPeak[lane].publish(epoch,peak);}
    std::array<double,2> consumePeaks() noexcept {const auto epoch=livePeakEpoch.load();return {pendingPeak[0].consume(epoch),pendingPeak[1].consume(epoch)};}
    void clearLivePeaks()noexcept{const auto epoch=livePeakEpoch.fetch_add(1)+1;for(auto&peak:pendingPeak)peak.clear(epoch);}
    void invalidateAnalysis()noexcept{analysisEpoch.fetch_add(1);clearLivePeaks();}
    std::atomic<bool> qualityPending{false};
    std::atomic<unsigned> resetEpoch{0},peakEpoch{0};
    unsigned observedReset=0;
    Plugin();
    static Steinberg::FUnknown* createInstance(void*) {return static_cast<Steinberg::Vst::IComponent*>(new Plugin);}
    std::array<double,ParamCount> snapshot() const noexcept;
    void edit(Param id,double normalized);
    // Audio owns the held maxima. An epoch makes RESET immediate to the UI
    // without allowing an in-flight old audio block to restore cleared peaks.
    void resetPeaks() noexcept {resetEpoch.fetch_add(1);clearLivePeaks();}
    std::array<double,2> heldPeaks()const noexcept {const auto epoch=resetEpoch.load();if(peakEpoch.load()!=epoch)return {};const std::array<double,2>peaks{subPeak.load(),bassPeak.load()};return resetEpoch.load()==epoch?peaks:std::array<double,2>{};}
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup&) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement*,Steinberg::int32,Steinberg::Vst::SpeakerArrangement*,Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API getBusInfo(Steinberg::Vst::MediaType type,Steinberg::Vst::BusDirection direction,Steinberg::int32 index,Steinberg::Vst::BusInfo& info) override {return index<0?Steinberg::kInvalidArgument:SingleComponentEffect::getBusInfo(type,direction,index,info);}
    Steinberg::tresult PLUGIN_API getBusArrangement(Steinberg::Vst::BusDirection direction,Steinberg::int32 index,Steinberg::Vst::SpeakerArrangement& arrangement) override {return index<0?Steinberg::kInvalidArgument:SingleComponentEffect::getBusArrangement(direction,index,arrangement);}
    Steinberg::tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType,Steinberg::Vst::BusDirection,Steinberg::int32,Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData&) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override {return requestedLatency(snapshot());}
    Steinberg::uint32 PLUGIN_API getTailSamples() override {return static_cast<Steinberg::uint32>(std::ceil(sampleRate.load()*2.))+getLatencySamples();}
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* stream) override {return setState(stream);}
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID,Steinberg::Vst::ParamValue) override;
    Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized(Steinberg::Vst::ParamID) override;
    Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID,Steinberg::Vst::TChar*,Steinberg::Vst::ParamValue&) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString) override;
};
Steinberg::IPlugView* createNativeEditor(Plugin&);
}
