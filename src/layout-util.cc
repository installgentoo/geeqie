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

#include "layout-util.h"

#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <gio/gio.h>
#include <glib-object.h>

#include <config.h>

#include "cache-maint.h"
#include "color-man.h"
#include "compat.h"
#include "debug.h"
#include "desktop-file.h"
#include "dupe.h"
#include "editors.h"
#include "filedata.h"
#include "fullscreen.h"
#include "history-list.h"
#include "image-overlay.h"
#include "image.h"
#include "img-view.h"
#include "intl.h"
#include "keymap-template.h"
#include "layout-image.h"
#include "layout.h"
#include "logwindow.h"
#include "main-defines.h"
#include "main.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "pixbuf-util.h"
#include "preferences.h"
#include "print.h"
#include "rcfile.h"
#include "search.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-utildlg.h"
#include "utilops.h"
#include "view-dir.h"
#include "view-file.h"
#include "window.h"

static void layout_util_sync_views(LayoutWindow *lw);

/*
 *-----------------------------------------------------------------------------
 * keyboard handler
 *-----------------------------------------------------------------------------
 */

static guint tree_key_overrides[] = {
	GDK_KEY_Page_Up,	GDK_KEY_KP_Page_Up,
	GDK_KEY_Page_Down,	GDK_KEY_KP_Page_Down,
	GDK_KEY_Home,	GDK_KEY_KP_Home,
	GDK_KEY_End,	GDK_KEY_KP_End
};

static gboolean layout_key_match(guint keyval)
{
	guint i;

	for (i = 0; i < sizeof(tree_key_overrides) / sizeof(guint); i++)
		{
		if (keyval == tree_key_overrides[i]) return TRUE;
		}

	return FALSE;
}

void keyboard_scroll_calc(gint &x, gint &y, const GdkEventKey *event)
{
	static gint delta = 0;
	static guint32 time_old = 0;
	static guint keyval_old = 0;

	if (event->state & GDK_SHIFT_MASK)
		{
		x *= 3;
		y *= 3;
		}

	if (event->state & GDK_CONTROL_MASK)
		{
		if (x < 0) x = G_MININT / 2;
		if (x > 0) x = G_MAXINT / 2;
		if (y < 0) y = G_MININT / 2;
		if (y > 0) y = G_MAXINT / 2;

		return;
		}

	if (options->progressive_key_scrolling)
		{
		guint32 time_diff;

		time_diff = event->time - time_old;

		/* key pressed within 125ms ? (1/8 second) */
		if (time_diff > 125 || event->keyval != keyval_old) delta = 0;

		time_old = event->time;
		keyval_old = event->keyval;

		delta += 2;
		}
	else
		{
		delta = 8;
		}

	x *= delta * options->keyboard_scroll_step;
	y *= delta * options->keyboard_scroll_step;
}

gboolean layout_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *focused;
	gboolean stop_signal = FALSE;
	gint x = 0;
	gint y = 0;

	if (lw->path_entry && gtk_widget_has_focus(lw->path_entry))
		{
		if (event->keyval == GDK_KEY_Escape && lw->dir_fd)
			{
			gq_gtk_entry_set_text(GTK_ENTRY(lw->path_entry), lw->dir_fd->path);
			}

		/* the gtkaccelgroup of the window is stealing presses before they get to the entry (and more),
		 * so when the some widgets have focus, give them priority (HACK)
		 */
		if (gtk_widget_event(lw->path_entry, reinterpret_cast<GdkEvent *>(event)))
			{
			return TRUE;
			}
		}

	if (lw->vf->file_filter.combo && gtk_widget_has_focus(gtk_bin_get_child(GTK_BIN(lw->vf->file_filter.combo))))
		{
		if (gtk_widget_event(gtk_bin_get_child(GTK_BIN(lw->vf->file_filter.combo)), reinterpret_cast<GdkEvent *>(event)))
			{
			return TRUE;
			}
		}

	if (lw->vd && lw->options.dir_view_type == DIRVIEW_TREE && gtk_widget_has_focus(lw->vd->view) &&
	    !layout_key_match(event->keyval) &&
	    gtk_widget_event(lw->vd->view, reinterpret_cast<GdkEvent *>(event)))
		{
		return TRUE;
		}

	focused = gtk_container_get_focus_child(GTK_CONTAINER(lw->image->widget));
	if (lw->image &&
	    ((focused && gtk_widget_has_focus(focused)) || (lw->tools && widget == lw->window) || lw->full_screen) )
		{
		stop_signal = TRUE;
		switch (event->keyval)
			{
			case GDK_KEY_Left: case GDK_KEY_KP_Left:
				x -= 1;
				break;
			case GDK_KEY_Right: case GDK_KEY_KP_Right:
				x += 1;
				break;
			case GDK_KEY_Up: case GDK_KEY_KP_Up:
				y -= 1;
				break;
			case GDK_KEY_Down: case GDK_KEY_KP_Down:
				y += 1;
				break;
			default:
				stop_signal = FALSE;
				break;
			}

		if (!stop_signal &&
		    !(event->state & GDK_CONTROL_MASK))
			{
			stop_signal = TRUE;
			switch (event->keyval)
				{
				case GDK_KEY_Menu:
					layout_image_menu_popup(lw);
					break;
				default:
					stop_signal = FALSE;
					break;
				}
			}
		}

	if (x != 0 || y!= 0)
		{
		keyboard_scroll_calc(x, y, event);
		layout_image_scroll(lw, x, y, (event->state & GDK_SHIFT_MASK));
		}

	return stop_signal;
}

void layout_keyboard_init(LayoutWindow *lw, GtkWidget *window)
{
	g_signal_connect(G_OBJECT(window), "key_press_event",
			 G_CALLBACK(layout_key_press_cb), lw);
}

/*
 *-----------------------------------------------------------------------------
 * menu callbacks
 *-----------------------------------------------------------------------------
 */


static GtkWidget *layout_window(LayoutWindow *lw)
{
	return lw->full_screen ? lw->full_screen->window : lw->window;
}

static void layout_exit_fullscreen(LayoutWindow *lw)
{
	if (!lw->full_screen) return;
	layout_image_full_screen_stop(lw);
}

static void layout_menu_search_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	search_new(lw->dir_fd, layout_image_get_fd(lw));
}

static void layout_menu_dupes_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	dupe_window_new();
}

static void layout_menu_print_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	print_window_new(layout_image_get_fd(lw), layout_selection_list(lw), layout_list(lw), layout_window(lw));
}

static void layout_menu_dir_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->vd) vd_new_folder(lw->vd, lw->dir_fd);
}

static void layout_menu_copy_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_copy(nullptr, layout_selection_list(lw), nullptr, layout_window(lw));
}

static void layout_menu_copy_path_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_path_list_to_clipboard(layout_selection_list(lw), TRUE, ClipboardAction::COPY);
}

static void layout_menu_copy_path_unquoted_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_path_list_to_clipboard(layout_selection_list(lw), FALSE, ClipboardAction::COPY);
}

static void layout_menu_copy_image_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	ImageWindow *imd = lw->image;

	GdkPixbuf *pixbuf;
	pixbuf = image_get_pixbuf(imd);
	if (!pixbuf) return;
	gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), pixbuf);
}

static void layout_menu_cut_path_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_path_list_to_clipboard(layout_selection_list(lw), FALSE, ClipboardAction::CUT);
}

static void layout_menu_move_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_move(nullptr, layout_selection_list(lw), nullptr, layout_window(lw));
}

