/* stub: <android/input.h> */
#ifndef FP_STUB_INPUT
#define FP_STUB_INPUT
#include <stddef.h>
#include <stdint.h>
typedef struct AInputEvent AInputEvent;
enum { AINPUT_EVENT_TYPE_KEY = 1, AINPUT_EVENT_TYPE_MOTION = 2 };
enum { AINPUT_SOURCE_TOUCHSCREEN = 0x00001002, AINPUT_SOURCE_JOYSTICK = 0x01000010 };
enum { AKEY_EVENT_ACTION_DOWN = 0, AKEY_EVENT_ACTION_UP = 1 };
enum { AMOTION_EVENT_ACTION_DOWN = 0, AMOTION_EVENT_ACTION_UP = 1, AMOTION_EVENT_ACTION_MOVE = 2,
       AMOTION_EVENT_ACTION_CANCEL = 3, AMOTION_EVENT_ACTION_POINTER_DOWN = 5,
       AMOTION_EVENT_ACTION_POINTER_UP = 6 };
#define AMOTION_EVENT_ACTION_MASK 0xff
#define AMOTION_EVENT_ACTION_POINTER_INDEX_MASK 0xff00
#define AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT 8
enum { AMOTION_EVENT_AXIS_X = 0, AMOTION_EVENT_AXIS_Y = 1, AMOTION_EVENT_AXIS_HAT_X = 15,
       AMOTION_EVENT_AXIS_HAT_Y = 16, AMOTION_EVENT_AXIS_LTRIGGER = 17, AMOTION_EVENT_AXIS_RTRIGGER = 18 };
int32_t AInputEvent_getType(const AInputEvent *event);
int32_t AInputEvent_getSource(const AInputEvent *event);
int32_t AMotionEvent_getAction(const AInputEvent *event);
size_t AMotionEvent_getPointerCount(const AInputEvent *event);
int32_t AMotionEvent_getPointerId(const AInputEvent *event, size_t pointer_index);
float AMotionEvent_getX(const AInputEvent *event, size_t pointer_index);
float AMotionEvent_getY(const AInputEvent *event, size_t pointer_index);
float AMotionEvent_getAxisValue(const AInputEvent *event, int32_t axis, size_t pointer_index);
int32_t AKeyEvent_getKeyCode(const AInputEvent *event);
int32_t AKeyEvent_getAction(const AInputEvent *event);
#endif
