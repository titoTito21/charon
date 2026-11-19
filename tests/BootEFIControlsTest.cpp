#include "../src/HAKBootEFIControls.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace hakbootefi;
    int reads=0,writes=0;bool writable=true;
    Backend backend;
    backend.volumes=[](std::vector<Volume>& values) {Volume v;v.disk="Disk A";v.label="EFI A";values.push_back(v);return true;};
    backend.targets=[&](Volume&,std::vector<Target>& values) {++reads;values.push_back({"exact-id","Haiku"});return true;};
    backend.choose=[&](const Volume&,const Target& target) {assert(target.identifier=="exact-id");++writes;return writable;};
    Controls control(backend,"pl");
    assert(control.OpensSubmenu());
    assert(Move(0,3,Up,false)==0 && Move(2,3,Down,false)==2);
    assert(Move(0,3,Up,true)==2 && Move(2,3,Down,true)==0);
    assert(control.Say().find("Disk A")<control.Say().find("EFI A"));
    assert(control.Handle(Open)==Stay && control.page==Controls::Targets && reads==1 && writes==0);
    assert(!control.OpensSubmenu());
    assert(control.Handle(Open)==Stay && control.page==Controls::Confirmation && control.current==0 && writes==0);
    control.Handle(Open);assert(control.page==Controls::Targets && writes==0);
    control.Handle(Open);control.Handle(Down);assert(control.Handle(Back)==Stay && writes==0);
    control.Handle(Open);control.Handle(Last);assert(control.Handle(Open)==Restart && writes==1);
    writable=false;assert(control.Handle(Open)==Stay && control.page==Controls::Error && writes==2);
    control.Handle(Back);assert(control.page==Controls::Volumes);assert(control.Handle(Back)==Leave);
    std::vector<uint8_t> snapshot(30,0);
    const char magic[8]={'H','A','K','S','E','T','1',0};
    for(int i=0;i<8;++i)snapshot[i]=magic[i];
    snapshot[10]=1;snapshot[11]=255;snapshot[12]=5;snapshot[14]=3;
    snapshot[26]=1;snapshot[28]=2;
    backend.settings=[&](const Volume&,std::vector<uint8_t>& data){data=snapshot;return true;};
    int settingsWrites=0;
    backend.saveSettings=[&](const Volume&,const std::vector<uint8_t>& data,uint16_t command,uint16_t value){
        assert(data==snapshot);assert(command==hakefi::kTimeout && value==6);++settingsWrites;return true;
    };
    Controls settings(backend,"pl");settings.Handle(Open);settings.Handle(Last);settings.Handle(Open);
    assert(settings.page==Controls::Settings);
    settings.Handle(Open);assert(settings.page==Controls::Timeout);
    settings.Handle(More);settings.Handle(Open);assert(settings.page==Controls::Confirmation);
    settings.Handle(Back);assert(settings.page==Controls::Timeout && settingsWrites==0);
    settings.Handle(Open);settings.Handle(Down);assert(settings.Handle(Open)==Restart && settingsWrites==1);
    Controls order(backend,"en");order.Handle(Open);order.Handle(Last);order.Handle(Open);order.Handle(Down);order.Handle(Open);
    assert(order.page==Controls::Order && order.Count()==2);
    order.Handle(Last);order.Handle(Open);order.Handle(Open);
    assert(order.page==Controls::Confirmation && order.Say().find("Set as default")!=std::string::npos);
    order.Handle(Open);assert(order.page==Controls::OrderAction && settingsWrites==1);
    order.Handle(Back);assert(order.page==Controls::Order && order.current==1);
    backend.secure=[](const Volume&,haksecure::Inventory& inventory) {
        inventory.databases={{"PK",haksecure::missing,{}},{"KEK",7,{}},{"db",0,{{"SHA-256","owner","digest",28}}},{"dbx",0,{}}};return true;
    };
    Controls keys(backend,"pl");keys.Handle(Open);keys.Handle(Last);keys.Handle(Open);keys.Handle(Last);keys.Handle(Open);
    assert(keys.page==Controls::Secure && keys.Count()==5);
    keys.Handle(Down);keys.Handle(Open);assert(keys.page==Controls::Secure && keys.Say().find("Brak bazy")!=std::string::npos);
    keys.Handle(Down);keys.Handle(Open);assert(keys.page==Controls::Secure && keys.Say().find("Błąd")!=std::string::npos);
    keys.Handle(Down);keys.Handle(Open);assert(keys.page==Controls::Keys && keys.Count()==1);
    keys.Handle(Open);assert(keys.page==Controls::KeyDetails && keys.Say().find("digest")!=std::string::npos);
    keys.Handle(Back);keys.Handle(Back);assert(keys.page==Controls::Secure && keys.current==3);
    keys.Handle(Down);keys.Handle(Open);assert(keys.page==Controls::Keys && keys.Count()==0);
    keys.Handle(Open);assert(keys.page==Controls::Keys);
    backend.volumes=[](std::vector<Volume>&){return true;};
    Controls empty(backend,"en");assert(empty.Count()==0 && empty.Handle(Down)==Stay && empty.Handle(Open)==Stay && empty.Handle(Back)==Leave);
    assert(Move(0,0,Down)==0 && Move(0,7,Up)==6 && Move(6,7,Down)==0);
    assert(Move(6,7,PageUp)==1 && Move(3,7,PageDown)==6);
    assert(Move(3,7,First)==0 && Move(0,7,Last)==6);
    puts("PASS spoken EFI list navigation, preserved selection, default cancel, explicit commit, failure and empty list");
}
