/* stub: <jni.h>, only what plat_android.c uses */
#ifndef FP_STUB_JNI
#define FP_STUB_JNI
#include <stdarg.h>
#include <stdint.h>
typedef int32_t jint;
typedef uint8_t jboolean;
typedef void *jobject;
typedef void *jclass;
typedef void *jmethodID;
typedef void *jfieldID;
typedef void *jstring;
typedef void *jarray;
typedef const struct JNINativeInterface *JNIEnv;
typedef const struct JNIInvokeInterface *JavaVM;
struct JNINativeInterface {
    void *reserved0, *reserved1, *reserved2, *reserved3;
    jint (*GetVersion)(JNIEnv *);
    jclass (*FindClass)(JNIEnv *, const char *);
    jmethodID (*GetMethodID)(JNIEnv *, jclass, const char *, const char *);
    jobject (*CallObjectMethod)(JNIEnv *, jobject, jmethodID, ...);
    void (*CallVoidMethod)(JNIEnv *, jobject, jmethodID, ...);
    void *reserved[11];
    jclass (*GetObjectClass)(JNIEnv *, jobject);
    jboolean (*IsInstanceOf)(JNIEnv *, jobject, jclass);
    jmethodID (*GetStaticMethodID)(JNIEnv *, jclass, const char *, const char *);
    void *reserved2x[15];
    jboolean (*ExceptionCheck)(JNIEnv *);
    void (*ExceptionClear)(JNIEnv *);
    void *reserved3x[9];
    const char *(*GetStringUTFChars)(JNIEnv *, jstring, jboolean *);
    void (*ReleaseStringUTFChars)(JNIEnv *, jstring, const char *);
    void (*DeleteLocalRef)(JNIEnv *, jobject);
};
struct JNIInvokeInterface {
    void *reserved0, *reserved1, *reserved2;
    jint (*DestroyJavaVM)(JavaVM *);
    jint (*AttachCurrentThread)(JavaVM *, JNIEnv **, void *);
    jint (*DetachCurrentThread)(JavaVM *);
    jint (*GetEnv)(JavaVM *, void **, jint);
};
#define JNIEXPORT
#define JNICALL
#define JNI_TRUE 1
#define JNI_FALSE 0
#endif
