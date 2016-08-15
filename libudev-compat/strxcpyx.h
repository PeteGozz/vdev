/*
 * strxcpyx.c
 * 
 * Forked from systemd/src/shared/strxcpyx.c on March 26, 2015
 * 
 * All modifications to the original source file are Copyright (C) 2015 Jude Nelson <judecn@gmail.com>
 *
 * Original copyright and license text produced below.
 */
/***
  This file is part of systemd.

  Copyright 2013 Kay Sievers

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

#ifndef _LIBUDEV_COMPAT_STRXCPYX_H_
#define _LIBUDEV_COMPAT_STRXCPYX_H_

#include <stdarg.h>
#include <stdbool.h>

// #include "macro.h"
#include "util.h"

size_t strpcpy(char **dest, size_t size, const char *src);
size_t strpcpyf(char **dest, size_t size, const char *src, ...) _printf_(3, 4);
size_t strpcpyl(char **dest, size_t size, const char *src, ...) _sentinel_;
size_t strscpy(char *dest, size_t size, const char *src);
size_t strscpyl(char *dest, size_t size, const char *src, ...) _sentinel_;

#endif
