/* stub: <aaudio/AAudio.h>, only what snd_android.c uses */
#ifndef FP_STUB_AAUDIO
#define FP_STUB_AAUDIO
#include <stdint.h>
typedef struct AAudioStream AAudioStream;
typedef struct AAudioStreamBuilder AAudioStreamBuilder;
typedef int32_t aaudio_result_t;
typedef int32_t aaudio_data_callback_result_t;
enum { AAUDIO_OK = 0, AAUDIO_ERROR_BASE = -900 };
enum { AAUDIO_DIRECTION_OUTPUT = 0 };
enum { AAUDIO_FORMAT_PCM_I16 = 1 };
enum { AAUDIO_PERFORMANCE_MODE_LOW_LATENCY = 12 };
enum { AAUDIO_SHARING_MODE_SHARED = 0 };
enum { AAUDIO_CALLBACK_RESULT_CONTINUE = 0, AAUDIO_CALLBACK_RESULT_STOP = 1 };
#define AAUDIO_UNSPECIFIED 0
typedef aaudio_data_callback_result_t (*AAudioStream_dataCallback)(AAudioStream *stream, void *userData, void *audioData, int32_t numFrames);
typedef void (*AAudioStream_errorCallback)(AAudioStream *stream, void *userData, aaudio_result_t error);
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **builder);
void AAudioStreamBuilder_delete(AAudioStreamBuilder *builder);
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *builder, int32_t direction);
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *builder, int32_t format);
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *builder, int32_t channelCount);
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *builder, int32_t sampleRate);
void AAudioStreamBuilder_setFramesPerDataCallback(AAudioStreamBuilder *builder, int32_t frames);
void AAudioStreamBuilder_setBufferCapacityInFrames(AAudioStreamBuilder *builder, int32_t frames);
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *builder, int32_t mode);
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *builder, int32_t mode);
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *builder, AAudioStream_dataCallback cb, void *userData);
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *builder, AAudioStream_errorCallback cb, void *userData);
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *builder, AAudioStream **stream);
int32_t AAudioStream_getSampleRate(AAudioStream *stream);
int32_t AAudioStream_getFramesPerBurst(AAudioStream *stream);
aaudio_result_t AAudioStream_requestStart(AAudioStream *stream);
aaudio_result_t AAudioStream_requestPause(AAudioStream *stream);
aaudio_result_t AAudioStream_requestStop(AAudioStream *stream);
aaudio_result_t AAudioStream_close(AAudioStream *stream);
#endif
