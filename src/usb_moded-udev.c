/**
 * @file usb_moded-udev.c
 *
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2018 Jolla Ltd.
 *
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
 * @author: Philippe De Swert <phdeswer@lumi.maa>
 * @author: Tapio Rantala <ext-tapio.rantala@nokia.com>
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Jarko Poutiainen <jarko.poutiainen@jollamobile.com>
 * @author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <locale.h>
#include <unistd.h>

#include <poll.h>

#include <libudev.h>

#include <glib.h>

#include "usb_moded-log.h"
#include "usb_moded-config-private.h"
#include "usb_moded-udev.h"
#include "usb_moded.h"
#include "usb_moded-modes.h"

/* ========================================================================= *
 * Types
 * ========================================================================= */

typedef enum {
    CABLE_STATE_UNKNOWN,
    CABLE_STATE_DISCONNECTED,
    CABLE_STATE_CHARGER_CONNECTED,
    CABLE_STATE_PC_CONNECTED,
    CABLE_STATE_NUMOF
} cable_state_t;

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- cable -- */

static gboolean      cable_state_timer_cb   (gpointer aptr);
static void          cable_state_stop_timer (void);
static void          cable_state_start_timer(void);
static bool          cable_state_connected  (void);
static cable_state_t cable_state_get        (void);
static void          cable_state_set        (cable_state_t state);
static void          cable_state_changed    (void);
static void          cable_state_from_udev  (cable_state_t state);

/* -- umudev -- */

