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

#include "view-file.h"

#include <array>

#include <gdk/gdk.h>
#include <glib-object.h>

#include "compat.h"
#include "debug.h"
#include "dnd.h"
#include "dupe.h"
#include "filedata.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "main.h"
#include "menu.h"
#include "misc.h"
#include "options.h"
#include "thumb-standard.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "uri-utils.h"
#include "utilops.h"
#include "view-file/view-file-icon.h"

/*
 *-----------------------------------------------------------------------------
 * signals
 *-----------------------------------------------------------------------------
 */

void vf_send_update(ViewFile *vf)
{
	if (vf->func_status) vf->func_status(vf, vf->data_status);
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

void vf_sort_set(ViewFile *vf, SortType type, gboolean ascend, gboolean case_sensitive)
{
	
	{

	vficon_sort_set(vf, type, ascend, case_sensitive);
	}
}

/*
 *-----------------------------------------------------------------------------
 * row stuff
 *-----------------------------------------------------------------------------
 */

FileData *vf_index_get_data(ViewFile *vf, gint row)
{
	return static_cast<FileData *>(g_list_nth_data(vf->list, row));
}

gint vf_index_by_fd(ViewFile *vf, FileData *fd)
{
	return vficon_index_by_fd(vf, fd);
}

guint vf_count(ViewFile *vf, gint64 *bytes)
{
	if (bytes)
		{
		gint64 b = 0;
		GList *work;

		work = vf->list;
		while (work)
			{
			auto fd = static_cast<FileData *>(work->data);
			work = work->next;

			b += fd->size;
			}

		*bytes = b;
		}

	return g_list_length(vf->list);
}

/*
 *-------------------------------------------------------------------
 * keyboard
 *-------------------------------------------------------------------
 */

static gboolean vf_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	return vficon_press_key_cb(vf, widget, event);
}

/*
 *-------------------------------------------------------------------
 * mouse
 *-------------------------------------------------------------------
 */

static gboolean vf_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	return vficon_press_cb(vf, widget, bevent);
}

static gboolean vf_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	return vficon_release_cb(vf, widget, bevent);
}


/*
 *-----------------------------------------------------------------------------
 * selections
 *-----------------------------------------------------------------------------
 */

guint vf_selection_count(ViewFile *vf, gint64 *bytes)
{
	return vficon_selection_count(vf, bytes);
}

GList *vf_selection_get_list(ViewFile *vf)
{
	return vficon_selection_get_list(vf);
}

GList *vf_selection_get_list_by_index(ViewFile *vf)
{
	return vficon_selection_get_list_by_index(vf);
}

void vf_select_all(ViewFile *vf)
{
	
	{

	vficon_select_all(vf);
	}
}

void vf_select_none(ViewFile *vf)
{
	
	{

	vficon_select_none(vf);
	}
}

void vf_select_invert(ViewFile *vf)
{
	
	{

	vficon_select_invert(vf);
	}
}

void vf_select_by_fd(ViewFile *vf, FileData *fd)
{
	
	{

	vficon_select_by_fd(vf, fd);
	}
}

/*
 *-----------------------------------------------------------------------------
 * dnd
 *-----------------------------------------------------------------------------
 */

static gboolean vf_is_selected(ViewFile *vf, FileData *fd)
{
	{

	return vficon_is_selected(vf, fd);
	}
}

static void vf_dnd_get(GtkWidget *, GdkDragContext *,
                       GtkSelectionData *selection_data, guint,
                       guint, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);

	if (!vf->click_fd) return;

	GList *list = nullptr;

	if (vf_is_selected(vf, vf->click_fd))
		{
		list = vf_selection_get_list(vf);
		}
	else
		{
		list = g_list_append(nullptr, file_data_ref(vf->click_fd));
		}

	if (!list) return;

	uri_selection_data_set_uris_from_filelist(selection_data, list);
	filelist_free(list);
}

static void vf_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);

	
	{
	vficon_dnd_begin(vf, widget, context);
	}
}

static void vf_dnd_end(GtkWidget *, GdkDragContext *context, gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);

	
	{
	vficon_dnd_end(vf, context);
	}
}

static FileData *vf_find_data_by_coord(ViewFile *vf, gint x, gint y, GtkTreeIter *iter)
{
	return vficon_find_data_by_coord(vf, x, y, iter);
}

