// SPDX-License-Identifier: GPL-2.0-or-later
// Charon r409: "the voice is resting after a stall of the sound card" (CharonAudioStall.h), as one flag
// that code outside the voice provider may read. The provider of THIS program sets it (Rest) and clears
// it (rest over). When the voice lives in another program (the resident reader), the flag stays false
// and a wait for speech simply runs to its own bound. A wait for speech reads it to end early: nothing
// will be heard while the stream rests, and the dialogue must not look hung.
#pragma once
namespace hakefi {
inline bool& VoiceResting() {static bool resting=false;return resting;}
}  // namespace hakefi
