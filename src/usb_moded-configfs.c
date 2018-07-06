/**
 * @file usb_moded-configfs.c
 *
 * Copyright (C) 2018 Jolla. All rights reserved.
 *
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

#include "usb_moded-configfs.h"

#include <sys/types.h>

#include <stdio.h>
#include <dirent.h>
#include <errno.h>

#include <glib.h>

#include "usb_moded.h"
#include "usb_moded-android.h"
#include "usb_moded-log.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-config-private.h"
#include "usb_moded-mac.h"

/* ========================================================================= *
 * Constants
 * ========================================================================= */

#define FUNCTION_MASS_STORAGE "mass_storage.usb0"
#define FUNCTION_RNDIS        "rndis_bam.rndis"
#define FUNCTION_MTP          "ffs.mtp"

#define CONFIGFS_GADGET        "/config/usb_gadget/g1"
#define CONFIGFS_CONFIG        CONFIGFS_GADGET"/configs/b.1"
#define CONFIGFS_FUNCTIONS     CONFIGFS_GADGET"/functions"
#define CONFIGFS_UDC           CONFIGFS_GADGET"/UDC"
#define CONFIGFS_ID_VENDOR     CONFIGFS_GADGET"/idVendor"
#define CONFIGFS_ID_PRODUCT    CONFIGFS_GADGET"/idProduct"
#define CONFIGFS_MANUFACTURER  CONFIGFS_GADGET"/strings/0x409/manufacturer"
#define CONFIGFS_PRODUCT       CONFIGFS_GADGET"/strings/0x409/product"
#define CONFIGFS_SERIAL        CONFIGFS_GADGET"/strings/0x409/serialnumber"

#define CONFIGFS_RNDIS_WCEIS   CONFIGFS_FUNCTIONS"/rndis_bam.rndis/wceis"
#define CONFIGFS_RNDIS_ETHADDR CONFIGFS_FUNCTIONS"/rndis_bam.rndis/ethaddr"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- configfs -- */

static int         configfs_file_type            (const char *path);
static const char *configfs_function_path        (char *buff, size_t size, const char *func);
static const char *configfs_config_path          (char *buff, size_t size, const char *func);
static const char *configfs_register_function    (const char *function);
#ifdef DEAD_CODE
static bool        configfs_unregister_function  (const char *function);
#endif
static bool        configfs_enable_function      (const char *function);
static bool        configfs_disable_function     (const char *function);
static bool        configfs_disable_all_functions(void);
static char       *configfs_strip                (char *str);
bool               configfs_in_use               (void);
static bool        configfs_probe                (void);
static const char *configfs_udc_enable_value     (void);
static bool        configfs_write_file           (const char *path, const char *text);
static bool        configfs_read_file            (const char *path, char *buff, size_t size);
#ifdef DEAD_CODE
static bool        configfs_read_udc             (char *buff, size_t size);
#endif
static bool        configfs_write_udc            (const char *text);
bool               configfs_set_udc              (bool enable);
bool               configfs_init_values          (void);
bool               configfs_set_charging_mode    (void);
bool               configfs_set_productid        (const char *id);
bool               configfs_set_vendorid         (const char *id);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static int configfs_probed = -1;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static int configfs_file_type(const char *path)
{
    int type = -1;

    if( !path )
        goto EXIT;

    struct stat st;
    if( lstat(path, &st) == -1 )
        goto EXIT;

    type = st.st_mode & S_IFMT;

EXIT:
    return type;
}

static const char *
configfs_function_path(char *buff, size_t size, const char *func)
{
    snprintf(buff, size, "%s/%s", CONFIGFS_FUNCTIONS, func);
    return buff;
}

static const char *
configfs_config_path(char *buff, size_t size, const char *func)
{
    snprintf(buff, size, "%s/%s", CONFIGFS_CONFIG, func);
    return buff;
}

static const char *
configfs_register_function(const char *function)
{
    const char *res = 0;

    static char fpath[256];
    configfs_function_path(fpath, sizeof fpath, function);

    if( mkdir(fpath, 0775) == -1 && errno != EEXIST ) {
        log_err("%s: mkdir failed: %m", fpath);
        goto EXIT;
    }

    if( configfs_file_type(fpath) != S_IFDIR ) {
        log_err("%s: is not a directory", fpath);
        goto EXIT;
    }

    log_debug("function %s is registered", function);

    res = fpath;

EXIT:
    return res;
}