static void layout_menu_rename_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_rename(nullptr, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_delete_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	options->file_ops.safe_delete_enable = FALSE;
	file_util_delete(nullptr, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_move_to_trash_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	options->file_ops.safe_delete_enable = TRUE;
	file_util_delete(nullptr, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_move_to_trash_key_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (options->file_ops.enable_delete_key)
		{
		options->file_ops.safe_delete_enable = TRUE;
		file_util_delete(nullptr, layout_selection_list(lw), layout_window(lw));
		}
}

static void layout_menu_exit_cb(GtkAction *, gpointer)
{
	exit_program();
}

static void layout_menu_alter_desaturate_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_set_desaturate(lw, gq_gtk_toggle_action_get_active(action));
}

static void layout_menu_alter_ignore_alpha_cb(GtkToggleAction *action, gpointer data)
{
   auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.ignore_alpha == gq_gtk_toggle_action_get_active(action)) return;

   layout_image_set_ignore_alpha(lw, gq_gtk_toggle_action_get_active(action));
}

static void layout_menu_exif_rotate_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	options->image.exif_rotate_enable = gq_gtk_toggle_action_get_active(action);
	layout_image_reset_orientation(lw);
}

static void layout_menu_select_overunderexposed_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_set_overunderexposed(lw, gq_gtk_toggle_action_get_active(action));
}

static void layout_menu_config_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	show_config_window(lw);
}

static void layout_menu_editors_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	show_editor_list_window();
}

static void layout_menu_layout_config_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_show_config_window(lw);
}

static void layout_menu_remove_thumb_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	cache_manager_show();
}

static void layout_menu_wallpaper_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_to_root(lw);
}

/* single window zoom */
static void layout_menu_zoom_in_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_adjust(lw, get_zoom_increment());
}

static void layout_menu_zoom_out_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_adjust(lw, -get_zoom_increment());
}

static void layout_menu_zoom_1_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 1.0);
}

static void layout_menu_zoom_fit_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 0.0);
}

static void layout_menu_zoom_fit_hor_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set_fill_geometry(lw, FALSE);
}

static void layout_menu_zoom_fit_vert_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set_fill_geometry(lw, TRUE);
}

static void layout_menu_zoom_2_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 2.0);
}

static void layout_menu_zoom_3_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 3.0);
}
static void layout_menu_zoom_4_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 4.0);
}

static void layout_menu_zoom_1_2_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, -2.0);
}

static void layout_menu_zoom_1_3_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, -3.0);
}

static void layout_menu_zoom_1_4_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, -4.0);
}

static void layout_menu_view_dir_as_cb(GtkToggleAction *action,  gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);

	if (gq_gtk_toggle_action_get_active(action))
		{
		layout_views_set(lw, DIRVIEW_TREE);
		}
	else
		{
		layout_views_set(lw, DIRVIEW_LIST);
		}
}

struct OpenWithData
{
	GAppInfo *application;
	GList *g_file_list;
	GtkWidget *app_chooser_dialog;
};

static void open_with_response_cb(GtkDialog *, gint response_id, gpointer data)
{
	GError *error = nullptr;
	auto open_with_data = static_cast<OpenWithData *>(data);

	if (response_id == GTK_RESPONSE_OK)
		{
		g_app_info_launch(open_with_data->application, open_with_data->g_file_list, nullptr, &error);

		if (error)
			{
			log_printf("Error launching app: %s\n", error->message);
			g_error_free(error);
			}
		}

	g_object_unref(open_with_data->application);
	g_object_unref(g_list_first(open_with_data->g_file_list)->data);
	g_list_free(open_with_data->g_file_list);
	gq_gtk_widget_destroy(GTK_WIDGET(open_with_data->app_chooser_dialog));
	g_free(open_with_data);
}

static void open_with_application_selected_cb(GtkAppChooserWidget *, GAppInfo *application, gpointer data)
{
	auto open_with_data = static_cast<OpenWithData *>(data);

	g_object_unref(open_with_data->application);

	open_with_data->application = g_app_info_dup(application);
}

static void open_with_application_activated_cb(GtkAppChooserWidget *, GAppInfo *application, gpointer data)
{
	GError *error = nullptr;
	auto open_with_data = static_cast<OpenWithData *>(data);

	g_app_info_launch(application, open_with_data->g_file_list, nullptr, &error);

	if (error)
		{
		log_printf("Error launching app.: %s\n", error->message);
		g_error_free(error);
		}

	g_object_unref(open_with_data->application);
	g_object_unref(g_list_first(open_with_data->g_file_list)->data);
	g_list_free(open_with_data->g_file_list);
	gq_gtk_widget_destroy(GTK_WIDGET(open_with_data->app_chooser_dialog));
	g_free(open_with_data);
}

static void layout_menu_open_with_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd;
	GtkWidget *widget;
	OpenWithData *open_with_data;

	if (layout_selection_list(lw))
		{
		open_with_data = g_new(OpenWithData, 1);

		fd = static_cast<FileData *>(g_list_first(layout_selection_list(lw))->data);

		open_with_data->g_file_list = g_list_append(nullptr, g_file_new_for_path(fd->path));

		open_with_data->app_chooser_dialog = gtk_app_chooser_dialog_new(nullptr, GTK_DIALOG_DESTROY_WITH_PARENT, G_FILE(g_list_first(open_with_data->g_file_list)->data));

		widget = gtk_app_chooser_dialog_get_widget(GTK_APP_CHOOSER_DIALOG(open_with_data->app_chooser_dialog));

		open_with_data->application = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(open_with_data->app_chooser_dialog));

		g_signal_connect(G_OBJECT(widget), "application-selected", G_CALLBACK(open_with_application_selected_cb), open_with_data);
		g_signal_connect(G_OBJECT(widget), "application-activated", G_CALLBACK(open_with_application_activated_cb), open_with_data);
		g_signal_connect(G_OBJECT(open_with_data->app_chooser_dialog), "response", G_CALLBACK(open_with_response_cb), open_with_data);
		g_signal_connect(G_OBJECT(open_with_data->app_chooser_dialog), "close", G_CALLBACK(open_with_response_cb), open_with_data);

		gtk_widget_show(open_with_data->app_chooser_dialog);
		}
}

static void layout_menu_fullscreen_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_full_screen_toggle(lw);
}

static void layout_menu_escape_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
}

static void layout_menu_overlay_toggle_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	image_osd_toggle(lw->image);
	layout_util_sync_views(lw);
}


static void layout_menu_overlay_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (gq_gtk_toggle_action_get_active(action))
		{
		OsdShowFlags flags = image_osd_get(lw->image);

		if ((flags | OSD_SHOW_INFO | OSD_SHOW_STATUS) != flags)
			image_osd_set(lw->image, static_cast<OsdShowFlags>(flags | OSD_SHOW_INFO | OSD_SHOW_STATUS));
		}
	else
		{
		image_osd_set(lw->image, OSD_SHOW_NOTHING);
		}
}

static void layout_menu_animate_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.animate == gq_gtk_toggle_action_get_active(action)) return;
	layout_image_animate_toggle(lw);
}

static void layout_menu_rectangular_selection_cb(GtkToggleAction *action, gpointer)
{
	options->collections.rectangular_selection = gq_gtk_toggle_action_get_active(action);
}

static void layout_menu_refresh_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_refresh(lw);
}

static void layout_menu_float_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.tools_float == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_tools_float_toggle(lw);
}

static void layout_menu_hide_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_tools_hide_toggle(lw);
}

static void layout_menu_selectable_toolbars_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.selectable_toolbars_hidden == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_selectable_toolbars_toggle(lw);
}

static void layout_menu_info_pixel_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.show_info_pixel == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_info_pixel_set(lw, !lw->options.show_info_pixel);
}

static void layout_menu_about_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	show_about_window(lw);
}

static void layout_menu_log_window_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	log_window_new(lw);
}


