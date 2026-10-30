/* stub: <android/looper.h> */
#ifndef FP_STUB_LOOPER
#define FP_STUB_LOOPER
enum { ALOOPER_POLL_WAKE = -1, ALOOPER_POLL_CALLBACK = -2, ALOOPER_POLL_TIMEOUT = -3, ALOOPER_POLL_ERROR = -4 };
typedef struct ALooper ALooper;
int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData);
#endif