#ifdef DEAD_CODE
static bool
configfs_unregister_function(const char *function)
{
    bool ack = false;

    char fpath[256];
    configfs_function_path(fpath, sizeof fpath, function);

    if( rmdir(fpath) == -1 && errno != ENOENT ) {
        log_err("%s: rmdir failed: %m", fpath);
        goto EXIT;
    }

    log_debug("function %s is unregistered", function);
    ack = true;

EXIT:
    return ack;
}
#endif

static bool
configfs_enable_function(const char *function)
{
    bool ack = false;

    const char *fpath = configfs_register_function(function);
    if( !fpath ) {
        log_err("function %s is not registered", function);
        goto EXIT;
    }

    char cpath[256];
    configfs_config_path(cpath, sizeof cpath, function);

    switch( configfs_file_type(cpath) ) {
    case S_IFLNK:
        if( unlink(cpath) == -1 ) {
            log_err("%s: unlink failed: %m", cpath);
            goto EXIT;
        }
        /* fall through */
    case -1:
        if( symlink(fpath, cpath) == -1 ) {
            log_err("%s: failed to symlink to %s: %m", cpath, fpath);
            goto EXIT;
        }
        break;
    default:
        log_err("%s: is not a symlink", cpath);
        goto EXIT;
    }

    log_debug("function %s is enabled", function);
    ack = true;

EXIT:
    return ack;
}

static bool
configfs_disable_function(const char *function)
{
    bool ack = false;

    char cpath[256];
    configfs_config_path(cpath, sizeof cpath, function);

    if( configfs_file_type(cpath) != S_IFLNK ) {
        log_err("%s: is not a symlink", cpath);
        goto EXIT;
    }

    if( unlink(cpath) == -1 ) {
        log_err("%s: unlink failed: %m", cpath);
        goto EXIT;
    }

    log_debug("function %s is disabled", function);
    ack = true;

EXIT:
    return ack;
}

static bool
configfs_disable_all_functions(void)
{
    bool  ack = false;
    DIR  *dir = 0;

    if( !(dir = opendir(CONFIGFS_CONFIG)) ) {
        log_err("%s: opendir failed: %m", CONFIGFS_CONFIG);
        goto EXIT;
    }

    ack = true;

    struct dirent *de;
    while( (de = readdir(dir)) ) {
        if( de->d_type != DT_LNK )
            continue;

        if( !configfs_disable_function(de->d_name) )
            ack = false;
    }

    if( ack )
        log_debug("all functions are disabled");

EXIT:
    if( dir )
        closedir(dir);

    return ack;
}

static char *configfs_strip(char *str)
{
    unsigned char *src = (unsigned char *)str;
    unsigned char *dst = (unsigned char *)str;

    while( *src > 0 && *src <= 32 ) ++src;

    for( ;; )
    {
        while( *src > 32 ) *dst++ = *src++;
        while( *src > 0 && *src <= 32 ) ++src;
        if( *src == 0 ) break;
        *dst++ = ' ';
    }
    *dst = 0;
    return str;
}

bool
configfs_in_use(void)
{
    if( configfs_probed < 0 )
        log_debug("configfs_in_use() called before configfs_probe()");
    return configfs_probed > 0;
}

static bool
configfs_probe(void)
{
    if( configfs_probed <= 0 ) {
        configfs_probed = access(CONFIGFS_GADGET, F_OK) == 0;
        log_warning("CONFIGFS %sdetected", configfs_probed ? "" : "not ");
    }
    return configfs_in_use();
}

static const char *
configfs_udc_enable_value(void)
{
    static bool  probed = false;
    static char *value  = 0;

    if( !probed ) {
        probed = true;

        /* Find first symlink in /sys/class/udc directory */
        struct dirent *de;
        DIR *dir = opendir("/sys/class/udc");
        if( dir ) {
            while( (de = readdir(dir)) ) {
                if( de->d_type != DT_LNK )
                    continue;
                if( de->d_name[0] == '.' )
                    continue;
                value = strdup(de->d_name);
                break;
            }
            closedir(dir);
        }
    }

    return value ?: "";
}