/*
 *-----------------------------------------------------------------------------
 * select menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_select_all_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_select_all(lw);
}

static void layout_menu_unselect_all_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_select_none(lw);
}

static void layout_menu_invert_selection_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_select_invert(lw);
}

static void layout_menu_file_filter_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_file_filter_set(lw, gq_gtk_toggle_action_get_active(action));
}

/*
 *-----------------------------------------------------------------------------
 * go menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_image_first_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_image_first(lw);
}

static void layout_menu_image_prev_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

		{
		layout_image_prev(lw);
		}
}

static void layout_menu_image_next_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

		{
		layout_image_next(lw);
		}
}

static void layout_menu_page_first_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_set_page_num(fd, 1);
		}
}

static void layout_menu_page_last_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_set_page_num(fd, -1);
		}
}

static void layout_menu_page_next_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_inc_page_num(fd);
		}
}

static void layout_menu_page_previous_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_dec_page_num(fd);
		}
}

static void layout_menu_image_forward_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	/* Obtain next image */
	layout_set_path(lw, image_chain_forward());
}

static void layout_menu_image_back_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	/* Obtain previous image */
	layout_set_path(lw, image_chain_back());
}

static void layout_menu_image_last_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_image_last(lw);
}

static void layout_menu_back_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *dir_fd;

	/* Obtain previous path */
	dir_fd = file_data_new_dir(history_chain_back());
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_forward_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *dir_fd;

	/* Obtain next path */
	dir_fd = file_data_new_dir(history_chain_forward());
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_home_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	const gchar *path;

	if (lw->options.home_path && *lw->options.home_path)
		path = lw->options.home_path;
	else
		path = homedir();

	if (path)
		{
		FileData *dir_fd = file_data_new_dir(path);
		layout_set_fd(lw, dir_fd);
		file_data_unref(dir_fd);
		}
}

static void layout_menu_up_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	ViewDir *vd = lw->vd;
	gchar *path;

	if (!vd->dir_fd || strcmp(vd->dir_fd->path, G_DIR_SEPARATOR_S) == 0) return;
	path = remove_level_from_path(vd->dir_fd->path);

	if (vd->select_func)
		{
		FileData *fd = file_data_new_dir(path);
		vd->select_func(vd, fd, vd->select_data);
		file_data_unref(fd);
		}

	g_free(path);
}


/*
 *-----------------------------------------------------------------------------
 * edit menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_edit_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	const gchar *key = gq_gtk_action_get_name(action);

	if (!editor_window_flag_set(key))
		layout_exit_fullscreen(lw);

	file_util_start_editor_from_filelist(key, layout_selection_list(lw), layout_get_path(lw), lw->window);
}


/*
 *-----------------------------------------------------------------------------
 * color profile button (and menu)
 *-----------------------------------------------------------------------------
 */
#if HAVE_LCMS
static void layout_color_menu_enable_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (layout_image_color_profile_get_use(lw) == gq_gtk_toggle_action_get_active(action)) return;

	layout_image_color_profile_set_use(lw, gq_gtk_toggle_action_get_active(action));
	layout_util_sync_color(lw);
	layout_image_refresh(lw);
}

static void layout_color_menu_use_image_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint input;
	gboolean use_image;

	if (!layout_image_color_profile_get(lw, input, use_image)) return;
	if (use_image == gq_gtk_toggle_action_get_active(action)) return;
	layout_image_color_profile_set(lw, input, gq_gtk_toggle_action_get_active(action));
	layout_util_sync_color(lw);
	layout_image_refresh(lw);
}

static void layout_color_menu_input_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint type;
	gint input;
	gboolean use_image;

	type = gq_gtk_radio_action_get_current_value(action);
	if (type < 0 || type >= COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS) return;

	if (!layout_image_color_profile_get(lw, input, use_image)) return;
	if (type == input) return;

	layout_image_color_profile_set(lw, type, use_image);
	layout_image_refresh(lw);
}
#else
static void layout_color_menu_enable_cb()
{
}

static void layout_color_menu_use_image_cb()
{
}

static void layout_color_menu_input_cb()
{
}
#endif

void layout_recent_add_path(const gchar *path)
{
	if (!path) return;

	history_list_add_to_key("recent", path, options->open_recent_list_maxsize);
}

/*
 *-----------------------------------------------------------------------------
 * menu
 *-----------------------------------------------------------------------------
 */

#define CB G_CALLBACK
/**
 * tooltip is used as the description field in the Help manual shortcuts documentation
 *
 * struct GtkActionEntry:
 *  name, stock_id, label, accelerator, tooltip, callback
 */