static void     umudev_io_error_cb          (gpointer data);
static gboolean umudev_io_input_cb          (GIOChannel *iochannel, GIOCondition cond, gpointer data);
static void     umudev_parse_properties     (struct udev_device *dev, bool initial);
static int      umudev_score_as_power_supply(const char *syspath);
gboolean        umudev_init                 (void);
void            umudev_quit                 (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static const char * const cable_state_name[CABLE_STATE_NUMOF] = {
    [CABLE_STATE_UNKNOWN]           = "unknown",
    [CABLE_STATE_DISCONNECTED]      = "disconnected",
    [CABLE_STATE_CHARGER_CONNECTED] = "charger_connected",
    [CABLE_STATE_PC_CONNECTED]      = "pc_connected",
};

/* global variables */
static struct udev         *umudev_object     = 0;
static struct udev_monitor *umudev_monitor    = 0;
static gchar               *umudev_sysname    = 0;
static guint                umudev_watch_id   = 0;
static bool                 umudev_in_cleanup = false;

/** Cable state as evaluated from udev events */
static cable_state_t cable_state_current  = CABLE_STATE_UNKNOWN;

/** Cable state considered active by usb-moded */
static cable_state_t cable_state_active   = CABLE_STATE_UNKNOWN;

/** Previously active cable state */
static cable_state_t cable_state_previous = CABLE_STATE_UNKNOWN;

/** Timer id for delaying: reported by udev -> active in usb-moded */
static guint cable_state_timer_id = 0;

/* ========================================================================= *
 * cable state
 * ========================================================================= */

static gboolean cable_state_timer_cb(gpointer aptr)
{
    (void)aptr;
    cable_state_timer_id = 0;

    log_debug("trigger delayed transfer to: %s",
              cable_state_name[cable_state_current]);
    cable_state_set(cable_state_current);
    return FALSE;
}

static void cable_state_stop_timer(void)
{
    if( cable_state_timer_id ) {
        log_debug("cancel delayed transfer to: %s",
                  cable_state_name[cable_state_current]);
        g_source_remove(cable_state_timer_id),
            cable_state_timer_id = 0;
    }
}

static void cable_state_start_timer(void)
{
    if( !cable_state_timer_id ) {
        log_debug("schedule delayed transfer to: %s",
                  cable_state_name[cable_state_current]);
        cable_state_timer_id = g_timeout_add(1500, cable_state_timer_cb, 0);
    }
}

static bool
cable_state_connected(void)
{
    bool connected = false;
    switch( cable_state_get() ) {
    default:
        break;
    case CABLE_STATE_CHARGER_CONNECTED:
    case CABLE_STATE_PC_CONNECTED:
        connected = true;
        break;
    }
    return connected;
}

static cable_state_t cable_state_get(void)
{
    return cable_state_active;
}

static void cable_state_set(cable_state_t state)
{
    cable_state_stop_timer();

    if( cable_state_active == state )
        goto EXIT;

    cable_state_previous = cable_state_active;
    cable_state_active   = state;

    log_debug("cable_state: %s -> %s",
              cable_state_name[cable_state_previous],
              cable_state_name[cable_state_active]);

    cable_state_changed();

EXIT:
    return;
}

static void cable_state_changed(void)
{
    /* The rest of usb-moded separates charger
     * and pc connection states... make single
     * state tracking compatible with that. */

    /* First handle pc/charger disconnect based
     * on previous state.
     */
    switch( cable_state_previous ) {
    default:
    case CABLE_STATE_DISCONNECTED:
        /* dontcare */
        break;
    case CABLE_STATE_CHARGER_CONNECTED:
        log_debug("*** HANDLE CHARGER DISCONNECT");
        usbmoded_set_charger_connected(false);
        break;
    case CABLE_STATE_PC_CONNECTED:
        log_debug("*** HANDLE PC DISCONNECT");
        usbmoded_set_usb_connected(false);
        break;
    }

    /* Then handle pc/charger sconnect based
     * on current state.
     */

    switch( cable_state_active ) {
    default:
    case CABLE_STATE_DISCONNECTED:
        /* dontcare */
        break;
    case CABLE_STATE_CHARGER_CONNECTED:
        log_debug("*** HANDLE CHARGER CONNECT");
        usbmoded_set_charger_connected(true);
        break;
    case CABLE_STATE_PC_CONNECTED:
        log_debug("*** HANDLE PC CONNECT");
        usbmoded_set_usb_connected(true);
        break;
    }
}

static void cable_state_from_udev(cable_state_t curr)
{
    cable_state_t prev = cable_state_current;
    cable_state_current = curr;

    if( prev == curr )
        goto EXIT;

    log_debug("reported cable state: %s -> %s",
              cable_state_name[prev],
              cable_state_name[curr]);

    if( curr == CABLE_STATE_PC_CONNECTED && prev != CABLE_STATE_UNKNOWN )
        cable_state_start_timer();
    else
        cable_state_set(curr);

EXIT:
    return;
}

/* ========================================================================= *
 * legacy code
 * ========================================================================= */

static void umudev_io_error_cb(gpointer data)
{
    (void)data;

    /* we do not want to restart when we try to clean up */
    if( !umudev_in_cleanup ) {
        log_debug("USB connection watch destroyed, restarting it\n!");
        /* restart trigger */
        umudev_quit();
        umudev_init();
    }
}

static gboolean umudev_io_input_cb(GIOChannel *iochannel, GIOCondition cond, gpointer data)
{
    (void)iochannel;
    (void)data;

    gboolean continue_watching = TRUE;

    /* No code paths are allowed to bypass the usbmoded_release_wakelock() call below */
    usbmoded_acquire_wakelock(USB_MODED_WAKELOCK_PROCESS_INPUT);

    if( cond & G_IO_IN )
    {
        /* This normally blocks but G_IO_IN indicates that we can read */
        struct udev_device *dev = udev_monitor_receive_device(umudev_monitor);
        if( !dev )
        {
            /* if we get something else something bad happened stop watching to avoid busylooping */
            continue_watching = FALSE;
        }
        else
        {
            /* check if it is the actual device we want to check */
            if( !strcmp(umudev_sysname, udev_device_get_sysname(dev)) )
            {
                if( !strcmp(udev_device_get_action(dev), "change") )
                {
                    umudev_parse_properties(dev, false);
                }
            }

            udev_device_unref(dev);
        }
    }

    if( cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL) )
    {
        /* Unhandled errors turn io watch to virtual busyloop too */
        continue_watching = FALSE;
    }

    if( !continue_watching && umudev_watch_id )
    {
        umudev_watch_id = 0;
        log_crit("udev io watch disabled");
    }

    usbmoded_release_wakelock(USB_MODED_WAKELOCK_PROCESS_INPUT);

    return continue_watching;
}

