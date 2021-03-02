/**
 * @file usb_moded-control.c
 *
 * Copyright (c) 2013 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the Lesser GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the Lesser GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "usb_moded-control.h"

#include "usb_moded-config-private.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-dyn-config.h"
#include "usb_moded-log.h"
#include "usb_moded-modes.h"
#include "usb_moded-worker.h"
#include "usb_moded-user.h"

#include <string.h>
#include <stdlib.h>

#ifdef SYSTEMD
# include <systemd/sd-login.h>
#endif

/* Sanity check, configure should take care of this */
#if defined SAILFISH_ACCESS_CONTROL && !defined SYSTEMD
# error if SAILFISH_ACCESS_CONTROL is defined, SYSTEMD must be defined as well
#endif

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CONTROL
 * ------------------------------------------------------------------------- */

void           control_rethink_usb_charging_fallback(void);
void           control_user_changed                 (void);
const char    *control_get_external_mode            (void);
static void    control_set_external_mode            (const char *mode);
void           control_clear_external_mode          (void);
static void    control_update_external_mode         (void);
const char    *control_get_target_mode              (void);
static void    control_set_target_mode              (const char *mode);
void           control_clear_target_mode            (void);
const char    *control_get_usb_mode                 (void);
void           control_clear_internal_mode          (void);
void           control_set_usb_mode                 (const char *mode);
void           control_mode_switched                (const char *mode);
void           control_select_usb_mode              (void);
void           control_select_usb_mode_ex           (bool user_changed);
void           control_set_cable_state              (cable_state_t cable_state);
cable_state_t  control_get_cable_state              (void);
void           control_clear_cable_state            (void);
bool           control_get_connection_state         (void);
uid_t          control_get_current_user             (void);
uid_t          control_get_user_for_mode            (void);
void           control_set_user_for_mode            (uid_t uid);

/* ========================================================================= *
 * Data
 * ========================================================================= */

/* The external mode;
 *
 * What was the last current mode signaled over D-Bus.
 */
static char *control_external_mode = NULL;

/* The target mode;
 *
 * What was the last target mode signaled over D-Bus.
 */
static char *control_target_mode = NULL;

/** The logical mode name
 *
 * Full set of valid modes can occur here
 */
static char *control_internal_mode = NULL;

/** Connection status
 *
 * Access only via:
 * - control_set_cable_state()
 * - control_get_connection_state()
 */
static cable_state_t control_cable_state = CABLE_STATE_UNKNOWN;

/** Uid of the user that has set current USB mode
 */
static uid_t control_user_for_mode = UID_UNKNOWN;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/** Get uid of the user that set the current mode
 *
 *  @return uid of user when current mode was set
 */
uid_t
control_get_user_for_mode(void)
{
    return control_user_for_mode;
}

/** Set the uid of the user that set the current mode
 *
 *  @param uid of current user
 */
void
control_set_user_for_mode(uid_t uid)
{
    LOG_REGISTER_CONTEXT;

    log_debug("control_user_for_mode: %d -> %d",
              (int)control_user_for_mode, (int)uid);
    control_user_for_mode = uid;
}

/** Check if we can/should enable charging fallback mode
 *
 * Called when user is changed
 */
void
control_user_changed(void)
{
    LOG_REGISTER_CONTEXT;

    /* Cable must be connected to a pc */
    if( control_get_cable_state() != CABLE_STATE_PC_CONNECTED )
        return;

    /* Don't leave developer mode if keep developer mode is set */
    if( !strcmp(control_get_usb_mode(), MODE_DEVELOPER) &&
        usbmoded_get_keep_developer_mode() )
        return;

    bool user_changed = (control_get_current_user() != control_get_user_for_mode());
    log_debug("control_user_changed: user_changed %d", user_changed);
    if (user_changed)
        control_select_usb_mode_ex(user_changed);
}

/** Check if we can/should leave charging fallback mode
 *
 * Called when device lock status, or device status (dsme)
 * changes.
 */
void
control_rethink_usb_charging_fallback(void)
{
    LOG_REGISTER_CONTEXT;

    /* Cable must be connected to a pc */
    if( control_get_cable_state() != CABLE_STATE_PC_CONNECTED )
        goto EXIT;

    /* Switching can happen only from MODE_UNDEFINED
     * or MODE_CHARGING_FALLBACK */
    const char *usb_mode = control_get_usb_mode();

    if( strcmp(usb_mode, MODE_UNDEFINED) &&
        strcmp(usb_mode, MODE_CHARGING_FALLBACK) )
        goto EXIT;

    if( !usbmoded_can_export() ) {
        log_notice("exporting data not allowed; stay in %s", usb_mode);
        goto EXIT;
    }

    log_debug("attempt to leave %s", usb_mode);
    control_select_usb_mode();

EXIT:
    return;
}

