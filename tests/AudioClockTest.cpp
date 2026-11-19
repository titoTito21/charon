// Clock robustness of HaikuAudioNode against a stalled or vanished time source.
// GPL-2.0-or-later. Registers a private BTimeSource in this process, slaves a
// node to it and checks that a stale clock (drift != 1, no publication for
// longer than the node's limit) and a time source without its shared area are
// reported as B_MEDIA_TIME_SOURCE_STOPPED instead of ending the team through
// BTimeSource's debugger() calls. No buffers are started; the mixer connection
// made by Open() stays idle.
#include "../src/HaikuAudioNode.h"
#include <Application.h>
#include <MediaRoster.h>
#include <TimeSource.h>
#include <OS.h>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
extern char** environ;

static std::atomic<int> rendered{0};
static void Render(void*, void* buffer, size_t size) { ++rendered;memset(buffer, 0, size); }
struct AudioClockTest {
    static void Send(HaikuAudioNode* node,bigtime_t when) { node->Send(when); }
};

struct TestClock : BTimeSource, BMediaEventLooper {
    std::atomic<bool> started{false};
    float drift;
    explicit TestClock(float drift)
        : BMediaNode("NVDA test clock"), BTimeSource(), BMediaEventLooper(), drift(drift) {}
    ~TestClock() override { Quit(); }
    BMediaAddOn* AddOn(int32*) const override { return nullptr; }
    void NodeRegistered() override { Run(); }
    void SetRunMode(run_mode mode) override {
        BTimeSource::SetRunMode(mode); BMediaEventLooper::SetRunMode(mode);
    }
    status_t HandleMessage(int32 code, const void* data, size_t size) override {
        if (BTimeSource::HandleMessage(code, data, size) == B_OK) return B_OK;
        return BMediaNode::HandleMessage(code, data, size);
    }
    void HandleEvent(const media_timed_event*, bigtime_t, bool) override {}
    status_t TimeSourceOp(const time_source_op_info& op, void*) override {
        if (op.op == B_TIMESOURCE_START) { started = true; Publish(); }
        else if (op.op == B_TIMESOURCE_STOP || op.op == B_TIMESOURCE_STOP_IMMEDIATELY) started = false;
        return B_OK;
    }
    void Publish() { bigtime_t now = system_time(); PublishTime(now, now, drift); }
    void PublishAged(bigtime_t age) {
        bigtime_t then = system_time() - age; PublishTime(then, then, drift);
    }
};

static int Fail(const char* what, status_t status) {
    fprintf(stderr, "FAIL %s: %s (%ld)\n", what, strerror(status), long(status));
    return 1;
}

