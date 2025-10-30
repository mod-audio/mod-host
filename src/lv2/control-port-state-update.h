/*
  LV2 Control Port State Update extension
  Copyright 2025 Darkglass Electronics

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
   @file control-port-state-update.h
   C header for the LV2 Control Port State Update extension <http://www.darkglass.com/lv2/ns/lv2ext/control-port-state-update>.
*/

#ifndef LV2_CONTROL_PORT_STATE_UPDATE_H
#define LV2_CONTROL_PORT_STATE_UPDATE_H

#include <lv2/core/lv2.h>

#define LV2_CONTROL_PORT_STATE_UPDATE_URI    "http://www.darkglass.com/lv2/ns/lv2ext/control-port-state-update"
#define LV2_CONTROL_PORT_STATE_UPDATE_PREFIX LV2_CONTROL_PORT_STATE_UPDATE_URI "#"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

/** A status code for LV2_CONTROL_PORT_STATE_UPDATE_URI functions. */
typedef enum {
	LV2_CONTROL_PORT_STATE_UPDATE_SUCCESS           = 0,  /**< Completed successfully. */
	LV2_CONTROL_PORT_STATE_UPDATE_ERR_UNKNOWN       = 1,  /**< Unknown error. */
	LV2_CONTROL_PORT_STATE_UPDATE_ERR_INVALID_INDEX = 2   /**< Failed due to invalid port index. */
} LV2_Control_Port_State_Update_Status;

/** A control port state plugin can report to host. */
typedef enum {
	LV2_CONTROL_PORT_STATE_NONE     = 0,  /**< No special state / Remove any previously set states. */
	LV2_CONTROL_PORT_STATE_INACTIVE = 1,  /**< Inactive state (updates to port value are inaudible / ineffective). */
	LV2_CONTROL_PORT_STATE_BLOCKED  = 2   /**< Blocked state (updates to port value are ignored by the plugin and they should be blocked and ignored by the host). */
} LV2_Control_Port_State;

/**
 *  Opaque handle for LV2_CONTROL_PORT_STATE_UPDATE_URI feature.
 */
typedef void* LV2_Control_Port_State_Update_Handle;

/**
 * On instantiation, host must supply LV2_CONTROL_PORT_STATE_UPDATE_URI feature.
 * LV2_Feature::data must be pointer to LV2_Control_Port_State_Update.
*/
typedef struct _LV2_Control_Port_State_Update {
    /**
     *  Opaque host data.
     */
    LV2_Control_Port_State_Update_Handle handle;

    /**
     * update_state()
     *
     * Ask the host to change a plugin's control port's state.
     * Parameter handle MUST be the 'handle' member of this struct.
     * Parameter index is port index to change.
     * Parameter state is the new state of the control port.
     *
     * Returns status of update.
     *
     * The plugin MUST call this function during run().
     */
    LV2_Control_Port_State_Update_Status (*update_state)(LV2_Control_Port_State_Update_Handle handle,
                                                         uint32_t index,
                                                         LV2_Control_Port_State state);

} LV2_Control_Port_State_Update;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV2_CONTROL_PORT_STATE_UPDATE_H */
