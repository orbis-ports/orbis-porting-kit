#pragma once
// The one audio output port: what the console measured, and nothing that opens it.
//
// ⚠ THIS IS NOT AN API. Until 2026-09-19 this header declared audio_start(), audio_stop(),
// audio_running() and audio_blocks(), and orbis_audio.cpp owned a std::thread that looped on
// sceAudioOutOutput. Both are gone. Audio output fails the kit's boundary test - a second program on
// this console would notice it was there, because it owns a thread and the single output port, and a
// resource with one instance per process has exactly one owner. That owner is the middleware or the
// engine: SDL2's src/audio/orbis driver for an SDL title, OpenGothic's own SoundDevice backend for
// Tempest. What the kit can honestly contribute to a third engine is the measurements below, which
// every engine otherwise rediscovers from the hardware one refusal code at a time.
//
// The numbers came out of orbis-ports/OpenGothic ps4/og_sound_orbis.cpp on 2026-09-18, from a
// standalone probe run twice identically. None is inferred from a header or from another platform's
// driver. Include this file for the constants, or just read it.
//
// ------------------------------------------------------------------ what the console measured
//
//   * THE MAIN PORT IS 48000 Hz AND NOTHING ELSE. 44100, 32000, 24000, 22050, 16000, 12000, 11025
//     and 8000 all return 0x80260008. So every asset that is not already 48 kHz is resampled by
//     the caller, on the path of the first sample that plays - not an optimisation to defer.
//   * GRAIN IS A MULTIPLE OF 256, from 256 to 2048. 128 and 300 return 0x80260006 - a different
//     code from a bad rate, so a refusal names which parameter was wrong.
//   * sceAudioOutOutput IS THE CLOCK, and there is no other. 64 calls at grain 256 measured min
//     5255, mean 5332, max 5425 us against 5333 us of audio in a block: 0.02% off theory. The call
//     BLOCKS until the port has room. So a backend needs no clock, no deadline scheduler and no
//     drift correction, and its fill callback must never be invoked from a render thread.
//   * ALL FOUR FORMATS OPEN, float included. Every owner below uses S16_STEREO anyway: it is the one
//     a human validated by ear, and the float range is documented nowhere. A verified path beats a
//     shorter unverified one.
//   * sceAudioOutOpen WANTS THE SYSTEM USER (0xFF). A real logged-in user id fails with 0x809b0001,
//     which is the opposite of scePadOpen (SDL_orbisaudio.c:30).
//   * THE PORT CAN OPEN SILENT. sceAudioOutSetVolume after Open is not optional on every firmware
//     (RetroArch/audio/drivers/ps4_audio.c:30).
//   * A PROCESS GETS ONE OUTPUT STREAM. A title that constructs several audio devices - which is
//     normal; the engine these numbers came from builds three before it starts - cannot give each
//     one a port. Mix in software and hand blocks to one owner. For the same reason a process-wide
//     master volume is usually wrong: keep per-source gain in the mixer, where the caller can tell
//     music from gunfire.
//
// ------------------------------------------------- ⚠ WHAT CLOSE DOES TO THE PORT IS UNRESOLVED
//
// Three places in this organisation measured the same question and do not agree. Nobody has run the
// experiment that separates them, so all three are recorded here verbatim and none is called right.
//
// 1. This header, until this rewrite, lines 27-28, from the OpenGothic-derived probe:
//
//        "THERE IS ONE PORT. Every Open in all three ladders returned the same handle, 0x20000007,
//         for every rate, grain and format, and after every Close. That is the id of the main port,
//         not an allocation: a process gets ONE output stream."
//
// 2. RetroArch/audio/drivers/ps4_audio.c:174-179:
//
//        "⚠ ONE PORT FOR THE LIFE OF THE PROCESS, BECAUSE THE SYSTEM DOES NOT GIVE THEM BACK.
//         Measured on hardware across eight core switches: eight opens, eight closes, EVERY close
//         returning 0x00000000 - and the ninth open failing with PORT_FULL. The handles counted
//         down 0x20000007, 0x20000006 ... 0x20000000 and were never reissued. So the port is not
//         being leaked by this driver; sceAudioOutClose reports success and the port stays spent."
//
// 3. SDL2 (orbis-ports/SDL2, branch orbis-2.32) src/audio/orbis/SDL_orbisaudio.c:33-35:
//
//        "sceAudioOutClose returns success but the port is never given back; after eight
//         open/close cycles sceAudioOutOpen fails with PORT_FULL. So the port is opened once per
//         process, cached in a static, and reused on every reopen."
//
// The disagreement is narrow and total: (1) says Close returns the handle and a reopen gets
// 0x20000007 again; (2) and (3) say Close reports success, the handle is spent, the next Open hands
// out the next number down, and the ninth Open in a process fails with PORT_FULL (0x80260005).
// Reading (1) against (2), the probe behind (1) may never have opened a ninth time - three ladders
// that each closed and reopened a few times would see 0x20000007 return only if the handle really
// is reissued, but a ladder that was restarted as a fresh process each time would see it either
// way, and the probe's process boundaries were not recorded.
//
// ⚠ THIS IS UNRESOLVED AND ONLY HARDWARE SETTLES IT. The experiment is one process, nine
// Open/Close pairs, every return value logged. Until somebody runs it, a backend should assume the
// stricter reading - open once, cache the handle, never close - because that is correct under both
// measurements, while a close-and-reopen design is correct under only one. This header does not pick
// a winner, and the deleted audio_stop() is exactly the kind of API that had to.
//
// ------------------------------------------------------------- who opens the port, as of today
//
// Four sceAudioOutOpen call sites in the organisation (grep -rn over every checkout, 2026-09-19):
//
//   * orbis-porting-kit/services/audio/orbis_audio.cpp:62  - DELETED by this commit; it was the
//     only one of the four with no engine behind it, and the only one that closed the port.
//   * RetroArch/audio/drivers/ps4_audio.c:193              - opens once, caches in a static,
//     deliberately never closes.
//   * SDL2/src/audio/orbis/SDL_orbisaudio.c:199 (orbis-2.32) - same policy, for every SDL title.
//   * OpenGothic/ps4/og_sound_orbis.cpp:402                - the port this file's numbers came from;
//     still built by that port.
//
// So after this commit three owners remain, one per middleware or engine, which is the intended
// shape: a title drives the port from ONE place.
#include <cstdint>

