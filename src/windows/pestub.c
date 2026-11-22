/* Stand-in for system DLLs that Windows Setup's WinPE does not have but NVDA binds at import time.
   One binary, copied under each missing name; every function reports failure. */
#define X __declspec(dllexport)
/* bthprops.cpl */
X unsigned long BluetoothGetDeviceInfo(void* radio,void* info) {(void)radio;(void)info;return 50;}   /* ERROR_NOT_SUPPORTED */
/* sas.dll */
X void SendSAS(int asUser) {(void)asUser;}
/* magnification.dll */
X int MagInitialize(void) {return 0;}
X int MagUninitialize(void) {return 0;}
X int MagSetFullscreenColorEffect(void* e) {(void)e;return 0;}
X int MagGetFullscreenColorEffect(void* e) {(void)e;return 0;}
X int MagShowSystemCursor(int show) {(void)show;return 0;}
X int MagSetFullscreenTransform(float level,int x,int y) {(void)level;(void)x;(void)y;return 0;}
X int MagGetFullscreenTransform(float* level,int* x,int* y) {(void)level;(void)x;(void)y;return 0;}
X int MagSetInputTransform(int enabled,void* source,void* dest) {(void)enabled;(void)source;(void)dest;return 0;}
X int MagGetInputTransform(int* enabled,void* source,void* dest) {(void)enabled;(void)source;(void)dest;return 0;}
/* winusb.dll */
X int WinUsb_Initialize(void* device,void** handle) {(void)device;(void)handle;return 0;}
X int WinUsb_Free(void* handle) {(void)handle;return 0;}
X int WinUsb_QueryInterfaceSettings(void* h,unsigned char n,void* d) {(void)h;(void)n;(void)d;return 0;}
X int WinUsb_QueryPipe(void* h,unsigned char a,unsigned char p,void* i) {(void)h;(void)a;(void)p;(void)i;return 0;}
X int WinUsb_ReadPipe(void* h,unsigned char p,void* b,unsigned long n,unsigned long* got,void* o) {(void)h;(void)p;(void)b;(void)n;(void)got;(void)o;return 0;}
X int WinUsb_WritePipe(void* h,unsigned char p,void* b,unsigned long n,unsigned long* put,void* o) {(void)h;(void)p;(void)b;(void)n;(void)put;(void)o;return 0;}
X int WinUsb_SetPipePolicy(void* h,unsigned char p,unsigned long t,unsigned long n,void* v) {(void)h;(void)p;(void)t;(void)n;(void)v;return 0;}
/* pdh.dll (psutil binds it; only its load-average counter uses it) */
X long PdhOpenQueryW(const void* source,unsigned long long user,void** query) {(void)source;(void)user;(void)query;return (long)0xC0000BBB;}   /* PDH_MEMORY_ALLOCATION_FAILURE */
X long PdhCloseQuery(void* query) {(void)query;return (long)0xC0000BBC;}   /* PDH_INVALID_HANDLE */
X long PdhRemoveCounter(void* counter) {(void)counter;return (long)0xC0000BBC;}
X long PdhAddEnglishCounterW(void* query,const void* path,unsigned long long user,void** counter) {(void)query;(void)path;(void)user;(void)counter;return (long)0xC0000BBC;}
X long PdhGetFormattedCounterValue(void* counter,unsigned long format,unsigned long* type,void* value) {(void)counter;(void)format;(void)type;(void)value;return (long)0xC0000BBC;}
X long PdhCollectQueryData(void* query) {(void)query;return (long)0xC0000BBC;}
X long PdhCollectQueryDataEx(void* query,unsigned long seconds,void* event) {(void)query;(void)seconds;(void)event;return (long)0xC0000BBC;}
/* d3d11.dll (NVDA's helper for Windows OneCore voices and screen recognition binds it) */
X long D3D11CreateDevice(void* a,int b,void* c,unsigned d,const void* e,unsigned f,unsigned g,void** h,void* i,void** j) {(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;return (long)0x80004005;}   /* E_FAIL */
X long CreateDirect3D11DeviceFromDXGIDevice(void* dxgi,void** device) {(void)dxgi;(void)device;return (long)0x80004005;}
int _DllMainCRTStartup(void* instance,unsigned long reason,void* reserved) {(void)instance;(void)reason;(void)reserved;return 1;}
