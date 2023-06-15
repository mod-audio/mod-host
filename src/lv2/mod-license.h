/*
  Copyright 2016 Robin Gareus <robin@gareus.org>
  Copyright 2016 Filipe Coelho <falktx@falktx.com>
  Copyright 2016-2023 MOD Audio UG

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
   @defgroup license License

   An interface for LV2 plugins to handle licensing and copy-protection,
   see <http://moddevices.com/ns/ext/license> for details.

   @{
*/

#ifndef MOD_LICENSE_H
#define MOD_LICENSE_H

#include <lv2/core/lv2.h>

#define MOD_LICENSE_URI    "http://moddevices.com/ns/ext/license"
#define MOD_LICENSE_PREFIX MOD_LICENSE_URI "#"

#define MOD_LICENSE__feature   MOD_LICENSE_PREFIX "feature"
#define MOD_LICENSE__interface MOD_LICENSE_PREFIX "interface"

#ifdef __cplusplus
extern "C" {
#endif

/**
   Status code for license functions.
*/
typedef enum {
        MOD_LICENSE_SUCCESS         = 0,  /**< Plugin is licensed. */
        MOD_LICENSE_ERR_UNKNOWN     = 1,  /**< Unknown error. */
        MOD_LICENSE_ERR_UNLICENSED  = 2,  /**< Plugin is not licensed - will run in restricted/demo mode. */
        MOD_LICENSE_ERR_UNSUPPORTED = 3   /**< Plugin does not support this license API. */
} MOD_License_Status;

/**
   MOD License Interface.

   When the plugin's extension_data is called with argument
   MOD_LICENSE__interface, the plugin MUST return an MOD_License_Interface
   structure, which remains valid for the lifetime of the plugin.

   The host can use the contained function pointers to query information about
   a plugin's license. This can be used by the host to provide information to
   the GUI (e.g. display name of licensee).
*/
typedef struct _MOD_License_Interface {
        /**
           Get the current license status for a plugin instance.

           @see MOD_License_Status

           @param instance The LV2 instance this is a method on.
        */
        int (*status)(LV2_Handle instance);

        /**
           Get the name of the licensee for a plugin instance.
           The caller is responsible for freeing the returned value with free().

           @param instance The LV2 instance this is a method on.
        */
        char* (*licensee)(LV2_Handle instance);
} MOD_License_Interface;

/**
   Opaque pointer to host data for MOD_License_Feature.
*/
typedef void* MOD_License_Handle;

/**
   MOD License Feature (MOD_LICENSE__feature)
*/
typedef struct _MOD_License_Feature {
        /**
           Opaque pointer to host data.

           This MUST be passed to license() and free() whenever they are called.
           Otherwise, it must not be interpreted in any way.
        */
        MOD_License_Handle handle;

        /**
           Ask the host about a license file for a specific uri
           (can be the plugin uri or a collection).

           The host will return the contents of the file, signed and encrypted,
           or NULL if no license exists.

           The plugin must call free() on the returned data.

           @param handle Must be the handle member of this struct.
           @param license_uri The uri for which to ask a license for.
        */
        char* (*license)(MOD_License_Handle handle, const char* license_uri);

        /**
           Free the returned data of a license() call.

           @param license The data to be freed.
        */
        void (*free)(MOD_License_Handle handle, char* license);
} MOD_License_Feature;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* MOD_LICENSE_H */

/**
   @}
*/
