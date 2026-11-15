# SUB BOX ONYX — current update source

Development update built on the supplied stable ONYX 1.2.12 Windows x64 VST3.

| Setting | Update |
| --- | --- |
| SUB crossover range | 20–60 Hz |
| BASS crossover range | 60–150 Hz |
| SOLO | Auditions the same processed lanes |
| SBO soft entry | −0.99 dBFS |
| SBO sample hard ceiling | −0.1 dBFS |
| SC default | OFF in processor and controller |
| Fixed guard latency | 384 samples (8 ms at 48 kHz) |

The new SUB/BASS gains share the native coherent fourth-order Linkwitz–Riley
filter bank at 20, 60 and 150 Hz. These are nominal crossover frequencies with
real filter transition slopes. Neutral crossover gain is unity; the coherent
crossover phase is permanent rather than changing when a knob/SOLO is used.

`update/` is the complete source for this update and its deterministic PE patch
builder. `baseline/` pins the exact production image by SHA-256.
`reference/onyx-1.2.7/` contains the older user-supplied layout/algorithm reference;
it is not represented as recovered 1.2.12 source. The current host, editor,
resources, parameter IDs and production licensing verifier remain in the pinned
production image. No private issuer keys, vaults, credentials or test licenses
are stored here.

Build: `python tools/build.py`. Run `python tools/verify.py` after building.
The Windows job additionally checks the native VST3 DLL loader and actual DSP
instructions; FL Studio listening and owner sealing remain separate acceptance
steps. Generated files and reports identify the exact module SHA-256 tested.

This is an owner development master, not a sealed customer release. The existing
production owner issuer must seal the exact final image for its recipient and
installation location. Existing valid licenses are retained.
