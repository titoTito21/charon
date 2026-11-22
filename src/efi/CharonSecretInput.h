// Secret typed input in a started program (r407: VeraCrypt). GPL-2.0-or-later.
//
// Some programs take a password on a plain console line: VeraCrypt's loader
// (DcsInt) prints "Password:" and then echoes every accepted key as '*', or
// as the key itself after F5 / with AuthorizeVisible=1. The reader must never
// say, braille, log or copy such characters by itself.
//
// For a program whose profile says `secretInput` the reader keeps a mask
// beside its screen model:
//  - an entry begins where the caret is when the program reads a key; the
//    text left of the caret on that row is the prompt;
//  - the entry is secret unless the prompt is known to ask for something
//    public (a menu letter, yes/no, a number with a default, the hash
//    choice). An unknown prompt is secret: a user's own PasswordMsg has no
//    fixed wording;
//  - every character the program writes into a secret entry is kept in the
//    screen model as '*'; the real character goes to `clear`, which only an
//    explicit review command reads (never automatic speech, braille, the
//    speech history, "what changed" or the clipboard);
//  - the program's own short notices at the caret (DCS: " TPM locked!") are
//    public and stay readable.
// Pure logic, no firmware calls: tests/CharonSecretInputTests.cpp.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace hakefi::secretinput {
inline char16_t Lower(char16_t c) {return c>=u'A' && c<=u'Z'?char16_t(c+32):c==0x0141?char16_t(0x0142):c;}
inline bool Contains(const char16_t* text,size_t n,const char16_t* word) {
    size_t w=0;while(word[w])++w;
    for(size_t i=0;i+w<=n;++i) {size_t k=0;while(k<w && Lower(text[i+k])==word[k])++k;if(k==w)return true;}
    return false;
}
// True when the prompt (the text left of the caret, `n` cells) asks for
// input that may be echoed and read aloud.
inline bool OpenPrompt(const char16_t* prompt,size_t n) {
    while(n && (prompt[n-1]==u' ' || !prompt[n-1]))--n;
    size_t from=0;while(from<n && (prompt[from]==u' ' || !prompt[from]))++from;
    prompt+=from;n-=from;
    if(!n)return false;
    // A word for a secret always wins ("[default] New password:").
    static const char16_t* const secret[]={u"pass",u"pim",u"pin",u"hasł",u"secret",u"key",u"token",u"phrase"};
    for(const auto* word:secret)if(Contains(prompt,n,word))return false;
    if(n>=2 && prompt[n-2]==u']' && prompt[n-1]==u':')return true;   // DCS menu: "[brn]:"
    if(prompt[n-1]==u'?')return true;                                   // "True crypt mode [N]?"
    if(n>=3 && prompt[n-1]==u']' && prompt[n-3]==u'[')return true;      // "Do you want to continue? [N]"
    if(prompt[0]==u'[')return true;                                     // "[0] Select EFI boot volume:" (a value with its default)
    if(Contains(prompt,n,u"hash"))return true;                         // "(0) TEST ALL (1) SHA512 ... Hash:"
    if(Contains(prompt,n,u"volume:"))return true;
    return false;
}
// Notices DCS prints at the caret of its password line (F7, F8, F9) and
// removes again: public text.
inline bool PublicNotice(const char16_t* text) {
    static const char16_t* const notices[]={u" Platform locked!",u" Platform unlocked!",u" TPM locked!",u" TPM unlocked!",u" Smart card locked!",u" Smart card unlocked!"};
    if(!text)return false;
    for(const auto* notice:notices) {size_t i=0;while(notice[i] && text[i]==notice[i])++i;if(!notice[i] && !text[i])return true;}
    return false;
}
// Characters of one console write that leave ink (not spaces, not controls).
inline size_t Inked(const char16_t* text) {size_t n=0;if(text)for(;*text;++text)if(*text>32 && *text!=127)++n;return n;}
template<size_t Rows,size_t Columns> struct Mask {
    char16_t clear[Rows][Columns]={};   // the real character of a masked cell; 0: the cell is not secret
    size_t marks=0;                      // masked cells on the screen
    size_t row=SIZE_MAX,column=0;        // the entry: the caret's row and the first cell after the prompt
    bool active=false,secret=false;
    size_t counted=0;                    // masked cells of the entry row at the last cue
    // The program read a key with the caret at (r,c); `left` is that row's text.
    void Key(size_t r,size_t c,const char16_t* left) {
        if(active && r==row && c>=column)return;
        active=true;row=r;column=c;secret=!OpenPrompt(left,c);counted=Count(r);
    }
    void Leave() {active=false;secret=false;}
    bool Entry() const {return active && secret;}
    bool Span(size_t r,size_t c) const {return active && secret && r==row && c>=column;}
    size_t Count(size_t r) const {size_t n=0;if(r<Rows)for(size_t c=0;c<Columns;++c)if(clear[r][c])++n;return n;}
    // Will a write of `text` at (r,c) be masked? `inked`: Inked(text). A
    // write of two or more characters into an entry nothing was typed in yet
    // is the program's own output (a countdown printed again), not an echo.
    bool Masks(size_t r,size_t c,size_t inked,bool notice) const {
        return Span(r,c) && !notice && (inked<2 || Count(r)!=0);
    }
    // One character written at (r,c). Returns what the screen model shows.
    char16_t Put(size_t r,size_t c,char16_t ch,bool mask) {
        if(r>=Rows || c>=Columns)return ch;
        if(clear[r][c]) {clear[r][c]=0;if(marks)--marks;}
        if(!mask || ch==u' ' || !ch)return ch;
        clear[r][c]=ch;++marks;return u'*';
    }
    // The screen scrolled up by one row (`rows` rows of `columns` cells).
    void ScrollUp(size_t rows,size_t columns) {
        if(rows>Rows)rows=Rows;if(columns>Columns)columns=Columns;
        if(!rows)return;
        for(size_t c=0;c<columns;++c)if(clear[0][c] && marks)--marks;
        for(size_t r=1;r<rows;++r)for(size_t c=0;c<columns;++c)clear[r-1][c]=clear[r][c];
        for(size_t c=0;c<columns;++c)clear[rows-1][c]=0;
        if(active) {if(!row)Leave();else --row;}
    }
    void Reset() {
        if(marks)for(size_t r=0;r<Rows;++r)for(size_t c=0;c<Columns;++c)clear[r][c]=0;
        marks=0;Leave();row=SIZE_MAX;column=0;counted=0;
    }
};
}  // namespace hakefi::secretinput
