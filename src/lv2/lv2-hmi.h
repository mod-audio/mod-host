/*
  LV2 HMI integration extension
  Copyright 2021 Filipe Coelho <falktx@falktx.com>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/**
   @file lv2-hmi.h
   C header for the LV2 HMI integration, as used in MOD Devices.
*/

#ifndef LV2_HMI_H_INCLUDED
#define LV2_HMI_H_INCLUDED

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define LV2_HMI_URI                 "http://moddevices.com/ns/hmi"
#define LV2_HMI_PREFIX              LV2_HMI_URI "#"
#define LV2_HMI__PluginNotification LV2_HMI_PREFIX "PluginNotification"
#define LV2_HMI__WidgetControl      LV2_HMI_PREFIX "WidgetControl"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

/**
 *  Opaque handle for addressing control given from host to plugins.
 */
typedef void* LV2_HMI_Addressing;

/**
 *  Opaque handle for LV2_HMI__WidgetControl feature.
 */
typedef void* LV2_HMI_WidgetControl_Handle;

/**
 *  ...
 */
typedef enum {
    LV2_HMI_AddressingType_ButtonOrFootswitch,
    LV2_HMI_AddressingType_EndlessKnob,
    LV2_HMI_AddressingType_PotentiometerKnob,
} LV2_HMI_AddressingType;

/**
 *  ...
 */
typedef enum {
    LV2_HMI_AddressingFlag_Coloured     = 1 << 0,
    LV2_HMI_AddressingFlag_MomentaryOn  = 1 << 1,
    LV2_HMI_AddressingFlag_MomentaryOff = 1 << 2,
    LV2_HMI_AddressingFlag_TapTempo     = 1 << 3,
} LV2_HMI_AddressingFlag;

/**
 *  ...
 */
typedef struct {
    LV2_HMI_AddressingType type;
    LV2_HMI_AddressingType flags;
    const char* label;
    float min, max;
    int steps;
} LV2_HMI_AddressingInfo;

/**
 *  ...
 */
typedef enum {
    LV2_HMI_Colour_Off,
    LV2_HMI_Colour_Red,
    LV2_HMI_Colour_Green,
    LV2_HMI_Colour_Blue,
    LV2_HMI_Colour_White,
} LV2_HMI_Colour;

/**
 * ... for plugin extension-data
 */
typedef struct {
    void (*addressed)(LV2_Handle handle, uint32_t index, LV2_HMI_Addressing addressing, const LV2_HMI_AddressingInfo* info);
    void (*unaddressed)(LV2_Handle handle, uint32_t index);
} LV2_HMI_PluginNotification;

/**
 * On instantiation, host must supply LV2_HMI__WidgetControl feature.
 * LV2_Feature::data must be pointer to LV2_HMI_WidgetControl.
 */
typedef struct _LV2_HMI_WidgetControl {
    /**
     *  Opaque host data.
     */
    LV2_HMI_WidgetControl_Handle handle;

    /**
     * ...
     */
    void (*set_led_colour)(LV2_HMI_WidgetControl_Handle handle,
                           LV2_HMI_Addressing addressing,
                           LV2_HMI_Colour led_color);

} LV2_HMI_WidgetControl;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV2_HMI_H_INCLUDED */