static void umudev_parse_properties(struct udev_device *dev, bool initial)
{
    (void)initial;

    /* udev properties we are interested in */
    const char *power_supply_present = 0;
    const char *power_supply_online  = 0;
    const char *power_supply_type    = 0;

    /* Assume there is no usb connection until proven otherwise */
    bool connected  = false;

    /* Unless debug logging has been request via command line,
     * suppress warnings about potential property issues and/or
     * fallback strategies applied (to avoid spamming due to the
     * code below seeing the same property values over and over
     * again also in stable states).
     */
    bool warnings = log_p(LOG_DEBUG);

    /*
     * Check for present first as some drivers use online for when charging
     * is enabled
     */
    power_supply_present = udev_device_get_property_value(dev, "POWER_SUPPLY_PRESENT");
    if( !power_supply_present ) {
        power_supply_present =
            power_supply_online = udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE");
    }

    if( power_supply_present && !strcmp(power_supply_present, "1") )
        connected = true;

    /* Transition period = Connection status derived from udev
     * events disagrees with usb-moded side bookkeeping. */
    if( connected != usbmoded_get_connection_state() ) {
        /* Enable udev property diagnostic logging */
        warnings = true;
        /* Block suspend briefly */
        usbmoded_delay_suspend();
    }

    if( !connected ) {
        /* Handle: Disconnected */

        if( warnings && !power_supply_present )
            log_err("No usable power supply indicator\n");
        cable_state_from_udev(CABLE_STATE_DISCONNECTED);

// QUARANTINE   log_debug("DISCONNECTED");
// QUARANTINE
// QUARANTINE   cancel_cable_connection_timeout();
// QUARANTINE
// QUARANTINE   if (charger) {
// QUARANTINE       log_debug("UDEV:USB dedicated charger disconnected\n");
// QUARANTINE       usbmoded_set_charger_connected(FALSE);
// QUARANTINE   }
// QUARANTINE
// QUARANTINE   if (cable) {
// QUARANTINE       log_debug("UDEV:USB cable disconnected\n");
// QUARANTINE       usbmoded_set_usb_connected(FALSE);
// QUARANTINE   }
// QUARANTINE
// QUARANTINE   cable = 0;
// QUARANTINE   charger = 0;
    }
    else {
        if( warnings && power_supply_online )
            log_warning("Using online property\n");

        /* At least h4113 i.e. "Xperia XA2 - Dual SIM" seem to have
         * POWER_SUPPLY_REAL_TYPE udev property with information
         * that usb-moded expects to be in POWER_SUPPLY_TYPE prop.
         */
        power_supply_type = udev_device_get_property_value(dev, "POWER_SUPPLY_REAL_TYPE");
        if( !power_supply_type )
            power_supply_type = udev_device_get_property_value(dev, "POWER_SUPPLY_TYPE");
        /*
         * Power supply type might not exist also :(
         * Send connected event but this will not be able
         * to discriminate between charger/cable.
         */
        if( !power_supply_type ) {
            if( warnings )
                log_warning("Fallback since cable detection might not be accurate. "
                            "Will connect on any voltage on charger.\n");
            cable_state_from_udev(CABLE_STATE_PC_CONNECTED);
// QUARANTINE       schedule_cable_connection_timeout();
            goto cleanup;
        }

        log_debug("CONNECTED - POWER_SUPPLY_TYPE = %s", power_supply_type);

        if( !strcmp(power_supply_type, "USB") ||
            !strcmp(power_supply_type, "USB_CDP") ) {
            cable_state_from_udev(CABLE_STATE_PC_CONNECTED);
// QUARANTINE       if( initial )
// QUARANTINE           setup_cable_connection();
// QUARANTINE       else
// QUARANTINE           schedule_cable_connection_timeout();
        }
        else if( !strcmp(power_supply_type, "USB_DCP") ||
                 !strcmp(power_supply_type, "USB_HVDCP") ||
                 !strcmp(power_supply_type, "USB_HVDCP_3") ) {
            cable_state_from_udev(CABLE_STATE_CHARGER_CONNECTED);
// QUARANTINE       setup_charger_connection();
        }
        else if( !strcmp(power_supply_type, "USB_FLOAT")) {
            if( !cable_state_connected() )
                log_warning("connection type detection failed, assuming charger");
            cable_state_from_udev(CABLE_STATE_CHARGER_CONNECTED);
        }
        else if( !strcmp(power_supply_type, "Unknown")) {
            // nop
            log_warning("unknown connection type reported, assuming disconnected");
            cable_state_from_udev(CABLE_STATE_DISCONNECTED);
        }
        else {
            if( warnings )
                log_warning("unhandled power supply type: %s", power_supply_type);
            cable_state_from_udev(CABLE_STATE_DISCONNECTED);
        }
    }

cleanup:
    return;
}

