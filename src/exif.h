/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Eric Swalens, Quy Tonthat, John Ellis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __EXIF_H
#define __EXIF_H

#include <glib.h>

/* Read-only metadata for the OSD's %exif%, %xmp% and %metadata% tags. Nothing else reads EXIF. */
struct ExifData;

void exif_init();

ExifData *exif_read(const gchar *path);
void exif_free(ExifData *exif);

gchar *exif_get_all_exif_as_text(ExifData *exif);
gchar *exif_get_all_xmp_as_text(ExifData *exif);
gchar *exif_get_all_metadata_as_text(ExifData *exif);

#endif
