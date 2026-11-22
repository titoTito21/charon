#pragma once
// r400: Charon workspace - one download call for http:// and https://.
// Plain HTTP goes through the firmware's EFI_HTTP (net::Download). HTTPS goes
// through Charon's own TLS client (CharonHttps.h, BearSSL) over the firmware's
// TCP, because firmware TLS with certificates is rarely there. Certificates
// are checked against the built-in roots and the firmware clock; a wrong
// clock is reported as such, never skipped. GPL-2.0-or-later.
#include "CharonWsNet.h"
#ifdef HAK_CHARON_HTTPS
#include "CharonTcp4Transport.h"
#endif
namespace hakefi {
namespace ws {
namespace net {
inline bool IsHttps(const char16_t* url) {
    return url && speechtext::LowerCase(url[0])==u'h' && speechtext::LowerCase(url[1])==u't' && speechtext::LowerCase(url[2])==u't'
        && speechtext::LowerCase(url[3])==u'p' && speechtext::LowerCase(url[4])==u's' && url[5]==u':';
}
#ifdef HAK_CHARON_HTTPS
struct HttpsJob {efi_file_protocol* file;Memory* memory;uint64_t done,total;const char16_t* label;unsigned spoken,rounds;bool failed;};
inline bool HttpsSink(void* context,const uint8_t* data,size_t size) {
    auto& j=*static_cast<HttpsJob*>(context);
    if(j.memory) {
        if(j.done+size>j.memory->capacity) {j.failed=true;return false;}
        memcpy(j.memory->data+j.done,data,size);
    } else {
        size_t wrote=size;
        if(j.file->Write(j.file,&wrote,const_cast<uint8_t*>(data))!=EFI_SUCCESS || wrote!=size) {j.failed=true;return false;}
    }
    j.done+=size;
    if(++j.rounds%16==0 && j.total) {
        Progress(j.label,j.done,j.total);
    }
    Pump();
    return true;
}
inline void HttpsFail(const https::Client& client,https::Result result) {
    static char16_t text[400];
    switch(result) {
        case https::Result::certificateUntrusted:Set(text,400,T(u"Serwer przedstawił certyfikat, któremu Charon nie ufa. Nic nie pobrano.",u"The server showed a certificate Charon does not trust. Nothing was downloaded."));break;
        case https::Result::certificateExpired:Set(text,400,T(u"Według zegara komputera certyfikat serwera jest nieważny. Sprawdź zegar w Narzędziach systemowych.",u"According to the computer's clock the server's certificate is not valid. Check the clock in System tools."));break;
        case https::Result::certificateWrongHost:Set(text,400,T(u"Certyfikat należy do innego serwera. Nic nie pobrano.",u"The certificate belongs to another server. Nothing was downloaded."));break;
        case https::Result::noClock:Set(text,400,T(u"Firmware nie podaje daty, więc certyfikatu nie da się sprawdzić.",u"The firmware gives no date, so the certificate cannot be checked."));break;
        case https::Result::connectFailed:Set(text,400,T(u"Nie udało się połączyć z serwerem: ",u"Could not connect to the server: "));Cat(text,400,tcp::ErrorText(client.transportError));break;
        default: {
            Set(text,400,T(u"Pobieranie przez HTTPS nie powiodło się: ",u"The HTTPS download failed: "));
            static char16_t wide[160];const char* reason=https::ResultText(result);size_t n=0;for(;reason[n] && n<158;++n)wide[n]=char16_t(uint8_t(reason[n]));
            wide[n]=0;Cat(text,400,wide);
        }
    }
    Fail(text);
}
inline bool DownloadHttps(const char16_t* url,const FileRef& target,uint64_t* received,Memory* memory,const char16_t* what) {
    static https::Client client;static char ascii[2048];static https::Response probe,answer;*received=0;
    size_t n=0;for(;url[n] && n<2046;++n) {if(url[n]>126 || url[n]<33) {Fail(T(u"Adres zawiera niedozwolone znaki.",u"The address contains characters that are not allowed."));return false;}ascii[n]=char(url[n]);}
    ascii[n]=0;client.transport=tcp::Transport();
    HttpsJob job{};job.memory=memory;job.label=what?what:T(u"Pobieranie",u"Downloading");
    auto result=https::Probe(client,ascii,probe);
    if(result!=https::Result::ok) {HttpsFail(client,result);return false;}
    job.total=probe.totalLength!=https::unknownLength?probe.totalLength:probe.contentLength!=https::unknownLength?probe.contentLength:0;
    if(memory) {
        if(!job.total) {Fail(T(u"Serwer nie podał rozmiaru pliku.",u"The server did not give the file size."));return false;}
        if(!Reserve(*memory,size_t(job.total)+memory->extra+4096)) {Fail(T(u"Za mało pamięci RAM na ten plik.",u"Not enough RAM for this file."));return false;}
    } else {
        if(OpenFile(target,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0,&job.file)==EFI_SUCCESS) {if(job.file->Delete(job.file)!=EFI_SUCCESS) {Fail(T(u"Nośnik jest tylko do odczytu.",u"The media is read-only."));return false;}job.file=nullptr;}
        if(OpenFile(target,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0,&job.file)!=EFI_SUCCESS) {Fail(T(u"Nie udało się utworzyć pliku.",u"The file could not be created."));return false;}
    }
    uint64_t offset=0;
    result=https::Download(client,ascii,offset,probe.validator[0]?probe.validator:nullptr,HttpsSink,&job,answer);
    *received=job.done;
    const bool ok=result==https::Result::ok && !job.failed;
    if(memory) {if(ok)memory->size=size_t(job.done);else Free(*memory);}
    else {if(ok) {if(job.file->Flush)job.file->Flush(job.file);job.file->Close(job.file);} else job.file->Delete(job.file);}
    if(ok)Progress(job.label,job.done,job.total?job.total:job.done);
    else if(job.failed)Fail(T(u"Zapis nie powiódł się; może brakuje miejsca.",u"Writing failed; the disk may be full."));
    else HttpsFail(client,result);
    return ok;
}
#endif
// http:// or https:// into a file, or into memory when `memory` is given.
inline bool DownloadAny(const char16_t* url,const FileRef& target,uint64_t* received,Memory* memory=nullptr,const char16_t* what=nullptr) {
#ifdef HAK_CHARON_HTTPS
    if(IsHttps(url))return DownloadHttps(url,target,received,memory,what);
#endif
    return Download(url,target,received,memory,what);
}
}  // namespace net
}  // namespace ws
}  // namespace hakefi
