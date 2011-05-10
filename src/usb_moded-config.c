/**
  @file usb_moded-config.c
 
  Copyright (C) 2010 Nokia Corporation. All rights reserved.

  @author: Philippe De Swert <philippe.de-swert@nokia.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the Lesser GNU General Public License 
  version 2 as published by the Free Software Foundation. 

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.
 
  You should have received a copy of the Lesser GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
  02110-1301 USA
*/

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <glib.h>
#include <glib/gkeyfile.h>

#include "usb_moded-config.h"
#include "usb_moded-log.h"

static int get_conf_int(const gchar *entry, const gchar *key);
static const char * get_conf_string(const gchar *entry, const gchar *key);

const char *find_mounts(void)
{
  
  const char *ret = NULL;

  ret = get_conf_string(FS_MOUNT_ENTRY, FS_MOUNT_KEY);
  if(ret == NULL)
  {
      ret = g_strdup(FS_MOUNT_DEFAULT);	  
      log_debug("Default mount = %s\n", ret);
  }
  return(ret);
}

int find_sync(void)
{

  return(get_conf_int(FS_SYNC_ENTRY, FS_SYNC_KEY));
}

const char * find_alt_mount(void)
{
  return(get_conf_string(ALT_MOUNT_ENTRY, ALT_MOUNT_KEY));
}

#ifdef UDEV
const char * find_udev_path(void)
{
  return(get_conf_string(UDEV_PATH_ENTRY, UDEV_PATH_KEY));
}
#endif /* UDEV */

#ifdef NOKIA
const char * find_cdrom_path(void)
{
  return(get_conf_string(CDROM_ENTRY, CDROM_PATH_KEY));
}

int find_cdrom_timeout(void)
{
  return(get_conf_int(CDROM_ENTRY, CDROM_TIMEOUT_KEY));
}
#endif /* NOKIA */

#ifdef APP_SYNC
const char * check_trigger(void)
{
  return(get_conf_string(TRIGGER_ENTRY, TRIGGER_PATH_KEY));
}

const char * check_trigger_mode(void)
{
  return(get_conf_string(TRIGGER_ENTRY, TRIGGER_MODE_KEY));
}
#endif /* APP_SYNC */

static int get_conf_int(const gchar *entry, const gchar *key)
{
  GKeyFile *settingsfile;
  gboolean test = FALSE;
  gchar **keys;
  int ret = 0;

  settingsfile = g_key_file_new();
  test = g_key_file_load_from_file(settingsfile, FS_MOUNT_CONFIG_FILE, G_KEY_FILE_NONE, NULL);
  if(!test)
  {
      log_debug("no conffile\n");
      g_key_file_free(settingsfile);
      return(ret);
  }
  keys = g_key_file_get_keys (settingsfile, entry, NULL, NULL);
  if(keys == NULL)
        return ret;
  while (*keys != NULL)
  {
        if(!strcmp(*keys, key))
        {
                ret = g_key_file_get_integer(settingsfile, entry, *keys, NULL);
                log_debug("%s key value  = %d\n", key, ret);
        }
        keys++;
  }
  g_key_file_free(settingsfile);
  return(ret);

}

static const char * get_conf_string(const gchar *entry, const gchar *key)
{
  GKeyFile *settingsfile;
  gboolean test = FALSE;
  gchar **keys;
  const char *ret = NULL, *tmp_char;

  settingsfile = g_key_file_new();
  test = g_key_file_load_from_file(settingsfile, FS_MOUNT_CONFIG_FILE, G_KEY_FILE_NONE, NULL);
  if(!test)
  {
      log_debug("No conffile.\n");
      g_key_file_free(settingsfile);
      return(ret);
  }
  keys = g_key_file_get_keys (settingsfile, entry, NULL, NULL);
  if(keys == NULL)
        return ret;
  while (*keys != NULL)
  {
        if(!strcmp(*keys, key))
        {
                tmp_char = g_key_file_get_string(settingsfile, entry, *keys, NULL);
                if(tmp_char)
                {
                        log_debug("key %s value  = %s\n", key, tmp_char);
                        ret = g_strdup(tmp_char);
                }
        }
        keys++;
  }
  g_key_file_free(settingsfile);
  return(ret);

}

