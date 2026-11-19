// Native check of the kit's start-up manager pieces that need no restart:
// options for one start in a kernel settings file, the user's settings file,
// the decision file format, the start-up signal. GPL-2.0-or-later.
// Usage: BootManagerTest DIRECTORY     (an empty scratch directory)
#include "../src/HAKBootManager.h"
#include "../src/HAKBootSettingsTexts.h"
#include "../src/HAKBootAudioMix.h"

#include <cstdio>
#include <string>
#include <vector>

#define CHECK(condition) do { if(!(condition)) { printf("FAIL %s (line %d)\n",#condition,__LINE__); return 1; } } while(0)

int main(int argc,char** argv) {
    CHECK(argc==2);
    const std::string kernel=std::string(argv[1])+"/kernel";
    const std::string settingsPath=std::string(argv[1])+"/settings";
    std::vector<std::string> options;
    // No file: nothing stored, nothing to remove, and no file is created by looking.
    CHECK(hakboot::PeekOnce(options,kernel.c_str()) && options.empty());
    CHECK(hakboot::RemoveOnce(options,kernel.c_str()) && options.empty());
    CHECK(access(kernel.c_str(),F_OK)!=0);
    // The user's own lines survive, with their comments, byte for byte.
    const std::string own="#disable_smp true\n\t# a comment\nload_symbols true\nserial_debug_output false";
    CHECK(hakboot::WriteFileAtomically(kernel.c_str(),own));
    CHECK(hakboot::AddOnce({"safemode","disable_user_addons"},kernel.c_str()));
    std::string data;
    CHECK(hakboot::ReadWhole(kernel.c_str(),data));
    CHECK(data==own+"\nsafemode true # hak-boot-once\ndisable_user_addons true # hak-boot-once\n");
    CHECK(hakboot::PeekOnce(options,kernel.c_str()) && options.size()==2 && options[0]=="safemode" && options[1]=="disable_user_addons");
    CHECK(hakboot::ReadWhole(kernel.c_str(),data) && data.find("hak-boot-once")!=std::string::npos);   // looking changes nothing
    CHECK(hakboot::RemoveOnce(options,kernel.c_str()) && options.size()==2);
    CHECK(hakboot::ReadWhole(kernel.c_str(),data) && data==own+"\n");
    CHECK(hakboot::RemoveOnce(options,kernel.c_str()) && options.empty());      // twice is harmless
    CHECK(hakboot::ReadWhole(kernel.c_str(),data) && data==own+"\n");
    // A line of the user that only looks similar is not touched.
    CHECK(hakboot::WriteFileAtomically(kernel.c_str(),"safemode true\n"));
    CHECK(hakboot::AddOnce({"fail_safe_video_mode"},kernel.c_str()));
    CHECK(hakboot::RemoveOnce(options,kernel.c_str()) && options.size()==1 && options[0]=="fail_safe_video_mode");
    CHECK(hakboot::ReadWhole(kernel.c_str(),data) && data=="safemode true\n");

    // Settings: defaults, values, comments, rubbish.
    hakboot::Settings settings;
    hakboot::LoadSettings(settings,settingsPath.c_str());                 // no file
    CHECK(settings.menu && settings.signal && settings.promptSeconds==5 && settings.language.empty());
    CHECK(hakboot::WriteFileAtomically(settingsPath.c_str(),
        "# my choices\nmenu off\n  signal   off  \nprompt_seconds 12 # longer\nlanguage pl\nunknown thing\n"));
    hakboot::LoadSettings(settings,settingsPath.c_str());
    CHECK(!settings.menu && !settings.signal && settings.promptSeconds==12 && settings.language=="pl");
    CHECK(hakboot::WriteFileAtomically(settingsPath.c_str(),
        "menu maybe\nprompt_seconds 0\nprompt_seconds 999\nprompt_seconds 7x\nlanguage xx\nsignal on\nmenu\n"));
    hakboot::LoadSettings(settings,settingsPath.c_str());
    CHECK(!settings.menu && settings.signal && settings.promptSeconds==12 && settings.language=="pl");   // bad values change nothing

    // Texts: every language has every text, and the fallback is English.
    for(const hakboot::Texts& texts : hakboot::kTexts) {
        CHECK(texts.prompt && *texts.prompt && texts.menu && *texts.menu && texts.position && texts.safeModeNow && texts.failSafeNow && texts.notWritten);
        for(int item=0;item<hakboot::kChoiceCount;++item)CHECK(texts.items[item] && *texts.items[item] && texts.confirmed[item] && *texts.confirmed[item]);
        char position[64];
        CHECK(snprintf(position,sizeof(position),texts.position,2,7)>3);
    }
    CHECK(std::string(hakboot::TextsFor("pl")->language)=="pl" && std::string(hakboot::TextsFor("zz")->language)=="en");
    CHECK(std::string(hakboot::DecisionWord(hakboot::kNoReader))=="no-reader"
        && std::string(hakboot::DecisionWord(hakboot::kReaderNoAddons))=="reader-no-addons"
        && std::string(hakboot::DecisionWord(hakboot::kContinue))=="normal"
        && std::string(hakboot::DecisionWord(hakboot::kSafeMode))=="normal");
    // The signal: about a third of a second, not silent, no clipping.
    std::vector<int16> signal;
    hakboot::Signal(signal,22050);
    int peak=0;
    for(int16 sample : signal)if(abs(sample)>peak)peak=abs(sample);
    CHECK(signal.size()>22050/5 && signal.size()<22050 && peak>4000 && peak<20000);
    // r256: the speed and the status level in the settings; one setting written back.
    {
        const std::string more=std::string(argv[1])+"/settings-r256";
        CHECK(hakboot::WriteFileAtomically(more.c_str(),"# mine\nmenu off\nrate 9999\nstatus brief\n"));
        hakboot::Settings loaded;
        hakboot::LoadSettings(loaded,more.c_str());
        CHECK(!loaded.menu && loaded.rate==0 && loaded.status==1);       // a speed out of range is ignored
        CHECK(hakboot::StoreSetting("rate","200",more.c_str()));
        CHECK(hakboot::StoreSetting("rate","225",more.c_str()));
        std::string stored;
        CHECK(hakboot::ReadWhole(more.c_str(),stored));
        CHECK(stored=="# mine\nmenu off\nrate 225\nstatus brief\n");     // replaced in place, the rest untouched
        hakboot::LoadSettings(loaded,more.c_str());
        CHECK(loaded.rate==225 && !loaded.menu);
        const std::string fresh=std::string(argv[1])+"/settings-new";
        CHECK(hakboot::StoreSetting("rate","150",fresh.c_str()) && hakboot::ReadWhole(fresh.c_str(),stored) && stored=="rate 150\n");
        hakboot::Settings defaults;
        CHECK(defaults.status==2 && defaults.rate==0);                   // 2: notes of the melody, no words
        // "full" (the word of older files, when names were spoken) and "tones" are the same level.
        const std::string words=std::string(argv[1])+"/settings-tones";
        hakboot::Settings level;
        CHECK(hakboot::WriteFileAtomically(words.c_str(),"status off\n"));
        hakboot::LoadSettings(level,words.c_str());
        CHECK(level.status==0 && hakboot::StoreSetting("status","full",words.c_str()));
        hakboot::LoadSettings(level,words.c_str());
        CHECK(level.status==2 && hakboot::StoreSetting("status","brief",words.c_str()) && hakboot::StoreSetting("status","tones",words.c_str()));
        hakboot::LoadSettings(level,words.c_str());
        CHECK(level.status==2 && sizeof(hakboot::kStatusWords)/sizeof(hakboot::kStatusWords[0])==size_t(hakboot::kStatusLevels));
        // The notes: a different pitch for every part, short, never clipped.
        std::vector<int16> first,last;
        hakboot::Cue(first,22050,0);
        hakboot::Cue(last,22050,hakboot::kPartCount-1);
        CHECK(first.size()==last.size() && first.size()>22050/5 && first.size()<22050/2 && first!=last);
        for(int16 sample : first)CHECK(sample>-12000 && sample<12000);
        // The start-up disk menu: every language has every text, with its place holders.
        for(const hakboot::DiskTexts& texts : hakboot::kDiskTexts)
            CHECK(texts.door[0] && texts.menu[0] && texts.notSetUp[0] && texts.notPossible[0] && texts.usualItem[0]
                && strstr(texts.partition,"%s") && strstr(texts.whole,"%s") && texts.unnamed[0] && strstr(texts.gigabytes,"%lld")
                && strstr(texts.megabytes,"%lld") && texts.current[0] && texts.removable[0] && texts.everyTime[0] && texts.nextStart[0]
                && strstr(texts.key,"%d") && strstr(texts.once,"%s") && strstr(texts.always,"%s") && texts.usualSet[0]
                && texts.failed[0] && texts.none[0]);
        CHECK(sizeof(hakboot::kDiskTexts)/sizeof(hakboot::kDiskTexts[0])==sizeof(hakboot::kTexts)/sizeof(hakboot::kTexts[0]));
    }
    // What has started, in words.
    {
        const hakboot::MoreTexts* english=hakboot::MoreFor("en");
        bool running[hakboot::kPartCount]={true,true,true,false,false,false,false,false,false};
        CHECK(hakboot::StatusText(english,running,true,false)
            =="Running: system services, graphics, keyboard and mouse. "
              "Still starting: disks, sound, sound devices, network, Tracker, Deskbar, screen reader.");
        for(bool& part : running)part=true;
        CHECK(hakboot::StatusText(english,running,false,false).find("Everything has started.")!=std::string::npos);
        CHECK(hakboot::WaitingText(english,running,false,false).empty());
        CHECK(hakboot::WaitingText(english,running,true,false)=="Still starting: screen reader.");
        CHECK(hakboot::WaitingText(english,running,true,true).empty());
        CHECK(std::string(hakboot::MoreFor("pl")->parts[6])=="sie\xc4\x87" && hakboot::MoreFor("xx")==english);
        CHECK(sizeof(hakboot::kMore)/sizeof(hakboot::kMore[0])==sizeof(hakboot::kTexts)/sizeof(hakboot::kTexts[0]));
        for(size_t index=0;index<sizeof(hakboot::kMore)/sizeof(hakboot::kMore[0]);++index)
            CHECK(std::string(hakboot::kMore[index].language)==hakboot::kTexts[index].language);
    }
    // How a start went, kept for the next one.
    {
        const std::string path=std::string(argv[1])+"/summary";
        hakboot::Summary none;
        CHECK(!hakboot::ReadSummary(none,path.c_str()) && !none.known);
        const hakboot::Texts* english=hakboot::TextsFor("en");
        const hakboot::MoreTexts* more=hakboot::MoreFor("en");
        CHECK(hakboot::LastStartText(english,more,none)=="Nothing is known about the previous start.");
        hakboot::Summary written;
        written.seconds=47;written.choice=hakboot::kReaderNoAddons;written.reader="missing";written.safeMode=false;
        CHECK(hakboot::WriteSummary(written,path.c_str()));
        hakboot::Summary read;
        CHECK(hakboot::ReadSummary(read,path.c_str()) && read.known && read.seconds==47
            && read.choice==hakboot::kReaderNoAddons && read.reader=="missing" && !read.safeMode);
        CHECK(hakboot::LastStartText(english,more,read)=="The previous start took 47 seconds. "
            "Chosen in the menu: Start the screen reader without add-ons. The screen reader did not start.");
        CHECK(hakboot::WriteFileAtomically(path.c_str(),"seconds 12\nchoice 99\nreader ready\nmode normal\n"));
        hakboot::Summary bad;
        CHECK(!hakboot::ReadSummary(bad,path.c_str()) && !bad.known);       // a choice that does not exist: nothing is told
    }
    // The sound in the background: adds to what is there, continues across calls, never clips, can be lowered.
    {
        std::vector<int16> one(22050,0),two(22050,0),whole(44100,0),low(22050,0);
        uint64 position=0,other=0,third=0;
        hakboot::Bed(one.data(),one.size(),22050,position,1.0);
        hakboot::Bed(two.data(),two.size(),22050,position,1.0);
        hakboot::Bed(whole.data(),whole.size(),22050,other,1.0);
        CHECK(position==44100 && other==44100);
        CHECK(std::vector<int16>(whole.begin(),whole.begin()+22050)==one && std::vector<int16>(whole.begin()+22050,whole.end())==two);
        hakboot::Bed(low.data(),low.size(),22050,third,0.35);
        int loud=0,soft=0;
        for(int16 sample : one)if(abs(sample)>loud)loud=abs(sample);
        for(int16 sample : low)if(abs(sample)>soft)soft=abs(sample);
        CHECK(loud>1000 && loud<3000 && soft>300 && soft<loud/2);
        std::vector<int16> full(2000,32000);
        uint64 fourth=0;
        hakboot::Bed(full.data(),full.size(),22050,fourth,1.0);
        for(int16 sample : full)CHECK(sample>=29000);   // added, limited, not wrapped
        hakboot::Settings loaded;
        const std::string path=std::string(argv[1])+"/settings-bed";
        CHECK(loaded.bed && hakboot::StoreSetting("bed","off",path.c_str()) && hakboot::StoreSetting("status","brief",path.c_str()));
        hakboot::LoadSettings(loaded,path.c_str());
        CHECK(!loaded.bed && loaded.status==1);
        for(const hakboot::MoreTexts& texts : hakboot::kMore)
            CHECK(texts.readerStarting && texts.levels[2] && texts.on && texts.off && strstr(texts.statusItem,"%s") && strstr(texts.bedItem,"%s")
                && strstr(texts.signalItem,"%s") && strstr(texts.promptItem,"%ld") && strstr(texts.languageItem,"%s")
                && strstr(texts.speedItem,"%d") && texts.settingsItem[0] && texts.settingsMenu[0] && texts.systemLanguage[0]);
        CHECK(sizeof(hakboot::kLanguageNames)/sizeof(hakboot::kLanguageNames[0])==sizeof(hakboot::kTexts)/sizeof(hakboot::kTexts[0]));
        // "language system" in the settings file is the system's language (an unknown word is ignored).
        CHECK(hakboot::StoreSetting("language","de",path.c_str()));
        hakboot::LoadSettings(loaded,path.c_str());
        CHECK(loaded.language=="de" && hakboot::StoreSetting("language","system",path.c_str()));
        hakboot::Settings again;
        hakboot::LoadSettings(again,path.c_str());
        CHECK(again.language.empty() && !again.bed);
    }
    // A sound file: stereo at twice the rate becomes mono at the rate; other things are refused.
    {
        const std::string path=std::string(argv[1])+"/sound.wav";
        std::string wave("RIFF____WAVEfmt ",16);
        auto put=[&wave](uint32 value,int bytes) {for(int index=0;index<bytes;++index)wave+=char((value>>(8*index))&255);};
        put(16,4);put(1,2);put(2,2);put(44100,4);put(44100*4,4);put(4,2);put(16,2);
        wave+="data";put(8*4,4);
        for(int frame=0;frame<8;++frame) {put(uint32(1000*frame)&0xffff,2);put(uint32(1000*frame+200)&0xffff,2);}
        CHECK(hakboot::WriteFileAtomically(path.c_str(),wave));
        std::vector<int16> sound;
        CHECK(hakboot::LoadWave(path.c_str(),sound,22050) && sound.size()==4);
        CHECK(sound[0]==600 && sound[1]==2600 && sound[3]==6600);    // left+right, then pairs of frames, averaged
        CHECK(hakboot::LoadWave(path.c_str(),sound,44100) && sound.size()==8 && sound[1]==1100);
        wave[20]=3;                                                   // not PCM
        CHECK(hakboot::WriteFileAtomically(path.c_str(),wave) && !hakboot::LoadWave(path.c_str(),sound,22050));
        CHECK(!hakboot::LoadWave((path+".none").c_str(),sound,22050));
    }
    // User-facing control semantics, bounds and persistence of the entire settings panel.
    {
        const std::string path=std::string(argv[1])+"/controls";
        hakboot::Settings model;
        CHECK(hakboot::kSettingCount==13);
        for(int setting=0;setting<hakboot::kSettingCount;++setting) {
            const auto kind=hakboot::ControlFor(setting);
            CHECK(hakboot::SettingDirection(setting,' ')==(kind==hakboot::kCheckbox ? 1 : 0));
            CHECK(hakboot::SettingDirection(setting,B_LEFT_ARROW)==(kind==hakboot::kCheckbox ? 0 : -1));
            CHECK(hakboot::SettingDirection(setting,B_RIGHT_ARROW)==(kind==hakboot::kCheckbox ? 0 : 1));
            CHECK(hakboot::SettingDirection(setting,B_DOWN_ARROW)==0);
            for(int step=0;step<7;++step) {
                CHECK(hakboot::ChangeSetting(model,setting,1));
                const auto value=hakboot::SettingValue(model,setting);
                CHECK(hakboot::StoreSetting(hakboot::SettingKey(setting),value,path.c_str()));
                hakboot::Settings loaded;
                hakboot::LoadSettings(loaded,path.c_str());
                CHECK(hakboot::SettingValue(loaded,setting)==value);
            }
        }
        CHECK(!hakboot::ChangeSetting(model,-1,1) && !hakboot::ChangeSetting(model,0,0));
        CHECK(hakboot::SettingDirection(-1,' ')==0);
        for(int setting:{hakboot::kSettingPrompt,hakboot::kSettingSpeed,hakboot::kSettingMenuTimeout,
            hakboot::kSettingReaderWait,hakboot::kSettingVolume,hakboot::kSettingPitch}) {
            for(int step=0;step<500;++step)CHECK(hakboot::ChangeSetting(model,setting,-1));
            auto minimum=hakboot::SettingValue(model,setting);
            CHECK(hakboot::ChangeSetting(model,setting,-1) && hakboot::SettingValue(model,setting)==minimum);
            for(int step=0;step<500;++step)CHECK(hakboot::ChangeSetting(model,setting,1));
            auto maximum=hakboot::SettingValue(model,setting);
            CHECK(hakboot::ChangeSetting(model,setting,1) && hakboot::SettingValue(model,setting)==maximum);
        }
        CHECK(model.volume==100 && model.pitch==100 && model.rate==450 && model.promptSeconds==60
            && model.menuSeconds==300 && model.readerSeconds==180);
        CHECK(hakboot::WriteFileAtomically(path.c_str(),"start_melody 3\nsignal off\nbackground_melody 4\nbed off\n"));
        hakboot::Settings loaded;hakboot::LoadSettings(loaded,path.c_str());
        CHECK(loaded.signal && loaded.bed && loaded.startMelody==3 && loaded.backgroundMelody==4);
        CHECK(hakboot::WriteFileAtomically(path.c_str(),"signal off\nbed off\n"));
        hakboot::Settings legacy;hakboot::LoadSettings(legacy,path.c_str());
        CHECK(!legacy.signal && !legacy.bed && legacy.startMelody==0 && legacy.backgroundMelody==0);
        CHECK(hakboot::WriteFileAtomically(path.c_str(),"volume 0\npitch 101\nmenu_seconds 0\nreader_seconds 9999\n"
            "start_melody 5\nbackground_melody -1\ndesktop_melody 99999999999999999999999999\n"));
        hakboot::Settings bad;hakboot::LoadSettings(bad,path.c_str());
        CHECK(bad.volume==100 && bad.pitch==50 && bad.menuSeconds==120 && bad.readerSeconds==60
            && bad.startMelody==1 && bad.backgroundMelody==1 && bad.desktopMelody==2);
        CHECK(hakboot::WriteFileAtomically(path.c_str(),"language system\n"));
        bad.language="pl";hakboot::LoadSettings(bad,path.c_str());CHECK(bad.language.empty());
        CHECK(sizeof(hakboot::kSettingsTexts)/sizeof(hakboot::kSettingsTexts[0])==5);
        for(const auto& language:hakboot::kSettingsTexts) {
            CHECK(language.help[0]);
            for(const char* melody:language.melodies)CHECK(melody && *melody);
            for(int setting=0;setting<hakboot::kSettingCount;++setting)
                CHECK(strstr(language.labels[setting],hakboot::ControlFor(setting)==hakboot::kSlider ? "%ld" : "%s"));
        }
        CHECK(std::string(hakboot::SettingsFor("en")->labels[hakboot::kSettingSpeed])=="Rate: %ld");
        CHECK(std::string(hakboot::SettingsFor("pl")->labels[hakboot::kSettingSpeed])=="Tempo: %ld");
        printf("PASS r263: 13 controls, keyboard semantics, slider bounds, settings round-trip, legacy files, five languages\n");
    }
    // Generated choices differ, carry their phase across buffers, and Off adds no sound.
    {
        std::vector<int16> previous,melody;
        hakboot::Melody(melody,22050,0);CHECK(melody.empty());
        for(int choice=1;choice<=4;++choice) {
            hakboot::Melody(melody,22050,choice);
            CHECK(!melody.empty() && melody!=previous && melody.front()==0 && abs(melody.back())<200);
            for(int16 sample:melody)CHECK(abs(sample)<=9000);
            previous=melody;
            std::vector<int16> whole(44100,0),first(22050,0),second(22050,0);
            uint64 all=0,parts=0;
            hakboot::Bed(whole.data(),whole.size(),22050,all,1.0,choice);
            hakboot::Bed(first.data(),first.size(),22050,parts,1.0,choice);
            hakboot::Bed(second.data(),second.size(),22050,parts,1.0,choice);
            CHECK(all==parts && std::vector<int16>(whole.begin(),whole.begin()+22050)==first
                && std::vector<int16>(whole.begin()+22050,whole.end())==second);
        }
        std::vector<int16> untouched(1000,123);uint64 position=0;
        hakboot::Bed(untouched.data(),untouched.size(),22050,position,1.0,0);
        CHECK(position==1000 && untouched==std::vector<int16>(1000,123));
        printf("PASS r263: four generated melodies plus Off, bounded audio, continuous background phase\n");
    }
    // Speech replacement and cancellation must never reset, discard or pause a melody.
    {
        hakboot::EarlyMix mix;mix.rate=22050;mix.bed=false;
        std::vector<int16> melody;
        for(int index=1;index<=12;++index)melody.push_back(int16(index*100));
        mix.Chime(melody,22050);
        int16 output[4];
        mix.Say(std::vector<int16>(8,10),22050);mix.Fill(output,4);
        CHECK(output[0]==110 && output[3]==410);
        mix.Say(std::vector<int16>(8,20),22050);mix.Fill(output,4);
        CHECK(output[0]==520 && output[3]==820); // new speech, same melody position
        mix.Chime(std::vector<int16>(4,2000),22050);mix.Hush();mix.Fill(output,4);
        CHECK(output[0]==900 && output[3]==1200); // queued cue did not replace the melody
        mix.Fill(output,4);CHECK(output[0]==2000 && output[3]==2000);
        mix.Fill(output,4);CHECK(output[0]==0 && output[3]==0);
        CHECK(mix.bedPosition==20);
        mix.Chime(std::vector<int16>(4,32000),22050);
        mix.Say(std::vector<int16>(4,32000),22050);mix.Fill(output,4);
        CHECK(output[0]==32767 && output[3]==32767); // saturation, never integer wrap
        printf("PASS r263: replacing/hushing eSpeak preserves melody progress; queued cues; bounded mixing\n");
    }
    printf("PASS r256 pieces: speed and status settings, a setting written back in place, status in words, the kept start, "
        "the background sound\n");
    printf("PASS start-up manager pieces: options for one start (user lines kept byte for byte), settings file, "
        "texts in %d languages, decision words, signal of %d ms\n",int(sizeof(hakboot::kTexts)/sizeof(hakboot::kTexts[0])),
        int(signal.size()*1000/22050));
    return 0;
}
