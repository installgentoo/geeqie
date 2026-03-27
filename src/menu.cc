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

#include "menu.h"

#include <gdk/gdk.h>

#include "editors.h"
#include "intl.h"
#include "pixbuf-util.h"
#include "ui-menu.h"

/*
 *-----------------------------------------------------------------------------
 * menu utils
 *-----------------------------------------------------------------------------
 */

static GtkWidget *add_menu_item(GtkWidget *menu, gchar *label, GtkAccelGroup *accel_group,
				guint accel_key, guint accel_mods, GCallback func, gpointer data)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label(label);
	gtk_widget_add_accelerator(item, "activate", accel_group, accel_key, static_cast<GdkModifierType>(accel_mods), GTK_ACCEL_VISIBLE);
	g_signal_connect(G_OBJECT(item), "activate", func, data);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show(item);

	return item;
}

gpointer submenu_item_get_data(GtkWidget *menu)
{
	if (!gtk_widget_get_parent(menu) || !GTK_IS_MENU(gtk_widget_get_parent(menu))) return nullptr;

	return g_object_get_data(G_OBJECT(gtk_widget_get_parent(menu)), "submenu_data");
}

/*
 *-----------------------------------------------------------------------------
 * edit menu
 *-----------------------------------------------------------------------------
 */
static void edit_item_destroy_cb(GtkWidget *, gpointer data)
{
	g_free(data);
}

static void add_edit_items(GtkWidget *menu, GCallback func, GList *fd_list)
{
	GList *editors_list = editor_list_get();
	GList *work = editors_list;

	while (work)
		{
		auto editor = static_cast<const EditorDescription *>(work->data);
		work = work->next;
		gboolean active = TRUE;

		if (fd_list && editor_errors(editor_command_parse(editor, fd_list, FALSE, nullptr)))
			active = FALSE;

		if (active)
			{
			GtkWidget *item;
			const gchar *stock_id = nullptr;
			gchar *key = g_strdup(editor->key);

			if (editor->icon && register_theme_icon_as_stock(key, editor->icon))
				stock_id = key;

			item = menu_item_add_stock(menu, editor->name, stock_id, func, key);
			g_signal_connect(G_OBJECT(item), "destroy", G_CALLBACK(edit_item_destroy_cb), key);
			}
		}

	g_list_free(editors_list);
}


GtkWidget *submenu_add_edit(GtkWidget *menu, GtkWidget **menu_item, GCallback func, gpointer data, GList *fd_list)
{
	GtkWidget *item;
	GtkWidget *submenu;
	GtkAccelGroup *accel_group;

	accel_group = gtk_accel_group_new();
	item = menu_item_add(menu, _("_Plugins"), nullptr, nullptr);

	submenu = gtk_menu_new();
	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);
	gtk_menu_set_accel_group(GTK_MENU(submenu), accel_group);
	g_object_set_data(G_OBJECT(submenu), "accel_group", accel_group);

	add_edit_items(submenu, func, fd_list);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);

	if (menu_item) *menu_item = item;

	return submenu;
}

/*
 *-----------------------------------------------------------------------------
 * sorting
 *-----------------------------------------------------------------------------
 */

gchar *sort_type_get_text(SortType method)
{
	switch (method)
		{
		case SORT_SIZE:
			return _("Sort by size");
			break;
		case SORT_TIME:
			return _("Sort by date");
			break;
		case SORT_CTIME:
			return _("Sort by file creation date");
			break;
		case SORT_EXIFTIME:
			return _("Sort by Exif date original");
			break;
		case SORT_EXIFTIMEDIGITIZED:
			return _("Sort by Exif date digitized");
			break;
		case SORT_NONE:
			return _("Unsorted");
			break;
		case SORT_PATH:
			return _("Sort by path");
			break;
		case SORT_NUMBER:
			return _("Sort by number");
			break;
		case SORT_CLASS:
			return _("Sort by class");
			break;
		case SORT_NAME:
		default:
			return _("Sort by name");
			break;
		}

	return nullptr;
}

