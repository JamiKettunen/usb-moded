/*
 *
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2018 Jolla Ltd.
 *
 * author: Philippe De Swert <philippe.de-swert@nokia.com>
 * author: Philippe De Swert <phdeswer@lumi.maa>
 * author: Philippe De Swert <philippedeswert@gmail.com>
 * author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * author: Thomas Perl <m@thp.io>
 * author: Slava Monich <slava.monich@jolla.com>
 * author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * author: Andrew den Exter <andrew.den.exter@jolla.com>
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

#ifndef  USB_MODED_DYN_CONFIG_H_
# define USB_MODED_DYN_CONFIG_H_

/* ========================================================================= *
 * Constants
 * ========================================================================= */

# define MODE_DIR_PATH  "/etc/usb-moded/dyn-modes"
# define DIAG_DIR_PATH  "/etc/usb-moded/diag"

# define MODE_ENTRY                      "mode"
# define MODE_NAME_KEY                   "name"
# define MODE_MODULE_KEY                 "module"
# define MODE_NEEDS_APPSYNC_KEY          "appsync"
# define MODE_NETWORK_KEY                "network"
# define MODE_MASS_STORAGE               "mass_storage"
# define MODE_NETWORK_INTERFACE_KEY      "network_interface"
# define MODE_OPTIONS_ENTRY              "options"
# define MODE_SYSFS_PATH                 "sysfs_path"
# define MODE_SYSFS_VALUE                "sysfs_value"
# define MODE_SYSFS_RESET_VALUE          "sysfs_reset_value"
# define MODE_SOFTCONNECT                "softconnect"
# define MODE_SOFTCONNECT_DISCONNECT     "softconnect_disconnect"
# define MODE_SOFTCONNECT_PATH           "softconnect_path"
/* Instead of hard-coding values that never change or have only one option,
 android engineers prefered to have sysfs entries... go figure... */
# define MODE_ANDROID_EXTRA_SYSFS_PATH   "android_extra_sysfs_path"
# define MODE_ANDROID_EXTRA_SYSFS_VALUE  "android_extra_sysfs_value"
/* in combined android gadgets we sometime need more than one extra sysfs path or value */
# define MODE_ANDROID_EXTRA_SYSFS_PATH2  "android_extra_sysfs_path2"
# define MODE_ANDROID_EXTRA_SYSFS_VALUE2 "android_extra_sysfs_value2"
# define MODE_ANDROID_EXTRA_SYSFS_PATH3  "android_extra_sysfs_path3"
# define MODE_ANDROID_EXTRA_SYSFS_VALUE3 "android_extra_sysfs_value3"
# define MODE_ANDROID_EXTRA_SYSFS_PATH4  "android_extra_sysfs_path4"
# define MODE_ANDROID_EXTRA_SYSFS_VALUE4 "android_extra_sysfs_value4"
/* For windows different modes/usb profiles need their own idProduct */
# define MODE_IDPRODUCT                  "idProduct"
# define MODE_IDVENDOROVERRIDE           "idVendorOverride"
# define MODE_HAS_NAT                    "nat"
# define MODE_HAS_DHCP_SERVER            "dhcp_server"
# ifdef CONNMAN
#  define MODE_CONNMAN_TETHERING         "connman_tethering"
# endif

/* ========================================================================= *
 * Types
 * ========================================================================= */

/**
 * Struct keeping all the data needed for the definition of a dynamic mode
 */
typedef struct mode_list_elem
{
    char *mode_name;                      /**< Mode name */
    char *mode_module;                    /**< Needed module for given mode */
    int appsync;                          /**< Requires appsync or not */
    int network;                          /**< Bring up network or not */
    int mass_storage;                     /**< Use mass-storage functions */
    char *network_interface;              /**< Which network interface to bring up if network needs to be enabled */
    char *sysfs_path;                     /**< Path to set sysfs options */
    char *sysfs_value;                    /**< Option name/value to write to sysfs */
    char *sysfs_reset_value;              /**< Value to reset the the sysfs to default */
    char *softconnect;                    /**< Value to be written to softconnect interface */
    char *softconnect_disconnect;         /**< Value to set on the softconnect interface to disable after disconnect */
    char *softconnect_path;               /**< Path for the softconnect */
    char *android_extra_sysfs_path;       /**< Path for static value that never changes that needs to be set by sysfs :( */
    char *android_extra_sysfs_value;      /**< Static value that never changes that needs to be set by sysfs :( */
    char *android_extra_sysfs_path2;      /**< Path for static value that never changes that needs to be set by sysfs :( */
    char *android_extra_sysfs_value2;     /**< Static value that never changes that needs to be set by sysfs :( */
    char *android_extra_sysfs_path3;      /**< Path for static value that never changes that needs to be set by sysfs :( */
    char *android_extra_sysfs_value3;     /**< Static value that never changes that needs to be set by sysfs :( */
    char *android_extra_sysfs_path4;      /**< Path for static value that never changes that needs to be set by sysfs :( */
    char *android_extra_sysfs_value4;     /**< Static value that never changes that needs to be set by sysfs :( */
    char *idProduct;                      /**< Product id to assign to a specific profile */
    char *idVendorOverride;               /**< Temporary vendor override for special modes used by odms in testing/manufacturing */
    int nat;                              /**< If NAT should be set up in this mode or not */
    int dhcp_server;                      /**< if a DHCP server needs to be configured and started or not */
# ifdef CONNMAN
    char* connman_tethering;              /**< Connman's tethering technology path */
# endif
} mode_list_elem;

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- dynconfig -- */

void   dynconfig_free_list_item(mode_list_elem *list_item);
void   dynconfig_free_mode_list(GList *modelist);
GList *dynconfig_read_mode_list(int diag);

#endif /* USB_MODED_DYN_CONFIG_H_ */
