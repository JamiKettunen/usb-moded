/**
 * @file usb_moded-modesetting.h
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2018 Jolla Ltd.
 *
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
 * @author: Philippe De Swert <phdeswer@lumi.maa>
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Thomas Perl <m@thp.io>
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

#ifndef  USB_MODED_MODESETTING_H_
# define USB_MODED_MODESETTING_H_

# include "usb_moded-dyn-config.h"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- modesetting -- */

void modesetting_verify_values     (void);
int  modesetting_write_to_file_real(const char *file, int line, const char *func, const char *path, const char *text);
int  modesetting_set_dynamic_mode  (void);
int  modesetting_cleanup           (const char *module);
void modesetting_init              (void);
void modesetting_quit              (void);

/* ========================================================================= *
 * Macros
 * ========================================================================= */

# define write_to_file(path,text)\
     modesetting_write_to_file_real(__FILE__,__LINE__,__FUNCTION__,(path),(text))

#endif /* USB_MODED_MODESETTING_H_ */