static int umudev_score_as_power_supply(const char *syspath)
{
    int                 score   = 0;
    struct udev_device *dev     = 0;
    const char         *sysname = 0;

    if( !umudev_object )
        goto EXIT;

    if( !(dev = udev_device_new_from_syspath(umudev_object, syspath)) )
        goto EXIT;

    if( !(sysname = udev_device_get_sysname(dev)) )
        goto EXIT;

    /* try to assign a weighed score */

    /* check that it is not a battery */
    if(strstr(sysname, "battery") || strstr(sysname, "BAT"))
        goto EXIT;

    /* if it contains usb in the name it very likely is good */
    if(strstr(sysname, "usb"))
        score = score + 10;

    /* often charger is also mentioned in the name */
    if(strstr(sysname, "charger"))
        score = score + 5;

    /* present property is used to detect activity, however online is better */
    if(udev_device_get_property_value(dev, "POWER_SUPPLY_PRESENT"))
        score = score + 5;

    if(udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE"))
        score = score + 10;

    /* type is used to detect if it is a cable or dedicated charger.
     Bonus points if it is there. */
    if(udev_device_get_property_value(dev, "POWER_SUPPLY_TYPE"))
        score = score + 10;

EXIT:
    /* clean up */
    if( dev )
        udev_device_unref(dev);

    return score;
}

gboolean umudev_init(void)
{
    gboolean                success = FALSE;

    char                   *configured_device = NULL;
    char                   *configured_subsystem = NULL;
    struct udev_device     *dev = 0;
    static GIOChannel      *iochannel  = 0;

    int ret = 0;

    /* Clear in-cleanup in case of restart */
    umudev_in_cleanup = false;

    /* Create the udev object */
    if( !(umudev_object = udev_new()) ) {
        log_err("Can't create umudev_object\n");
        goto EXIT;
    }

    if( !(configured_device = config_find_udev_path()) )
        configured_device = g_strdup("/sys/class/power_supply/usb");

    if( !(configured_subsystem = config_find_udev_subsystem()) )
        configured_subsystem = g_strdup("power_supply");

    /* Try with configured / default device */
    dev = udev_device_new_from_syspath(umudev_object, configured_device);

    /* If needed, try heuristics */
    if( !dev ) {
        log_debug("Trying to guess $power_supply device.\n");

        int    current_score = 0;
        gchar *current_name  = 0;

        struct udev_enumerate  *list;
        struct udev_list_entry *list_entry;
        struct udev_list_entry *first_entry;

        list = udev_enumerate_new(umudev_object);
        udev_enumerate_add_match_subsystem(list, "power_supply");
        udev_enumerate_scan_devices(list);
        first_entry = udev_enumerate_get_list_entry(list);
        udev_list_entry_foreach(list_entry, first_entry) {
            const char *name = udev_list_entry_get_name(list_entry);
            int score = umudev_score_as_power_supply(name);
            if( current_score < score ) {
                g_free(current_name);
                current_name = g_strdup(name);
                current_score = score;
            }
        }
        /* check if we found anything with some kind of score */
        if(current_score > 0) {
            dev = udev_device_new_from_syspath(umudev_object, current_name);
        }
        g_free(current_name);
    }

    /* Give up if no power supply device was found */
    if( !dev ) {
        log_err("Unable to find $power_supply device.");
        /* communicate failure, mainloop will exit and call appropriate clean-up */
        goto EXIT;
    }

    /* Cache device name */
    umudev_sysname = g_strdup(udev_device_get_sysname(dev));
    log_debug("device name = %s\n", umudev_sysname);

    /* Start monitoring for changes */
    umudev_monitor = udev_monitor_new_from_netlink(umudev_object, "udev");
    if( !umudev_monitor )
    {
        log_err("Unable to monitor the netlink\n");
        /* communicate failure, mainloop will exit and call appropriate clean-up */
        goto EXIT;
    }

    ret = udev_monitor_filter_add_match_subsystem_devtype(umudev_monitor,
                                                          configured_subsystem,
                                                          NULL);
    if(ret != 0)
    {
        log_err("Udev match failed.\n");
        goto EXIT;
    }

    ret = udev_monitor_enable_receiving(umudev_monitor);
    if(ret != 0)
    {
        log_err("Failed to enable monitor recieving.\n");
        goto EXIT;
    }

    iochannel = g_io_channel_unix_new(udev_monitor_get_fd(umudev_monitor));
    if( !iochannel )
        goto EXIT;

    umudev_watch_id = g_io_add_watch_full(iochannel, 0, G_IO_IN, umudev_io_input_cb, NULL, umudev_io_error_cb);
    if( !umudev_watch_id )
        goto EXIT;

    /* everything went well */
    success = TRUE;

    /* check initial status */
    umudev_parse_properties(dev, true);

EXIT:
    /* Cleanup local resources */
    if( iochannel )
        g_io_channel_unref(iochannel);

    if( dev )
        udev_device_unref(dev);

    g_free(configured_subsystem);
    g_free(configured_device);

    /* All or nothing */
    if( !success )
        umudev_quit();

    return success;
}

void umudev_quit(void)
{
    umudev_in_cleanup = true;

    log_debug("HWhal cleanup\n");

// QUARANTINE     cancel_cable_connection_timeout();

    if( umudev_watch_id )
    {
        g_source_remove(umudev_watch_id),
            umudev_watch_id = 0;
    }

    if( umudev_monitor ) {
        udev_monitor_unref(umudev_monitor),
            umudev_monitor = 0;
    }

    if( umudev_object ) {
        udev_unref(umudev_object),
            umudev_object =0 ;
    }

    g_free(umudev_sysname),
        umudev_sysname = 0;

    cable_state_stop_timer();
}

// QUARANTINE static void setup_cable_connection(void)
// QUARANTINE {
// QUARANTINE   cancel_cable_connection_timeout();
// QUARANTINE
// QUARANTINE   log_debug("UDEV:USB pc cable connected\n");
// QUARANTINE
// QUARANTINE   cable = 1;
// QUARANTINE   charger = 0;
// QUARANTINE   usbmoded_set_usb_connected(TRUE);
// QUARANTINE }

// QUARANTINE static void setup_charger_connection(void)
// QUARANTINE {
// QUARANTINE   cancel_cable_connection_timeout();
// QUARANTINE
// QUARANTINE   log_debug("UDEV:USB dedicated charger connected\n");
// QUARANTINE
// QUARANTINE   if (cable) {
// QUARANTINE           /* The connection was initially reported incorrectly
// QUARANTINE            * as pc cable, then later on declared as charger.
// QUARANTINE            *
// QUARANTINE            * Clear "connected" boolean flag so that the
// QUARANTINE            * usbmoded_set_charger_connected() call below acts as if charger
// QUARANTINE            * were detected already on connect: mode gets declared
// QUARANTINE            * as dedicated charger (and the mode selection dialog
// QUARANTINE            * shown by ui gets closed).
// QUARANTINE            */
// QUARANTINE           usbmoded_set_connection_state(FALSE);
// QUARANTINE   }
// QUARANTINE
// QUARANTINE   charger = 1;
// QUARANTINE   cable = 0;
// QUARANTINE   usbmoded_set_charger_connected(TRUE);
// QUARANTINE }

// QUARANTINE static gboolean cable_connection_timeout_cb(gpointer data)
// QUARANTINE {
// QUARANTINE   (void)data;
// QUARANTINE
// QUARANTINE   log_debug("connect delay: timeout");
// QUARANTINE   cable_connection_timeout_id = 0;
// QUARANTINE
// QUARANTINE   setup_cable_connection();
// QUARANTINE
// QUARANTINE   return FALSE;
// QUARANTINE }

// QUARANTINE static void cancel_cable_connection_timeout(void)
// QUARANTINE {
// QUARANTINE   if (cable_connection_timeout_id) {
// QUARANTINE           log_debug("connect delay: cancel");
// QUARANTINE           g_source_remove(cable_connection_timeout_id);
// QUARANTINE           cable_connection_timeout_id = 0;
// QUARANTINE   }
// QUARANTINE }

// QUARANTINE static void schedule_cable_connection_timeout(void)
// QUARANTINE {
// QUARANTINE   /* Ignore If already connected */
// QUARANTINE   if (usbmoded_get_connection_state())
// QUARANTINE           return;
// QUARANTINE
// QUARANTINE   if (!cable_connection_timeout_id && usbmoded_cable_connection_delay > 0) {
// QUARANTINE           /* Dedicated charger might be initially misdetected as
// QUARANTINE            * pc cable. Delay a bit befor accepting the state. */
// QUARANTINE
// QUARANTINE           log_debug("connect delay: started (%d ms)",
// QUARANTINE                     usbmoded_cable_connection_delay);
// QUARANTINE           cable_connection_timeout_id =
// QUARANTINE                   g_timeout_add(usbmoded_cable_connection_delay,
// QUARANTINE                                 cable_connection_timeout_cb,
// QUARANTINE                                 NULL);
// QUARANTINE   }
// QUARANTINE   else {
// QUARANTINE           /* If more udev events indicating cable connection
// QUARANTINE            * are received while waiting, accept immediately. */
// QUARANTINE           setup_cable_connection();
// QUARANTINE   }
// QUARANTINE }