static bool
configfs_write_file(const char *path, const char *text)
{
    bool ack = false;
    int  fd  = -1;

    if( !path || !text )
        goto EXIT;

    log_debug("WRITE %s '%s'", path, text);

    char buff[64];
    snprintf(buff, sizeof buff, "%s\n", text);
    size_t size = strlen(buff);

    if( (fd = open(path, O_WRONLY)) == -1 ) {
        log_err("%s: can't open for writing: %m", path);
        goto EXIT;
    }

    int rc = write(fd, text, size);
    if( rc == -1 ) {
        log_err("%s: write failure: %m", path);
        goto EXIT;
    }

    if( (size_t)rc != size ) {
        log_err("%s: write failure: partial success", path);
        goto EXIT;
    }

    ack = true;

EXIT:
    if( fd != -1 )
        close(fd);

    return ack;
}

static bool
configfs_read_file(const char *path, char *buff, size_t size)
{
    bool ack = false;
    int  fd  = -1;

    if( !path || !buff )
        goto EXIT;

    if( size < 2 )
        goto EXIT;

    if( (fd = open(path, O_RDONLY)) == -1 ) {
        log_err("%s: can't open for reading: %m", path);
        goto EXIT;
    }

    int rc = read(fd, buff, size - 1);
    if( rc == -1 ) {
        log_err("%s: read failure: %m", path);
        goto EXIT;
    }

    buff[rc] = 0;
    configfs_strip(buff);

    ack = true;

    log_debug("READ %s '%s'", path, buff);

EXIT:
    if( fd != -1 )
        close(fd);

    return ack;
}

#ifdef DEAD_CODE
static bool
configfs_read_udc(char *buff, size_t size)
{
    return configfs_read_file(CONFIGFS_UDC, buff, size);
}
#endif

static bool
configfs_write_udc(const char *text)
{
    bool ack = false;

    char prev[64];

    if( !configfs_read_file(CONFIGFS_UDC, prev, sizeof prev) )
        goto EXIT;

    if( strcmp(prev, text) ) {
        if( !configfs_write_file(CONFIGFS_UDC, text) )
            goto EXIT;
    }

    ack = true;

EXIT:
    return ack;

}

bool
configfs_set_udc(bool enable)
{
    log_debug("UDC - %s", enable ? "ENABLE" : "DISABLE");

    const char *value = "";

    if( enable )
        value = configfs_udc_enable_value();

    return configfs_write_udc(value);
}

/** initialize the basic configfs values
 */
bool
configfs_init_values(void)
{
    if( !configfs_probe() )
        goto EXIT;

    /* Disable */
    configfs_set_udc(false);

    /* Configure */
    gchar *text;
    if( (text = config_get_android_vendor_id()) ) {
        configfs_write_file(CONFIGFS_ID_VENDOR, text);
        g_free(text);
    }

    if( (text = config_get_android_product_id()) ) {
        configfs_write_file(CONFIGFS_ID_PRODUCT, text);
        g_free(text);
    }

    if( (text = config_get_android_manufacturer()) ) {
        configfs_write_file(CONFIGFS_MANUFACTURER, text);
        g_free(text);
    }

    if( (text = config_get_android_product()) ) {
        configfs_write_file(CONFIGFS_PRODUCT, text);
        g_free(text);
    }

    if( (text = android_get_serial()) ) {
        configfs_write_file(CONFIGFS_SERIAL, text);
        g_free(text);
    }

    /* Prep: charging_only */
    configfs_register_function(FUNCTION_MASS_STORAGE);

    /* Prep: mtp_mode */
    configfs_register_function(FUNCTION_MTP);
    if( access("/dev/mtp/ep0", F_OK) == -1 ) {
        usbmoded_system("/bin/mount -o uid=100000,gid=100000 -t functionfs mtp /dev/mtp");
    }

    /* Prep: developer_mode */
    configfs_register_function(FUNCTION_RNDIS);
    if( (text = mac_read_mac()) ) {
        configfs_write_file(CONFIGFS_RNDIS_ETHADDR, text);
        g_free(text);
    }
    /* For rndis to be discovered correctly in M$ Windows (vista and later) */
    configfs_write_file(CONFIGFS_RNDIS_WCEIS, "1");

    /* Leave disabled, will enable on cable connect detected */
EXIT:
    return configfs_in_use();
}

