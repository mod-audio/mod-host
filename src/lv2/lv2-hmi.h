/*
  LV2 HMI integration extension
  Copyright 2021-2023 Filipe Coelho <falktx@falktx.com>

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

#include <lv2/core/lv2.h>

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
 *  The capabilities of an addressing,
 *  indicating which functions from LV2_HMI_WidgetControl will actually do something.
 */
typedef enum {
    LV2_HMI_AddressingCapability_LED        = 1 << 0,
    LV2_HMI_AddressingCapability_Label      = 1 << 1,
    LV2_HMI_AddressingCapability_Value      = 1 << 2,
    LV2_HMI_AddressingCapability_Unit       = 1 << 3,
    LV2_HMI_AddressingCapability_Indicator  = 1 << 4
} LV2_HMI_AddressingCapabilities;

/**
 *  The flags of an addressing, exposed to the plugin so it can change its behaviour if needed.
 */
typedef enum {
   /**
    *  Parameter is a coloured list, instead of monotone.
    *  Currently unused.
    */
    LV2_HMI_AddressingFlag_Coloured = 1 << 0,

   /**
    *  The addressed parameter is acting in momentary mode instead of toggle.
    *  This means the parameter value will change on each press and release of the hardware control.
    *  By itself this flag indicates "momentary-on" mode, where press&hold is ON and release if OFF.
    */
    LV2_HMI_AddressingFlag_Momentary = 1 << 1,

   /**
    *  Relates to LV2_HMI_AddressingFlag_Momentary.
    *  This flag indicates the reverse, so it is "momentary-off" mode, where press&hold is OFF and release if ON.
    */
    LV2_HMI_AddressingFlag_Reverse = 1 << 2,

   /**
    *  Parameter is mapped to tap-tempo.
    *  Currently unused.
    */
    LV2_HMI_AddressingFlag_TapTempo = 1 << 3
} LV2_HMI_AddressingFlags;

/**
 *  The (pre-set) blink timings of an LED.
 *  Conveniently defined as an enum to ensure consistency between plugins.
 */
typedef enum {
    LV2_HMI_LED_Blink_None = 0,
    LV2_HMI_LED_Blink_Slow = -1,
    LV2_HMI_LED_Blink_Mid = -2,
    LV2_HMI_LED_Blink_Fast = -3
} LV2_HMI_LED_Blink;

/**
 *  The (pre-set) brightness values of an LED.
 *  Conveniently defined as an enum to ensure consistency between plugins.
 */
typedef enum {
    LV2_HMI_LED_Brightness_None = 0,
    LV2_HMI_LED_Brightness_Low = -1,
    LV2_HMI_LED_Brightness_Mid = -2,
    LV2_HMI_LED_Brightness_High = -3,
    LV2_HMI_LED_Brightness_Normal = LV2_HMI_LED_Brightness_High
} LV2_HMI_LED_Brightness;

/**
 *  The (pre-set) colour values of an LED.
 *  Contrary to LED brightness, plugins can not use arbitrary values for colour.
 */
typedef enum {
    LV2_HMI_LED_Colour_Off,
    LV2_HMI_LED_Colour_Red,
    LV2_HMI_LED_Colour_Green,
    LV2_HMI_LED_Colour_Blue,
    LV2_HMI_LED_Colour_Cyan,
    LV2_HMI_LED_Colour_Magenta,
    LV2_HMI_LED_Colour_Yellow,
    LV2_HMI_LED_Colour_White
} LV2_HMI_LED_Colour;

/**
 *  The color values of the screen popup.
 *  Conveniently defined as an enum to ensure consistency between plugins.
 */
typedef enum {
    LV2_HMI_Popup_Style_Normal = 0,
    LV2_HMI_Popup_Style_Inverted = 1
} LV2_HMI_Popup_Style;

/**
 *  Parameter addressing information passed to the plugin, as triggered by the host (via user input).
 */
typedef struct {
   /**
    *  Capabilities of this addressing, indicating what can be set by the plugin.
    */
    LV2_HMI_AddressingCapabilities caps;

   /**
    *  Extra addressing flags.
    */
    LV2_HMI_AddressingFlags flags;

   /**
    *  The label given by the user as a way to identify this addressing on the hardware display.
    */
    const char* label;

   /**
    *  Custom parameter ranges as set by the user, or the default min and max from the parameter being addressed.
    */
    float min, max;

   /**
    *  Number of steps requested by the user, or a device default.
    *  On Dwarf this value is 201 by default, 33 on all other units.
    */
    int steps;

} LV2_HMI_AddressingInfo;

