/* Stub of the NDK's <sys/system_properties.h> for the host-side syntax check (tools/android/check).
 * Only what the port uses: the model name in the log header. */
#ifndef FP_NDKSTUB_SYSTEM_PROPERTIES_H
#define FP_NDKSTUB_SYSTEM_PROPERTIES_H

#define PROP_VALUE_MAX 92

#ifdef __cplusplus
extern "C" {
#endif
int __system_property_get(const char *name, char *value);
#ifdef __cplusplus
}
#endif

#endif
