#include "Plugin.h"
#include "licensing/ReleaseIntegrity.h"
#include "public.sdk/source/main/pluginfactory.h"
using namespace Steinberg;
using namespace Steinberg::Vst;
#ifndef SUBBOX_RELEASE_VERSION
#define SUBBOX_RELEASE_VERSION "1.2.7"
#endif
namespace {FUnknown* secureCreate(void*){auto*p=new subbox::Plugin;p->licensing.requireReleaseSeal(rsa::licensing::release::modulePath());return static_cast<IComponent*>(p);}}
BEGIN_FACTORY_DEF("Ritual Summon Audio","https://github.com/ritualsummonaudiosubbox/subbox","")
DEF_CLASS2(INLINE_UID(0x4F4E5958,0x6ED54B62,0xA4D7F963,0x10201826),PClassInfo::kManyInstances,kVstAudioEffectClass,"SUB BOX ONYX",0,"Fx|Dynamics",SUBBOX_RELEASE_VERSION,kVstVersionString,secureCreate)
END_FACTORY