const char *control_get_external_mode(void)
{
    LOG_REGISTER_CONTEXT;

    return control_external_mode ?: MODE_UNDEFINED;
}

static void control_set_external_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    gchar *previous = control_external_mode;
    if( !g_strcmp0(previous, mode) )
        goto EXIT;

    log_debug("external_mode: %s -> %s",
              previous, mode);

    control_external_mode = g_strdup(mode);
    g_free(previous);

    // DO THE DBUS BROADCAST

    if( !strcmp(control_external_mode, MODE_ASK) ) {
        /* send signal, mode will be set when the dialog service calls
         * the set_mode method call. */
        umdbus_send_event_signal(USB_CONNECTED_DIALOG_SHOW);
    }

    umdbus_send_current_state_signal(control_external_mode);

    if( strcmp(control_external_mode, MODE_BUSY) ) {
        /* Stable state reached. Synchronize target state.
         *
         * Note that normally this ends up being a nop,
         * but might be needed if the originally scheduled
         * target could not be reached due to errors / user
         * disconnecting the cable.
         */
        control_set_target_mode(control_external_mode);
    }

EXIT:
    return;
}

void control_clear_external_mode(void)
{
    LOG_REGISTER_CONTEXT;

    g_free(control_external_mode),
        control_external_mode = 0;
}

static void control_update_external_mode(void)
{
    LOG_REGISTER_CONTEXT;

    const char *internal_mode = control_get_usb_mode();
    const char *external_mode = common_map_mode_to_external(internal_mode);

    control_set_external_mode(external_mode);
}

const char *control_get_target_mode(void)
{
    LOG_REGISTER_CONTEXT;

    return control_target_mode ?: MODE_UNDEFINED;
}

static void control_set_target_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    gchar *previous = control_target_mode;
    if( !g_strcmp0(previous, mode) )
        goto EXIT;

    log_debug("target_mode: %s -> %s",
              previous, mode);

    control_target_mode = g_strdup(mode);
    g_free(previous);

    umdbus_send_target_state_signal(control_target_mode);

EXIT:
    return;
}

void control_clear_target_mode(void)
{
    LOG_REGISTER_CONTEXT;

    g_free(control_target_mode),
        control_target_mode = 0;
}

/** get the usb mode
 *
 * @return the currently set mode
 *
 */
const char * control_get_usb_mode(void)
{
    LOG_REGISTER_CONTEXT;

    return control_internal_mode;
}

void control_clear_internal_mode(void)
{
    LOG_REGISTER_CONTEXT;

    g_free(control_internal_mode),
        control_internal_mode = 0;
}

/** set the usb mode
 *
 * @param mode The requested USB mode
 */
void control_set_usb_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    gchar *previous = control_internal_mode;
    if( !g_strcmp0(previous, mode) )
        goto EXIT;

    log_debug("internal_mode: %s -> %s",
              previous, mode);

    control_internal_mode = g_strdup(mode);
    g_free(previous);

    /* Update target mode before declaring busy */
    control_set_target_mode(control_internal_mode);

    /* Invalidate current mode for the duration of mode transition */
    control_set_external_mode(MODE_BUSY);

    /* Set mode owner to unknown until it has been changed */
    control_set_user_for_mode(UID_UNKNOWN);

    /* Propagate down to gadget config */
    worker_request_hardware_mode(control_internal_mode);

EXIT:
    return;
}

/* Worker thread has finished mode switch
 *
 * @param mode The activated USB mode
 */
void control_mode_switched(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    /* Update state data - without retriggering the worker thread
     */
    if( g_strcmp0(control_internal_mode, mode) ) {
        log_debug("internal_mode: %s -> %s",
                  control_internal_mode, mode);
        g_free(control_internal_mode),
            control_internal_mode = g_strdup(mode);
    }

    /* Propagate up to D-Bus */
    control_update_external_mode();
    control_set_user_for_mode(control_get_current_user());

    return;
}

/** set the chosen usb state
 *
 * gauge what mode to enter and then call control_set_usb_mode()
 *
 */