static void vf_drag_data_received(GtkWidget *, GdkDragContext *,
                                  int x, int y, GtkSelectionData *,
                                  guint info, guint, gpointer data)
{
	if (info != TARGET_TEXT_PLAIN) return;

	auto *vf = static_cast<ViewFile *>(data);

	FileData *fd = vf_find_data_by_coord(vf, x, y, nullptr);
	if (!fd) return;
}

static void vf_dnd_init(ViewFile *vf)
{
	gtk_drag_source_set(vf->listview, static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
	                    dnd_file_drag_types.data(), dnd_file_drag_types.size(),
	                    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	gtk_drag_dest_set(vf->listview, GTK_DEST_DEFAULT_ALL,
	                  dnd_file_drag_types.data(), dnd_file_drag_types.size(),
	                  static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));

	g_signal_connect(G_OBJECT(vf->listview), "drag_data_get",
	                 G_CALLBACK(vf_dnd_get), vf);
	g_signal_connect(G_OBJECT(vf->listview), "drag_begin",
	                 G_CALLBACK(vf_dnd_begin), vf);
	g_signal_connect(G_OBJECT(vf->listview), "drag_end",
	                 G_CALLBACK(vf_dnd_end), vf);
	g_signal_connect(G_OBJECT(vf->listview), "drag_data_received",
	                 G_CALLBACK(vf_drag_data_received), vf);
}

/*
 *-----------------------------------------------------------------------------
 * pop-up menu
 *-----------------------------------------------------------------------------
 */

GList *vf_pop_menu_file_list(ViewFile *vf)
{
	if (!vf->click_fd) return nullptr;

	if (vf_is_selected(vf, vf->click_fd))
		{
		return vf_selection_get_list(vf);
		}

	return vf_selection_get_one(vf, vf->click_fd);
}

GList *vf_selection_get_one(ViewFile *vf, FileData *fd)
{
	return vficon_selection_get_one(vf, fd);
}

static void vf_pop_menu_edit_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf;
	auto key = static_cast<const gchar *>(data);

	vf = static_cast<ViewFile *>(submenu_item_get_data(widget));

	if (!vf) return;

	file_util_start_editor_from_filelist(key, vf_pop_menu_file_list(vf), vf->dir_fd->path, vf->listview);
}

static void vf_pop_menu_copy_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_util_copy(nullptr, vf_pop_menu_file_list(vf), nullptr, vf->listview);
}

static void vf_pop_menu_move_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_util_move(nullptr, vf_pop_menu_file_list(vf), nullptr, vf->listview);
}

static void vf_pop_menu_rename_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	
	{

	vficon_pop_menu_rename_cb(vf);
	}
}

static void vf_pop_menu_delete_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_util_delete(nullptr, vf_pop_menu_file_list(vf), vf->listview);
}

static void vf_pop_menu_copy_path_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	file_util_path_list_to_clipboard(vf_pop_menu_file_list(vf), TRUE);
}

static void vf_pop_menu_duplicates_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	DupeWindow *dw;

	dw = dupe_window_new();
	dupe_window_add_files(dw, vf_pop_menu_file_list(vf), FALSE);
}

static void vf_pop_menu_sort_cb(GtkWidget *widget, gpointer data)
{
	ViewFile *vf;
	SortType type;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	vf = static_cast<ViewFile *>(submenu_item_get_data(widget));
	if (!vf) return;

	type = static_cast<SortType>GPOINTER_TO_INT(data);

	if (vf->layout)
		{
		layout_sort_set_files(vf->layout, type, vf->sort_ascend, vf->sort_case);
		}
	else
		{
		vf_sort_set(vf, type, vf->sort_ascend, vf->sort_case);
		}
}

static void vf_pop_menu_sort_ascend_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	if (vf->layout)
		{
		layout_sort_set_files(vf->layout, vf->sort_method, !vf->sort_ascend, vf->sort_case);
		}
	else
		{
		vf_sort_set(vf, vf->sort_method, !vf->sort_ascend, vf->sort_case);
		}
}

static void vf_pop_menu_sort_case_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	if (vf->layout)
		{
		layout_sort_set_files(vf->layout, vf->sort_method, vf->sort_ascend, !vf->sort_case);
		}
	else
		{
		vf_sort_set(vf, vf->sort_method, vf->sort_ascend, !vf->sort_case);
		}
}


static void vf_pop_menu_refresh_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vficon_pop_menu_refresh_cb(vf);
}

