/*
 * sd-hwdb.h
 * 
 * Forked from systemd/src/libsystemd/sd-hwdb.h on March 26, 2015
 * 
 * All modifications to the original source file are Copyright (C) 2015 Jude Nelson <judecn@gmail.com>
 *
 * Original copyright and license text produced below.
 */
/***
  This file is part of systemd.

  Copyright 2008-2012 Kay Sievers <kay@vrfy.org>
  Copyright 2014 Tom Gundersen <teg@jklm.no>

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#ifndef _LIBUDEV_COMPAT_SD_HWDB_H_
#define _LIBUDEV_COMPAT_SD_HWDB_H_

#include "util.h"

typedef struct sd_hwdb sd_hwdb;

sd_hwdb *sd_hwdb_ref (sd_hwdb * hwdb);
sd_hwdb *sd_hwdb_unref (sd_hwdb * hwdb);

int sd_hwdb_new (sd_hwdb ** ret);

int sd_hwdb_get (sd_hwdb * hwdb, const char *modalias, const char *key,
		 const char **value);

int sd_hwdb_seek (sd_hwdb * hwdb, const char *modalias);
int sd_hwdb_enumerate (sd_hwdb * hwdb, const char **key, const char **value);

/* the inverse condition avoids ambiguity of danling 'else' after the macro */
#define SD_HWDB_FOREACH_PROPERTY(hwdb, modalias, key, value)            \
        if (sd_hwdb_seek(hwdb, modalias) < 0) { }                       \
        else while (sd_hwdb_enumerate(hwdb, &(key), &(value)) > 0)

#endif
