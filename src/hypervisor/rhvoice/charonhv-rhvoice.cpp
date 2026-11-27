// SPDX-License-Identifier: GPL-2.0-or-later
// Charon Hypervisor: the RHVoice voice. A small front end to the RHVoice library (LGPL-2.1, pinned 1.18.5 source):
//   charonhv-rhvoice DATA_DIR PROFILE RATE PITCH VOLUME  < text (UTF-8)  > raw signed 16-bit mono PCM
// RATE, PITCH and VOLUME are relative (1.0 = the voice's own). The first line on stderr is "rate=N" (the sample
// rate), so the caller starts aplay with the right rate. Exit status 0 = spoken, 2 = no voice, 3 = synthesis failed.
//
// Session 10 (user: sound "as a normal PC", not delayed): loading the engine and the voice took ~90 ms of every
// utterance (measured 83-95 ms to the first sample, 254 ms cold). The engine can now stay loaded:
//   charonhv-rhvoice --serve SOCKET DATA_DIR      one loaded engine, one utterance at a time over a Unix socket
//   charonhv-rhvoice --client SOCKET DATA_DIR PROFILE RATE PITCH VOLUME  < text  > PCM
// The client sends "PROFILE RATE PITCH VOLUME\n" + the text, shuts its sending side and copies the PCM to stdout;
// when the server cannot be reached it speaks by itself (the old way), so speech never depends on the server. A
// client that is killed (the menu interrupts speech) closes the socket: the server's next write fails and the
// utterance stops at once.
#include <RHVoice.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int rateSent = 0;
static int outFd = 1;

static int setRate(int rate, void*)
{
    if (!rateSent) {
        std::fprintf(stderr, "rate=%d\n", rate);
        std::fflush(stderr);
        rateSent = rate;
    }
    return 1;
}

static bool writeAll(int fd, const char* p, size_t n)
{
    while (n) {
        ssize_t w = fd == 1 ? write(fd, p, n) : send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

static int play(const short* samples, unsigned int count, void*)
{
    return writeAll(outFd, reinterpret_cast<const char*>(samples), count * sizeof(short)) ? 1 : 0;
}

static RHVoice_tts_engine newEngine(const char* data)
{
    RHVoice_init_params init;
    std::memset(&init, 0, sizeof init);
    init.data_path = data;
    init.config_path = data;
    init.callbacks.set_sample_rate = setRate;
    init.callbacks.play_speech = play;
    RHVoice_tts_engine engine = RHVoice_new_tts_engine(&init);
    if (engine && RHVoice_get_number_of_voices(engine) == 0) {
        RHVoice_delete_tts_engine(engine);
        engine = nullptr;
    }
    if (!engine)
        std::fprintf(stderr, "no voice in %s\n", data);
    return engine;
}

static bool speak(RHVoice_tts_engine engine, const std::string& profile, double rate, double pitch, double volume,
                  const std::string& text)
{
    RHVoice_synth_params p;
    std::memset(&p, 0, sizeof p);
    p.voice_profile = profile.c_str();
    p.relative_rate = rate;
    p.relative_pitch = pitch;
    p.relative_volume = volume;
    p.punctuation_mode = RHVoice_punctuation_default;
    p.capitals_mode = RHVoice_capitals_default;
    RHVoice_message msg = RHVoice_new_message(engine, text.data(), static_cast<unsigned int>(text.size()),
                                              RHVoice_message_text, &p, nullptr);
    bool ok = msg && RHVoice_speak(msg);
    if (msg)
        RHVoice_delete_message(msg);
    return ok;
}

static int unixSocket(const char* path, sockaddr_un& addr)
{
    std::memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (std::strlen(path) >= sizeof addr.sun_path)
        return -1;
    std::strcpy(addr.sun_path, path);
    return socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
}

static int serve(const char* path, const char* data)
{
    RHVoice_tts_engine engine = newEngine(data);
    if (!engine)
        return 2;
    sockaddr_un addr;
    int s = unixSocket(path, addr);
    unlink(path);
    if (s < 0 || bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(s, 4) != 0) {
        std::fprintf(stderr, "cannot listen on %s: %s\n", path, std::strerror(errno));
        return 1;
    }
    std::signal(SIGPIPE, SIG_IGN);
    for (;;) {
        int c = accept(s, nullptr, nullptr);
        if (c < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        std::string req;
        char buf[4096];
        ssize_t n;
        while ((n = read(c, buf, sizeof buf)) > 0 && req.size() < (1u << 20))
            req.append(buf, static_cast<size_t>(n));
        size_t nl = req.find('\n');
        if (nl != std::string::npos) {
            std::istringstream head(req.substr(0, nl));
            std::string profile;
            double rate = 1, pitch = 1, volume = 1;
            if (head >> profile >> rate >> pitch >> volume) {
                outFd = c;
                speak(engine, profile, rate, pitch, volume, req.substr(nl + 1));
                outFd = 1;
            }
        }
        close(c);
    }
    RHVoice_delete_tts_engine(engine);
    return 1;
}

static int local(const char* data, const char* profile, const char* rate, const char* pitch, const char* volume,
                 const std::string& text)
{
    RHVoice_tts_engine engine = newEngine(data);
    if (!engine)
        return 2;
    bool ok = speak(engine, profile, std::atof(rate), std::atof(pitch), std::atof(volume), text);
    RHVoice_delete_tts_engine(engine);
    return ok ? 0 : 3;
}

static int client(char** a, const std::string& text)
{
    // a: SOCKET DATA_DIR PROFILE RATE PITCH VOLUME
    sockaddr_un addr;
    int s = unixSocket(a[0], addr);
    if (s < 0 || connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        if (s >= 0)
            close(s);
        return local(a[1], a[2], a[3], a[4], a[5], text);
    }
    std::string req = std::string(a[2]) + " " + a[3] + " " + a[4] + " " + a[5] + "\n" + text;
    if (!writeAll(s, req.data(), req.size())) {
        close(s);
        return local(a[1], a[2], a[3], a[4], a[5], text);
    }
    shutdown(s, SHUT_WR);
    char buf[8192];
    ssize_t n;
    size_t got = 0;
    while ((n = read(s, buf, sizeof buf)) > 0) {
        if (!writeAll(1, buf, static_cast<size_t>(n)))
            break;
        got += static_cast<size_t>(n);
    }
    close(s);
    return got ? 0 : 3;
}

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--serve") == 0)
        return serve(argv[2], argv[3]);
    if (argc != 6 && !(argc == 8 && std::strcmp(argv[1], "--client") == 0)) {
        std::fprintf(stderr, "usage: charonhv-rhvoice DATA_DIR PROFILE RATE PITCH VOLUME < text\n"
                             "       charonhv-rhvoice --serve SOCKET DATA_DIR\n"
                             "       charonhv-rhvoice --client SOCKET DATA_DIR PROFILE RATE PITCH VOLUME < text\n");
        return 1;
    }
    std::string text((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    if (argc == 8)
        return client(argv + 2, text);
    return local(argv[1], argv[2], argv[3], argv[4], argv[5], text);
}
