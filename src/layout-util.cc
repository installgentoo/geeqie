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
#include "intl.h"
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

	file_util_path_list_to_clipboard(layout_selection_list(lw), TRUE);
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

	file_util_delete(nullptr, layout_selection_list(lw), layout_window(lw));
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
  { "CopyPath",              nullptr,                           N_("_Copy to clipboard"),                               nullptr,               N_("Copy to clipboard"),                               CB(layout_menu_copy_path_cb) },
  { "EditMenu",              nullptr,                           N_("_Edit"),                                            nullptr,               nullptr,                                               nullptr },
  { "EscapeAlt1",            GQ_ICON_LEAVE_FULLSCREEN,          N_("_Leave full screen"),                               "Q",                   N_("Leave full screen"),                               CB(layout_menu_escape_cb) },                           
  { "Escape",                GQ_ICON_LEAVE_FULLSCREEN,          N_("_Leave full screen"),                              "Escape",               N_("Leave full screen"),                               CB(layout_menu_escape_cb) },                           
  { "FileMenu",              nullptr,                           N_("_File"),                                            nullptr,               nullptr,                                               nullptr },
  { "FindDupes",             GQ_ICON_FIND,                      N_("_Find duplicates..."),                              "D",                   N_("Find duplicates..."),                              CB(layout_menu_dupes_cb) },
  { "FirstImage",            GQ_ICON_GO_TOP,                    N_("_First Image"),                                     "Home",                N_("First Image"),                                     CB(layout_menu_image_first_cb) },
  { "FirstPage",             GQ_ICON_PREV_PAGE,                 N_("_First Page"),                                      "<control>Home",       N_( "First Page of multi-page image"),                 CB(layout_menu_page_first_cb) },
  { "Forward",               GQ_ICON_GO_NEXT,                   N_("_Forward"),                                         nullptr,               N_("Forward in folder history"),                       CB(layout_menu_forward_cb) },
  { "FullScreenAlt1",        GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "V",                   N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "FullScreenAlt2",        GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "F11",                 N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "FullScreen",            GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "F",                   N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "HelpMenu",              nullptr,                           N_("_Help"),                                            nullptr,               nullptr,                                               nullptr },
  { "Home",                  GQ_ICON_HOME,                      N_("_Home"),                                            nullptr,               N_("Home"),                                            CB(layout_menu_home_cb) },
  { "ImageOverlayCycle",     nullptr,                           N_("_Cycle through overlay modes"),                     "I",                   N_("Cycle through Overlay modes"),                     CB(layout_menu_overlay_toggle_cb) },                   
  { "LastImage",             GQ_ICON_GO_BOTTOM,                 N_("_Last Image"),                                      "End",                 N_("Last Image"),                                      CB(layout_menu_image_last_cb) },
  { "LastPage",              GQ_ICON_NEXT_PAGE,                 N_("_Last Page"),                                       "<control>End",        N_("Last Page of multi-page image"),                   CB(layout_menu_page_last_cb) },
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
  { "ZoomFitAlt1",           GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "KP_Multiply",         N_("Zoom to fit"),                                     CB(layout_menu_zoom_fit_cb) },
  { "ZoomFit",               GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "X",                   N_("Zoom to fit"),                                     CB(layout_menu_zoom_fit_cb) },
  { "ZoomInAlt1",            GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "KP_Add",              N_("Zoom in"),                                         CB(layout_menu_zoom_in_cb) },
  { "ZoomIn",                GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "equal",               N_("Zoom in"),                                         CB(layout_menu_zoom_in_cb) },
  { "ZoomOutAlt1",           GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "KP_Subtract",         N_("Zoom out"),                                        CB(layout_menu_zoom_out_cb) },
  { "ZoomOut",               GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "minus",               N_("Zoom out"),                                        CB(layout_menu_zoom_out_cb) }
};

