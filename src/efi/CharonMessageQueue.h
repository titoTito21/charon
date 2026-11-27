// Boot messages that wait for a quiet screen (r410, session 4).
// GPL-2.0-or-later.
//
// The reader collects a program's new text and speaks it as one message once
// the screen has been quiet for a moment. Two things went wrong with a loader
// that prints for seconds and then clears the screen for its menu (FreeBSD's
// loader on a slow or loaded machine): the early text was still waiting when
// the menu came, so the menu was appended behind it - spoken a minute late,
// or cut off without a word when the buffer was full.
//
// Rules, kept here so that a host test can replay recorded screens:
//   1. Text that waits and whose screen has been cleared is sent at once (it
//      is spoken and kept in the history); what the new screen shows starts a
//      new message.
//   2. Newer text is never dropped for lack of room: the waiting text goes out
//      first; when it cannot be spoken now it is kept in the history instead.
//   3. (user, fourth set H4) What happens to waiting text whose screen was
//      cleared depends on the profile. A profile that reads everything in
//      order (beginner) and every error or warning: rule 1, at once. The
//      others: the text waits for what replaces it - new screen text (a menu)
//      is read at once and the vanished text is filed in the history only;
//      when nothing replaces it and the screen falls quiet, it is spoken.
// No firmware dependency.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hakefi::messagequeue {
enum class Step : uint8_t {
    append,      // the waiting text is still on the screen and both fit: one message
    sendFirst    // speak (or, failing that, file in the history) the waiting text, then start anew
};
// separatorRoom: the characters the reader puts between two parts (". " with its marks).
constexpr size_t separatorRoom=4;
inline size_t Length(const char16_t* text) {size_t n=0;if(text)while(text[n])++n;return n;}
// `waitingGone`: the screen was cleared after the waiting text was collected.
inline Step Next(const char16_t* waiting,size_t capacity,const char16_t* fresh,bool waitingGone) {
    const size_t have=Length(waiting);
    if(!have)return Step::append;
    if(waitingGone)return Step::sendFirst;
    return have+separatorRoom+Length(fresh)+1>capacity?Step::sendFirst:Step::append;
}
// Rule 3. `everything`: the profile reads everything in order; `alarm`: the waiting text is an error or a warning.
enum class Vanished : uint8_t {
    speak,       // spoken (and kept in the history) before the newer text
    historyOnly  // kept in the history, marked as a message; the newer text is read at once
};
// Is waiting text sent the moment its screen is cleared, without waiting for what replaces it?
inline bool SendWhenCleared(bool everything,bool alarm) {return everything || alarm;}
// Newer text has arrived for a screen that replaced the waiting text.
inline Vanished Replaced(bool everything,bool alarm) {return everything || alarm?Vanished::speak:Vanished::historyOnly;}
}  // namespace hakefi::messagequeue