static GtkActionEntry menu_entries[] = {
  { "About",                 GQ_ICON_ABOUT,                     N_("_About"),                                           nullptr,               N_("About"),                                           CB(layout_menu_about_cb) },
  { "Back",                  GQ_ICON_GO_PREV,                   N_("_Back"),                                            nullptr,               N_("Back in folder history"),                          CB(layout_menu_back_cb) },
  { "ColorMenu",             nullptr,                           N_("_Color Management"),                                nullptr,               nullptr,                                               nullptr },
  { "Copy",                  GQ_ICON_COPY,                      N_("_Copy..."),                                         "<control>C",          N_("Copy..."),                                         CB(layout_menu_copy_cb) },
  { "CopyImage",             nullptr,                           N_("_Copy image to clipboard"),                         nullptr,               N_("Copy image to clipboard"),                         CB(layout_menu_copy_image_cb) },
  { "CopyPath",              nullptr,                           N_("_Copy to clipboard"),                               nullptr,               N_("Copy to clipboard"),                               CB(layout_menu_copy_path_cb) },
  { "CopyPathUnquoted",      nullptr,                           N_("_Copy to clipboard (unquoted)"),                    nullptr,               N_("Copy to clipboard (unquoted)"),                    CB(layout_menu_copy_path_unquoted_cb) },
  { "CutPath",               nullptr,                           N_("_Cut to clipboard"),                                "<control>X",          N_("Cut to clipboard"),                                CB(layout_menu_cut_path_cb) },
  { "DeleteAlt1",            GQ_ICON_USER_TRASH,                N_("Move selection to Trash..."),                       "Delete",              N_("Move selection to Trash..."),                      CB(layout_menu_move_to_trash_key_cb) },
  { "DeleteAlt2",            GQ_ICON_USER_TRASH,                N_("Move selection to Trash..."),                       "KP_Delete",           N_("Move selection to Trash..."),                      CB(layout_menu_move_to_trash_key_cb) },
  { "Delete",                GQ_ICON_USER_TRASH,                N_("Move selection to Trash..."),                       "<control>D",          N_("Move selection to Trash..."),                      CB(layout_menu_move_to_trash_cb) },
  { "EditMenu",              nullptr,                           N_("_Edit"),                                            nullptr,               nullptr,                                               nullptr },
  { "EscapeAlt1",            GQ_ICON_LEAVE_FULLSCREEN,          N_("_Leave full screen"),                               "Q",                   N_("Leave full screen"),                               CB(layout_menu_escape_cb) },                           
  { "Escape",                GQ_ICON_LEAVE_FULLSCREEN,          N_("_Leave full screen"),                              "Escape",               N_("Leave full screen"),                               CB(layout_menu_escape_cb) },                           
  { "FileDirMenu",           nullptr,                           N_("_Files and Folders"),                               nullptr,               nullptr,                                               nullptr },
  { "FileMenu",              nullptr,                           N_("_File"),                                            nullptr,               nullptr,                                               nullptr },
  { "FindDupes",             GQ_ICON_FIND,                      N_("_Find duplicates..."),                              "D",                   N_("Find duplicates..."),                              CB(layout_menu_dupes_cb) },
  { "FirstImage",            GQ_ICON_GO_TOP,                    N_("_First Image"),                                     "Home",                N_("First Image"),                                     CB(layout_menu_image_first_cb) },
  { "FirstPage",             GQ_ICON_PREV_PAGE,                 N_("_First Page"),                                      "<control>Home",       N_( "First Page of multi-page image"),                 CB(layout_menu_page_first_cb) },
  { "Forward",               GQ_ICON_GO_NEXT,                   N_("_Forward"),                                         nullptr,               N_("Forward in folder history"),                       CB(layout_menu_forward_cb) },
  { "FullScreenAlt1",        GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "V",                   N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "FullScreenAlt2",        GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "F11",                 N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "FullScreen",            GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "F",                   N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "HelpMenu",              nullptr,                           N_("_Help"),                                            nullptr,               nullptr,                                               nullptr },
  { "HideTools",             PIXBUF_INLINE_ICON_HIDETOOLS,      N_("_Hide file list"),                                  "<control>H",          N_("Hide file list"),                                  CB(layout_menu_hide_cb) },
  { "Home",                  GQ_ICON_HOME,                      N_("_Home"),                                            nullptr,               N_("Home"),                                            CB(layout_menu_home_cb) },
  { "ImageBack",             GQ_ICON_GO_FIRST,                  N_("Image Back"),                                       nullptr,               N_("Back in image history"),                           CB(layout_menu_image_back_cb) },
  { "ImageForward",          GQ_ICON_GO_LAST,                   N_("Image Forward"),                                    nullptr,               N_("Forward in image history"),                        CB(layout_menu_image_forward_cb) },
  { "ImageOverlayCycle",     nullptr,                           N_("_Cycle through overlay modes"),                     "I",                   N_("Cycle through Overlay modes"),                     CB(layout_menu_overlay_toggle_cb) },                   
  { "LastImage",             GQ_ICON_GO_BOTTOM,                 N_("_Last Image"),                                      "End",                 N_("Last Image"),                                      CB(layout_menu_image_last_cb) },
  { "LastPage",              GQ_ICON_NEXT_PAGE,                 N_("_Last Page"),                                       "<control>End",        N_("Last Page of multi-page image"),                   CB(layout_menu_page_last_cb) },
  { "LayoutConfig",          GQ_ICON_PREFERENCES,               N_("_Configure this window..."),                        nullptr,               N_("Configure this window..."),                        CB(layout_menu_layout_config_cb) },
  { "LogWindow",             nullptr,                           N_("_Log Window"),                                      nullptr,               N_("Log Window"),                                      CB(layout_menu_log_window_cb) },
  { "Maintenance",           PIXBUF_INLINE_ICON_MAINTENANCE,    N_("_Cache maintenance..."),                            nullptr,               N_("Cache maintenance..."),                            CB(layout_menu_remove_thumb_cb) },
  { "Move",                  PIXBUF_INLINE_ICON_MOVE,           N_("_Move..."),                                         "<control>M",          N_("Move..."),                                         CB(layout_menu_move_cb) },
  { "NewFolder",             GQ_ICON_DIRECTORY,                 N_("N_ew folder..."),                                   "<control>F",          N_("New folder..."),                                   CB(layout_menu_dir_cb) },
  { "NextImageAlt1",         GQ_ICON_GO_DOWN,                   N_("_Next Image"),                                      "Page_Down",           N_("Next Image"),                                      CB(layout_menu_image_next_cb) },
  { "NextImageAlt2",         GQ_ICON_GO_DOWN,                   N_("_Next Image"),                                      "KP_Page_Down",        N_("Next Image"),                                      CB(layout_menu_image_next_cb) },
  { "NextImage",             GQ_ICON_GO_DOWN,                   N_("_Next Image"),                                      "space",               N_("Next Image"),                                      CB(layout_menu_image_next_cb) },
  { "NextPage",              GQ_ICON_FORWARD_PAGE,              N_("_Next Page"),                                       "<control>Page_Down",  N_("Next Page of multi-page image"),                   CB(layout_menu_page_next_cb) },
  { "OpenWith",              GQ_ICON_OPEN_WITH,                 N_("Open With..."),                                     nullptr,               N_("Open With..."),                                    CB(layout_menu_open_with_cb) },
  { "OverlayMenu",           nullptr,                           N_("Image _Overlay"),                                   nullptr,               nullptr,                                               nullptr },
  { "PermanentDelete",       GQ_ICON_DELETE,                    N_("Delete selection..."),                              "<shift>Delete",       N_("Delete selection..."),                             CB(layout_menu_delete_cb) },
  { "Plugins",               GQ_ICON_PREFERENCES,               N_("Configure _Plugins..."),                            nullptr,               N_("Configure Plugins..."),                            CB(layout_menu_editors_cb) },
  { "Preferences",           GQ_ICON_PREFERENCES,               N_("P_references..."),                                  "<control>O",          N_("Preferences..."),                                  CB(layout_menu_config_cb) },
  { "PreferencesMenu",       nullptr,                           N_("P_references"),                                     nullptr,               nullptr,                                               nullptr },
  { "PrevImageAlt1",         GQ_ICON_GO_UP,                     N_("_Previous Image"),                                  "Page_Up",             N_("Previous Image"),                                  CB(layout_menu_image_prev_cb) },
  { "PrevImageAlt2",         GQ_ICON_GO_UP,                     N_("_Previous Image"),                                  "KP_Page_Up",          N_("Previous Image"),                                  CB(layout_menu_image_prev_cb) },
  { "PrevImage",             GQ_ICON_GO_UP,                     N_("_Previous Image"),                                  "BackSpace",           N_("Previous Image"),                                  CB(layout_menu_image_prev_cb) },
  { "PrevPage",              GQ_ICON_BACK_PAGE,                 N_("_Previous Page"),                                   "<control>Page_Up",    N_("Previous Page of multi-page image"),               CB(layout_menu_page_previous_cb) },
  { "Print",                 GQ_ICON_PRINT,                     N_("_Print..."),                                        "<shift>P",            N_("Print..."),                                        CB(layout_menu_print_cb) },
  { "Quit",                  GQ_ICON_QUIT,                      N_("_Quit"),                                            "<control>Q",          N_("Quit"),                                            CB(layout_menu_exit_cb) },
  { "Refresh",               GQ_ICON_REFRESH,                   N_("_Refresh"),                                         "R",                   N_("Refresh"),                                         CB(layout_menu_refresh_cb) },
  { "Rename",                PIXBUF_INLINE_ICON_RENAME,         N_("_Rename..."),                                       "<control>R",          N_("Rename..."),                                       CB(layout_menu_rename_cb) },
  { "Search",                GQ_ICON_FIND,                      N_("_Search..."),                                       "F3",                  N_("Search..."),                                       CB(layout_menu_search_cb) },
  { "SelectAll",             PIXBUF_INLINE_ICON_SELECT_ALL,     N_("Select _all"),                                      "<control>A",          N_("Select all"),                                      CB(layout_menu_select_all_cb) },
  { "SelectInvert",          PIXBUF_INLINE_ICON_SELECT_INVERT,  N_("_Invert Selection"),                                "<control><shift>I",   N_("Invert Selection"),                                CB(layout_menu_invert_selection_cb) },
  { "SelectMenu",            nullptr,                           N_("_Select"),                                          nullptr,               nullptr,                                               nullptr },
  { "SelectNone",            PIXBUF_INLINE_ICON_SELECT_NONE,    N_("Select _none"),                                     "<control><shift>A",   N_("Select none"),                                     CB(layout_menu_unselect_all_cb) },
  { "Up",                    GQ_ICON_GO_UP,                     N_("_Up"),                                              nullptr,               N_("Up one folder"),                                   CB(layout_menu_up_cb) },
  { "ViewMenu",              nullptr,                           N_("_View"),                                            nullptr,               nullptr,                                               nullptr },
  { "Wallpaper",             nullptr,                           N_("Set as _wallpaper"),                                nullptr,               N_("Set as wallpaper"),                                CB(layout_menu_wallpaper_cb) },
  { "Zoom100Alt1",           GQ_ICON_ZOOM_100,                  N_("Zoom _1:1"),                                        "KP_Divide",           N_("Zoom 1:1"),                                        CB(layout_menu_zoom_1_1_cb) },
  { "Zoom100",               GQ_ICON_ZOOM_100,                  N_("Zoom _1:1"),                                        "Z",                   N_("Zoom 1:1"),                                        CB(layout_menu_zoom_1_1_cb) },
  { "Zoom200",               GQ_ICON_GENERIC,                   N_("Zoom _2:1"),                                        nullptr,               N_("Zoom 2:1"),                                        CB(layout_menu_zoom_2_1_cb) },
  { "Zoom25",                GQ_ICON_GENERIC,                   N_("Zoom 1:4"),                                         nullptr,               N_("Zoom 1:4"),                                        CB(layout_menu_zoom_1_4_cb) },
  { "Zoom300",               GQ_ICON_GENERIC,                   N_("Zoom _3:1"),                                        nullptr,               N_("Zoom 3:1"),                                        CB(layout_menu_zoom_3_1_cb) },
  { "Zoom33",                GQ_ICON_GENERIC,                   N_("Zoom 1:3"),                                         nullptr,               N_("Zoom 1:3"),                                        CB(layout_menu_zoom_1_3_cb) },
  { "Zoom400",               GQ_ICON_GENERIC,                   N_("Zoom _4:1"),                                        nullptr,               N_("Zoom 4:1"),                                        CB(layout_menu_zoom_4_1_cb) },
  { "Zoom50",                GQ_ICON_GENERIC,                   N_("Zoom 1:2"),                                         nullptr,               N_("Zoom 1:2"),                                        CB(layout_menu_zoom_1_2_cb) },
  { "ZoomFillHor",           PIXBUF_INLINE_ICON_ZOOMFILLHOR,    N_("Fit _Horizontally"),                                "H",                   N_("Fit Horizontally"),                                CB(layout_menu_zoom_fit_hor_cb) },
  { "ZoomFillVert",          PIXBUF_INLINE_ICON_ZOOMFILLVERT,   N_("Fit _Vertically"),                                  "W",                   N_("Fit Vertically"),                                  CB(layout_menu_zoom_fit_vert_cb) },
  { "ZoomFitAlt1",           GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "KP_Multiply",         N_("Zoom to fit"),                                     CB(layout_menu_zoom_fit_cb) },
  { "ZoomFit",               GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "X",                   N_("Zoom to fit"),                                     CB(layout_menu_zoom_fit_cb) },
  { "ZoomInAlt1",            GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "KP_Add",              N_("Zoom in"),                                         CB(layout_menu_zoom_in_cb) },
  { "ZoomIn",                GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "equal",               N_("Zoom in"),                                         CB(layout_menu_zoom_in_cb) },
  { "ZoomMenu",              nullptr,                           N_("_Zoom"),                                            nullptr,               nullptr,                                               nullptr },
  { "ZoomOutAlt1",           GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "KP_Subtract",         N_("Zoom out"),                                        CB(layout_menu_zoom_out_cb) },
  { "ZoomOut",               GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "minus",               N_("Zoom out"),                                        CB(layout_menu_zoom_out_cb) }
};

