// Charon resident reader: CONTROL READER for graphical screens (r410, the user's answer 8). GPL-2.0-or-later.
// Included INSIDE class ResidentReader, after CharonResidentModel.inc.
//
// "Like the pc reader, because every boot manager, even a graphical one, must be accessible": the picture of the
// screen is analysed by CharonPixelControls.h (icons and tiles as list items, buttons, fields, sliders, tabs, check
// boxes, the selection shown by a frame or a plate), labels come from what the reader's own OCR read near each
// control, and the user walks the controls with the element keys. On a screen that only a mouse can use the reader
// itself moves the program's pointer and clicks.
//   Insert+comma  / Ctrl+Alt+4   what is on the screen: the selected control, then how many controls (twice: all)
//   Insert+F5, F6 / Ctrl+Alt+[ ] previous / next control (when the screen has no text elements)
//   Insert+Enter  / Ctrl+Alt+2   click the control the reader stands on
// Honest limits, said in speech: a control the layer is not sure of (confidence under 60) is not offered as a fact;
// with nothing sure the reader falls back to the layout description (DescribeGraphics); a label is only ever text
// that was read on the screen - otherwise "icon 2 of 4".
//
// The pointer (work/r410-evidence/web/pointer-research.txt): programs call EFI_SIMPLE_POINTER_PROTOCOL.GetState
// through the protocol structure every time (rEFInd pointer.c, Clover XPointer.cpp, OpenCore AIM.c, edk2
// ConSplitter), so the function pointer is replaced in place, as the reader does with ReadKeyStroke; a program
// that sleeps on WaitForInput is woken by signalling that event (edk2 lets any caller signal a wait event). An
// injected report is handed to ONE caller only (the splitter's and the device's instance are both polled).
// The pointer is walked in a closed loop: push it into the top left corner, move, find it in the picture
// (pixelcontrols::FindPointer on two frames), learn pixels per count, repeat until it stands on the control; only
// then the click (press, release). When the pointer cannot be seen the reader says so and does NOT click.
struct PointerMode {uint64_t resolutionX,resolutionY,resolutionZ;bool left,right;};
struct PointerState {int32_t x,y,z;bool left,right;};
struct SimplePointer {
    efi_status (EFIAPI *Reset)(SimplePointer*,bool);
    efi_status (EFIAPI *GetState)(SimplePointer*,PointerState*);
    efi_event waitForInput;PointerMode* mode;
};
using PointerGetFn=efi_status (EFIAPI *)(SimplePointer*,PointerState*);
struct PointerHook {SimplePointer* pointer;PointerGetFn original;};
static constexpr size_t maxPointerHooks=8;
PointerHook pointerHooks_[maxPointerHooks]={};size_t pointerHookCount_=0;
struct PointerReport {int32_t x,y;bool left;};
PointerReport pointerQueue_[32]={};volatile uint8_t pointerHead_=0,pointerTail_=0;
uint32_t pointerReads_=0;
static efi_status EFIAPI HookPointerState(SimplePointer* self,PointerState* state) {
    auto* r=active_;PointerGetFn original=nullptr;
    if(r)for(size_t i=0;i<r->pointerHookCount_;++i)if(r->pointerHooks_[i].pointer==self)original=r->pointerHooks_[i].original;
    if(!r || !original)return EFI_DEVICE_ERROR;
    ++r->pointerReads_;
    if(state && r->pointerHead_!=r->pointerTail_) {
        const PointerReport report=r->pointerQueue_[r->pointerHead_];r->pointerHead_=uint8_t((r->pointerHead_+1)%32);
        // The reader counts in the device's own unit (Mode->Resolution counts): programs divide a movement by the
        // resolution before they turn it into pixels (rEFInd: x * speed / ResolutionX; seen with the test program:
        // 4000 raw counts at a resolution of 65536 moved nothing).
        const int64_t ux=self->mode && self->mode->resolutionX?int64_t(self->mode->resolutionX):1,uy=self->mode && self->mode->resolutionY?int64_t(self->mode->resolutionY):1;
        auto fit=[](int64_t v) {return int32_t(v>0x7fffffff?0x7fffffff:v<-0x7fffffff?-0x7fffffff:v);};
        state->x=fit(report.x*ux);state->y=fit(report.y*uy);state->z=0;state->left=report.left;state->right=false;
        r->pointerHeld_=report.left;
        // more reports wait: a program that sleeps on WaitForInput is woken again (an event is signalled once,
        // however often it was signalled; seen with the waiting variant of the test program)
        if(r->pointerHead_!=r->pointerTail_ && self->waitForInput)r->boot_->SignalEvent(self->waitForInput);
        return EFI_SUCCESS;
    }
    const efi_status status=original(self,state);
    // while the reader holds the button (between its press and release) a device report must not let go of it
    if(status==EFI_SUCCESS && state && r->pointerHeld_)state->left=true;
    return status;
}
bool pointerHeld_=false;
size_t HookPointers() {
    if(pointerHookCount_ || !boot_->LocateHandleBuffer)return pointerHookCount_;
    efi_guid guid={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
    size_t count=0;efi_handle* handles=nullptr;
    if(boot_->LocateHandleBuffer(ByProtocol,&guid,nullptr,&count,&handles)!=EFI_SUCCESS || !handles)return 0;
    for(size_t i=0;i<count && pointerHookCount_<maxPointerHooks;++i) {
        SimplePointer* p=nullptr;
        if(boot_->HandleProtocol(handles[i],&guid,reinterpret_cast<void**>(&p))!=EFI_SUCCESS || !p || !p->GetState || p->GetState==HookPointerState)continue;
        bool known=false;for(size_t k=0;k<pointerHookCount_;++k)if(pointerHooks_[k].pointer==p)known=true;
        if(known)continue;
        pointerHooks_[pointerHookCount_++]={p,p->GetState};p->GetState=HookPointerState;
    }
    boot_->FreePool(handles);
#ifdef HAK_CHARON_TRACE
    charontrace::Value("CHARON pointer instances wrapped",pointerHookCount_);
#endif
    return pointerHookCount_;
}
void UnhookPointers() {
    for(size_t i=0;i<pointerHookCount_;++i)if(pointerHooks_[i].pointer && pointerHooks_[i].pointer->GetState==HookPointerState)pointerHooks_[i].pointer->GetState=pointerHooks_[i].original;
    pointerHookCount_=0;pointerHead_=pointerTail_=0;pointerHeld_=false;
}
void PushPointer(int32_t x,int32_t y,bool left) {
    const uint8_t next=uint8_t((pointerTail_+1)%32);if(next==pointerHead_)return;
    pointerQueue_[pointerTail_]={x,y,left};pointerTail_=next;
    for(size_t i=0;i<pointerHookCount_;++i)if(pointerHooks_[i].pointer->waitForInput)boot_->SignalEvent(pointerHooks_[i].pointer->waitForInput);
}
bool PointerQueueEmpty() const {return pointerHead_==pointerTail_;}

// ---- the picture and its controls -------------------------------------------
pixelcontrols::Workspace* pcWork_=nullptr;pixelcontrols::Result* pcResult_=nullptr;
uint32_t* pcFrame_=nullptr;uint32_t* pcBefore_=nullptr;size_t pcWidth_=0,pcHeight_=0;
int pcAt_=-1;uint64_t pcTick_=0;bool pcValid_=false;
static constexpr size_t maxControlLines=96;
bool ControlBuffers(size_t width,size_t height) {
    if(!pcWork_) {
        if(boot_->AllocatePool(EfiBootServicesData,sizeof(pixelcontrols::Workspace),(void**)&pcWork_)!=EFI_SUCCESS || !pcWork_)return false;
        if(boot_->AllocatePool(EfiBootServicesData,sizeof(pixelcontrols::Result),(void**)&pcResult_)!=EFI_SUCCESS || !pcResult_)return false;
    }
    if(pcFrame_ && (pcWidth_!=width || pcHeight_!=height)) {boot_->FreePool(pcFrame_);pcFrame_=nullptr;pcBefore_=nullptr;}
    if(!pcFrame_) {
        if(width*height>3840u*2160u || boot_->AllocatePool(EfiBootServicesData,width*height*8,(void**)&pcFrame_)!=EFI_SUCCESS || !pcFrame_) {pcFrame_=nullptr;return false;}
        pcBefore_=pcFrame_+width*height;pcWidth_=width;pcHeight_=height;
    }
    return true;
}
// The screen as it is now into pcFrame_ (what was there goes to pcBefore_ when `keep`).
bool ControlGrab(bool keep) {
    Gop* gop=DeviceGop();const GopBltFn blt=gop?OriginalBlt(gop):nullptr;
    if(!gop || !blt || !gop->Mode || !gop->Mode->info)return false;
    const size_t width=gop->Mode->info->horizontal,height=gop->Mode->info->vertical;
    if(width<320 || height<200 || !ControlBuffers(width,height))return false;
    if(keep)for(size_t i=0;i<width*height;++i)pcBefore_[i]=pcFrame_[i];
    if(barShown_)BarRestore();   // the reader's own bar is no control
    if(blt(gop,pcFrame_,bltVideoToBuffer,0,0,0,0,width,height,width*4)!=EFI_SUCCESS)return false;
    for(size_t i=0;i<width*height;++i)pcFrame_[i]&=0xffffff;
    return true;
}
bool ControlsAnalyse() {
    pcValid_=false;
    if(!ControlGrab(false))return false;
    // what the reader's OCR read, as lines with their place: OCR cells are 8 x 16 pixels (RecognizeArea)
    static pixelcontrols::TextLine lines[maxControlLines];static char16_t words[maxControlLines][64];size_t count=0;
    for(size_t r=0;r<rows_ && r<maxRows && count<maxControlLines;++r) {
        size_t c=0;
        while(c<columns_ && count<maxControlLines) {
            while(c<columns_ && (text_[r][c]==u' ' || !text_[r][c]))++c;
            if(c>=columns_)break;
            size_t e=c,gap=0,last=c;
            while(e<columns_ && gap<3) {if(text_[r][e]==u' ' || !text_[r][e])++gap;else {gap=0;last=e;}++e;}
            size_t n=0;for(size_t k=c;k<=last && n<63;++k)words[count][n++]=text_[r][k]?text_[r][k]:u' ';
            words[count][n]=0;
            lines[count]={int32_t(c*8),int32_t(r*16),int32_t((last+1-c)*8),16,words[count]};++count;
            c=last+1;
        }
    }
    const pixelcontrols::Frame frame{pcFrame_,uint32_t(pcWidth_),uint32_t(pcHeight_),uint32_t(pcWidth_)};
    pixelcontrols::Analyse(frame,lines,count,*pcWork_,*pcResult_);
    pcValid_=true;pcTick_=tick_;
#ifdef HAK_CHARON_TRACE
    {uint64_t sure=0;for(size_t i=0;i<pcResult_->count;++i)if(pcResult_->controls[i].confidence>=pixelcontrols::confident)++sure;
     charontrace::Value("CHARON controls: lines, found, sure, focused",(uint64_t(count)<<48)|(uint64_t(pcResult_->count)<<32)|(sure<<16)|uint16_t(pcResult_->focused));}
#endif
    return true;
}
size_t SureControls() const {size_t n=0;if(pcValid_)for(size_t i=0;i<pcResult_->count;++i)if(pcResult_->controls[i].confidence>=pixelcontrols::confident)++n;return n;}
// r410 (user, third set G3): an icon without readable text gets the name of the configuration's entry at the same
// place, with the caveat, only when the configuration lists exactly as many entries as there are icons in this row;
// otherwise the position alone. Null: no such name.
const char16_t* ProbableName(const pixelcontrols::Control& c) const {
    using pixelcontrols::Role;
    if(c.label[0] || !(c.role==Role::iconItem || c.role==Role::tile) || c.count<2 || c.index<1 || c.index>c.count)return nullptr;
    size_t listed=0,at=SIZE_MAX;
    for(size_t i=0;i<entryInfoCount_;++i) {const auto& e=entryInfo_[i];if(e.depth || e.disabled || !e.title[0])continue;++listed;if(listed==c.index)at=i;}
    return listed==c.count && at!=SIZE_MAX?entryInfo_[at].title:nullptr;
}
// "label, role, state, N of M" of one control. Only what the layer is sure of.
void ControlWords(const pixelcontrols::Control& c,NavOut& o) {
    using pixelcontrols::Role;
    const bool pl=options_.polish;
    static const char16_t* const roleEn[]={u"element",u"icon",u"tile",u"button",u"edit field",u"slider",u"tab",u"check box",u"list item",u"pointer",u"picture"};
    static const char16_t* const rolePl[]={u"element",u"ikona",u"kafelek",u"przycisk",u"pole edycji",u"suwak",u"karta",u"pole wyboru",u"pozycja listy",u"wska\x017anik",u"obraz"};
    const size_t role=size_t(c.role)<11?size_t(c.role):0;
    if(c.label[0]) {o.S(c.label);o.Own(u", ");}
    else if(const char16_t* probable=ProbableName(c)) {
        o.Own(pl?u"prawdopodobnie ":u"probably ");o.S(probable);o.Own(u", ");
#ifdef HAK_CHARON_TRACE
        charontrace::Value("CHARON control named from the configuration, index",c.index);
#endif
    }
    o.Own((pl?rolePl:roleEn)[role]);
    if(c.role==Role::field && c.value[0]) {o.Own(u": ");o.S(c.value);}
    char16_t words[160];NavOut w{words,160,0};words[0]=0;
    if(c.role==Role::slider && c.percent!=pixelcontrols::noPercent) {w.S(u", ");w.N(c.percent);w.S(pl?u" procent":u" percent");}
    if(c.stateConfidence>=pixelcontrols::confident) {
        if(c.state&pixelcontrols::stateChecked)w.S(pl?u", zaznaczone":u", checked");
        else if(c.role==Role::checkbox)w.S(pl?u", niezaznaczone":u", not checked");
        if(c.state&(pixelcontrols::stateSelected|pixelcontrols::stateFocused))w.S(pl?u", wybrane":u", selected");
        if(c.state&pixelcontrols::stateDisabled)w.S(pl?u", niedost\x0119pne":u", unavailable");
    }
    if(c.count>=2 && c.index) {w.S(u", ");w.N(c.index);w.S(pl?u" z ":u" of ");w.N(c.count);}
    if(words[0])o.Own(words);
}
void SayControl(int index,const char16_t* head=nullptr) {
    if(!pcValid_ || index<0 || index>=int(pcResult_->count))return;
    static char16_t text[500];NavOut o{text,500,0};text[0]=0;
    if(head)o.Own(head);
    ControlWords(pcResult_->controls[index],o);
#ifdef HAK_CHARON_TRACE
    {const auto& c=pcResult_->controls[index];charontrace::Value("CHARON control said: index, role, confidence, x, y",(uint64_t(index)<<56)|(uint64_t(c.role)<<48)|(uint64_t(c.confidence)<<40)|(uint64_t(uint16_t(c.rect.x))<<16)|uint16_t(c.rect.y));}
#endif
    Speak(text);
}
// What is on this graphical screen. detailed: every control the layer is sure of.
void SayControls(bool detailed) {
    const bool pl=options_.polish;
    if(!ControlsAnalyse()) {SayOwn(pl?u"Nie mog\x0119 pobra\x0107 obrazu ekranu.":u"I cannot get the picture of the screen.");return;}
    const size_t sure=SureControls();
    static char16_t text[1500];NavOut o{text,1500,0};text[0]=0;
    if(!sure) {
        // nothing certain: say so, then the layout description as before
        o.Own(pl?u"Nie rozpoznaj\x0119 na tym ekranie \x017c" u"adnej kontrolki z pewno\x015b" u"ci\x0105. ":u"I recognise no control on this screen with certainty. ");
        static char16_t shape[300];shape[0]=0;if(DescribeGraphics(shape,300)) {size_t k=0;while(shape[k])++k;if(o.at+k+1<1500) {for(size_t i=0;i<k;++i)text[o.at++]=shape[i];text[o.at]=0;}}
        Speak(text);return;
    }
    if(pcResult_->focused>=0 && pcResult_->controls[pcResult_->focused].confidence>=pixelcontrols::confident) {pcAt_=pcResult_->focused;ControlWords(pcResult_->controls[pcAt_],o);o.Own(u". ");}
    {char16_t n[80];NavOut w{n,80,0};w.N(sure);w.S(pl?(sure==1?u" kontrolka na ekranie":u" kontrolek na ekranie"):(sure==1?u" control on the screen":u" controls on the screen"));
     if(pcResult_->count>sure) {w.S(pl?u", niepewnych: ":u", not sure: ");w.N(pcResult_->count-sure);}
     w.S(u". ");o.Own(n);}
    if(detailed) {
        uint8_t order[pixelcontrols::maxControls];const size_t n=pixelcontrols::ReadingOrder(*pcResult_,order,pixelcontrols::maxControls);
        for(size_t i=0;i<n;++i) {const auto& c=pcResult_->controls[order[i]];if(c.confidence<pixelcontrols::confident)continue;ControlWords(c,o);o.Own(u". ");}
    } else if(Item(formatting::itemHints))o.Own(pl?u"Insert F6 i F5 przechodz\x0105 po kontrolkach, Insert Enter klika.":u"Insert F6 and F5 walk the controls, Insert Enter clicks.");
    Speak(text);
}
// The next / previous control the layer is sure of. False: this screen has none (the caller uses text elements).
bool StepControl(bool forward) {
    if(!GraphicalNow())return false;
    {BuildModel();const screenmodel::View v=ModelView();if(model_.list.found && model_.list.sure && !v.iconCount)return false;}   // a text menu drawn as a picture: its text elements
    if(!pcValid_ || tick_-pcTick_>300) {const int keep=pcAt_;if(!ControlsAnalyse())return false;pcAt_=keep<int(pcResult_->count)?keep:-1;}
    if(!SureControls())return false;
    const bool pl=options_.polish;
    int at=pcAt_;
    for(unsigned guard=0;guard<pixelcontrols::maxControls;++guard) {
        at=pixelcontrols::Step(*pcResult_,at,forward?1:-1);
        if(at<0)break;
        if(pcResult_->controls[at].confidence>=pixelcontrols::confident)break;
    }
    if(at<0) {SayOwn(forward?(pl?u"Ostatnia kontrolka":u"Last control"):(pl?u"Pierwsza kontrolka":u"First control"));Raise(forward?formatting::Event::listLast:formatting::Event::listFirst);return true;}
    pcAt_=at;SayControl(at);return true;
}
// Is the screen a picture now (no console text of the program on it)?
bool GraphicalNow() const {
    // the program's last drawing was pixels (ObserveBlt), not console text (Capture)
    return lastBlt_ && lastBlt_>=consoleCapture_;
}
// ---- walking the pointer and clicking ---------------------------------------
enum : uint8_t {walkIdle=0,walkHome,walkProbe,walkMove,walkPress,walkRelease,walkDone};
uint8_t walkState_=walkIdle,walkTries_=0;uint64_t walkTick_=0;int walkTarget_=-1;
pixelcontrols::PointerWalk walkScale_{};pixelcontrols::Point walkAt_{0,0};int32_t walkSentX_=0,walkSentY_=0;bool walkKnown_=false,walkClick_=false;
uint32_t walkReads_=0,walkSeen_=0;
void StartClick(bool click) {
    const bool pl=options_.polish;
    if(walkState_!=walkIdle) {SayOwn(pl?u"Jeszcze przesuwam wska\x017anik.":u"I am still moving the pointer.");return;}
    if(!pcValid_ || pcAt_<0 || pcAt_>=int(pcResult_->count)) {SayOwn(pl?u"Najpierw wybierz kontrolk\x0119: Insert F6 albo Insert przecinek.":u"Choose a control first: Insert F6 or Insert comma.");return;}
    if(!HookPointers()) {SayOwn(pl?u"To firmware nie udost\x0119pnia programom wska\x017anika, wi\x0119" u"c nie mog\x0119 klikn\x0105\x0107.":u"This firmware offers programs no pointer, so I cannot click.");return;}
    walkTarget_=pcAt_;walkClick_=click;walkTries_=0;walkKnown_=false;walkReads_=pointerReads_;walkSeen_=0;
    // into the top left corner first: wherever the program's pointer is, it ends at 0,0 (programs clamp)
    for(int i=0;i<8;++i)PushPointer(-400,-400,false);
    walkState_=walkHome;walkTick_=tick_;
#ifdef HAK_CHARON_TRACE
    charontrace::Value("CHARON pointer walk starts for control",uint64_t(walkTarget_));
#endif
}
void WalkFail(const char16_t* en,const char16_t* plText) {
    walkState_=walkIdle;pointerHead_=pointerTail_=0;pointerHeld_=false;
#ifdef HAK_CHARON_TRACE
    charontrace::Value("CHARON pointer walk failed in try",walkTries_);
#endif
    SayOwn(options_.polish?plText:en);
}
// From the timer, every tick while a walk runs. Never waits: it looks whether the program took the reports, gives
// it 150 ms to draw, and decides the next step from the picture.
void WalkWork() {
    if(walkState_==walkIdle)return;
    const bool pl=options_.polish;
    if(tick_-walkTick_>600) {   // 6 s without progress
        WalkFail(pointerReads_==walkReads_?u"This program does not read the pointer, so I cannot click here.":u"The pointer did not get there in time. I did not click.",
                 pointerReads_==walkReads_?u"Ten program nie czyta wska\x017anika, wi\x0119" u"c nie mog\x0119 tu klikn\x0105\x0107.":u"Wska\x017anik nie dotar\x0142 na czas. Nie klikn\x0105\x0142" u"em.");
        return;}
    if(!PointerQueueEmpty()) {
        if(!(tick_%20))for(size_t i=0;i<pointerHookCount_;++i)if(pointerHooks_[i].pointer->waitForInput)boot_->SignalEvent(pointerHooks_[i].pointer->waitForInput);   // every 200 ms: wake a sleeping program
        walkSettle_=tick_;return;}
    if(tick_-walkSettle_<15)return;
    if(walkTarget_<0 || walkTarget_>=int(pcResult_->count)) {walkState_=walkIdle;return;}
    const auto& control=pcResult_->controls[walkTarget_];
    const pixelcontrols::Point target=pixelcontrols::ClickPoint(control);
    auto send=[&](int32_t cx,int32_t cy) {
        walkSentX_=cx;walkSentY_=cy;
        // in pieces of at most 400 counts, so no program's acceleration or overflow rule is met
        while(cx || cy) {const int32_t sx=cx>400?400:cx<-400?-400:cx,sy=cy>400?400:cy<-400?-400:cy;PushPointer(sx,sy,false);cx-=sx;cy-=sy;}
        walkTick_=tick_;walkSettle_=tick_;
    };
    switch(walkState_) {
        case walkHome:
            if(!ControlGrab(false)) {WalkFail(u"I cannot get the picture of the screen.",u"Nie mog\x0119 pobra\x0107 obrazu ekranu.");return;}
            walkAt_={0,0};send(walkScale_.scaleX>0?0:12,walkScale_.scaleY>0?0:12);
            if(!walkSentX_ && !walkSentY_) {   // the scale is known from an earlier walk: go straight
                int32_t cx=0,cy=0;walkScale_.Next(walkAt_,target,12,400,cx,cy);send(cx,cy);walkState_=walkMove;walkKnown_=true;return;}
            walkState_=walkProbe;return;
        case walkProbe:case walkMove: {
            if(!ControlGrab(true)) {WalkFail(u"I cannot get the picture of the screen.",u"Nie mog\x0119 pobra\x0107 obrazu ekranu.");return;}
            const pixelcontrols::Frame before{pcBefore_,uint32_t(pcWidth_),uint32_t(pcHeight_),uint32_t(pcWidth_)},after{pcFrame_,uint32_t(pcWidth_),uint32_t(pcHeight_),uint32_t(pcWidth_)};
            pixelcontrols::PointerFind found{};const pixelcontrols::Point direction{walkSentX_,walkSentY_};
            const bool seen=pixelcontrols::FindPointer(before,after,*pcWork_,found,&direction) && found.found && found.confidence>=50;
#ifdef HAK_CHARON_TRACE
            charontrace::Value("CHARON pointer seen, confidence, x, y",(uint64_t(seen)<<56)|(uint64_t(found.confidence)<<48)|(uint64_t(uint16_t(found.x))<<16)|uint16_t(found.y));
#endif
            if(!seen) {
                // r410 (session 4, G4): a pointer that was followed twice and then is not found has most likely
                // reached the control - the program highlights what is under it, and the highlight hides the
                // small change the pointer makes (Clover, OpenCanopy). With the scale measured on both axes the
                // place it must be at is known; when that place is on the control, the click goes there.
                if(walkState_==walkMove && walkKnown_ && walkScale_.scaleX>0 && walkScale_.scaleY>0 && walkSeen_>=2) {
                    const pixelcontrols::Point guess{walkAt_.x+int32_t(int64_t(walkSentX_)*walkScale_.scaleX/1024),walkAt_.y+int32_t(int64_t(walkSentY_)*walkScale_.scaleY/1024)};
                    if(pixelcontrols::On(control,guess)) {
#ifdef HAK_CHARON_TRACE
                        charontrace::Value("CHARON pointer not seen on the control, place by scale x, y",(uint64_t(uint32_t(guess.x))<<32)|uint32_t(guess.y));
#endif
                        walkAt_=guess;
                        if(!walkClick_) {walkState_=walkIdle;SayControl(walkTarget_,pl?u"Wska\x017anik na: ":u"Pointer on: ");return;}
                        PushPointer(0,0,true);walkState_=walkPress;walkTick_=tick_;walkSettle_=tick_;return;
                    }
                }
                if(++walkTries_>=4) {WalkFail(u"I cannot see the pointer on this screen, so I do not click.",u"Nie widz\x0119 wska\x017anika na tym ekranie, wi\x0119" u"c nie klikam.");return;}
                send(walkSentX_?walkSentX_:12,walkSentY_?walkSentY_:12);return;   // the same push again
            }
            ++walkSeen_;
            const pixelcontrols::Point now{found.x,found.y};
            walkScale_.Learn(walkSentX_,walkSentY_,now.x-walkAt_.x,now.y-walkAt_.y);
            walkAt_=now;walkKnown_=true;
            if(pixelcontrols::On(control,now)) {
                if(!walkClick_) {walkState_=walkIdle;SayControl(walkTarget_,pl?u"Wska\x017anik na: ":u"Pointer on: ");return;}
                PushPointer(0,0,true);walkState_=walkPress;walkTick_=tick_;walkSettle_=tick_;return;
            }
            if(++walkTries_>=12) {WalkFail(u"The pointer does not reach the control. I did not click.",u"Wska\x017anik nie trafia w kontrolk\x0119. Nie klikn\x0105\x0142" u"em.");return;}
            int32_t cx=0,cy=0;walkScale_.Next(now,target,12,300,cx,cy);
            if(!cx && !cy) {cx=target.x>now.x?1:target.x<now.x?-1:0;cy=target.y>now.y?1:target.y<now.y?-1:0;}
            send(cx,cy);walkState_=walkMove;return;}
        case walkPress:PushPointer(0,0,false);walkState_=walkRelease;walkTick_=tick_;walkSettle_=tick_;return;
        case walkRelease: {
            walkState_=walkIdle;pointerHeld_=false;
#ifdef HAK_CHARON_TRACE
            charontrace::Value("CHARON pointer clicked at x, y",(uint64_t(uint32_t(walkAt_.x))<<32)|uint32_t(walkAt_.y));
#endif
            static char16_t text[300];NavOut o{text,300,0};text[0]=0;
            o.Own(pl?u"Klikni\x0119to: ":u"Clicked: ");
            ControlWords(control,o);   // the same words as when it was walked to
            Speak(text);
            pcValid_=false;   // the screen may have changed
            return;}
        default:walkState_=walkIdle;return;
    }
}
uint64_t walkSettle_=0;
// Where the pointer is: what the last walk saw, and the control under it.
void SayPointer() {
    const bool pl=options_.polish;
    static char16_t text[400];NavOut o{text,400,0};text[0]=0;
    if(!walkKnown_) {o.Own(pl?u"Nie wiem jeszcze, gdzie jest wska\x017anik: ten program pokazuje go dopiero, gdy si\x0119 poruszy. ":u"I do not know yet where the pointer is: it shows only once it has moved. ");Speak(text);return;}
    char16_t n[120];NavOut w{n,120,0};
    w.S(pl?u"Wska\x017anik: ":u"Pointer: ");w.N(pcWidth_?size_t(walkAt_.x)*100/pcWidth_:0);w.S(pl?u" procent od lewej, ":u" percent from the left, ");w.N(pcHeight_?size_t(walkAt_.y)*100/pcHeight_:0);w.S(pl?u" procent od g\x00f3ry. ":u" percent from the top. ");
    o.Own(n);
    if(pcValid_)for(size_t i=0;i<pcResult_->count;++i)if(pcResult_->controls[i].confidence>=pixelcontrols::confident && pixelcontrols::On(pcResult_->controls[i],walkAt_)) {ControlWords(pcResult_->controls[i],o);break;}
    Speak(text);
}
// r410 (user, fourth set H2): Insert+Enter on a control whose name was READ FROM THE SCREEN clicks at once. A name
// taken from the configuration ("probably NAME") or no name at all is asked about first: "Start NAME? Enter
// confirms." The next key answers: Enter clicks, any other key cancels with a word. The answering key never
// reaches the program (FilterKey), and a question nobody answers is dropped after 20 s with the same word.
static constexpr char16_t clickYesCommand=0x15,clickNoCommand=0x16;
bool clickAsk_=false;uint64_t clickAskTick_=0;int clickAskAt_=-1;
void AskOrClick() {
    const bool pl=options_.polish;
    if(walkState_!=walkIdle || !pcValid_ || pcAt_<0 || pcAt_>=int(pcResult_->count)) {StartClick(true);return;}   // its own words
    const auto& c=pcResult_->controls[pcAt_];
    if(c.label[0]) {StartClick(true);return;}
    using pixelcontrols::Role;
    static const char16_t* const roleEn[]={u"element",u"icon",u"tile",u"button",u"edit field",u"slider",u"tab",u"check box",u"list item",u"pointer",u"picture"};
    static const char16_t* const rolePl[]={u"element",u"ikona",u"kafelek",u"przycisk",u"pole edycji",u"suwak",u"karta",u"pole wyboru",u"pozycja listy",u"wska\x017anik",u"obraz"};
    const size_t role=size_t(c.role)<11?size_t(c.role):0;
    const bool starts=c.role==Role::iconItem || c.role==Role::tile;
    static char16_t text[300];NavOut o{text,300,0};text[0]=0;
    o.Own(starts?(pl?u"Uruchomi\x0107: ":u"Start: "):(pl?u"Klikn\x0105\x0107: ":u"Click: "));
    if(const char16_t* probable=ProbableName(c)) {o.Own(pl?u"prawdopodobnie ":u"probably ");o.S(probable);}
    else {
        char16_t words[80];NavOut w{words,80,0};words[0]=0;
        w.S((pl?rolePl:roleEn)[role]);
        if(c.count>=2 && c.index) {w.S(u" ");w.N(c.index);w.S(pl?u" z ":u" of ");w.N(c.count);}
        o.Own(words);
    }
    o.Own(pl?u"? Enter potwierdza, inny klawisz anuluje.":u"? Enter confirms, any other key cancels.");
    clickAsk_=true;clickAskTick_=tick_;clickAskAt_=pcAt_;
#ifdef HAK_CHARON_TRACE
    charontrace::Value("CHARON click asked for control",uint64_t(pcAt_));
#endif
    Speak(text);
}
// From FilterKey (the program's key read): the answer is only noted; the timer carries it out.
bool ClickAnswerIn(const efi_input_key& key) {
    if(!clickAsk_ || (!key.ScanCode && !key.UnicodeChar))return false;
    command_=(!key.ScanCode && key.UnicodeChar==0x0d)?clickYesCommand:clickNoCommand;
    return true;
}
void ClickAnswer(bool yes) {
    if(!clickAsk_)return;
    clickAsk_=false;
#ifdef HAK_CHARON_TRACE
    charontrace::Value("CHARON click question answered, yes",yes);
#endif
    if(yes && pcValid_ && pcAt_==clickAskAt_) {StartClick(true);return;}
    SayOwn(options_.polish?u"Anulowano, nie klikam.":u"Cancelled, no click.");
}
void ClickAskWork() {if(clickAsk_ && tick_-clickAskTick_>2000)ClickAnswer(false);}
bool ControlCommand(char16_t c) {
    const bool pl=options_.polish;
    switch(c) {
        case u'O':
            if(!GraphicalNow()) {SayOwn(pl?u"To ekran tekstowy: jego elementy podaje lista element\x00f3w, Insert F7.":u"This is a text screen: its elements are in the elements list, Insert F7.");return true;}
            if(pressCount_>=3) {SayPointer();return true;}
            SayControls(pressCount_==2);return true;
        case u'A':
            if(!GraphicalNow()) {SayOwn(pl?u"Klikanie dzia\x0142" u"a tylko na ekranach graficznych.":u"Clicking works on graphical screens only.");return true;}
            AskOrClick();return true;
        default:return false;
    }
}
