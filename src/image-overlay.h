/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
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

#ifndef IMAGE_OVERLAY_H
#define IMAGE_OVERLAY_H

#include <glib.h>

struct ImageWindow;

void set_image_overlay_template_string(gchar **template_string, const gchar *value);
void set_image_overlay_font_string(gchar **font_string, const gchar *value);

void image_osd_set(ImageWindow *imd, gboolean show);
gboolean image_osd_get(ImageWindow *imd);

void image_osd_copy_status(ImageWindow *src, ImageWindow *dest);

void image_osd_update(ImageWindow *imd);

#endif