/**
 * Interface to be implemented by the plugin, returned on extension_data() when URI is LV2_HMI__PluginNotification.
 */
typedef struct {
   /**
    * Notifies the plugin about a hardware addressing made by the user to a plugin parameter.
    * The @a addressing variable should be saved by the plugin and used as argument to widget control functions.
    */
    void (*addressed)(LV2_Handle handle, uint32_t index, LV2_HMI_Addressing addressing, const LV2_HMI_AddressingInfo* info);

   /**
    * Notifies the plugin about a hardware addressing being removed.
    * The plugin MUST NOT call any widget control functions belonging to this parameter after this point.
    */
    void (*unaddressed)(LV2_Handle handle, uint32_t index);

} LV2_HMI_PluginNotification;

/**
 * Compatibility size for LV2_HMI_WidgetControl.
 */
#define LV2_HMI_WIDGETCONTROL_SIZE_BASE (sizeof(size_t) + sizeof(void*) * 7)

/**
 * Size check for LV2_HMI_WidgetControl::popup_message call.
 */
#define LV2_HMI_WIDGETCONTROL_SIZE_POPUP_MESSAGE (LV2_HMI_WIDGETCONTROL_SIZE_BASE + sizeof(void*))

/**
 * On instantiation, host must supply LV2_HMI__WidgetControl feature.
 * LV2_Feature::data must be pointer to LV2_HMI_WidgetControl.
 */
typedef struct {
    /**
     *  Opaque host data.
     */
    LV2_HMI_WidgetControl_Handle handle;

    /**
     * The size of this struct.
     * To be used in future revisions of this API for backwards compatibility, can be ignored for now.
     */
    size_t size;

    /**
     * Set the LED color, with optional blink timing in milliseconds from 0 to 5000.
     * Using 0 as @a on_blink_time means no blinking.
     *
     * The plugin SHOULD use values from LV2_HMI_LED_Blink unless it has very good reasons not to do so.
     * When using values from LV2_HMI_LED_Blink, only the @a on_blink_time needs to be set, @a off_blink_time can be 0.
     */
    void (*set_led_with_blink)(LV2_HMI_WidgetControl_Handle handle,
                               LV2_HMI_Addressing addressing,
                               LV2_HMI_LED_Colour color,
                               int on_blink_time,
                               int off_blink_time);

    /**
     * Set the LED color, with optional brightness from 0 to 100.
     * Using 0 as @a brightness means to turn off the LED.
     *
     * The plugin SHOULD use values from LV2_HMI_LED_Brightness unless it has very good reasons not to do so.
     */
    void (*set_led_with_brightness)(LV2_HMI_WidgetControl_Handle handle,
                                    LV2_HMI_Addressing addressing,
                                    LV2_HMI_LED_Colour color,
                                    int brightness);

    /**
     * Set the label on the display.
     */
    void (*set_label)(LV2_HMI_WidgetControl_Handle handle,
                      LV2_HMI_Addressing addressing,
                      const char* label);

    /**
     * Set the value on the display.
     */
    void (*set_value)(LV2_HMI_WidgetControl_Handle handle,
                      LV2_HMI_Addressing addressing,
                      const char* value);

    /**
     * Set the unit on the display.
     */
    void (*set_unit)(LV2_HMI_WidgetControl_Handle handle,
                     LV2_HMI_Addressing addressing,
                     const char* unit);

    /**
     * Set the indicator position on the display, using normalized 0-1 values.
     */
    void (*set_indicator)(LV2_HMI_WidgetControl_Handle handle,
                          LV2_HMI_Addressing addressing,
                          const float indicator_pos);

    /**
     * Open a popup with a custom text message.
     * @note This function must only be used if size >= LV2_HMI_WIDGETCONTROL_SIZE_POPUP_MESSAGE
     */
    void (*popup_message)(LV2_HMI_WidgetControl_Handle handle,
                          LV2_HMI_Addressing addressing,
                          LV2_HMI_Popup_Style style,
                          const char* title,
                          const char* message);

} LV2_HMI_WidgetControl;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV2_HMI_H_INCLUDED */
