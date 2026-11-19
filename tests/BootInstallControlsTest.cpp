#include "../src/HAKBootEFIControls.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace hakbootefi;
    Backend backend;int reviews=0,commits=0,cancels=0,status=0;
    backend.volumes=[](std::vector<Volume>& values){Volume v;v.id=42;v.disk="Test disk";v.label="EFI";values.push_back(v);return true;};
    backend.targets=[](Volume&,std::vector<Target>&){return false;};
    backend.installReview=[&](const Volume& v){assert(v.id==42);++reviews;return true;};
    backend.installCommit=[&](){++commits;return true;};
    backend.installPoll=[&](){return status;};
    backend.installCancelReview=[&](){++cancels;};
    Controls controls(backend,"pl");
    controls.Handle(Open);assert(controls.page==Controls::Targets && controls.Count()==1);
    assert(controls.Say().find("Instalacja HAK")!=std::string::npos);
    controls.Handle(Open);assert(controls.page==Controls::InstallReviewing && reviews==1 && commits==0);
    assert(!controls.Tick());controls.Handle(Back);assert(cancels==1 && controls.page==Controls::Targets);
    controls.Handle(Open);status=1;assert(controls.Tick() && controls.page==Controls::InstallConfirmation && controls.current==0);
    controls.Handle(Open);assert(controls.page==Controls::Targets && commits==0 && cancels==2);
    controls.Handle(Open);controls.Tick();controls.Handle(Last);status=0;controls.Handle(Open);
    assert(controls.page==Controls::Installing && commits==1);
    controls.Handle(Back);controls.Handle(Open);assert(controls.page==Controls::Installing && commits==1);
    assert(!controls.Tick());status=1;assert(controls.Tick() && controls.page==Controls::Installed);
    assert(controls.Say().find("nie został jeszcze aktywowany")!=std::string::npos);
    controls.Handle(Back);controls.Handle(Open);status=-1;assert(controls.Tick() && controls.page==Controls::InstallFailure);
    assert(commits==1);controls.Handle(Back);assert(controls.page==Controls::Targets);
    puts("PASS spoken installation controls: empty ESP, cancel-first review, async completion, no duplicate commit/restart");
}
