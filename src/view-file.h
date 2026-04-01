/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Laurent Monin
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

#ifndef VIEW_FILE_H
#define VIEW_FILE_H

#include <ctime>
#include <functional>

#include <glib.h>
#include <gtk/gtk.h>

#include "typedefs.h"

class FileData;
struct LayoutWindow;
struct ThumbLoader;

struct ViewFile
{
	gpointer info;

	GtkWidget *widget;
	GtkWidget *listview;
	GtkWidget *scrolled;

	struct {
		GtkWidget *combo;
		GtkWidget *frame;
		gint count;
		gint last_selected;
		gboolean case_sensitive;
	} file_filter;

	FileData *dir_fd;
	GList *list;

	FileData *click_fd;

	SortType sort_method;
	gboolean sort_ascend;
	gboolean sort_case;

	/* func list */
	void (*func_thumb_status)(ViewFile *vf, gdouble val, const gchar *text, gpointer data);
	gpointer data_thumb_status;

	void (*func_status)(ViewFile *vf, gpointer data);
	gpointer data_status;

	LayoutWindow *layout;

	GtkWidget *popup;

	/* thumbs updates*/
	gboolean thumbs_running;
	ThumbLoader *thumbs_loader;
	FileData *thumbs_filedata;
	GHashTable *thumbs_priority;
	guint thumbs_scroll_id;
	GQueue *thumbs_lru;
	GHashTable *thumbs_lru_index;

	/* refresh */
	guint refresh_idle_id; /**< event source id */
	time_t time_refresh_set; /**< time when refresh_idle_id was set */

	GList *editmenu_fd_list; /**< file list for edit menu */

	using SelectionCallback = std::function<void(FileData *)>;
};

void vf_send_update(ViewFile *vf);

ViewFile *vf_new(FileData *dir_fd);

void vf_set_status_func(ViewFile *vf, void (*func)(ViewFile *vf, gpointer data), gpointer data);
void vf_set_thumb_status_func(ViewFile *vf, void (*func)(ViewFile *vf, gdouble val, const gchar *text, gpointer data), gpointer data);

void vf_set_layout(ViewFile *vf, LayoutWindow *layout);

gboolean vf_set_fd(ViewFile *vf, FileData *fd);
gboolean vf_refresh(ViewFile *vf);
void vf_refresh_idle(ViewFile *vf);

void vf_sort_set(ViewFile *vf, SortType type, gboolean ascend, gboolean case_sensitive);

guint vf_class_get_filter(ViewFile *vf);

GList *vf_selection_get_one(ViewFile *vf, FileData *fd);
GList *vf_pop_menu_file_list(ViewFile *vf);
GtkWidget *vf_pop_menu(ViewFile *vf);

FileData *vf_index_get_data(ViewFile *vf, gint row);
gint vf_index_by_fd(ViewFile *vf, FileData *in_fd);
guint vf_count(ViewFile *vf, gint64 *bytes);
GList *vf_get_list(ViewFile *vf);

guint vf_selection_count(ViewFile *vf, gint64 *bytes);
GList *vf_selection_get_list(ViewFile *vf);
GList *vf_selection_get_list_by_index(ViewFile *vf);

void vf_select_all(ViewFile *vf);
void vf_select_none(ViewFile *vf);
void vf_select_invert(ViewFile *vf);
void vf_select_by_fd(ViewFile *vf, FileData *fd);

void vf_refresh_idle_cancel(ViewFile *vf);
void vf_notify_cb(FileData *fd, NotifyType type, gpointer data);

void vf_thumb_update(ViewFile *vf);
void vf_thumb_cleanup(ViewFile *vf);
void vf_file_filter_set(ViewFile *vf, gboolean enable);
GRegex *vf_file_filter_get_filter(ViewFile *vf);

#endif /* VIEW_FILE_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
