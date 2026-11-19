#pragma once
#include "BootMenuVoice.h"
#include "CharonCues.h"
namespace hakefi {
// Does not return until every HII notification has been unregistered. Firmware
// Extract/Route callbacks are synchronous; pumping brackets, not preempts, them.
efi_status RunHiiMenu(efi_system_table*,MenuVoice*,AudioEffects*,bool polish,
    bool sounds,bool explicitMessages);
}