static void vf_popup_destroy_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	
	{

	vficon_popup_destroy_cb(vf);
	}

	vf->click_fd = nullptr;
	vf->popup = nullptr;

	filelist_free(vf->editmenu_fd_list);
	vf->editmenu_fd_list = nullptr;
}

GtkWidget *vf_pop_menu(ViewFile *vf)
{
	GtkWidget *menu;
	GtkWidget *item;
	GtkWidget *submenu;
	gboolean active = FALSE;
	GtkAccelGroup *accel_group;

	active = (vf->click_fd != nullptr);

	menu = popup_menu_short_lived();

	accel_group = gtk_accel_group_new();
	gtk_menu_set_accel_group(GTK_MENU(menu), accel_group);

	g_object_set_data(G_OBJECT(menu), "accel_group", accel_group);

	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(vf_popup_destroy_cb), vf);

	vf->editmenu_fd_list = vf_pop_menu_file_list(vf);
	submenu_add_edit(menu, &item, G_CALLBACK(vf_pop_menu_edit_cb), vf, vf->editmenu_fd_list);
	gtk_widget_set_sensitive(item, active);

	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, _("_Copy..."), GQ_ICON_COPY, active,
				      G_CALLBACK(vf_pop_menu_copy_cb), vf);
	menu_item_add_sensitive(menu, _("_Move..."), active,
				G_CALLBACK(vf_pop_menu_move_cb), vf);
	menu_item_add_sensitive(menu, _("_Rename..."), active,
				G_CALLBACK(vf_pop_menu_rename_cb), vf);
	menu_item_add_sensitive(menu, _("_Copy to clipboard"), active,
				G_CALLBACK(vf_pop_menu_copy_path_cb), vf);
	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu,
				options->file_ops.confirm_delete ? _("_Delete selection...") :
					_("_Delete selection"), GQ_ICON_DELETE_SHRED, active,
				G_CALLBACK(vf_pop_menu_delete_cb), vf);
	menu_item_add_divider(menu);

	menu_item_add_icon_sensitive(menu, _("_Find duplicates..."), GQ_ICON_FIND, active,
				G_CALLBACK(vf_pop_menu_duplicates_cb), vf);
	menu_item_add_divider(menu);

	submenu = submenu_add_sort(nullptr, G_CALLBACK(vf_pop_menu_sort_cb), vf,
				   FALSE, FALSE, TRUE, vf->sort_method);
	menu_item_add_divider(submenu);
	menu_item_add_check(submenu, _("Ascending"), vf->sort_ascend,
			    G_CALLBACK(vf_pop_menu_sort_ascend_cb), vf);
	menu_item_add_check(submenu, _("Case"), vf->sort_ascend,
			    G_CALLBACK(vf_pop_menu_sort_case_cb), vf);

	item = menu_item_add(menu, _("_Sort"), nullptr, nullptr);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);

	vficon_pop_menu_add_items(vf, menu);

	menu_item_add_icon(menu, _("Re_fresh"), GQ_ICON_REFRESH, G_CALLBACK(vf_pop_menu_refresh_cb), vf);

	return menu;
}

gboolean vf_refresh(ViewFile *vf)
{
	return vficon_refresh(vf);
}

gboolean vf_refresh_filter(ViewFile *vf)
{
	return vficon_refresh_filter(vf);
}

static void vf_thumb_stop(ViewFile *vf);
gboolean vf_set_fd(ViewFile *vf, FileData *dir_fd)
{
	vf_thumb_stop(vf);
	g_clear_handle_id(&vf->thumbs_scroll_id, g_source_remove);

	return vficon_set_fd(vf, dir_fd);
}

static void vf_destroy_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	{

	vficon_destroy_cb(vf);
	}

	if (vf->popup)
		{
		g_signal_handlers_disconnect_matched(G_OBJECT(vf->popup), G_SIGNAL_MATCH_DATA,
						     0, 0, nullptr, nullptr, vf);
		gq_gtk_widget_destroy(vf->popup);
		}

	g_clear_handle_id(&vf->thumbs_scroll_id, g_source_remove);
	g_clear_handle_id(&vf->file_filter.refresh_idle_id, g_source_remove);
	filelist_free(vf->list_raw);
	file_data_unref(vf->dir_fd);
	g_free(vf->info);
	g_free(vf);
}

