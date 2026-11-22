// CharonPC - the Bochs low-level sound driver "charon" (r407). GPL-2.0-or-later.
//
// Bochs's sound devices (Sound Blaster 16 with its OPL3 FM chip, the PC
// speaker) hand their sound to a "lowlevel" driver. The drivers of Bochs run
// a resampler thread and a mixer thread; a UEFI application has no threads.
// This driver does both jobs from the GUI event hook (pc_gui.cc handle_events,
// about once per millisecond of emulated time):
//  - sendwavepacket (SB16 DSP: DMA and direct output) converts the packet to
//    48 kHz stereo signed 16 bit at once (linear interpolation; Bochs without
//    libsamplerate does not resample at all) into a FIFO;
//  - SoundTick renders as many frames as real time has passed since the last
//    tick: FIFO + every registered generator (FM, PC speaker), and writes them
//    to the output (pc_audio.cpp: Charon's PCM sink or CharonPC's own card).
// Nothing is written while the machine is silent, so Charon's speech is not
// delayed by a queue of silence; a sound is followed by 300 ms of it.
//
// Timing. Bochs's SB16 collects 0.1 s of samples before it calls
// sendwavepacket, so the FIFO is fed in bursts. Playing a new stream starts
// 50 ms after its first packet (a later packet may then be up to 50 ms late
// without a gap). Added to the output's own 40 ms start buffer that is about
// 0.2 s from the guest's write to the loudspeaker.
#include "bochs.h"
#include "plugin.h"
#include "pc_system.h"
#include "sound/soundlow.h"
#include "pc_glue.h"
#include "pc_platform.h"

#if BX_SUPPORT_SOUNDLOW

// Bochs's macro does not use all parameters of the plugin entry.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
PLUGIN_ENTRY_FOR_SND_MODULE(charon)
{
    if (mode == PLUGIN_PROBE) return (int)PLUGTYPE_SND;
    return 0;
}
#pragma clang diagnostic pop

namespace {

constexpr unsigned outRate = 48000;
constexpr size_t fifoFrames = 48000;        // 1 s
constexpr size_t fifoHigh = 40000;          // more than 0.83 s waiting (the rate control did not cope): ...
constexpr size_t fifoKeep = 9600;           //   ... the oldest sound is skipped down to 0.2 s
constexpr uint64_t startDelayUs = 150000;   // a new stream starts this long after its first packet at the latest
// r407 rate control. The guest does not make sound at exactly the speed of real time: an emulator
// that falls behind makes it slower for a while, Bochs catching up makes it faster. Cutting the
// surplus out (and playing gaps) is what the user heard as stutter. The FIFO is read at a speed that
// follows its fill instead: a little faster when sound piles up, a little slower when it runs low
// while packets still arrive.
// The sound card hands its sound over in packets of a tenth of a second, so the fill is a saw-tooth:
// the speed follows the fill AVERAGED over about a second, gently (2 % per 100 ms of surplus), or
// the pitch would waver with every packet (heard as a "strange" sound in the first trial).
constexpr size_t rateTarget = 7200;         // 150 ms waiting on average is the aim; a stream starts when that much is there
constexpr uint32_t rateLow = 64225, rateHigh = 78643;   // 0.98 .. 1.20 of the normal speed (16.16)
constexpr uint64_t tailUs = 300000;         // silence written after the last sound
constexpr size_t tickFrames = 4096;         // most frames rendered by one tick (85 ms)
constexpr size_t queueHigh = 6000;          // output queue above this: the tick's frames are dropped

class bx_soundlow_waveout_charon_c : public bx_soundlow_waveout_c {
public:
    bx_soundlow_waveout_charon_c() : bx_soundlow_waveout_c() {}
    virtual ~bx_soundlow_waveout_charon_c() {}
    int openwaveoutput(const char* wavedev) override;
    int set_pcm_params(bx_pcm_param_t* param) override;
    int sendwavepacket(int length, Bit8u data[], bx_pcm_param_t* src_param) override;
    int get_packetsize() override { return (int)(outRate * 4 / 10); }
    int closewaveoutput() override;
    // Bochs's threads would call these; here the packet is converted in sendwavepacket.
    void resampler(audio_buffer_t* inbuffer, audio_buffer_t* outbuffer) override { UNUSED(inbuffer); UNUSED(outbuffer); }
    void Tick();
    uint64_t GuestPackets() const { return guestPackets; }

private:
    void Push(int left, int right);
    size_t Fill() const { return (size_t)(pushed - popped); }

