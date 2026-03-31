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

#ifndef LAYOUT_IMAGE_H
#define LAYOUT_IMAGE_H

#include <glib.h>
#include <gtk/gtk.h>

#include "typedefs.h"

class FileData;
struct LayoutWindow;

void layout_image_init(LayoutWindow *lw);

void layout_image_set_fd(LayoutWindow *lw, FileData *fd);
void layout_image_set_with_ahead(LayoutWindow *lw, FileData *fd, FileData *read_ahead_fd);

void layout_image_set_index(LayoutWindow *lw, gint index);

void layout_image_refresh(LayoutWindow *lw);

void layout_image_color_profile_set(LayoutWindow *lw, gint input_type, gboolean use_image);
gboolean layout_image_color_profile_get(LayoutWindow *lw, gint &input_type, gboolean &use_image);
void layout_image_color_profile_set_use(LayoutWindow *lw, gint enable);
gboolean layout_image_color_profile_get_use(LayoutWindow *lw);
gboolean layout_image_color_profile_get_status(LayoutWindow *lw, gchar **image_profile, gchar **screen_profile);

FileData *layout_image_get_fd(LayoutWindow *lw);

void layout_image_zoom_adjust(LayoutWindow *lw, gdouble increment);
void layout_image_zoom_adjust_at_point(LayoutWindow *lw, gdouble increment, gint x, gint y);
void layout_image_zoom_set(LayoutWindow *lw, gdouble zoom);
void layout_image_set_desaturate(LayoutWindow *lw, gboolean desaturate);
gboolean layout_image_get_desaturate(LayoutWindow *lw);
void layout_image_set_overunderexposed(LayoutWindow *lw, gboolean overunderexposed);

void layout_image_next(LayoutWindow *lw);
void layout_image_prev(LayoutWindow *lw);
void layout_image_first(LayoutWindow *lw);
void layout_image_last(LayoutWindow *lw);

void layout_image_to_root(LayoutWindow *lw);

void layout_image_full_screen_start(LayoutWindow *lw);
void layout_image_full_screen_stop(LayoutWindow *lw);
void layout_image_full_screen_toggle(LayoutWindow *lw);

void layout_image_animate_toggle(LayoutWindow *lw);

void layout_image_notify_cb(FileData *fd, NotifyType type, gpointer data);
void layout_image_reset_orientation(LayoutWindow *lw);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