void control_select_usb_mode_ex(bool user_changed)
{
    LOG_REGISTER_CONTEXT;

    char *mode_to_set = 0;

    if( usbmoded_get_rescue_mode() ) {
        log_debug("Entering rescue mode!\n");
        control_set_usb_mode(MODE_DEVELOPER);
        goto EXIT;
    }

    if( usbmoded_get_diag_mode() ) {
        /* Assumption is that in diag-mode there is only
         * one mode configured i.e. list head is diag-mode. */
        GList *iter = usbmoded_get_modelist();
        if( !iter ) {
            log_err("Diagnostic mode is not configured!");
        }
        else {
            modedata_t *data = iter->data;
            log_debug("Entering diagnostic mode!");
            control_set_usb_mode(data->mode_name);
        }
        goto EXIT;
    }

    uid_t current_user = control_get_current_user();
    /* If current user could not be determined, assume that device is
     * booting up or between sessions. Therefore we either must use whatever
     * is configured as global mode or let device lock to prevent the mode
     * so that it can be set again once the device is unlocked */
    mode_to_set = config_get_mode_setting((current_user == UID_UNKNOWN) ? 0 : current_user);

    /* If there is only one allowed mode, use it without
     * going through ask-mode */
    if( !strcmp(MODE_ASK, mode_to_set) ) {
        if( current_user == UID_UNKNOWN ) {
            /* Use charging only if no user has been seen */
            free(mode_to_set), mode_to_set = 0;
        } else {
            // FIXME free() vs g_free() conflict
            gchar *available = common_get_mode_list(AVAILABLE_MODES_LIST, current_user);
            if( *available && !strchr(available, ',') ) {
                free(mode_to_set), mode_to_set = available, available = 0;
            }
            g_free(available);
        }
    }

    if( mode_to_set && usbmoded_can_export() && !user_changed ) {
        control_set_usb_mode(mode_to_set);
    }
    else {
        /* config is corrupted or we do not have a mode configured, fallback to charging
         * We also fall back here in case the device is locked and we do not
         * export the system contents, if we are in acting dead mode or changing user.
         */
        control_set_usb_mode(MODE_CHARGING_FALLBACK);
    }
EXIT:
    free(mode_to_set);
}

void control_select_usb_mode(void)
{
    control_select_usb_mode_ex(false);
}

/** set the usb connection status
 *
 * @param cable_state CABLE_STATE_DISCONNECTED, ...
 */
void control_set_cable_state(cable_state_t cable_state)
{
    LOG_REGISTER_CONTEXT;

    cable_state_t prev = control_cable_state;
    control_cable_state = cable_state;

    if( control_cable_state == prev )
        goto EXIT;

    log_debug("control_cable_state: %s -> %s",
              cable_state_repr(prev),
              cable_state_repr(control_cable_state));

    switch( control_cable_state ) {
    default:
    case CABLE_STATE_DISCONNECTED:
        control_set_usb_mode(MODE_UNDEFINED);
        break;
    case CABLE_STATE_CHARGER_CONNECTED:
        control_set_usb_mode(MODE_CHARGER);
        break;
    case CABLE_STATE_PC_CONNECTED:
        control_select_usb_mode();
        break;
    }

EXIT:
    return;
}

/** get the usb connection status
 *
 * @return CABLE_STATE_DISCONNECTED, ...
 */
cable_state_t control_get_cable_state(void)
{
    LOG_REGISTER_CONTEXT;

    return control_cable_state;
}

void control_clear_cable_state(void)
{
    LOG_REGISTER_CONTEXT;

    control_cable_state = CABLE_STATE_UNKNOWN;
}

/** Get if the cable (pc or charger) is connected or not
 *
 * @ return true if connected, false if disconnected
 */
bool control_get_connection_state(void)
{
    LOG_REGISTER_CONTEXT;

    bool connected = false;
    switch( control_get_cable_state() ) {
    case CABLE_STATE_CHARGER_CONNECTED:
    case CABLE_STATE_PC_CONNECTED:
        connected = true;
        break;
    default:
        break;
    }
    return connected;
}

/**
 * Get the user using the device
 *
 * When built without Sailfish access control support,
 * this returns root's uid (0) unconditionally.
 *
 * @return current user on seat0 or UID_UNKNOWN if it can not be determined
 */
uid_t control_get_current_user(void)
{
    return user_get_current_user();
}
