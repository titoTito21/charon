/* GPL-2.0-or-later. See ShellSpeechModel.h. */
#include "ShellSpeechModel.h"
static void CopyLine(HAK_CHAR16* to,const HAK_CHAR16* from,unsigned size) {
  unsigned i;for(i=0;i<size && i<HAK_SHELL_COLUMNS-1;i++)to[i]=from[i];to[i]=0;
}
unsigned HakModelUtf8(const HAK_CHAR16* in,unsigned length,char* out,unsigned capacity) {
  unsigned i,at=0,c,n;for(i=0;i<length;i++) {
    c=in[i];if(c>=0xd800 && c<=0xdbff) {
      if(i+1<length && in[i+1]>=0xdc00 && in[i+1]<=0xdfff)c=0x10000+((c-0xd800)<<10)+(in[++i]-0xdc00);
      else c=0xfffd;
    } else if(c>=0xdc00 && c<=0xdfff)c=0xfffd;
    n=c<0x80?1:c<0x800?2:c<0x10000?3:4;if(at+n>=capacity)break;
    if(n==1)out[at++]=(char)c;
    else {if(n==4)out[at++]=(char)(0xf0|(c>>18));
      if(n>=3)out[at++]=(char)((n==3?0xe0:0x80)|((c>>12)&(n==3?15:63)));
      out[at++]=(char)((n==2?0xc0:0x80)|((c>>6)&(n==2?31:63)));
      out[at++]=(char)(0x80|(c&63));}
  }
  if(capacity)out[at]=0;return at;
}
static int Literal(char* out,const char* text) {unsigned i=0;while(text[i]){out[i]=text[i];i++;}out[i]=0;return 1;}
static void Commit(HAK_SHELL_MODEL* m) {
  unsigned n=m->length;while(n && m->line[n-1]==' ')--n;
  if(n) {CopyLine(m->history[m->head],m->line,n);m->head=(m->head+1)%HAK_SHELL_LINES;
    if(m->count<HAK_SHELL_LINES)++m->count;m->review=0;m->pending=1;m->completedPending=1;}
  m->column=0;m->length=0;m->line[0]=0;
}
void HakModelOutput(HAK_SHELL_MODEL* m,const HAK_CHAR16* text) {
  unsigned c;if(m->editing || !text)return;
  while((c=*text++)!=0) {
    if(c==13)m->column=0;
    else if(c==10) {Commit(m);++m->row;}
    else if(c==8) {if(m->column)--m->column;}
    else if(c==9) {unsigned next=(m->column+8)&~7u;while(m->column<next && m->column<HAK_SHELL_COLUMNS-1)m->line[m->column++]=' ';if(m->column>m->length)m->length=m->column;m->line[m->length]=0;}
    else if(c>=32 && c!=127) {
      if(m->column<HAK_SHELL_COLUMNS-1) {while(m->length<m->column)m->line[m->length++]=' ';
        m->line[m->column++]=(HAK_CHAR16)c;if(m->column>m->length)m->length=m->column;m->line[m->length]=0;m->pending=1;}
      else m->truncated=1;
    }
  }
}
void HakModelCursor(HAK_SHELL_MODEL* m,unsigned column,unsigned row) {
  if(m->editing)return;
  if(row!=m->row && m->length)Commit(m);
  m->row=row;m->column=column<HAK_SHELL_COLUMNS?column:HAK_SHELL_COLUMNS-1;
}
void HakModelClear(HAK_SHELL_MODEL* m) {
  if(m->editing)return;
  m->column=m->row=m->length=0;m->line[0]=0;m->pending=m->completedPending=0;
}
int HakModelFlush(HAK_SHELL_MODEL* m,char* out) {
  const HAK_CHAR16* line;unsigned length=0,at;int completed;
  if(!m->pending)return 0;m->pending=0;completed=m->completedPending;m->completedPending=0;
  line=completed && m->count?m->history[(m->head+HAK_SHELL_LINES-1)%HAK_SHELL_LINES]:m->length?m->line:0;
  if(!line)return 0;while(length<HAK_SHELL_COLUMNS-1 && line[length])++length;
  at=HakModelUtf8(line,length,out,HAK_SHELL_UTF8);
  if(completed && m->length && at+3<HAK_SHELL_UTF8) {
    out[at++]='.';out[at++]=' ';at+=HakModelUtf8(m->line,m->length,out+at,HAK_SHELL_UTF8-at);
  }
  return at!=0;
}
int HakModelReview(HAK_SHELL_MODEL* m,int direction,char* out) {
  const HAK_CHAR16* line;unsigned length=0;
  if(!m->count)return Literal(out,m->polish?"Brak zapisanych wierszy":"No retained lines");
  if(direction<0) {if(m->review<m->count)++m->review;else return Literal(out,m->polish?"Początek historii":"Start of history");}
  else if(m->review>1)--m->review;
  else return Literal(out,m->polish?"Koniec historii":"End of history");
  line=m->history[(m->head+HAK_SHELL_LINES-m->review)%HAK_SHELL_LINES];
  while(length<HAK_SHELL_COLUMNS-1 && line[length])++length;
  HakModelUtf8(line,length,out,HAK_SHELL_UTF8);return 1;
}
int HakModelEditorLine(HAK_SHELL_MODEL* m,char* out) {
  if(!m->editorLength)return Literal(out,m->polish?"Pusty wiersz":"Blank line");
  HakModelUtf8(m->editor,m->editorLength,out,HAK_SHELL_UTF8);return 1;
}
int HakModelEditor(HAK_SHELL_MODEL* m,const HAK_CHAR16* value,unsigned length,unsigned cursor,char* out) {
  unsigned i=0,start=0,size=0,j,delta=0;int changed,result=0,insertion=0,deletion=0;
  if(length>=HAK_SHELL_COLUMNS){length=HAK_SHELL_COLUMNS-1;m->truncated=1;}
  if(cursor>length)cursor=length;
  while(i<length && i<m->editorLength && value[i]==m->editor[i])++i;
  changed=i!=length || length!=m->editorLength;
  if(length>m->editorLength) {
    delta=length-m->editorLength;
    if(delta<=2 && cursor>=delta && i==cursor-delta) {
      insertion=1;for(j=cursor;j<length;j++)if(value[j]!=m->editor[j-delta])insertion=0;
    }
  } else if(m->editorLength>length) {
    delta=m->editorLength-length;
    if(delta<=2 && cursor==i) {
      deletion=1;for(j=i;j<length;j++)if(value[j]!=m->editor[j+delta])deletion=0;
    }
  }
  if(changed) {
    if(insertion) {
      start=cursor-1;if(value[start]>=0xdc00 && value[start]<=0xdfff && start>0)--start;
      size=cursor-start;HakModelUtf8(value+start,size,out,HAK_SHELL_UTF8);result=1;
    } else if(deletion) {
      start=i;size=delta;
      HakModelUtf8(m->editor+start,size,out,HAK_SHELL_UTF8);result=1;
    } else {HakModelUtf8(value,length,out,HAK_SHELL_UTF8);result=length!=0;if(!result)result=Literal(out,m->polish?"Pusty wiersz":"Blank line");}
  } else if(cursor!=m->editorCursor) {
    if(cursor==length)result=Literal(out,m->polish?"Koniec wiersza":"End of line");
    else {start=cursor;if(value[start]>=0xdc00 && value[start]<=0xdfff && start>0)--start;
      size=value[start]>=0xd800 && value[start]<=0xdbff && start+1<length?2:1;
      HakModelUtf8(value+start,size,out,HAK_SHELL_UTF8);result=1;}
  }
  CopyLine(m->editor,value,length);m->editorLength=length;m->editorCursor=cursor;return result;
}