/* Set a charging mode for the configfs gadget
 *
 * @return true if successful, false on failure
 */
bool
configfs_set_charging_mode(void)
{
    bool ack = false;

    if( !configfs_set_function("mass_storage") )
        goto EXIT;

    /* TODO: make this configurable */
    configfs_set_productid("0AFE");

    if( !configfs_set_udc(true) )
        goto EXIT;

    ack = true;

EXIT:
    log_debug("CONFIGFS %s() -> %d", __func__, ack);
    return ack;
}

/* Set a product id for the configfs gadget
 *
 * @return true if successful, false on failure
 */
bool
configfs_set_productid(const char *id)
{
    bool ack = false;

    if( id && configfs_in_use() ) {
        /* Config files have things like "0A02".
         * Kernel wants to see "0x0a02" ... */
        char *end = 0;
        unsigned num = strtol(id, &end, 16);
        char     str[16];
        if( end > id && *end == 0 ) {
            snprintf(str, sizeof str, "0x%04x", num);
            id = str;
        }
        ack = configfs_write_file(CONFIGFS_ID_PRODUCT, id);
    }

    log_debug("CONFIGFS %s(%s) -> %d", __func__, id, ack);
    return ack;
}

/* Set a vendor id for the configfs gadget
 *
 * @return true if successful, false on failure
 */
bool
configfs_set_vendorid(const char *id)
{
    bool ack = false;

    if( id && configfs_in_use() ) {
        log_debug("%s(%s) was called", __func__, id);

        /* Config files have things like "0A02".
         * Kernel wants to see "0x0a02" ... */
        char *end = 0;
        unsigned num = strtol(id, &end, 16);
        char     str[16];

        if( end > id && *end == 0 ) {
            snprintf(str, sizeof str, "0x%04x", num);
            id = str;
        }

        ack = configfs_write_file(CONFIGFS_ID_VENDOR, id);
    }

    log_debug("CONFIGFS %s(%s) -> %d", __func__, id, ack);
    return ack;
}

static const char *
configfs_map_function(const char *func)
{
    if( !strcmp(func, "mass_storage") )
        func = FUNCTION_MASS_STORAGE;
    else if( !strcmp(func, "rndis") )
        func = FUNCTION_RNDIS;
    else if( !strcmp(func, "mtp") )
        func = FUNCTION_MTP;
    else if( !strcmp(func, "ffs") ) // existing config files ...
        func = FUNCTION_MTP;
    return func;
}

/* Set a function
 *
 * @return true if successful, false on failure
 */
bool
configfs_set_function(const char *func)
{
    bool ack = false;

    if( !func )
        goto EXIT;

    if( !configfs_in_use() )
        goto EXIT;

    /* Normalize names used by usb-moded itself and already
     * existing configuration files etc.
     */
    func = configfs_map_function(func);

    /* HACK: Stop mtp daemon when enabling any other function
     *       after bootup is finished (assumption being it
     *       can't be started before init done and we do not
     *       want to spam bootup journal with warnings.
     */
    if( strcmp(func, FUNCTION_MTP) && usbmoded_init_done_p() )
        usbmoded_system("systemctl-user stop buteo-mtp.service");

    if( !configfs_set_udc(false) )
        goto EXIT;

    if( !configfs_disable_all_functions() )
        goto EXIT;

    if( !configfs_enable_function(func) )
        goto EXIT;

    /* HACK: Start mtp daemon when enabling mtp function.
     *       Then wait "a bit" since udc can't be enabled
     *       before mtpd has written suitable configuration
     *       to control endpoint.
     */
    if( !strcmp(func, FUNCTION_MTP) ) {
        usbmoded_system("systemctl-user start buteo-mtp.service");
        usbmoded_msleep(1500);
    }

    /* Leave disabled, so that caller can adjust attributes
     * etc before enabling */

    ack = true;

EXIT:
    log_debug("CONFIGFS %s(%s) -> %d", __func__, func, ack);
    return ack;
}
