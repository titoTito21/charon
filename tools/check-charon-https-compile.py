"""Compile and link check of Charon's HTTPS client for the UEFI target (r401).

Nothing in the tree is edited. Two scratch translation units are written into
NEW_OUTPUT_DIRECTORY and built with the flags of tools/build-charon-windows.py:

1. boot-with-https.cpp: the whole boot unit (src/efi/HAKBootEFI.cpp, included
   unchanged) followed by CharonTcp4Transport.h, CharonHttps.h and CharonMsIso.h
   and one function that uses them. Proves the new headers compile inside the
   Charon tree with -Wall -Wextra -Werror next to every existing header.
2. closure.cpp: the HTTPS client and the Microsoft flow alone, over a dummy
   transport, linked (ld.lld -r --gc-sections) with the BearSSL archive and
   CharonMemory.c. Proves there are no unresolved symbols and no global
   constructors, and measures the object code the feature adds.

Usage: python check-charon-https-compile.py NEW_OUTPUT_DIRECTORY BEARSSL_BUILD_DIRECTORY
       (BEARSSL_BUILD_DIRECTORY: output of tools/build-charon-bearssl.py)
"""
from pathlib import Path
import json,os,shutil,subprocess,sys

root=Path(__file__).resolve().parents[1]
container=root.parents[1]
llvm=Path(os.environ.get('CHARON_LLVM',container/'work/llvm-23.1.5/bin'))
if len(sys.argv)!=3:sys.exit(__doc__)
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=False)
archive=Path(sys.argv[2]).resolve()/'libcharon-bearssl.a'
assert archive.exists(),'run tools/build-charon-bearssl.py first'
clang=str(llvm/'clang.exe');clangxx=str(llvm/'clang++.exe');lld=str(llvm/'ld.lld.exe');nm=str(llvm/'llvm-nm.exe');readelf=str(llvm/'llvm-readelf.exe')
target=['--target=x86_64-unknown-elf']
flags=target+'-O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -mcmodel=large -mno-red-zone -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables'.split()
cxx=flags+'-std=c++17 -fno-exceptions -fno-rtti'.split()
boot=['-fshort-wchar','-Wall','-Wextra','-Werror','-DHAK_CHARON_LIVE_VOICE']
def run(command):
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    if result.returncode:
        sys.stderr.write(result.stdout+result.stderr);raise SystemExit('FAILED: '+' '.join(map(str,command[-4:])))
    return result.stdout
shutil.copytree(root/'src/efi/haiku',out/'include/efi')
include=['-I'+str(out/'include'),'-I'+str(root/'tools/charon-compat-include'),'-I'+str(root/'src/efi')]
(out/'boot-with-https.cpp').write_text('''// Scratch unit: the boot unit unchanged, then the new headers.
#include "HAKBootEFI.cpp"
#include "CharonTcp4Transport.h"
#include "CharonHttps.h"
#include "CharonMsIso.h"
namespace {
bool Discard(void*,const uint8_t*,size_t) {return true;}
}
extern "C" int charon_https_probe(const char* language,const char* url) {
    using namespace hakefi;
    static https::Client client;static msiso::Link link;
    client.transport=ws::net::tcp::Transport();
    const auto found=msiso::Windows11Link(client,language,link);
    https::Response response;uint64_t offset=0;
    const auto result=https::Download(client,url?url:link.url,offset,nullptr,Discard,nullptr,response);
    if(result!=https::Result::ok)ws::Fail(ws::net::tcp::ErrorText(client.transportError));
    return int(found)*100+int(result);
}
''',newline='\n')
run([clangxx,*cxx,*boot,*include,'-c',out/'boot-with-https.cpp','-o',out/'boot-with-https.o'])
symbols=run([nm,'-C',out/'boot-with-https.o'])
for wanted in ('hakefi::ws::net::tcp::Connect','hakefi::ws::net::tcp::ResolveOwn','hakefi::https::Get','hakefi::msiso::Windows11Link'):
    assert wanted in symbols,'not generated: '+wanted
assert '__cxa_guard' not in symbols,'a function-local static needs a constructor guard (no C++ runtime in Charon)'
sections=run([readelf,'-S','-W',out/'boot-with-https.o'])
assert 'INIT_ARRAY' not in sections and '.ctors' not in sections,'the new headers add a global constructor'
print('PASS 1: HAKBootEFI.cpp + CharonTcp4Transport.h + CharonHttps.h + CharonMsIso.h compile with the Charon flags (-Wall -Wextra -Werror), no global constructors')

(out/'closure.cpp').write_text('''// Scratch unit: client and Microsoft flow over a dummy transport.
#include "CharonHttps.h"
#include "CharonMsIso.h"
namespace {
int Connect(void*,const char*,uint16_t) {return -1;}
int Send(void*,const uint8_t*,size_t) {return -1;}
int Receive(void*,uint8_t*,size_t) {return -1;}
void Shut(void*) {}
bool Clock(void*,hakefi::https::UtcTime*) {return false;}
bool Entropy(void*,uint8_t*,size_t) {return false;}
bool Discard(void*,const uint8_t*,size_t) {return true;}
}
extern "C" int charon_https_closure(const char* language) {
    using namespace hakefi;
    static https::Client client;static msiso::Link link;
    client.transport={nullptr,Connect,Send,Receive,Shut,Clock,Entropy,nullptr};
    const auto found=msiso::Windows11Link(client,language,link);
    https::Response response;uint64_t offset=0;
    return int(found)*100+int(https::Download(client,link.url,offset,nullptr,Discard,nullptr,response));
}
''',newline='\n')
run([clangxx,*cxx,*boot,*include,'-c',out/'closure.cpp','-o',out/'closure.o'])
run([clang,*flags,'-std=c11','-c',root/'src/tts/runtime/CharonMemory.c','-o',out/'memory.o'])
def measure(path):
    total={'text':0,'rodata':0,'data':0,'bss':0}
    for line in run([readelf,'-S','-W',path]).splitlines():
        parts=line.replace('[',' ').replace(']',' ').split()
        if len(parts)<6 or not parts[0].isdigit():continue
        name,kind,length=parts[1],parts[2],int(parts[5],16)
        if name.startswith(('.text','.ltext')):total['text']+=length
        elif name.startswith(('.rodata','.lrodata')):total['rodata']+=length
        elif kind=='NOBITS':total['bss']+=length
        elif name.startswith(('.data','.ldata')):total['data']+=length
    return total
run([lld,'-r','--gc-sections','--undefined=charon_https_closure','-o',out/'closed.o',out/'closure.o',out/'memory.o',archive])
undefined=run([nm,'-u',out/'closed.o']).strip()
assert not undefined,'unresolved symbols:\n'+undefined
sections=run([readelf,'-S','-W',out/'closed.o'])
assert 'INIT_ARRAY' not in sections and '.ctors' not in sections,'dynamic initialization present'
run([lld,'-r','--gc-sections','--undefined=memcpy','--undefined=memmove','--undefined=memset','--undefined=memcmp','--undefined=strlen','-o',out/'memory-only.o',out/'memory.o'])
whole=measure(out/'closed.o');base=measure(out/'memory-only.o')
added={k:whole[k]-base[k] for k in whole}
report={'addedByHttpsAndBearSSL':added,'imageBytesAdded':added['text']+added['rodata']+added['data'],'zeroInitializedBytes':added['bss']}
(out/'https-size.json').write_text(json.dumps(report,indent=1))
print(json.dumps(report,indent=1))
print('PASS 2: closed link of the HTTPS client + Microsoft flow + BearSSL + CharonMemory.c has no unresolved symbols and no global constructors')
