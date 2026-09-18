// See orbis_audio.h for what the console measured. This file is the port and the thread; the mixer
// that feeds it belongs to the caller.
#include "orbis_audio.h"

#include <orbis_log.h>

#include <orbis/libkernel.h>
#include <orbis/AudioOut.h>
#include <orbis/UserService.h>

#include <atomic>
#include <thread>
#include <vector>

namespace orbis {

namespace {

std::atomic<bool>     g_run{false};
std::atomic<uint64_t> g_blocks{0};
std::thread           g_thread;
int32_t               g_port  = -1;
AudioFill             g_fill  = nullptr;
void*                 g_user  = nullptr;
unsigned              g_grain = 256;
std::vector<int16_t>  g_out;

void pump() {
  // ⚠ NO SLEEP, NO CLOCK, NO DEADLINE. sceAudioOutOutput blocks until the port has room, measured
  // at 5332 us against 5333 us of audio per block at grain 256. A pump that also slept would
  // drift; one that watched a clock would fight this one.
  while(g_run.load(std::memory_order_relaxed)) {
    g_fill(g_out.data(),g_grain,g_user);
    sceAudioOutOutput(g_port,g_out.data());
    g_blocks.fetch_add(1,std::memory_order_relaxed);
    }
  }

}

bool audio_start(AudioFill fill, void* user, const AudioConfig& cfg) {
  if(g_run.load())
    return g_port>0;
  if(fill==nullptr) {
    orbis_log("audio: audio_start called with no fill callback - refusing rather than opening a "
              "port that would play whatever the buffer happened to hold");
    return false;
    }

  // Refused here rather than by the port, because the port's two codes are the only thing that
  // distinguishes them and a caller should not have to look 0x80260006 up to learn that 300 is not
  // a multiple of 256.
  if(cfg.rate!=48000)
    orbis_log("audio: rate %u requested; the main port accepts 48000 and answers 0x80260008 to "
              "everything else. Passing it through so the refusal is the hardware's.",cfg.rate);
  if(cfg.grain<256 || cfg.grain>2048 || (cfg.grain%256)!=0)
    orbis_log("audio: grain %u is not a multiple of 256 in 256..2048; the port answers 0x80260006. "
              "Passing it through so the refusal is the hardware's.",cfg.grain);

  const int32_t userRc = sceUserServiceInitialize(nullptr);
  const int32_t initRc = sceAudioOutInit();
  g_port = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM,ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,0,
                           cfg.grain,cfg.rate,ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
  if(g_port<=0) {
    // Named, counted once, and NOT fatal: a title that cannot open the port must still run, and
    // the difference between "silent because there is no backend" and "silent because Open refused
    // with this code" is the whole reason this line exists.
    orbis_log("audio: sceAudioOutOpen refused 0x%08x (userService 0x%08x, init 0x%08x) - "
              "the title runs silent",uint32_t(g_port),uint32_t(userRc),uint32_t(initRc));
    g_port = -1;
    return false;
    }

  int32_t vol[8] = { 32768,32768,32768,32768,32768,32768,32768,32768 };
  sceAudioOutSetVolume(g_port,0xFF,vol);

  g_fill  = fill;
  g_user  = user;
  g_grain = cfg.grain;
  g_out.assign(size_t(g_grain)*2,0);
  g_blocks.store(0);

  orbis_log("audio: sceAudioOut port %d open - %u Hz, grain %u (%.2f ms), s16 stereo; every source "
            "that is not 48 kHz is resampled by the caller, because the port accepts no other rate",
            int(g_port),cfg.rate,g_grain,1000.0*double(g_grain)/double(cfg.rate));

  g_run.store(true);
  g_thread = std::thread(pump);
  return true;
  }

void audio_stop() {
  if(!g_run.load())
    return;
  g_run.store(false);
  // ⚠ THE THREAD IS INSIDE A BLOCKING CALL WHEN THIS RUNS, and it is left to finish that block
  // rather than being killed: sceAudioOutOutput returns when the port has room, at most one block
  // of audio away - 5.3 ms at grain 256. Closing the port under a thread that is writing to it is
  // the alternative, and it is not one.
  if(g_thread.joinable())
    g_thread.join();
  sceAudioOutClose(g_port);
  orbis_log("audio: port %d closed after %llu block(s)",int(g_port),
            (unsigned long long)g_blocks.load());
  g_port = -1;
  g_fill = nullptr;
  g_user = nullptr;
  }

bool     audio_running() { return g_run.load(); }
uint64_t audio_blocks()  { return g_blocks.load(); }

}
