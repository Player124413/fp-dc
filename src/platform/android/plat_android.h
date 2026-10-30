/* Shared between the Android platform layer and its audio backend. */
#ifndef FP_ANDROID_PLAT_H
#define FP_ANDROID_PLAT_H

/* <externalDataPath>/data: the imported game data package (Assets.dat tree) */
const char *plat_data_root(void);
/* <internalDataPath>: port.txt and the save file */
const char *plat_files_root(void);

#endif
