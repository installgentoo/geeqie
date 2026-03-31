/*
 * Copyright (C) 2004 John Ellis
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

#ifndef LAYOUT_UTIL_H
#define LAYOUT_UTIL_H

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "typedefs.h"

struct LayoutWindow;

void keyboard_scroll_calc(gint &x, gint &y, const GdkEventKey *event);

void layout_util_sync_file_filter(LayoutWindow *lw);
void layout_util_sync_color(LayoutWindow *lw);
void layout_util_sync(LayoutWindow *lw);

void layout_recent_add_path(const gchar *path);

void layout_copy_path_update_all();

void layout_editors_reload_start();
void layout_editors_reload_finish();
void layout_actions_setup(LayoutWindow *lw);
void layout_actions_add_window(LayoutWindow *lw, GtkWidget *window);
GtkWidget *layout_actions_menu_bar(LayoutWindow *lw);

gboolean accel_action_matches(const gchar *action_name, const GdkEventKey *event);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
