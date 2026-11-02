/* stub: the NDK's <android/asset_manager.h>, enough for a -fsyntax-only pass of the platform layer.
 * D-136 uses it for one thing: reading the build's own scene scripts out of the APK's assets. */
#ifndef FP_STUB_ASSET_MANAGER_H
#define FP_STUB_ASSET_MANAGER_H
#include <sys/types.h>

typedef struct AAssetManager AAssetManager;
typedef struct AAsset AAsset;

enum { AASSET_MODE_UNKNOWN = 0, AASSET_MODE_RANDOM = 1, AASSET_MODE_STREAMING = 2, AASSET_MODE_BUFFER = 3 };

AAssetManager *AAssetManager_fromJava(void *env, void *assetManager);
AAsset *AAssetManager_open(AAssetManager *mgr, const char *filename, int mode);
void AAsset_close(AAsset *asset);
const void *AAsset_getBuffer(AAsset *asset);
off_t AAsset_getLength(AAsset *asset);
off_t AAsset_getRemainingLength(AAsset *asset);
int AAsset_openFileDescriptor(AAsset *asset, off_t *outStart, off_t *outLength);
int AAsset_read(AAsset *asset, void *buf, size_t count);
off_t AAsset_seek(AAsset *asset, off_t offset, int whence);
off_t AAsset_seek64(AAsset *asset, off64_t offset, int whence);

#endif
