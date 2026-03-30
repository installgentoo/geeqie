/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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

#ifndef MISC_H
#define MISC_H

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <config.h>

const gchar *gq_gtk_entry_get_text(GtkEntry *entry);
gchar *expand_tilde(const gchar *filename);
gchar *utf8_validate_or_convert(const gchar *text);
gdouble get_zoom_increment();
gint get_cpu_cores();
gint utf8_compare(const gchar *s1, const gchar *s2, gboolean case_sensitive);
gint gq_gtk_tree_iter_utf8_collate(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gint sort_column_id);
int runcmd(const gchar *cmd);
void gq_gtk_entry_set_text(GtkEntry *entry, const gchar *text);
void gq_gtk_grid_attach_default(GtkGrid *grid, GtkWidget *child, guint left_attach, guint right_attach, guint top_attach, guint bottom_attach);
void gq_gtk_grid_attach(GtkGrid *grid, GtkWidget *child, guint left_attach, guint right_attach, guint top_attach, guint bottom_attach, GtkAttachOptions, GtkAttachOptions, guint, guint);

void convert_gdkcolor_to_gdkrgba(gpointer data, GdkRGBA *gdk_rgba);

void cell_renderer_height_override(GtkCellRenderer *renderer); /**< cell max with/height hack utility */
void widget_set_cursor(GtkWidget *widget, gint icon);

#endif /* MISC_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