static gboolean vf_file_filter_refresh_idle_cb(gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vf->file_filter.refresh_idle_id = 0;
	vf_refresh_filter(vf);

	return G_SOURCE_REMOVE;
}

static void vf_file_filter_refresh_schedule(ViewFile *vf)
{
	g_clear_handle_id(&vf->file_filter.refresh_idle_id, g_source_remove);
	/* Debounce expensive refreshes while typing in the filter entry. */
	vf->file_filter.refresh_idle_id = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 150,
							     vf_file_filter_refresh_idle_cb, vf, nullptr);
}

static void vf_file_filter_save_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gchar *entry_text;
	gchar *remove_text = nullptr;

	entry_text = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(vf->file_filter.combo)))));

	if (entry_text[0] == '\0' && vf->file_filter.last_selected >= 0)
		{
		gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), vf->file_filter.last_selected);
		remove_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(vf->file_filter.combo));
		gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(vf->file_filter.combo), vf->file_filter.last_selected);
		g_free(remove_text);

		gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), -1);
		vf->file_filter.last_selected = - 1;
		gq_gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(vf->file_filter.combo))), "");
		vf->file_filter.count--;
		}
	else
		{
		if (entry_text[0] != '\0')
			{
			gboolean text_found = FALSE;

			for (gint i = 0; i < vf->file_filter.count; i++)
				{
				gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), i);

				g_autofree gchar *index_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(vf->file_filter.combo));
				if (g_strcmp0(index_text, entry_text) == 0)
					{
					text_found = TRUE;
					break;
					}
				}

			if (!text_found)
				{
				gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(vf->file_filter.combo), entry_text);
				vf->file_filter.count++;
				gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), vf->file_filter.count - 1);
				}
			}
		}
	vf_file_filter_refresh_schedule(vf);

	g_free(entry_text);
}

static void vf_file_filter_cb(GtkWidget *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vf_file_filter_refresh_schedule(vf);
}

static gboolean vf_file_filter_press_cb(GtkWidget *widget, GdkEventButton *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vf->file_filter.last_selected = gtk_combo_box_get_active(GTK_COMBO_BOX(vf->file_filter.combo));

	gtk_widget_grab_focus(widget);

	return TRUE;
}

void vf_file_filter_set(ViewFile *vf, gboolean enable)
{
	if (enable)
		{
		gtk_widget_show(vf->file_filter.combo);
		gtk_widget_show(vf->file_filter.frame);
		}
	else
		{
		gtk_widget_hide(vf->file_filter.combo);
		gtk_widget_hide(vf->file_filter.frame);
		}

	vf_file_filter_refresh_schedule(vf);
}

static gboolean vf_file_filter_class_cb(GtkWidget *widget, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gint i;

	gboolean state = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));

	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		if (g_strcmp0(format_class_list[i], gtk_menu_item_get_label(GTK_MENU_ITEM(widget))) == 0)
			{
			options->class_filter[i] = state;
			}
		}
	vf_file_filter_refresh_schedule(vf);

	return TRUE;
}

static gboolean vf_file_filter_class_set_all_cb(GtkWidget *widget, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	GtkWidget *parent;
	GList *children;
	gint i;
	gboolean state;

	if (g_strcmp0(_("Select all"), gtk_menu_item_get_label(GTK_MENU_ITEM(widget))) == 0)
		{
		state = TRUE;
		}
	else
		{
		state = FALSE;
		}

	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		options->class_filter[i] = state;
		}

	i = 0;
	parent = gtk_widget_get_parent(widget);
	children = gtk_container_get_children(GTK_CONTAINER(parent));
	for (GList *work = children; work; work = work->next)
		{
		if (i < FILE_FORMAT_CLASSES)
			{
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(work->data), state);
			}
		i++;
		}
	g_list_free(children);
	vf_file_filter_refresh_schedule(vf);

	return TRUE;
}

