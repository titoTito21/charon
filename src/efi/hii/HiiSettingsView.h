#pragma once
#include "HiiDatabase.h"
#include "HiiConfig.h"
namespace charonhii {
enum class ViewPage {Sets,Forms,Questions,EnumChoices,Number,String,Confirm,Stale};
enum class ViewKey {Up,Down,Left,Right,Enter,Space,Back,Erase};
enum class ViewResult {None,Focus,Opened,Closed,Edge,Unavailable,ProposalReady,Stale};
// Read-only UI and proposal staging. Deliberately never calls Config::Commit.
// The caller owns Database/Config and retains them for this view's lifetime.
class SettingsView {
public:
    bool Bind(efi_boot_services*,Database&,Config&,const Database::Identity&,bool polish);
    ViewResult Key(ViewKey);
    bool SetEditText(const char16_t*,size_t units);
    // Intermediate decimal input; Propose validates final bounds and step.
    bool SetEditNumber(uint64_t value);
    size_t Count();size_t Selected()const {return selected_;}
    ViewPage Page()const {return page_;}
    bool RowLabel(size_t row,char16_t* output,size_t capacity);
    bool FocusAnnouncement(char16_t* output,size_t capacity);
    bool Help(char16_t* output,size_t capacity);
    const char* ResolvedLanguage();
    // Non-null only after explicit Yes/Enter, and only while identity is live.
    const ConfigProposal* ConfirmedProposal();
    efi_status ValueStatus()const {return valueStatus_;}
private:
    bool Live();const Model* ModelNow();size_t Record(size_t row)const;
    void FocusValue();void CancelEdit();ViewResult Propose();
    bool Label(uint16_t id,char16_t* output,size_t capacity)const;
    const Question* QuestionNow()const;
    efi_boot_services* boot_=nullptr;Database* database_=nullptr;Config* config_=nullptr;
    Database::Identity identity_{};const Model* model_=nullptr;
    ViewPage page_=ViewPage::Stale;size_t selected_=0,set_=0,form_=0,question_=0,questionRow_=0;
    bool polish_=false,valueKnown_=false,confirmed_=false,proposalReady_=false;
    efi_status valueStatus_=EFI_NOT_READY;
    ConfigValue current_{},edit_{};ConfigProposal proposal_{};
};
}