static GtkToggleActionEntry menu_toggle_entries[] = {
  { "Animate",                 nullptr,                              N_("_Animation"),               "A",               N_("Toggle animation"),              CB(layout_menu_animate_cb),                  FALSE  },
  { "ExifRotate",              GQ_ICON_ROTATE_LEFT,                  N_("_Exif rotate"),             "<alt>X",          N_("Toggle Exif rotate"),            CB(layout_menu_exif_rotate_cb),              FALSE  },
  { "FloatTools",              PIXBUF_INLINE_ICON_FLOAT,             N_("_Float file list"),         "L",               N_("Float file list"),               CB(layout_menu_float_cb),                    FALSE  },
  { "Grayscale",               PIXBUF_INLINE_ICON_GRAYSCALE,         N_("Toggle _grayscale"),        "<shift>G",        N_("Toggle grayscale"),              CB(layout_menu_alter_desaturate_cb),         FALSE  },
  { "HideSelectableToolbars",  nullptr,                              N_("Hide Selectable Bars"),     "<control>grave",  N_("Hide Selectable Bars"),          CB(layout_menu_selectable_toolbars_cb),      FALSE  },
  { "IgnoreAlpha",             GQ_ICON_STRIKETHROUGH,                N_("Hide _alpha"),              "<shift>A",        N_("Hide alpha channel"),            CB(layout_menu_alter_ignore_alpha_cb),       FALSE  },
  { "ImageOverlay",            nullptr,                              N_("Image _Overlay"),           nullptr,           N_("Image Overlay"),                 CB(layout_menu_overlay_cb),                  FALSE  },
  { "OverUnderExposed",        PIXBUF_INLINE_ICON_EXPOSURE,          N_("Over/Under Exposed"),       "<shift>E",        N_("Highlight over/under exposed"),  CB(layout_menu_select_overunderexposed_cb),  FALSE  },
  { "RectangularSelection",    PIXBUF_INLINE_ICON_SELECT_RECTANGLE,  N_("Rectangular Selection"),    "<alt>R",          N_("Rectangular Selection"),         CB(layout_menu_rectangular_selection_cb),    FALSE  },
  { "ShowFileFilter",          GQ_ICON_FILE_FILTER,                  N_("Show File Filter"),         nullptr,           N_("Show File Filter"),              CB(layout_menu_file_filter_cb),              FALSE  },
  { "ShowInfoPixel",           GQ_ICON_SELECT_COLOR,                 N_("Pi_xel Info"),              nullptr,           N_("Show Pixel Info"),               CB(layout_menu_info_pixel_cb),               FALSE  },
  { "UseColorProfiles",        GQ_ICON_COLOR_MANAGEMENT,             N_("Use _color profiles"),      nullptr,           N_("Use color profiles"),            CB(layout_color_menu_enable_cb),             FALSE  },
  { "UseImageProfile",         nullptr,                              N_("Use profile from _image"),  nullptr,           N_("Use profile from image"),        CB(layout_color_menu_use_image_cb),          FALSE  }
};

static GtkToggleActionEntry menu_view_dir_toggle_entries[] = {
  { "FolderTree",  nullptr,  N_("T_oggle Folder View"),  "<control>T",  N_("Toggle Folders View"),  CB(layout_menu_view_dir_as_cb),FALSE },
};

static GtkRadioActionEntry menu_color_radio_entries[] = {
  { "ColorProfile0",  nullptr,  N_("Input _0: sRGB"),                 nullptr,  N_("Input 0: sRGB"),                 COLOR_PROFILE_SRGB },
  { "ColorProfile1",  nullptr,  N_("Input _1: AdobeRGB compatible"),  nullptr,  N_("Input 1: AdobeRGB compatible"),  COLOR_PROFILE_ADOBERGB },
  { "ColorProfile2",  nullptr,  N_("Input _2"),                       nullptr,  N_("Input 2"),                       COLOR_PROFILE_FILE },
  { "ColorProfile3",  nullptr,  N_("Input _3"),                       nullptr,  N_("Input 3"),                       COLOR_PROFILE_FILE + 1 },
  { "ColorProfile4",  nullptr,  N_("Input _4"),                       nullptr,  N_("Input 4"),                       COLOR_PROFILE_FILE + 2 },
  { "ColorProfile5",  nullptr,  N_("Input _5"),                       nullptr,  N_("Input 5"),                       COLOR_PROFILE_FILE + 3 }
};

#undef CB

static gchar *menu_translate(const gchar *path, gpointer)
{
	return static_cast<gchar *>(_(path));
}