static GtkWidget *class_filter_menu (ViewFile *vf)
{
	GtkWidget *menu;
	GtkWidget *menu_item;
	int i;

	menu = gtk_menu_new();

	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
	    {
		menu_item = gtk_check_menu_item_new_with_label(format_class_list[i]);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item), options->class_filter[i]);
		g_signal_connect(G_OBJECT(menu_item), "toggled", G_CALLBACK(vf_file_filter_class_cb), vf);
		gtk_menu_shell_append(GTK_MENU_SHELL (menu), menu_item);
		gtk_widget_show(menu_item);
		}

	menu_item = gtk_menu_item_new_with_label(_("Select all"));
	gtk_menu_shell_append(GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show(menu_item);
	g_signal_connect(G_OBJECT(menu_item), "activate", G_CALLBACK(vf_file_filter_class_set_all_cb), vf);

	menu_item = gtk_menu_item_new_with_label(_("Select none"));
	gtk_menu_shell_append(GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show(menu_item);
	g_signal_connect(G_OBJECT(menu_item), "activate", G_CALLBACK(vf_file_filter_class_set_all_cb), vf);

	return menu;
}

static void case_sensitive_cb(GtkWidget *widget, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vf->file_filter.case_sensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	vf_file_filter_refresh_schedule(vf);
}

static void file_filter_clear_cb(GtkEntry *, GtkEntryIconPosition pos, GdkEvent *, gpointer userdata)
{
	if (pos == GTK_ENTRY_ICON_SECONDARY)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(userdata), "");
		gtk_widget_grab_focus(GTK_WIDGET(userdata));
		}
}

static GtkWidget *vf_file_filter_init(ViewFile *vf)
{
	GtkWidget *frame = gtk_frame_new(nullptr);
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *combo_entry;
	GtkWidget *menubar;
	GtkWidget *menuitem;
	GtkWidget *case_sensitive;
	GtkWidget *box;
	GtkWidget *icon;
	GtkWidget *label;

	vf->file_filter.combo = gtk_combo_box_text_new_with_entry();
	combo_entry = gtk_bin_get_child(GTK_BIN(vf->file_filter.combo));
	gtk_widget_show(gtk_bin_get_child(GTK_BIN(vf->file_filter.combo)));
	gtk_widget_show((GTK_WIDGET(vf->file_filter.combo)));
	gtk_widget_set_tooltip_text(GTK_WIDGET(vf->file_filter.combo), _("Use regular expressions"));

	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(combo_entry), GTK_ENTRY_ICON_SECONDARY, GQ_ICON_CLEAR);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY(combo_entry), GTK_ENTRY_ICON_SECONDARY, _("Clear"));
	g_signal_connect(GTK_ENTRY(combo_entry), "icon-press", G_CALLBACK(file_filter_clear_cb), combo_entry);
	gtk_combo_box_set_active(GTK_COMBO_BOX(vf->file_filter.combo), -1);
	gq_gtk_entry_set_text(GTK_ENTRY(combo_entry), "");

	g_signal_connect(G_OBJECT(combo_entry), "activate",
		G_CALLBACK(vf_file_filter_save_cb), vf);

	g_signal_connect(G_OBJECT(vf->file_filter.combo), "changed",
		G_CALLBACK(vf_file_filter_cb), vf);

	g_signal_connect(G_OBJECT(combo_entry), "button_press_event",
			 G_CALLBACK(vf_file_filter_press_cb), vf);

	gq_gtk_box_pack_start(GTK_BOX(hbox), vf->file_filter.combo, FALSE, FALSE, 0);
	gtk_widget_show(vf->file_filter.combo);
	gq_gtk_container_add(GTK_WIDGET(frame), hbox);
	gtk_widget_show(hbox);

	case_sensitive = gtk_check_button_new_with_label(_("Case"));
	gq_gtk_box_pack_start(GTK_BOX(hbox), case_sensitive, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text(GTK_WIDGET(case_sensitive), _("Case sensitive"));
	g_signal_connect(G_OBJECT(case_sensitive), "clicked", G_CALLBACK(case_sensitive_cb), vf);
	gtk_widget_show(case_sensitive);

	menubar = gtk_menu_bar_new();
	gq_gtk_box_pack_start(GTK_BOX(hbox), menubar, FALSE, TRUE, 0);
	gtk_widget_show(menubar);

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	icon = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN, GTK_ICON_SIZE_MENU);
	label = gtk_label_new(_("Class"));

	gq_gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	gq_gtk_box_pack_end(GTK_BOX(box), icon, FALSE, FALSE, 0);

	menuitem = gtk_menu_item_new();

	gtk_widget_set_tooltip_text(GTK_WIDGET(menuitem), _("Select Class filter"));
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), class_filter_menu(vf));
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menuitem);
	gq_gtk_container_add(GTK_WIDGET(menuitem), box);
	gq_gtk_widget_show_all(menuitem);

	return frame;
}

