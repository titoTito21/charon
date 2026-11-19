#include "../src/HAKBootSigner.h"
#include <cassert>
int main(int argc,char** argv) {
    assert(argc==2);std::string base=argv[1];haksecure::SignInput input;
    assert(haksecure::Read(base.c_str(),input.before));input.database=2;
    input.certificate=base+"/cert.pem";input.key=base+"/key.pem";input.passwordFile=base+"/password.txt";
    haksecure::Signer signer;haksecure::Review review;
    assert(signer.Start(input));signer.Cancel();assert(signer.Poll(review)==-1);
    assert(signer.Start(input));int state=0;
    while(!(state=signer.Poll(review)))usleep(10000);
    assert(state==1 && review.entries.empty() && haksecure::MatchesDraft(review.auth,{}));
    FILE* out=fopen((base+"/signed.auth").c_str(),"wb");assert(out);
    assert(fwrite(review.auth.data(),1,review.auth.size(),out)==review.auth.size());assert(fclose(out)==0);
    input.passwordFile.clear();assert(signer.Start(input));
    while(!(state=signer.Poll(review)))usleep(10000);
    assert(state==-1);
    input.database=4;assert(!signer.Start(input));
    puts("PASS native asynchronous signing, cancellation, encrypted key/password file, missing password refusal and target bounds");
}
