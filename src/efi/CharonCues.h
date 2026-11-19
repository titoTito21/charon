#pragma once
#include "BootMenuModel.h"
namespace hakefi {
// r361: systemLaunch (the operating system takes over, ExitBootServices) and
// bootManagerLoaded (another boot manager starts) are optional assets: an
// ESP prepared before r361 lacks them and plays nothing for these events.
enum class MenuCue {none,welcome,focus,edge,wrap,submenuOpen,submenuClose,popupOpen,popupClose,dialogOpen,dialogClose,select,continueBoot,systemLaunch,bootManagerLoaded};
constexpr unsigned menuCueCount=15,requiredMenuCues=13;
// Supplied only by an actual shared mixer. QueueCue is nonblocking; disabling
// cancels pending/active effects, independently of live speech. Null means no
// effects backend, never successful silent playback.
struct AudioEffects {
    void* context=nullptr;
    void (*QueueCue)(void*,MenuCue)=nullptr;
    void (*SetEnabled)(void*,bool)=nullptr;
    void (*SetMelodies)(void*,unsigned,unsigned)=nullptr;
    // Speech rate in tenths (5..20 = 50%..200%). Optional; null keeps 100%.
    void (*SetRate)(void*,unsigned)=nullptr;
    // Speech 1..10 and effects (cues + melodies) 0..10, in tenths of the
    // provider's default mix. Optional; null keeps the default gains.
    bool (*SetVolumes)(void*,unsigned,unsigned)=nullptr;
    // True when no utterance is preparing or audible. Optional; callers that
    // wait for speech must bound the wait and treat null as "unknown".
    bool (*SpeechIdle)(void*)=nullptr;
    // True when no UI cue is queued or sounding. Optional (r361).
    bool (*CuesIdle)(void*)=nullptr;
    // Short sine beep (NVDA progress style), mixed on the cue channel. Optional.
    void (*Tone)(void*,unsigned hertz,unsigned milliseconds)=nullptr;
    // Voice pitch in tenths (5..20 = 50%..200%). Optional (r382); null keeps 100%.
    void (*SetPitch)(void*,unsigned)=nullptr;
    // r389: the second voice for boot messages read before a menu. It speaks
    // at the same time as the reader and is never cut by it; a newer message
    // is said after the current one. Optional; null = the reader's voice.
    bool (*SpeakMessage)(void*,const char* language,const char* utf8)=nullptr;
    void (*SetMessageVoice)(void*,unsigned rateTenths,unsigned pitchTenths)=nullptr;
    bool (*MessageIdle)(void*)=nullptr;
    // r389: Ctrl (as in NVDA): stop both voices now; the next text speaks again.
    void (*Silence)(void*)=nullptr;
    // r385: square-wave beep like a PC speaker's POST beep, on the cue
    // channel; asked for before the audio controller is bound, it plays as
    // soon as the stream opens. Optional.
    void (*Beep)(void*,unsigned hertz,unsigned milliseconds)=nullptr;
    // r393: synthesizer add-on (\EFI\HAK\synths, CharonSynthHost.h): key 0 = TVox,
    // else the manifest key and the index of the voice in its manifest. Only at
    // TPL_APPLICATION (it may load a module). Writes what happened (spoken by
    // the caller) into report. Optional; null = TVox only.
    bool (*SetSynth)(void*,uint8_t key,uint8_t voice,char16_t* report,size_t capacity)=nullptr;
    // r393: the loaded add-on's voices for the settings ring (no loading): count,
    // the voice in use (SIZE_MAX: TVox) and switching among TVox and them.
    size_t (*SynthVoices)(void*,size_t* current)=nullptr;
    bool (*UseSynthVoice)(void*,size_t index,char16_t* name,size_t capacity)=nullptr;
    // r396: the installed add-ons for the ring's synthesizer setting (scanned
    // once, no loading): index 0.. -> manifest key and name; false past the
    // end. ActiveSynthKey: 0 = TVox.
    bool (*SynthAt)(void*,size_t index,uint8_t* key,char16_t* name,size_t capacity)=nullptr;
    uint8_t (*ActiveSynthKey)(void*)=nullptr;
    // r396e: modules are loaded only at TPL_APPLICATION; the ring switches at
    // TPL_CALLBACK. PreloadSynths (TPL_APPLICATION) loads every installed
    // add-on once; SynthLoaded says whether switching to key needs no load.
    void (*PreloadSynths)(void*)=nullptr;
    bool (*SynthLoaded)(void*,uint8_t key)=nullptr;
};
// r382: one voice profile (the menu's, or the screen reader's) on the shared mixer.
inline void ApplyVoiceProfile(AudioEffects* effects,unsigned rate,unsigned pitch,unsigned volume,unsigned effectsVolume) {
    if(!effects)return;
    if(effects->SetRate)effects->SetRate(effects->context,rate);
    if(effects->SetPitch)effects->SetPitch(effects->context,pitch);
    if(effects->SetVolumes)effects->SetVolumes(effects->context,volume,effectsVolume);
}
inline const char* MenuCueAsset(MenuCue cue) {
    switch(cue) {
        case MenuCue::welcome:return "welcome.wav";
        case MenuCue::focus:return "focus.wav";
        case MenuCue::edge:return "edge.wav";
        case MenuCue::wrap:return "wrap.wav";
        case MenuCue::submenuOpen:return "submenu_open.wav";
        case MenuCue::submenuClose:return "submenu_close.wav";
        case MenuCue::popupOpen:return "popUp.wav";
        case MenuCue::popupClose:return "popup_close.wav";
        case MenuCue::dialogOpen:return "dialog_open.wav";
        case MenuCue::dialogClose:return "dialog_close.wav";
        case MenuCue::select:return "select.wav";
        case MenuCue::continueBoot:return "continue.wav";
        case MenuCue::systemLaunch:return "system_will_launch.wav";
        case MenuCue::bootManagerLoaded:return "boot_manager_loaded.wav";
        default:return nullptr;
    }
}
inline MenuCue TransitionCue(const MenuModel& before,MenuEvent event,MenuAction action,const MenuModel& after) {
    if(!after.sounds)return MenuCue::none;
    switch(action) {
        case MenuAction::changed:
            return (before.wrap && ((event==MenuEvent::up && before.index==0)
                || (event==MenuEvent::down && before.index+1==before.Count())))?MenuCue::wrap:MenuCue::focus;
        case MenuAction::edge:case MenuAction::timeoutEdge:case MenuAction::clockEdge:return MenuCue::edge;
        case MenuAction::changeClock:return MenuCue::focus;
        case MenuAction::moveEntry:return MenuCue::focus;
        case MenuAction::grabEntry:case MenuAction::dropEntry:case MenuAction::grabCancelled:case MenuAction::revertOrder:
        case MenuAction::toggleRemember:return MenuCue::select;
        // Container lifecycle belongs to successfully presented frames below,
        // not requested actions. A failed paint is not an opened popup.
        case MenuAction::continueBoot:return MenuCue::continueBoot;
        case MenuAction::startDevice:case MenuAction::toggleWrap:case MenuAction::toggleSounds:
        case MenuAction::refresh:case MenuAction::firmwareInfo:
        case MenuAction::systemInfo:case MenuAction::clockInfo:case MenuAction::updatesInfo:case MenuAction::listDetail:case MenuAction::connectAll:case MenuAction::toggleWelcome:case MenuAction::toggleDetail:case MenuAction::toggleTimer:case MenuAction::toggleCharonFirst:return MenuCue::select;
        default:return MenuCue::none;
    }
}
inline bool Popup(MenuPage page) {
    return page==MenuPage::language || page==MenuPage::notice || page==MenuPage::startMelody || page==MenuPage::backgroundMelody
        || page==MenuPage::verbosity || page==MenuPage::autoDelay || page==MenuPage::progress || page==MenuPage::welcome;
}
inline bool Submenu(MenuPage page) {return page==MenuPage::settings || page==MenuPage::reader || page==MenuPage::detail || page==MenuPage::devices || page==MenuPage::bootOrder || page==MenuPage::tools || page==MenuPage::infoList || page==MenuPage::clockEdit;}
struct PresentedMenuCues {
    bool initialized=false;
    bool menuOpenPending=false;
    bool welcomeSound=true;   // r382: Welcome setting, sound check box
    MenuPage page=MenuPage::main;
    void RequestMainMenu() {menuOpenPending=true;}
    template<class Emit> void Present(MenuPage next,bool sounds,Emit emit) {
        auto send=[&](MenuCue cue) {if(sounds)emit(cue);};
        const bool opened=menuOpenPending && initialized && next==MenuPage::main;
        menuOpenPending=false;
        if(!initialized) {initialized=true;page=next;if(welcomeSound)send(MenuCue::welcome);return;}
        if(opened)send(MenuCue::select);
        if(page==next)return;
        if(Popup(page))send(MenuCue::popupClose);
        else if(page==MenuPage::confirm)send(MenuCue::dialogClose);
        if(Popup(next))send(MenuCue::popupOpen);
        else if(next==MenuPage::confirm)send(MenuCue::dialogOpen);
        else if(page==MenuPage::main && Submenu(next))send(MenuCue::submenuOpen);
        else if(next==MenuPage::main && Submenu(page))send(MenuCue::submenuClose);
        else if(page==MenuPage::settings && (next==MenuPage::reader || next==MenuPage::detail))send(MenuCue::submenuOpen);
        else if(next==MenuPage::settings && (page==MenuPage::reader || page==MenuPage::detail))send(MenuCue::submenuClose);
        page=next;
    }
};
}
