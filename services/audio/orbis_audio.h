#pragma once
// The one audio output port, and the thread the hardware paces.
//
// Extracted 2026-09-18 from orbis-ports/OpenGothic ps4/og_sound_orbis.cpp. ⚠ THAT FILE IS 1075
// LINES AND ONLY ABOUT 250 OF THEM ARE PLATFORM. The rest is a Tempest SoundDevice backend, a
// software mixer, a resampler and an IMA-ADPCM decoder - things an engine already has, and things
// that would make this a Tempest kit rather than a PS4 one. What is here is the part every engine
// has to rediscover from the hardware, and nothing else.
//
// ------------------------------------------------------------------ what the console measured
//
// Every number is from a standalone probe run twice identically. None is inferred from a header or
// from another platform's driver.
//
//   * THE MAIN PORT IS 48000 Hz AND NOTHING ELSE. 44100, 32000, 24000, 22050, 16000, 12000, 11025
//     and 8000 all return 0x80260008. So every asset that is not already 48 kHz is resampled by
//     the caller, on the path of the first sample that plays - not an optimisation to defer.
//   * GRAIN IS A MULTIPLE OF 256, from 256 to 2048. 128 and 300 return 0x80260006 - a different
//     code from a bad rate, so a refusal names which parameter was wrong.
//   * sceAudioOutOutput IS THE CLOCK, and there is no other. 64 calls at grain 256 measured min
//     5255, mean 5332, max 5425 us against 5333 us of audio in a block: 0.02% off theory. The call
//     BLOCKS until the port has room. So this has no clock, no deadline scheduler and no drift
//     correction, and the fill callback must never be invoked from a render thread.
//   * ALL FOUR FORMATS OPEN, float included. S16_STEREO is used anyway: it is the one a human
//     validated by ear, and the float range is documented nowhere. A verified path beats a shorter
//     unverified one.
//   * THERE IS ONE PORT. Every Open in all three ladders returned the same handle, 0x20000007, for
//     every rate, grain and format, and after every Close. That is the id of the main port, not an
//     allocation: a process gets ONE output stream.
//
// ⚠ ONE PORT IS AN API CONSTRAINT, NOT A DETAIL. A title that constructs several audio devices -
// which is normal; the engine this came from builds three before it starts - cannot give each one a
// port. They would all hold 0x20000007 and fight. Mix in software, then hand blocks to this. For
// the same reason a process-wide master volume is usually wrong: keep per-source gain in the mixer
// where the caller can tell music from gunfire.
//
// Requires orbis-compat for orbis_log(); libSceAudioOut and libSceUserService come from the SDK.
#include <cstdint>

namespace orbis {

// Called on the audio thread, once per block, and must fill `frames` frames of interleaved
// stereo int16. It is called with the port already waiting: whatever it does NOT write is played
// as whatever the buffer held before, so fill or clear all of it.
using AudioFill = void (*)(int16_t* out, unsigned frames, void* user);

struct AudioConfig {
  unsigned rate  = 48000;   // the only rate the main port accepts; see above
  unsigned grain = 256;     // frames per block: a multiple of 256, 256..2048
  };

// Opens the port and starts the thread. False means the port refused and the log names the code;
// a title that cannot open it must still run, silently, so this is not fatal by itself.
// Calling it twice without audio_stop() is a no-op that returns whether the first call succeeded.
bool audio_start(AudioFill fill, void* user, const AudioConfig& cfg = {});

// Stops the thread and closes the port. Idempotent. The fill callback is not called after this
// returns.
void audio_stop();

bool     audio_running();
// Blocks handed to sceAudioOutOutput since the last audio_start. The cheapest evidence that the
// clock is running: multiply by grain/rate for the audio time produced, and compare with wall time.
uint64_t audio_blocks();

}