int main(int argc,char** argv) {
    setbuf(stdout,nullptr);
    const bool discontinuityOnly=argc==2 && strcmp(argv[1],"--discontinuity-only")==0;
    BApplication app("application/x-vnd.NVDAHaiku.AudioClockTest");
    BMediaRoster* roster = BMediaRoster::Roster();
    if(argc==3 && strcmp(argv[1],"--publisher")==0) {
        auto* publisher=new TestClock(1.0005f);
        status_t status=roster->RegisterNode(publisher);
        if(status!=B_OK)return Fail("register external clock",status);
        status=roster->StartTimeSource(publisher->Node(),system_time());
        if(status!=B_OK)return Fail("start external clock",status);
        publisher->Publish();
        FILE* file=fopen(argv[2],"w");
        if(!file)return Fail("publish clock identity",B_ERROR);
        fprintf(file,"%ld\n",long(publisher->Node().node));fclose(file);
        for(;;) {publisher->Publish();snooze(20000);}
    }
    media_raw_audio_format format = media_raw_audio_format::wildcard;
    format.frame_rate = 22050; format.channel_count = 1;
    format.format = media_raw_audio_format::B_AUDIO_SHORT;
    format.byte_order = B_MEDIA_HOST_ENDIAN;

    // Phase A: live private clock with drift 1.0005 (the value that makes
    // BTimeSource::PerformanceTimeFor call debugger() once it is 2^24 us stale).
    HaikuAudioNode* node = new HaikuAudioNode(format, Render, nullptr, nullptr);
    status_t status = node->Open();
    if (status != B_OK) return Fail("open", status);
    TestClock* clock = new TestClock(1.0005f);
    if ((status = roster->RegisterNode(clock)) != B_OK) return Fail("register clock", status);
    if ((status = roster->SetTimeSourceFor(node->Node().node, clock->Node().node)) != B_OK)
        return Fail("slave node", status);
    if ((status = roster->StartTimeSource(clock->Node(), system_time())) != B_OK)
        return Fail("start clock", status);
    std::atomic<bool> publishing{true};
    std::thread publisher([&] { while (publishing) { clock->Publish(); snooze(20000); } });
    snooze(100000);
    bigtime_t first = node->PlaybackTime();
    snooze(300000);
    bigtime_t second = node->PlaybackTime();
    publishing = false; publisher.join();
    bigtime_t advance = second - first;
    if (node->Error() != B_OK) return Fail("live clock error", node->Error());
    if (advance < 250000 || advance > 400000) {
        fprintf(stderr, "FAIL live clock advanced %lld us in 300 ms\n", (long long)advance);
        return 1;
    }
    printf("PASS live private clock: playback time advanced %lld us in 300 ms, drift %.4f\n",
        (long long)advance, clock->drift);

    // Phase B: the clock stops publishing while reported running. The node must
    // report the stall after its limit (5 s, confirmed 200 ms later) and well
    // before the 16.7 s at which the stock Media Kit arithmetic ends the team.
    bigtime_t stalledAt = system_time();
    bigtime_t reportedAt = 0;
    while (system_time() - stalledAt < 12000000) {
        node->PlaybackTime();
        if (node->Error() != B_OK) { reportedAt = system_time(); break; }
        snooze(100000);
    }
    if (!reportedAt) { fprintf(stderr, "FAIL stalled clock not reported within 12 s\n"); return 1; }
    if (node->Error() != B_MEDIA_TIME_SOURCE_STOPPED) return Fail("stalled clock status", node->Error());
    bigtime_t after = reportedAt - stalledAt;
    if (after < 5000000 || after > 8000000) {
        fprintf(stderr, "FAIL stall reported after %lld us\n", (long long)after);
        return 1;
    }
    printf("PASS stalled clock reported as B_MEDIA_TIME_SOURCE_STOPPED after %.2f s, team alive\n",
        after / 1000000.0);
    // Halt/Close must still complete without a usable clock (events dispatch at once).
    bigtime_t haltStarted = system_time();
    status = node->Halt();
    if (status != B_OK) return Fail("halt without clock", status);
    node->Close(); node->Release();
    printf("PASS halt and close without a usable clock in %lld us\n",
        (long long)(system_time() - haltStarted));

    // First observation after a long suspension: no 200 ms grace may expose
    // an already dangerous delta to the consumer. Private clock, no PCM sent.
    HaikuAudioNode* resumed = new HaikuAudioNode(format, Render, nullptr, nullptr);
    if ((status = resumed->Open()) != B_OK) return Fail("open resumed", status);
    if ((status = roster->SetTimeSourceFor(resumed->Node().node, clock->Node().node)) != B_OK)
        return Fail("slave resumed", status);
    clock->Publish();
    resumed->PlaybackTime();
    if (resumed->Error() != B_OK) return Fail("resumed live baseline", resumed->Error());
    clock->PublishAged(20000000);
    const int beforeRender=rendered.load();
    AudioClockTest::Send(resumed,system_time());
    if (resumed->Error() != B_MEDIA_TIME_SOURCE_STOPPED)
        return Fail("first read after 20-second discontinuity", resumed->Error());
    if(rendered.load()!=beforeRender)return Fail("stale clock rendered PCM",B_ERROR);
    resumed->Close();resumed->Release();
    puts("PASS first stale observation rejected before PCM render/send after 20-second discontinuity");
    if(discontinuityOnly) {
        puts("PASS discontinuity-only scope; destructive area-deletion phase not run");
        return 0;
    }

    // Phase C: a real external publisher exits. Deleting a same-process area
    // under Media Kit threads invalidates their own pointers, unlike losing
    // an external publisher while the consumer's clone remains mapped.
    puts("BEGIN external publisher exit phase");
    char identityPath[]="/tmp/nvda-clock-publisher-XXXXXX";
    int identityFile=mkstemp(identityPath);
    if(identityFile<0)return Fail("publisher identity file",B_ERROR);
    close(identityFile);
    const char* childArgs[]={argv[0],"--publisher",identityPath,nullptr};
    thread_id publisherThread=load_image(3,childArgs,const_cast<const char**>(environ));
    if(publisherThread<0)return Fail("load publisher",publisherThread);
    thread_info publisherInfo;
    if((status=get_thread_info(publisherThread,&publisherInfo))!=B_OK)return Fail("publisher team",status);
    struct PublisherCleanup {
        team_id team;const char* path;
        ~PublisherCleanup() {if(team>=0)kill_team(team);unlink(path);}
    } publisherCleanup{publisherInfo.team,identityPath};
    if((status=resume_thread(publisherThread))!=B_OK)return Fail("resume publisher",status);
    long publisherNode=-1;
    bigtime_t deadline=system_time()+5000000;
    while(publisherNode<0 && system_time()<deadline) {
        FILE* file=fopen(identityPath,"r");
        if(file) {if(fscanf(file,"%ld",&publisherNode)!=1)publisherNode=-1;fclose(file);}
        if(publisherNode<0)snooze(20000);
    }
    if(publisherNode<0)return Fail("publisher ready",B_TIMED_OUT);
    HaikuAudioNode* second_node = new HaikuAudioNode(format, Render, nullptr, nullptr);
    if ((status = second_node->Open()) != B_OK) return Fail("open second", status);
    if ((status = roster->SetTimeSourceFor(second_node->Node().node, publisherNode)) != B_OK)
        return Fail("slave second", status);
    snooze(50000);
    second_node->PlaybackTime();
    if (second_node->Error() != B_OK) return Fail("second clock before deletion", second_node->Error());
    char name[32];
    snprintf(name, sizeof(name), "__timesource_buf_%ld", publisherNode);
    area_id area = find_area(name);
    if (area < 0) return Fail("find clock area", area);
    if ((status = kill_team(publisherCleanup.team)) != B_OK) return Fail("stop publisher", status);
    publisherCleanup.team=-1;
    deadline=system_time()+2000000;
    while(find_area(name)>=0 && system_time()<deadline)snooze(10000);
    if(find_area(name)>=0)return Fail("publisher area remained",B_TIMED_OUT);
    snooze(300000); // past the node's area re-check interval
    second_node->PlaybackTime();
    if (second_node->Error() != B_MEDIA_TIME_SOURCE_STOPPED) return Fail("vanished clock status", second_node->Error());
    printf("PASS time source without shared area reported as B_MEDIA_TIME_SOURCE_STOPPED\n");
    second_node->Close(); second_node->Release();
    roster->UnregisterNode(clock); clock->Release();
    puts("PASS audio clock robustness");
    return 0;
}
