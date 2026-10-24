#ifndef GBAEMU_JNI_SHARED_H
#define GBAEMU_JNI_SHARED_H

#include "emu_thread.h"
#include "ra.h"

/* What the two JNI files share (milestone 14). */
emu_t *jni_emu(void); /* the game session, or NULL (jni_bridge.c) */
ra_t *jni_ra(void);   /* RetroAchievements for the app's whole life, made on first use; NULL without memory (jni_ra.c) */

#endif
