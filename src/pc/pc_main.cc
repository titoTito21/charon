// CharonPC - starts Bochs's own main sequence (main.cc bxmain) with the
// configuration generated from CHARONPC.CFG. GPL-2.0-or-later.
#include "bochs.h"
#include "gui/siminterface.h"
#include "pc_platform.h"

int bxmain(void);

extern "C" int pc_bochs_main(void)
{
    static char name[] = "charonpc", quick[] = "-q", file[] = "-f", config[] = "rom:bochsrc";
    static char* argv[] = {name, quick, file, config, nullptr};
    bx_startup_flags.argc = 4;
    bx_startup_flags.argv = argv;
    return bxmain();
}