static void vf_thumb_scroll_changed_cb(GtkAdjustment *, gpointer data);

ViewFile *vf_new(FileData *dir_fd)
{
	ViewFile *vf;

	vf = g_new0(ViewFile, 1);

	vf->sort_method = SORT_NAME;
	vf->sort_ascend = TRUE;

	vf->scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(vf->scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(vf->scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	vf->file_filter.frame = vf_file_filter_init(vf);

	vf->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(vf->widget), vf->file_filter.frame, FALSE, FALSE, 0);
	gq_gtk_box_pack_start(GTK_BOX(vf->widget), vf->scrolled, TRUE, TRUE, 0);
	gtk_widget_show(vf->scrolled);

	g_signal_connect(G_OBJECT(vf->widget), "destroy",
			 G_CALLBACK(vf_destroy_cb), vf);

	vf = vficon_new(vf);

	vf_dnd_init(vf);

	g_signal_connect(G_OBJECT(vf->listview), "key_press_event",
			 G_CALLBACK(vf_press_key_cb), vf);
	g_signal_connect(G_OBJECT(vf->listview), "button_press_event",
			 G_CALLBACK(vf_press_cb), vf);
	g_signal_connect(G_OBJECT(vf->listview), "button_release_event",
			 G_CALLBACK(vf_release_cb), vf);

	gq_gtk_container_add(GTK_WIDGET(vf->scrolled), vf->listview);
	gtk_widget_show(vf->listview);

	g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(vf->scrolled)),
			 "value_changed", G_CALLBACK(vf_thumb_scroll_changed_cb), vf);
	g_signal_connect(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(vf->scrolled)),
			 "value_changed", G_CALLBACK(vf_thumb_scroll_changed_cb), vf);

	if (dir_fd) vf_set_fd(vf, dir_fd);

	return vf;
}

void vf_set_status_func(ViewFile *vf, void (*func)(ViewFile *vf, gpointer data), gpointer data)
{
	vf->func_status = func;
	vf->data_status = data;
}

void vf_set_thumb_status_func(ViewFile *vf, void (*func)(ViewFile *vf, gdouble val, const gchar *text, gpointer data), gpointer data)
{
	vf->func_thumb_status = func;
	vf->data_thumb_status = data;
}

static gboolean vf_thumb_next(ViewFile *vf);
static gboolean vf_thumb_scroll_idle_cb(gpointer data);
constexpr guint THUMB_LRU_LIMIT = 360;

static void vf_thumb_scroll_changed_cb(GtkAdjustment *, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	g_clear_handle_id(&vf->thumbs_scroll_id, g_source_remove);
	vf->thumbs_scroll_id = g_idle_add(vf_thumb_scroll_idle_cb, vf);
}

static gboolean vf_thumb_scroll_idle_cb(gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	vf->thumbs_scroll_id = 0;

	if (!gtk_widget_get_realized(vf->listview)) return G_SOURCE_REMOVE;
	if (vf->thumbs_running) return G_SOURCE_REMOVE;

	vf_thumb_update(vf);
	return G_SOURCE_REMOVE;
}

static void vf_thumb_lru_add(ViewFile *vf, FileData *fd)
{
	if (!fd || !fd->thumb_pixbuf) return;

	if (!vf->thumbs_lru) vf->thumbs_lru = g_queue_new();
	if (!vf->thumbs_lru_index) vf->thumbs_lru_index = g_hash_table_new(g_direct_hash, g_direct_equal);

	if (g_hash_table_contains(vf->thumbs_lru_index, fd))
		{
		if (GList *link = g_queue_find(vf->thumbs_lru, fd); link)
			{
			g_queue_unlink(vf->thumbs_lru, link);
			g_queue_push_head_link(vf->thumbs_lru, link);
			}
		return;
		}

	file_data_ref(fd);
	g_queue_push_head(vf->thumbs_lru, fd);
	g_hash_table_add(vf->thumbs_lru_index, fd);

	while (vf->thumbs_lru->length > THUMB_LRU_LIMIT)
		{
		auto old_fd = static_cast<FileData *>(g_queue_pop_tail(vf->thumbs_lru));
		if (!old_fd) break;

		g_hash_table_remove(vf->thumbs_lru_index, old_fd);
		if (old_fd->thumb_pixbuf)
			{
			g_object_unref(old_fd->thumb_pixbuf);
			old_fd->thumb_pixbuf = nullptr;
			}
		file_data_unref(old_fd);
		}
}