static GtkToggleActionEntry menu_toggle_entries[] = {
  { "Animate",                 nullptr,                              N_("_Animation"),               "A",               N_("Toggle animation"),              CB(layout_menu_animate_cb),                  FALSE  },
  { "ExifRotate",              GQ_ICON_ROTATE_LEFT,                  N_("_Exif rotate"),             "<alt>X",          N_("Toggle Exif rotate"),            CB(layout_menu_exif_rotate_cb),              FALSE  },
  { "Grayscale",               PIXBUF_INLINE_ICON_GRAYSCALE,         N_("Toggle _grayscale"),        "<shift>G",        N_("Toggle grayscale"),              CB(layout_menu_alter_desaturate_cb),         FALSE  },
  { "ImageOverlay",            nullptr,                              N_("Image _Overlay"),           nullptr,           N_("Image Overlay"),                 CB(layout_menu_overlay_cb),                  FALSE  },
  { "OverUnderExposed",        PIXBUF_INLINE_ICON_EXPOSURE,          N_("Over/Under Exposed"),       "<shift>E",        N_("Highlight over/under exposed"),  CB(layout_menu_select_overunderexposed_cb),  FALSE  },
  { "RectangularSelection",    PIXBUF_INLINE_ICON_SELECT_RECTANGLE,  N_("Rectangular Selection"),    "<alt>R",          N_("Rectangular Selection"),         CB(layout_menu_rectangular_selection_cb),    FALSE  },
  { "ShowFileFilter",          GQ_ICON_FILE_FILTER,                  N_("Show File Filter"),         nullptr,           N_("Show File Filter"),              CB(layout_menu_file_filter_cb),              FALSE  },
  { "UseColorProfiles",        GQ_ICON_COLOR_MANAGEMENT,             N_("Use _color profiles"),      nullptr,           N_("Use color profiles"),            CB(layout_color_menu_enable_cb),             FALSE  },
  { "UseImageProfile",         nullptr,                              N_("Use profile from _image"),  nullptr,           N_("Use profile from image"),        CB(layout_color_menu_use_image_cb),          FALSE  }
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

static void layout_actions_setup_editors(LayoutWindow *lw)
{
	GError *error;
	GList *editors_list;
	GList *work;
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
	desc = g_string_new("<ui>");

	editors_list = editor_list_get();

	work = editors_list;
	while (work)
		{
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

		g_string_append_printf(desc, "<accelerator action='%s'/>", editor->key);

		work = work->next;
		}

	g_string_append(desc, "</ui>");

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

static gint layout_editors_reload_idle_id = -1;
static GList *layout_editors_desktop_files = nullptr;

static gboolean layout_editors_reload_idle_cb(gpointer)
{
	if (!layout_editors_desktop_files)
		{
		DEBUG_1("%s layout_editors_reload_idle_cb: get_desktop_files", get_exec_time());
		layout_editors_desktop_files = editor_get_desktop_files();
		return G_SOURCE_CONTINUE;
		}

	editor_read_desktop_file(static_cast<const gchar *>(layout_editors_desktop_files->data));
	g_free(layout_editors_desktop_files->data);
	layout_editors_desktop_files = g_list_delete_link(layout_editors_desktop_files, layout_editors_desktop_files);


	if (!layout_editors_desktop_files)
		{
		DEBUG_1("%s layout_editors_reload_idle_cb: setup_editors", get_exec_time());
		editor_table_finish();

		layout_actions_setup_editors(main_lw);

		DEBUG_1("%s layout_editors_reload_idle_cb: setup_editors done", get_exec_time());

		layout_editors_reload_idle_id = -1;
		return G_SOURCE_REMOVE;
		}
	return G_SOURCE_CONTINUE;
}

void layout_editors_reload_start()
{
	DEBUG_1("%s layout_editors_reload_start", get_exec_time());

	if (layout_editors_reload_idle_id != -1)
		{
		g_source_remove(layout_editors_reload_idle_id);
		g_list_free_full(layout_editors_desktop_files, g_free);
		}

	editor_table_clear();
	layout_editors_reload_idle_id = g_idle_add(layout_editors_reload_idle_cb, nullptr);
}

void layout_editors_reload_finish()
{
	if (layout_editors_reload_idle_id != -1)
		{
		DEBUG_1("%s layout_editors_reload_finish", get_exec_time());
		g_source_remove(layout_editors_reload_idle_id);
		while (layout_editors_reload_idle_id != -1)
			{
			layout_editors_reload_idle_cb(nullptr);
			}
		}
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
}

void layout_util_sync(LayoutWindow *lw)
{
	layout_util_sync_views(lw);
}
