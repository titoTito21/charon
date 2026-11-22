/* Stand-in for NVDA's 32-bit helper loader in Windows Setup's WinPE, which runs no 32-bit programs:
   waits until NVDA closes the pipe on standard input, as the real loader does, then ends. */
__declspec(dllimport) void* __stdcall GetStdHandle(unsigned long which);
__declspec(dllimport) int __stdcall ReadFile(void* file,void* buffer,unsigned long bytes,unsigned long* got,void* overlapped);
__declspec(dllimport) void __stdcall ExitProcess(unsigned code);
void entry(void) {
    void* in=GetStdHandle((unsigned long)-10);char byte;unsigned long got=0;
    while(ReadFile(in,&byte,1,&got,0) && got) {}
    ExitProcess(0);
}