static gdouble vf_thumb_progress(ViewFile *vf)
{
	gint count = 0;
	gint done = 0;

	
	{

	vficon_thumb_progress_count(vf->list, count, done);
	}

	DEBUG_1("thumb progress: %d of %d", done, count);
	return static_cast<gdouble>(done) / count;
}

static void vf_set_thumb_fd(ViewFile *vf, FileData *fd)
{
	
	{

	vficon_set_thumb_fd(vf, fd);
	}
}

static void vf_thumb_status(ViewFile *vf, gdouble val, const gchar *text)
{
	if (vf->func_thumb_status)
		{
		vf->func_thumb_status(vf, val, text, vf->data_thumb_status);
		}
}

static void vf_thumb_do(ViewFile *vf, FileData *fd)
{
	if (!fd) return;

	vf_set_thumb_fd(vf, fd);
	vf_thumb_lru_add(vf, fd);
	vf_thumb_status(vf, vf_thumb_progress(vf), _("Loading thumbs..."));
}

void vf_thumb_cleanup(ViewFile *vf)
{
	vf_thumb_status(vf, 0.0, nullptr);

	vf->thumbs_running = FALSE;

	thumb_loader_free(vf->thumbs_loader);
	vf->thumbs_loader = nullptr;

	vf->thumbs_filedata = nullptr;
	g_clear_pointer(&vf->thumbs_priority, g_hash_table_destroy);
	g_clear_handle_id(&vf->thumbs_scroll_id, g_source_remove);
}

static void vf_thumb_stop(ViewFile *vf)
{
	if (vf->thumbs_running) vf_thumb_cleanup(vf);
}

static void vf_thumb_common_cb(ThumbLoader *tl, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	if (vf->thumbs_filedata && vf->thumbs_loader == tl)
		{
		vf_thumb_do(vf, vf->thumbs_filedata);
		}

	while (vf_thumb_next(vf));
}

static void vf_thumb_error_cb(ThumbLoader *tl, gpointer data)
{
	vf_thumb_common_cb(tl, data);
}

static void vf_thumb_done_cb(ThumbLoader *tl, gpointer data)
{
	vf_thumb_common_cb(tl, data);
}

static gboolean vf_thumb_next(ViewFile *vf)
{
	FileData *fd = nullptr;

	if (!gtk_widget_get_realized(vf->listview))
		{
		vf_thumb_status(vf, 0.0, nullptr);
		return FALSE;
		}

	
	{

	fd = vficon_thumb_next_fd(vf);
	}

	if (!fd)
		{
		/* done */
		vf_thumb_cleanup(vf);
		return FALSE;
		}

	vf->thumbs_filedata = fd;

	if (vf->thumbs_priority &&
	    g_hash_table_size(vf->thumbs_priority) > 0 &&
	    !g_hash_table_contains(vf->thumbs_priority, fd))
		{
		vf_thumb_cleanup(vf);
		return FALSE;
		}

	if (vf->thumbs_priority)
		{
		g_hash_table_remove(vf->thumbs_priority, fd);
		}

	thumb_loader_free(vf->thumbs_loader);

	vf->thumbs_loader = thumb_loader_new(options->thumbnails.save_width, options->thumbnails.display_width);
	thumb_loader_set_callbacks(vf->thumbs_loader,
				   vf_thumb_done_cb,
				   vf_thumb_error_cb,
				   nullptr,
				   vf);

	if (!thumb_loader_start(vf->thumbs_loader, fd))
		{
		/* set icon to unknown, continue */
		DEBUG_1("thumb loader start failed %s", fd->path);
		vf_thumb_do(vf, fd);

		return TRUE;
		}

	return FALSE;
}

static void vf_thumb_reset_all(ViewFile *vf)
{
	GList *work;

	for (work = vf->list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);
		if (fd->thumb_pixbuf)
			{
			g_object_unref(fd->thumb_pixbuf);
			fd->thumb_pixbuf = nullptr;
			}
		}
}

