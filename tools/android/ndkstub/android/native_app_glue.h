/* stub: <android/native_app_glue.h> (plus the bits of native_activity.h it pulls in) */
#ifndef FP_STUB_APP_GLUE
#define FP_STUB_APP_GLUE
#include <stdint.h>
#include <jni.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/keycodes.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct ANativeActivity ANativeActivity;
struct ANativeActivity {
    void *callbacks;
    JavaVM *vm;                 /* opaque here (jni.h defines the real one) */
    JNIEnv *env;
    jobject clazz;
    const char *internalDataPath;
    const char *externalDataPath;
    int32_t sdkVersion;
    void *instance;
    void *assetManager;
    const char *obbPath;
};
enum { APP_CMD_INPUT_CHANGED = 0, APP_CMD_INIT_WINDOW = 1, APP_CMD_TERM_WINDOW = 2, APP_CMD_WINDOW_RESIZED = 3,
       APP_CMD_WINDOW_REDRAW = 4, APP_CMD_CONTENT_RECT_CHANGED = 5, APP_CMD_GAINED_FOCUS = 6,
       APP_CMD_LOST_FOCUS = 7, APP_CMD_CONFIG_CHANGED = 8, APP_CMD_LOW_MEMORY = 9, APP_CMD_START = 10,
       APP_CMD_RESUME = 11, APP_CMD_SAVE_STATE = 12, APP_CMD_PAUSE = 13, APP_CMD_STOP = 14,
       APP_CMD_DESTROY = 15 };
struct android_app;
struct android_poll_source { int32_t id; void (*process)(struct android_app *app, struct android_poll_source *source); };
struct android_app {
    void *userData;
    void (*onAppCmd)(struct android_app *app, int32_t cmd);
    int32_t (*onInputEvent)(struct android_app *app, AInputEvent *event);
    ANativeActivity *activity;
    AConfiguration *config;
    ANativeWindow *window;
    void *looper;
    struct android_poll_source *inputPollSource;
    int32_t activityState;
    int32_t destroyRequested;
    void *mutex, *cond, *msgread, *msgwrite, *thread, *cmdPollSource, *looperId;
};
void android_main(struct android_app *app);
#endif