static GtkWidget *submenu_add_sort_item(GtkWidget *menu,
					GCallback func, SortType type,
					gboolean show_current, SortType show_type)
{
	GtkWidget *item;

	if (show_current)
		{
		item = menu_item_add_radio(menu,
					   sort_type_get_text(type), GINT_TO_POINTER((gint)type), (type == show_type),
					   func, GINT_TO_POINTER((gint)type));
		}
	else
		{
		item = menu_item_add(menu, sort_type_get_text(type),
				     func, GINT_TO_POINTER((gint)type));
		}

	return item;
}

GtkWidget *submenu_add_sort(GtkWidget *menu, GCallback func, gpointer data,
			    gboolean include_none, gboolean include_path,
			    gboolean show_current, SortType type)
{
	GtkWidget *submenu;

	submenu = gtk_menu_new();
	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);

	submenu_add_sort_item(submenu, func, SORT_NAME, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_NUMBER, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_TIME, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_CTIME, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_EXIFTIME, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_EXIFTIMEDIGITIZED, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_SIZE, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_CLASS, show_current, type);
	if (include_path) submenu_add_sort_item(submenu, func, SORT_PATH, show_current, type);
	if (include_none) submenu_add_sort_item(submenu, func, SORT_NONE, show_current, type);

	if (menu)
		{
		GtkWidget *item;

		item = menu_item_add(menu, _("Sort"), nullptr, nullptr);
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
		return item;
		}

	return submenu;
}

GtkWidget *submenu_add_dir_sort(GtkWidget *menu, GCallback func, gpointer data,
			    gboolean include_none, gboolean include_path,
			    gboolean show_current, SortType type)
{
	GtkWidget *submenu;

	submenu = gtk_menu_new();
	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);

	submenu_add_sort_item(submenu, func, SORT_NAME, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_NUMBER, show_current, type);
	submenu_add_sort_item(submenu, func, SORT_TIME, show_current, type);
	if (include_path) submenu_add_sort_item(submenu, func, SORT_PATH, show_current, type);
	if (include_none) submenu_add_sort_item(submenu, func, SORT_NONE, show_current, type);

	if (menu)
		{
		GtkWidget *item;

		item = menu_item_add(menu, _("Sort"), nullptr, nullptr);
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
		return item;
		}

	return submenu;
}

static gchar *zoom_type_get_text(ZoomMode method)
{
	switch (method)
		{
		case ZOOM_RESET_ORIGINAL:
			return _("Zoom to original size");
			break;
		case ZOOM_RESET_FIT_WINDOW:
			return _("Fit image to window");
			break;
		case ZOOM_RESET_NONE:
			return _("Leave Zoom at previous setting");
			break;
		default:
			return _("Zoom to original size");
			break;
		}

	return nullptr;
}

static GtkWidget *submenu_add_zoom_item(GtkWidget *menu,
					GCallback func, ZoomMode mode,
					gboolean show_current, ZoomMode show_mode)
{
	GtkWidget *item;

	if (show_current)
		{
		item = menu_item_add_radio(menu,
					   zoom_type_get_text(mode), GINT_TO_POINTER((gint)mode), (mode == show_mode),
					   func, GINT_TO_POINTER((gint)mode));
		}
	else
		{
		item = menu_item_add(menu, zoom_type_get_text(mode),
				     func, GINT_TO_POINTER((gint)mode));
		}

	return item;
}

GtkWidget *submenu_add_zoom(GtkWidget *menu, GCallback func, gpointer data,
			    gboolean, gboolean, gboolean show_current, ZoomMode mode)
{
	GtkWidget *submenu;

	if (!menu)
		{
		submenu = gtk_menu_new();
		g_object_set_data(G_OBJECT(submenu), "submenu_data", data);
		}
	else
		{
		submenu = menu;
		}

	submenu_add_zoom_item(submenu, func, ZOOM_RESET_ORIGINAL, show_current, mode);
	submenu_add_zoom_item(submenu, func, ZOOM_RESET_FIT_WINDOW, show_current, mode);
	submenu_add_zoom_item(submenu, func, ZOOM_RESET_NONE, show_current, mode);

	if (menu)
		{
		GtkWidget *item;

		item = menu_item_add(menu, _("Zoom"), nullptr, nullptr);
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
		return item;
		}

	return submenu;
}