void vf_thumb_update(ViewFile *vf)
{
	vf_thumb_stop(vf);

	g_clear_pointer(&vf->thumbs_priority, g_hash_table_destroy);
	vf->thumbs_priority = g_hash_table_new(g_direct_hash, g_direct_equal);

	vf_thumb_status(vf, 0.0, _("Loading thumbs..."));
	vf->thumbs_running = TRUE;

	if (thumb_format_changed)
		{
		vf_thumb_reset_all(vf);
		thumb_format_changed = FALSE;
		}

	while (vf_thumb_next(vf));
}

GRegex *vf_file_filter_get_filter(ViewFile *vf)
{
	GRegex *ret = nullptr;
	GError *error = nullptr;
	gchar *file_filter_text = nullptr;

	if (!gtk_widget_get_visible(vf->file_filter.combo))
		{
		return g_regex_new("", static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), nullptr);
		}

	file_filter_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(vf->file_filter.combo));

	if (file_filter_text[0] != '\0')
		{
		ret = g_regex_new(file_filter_text, vf->file_filter.case_sensitive ? static_cast<GRegexCompileFlags>(0) : G_REGEX_CASELESS, static_cast<GRegexMatchFlags>(0), &error);
		if (error)
			{
			log_printf("Error: could not compile regular expression %s\n%s\n", file_filter_text, error->message);
			g_error_free(error);
			error = nullptr;
			ret = g_regex_new("", static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), nullptr);
			}
		g_free(file_filter_text);
		}
	else
		{
		ret = g_regex_new("", static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), nullptr);
		}

	return ret;
}

guint vf_class_get_filter(ViewFile *vf)
{
	guint ret = 0;
	gint i;

	if (!gtk_widget_get_visible(vf->file_filter.combo))
		{
		return G_MAXUINT;
		}

	for ( i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		if (options->class_filter[i])
			{
			ret |= 1 << i;
			}
		}

	return ret;
}

void vf_set_layout(ViewFile *vf, LayoutWindow *layout)
{
	vf->layout = layout;
}


/*
 *-----------------------------------------------------------------------------
 * maintenance (for rename, move, remove)
 *-----------------------------------------------------------------------------
 */

static gboolean vf_refresh_idle_cb(gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);

	vf_refresh(vf);
	vf->refresh_idle_id = 0;
	return G_SOURCE_REMOVE;
}

void vf_refresh_idle_cancel(ViewFile *vf)
{
	if (vf->refresh_idle_id)
		{
		g_source_remove(vf->refresh_idle_id);
		vf->refresh_idle_id = 0;
		}
}


void vf_refresh_idle(ViewFile *vf)
{
	if (!vf->refresh_idle_id)
		{
		vf->time_refresh_set = time(nullptr);
		/* file operations run with G_PRIORITY_DEFAULT_IDLE */
		vf->refresh_idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE + 50, vf_refresh_idle_cb, vf, nullptr);
		}
	else if (time(nullptr) - vf->time_refresh_set > 1)
		{
		/* more than 1 sec since last update - increase priority */
		vf_refresh_idle_cancel(vf);
		vf->time_refresh_set = time(nullptr);
		vf->refresh_idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE - 50, vf_refresh_idle_cb, vf, nullptr);
		}
}

void vf_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto vf = static_cast<ViewFile *>(data);
	gboolean refresh;

	auto interested = static_cast<NotifyType>(NOTIFY_CHANGE | NOTIFY_REREAD);
	/** @FIXME NOTIFY_METADATA should be checked by the keyword-to-mark functions and converted to NOTIFY_MARKS only if there was a change */

	if (!(type & interested) || vf->refresh_idle_id || !vf->dir_fd) return;

	refresh = (fd == vf->dir_fd);

	if (!refresh)
		{
		gchar *base = remove_level_from_path(fd->path);
		refresh = (g_strcmp0(base, vf->dir_fd->path) == 0);
		g_free(base);
		}

	if ((type & NOTIFY_CHANGE) && fd->change)
		{
		if (!refresh && fd->change->dest)
			{
			gchar *dest_base = remove_level_from_path(fd->change->dest);
			refresh = (g_strcmp0(dest_base, vf->dir_fd->path) == 0);
			g_free(dest_base);
			}

		if (!refresh && fd->change->source)
			{
			gchar *source_base = remove_level_from_path(fd->change->source);
			refresh = (g_strcmp0(source_base, vf->dir_fd->path) == 0);
			g_free(source_base);
			}
		}

	if (refresh)
		{
		DEBUG_1("Notify vf: %s %04x", fd->path, type);
		vf_refresh_idle(vf);
		}
}
