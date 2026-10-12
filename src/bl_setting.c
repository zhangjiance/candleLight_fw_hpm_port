/*
 * bl_setting.c - see bl_setting.h.
 */
#include "bl_setting.h"

/* Filled in by the build (CMake -DBL_SETTING_APP_VERSION="x.y.z").  A version
 * is not required for the hand-shake, so fall back to 0.0.0 when undefined. */
#ifndef BL_SETTING_APP_VERSION_MAJOR
#define BL_SETTING_APP_VERSION_MAJOR 0
#endif
#ifndef BL_SETTING_APP_VERSION_MINOR
#define BL_SETTING_APP_VERSION_MINOR 0
#endif
#ifndef BL_SETTING_APP_VERSION_PATCH
#define BL_SETTING_APP_VERSION_PATCH 0
#endif

#define BL_BLOCK ((volatile bl_setting_t *)BL_SETTING_ADDR)

bl_setting_t *bl_setting(void)
{
    return (bl_setting_t *)BL_SETTING_ADDR;
}

void bl_setting_app_init(void)
{
    volatile bl_setting_t *s = BL_BLOCK;

    if (s->magic != BL_SETTING_MAGIC) {
        /* First boot after a fresh flash: (re)initialise the whole block. */
        s->magic = BL_SETTING_MAGIC;
        s->data0 = 0;
        s->bl_version.major = 0;
        s->bl_version.minor = 0;
        s->bl_version.patch = 0;
        s->bl_version.reserved = 0;
        s->app_version.major = 0;
        s->app_version.minor = 0;
        s->app_version.patch = 0;
        s->app_version.reserved = 0;
    }

    /* A successful application boot resets the failure counter. */
    s->fail_cnt = 0;

    s->app_version.major = BL_SETTING_APP_VERSION_MAJOR;
    s->app_version.minor = BL_SETTING_APP_VERSION_MINOR;
    s->app_version.patch = BL_SETTING_APP_VERSION_PATCH;
}

void bl_setting_request_bootloader(void)
{
    volatile bl_setting_t *s = BL_BLOCK;

    s->magic = BL_SETTING_MAGIC;
    s->keep_bootloader = 1;
}
