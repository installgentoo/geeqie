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

gboolean layout_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data);

void layout_util_sync_file_filter(LayoutWindow *lw);
void layout_util_sync_color(LayoutWindow *lw);
void layout_util_sync(LayoutWindow *lw);

void layout_recent_add_path(const gchar *path);

void layout_copy_path_update_all();

void layout_actions_setup(LayoutWindow *lw);
void layout_actions_add_window(LayoutWindow *lw, GtkWidget *window);
GtkWidget *layout_actions_menu_bar(LayoutWindow *lw);
void layout_toolbar_add_from_config(LayoutWindow *lw, ToolbarType type, const gchar **attribute_names, const gchar **attribute_values);

GtkWidget *layout_actions_toolbar(LayoutWindow *lw, ToolbarType type);

void layout_toolbar_write_config(LayoutWindow *lw, ToolbarType type, GString *outstr, gint indent);
void layout_toolbar_clear(LayoutWindow *lw, ToolbarType type);
void layout_toolbar_add(LayoutWindow *lw, ToolbarType type, const gchar *action);
void layout_toolbar_add_default(LayoutWindow *lw, ToolbarType type);
void layout_keyboard_init(LayoutWindow *lw, GtkWidget *window);

GtkWidget *layout_actions_menu_tool_bar(LayoutWindow *lw);

void create_toolbars(LayoutWindow *lw);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
