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

#ifndef LAYOUT_H
#define LAYOUT_H

#include <glib.h>
#include <gtk/gtk.h>

#include "options.h"
#include "typedefs.h"

struct AnimationData;
class FileData;
struct FullScreenData;
struct ImageWindow;
struct ViewDir;
struct ViewFile;

struct LayoutWindow;
extern LayoutWindow *main_lw;

struct LayoutWindow
{
	LayoutOptions options;

	FileData *dir_fd;

	/* base */

	GtkWidget *window;

	GtkWidget *main_box;

	GtkWidget *group_box;
	GtkWidget *h_pane;
	GtkWidget *v_pane;

	/* menus, path selector */

	GtkActionGroup *action_group;
	GtkActionGroup *action_group_editors;
	guint ui_editors_id;
	GtkUIManager *ui_manager;

	GtkWidget *path_entry;

	ImageWindow *image;

	GtkWidget *menu_bar; /**< referenced by lw, exist during whole lw lifetime */
	/* toolbar */

	ViewDir *vd;
	GtkWidget *dir_view;

	ViewFile *vf;

	GtkWidget *file_view;

	GtkWidget *info_box; /**< status bar */
	GtkWidget *info_progress_bar; /**< status bar */
	GtkWidget *info_sort; /**< status bar */
	GtkWidget *info_status; /**< status bar */
	GtkWidget *info_details; /**< status bar */
	GtkWidget *info_zoom; /**< status bar */

	/* full screen */

	FullScreenData *full_screen;

	AnimationData *animation;

	GtkWidget *log_window;
};

LayoutWindow *layout_new_with_geometry(FileData *dir_fd, LayoutOptions *lop,
				       const gchar *geometry);
LayoutWindow *layout_new_from_config(const gchar **attribute_names, const gchar **attribute_values, gboolean use_commandline);
LayoutWindow *layout_new_from_default();

gboolean layout_valid(LayoutWindow **lw);

void layout_show_config_window(LayoutWindow *lw);

void layout_sync_options_with_current_state(LayoutWindow *lw);
void layout_load_attributes(LayoutOptions *layout, const gchar **attribute_names, const gchar **attribute_values);
void layout_write_attributes(LayoutOptions *layout, GString *outstr, gint indent);
void layout_write_config(LayoutWindow *lw, GString *outstr, gint indent);


gint layout_compare_options_id(const LayoutWindow *lw, const gchar *id);

const gchar *layout_get_path(LayoutWindow *lw);
gboolean layout_set_path(LayoutWindow *lw, const gchar *path);
gboolean layout_set_fd(LayoutWindow *lw, FileData *fd);

void layout_status_update_progress(LayoutWindow *lw, gdouble val, const gchar *text);
void layout_status_update_info(LayoutWindow *lw, const gchar *text);
void layout_status_update_image(LayoutWindow *lw);

GList *layout_list(LayoutWindow *lw);
guint layout_list_count(LayoutWindow *lw, gint64 *bytes);
FileData *layout_list_get_fd(LayoutWindow *lw, gint index);
gint layout_list_get_index(LayoutWindow *lw, FileData *fd);
void layout_list_sync_fd(LayoutWindow *lw, FileData *fd);

GList *layout_selection_list(LayoutWindow *lw);
/* return list of pointers to int for selection */
GList *layout_selection_list_by_index(LayoutWindow *lw);
guint layout_selection_count(LayoutWindow *lw, gint64 *bytes);
void layout_select_all(LayoutWindow *lw);
void layout_select_none(LayoutWindow *lw);
void layout_select_invert(LayoutWindow *lw);

void layout_refresh(LayoutWindow *lw);

void layout_file_filter_set(LayoutWindow *lw, gboolean enable);

void layout_sort_set_files(LayoutWindow *lw, SortType type, gboolean ascend, gboolean case_sensitive);

gboolean layout_geometry_get_dividers(LayoutWindow *lw, gint *h, gint *v);

void layout_views_set(LayoutWindow *lw);

void layout_views_set_sort_dir(LayoutWindow *lw, SortType method, gboolean ascend, gboolean case_sensitive);

void layout_status_update(LayoutWindow *lw, const gchar *text);

void layout_menu_update_edit();
void layout_styles_update();

gchar *layout_get_unique_id();

#endif