    bool opened = false, output = false;
    int16_t fifo[fifoFrames * 2];
    uint64_t pushed = 0, popped = 0;
    // resampler state: the previous source frame and the place between it and the next (16.16)
    int previous[2] = {0, 0};
    uint32_t place = 0;
    uint32_t rate = 65536, ratePlace = 0;   // rate control: FIFO frames per output frame, and the place between two
    long long fillAverage = 0;              // frames waiting, averaged over about a second (times 1024)
    uint64_t lastPacketUs = 0;
    bool primed = false;
    unsigned sourceRate = 0, sourceChannels = 0, sourceBits = 0;
    // stream state
    bool running = false;
    uint64_t firstPacketUs = 0, lastSoundUs = 0, lastTickUs = 0, carry = 0;
    bool writing = false;
    // counters for the log
    uint64_t statPackets = 0, statWritten = 0, statDropped = 0, statSkipped = 0, statDry = 0, statLogUs = 0;
    // what the guest played, also without an output: packets and the highest sample since the last log line
    uint64_t guestPackets = 0, guestLogged = 0, guestLogUs = 0;
    int guestPeak = 0;
    unsigned guestRate = 0, guestBits = 0, guestChannels = 0;
    unsigned statFillMax = 0, statQueueMax = 0;
};

#define LOG_THIS this->

int bx_soundlow_waveout_charon_c::openwaveoutput(const char* wavedev)
{
    UNUSED(wavedev);
    real_pcm_param.samplerate = (Bit16u)outRate;
    real_pcm_param.bits = 16;
    real_pcm_param.channels = 2;
    real_pcm_param.format = 1;
    if (!opened) {
        opened = true;
        output = pc::AudioOpen();
        BX_INFO(("CharonPC sound: %s", pc::AudioRoute()));
    }
    // Without an output the devices still work (the guest's driver sees a sound card).
    return BX_SOUNDLOW_OK;
}

int bx_soundlow_waveout_charon_c::set_pcm_params(bx_pcm_param_t* param)
{
    UNUSED(param);
    return BX_SOUNDLOW_OK;
}

int bx_soundlow_waveout_charon_c::closewaveoutput() { return BX_SOUNDLOW_OK; }

void bx_soundlow_waveout_charon_c::Push(int left, int right)
{
    if (Fill() >= fifoFrames) { ++statDropped; return; }
    const size_t at = (size_t)(pushed % fifoFrames) * 2;
    fifo[at] = (int16_t)left;
    fifo[at + 1] = (int16_t)right;
    ++pushed;
}

int bx_soundlow_waveout_charon_c::sendwavepacket(int length, Bit8u data[], bx_pcm_param_t* src)
{
    if (length <= 0 || !src->samplerate || (src->bits != 8 && src->bits != 16) || (src->channels != 1 && src->channels != 2))
        return BX_SOUNDLOW_OK;
    ++statPackets;
    lastPacketUs = pc_usec();
    ++guestPackets; guestRate = src->samplerate; guestBits = src->bits; guestChannels = src->channels;
    const unsigned bytesPerFrame = (src->bits / 8) * src->channels;
    const size_t frames = (size_t)length / bytesPerFrame;
    const bool isSigned = src->format & 1;
    // volume: low byte left, high byte right, 255 = full (0xffff: not set)
    const int volumeLeft = src->volume & 0xff, volumeRight = src->volume >> 8;
    // a new stream (or one that was interrupted): interpolation starts at its first sample
    if (Fill() == 0 && !running) { firstPacketUs = pc_usec(); primed = false; }
    if (src->samplerate != sourceRate || src->channels != sourceChannels || src->bits != sourceBits) {
        sourceRate = src->samplerate; sourceChannels = src->channels; sourceBits = src->bits;
        primed = false;
    }
    const uint32_t step = (uint32_t)(((uint64_t)sourceRate << 16) / outRate);
    const Bit8u* p = data;
    for (size_t i = 0; i < frames; ++i) {
        int current[2];
        for (unsigned c = 0; c < src->channels; ++c) {
            int value;
            if (src->bits == 8) { value = isSigned ? (int)(int8_t)p[0] * 256 : ((int)p[0] - 128) * 256; p += 1; }
            else { const unsigned raw = p[0] | (unsigned)p[1] << 8; value = isSigned ? (int)(int16_t)raw : (int)raw - 32768; p += 2; }
            current[c] = value;
        }
        if (src->channels == 1) current[1] = current[0];
        { const int level = current[0] < 0 ? -current[0] : current[0]; if (level > guestPeak) guestPeak = level; }
        current[0] = current[0] * volumeLeft / 255;
        current[1] = current[1] * volumeRight / 255;
        if (!primed) { previous[0] = current[0]; previous[1] = current[1]; place = 0; primed = true; continue; }
        while (place < 65536) {
            Push(previous[0] + (int)(((int64_t)(current[0] - previous[0]) * place) >> 16),
                 previous[1] + (int)(((int64_t)(current[1] - previous[1]) * place) >> 16));
            place += step;
        }
        place -= 65536;
        previous[0] = current[0];
        previous[1] = current[1];
    }
    // A source that runs faster than its declared rate (Bochs's direct DAC mode: 22222 frames a
    // second declared as 22050) fills the FIFO slowly: the oldest sound is skipped.
    if (Fill() > fifoHigh) { statSkipped += Fill() - fifoKeep; popped = pushed - fifoKeep; }
    if (Fill() > statFillMax) statFillMax = (unsigned)Fill();
    return BX_SOUNDLOW_OK;
}

void bx_soundlow_waveout_charon_c::Tick()
{
    if (!opened) return;
    const uint64_t now = pc_usec();
    if (!lastTickUs) { lastTickUs = now; return; }
    carry += (now - lastTickUs) * (outRate / 1000);   // frames * 1000
    lastTickUs = now;
    size_t frames = (size_t)(carry / 1000);
    carry %= 1000;
    if (!frames) return;
    if (frames > tickFrames) frames = tickFrames;     // the host stood still: that time is not made up
    if ((pc::settings.debugcon || pc::settings.debugserial) && guestPackets != guestLogged && now - guestLogUs > 2000000) {
        BX_INFO(("CharonPC sound: the guest played %lu packets more (%u Hz, %u bit, %u channels), highest sample %d of 32767, output %s",
                 (unsigned long)(guestPackets - guestLogged), guestRate, guestBits, guestChannels, guestPeak, output ? "open" : "none"));
        guestLogged = guestPackets; guestLogUs = now; guestPeak = 0;
    }
    if (!output) { popped = pushed; return; }         // no sound card: the devices play into nothing

    static int16_t mixed[tickFrames * 2];
    static Bit8u generated[tickFrames * 4];
    memset(mixed, 0, frames * 4);
    bool sound = false;

    // the FIFO of DSP packets
    if (!running && Fill() > 1 && (Fill() >= rateTarget || now - firstPacketUs >= startDelayUs)) {
        running = true; rate = 65536; ratePlace = 0; fillAverage = (long long)rateTarget * 1024;
    }
    if (running) {
        const size_t fill = Fill();
        fillAverage += ((long long)fill * 1024 - fillAverage) * (long long)frames / 48000;
        long long wanted = 65536 + (fillAverage / 1024 - (long long)rateTarget) * 65536 / 240000;
        if (wanted < rateLow) wanted = rateLow;
        if (wanted > rateHigh) wanted = rateHigh;
        rate = (uint32_t)wanted;
        size_t made = 0;
        for (; made < frames; ++made) {
            const size_t whole = ratePlace >> 16;
            if (whole + 1 >= fill) break;                 // two frames are needed to stand between them
            const size_t a = (size_t)((popped + whole) % fifoFrames) * 2, b = (size_t)((popped + whole + 1) % fifoFrames) * 2;
            const int part = (int)(ratePlace & 0xFFFF);
            mixed[2 * made] = (int16_t)(fifo[a] + (((int)fifo[b] - (int)fifo[a]) * part >> 16));
            mixed[2 * made + 1] = (int16_t)(fifo[a + 1] + (((int)fifo[b + 1] - (int)fifo[a + 1]) * part >> 16));
            ratePlace += rate;
        }
        popped += ratePlace >> 16;
        ratePlace &= 0xFFFF;
        if (made) sound = true;
        if (made < frames) {                               // the stream ended, or its next packet is late
            running = false; ++statDry;
            if (Fill() <= 1) popped = pushed;
        }
    }
    // FM synthesis and the PC speaker: generated for exactly these frames
    for (int i = 0; i < cb_count; ++i) {
        if (!get_wave[i].cb) continue;
        memset(generated, 0, frames * 4);
        if (!get_wave[i].cb(get_wave[i].device, (Bit16u)outRate, generated, (Bit32u)(frames * 4))) continue;
        sound = true;
        for (size_t k = 0; k < frames * 2; ++k) {
            const int value = mixed[k] + (int)(int16_t)(generated[2 * k] | generated[2 * k + 1] << 8);
            mixed[k] = (int16_t)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
        }
    }
    if (sound) lastSoundUs = now;
    const bool wasWriting = writing;
    writing = lastSoundUs && now - lastSoundUs <= tailUs;
    if (writing && output) {
        const size_t queued = pc::AudioQueued();
        if (queued > statQueueMax) statQueueMax = (unsigned)queued;
        if (queued > queueHigh) statDropped += frames;   // the card takes less than real time gives
        else {
            const size_t accepted = pc::AudioWrite(mixed, frames);
            statWritten += accepted;
            statDropped += frames - accepted;
        }
    }
    pc::AudioService();
    if ((pc::settings.debugcon || pc::settings.debugserial) && (wasWriting != writing || (writing && now - statLogUs > 2000000))) {
        statLogUs = now;
        BX_INFO(("CharonPC sound: %s at %u%%, packets %lu, frames written %lu dropped %lu skipped %lu, fifo dry %lu, fifo max %u, queue max %u",
                 writing ? "playing" : "silent", (unsigned)(rate * 100u / 65536u), (unsigned long)statPackets, (unsigned long)statWritten, (unsigned long)statDropped,
                 (unsigned long)statSkipped, (unsigned long)statDry, statFillMax, statQueueMax));
    }
}

#undef LOG_THIS

bx_soundlow_waveout_charon_c* theWaveout;

class bx_sound_charon_c : public bx_sound_lowlevel_c {
public:
    bx_sound_charon_c() : bx_sound_lowlevel_c("charon") {}
    virtual ~bx_sound_charon_c() {}
    bx_soundlow_waveout_c* get_waveout() override
    {
        if (!waveout) waveout = theWaveout = new bx_soundlow_waveout_charon_c();
        return waveout;
    }
    // No recording and no MIDI device: Bochs's silent base classes.
    bx_soundlow_wavein_c* get_wavein() override
    {
        if (!wavein) wavein = new bx_soundlow_wavein_c();
        return wavein;
    }
    bx_soundlow_midiout_c* get_midiout() override
    {
        if (!midiout) midiout = new bx_soundlow_midiout_c();
        return midiout;
    }
} bx_sound_charon;

}  // namespace

void pc::SoundTick()
{
    if (theWaveout) theWaveout->Tick();
}

uint64_t pc::SoundGuestPackets() { return theWaveout ? theWaveout->GuestPackets() : 0; }

// Bochs's soundlow.cc starts its resampler and mixer in threads. This driver never asks for
// them (start_resampler_thread / start_mixer_thread are not called); the linker still wants
// the two functions. No thread is ever created in CharonPC.
extern "C" int pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*) { return 11; /* EAGAIN */ }
extern "C" void pthread_exit(void*) { abort(); }

#else
void pc::SoundTick() {}
uint64_t pc::SoundGuestPackets() { return 0; }
#endif
