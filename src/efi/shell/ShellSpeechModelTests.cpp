#include "ShellSpeechModel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
static const HAK_CHAR16* U(const char16_t* text){return reinterpret_cast<const HAK_CHAR16*>(text);}
int main() {
  HAK_SHELL_MODEL m{};char out[HAK_SHELL_UTF8];
  HakModelOutput(&m,U(u"abc\rZ"));CHECK(HakModelFlush(&m,out) && std::strcmp(out,"Zbc")==0);
  CHECK(!HakModelFlush(&m,out));
  HakModelOutput(&m,U(u"\r\nsecond\r\n"));CHECK(m.count==2);
  CHECK(HakModelFlush(&m,out) && std::strcmp(out,"second")==0);
  CHECK(HakModelReview(&m,-1,out) && std::strcmp(out,"second")==0);
  CHECK(HakModelReview(&m,-1,out) && std::strcmp(out,"Zbc")==0);
  CHECK(HakModelReview(&m,-1,out) && std::strcmp(out,"Start of history")==0);
  CHECK(HakModelReview(&m,1,out) && std::strcmp(out,"second")==0);
  m.editing=1;HakModelOutput(&m,U(u"redrawn history never becomes new output"));CHECK(!m.length && m.count==2);
  CHECK(HakModelEditor(&m,U(u"h"),1,1,out) && std::strcmp(out,"h")==0);
  CHECK(HakModelEditor(&m,U(u"help"),4,4,out) && std::strcmp(out,"help")==0);
  CHECK(HakModelEditor(&m,U(u"reset"),5,5,out) && std::strcmp(out,"reset")==0);
  CHECK(HakModelEditor(&m,U(u"reset"),5,0,out) && std::strcmp(out,"r")==0);
  CHECK(HakModelEditor(&m,U(u"reset"),5,5,out) && std::strcmp(out,"End of line")==0);
  CHECK(!HakModelEditor(&m,U(u"reset"),5,5,out));
  CHECK(HakModelEditor(&m,U(u"rese"),4,4,out) && std::strcmp(out,"t")==0);
  CHECK(HakModelEditor(&m,U(u"reseX"),5,5,out) && std::strcmp(out,"X")==0);
  CHECK(HakModelEditor(&m,U(u""),0,0,out) && std::strcmp(out,"Blank line")==0);
  CHECK(HakModelEditor(&m,U(u"\U0001f600"),2,2,out) && std::strcmp(out,"\xf0\x9f\x98\x80")==0);
  CHECK(HakModelEditor(&m,U(u"\U0001f600"),2,1,out) && std::strcmp(out,"\xf0\x9f\x98\x80")==0);
  HAK_CHAR16 malformed[]={0xd800,'x',0xdc00};
  CHECK(HakModelUtf8(malformed,3,out,sizeof(out))==7 && std::strcmp(out,"\xef\xbf\xbdx\xef\xbf\xbd")==0);
  for(unsigned cap=0;cap<8;++cap){char bounded[9];std::memset(bounded,42,sizeof(bounded));unsigned written=HakModelUtf8(U(u"\U0001f600"),2,bounded,cap);CHECK(written==0 || written==4);CHECK(bounded[cap]==42);if(cap)CHECK(bounded[written]==0);}
  m.editing=0;HakModelClear(&m);HakModelCursor(&m,2,0);HakModelOutput(&m,U(u"x\bY"));
  CHECK(HakModelFlush(&m,out) && std::strcmp(out,"  Y")==0);
  HakModelClear(&m);for(unsigned i=0;i<100;++i)HakModelOutput(&m,U(u"retained\r\n"));CHECK(m.count==32);
  HakModelClear(&m);
  HAK_CHAR16 longLine[800];for(auto& c:longLine)c='a';longLine[799]=0;
  HakModelOutput(&m,longLine);CHECK(m.length==255 && m.truncated);
  CHECK(HakModelFlush(&m,out) && std::strlen(out)==255);
  HakModelClear(&m);HakModelOutput(&m,U(u"Command result\r\nFS0:> "));
  CHECK(HakModelFlush(&m,out) && std::strcmp(out,"Command result. FS0:> ")==0);
  m.polish=1;m.count=0;CHECK(HakModelReview(&m,-1,out) && std::strcmp(out,"Brak zapisanych wierszy")==0);
  std::puts("PASS bounded Shell model: CR/cursor overwrite, history/review, redraw suppression, editor replacement/deletion/movement, Unicode and truncation");
}