static GList *layout_actions_editor_menu_path(EditorDescription *editor)
{
	gchar **split = g_strsplit(editor->menu_path, "/", 0);
	gint i = 0;
	GList *ret = nullptr;

	if (split[0] == nullptr)
		{
		g_strfreev(split);
		return nullptr;
		}

	while (split[i])
		{
		ret = g_list_prepend(ret, g_strdup(split[i]));
		i++;
		}

	g_strfreev(split);

	ret = g_list_prepend(ret, g_strdup(editor->key));

	return g_list_reverse(ret);
}

static void layout_actions_editor_add(GString *desc, GList *path, GList *old_path)
{
	gint to_open;
	gint to_close;
	gint i;
	while (path && old_path && strcmp(static_cast<gchar *>(path->data), static_cast<gchar *>(old_path->data)) == 0)
		{
		path = path->next;
		old_path = old_path->next;
		}
	to_open = g_list_length(path) - 1;
	to_close = g_list_length(old_path) - 1;

	if (to_close > 0)
		{
		old_path = g_list_last(old_path);
		old_path = old_path->prev;
		}

	for (i =  0; i < to_close; i++)
		{
		auto name = static_cast<gchar *>(old_path->data);
		if (g_str_has_suffix(name, "Section"))
			{
			g_string_append(desc,	"      </placeholder>");
			}
		else if (g_str_has_suffix(name, "Menu"))
			{
			g_string_append(desc,	"    </menu>");
			}
		else
			{
			g_warning("invalid menu path item %s", name);
			}
		old_path = old_path->prev;
		}

	for (i =  0; i < to_open; i++)
		{
		auto name = static_cast<gchar *>(path->data);
		if (g_str_has_suffix(name, "Section"))
			{
			g_string_append_printf(desc,	"      <placeholder name='%s'>", name);
			}
		else if (g_str_has_suffix(name, "Menu"))
			{
			g_string_append_printf(desc,	"    <menu action='%s'>", name);
			}
		else
			{
			g_warning("invalid menu path item %s", name);
			}
		path = path->next;
		}

	if (path)
		g_string_append_printf(desc, "      <menuitem action='%s'/>", static_cast<gchar *>(path->data));
}

static void layout_actions_setup_editors(LayoutWindow *lw)
{
	GError *error;
	GList *editors_list;
	GList *work;
	GList *old_path;
	GString *desc;

	if (lw->ui_editors_id)
		{
		gq_gtk_ui_manager_remove_ui(lw->ui_manager, lw->ui_editors_id);
		}

	if (lw->action_group_editors)
		{
		gq_gtk_ui_manager_remove_action_group(lw->ui_manager, lw->action_group_editors);
		g_object_unref(lw->action_group_editors);
		}
	lw->action_group_editors = gq_gtk_action_group_new("MenuActionsExternal");
	gq_gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group_editors, 1);

	/* lw->action_group_editors contains translated entries, no translate func is required */
	desc = g_string_new(
				"<ui>"
				"  <menubar name='MainMenu'>");

	editors_list = editor_list_get();

	old_path = nullptr;
	work = editors_list;
	while (work)
		{
		GList *path;
		auto editor = static_cast<EditorDescription *>(work->data);
		GtkActionEntry entry = { editor->key,
		                         nullptr,
		                         editor->name,
		                         editor->hotkey,
		                         editor->comment ? editor->comment : editor->name,
		                         G_CALLBACK(layout_menu_edit_cb) };

		if (editor->icon)
			{
			entry.stock_id = editor->key;
			}
		gq_gtk_action_group_add_actions(lw->action_group_editors, &entry, 1, lw);

		GList *button_list;
		GList *work_button_list;

		button_list = gtk_container_get_children(GTK_CONTAINER(lw->toolbar[TOOLBAR_MAIN]));
		work_button_list = button_list;

		while (work_button_list)
			{
			if (g_strcmp0(gtk_widget_get_tooltip_text(GTK_WIDGET(work_button_list->data)), editor->key) == 0)
				{
				GtkWidget *image = nullptr;
				if (editor->icon)
					{
					image = gtk_image_new_from_stock(editor->key, GTK_ICON_SIZE_BUTTON);
					}
				else
					{
					image = gtk_image_new_from_icon_name(GQ_ICON_MISSING_IMAGE, GTK_ICON_SIZE_BUTTON);
					}
				gtk_button_set_image(GTK_BUTTON(work_button_list->data), GTK_WIDGET(image));
				gtk_widget_set_tooltip_text(GTK_WIDGET(work_button_list->data), editor->name);
				}
			work_button_list = work_button_list->next;
			}

		g_list_free(button_list);

		path = layout_actions_editor_menu_path(editor);
		layout_actions_editor_add(desc, path, old_path);

		g_list_free_full(old_path, g_free);
		old_path = path;
		work = work->next;
		}

	layout_actions_editor_add(desc, nullptr, old_path);
	g_list_free_full(old_path, g_free);

	g_string_append(desc,"  </menubar>"
				"</ui>" );

	error = nullptr;

	lw->ui_editors_id = gq_gtk_ui_manager_add_ui_from_string(lw->ui_manager, desc->str, -1, &error);
	if (!lw->ui_editors_id)
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}
	g_string_free(desc, TRUE);
	g_list_free(editors_list);
}

void create_toolbars(LayoutWindow *lw)
{
	gint i;

	for (i = 0; i < TOOLBAR_COUNT; i++)
		{
		layout_actions_toolbar(lw, static_cast<ToolbarType>(i));
		layout_toolbar_clear(lw, static_cast<ToolbarType>(i));
		layout_toolbar_add_default(lw, static_cast<ToolbarType>(i));
		}
}

void layout_actions_setup(LayoutWindow *lw)
{
	GError *error;

	DEBUG_1("%s layout_actions_setup: start", get_exec_time());
	if (lw->ui_manager) return;

	lw->action_group = gq_gtk_action_group_new("MenuActions");
	gq_gtk_action_group_set_translate_func(lw->action_group, menu_translate, nullptr, nullptr);

	gq_gtk_action_group_add_actions(lw->action_group,
				     menu_entries, G_N_ELEMENTS(menu_entries), lw);
	gq_gtk_action_group_add_toggle_actions(lw->action_group,
					    menu_toggle_entries, G_N_ELEMENTS(menu_toggle_entries), lw);
	gq_gtk_action_group_add_toggle_actions(lw->action_group,
					   menu_view_dir_toggle_entries, G_N_ELEMENTS(menu_view_dir_toggle_entries),
					    lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
					   menu_color_radio_entries, COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS,
					   0, G_CALLBACK(layout_color_menu_input_cb), lw);


	lw->ui_manager = gq_gtk_ui_manager_new();
	gq_gtk_ui_manager_set_add_tearoffs(lw->ui_manager, TRUE);
	gq_gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group, 0);

	DEBUG_1("%s layout_actions_setup: add menu", get_exec_time());
	error = nullptr;

	if (!gq_gtk_ui_manager_add_ui_from_resource(lw->ui_manager, GQ_RESOURCE_PATH_UI "/menu-classic.ui" , &error))
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}

	DEBUG_1("%s layout_actions_setup: editors", get_exec_time());
	layout_actions_setup_editors(lw);

	DEBUG_1("%s layout_actions_setup: actions_add_window", get_exec_time());
	layout_actions_add_window(lw, lw->window);
	DEBUG_1("%s layout_actions_setup: end", get_exec_time());
}

void layout_actions_add_window(LayoutWindow *lw, GtkWidget *window)
{
	GtkAccelGroup *group;

	if (!lw->ui_manager) return;

	group = gq_gtk_ui_manager_get_accel_group(lw->ui_manager);
	gtk_window_add_accel_group(GTK_WINDOW(window), group);
}

GtkWidget *layout_actions_menu_bar(LayoutWindow *lw)
{
	if (lw->menu_bar) return lw->menu_bar;
	lw->menu_bar = gq_gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu");
	g_object_ref(lw->menu_bar);
	return lw->menu_bar;
}