namespace orbis {

// The facts above, as constants, for a backend to build itself around. They describe the hardware;
// nothing in this repository acts on them.
namespace audio_hw {

inline constexpr unsigned kRate       = 48000;  // the only rate the main port accepts
inline constexpr unsigned kGrainUnit  = 256;    // grain must be a multiple of this
inline constexpr unsigned kGrainMin   = 256;
inline constexpr unsigned kGrainMax   = 2048;
inline constexpr unsigned kChannels   = 2;      // S16_STEREO is the validated format

// The refusal codes seen above are the SDK's own, in <orbis/_types/errors.h>, and are NOT
// re-spelled here - one source of truth for a value somebody else defines:
//
//   0x80260008  ORBIS_AUDIO_OUT_ERROR_INVALID_SAMPLE_FREQ  any rate but 48000
//   0x80260006  ORBIS_AUDIO_OUT_ERROR_INVALID_SIZE         grain not 256..2048 in steps of 256
//   0x80260005  ORBIS_AUDIO_OUT_ERROR_PORT_FULL            the unresolved section above
//   0x8026000E  ORBIS_AUDIO_OUT_ERROR_ALREADY_INIT         sceAudioOutInit twice; not fatal
//   0x809b0001  a real user id instead of SYSTEM (0xFF); no SDK name for it
//
}  // namespace audio_hw

}  // namespace orbis
