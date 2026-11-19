#include "HiiSettingsView.h"
namespace charonhii {
namespace {
bool Copy(char16_t* out,size_t cap,const char16_t* text) {
    if(!out || !cap)return false;
    size_t n=0;while(text[n])++n;
    if(n>=cap) {out[0]=0;return false;}
    for(size_t i=0;i<=n;++i)out[i]=text[i];
    return true;
}
bool Add(char16_t* out,size_t cap,const char16_t* text) {
    size_t at=0;while(at<cap && out[at])++at;
    if(at==cap)return false;
    return Copy(out+at,cap-at,text);
}
bool Number(char16_t* out,size_t cap,uint64_t number) {
    char16_t reversed[21],text[21];size_t n=0;
    do {reversed[n++]=char16_t(u'0'+number%10);number/=10;}while(number);
    for(size_t i=0;i<n;++i)text[i]=reversed[n-i-1];
    text[n]=0;return Add(out,cap,text);
}
const char16_t* Role(Kind kind,bool pl) {
    switch(kind) {
        case Kind::Checkbox:return pl?u"pole wyboru":u"checkbox";
        case Kind::Enum:return pl?u"lista wyboru":u"choice list";
        case Kind::Numeric:return pl?u"pole liczbowe":u"number field";
        case Kind::String:return pl?u"pole tekstowe":u"text field";
        case Kind::Password:return pl?u"chronione hasło":u"protected password";
        case Kind::OrderedList:return pl?u"lista uporządkowana":u"ordered list";
        case Kind::Action:return pl?u"akcja":u"action";
        case Kind::Reference:return pl?u"odnośnik formularza":u"form reference";
        case Kind::Date:return pl?u"data":u"date";
        case Kind::Time:return pl?u"czas":u"time";
    }return u"";
}
}
bool SettingsView::Bind(efi_boot_services* boot,Database& database,Config& config,const Database::Identity& identity,bool polish) {
    if(config_)config_->Reset();
    boot_=boot;database_=&database;config_=&config;identity_=identity;polish_=polish;
    model_=database.View(identity);page_=model_?ViewPage::Sets:ViewPage::Stale;
    selected_=set_=form_=question_=questionRow_=0;confirmed_=proposalReady_=valueKnown_=false;proposal_={};
    config_->Reset();return model_!=nullptr;
}
bool SettingsView::Live() {
    if(!database_ || !(model_=database_->View(identity_))) {
        page_=ViewPage::Stale;selected_=0;confirmed_=proposalReady_=valueKnown_=false;proposal_={};current_={};edit_={};
        if(config_)config_->Reset();
        return false;
    }return true;
}
const Model* SettingsView::ModelNow() {return Live()?model_:nullptr;}
size_t SettingsView::Count() {
    if(!Live())return 1;
    if(page_==ViewPage::Sets)return model_->setCount;
    if(page_==ViewPage::Confirm)return 2;
    if(page_==ViewPage::Number || page_==ViewPage::String)return 1;
    size_t count=0;
    if(page_==ViewPage::Forms) {for(unsigned i=0;i<model_->formCount;++i)if(model_->forms[i].set==set_)++count;}
    else if(page_==ViewPage::Questions) {for(unsigned i=0;i<model_->questionCount;++i)if(model_->questions[i].form==form_)++count;}
    else if(page_==ViewPage::EnumChoices) {for(unsigned i=0;i<model_->optionCount;++i)if(model_->options[i].question==question_)++count;}
    return count;
}
size_t SettingsView::Record(size_t row)const {
    if(page_==ViewPage::Sets)return row;
    size_t at=0;
    if(page_==ViewPage::Forms) {for(unsigned i=0;i<model_->formCount;++i)if(model_->forms[i].set==set_ && at++==row)return i;}
    if(page_==ViewPage::Questions) {for(unsigned i=0;i<model_->questionCount;++i)if(model_->questions[i].form==form_ && at++==row)return i;}
    if(page_==ViewPage::EnumChoices) {for(unsigned i=0;i<model_->optionCount;++i)if(model_->options[i].question==question_ && at++==row)return i;}
    return size_t(-1);
}
const Question* SettingsView::QuestionNow()const {return model_ && question_<model_->questionCount?&model_->questions[question_]:nullptr;}
void SettingsView::FocusValue() {
    valueKnown_=false;current_={};edit_={};confirmed_=proposalReady_=false;proposal_={};config_->Reset();
    const auto index=Record(selected_);if(index>=model_->questionCount)return;
    question_=index;questionRow_=selected_;const auto& question=model_->questions[index];
    if(question.kind==Kind::Password || (question.reasons&(Protected|Conditional|Unsupported|Dynamic|Callback|BoundsUnresolved))) {valueStatus_=EFI_UNSUPPORTED;return;}
    valueStatus_=config_->Bind(boot_,*database_,identity_,uint16_t(index));
    if(valueStatus_==EFI_SUCCESS)valueStatus_=config_->Read(current_);
    valueKnown_=valueStatus_==EFI_SUCCESS;
}
bool SettingsView::Label(uint16_t id,char16_t* out,size_t cap)const {
    uint16_t label[512];const auto required=model_->CopyString(id,label,512);
    if(required>1 && required<=512) {
        if(required>cap || !out || !cap)return false;
        for(size_t i=0;i<required;++i)out[i]=char16_t(label[i]);
        return true;
    }
    const auto* fallback=required==1?(polish_?u"Pozycja bez nazwy #":u"Unnamed item #"):
        (polish_?u"Nierozwiązana etykieta #":u"Unresolved label #");
    return Copy(out,cap,fallback) && Number(out,cap,id);
}
bool SettingsView::RowLabel(size_t row,char16_t* out,size_t cap) {
    if(!Live())return Copy(out,cap,polish_?u"Ustawienia firmware zmieniły się. Odśwież widok.":u"Firmware settings changed. Refresh the view.");
    if(!Count())return Copy(out,cap,polish_?u"Brak pozycji w tym widoku. Escape: wstecz; F5: odśwież.":u"No items in this view. Escape: back; F5: refresh.");
    if(row>=Count())return false;
    if(page_==ViewPage::Confirm)return Copy(out,cap,row?(polish_?u"Potwierdź propozycję zmiany":u"Confirm change proposal"):(polish_?u"Anuluj":u"Cancel"));
    if(page_==ViewPage::Number) {if(!Copy(out,cap,u""))return false;return Number(out,cap,edit_.number);}
    if(page_==ViewPage::String)return Copy(out,cap,edit_.text);
    const auto record=Record(row);
    if(page_==ViewPage::Sets)return Label(model_->sets[record].title,out,cap);
    if(page_==ViewPage::Forms)return Label(model_->forms[record].title,out,cap);
    if(page_==ViewPage::EnumChoices)return Label(model_->options[record].text,out,cap);
    const auto& question=model_->questions[record];
    if(!Label(question.prompt,out,cap) || !Add(out,cap,u", ") || !Add(out,cap,Role(question.kind,polish_)))return false;
    if(question.reasons&ReadOnly)if(!Add(out,cap,polish_?u", tylko odczyt":u", read only"))return false;
    if(question.reasons&Protected)if(!Add(out,cap,polish_?u", treść ukryta":u", content hidden"))return false;
    if(question.reasons&(Conditional|Dynamic|Unsupported|Callback|BoundsUnresolved|NoStorage))
        if(!Add(out,cap,polish_?u", warunki lub obsługa nierozwiązane":u", conditions or support unresolved"))return false;
    if(question.reasons&ResetRequired)if(!Add(out,cap,polish_?u", wymagany restart":u", restart required"))return false;
    return true;
}
bool SettingsView::FocusAnnouncement(char16_t* out,size_t cap) {
    if(!RowLabel(selected_,out,cap))return false;
    if(page_==ViewPage::Confirm) {
        if(!Add(out,cap,polish_?u", proponowana wartość: ":u", proposed value: "))return false;
        if(edit_.kind==Kind::String)return Add(out,cap,edit_.text);
        if(edit_.kind==Kind::Checkbox)return Add(out,cap,edit_.number?(polish_?u"zaznaczone":u"checked"):(polish_?u"niezaznaczone":u"not checked"));
        if(edit_.kind==Kind::Enum)for(unsigned i=0;i<model_->optionCount;++i)
            if(model_->options[i].question==question_ && model_->options[i].value==edit_.number) {
                char16_t label[512];return Label(model_->options[i].text,label,512) && Add(out,cap,label);
            }
        return Number(out,cap,edit_.number);
    }
    if(!Count() || page_!=ViewPage::Questions)return true;
    if(!valueKnown_)return Add(out,cap,polish_?u", wartość niedostępna":u", value unavailable");
    if(!Add(out,cap,u", "))return false;
    switch(current_.kind) {
        case Kind::Checkbox:return Add(out,cap,current_.number?(polish_?u"zaznaczone":u"checked"):(polish_?u"niezaznaczone":u"not checked"));
        case Kind::Numeric:return Number(out,cap,current_.number);
        case Kind::String:return Add(out,cap,current_.text);
        case Kind::Enum: {
            for(unsigned i=0;i<model_->optionCount;++i)if(model_->options[i].question==question_ && model_->options[i].value==current_.number) {
                char16_t label[512];return Label(model_->options[i].text,label,512) && Add(out,cap,label);
            }
            return Add(out,cap,polish_?u"wartość spoza listy":u"value outside list");
        }
        default:return Add(out,cap,polish_?u"wartość niedostępna":u"value unavailable");
    }
}
bool SettingsView::Help(char16_t* out,size_t cap) {
    if(!Live())return RowLabel(0,out,cap);
    if(!Count())return Copy(out,cap,u"");
    if(page_==ViewPage::Sets && selected_<Count())return Label(model_->sets[selected_].help,out,cap);
    if(page_==ViewPage::Questions || page_==ViewPage::EnumChoices || page_==ViewPage::Number || page_==ViewPage::String || page_==ViewPage::Confirm) {
        const auto* question=QuestionNow();if(question)return Label(question->help,out,cap);
    }
    return Copy(out,cap,u"");
}
void SettingsView::CancelEdit() {page_=ViewPage::Questions;selected_=questionRow_;confirmed_=proposalReady_=false;proposal_={};FocusValue();}
ViewResult SettingsView::Propose() {
    valueStatus_=config_->Propose(edit_,proposal_,&current_);
    if(!Live())return ViewResult::Stale;
    if(valueStatus_!=EFI_SUCCESS)return ViewResult::Unavailable;
    proposalReady_=true;confirmed_=false;page_=ViewPage::Confirm;selected_=0;return ViewResult::Opened;
}
ViewResult SettingsView::Key(ViewKey key) {
    if(!Live())return ViewResult::Stale;
    if(key==ViewKey::Back || (key==ViewKey::Left && page_!=ViewPage::Number)) {
        if(page_==ViewPage::Sets)return ViewResult::Closed;
        if(page_==ViewPage::Forms) {page_=ViewPage::Sets;selected_=set_;}
        else if(page_==ViewPage::Questions) {page_=ViewPage::Forms;selected_=0;valueKnown_=false;config_->Reset();}
        else CancelEdit();
        return ViewResult::Closed;
    }
    const auto count=Count();if(!count)return ViewResult::Edge;
    if(page_==ViewPage::Number && (key==ViewKey::Left || key==ViewKey::Right || key==ViewKey::Up || key==ViewKey::Down)) {
        const auto* question=QuestionNow();const uint64_t step=question->step?question->step:1;
        const bool increase=key==ViewKey::Right || key==ViewKey::Up;
        if(increase) {if(edit_.number>question->maximum || step>question->maximum-edit_.number)return ViewResult::Edge;edit_.number+=step;}
        else {if(edit_.number<question->minimum || step>edit_.number-question->minimum)return ViewResult::Edge;edit_.number-=step;}
        return ViewResult::Focus;
    }
    if(page_==ViewPage::String && key==ViewKey::Erase) {
        size_t n=0;while(edit_.text[n])++n;if(!n)return ViewResult::Edge;
        if(n>=2 && edit_.text[n-1]>=0xdc00 && edit_.text[n-1]<=0xdfff)--n;
        edit_.text[n-1]=0;return ViewResult::Focus;
    }
    if(key==ViewKey::Up || key==ViewKey::Down) {
        if(key==ViewKey::Up) {if(!selected_)return ViewResult::Edge;--selected_;}
        else {if(selected_+1==count)return ViewResult::Edge;++selected_;}
        if(page_==ViewPage::Questions)FocusValue();
        return ViewResult::Focus;
    }
    if(key!=ViewKey::Enter && key!=ViewKey::Space && key!=ViewKey::Right)return ViewResult::None;
    if(page_==ViewPage::Sets) {set_=Record(selected_);page_=ViewPage::Forms;selected_=0;return ViewResult::Opened;}
    if(page_==ViewPage::Forms) {form_=Record(selected_);page_=ViewPage::Questions;selected_=0;if(Count())FocusValue();return ViewResult::Opened;}
    if(page_==ViewPage::Confirm) {
        if(key==ViewKey::Right)return ViewResult::None;
        if(!selected_) {CancelEdit();return ViewResult::Closed;}
        confirmed_=proposalReady_;return confirmed_?ViewResult::ProposalReady:ViewResult::Unavailable;
    }
    if(page_==ViewPage::EnumChoices) {
        if(key==ViewKey::Right)return ViewResult::None;
        const auto& option=model_->options[Record(selected_)];if(option.reasons) {valueStatus_=EFI_UNSUPPORTED;return ViewResult::Unavailable;}
        edit_.number=option.value;return Propose();
    }
    if(page_==ViewPage::Number || page_==ViewPage::String)return key==ViewKey::Enter?Propose():ViewResult::None;
    const auto* question=QuestionNow();
    if(!valueKnown_ || !question)return ViewResult::Unavailable;
    if(question->reasons&~uint32_t(ResetRequired)) {
        valueStatus_=(question->reasons&ReadOnly)?EFI_WRITE_PROTECTED:EFI_UNSUPPORTED;return ViewResult::Unavailable;
    }
    edit_=current_;
    if(question->kind==Kind::Checkbox) {if(key==ViewKey::Right)return ViewResult::None;edit_.number=current_.number?0:1;return Propose();}
    if(question->kind==Kind::Numeric) {page_=ViewPage::Number;selected_=0;return ViewResult::Opened;}
    if(question->kind==Kind::String) {page_=ViewPage::String;selected_=0;return ViewResult::Opened;}
    if(question->kind==Kind::Enum) {
        page_=ViewPage::EnumChoices;size_t row=0;
        for(unsigned i=0;i<model_->optionCount;++i)if(model_->options[i].question==question_) {
            if(model_->options[i].value==current_.number) {selected_=row;return ViewResult::Opened;}++row;
        }
        page_=ViewPage::Questions;selected_=questionRow_;valueStatus_=EFI_UNSUPPORTED;return ViewResult::Unavailable;
    }
    return ViewResult::Unavailable;
}
bool SettingsView::SetEditText(const char16_t* text,size_t units) {
    if(!Live() || page_!=ViewPage::String || !text || units>255)return false;
    const auto* question=QuestionNow();if(units>question->maxLength)return false;
    for(size_t i=0;i<units;++i) {
        const auto ch=text[i];if(!ch)return false;
        if(ch>=0xd800 && ch<=0xdbff) {if(++i==units || text[i]<0xdc00 || text[i]>0xdfff)return false;}
        else if(ch>=0xdc00 && ch<=0xdfff)return false;
    }
    for(size_t i=0;i<units;++i)edit_.text[i]=text[i];
    edit_.text[units]=0;return true;
}
bool SettingsView::SetEditNumber(uint64_t value) {
    if(!Live() || page_!=ViewPage::Number)return false;
    edit_.number=value;return true;
}
const char* SettingsView::ResolvedLanguage() {return Live()?database_->SelectedLanguage(identity_):nullptr;}
const ConfigProposal* SettingsView::ConfirmedProposal() {return Live() && proposalReady_ && confirmed_?&proposal_:nullptr;}
}