GtkWidget *layout_actions_toolbar(LayoutWindow *lw, ToolbarType type)
{
	if (lw->toolbar[type]) return lw->toolbar[type];

	lw->toolbar[type] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	gtk_widget_show(lw->toolbar[type]);
	g_object_ref(lw->toolbar[type]);
	return lw->toolbar[type];
}

GtkWidget *layout_actions_menu_tool_bar(LayoutWindow *lw)
{
	GtkWidget *menu_bar;
	GtkWidget *toolbar;

	if (lw->menu_tool_bar) return lw->menu_tool_bar;

	toolbar = layout_actions_toolbar(lw, TOOLBAR_MAIN);
	DEBUG_NAME(toolbar);
	lw->menu_tool_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

		{
		menu_bar = layout_actions_menu_bar(lw);
		DEBUG_NAME(menu_bar);
		gq_gtk_box_pack_start(GTK_BOX(lw->menu_tool_bar), menu_bar, FALSE, FALSE, 0);
		}

	gq_gtk_box_pack_start(GTK_BOX(lw->menu_tool_bar), toolbar, FALSE, FALSE, 0);

	g_object_ref(lw->menu_tool_bar);
	return lw->menu_tool_bar;
}

static void toolbar_clear_cb(GtkWidget *widget, gpointer)
{
	GtkAction *action;

	if (GTK_IS_BUTTON(widget))
		{
		action = static_cast<GtkAction *>(g_object_get_data(G_OBJECT(widget), "action"));
		if (g_object_get_data(G_OBJECT(widget), "id") )
			{
			g_signal_handler_disconnect(action, GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "id")));
			}
		}
	gq_gtk_widget_destroy(widget);
}

void layout_toolbar_clear(LayoutWindow *lw, ToolbarType type)
{
	if (lw->toolbar_merge_id[type])
		{
		gq_gtk_ui_manager_remove_ui(lw->ui_manager, lw->toolbar_merge_id[type]);
		gq_gtk_ui_manager_ensure_update(lw->ui_manager);
		}
	g_list_free_full(lw->toolbar_actions[type], g_free);
	lw->toolbar_actions[type] = nullptr;

	lw->toolbar_merge_id[type] = gq_gtk_ui_manager_new_merge_id(lw->ui_manager);

	if (lw->toolbar[type])
		{
		gtk_container_foreach(GTK_CONTAINER(lw->toolbar[type]), (GtkCallback)G_CALLBACK(toolbar_clear_cb), nullptr);
		}
}

static void action_radio_changed_cb(GtkAction *action, GtkAction *current, gpointer data)
{
	auto button = static_cast<GtkToggleButton *>(data);

	if (action == current )
		{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
		}
	else
		{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
		}
}

static void action_toggle_activate_cb(GtkAction* self, gpointer data)
{
	auto button = static_cast<GtkToggleButton *>(data);

	if (gq_gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(self)) != gtk_toggle_button_get_active(button))
		{
		gtk_toggle_button_set_active(button, gq_gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(self)));
		}
}

static gboolean toolbar_button_press_event_cb(GtkWidget *, GdkEvent *, gpointer data)
{
	gq_gtk_action_activate(GTK_ACTION(data));

	return TRUE;
}

void layout_toolbar_add(LayoutWindow *lw, ToolbarType type, const gchar *action_name)
{
	const gchar *path = nullptr;
	const gchar *tooltip_text = nullptr;
	GtkAction *action;
	GtkWidget *action_icon = nullptr;
	GtkWidget *button;
	gulong id;

	if (!action_name || !lw->ui_manager) return;

	if (!lw->toolbar[type])
		{
		return;
		}

	switch (type)
		{
		case TOOLBAR_MAIN:
			path = "/ToolBar";
			break;
		case TOOLBAR_STATUS:
			path = "/StatusBar";
			break;
		default:
			break;
		}

	if (g_str_has_suffix(action_name, ".desktop"))
		{
		/* this may be called before the external editors are read
		   create a dummy action for now */
		if (!lw->action_group_editors)
			{
			lw->action_group_editors = gq_gtk_action_group_new("MenuActionsExternal");
			gq_gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group_editors, 1);
			}
		if (!gq_gtk_action_group_get_action(lw->action_group_editors, action_name))
			{
			GtkActionEntry entry = { action_name,
			                         GQ_ICON_MISSING_IMAGE,
			                         action_name,
			                         nullptr,
			                         nullptr,
			                         nullptr
			                       };
			DEBUG_1("Creating temporary action %s", action_name);
			gq_gtk_action_group_add_actions(lw->action_group_editors, &entry, 1, lw);
			}
		}

	if (g_strcmp0(action_name, "Separator") == 0)
		{
		button = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
		}
	else
		{
		if (g_str_has_suffix(action_name, ".desktop"))
			{
			action = gq_gtk_action_group_get_action(lw->action_group_editors, action_name);

			/** @FIXME Using tootip as a flag to layout_actions_setup_editors()
			 * is not a good way.
			 */
			tooltip_text = gtk_action_get_label(action);
			}
		else
			{
			action = gq_gtk_action_group_get_action(lw->action_group, action_name);

			tooltip_text = gq_gtk_action_get_tooltip(action);
			}

		action_icon = gq_gtk_action_create_icon(action, GTK_ICON_SIZE_SMALL_TOOLBAR);

		gq_gtk_ui_manager_add_ui(lw->ui_manager, lw->toolbar_merge_id[type], path, action_name, action_name, GTK_UI_MANAGER_TOOLITEM, FALSE);

		if (GQ_GTK_IS_RADIO_ACTION(action) || GQ_GTK_IS_TOGGLE_ACTION(action))
			{
			button = gtk_toggle_button_new();
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), gq_gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
			}
		else
			{
			button = gtk_button_new();
			}

		if (action_icon)
			{
			gtk_button_set_image(GTK_BUTTON(button), action_icon);
			}
		else
			{
			gtk_button_set_label(GTK_BUTTON(button), action_name);
			}

		gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
		gtk_widget_set_tooltip_text(button, tooltip_text);

		if (GQ_GTK_IS_RADIO_ACTION(action))
			{
			id = g_signal_connect(G_OBJECT(action), "changed", G_CALLBACK(action_radio_changed_cb), button);
			g_object_set_data(G_OBJECT(button), "id", GUINT_TO_POINTER(id));
			}
		else if (GQ_GTK_IS_TOGGLE_ACTION(action))
			{
			id = g_signal_connect(G_OBJECT(action), "activate", G_CALLBACK(action_toggle_activate_cb), button);
			g_object_set_data(G_OBJECT(button), "id", GUINT_TO_POINTER(id));
			}

		g_signal_connect(G_OBJECT(button), "button_press_event", G_CALLBACK(toolbar_button_press_event_cb), action);
		g_object_set_data(G_OBJECT(button), "action", action);
		}

	gq_gtk_container_add(GTK_WIDGET(lw->toolbar[type]), GTK_WIDGET(button));
	gtk_widget_show(GTK_WIDGET(button));

	lw->toolbar_actions[type] = g_list_append(lw->toolbar_actions[type], g_strdup(action_name));
}

void layout_toolbar_add_default(LayoutWindow *lw, ToolbarType type)
{
	LayoutWindow *lw_first;
	GList *work_action;

	switch (type)
		{
		case TOOLBAR_MAIN:
			if (layout_window_list)
				{
				lw_first = static_cast<LayoutWindow *>(layout_window_list->data);
				if (lw_first->toolbar_actions[TOOLBAR_MAIN])
					{
					work_action = lw_first->toolbar_actions[type];
					while (work_action)
						{
						auto action = static_cast<gchar *>(work_action->data);
						work_action = work_action->next;
						layout_toolbar_add(lw, type, action);
						}
					}
				else
					{
					layout_toolbar_add(lw, type, "Back");
					layout_toolbar_add(lw, type, "Forward");
					layout_toolbar_add(lw, type, "Up");
					layout_toolbar_add(lw, type, "Home");
					layout_toolbar_add(lw, type, "Refresh");
					layout_toolbar_add(lw, type, "ZoomIn");
					layout_toolbar_add(lw, type, "ZoomOut");
					layout_toolbar_add(lw, type, "ZoomFit");
					layout_toolbar_add(lw, type, "Zoom100");
					layout_toolbar_add(lw, type, "Preferences");
					layout_toolbar_add(lw, type, "FloatTools");
					}
				}
			else
				{
				layout_toolbar_add(lw, type, "Back");
				layout_toolbar_add(lw, type, "Forward");
				layout_toolbar_add(lw, type, "Up");
				layout_toolbar_add(lw, type, "Home");
				layout_toolbar_add(lw, type, "Refresh");
				layout_toolbar_add(lw, type, "ZoomIn");
				layout_toolbar_add(lw, type, "ZoomOut");
				layout_toolbar_add(lw, type, "ZoomFit");
				layout_toolbar_add(lw, type, "Zoom100");
				layout_toolbar_add(lw, type, "Preferences");
				layout_toolbar_add(lw, type, "FloatTools");
				}
			break;
		case TOOLBAR_STATUS:
			if (layout_window_list)
				{
				lw_first = static_cast<LayoutWindow *>(layout_window_list->data);
				if (lw_first->toolbar_actions[TOOLBAR_STATUS])
					{
					work_action = lw_first->toolbar_actions[type];
					while (work_action)
						{
						auto action = static_cast<gchar *>(work_action->data);
						work_action = work_action->next;
						layout_toolbar_add(lw, type, action);
						}
					}
				else
					{
					layout_toolbar_add(lw, type, "ExifRotate");
					layout_toolbar_add(lw, type, "ShowInfoPixel");
					layout_toolbar_add(lw, type, "UseColorProfiles");
					}
				}
			else
				{
				layout_toolbar_add(lw, type, "ExifRotate");
				layout_toolbar_add(lw, type, "ShowInfoPixel");
				layout_toolbar_add(lw, type, "UseColorProfiles");
				}
			break;
		default:
			break;
		}
}



void layout_toolbar_write_config(LayoutWindow *lw, ToolbarType type, GString *outstr, gint indent)
{
	const gchar *name = nullptr;
	GList *work = lw->toolbar_actions[type];

	switch (type)
		{
		case TOOLBAR_MAIN:
			name = "toolbar";
			break;
		case TOOLBAR_STATUS:
			name = "statusbar";
			break;
		default:
			break;
		}

	WRITE_NL(); WRITE_STRING("<%s>", name);
	indent++;
	WRITE_NL(); WRITE_STRING("<clear/>");
	while (work)
		{
		auto action = static_cast<gchar *>(work->data);
		work = work->next;
		WRITE_NL(); WRITE_STRING("<toolitem ");
		write_char_option(outstr, indent + 1, "action", action);
		WRITE_STRING("/>");
		}
	indent--;
	WRITE_NL(); WRITE_STRING("</%s>", name);
}

void layout_toolbar_add_from_config(LayoutWindow *lw, ToolbarType type, const char **attribute_names, const gchar **attribute_values)
{
	gchar *action = nullptr;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("action", action)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	layout_toolbar_add(lw, type, action);
	g_free(action);
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

static gchar *layout_color_name_parse(const gchar *name)
{
	if (!name || !*name) return g_strdup(_("Empty"));
	return g_strdelimit(g_strdup(name), "_", '-');
}

void layout_util_sync_color(LayoutWindow *lw)
{
	GtkAction *action;
	gint input = 0;
	gboolean use_color;
	gboolean use_image = FALSE;
	gint i;
	gchar action_name[15];
#if HAVE_LCMS
	gchar *image_profile;
	gchar *screen_profile;
#endif

	if (!lw->action_group) return;
	if (!layout_image_color_profile_get(lw, input, use_image)) return;

	use_color = layout_image_color_profile_get_use(lw);

	action = gq_gtk_action_group_get_action(lw->action_group, "UseColorProfiles");
#if HAVE_LCMS
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), use_color);
	if (layout_image_color_profile_get_status(lw, &image_profile, &screen_profile))
		{
		gchar *buf;
		buf = g_strdup_printf(_("Image profile: %s\nScreen profile: %s"), image_profile, screen_profile);
		g_object_set(G_OBJECT(action), "tooltip", buf, NULL);
		g_free(image_profile);
		g_free(screen_profile);
		g_free(buf);
		}
	else
		{
		g_object_set(G_OBJECT(action), "tooltip", _("Click to enable color management"), NULL);
		}
#else
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), FALSE);
	gq_gtk_action_set_sensitive(action, FALSE);
	g_object_set(G_OBJECT(action), "tooltip", _("Color profiles not supported"), NULL);
#endif

	action = gq_gtk_action_group_get_action(lw->action_group, "UseImageProfile");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), use_image);
	gq_gtk_action_set_sensitive(action, use_color);

	for (i = 0; i < COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS; i++)
		{
		sprintf(action_name, "ColorProfile%d", i);
		action = gq_gtk_action_group_get_action(lw->action_group, action_name);

		if (i >= COLOR_PROFILE_FILE)
			{
			const gchar *name = options->color_profile.input_name[i - COLOR_PROFILE_FILE];
			const gchar *file = options->color_profile.input_file[i - COLOR_PROFILE_FILE];
			gchar *end;
			gchar *buf;

			if (!name || !name[0]) name = filename_from_path(file);

			end = layout_color_name_parse(name);
			buf = g_strdup_printf(_("Input _%d: %s"), i, end);
			g_free(end);

			g_object_set(G_OBJECT(action), "label", buf, NULL);
			g_free(buf);

			gq_gtk_action_set_visible(action, file && file[0]);
			}

		gq_gtk_action_set_sensitive(action, !use_image);
		gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), (i == input));
		}

	action = gq_gtk_action_group_get_action(lw->action_group, "Grayscale");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_image_get_desaturate(lw));
}

void layout_util_sync_file_filter(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowFileFilter");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_file_filter);
}

static void layout_util_sync_views(LayoutWindow *lw)
{
	GtkAction *action;
	OsdShowFlags osd_flags = image_osd_get(lw->image);

	if (!lw->action_group) return;

	action = gq_gtk_action_group_get_action(lw->action_group, "FolderTree");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.dir_view_type);

	action = gq_gtk_action_group_get_action(lw->action_group, "FloatTools");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.tools_float);

	action = gq_gtk_action_group_get_action(lw->action_group, "HideSelectableToolbars");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.selectable_toolbars_hidden);

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowInfoPixel");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_info_pixel);

	action = gq_gtk_action_group_get_action(lw->action_group, "IgnoreAlpha");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.ignore_alpha);

	action = gq_gtk_action_group_get_action(lw->action_group, "Animate");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.animate);

	action = gq_gtk_action_group_get_action(lw->action_group, "ImageOverlay");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), osd_flags != OSD_SHOW_NOTHING);

	action = gq_gtk_action_group_get_action(lw->action_group, "ExifRotate");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->image.exif_rotate_enable);

	action = gq_gtk_action_group_get_action(lw->action_group, "OverUnderExposed");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->overunderexposed);

	action = gq_gtk_action_group_get_action(lw->action_group, "RectangularSelection");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->collections.rectangular_selection);

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowFileFilter");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_file_filter);

	layout_util_sync_color(lw);
	layout_image_set_ignore_alpha(lw, lw->options.ignore_alpha);
}

void layout_util_sync(LayoutWindow *lw)
{
	layout_util_sync_views(lw);
}
