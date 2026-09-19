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

#include "cache-maint.h"


#include <cstdlib>
#include <cstring>

#include <glib-object.h>
#include <gtk/gtk.h>

#include "cache-loader.h"
#include "cache.h"
#include "compat.h"
#include "debug.h"
#include "filedata.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "main.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "thumb-standard.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"
#include "ui-utildlg.h"

namespace
{

struct CMData
{
	GenericDialog *gd;
	GtkWidget *entry;
	GtkWidget *spinner;
	GtkWidget *button_close;
	gint removed;
	GDestroyNotify done_func; /**< command line runs only: called with this instead of updating a dialog */
};

constexpr gint PURGE_DIALOG_WIDTH = 400;

void cache_maintain_home_close(CMData *cm)
{
	if (cm->gd) generic_dialog_close(cm->gd);
	g_free(cm);
}

} // namespace

/*
 *-----------------------------------------------------------------------------
 * Command line cache maintenance program functions
 *-----------------------------------------------------------------------------
 */
static gchar *cache_maintenance_path = nullptr;
static GtkStatusIcon *status_icon;

static void cache_manager_sim_remote(const gchar *path, gboolean recurse, GSourceFunc destroy_func);

static gboolean cache_maintenance_sim_stop_cb(gpointer data)
{
	g_free(data);
	exit(EXIT_SUCCESS);
	return G_SOURCE_REMOVE;
}

static gboolean cache_maintenance_render_stop_cb(gpointer data)
{
	g_free(data);
	gtk_status_icon_set_tooltip_text(status_icon, _("Geeqie: Creating sim data..."));
	cache_manager_sim_remote(cache_maintenance_path, TRUE, cache_maintenance_sim_stop_cb);
	return G_SOURCE_REMOVE;
}

static void cache_manager_render_remote(const gchar *path, gboolean recurse, gboolean, GSourceFunc destroy_func);
static void cache_maintenance_clean_stop_cb(gpointer data)
{
	cache_maintain_home_close(static_cast<CMData *>(data));
	gtk_status_icon_set_tooltip_text(status_icon, _("Geeqie: Creating thumbs..."));
	cache_manager_render_remote(cache_maintenance_path, TRUE, FALSE, cache_maintenance_render_stop_cb);
}

static void cache_maintenance_user_cancel_cb()
{
	exit(EXIT_FAILURE);
}

static void cache_maintenance_status_icon_activate_cb(GtkStatusIcon *, gpointer)
{
	GtkWidget *menu;
	GtkWidget *item;

	menu = gtk_menu_new();

	item = gtk_menu_item_new_with_label(_("Exit Geeqie Cache Maintenance"));

	g_signal_connect(G_OBJECT(item), "activate", cache_maintenance_user_cancel_cb, item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show(item);

	/* take ownership of menu */
	g_object_ref_sink(G_OBJECT(menu));

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static void cache_maintain_home_remote(gboolean, gboolean, GDestroyNotify func);
void cache_maintenance(const gchar *path)
{
	cache_maintenance_path = g_strdup(path);

	g_autoptr(GdkPixbuf) pixbuf_icon = pixbuf_inline(PIXBUF_INLINE_ICON);
	status_icon = gtk_status_icon_new_from_pixbuf(pixbuf_icon);
	gtk_status_icon_set_tooltip_text(status_icon, _("Geeqie: Cleaning thumbs..."));
	gtk_status_icon_set_visible(status_icon, TRUE);
	g_signal_connect(G_OBJECT(status_icon), "activate", G_CALLBACK(cache_maintenance_status_icon_activate_cb), NULL);

	cache_maintain_home_remote(FALSE, FALSE, cache_maintenance_clean_stop_cb);
}

/*
 *-------------------------------------------------------------------
 * cache maintenance
 *-------------------------------------------------------------------
 */

static gboolean cache_maintain_home_done_cb(gpointer data)
{
	auto cm = static_cast<CMData *>(data);

	if (cm->done_func)
		{
		cm->done_func(cm);
		return G_SOURCE_REMOVE;
		}

	g_autofree gchar *text = g_strdup_printf(_("done, %d entries removed"), cm->removed);
	gq_gtk_entry_set_text(GTK_ENTRY(cm->entry), text);
	gtk_spinner_stop(GTK_SPINNER(cm->spinner));
	gtk_widget_set_sensitive(cm->button_close, TRUE);

	return G_SOURCE_REMOVE;
}

/* cache_sim_clean() stats the file of every row, so it runs on its own thread; cm outlives it because
 * the dialog cannot be closed (and the command line chain does not continue) until the done callback. */
static void cache_maintain_home_start(CMData *cm)
{
	g_thread_unref(g_thread_new("sim-cache-clean", [](gpointer data) -> gpointer
		{
		auto cm = static_cast<CMData *>(data);
		cm->removed = cache_sim_clean();
		g_idle_add(cache_maintain_home_done_cb, cm);
		return nullptr;
		}, cm));
}

static void cache_maintain_home_close_cb(GenericDialog *, gpointer data)
{
	auto cm = static_cast<CMData *>(data);

	if (!gtk_widget_get_sensitive(cm->button_close)) return;

	cache_maintain_home_close(cm);
}

static void cache_maintain_home(gboolean, gboolean, GtkWidget *parent)
{
	auto cm = g_new0(CMData, 1);

	cm->gd = generic_dialog_new(_("Maintenance"),
				    "main_maintenance",
				    parent, FALSE,
				    nullptr, cm);
	cm->gd->cancel_cb = cache_maintain_home_close_cb;
	cm->button_close = generic_dialog_add_button(cm->gd, GQ_ICON_CLOSE, _("Close"),
						     cache_maintain_home_close_cb, FALSE);
	gtk_widget_set_sensitive(cm->button_close, FALSE);

	generic_dialog_add_message(cm->gd, nullptr, _("Removing similarity data of deleted or changed files..."), nullptr, FALSE);
	gtk_window_set_default_size(GTK_WINDOW(cm->gd->dialog), PURGE_DIALOG_WIDTH, -1);

	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(cm->gd->vbox), hbox, FALSE, FALSE, 5);
	gtk_widget_show(hbox);

	cm->entry = gtk_entry_new();
	gtk_widget_set_can_focus(cm->entry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(cm->entry), FALSE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), cm->entry, TRUE, TRUE, 0);
	gtk_widget_show(cm->entry);

	cm->spinner = gtk_spinner_new();
	gtk_spinner_start(GTK_SPINNER(cm->spinner));
	gq_gtk_box_pack_start(GTK_BOX(hbox), cm->spinner, FALSE, FALSE, 0);
	gtk_widget_show(cm->spinner);

	gtk_widget_show(cm->gd->dialog);

	cache_maintain_home_start(cm);
}

/**
 * @brief culls cached data
 * @param func Called with the CMData when done
 */
static void cache_maintain_home_remote(gboolean, gboolean, GDestroyNotify func)
{
	auto cm = g_new0(CMData, 1);
	cm->done_func = func;

	cache_maintain_home_start(cm);
}

static void cache_maint_moved(FileData *fd)
{
	const gchar *src = fd->change->source;
	const gchar *dest = fd->change->dest;

	if (!src || !dest) return;

	cache_sim_moved(src, dest);

	if (options->thumbnails.enable_caching)
		thumb_std_maint_moved(src, dest);
}

static void cache_maint_removed(FileData *fd)
{
	cache_sim_removed(fd->path);

	if (options->thumbnails.enable_caching)
		thumb_std_maint_removed(fd->path);
}

void cache_notify_cb(FileData *fd, NotifyType type, gpointer)
{
	if (!(type & NOTIFY_CHANGE) || !fd->change) return;

	DEBUG_1("Notify cache_maint: %s %04x", fd->path, type);
	switch (fd->change->type)
		{
		case FILEDATA_CHANGE_MOVE:
		case FILEDATA_CHANGE_RENAME:
			cache_maint_moved(fd);
			break;
		case FILEDATA_CHANGE_DELETE:
			cache_maint_removed(fd);
			break;
		case FILEDATA_CHANGE_COPY:
		case FILEDATA_CHANGE_UNSPECIFIED:
			break;
		}
}


/*
 *-------------------------------------------------------------------
 * new cache maintenance utilities
 *-------------------------------------------------------------------
 */

struct CacheManager
{
	GenericDialog *dialog;
	GtkWidget *folder_entry;
	GtkWidget *progress;

	GList *list_todo;

	gint count_total;
	gint count_done;
};

struct CacheOpsData
{
	GenericDialog *gd;
	GList *active; /**< the loaders in flight: ThumbLoader* while rendering thumbnails, CacheLoader* for sim data */
	ThumbValidate *tv;
	GSourceFunc destroy_func; /* Used by the command line prog. functions */

	GList *list;
	GList *list_dir;

	gint days;

	GtkWidget *button_close;
	GtkWidget *button_stop;
	GtkWidget *button_start;
	GtkWidget *progress;
	GtkWidget *progress_bar;
	GtkWidget *spinner;

	GtkWidget *group;
	GtkWidget *entry;

	gint count_total;
	gint count_done;

	gboolean recurse;

	gboolean remote;

	guint idle_id; /* event source id */
};

static void cache_manager_render_release_thumb_pixbuf(ThumbLoader *tl)
{
	if (!tl || !tl->fd || !tl->fd->thumb_pixbuf) return;

	g_object_unref(tl->fd->thumb_pixbuf);
	tl->fd->thumb_pixbuf = nullptr;
}

static void cache_manager_render_free_active(CacheOpsData *cd)
{
	for (GList *work = cd->active; work; work = work->next)
		{
		auto tl = static_cast<ThumbLoader *>(work->data);
		cache_manager_render_release_thumb_pixbuf(tl);
		thumb_loader_free(tl);
		}

	g_list_free(cd->active);
	cd->active = nullptr;
}

static void cache_manager_render_reset(CacheOpsData *cd)
{
	filelist_free(cd->list);
	cd->list = nullptr;

	filelist_free(cd->list_dir);
	cd->list_dir = nullptr;

	cache_manager_render_free_active(cd);
}

static void cache_manager_render_close_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!gtk_widget_get_sensitive(cd->button_close)) return;

	cache_manager_render_reset(cd);
	generic_dialog_close(cd->gd);
	g_free(cd);
}

static void cache_manager_render_finish(CacheOpsData *cd)
{
	cache_manager_render_reset(cd);
	if (!cd->remote)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("done"));
		gtk_spinner_stop(GTK_SPINNER(cd->spinner));

		gtk_widget_set_sensitive(cd->group, TRUE);
		gtk_widget_set_sensitive(cd->button_start, TRUE);
		gtk_widget_set_sensitive(cd->button_stop, FALSE);
		gtk_widget_set_sensitive(cd->button_close, TRUE);
		}
}

static void cache_manager_render_stop_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("stopped"));
	cache_manager_render_finish(cd);
}

static void cache_manager_render_folder(CacheOpsData *cd, FileData *dir_fd)
{
	GList *list_d = nullptr;
	GList *list_f = nullptr;

	if (cd->recurse)
		{
		filelist_read(dir_fd, &list_f, &list_d);
		}
	else
		{
		filelist_read(dir_fd, &list_f, nullptr);
		}

	list_f = filelist_filter(list_f, FALSE);
	list_d = filelist_filter(list_d, TRUE);

	cd->list = g_list_concat(list_f, cd->list);
	cd->list_dir = g_list_concat(list_d, cd->list_dir);
}

static void cache_manager_render_fill(CacheOpsData *cd);

static void cache_manager_render_thumb_done_cb(ThumbLoader *tl, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	cache_manager_render_release_thumb_pixbuf(tl);
	cd->active = g_list_remove(cd->active, tl);
	thumb_loader_free(tl);

	cache_manager_render_fill(cd);
}

/**
 * Keeps loaders in flight, so the thumbnail worker pool has something to decode on every core; it bounds the
 * decoding itself (thumb-standard.cc), while this window bounds how many loaders a huge folder allocates.
 */
static void cache_manager_render_fill(CacheOpsData *cd)
{
	const guint in_flight_max = 2 * worker_thread_limit();

	while (g_list_length(cd->active) < in_flight_max)
		{
		if (cd->list)
			{
			auto fd = static_cast<FileData *>(cd->list->data);
			cd->list = g_list_remove(cd->list, fd);

			ThumbLoader *tl = thumb_loader_new(options->thumbnails.save_width, options->thumbnails.display_width);
			thumb_loader_set_callbacks(tl,
						   cache_manager_render_thumb_done_cb,
						   cache_manager_render_thumb_done_cb,
						   cd);
			thumb_loader_set_cache(tl);

			/* listed before starting: the done callback removes it, and must not run against a stale list */
			cd->active = g_list_prepend(cd->active, tl);

			if (thumb_loader_start(tl, fd))
				{
				if (!cd->remote)
					{
					gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), fd->path);
					cd->count_done = cd->count_done + 1;
					gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cd->progress_bar), static_cast<gdouble>(cd->count_done) / cd->count_total);
					}
				}
			else
				{
				cache_manager_render_release_thumb_pixbuf(tl);
				cd->active = g_list_remove(cd->active, tl);
				thumb_loader_free(tl);
				}

			file_data_unref(fd);
			continue;
			}

		if (cd->list_dir)
			{
			auto fd = static_cast<FileData *>(cd->list_dir->data);
			cd->list_dir = g_list_remove(cd->list_dir, fd);

			cache_manager_render_folder(cd, fd);

			file_data_unref(fd);
			continue;
			}

		break;
		}

	if (cd->active) return;

	if (!cd->remote)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("done"));
		}
	cache_manager_render_finish(cd);

	if (cd->destroy_func)
		{
		g_idle_add(cd->destroy_func, cd);
		}
}

static void cache_manager_render_start_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);
	gchar *path;
	GList *list_total = nullptr;

	if(!cd->remote)
		{
		if (cd->list || !gtk_widget_get_sensitive(cd->button_start)) return;
		}

	path = remove_trailing_slash((gq_gtk_entry_get_text(GTK_ENTRY(cd->entry))));
	parse_out_relatives(path);

	if (!isdir(path))
		{
		if (!cd->remote)
			{
			warning_dialog(_("Invalid folder"),
			_("The specified folder can not be found."),
			GQ_ICON_DIALOG_WARNING, cd->gd->dialog);
			}
		else
			{
			log_printf("The specified folder can not be found: %s\n", path);
			}
		}
	else
		{
		FileData *dir_fd;
		if(!cd->remote)
			{
			gtk_widget_set_sensitive(cd->group, FALSE);
			gtk_widget_set_sensitive(cd->button_start, FALSE);
			gtk_widget_set_sensitive(cd->button_stop, TRUE);
			gtk_widget_set_sensitive(cd->button_close, FALSE);

			gtk_spinner_start(GTK_SPINNER(cd->spinner));
			}
		dir_fd = file_data_new_dir(path);
		cache_manager_render_folder(cd, dir_fd);
		list_total = filelist_recursive(dir_fd);
		cd->count_total = g_list_length(list_total);
		file_data_unref(dir_fd);
		g_list_free(list_total);
		cd->count_done = 0;

		cache_manager_render_fill(cd);
		}

	g_free(path);
}

static void cache_manager_render_start_render_remote(CacheOpsData *cd, const gchar *user_path)
{
	gchar *path;

	path = remove_trailing_slash(user_path);
	parse_out_relatives(path);

	if (!isdir(path))
		{
		log_printf("The specified folder can not be found: %s\n", path);
		}
	else
		{
		FileData *dir_fd;

		dir_fd = file_data_new_dir(path);
		cache_manager_render_folder(cd, dir_fd);
		file_data_unref(dir_fd);
		cache_manager_render_fill(cd);
		}

	g_free(path);
}

static void cache_manager_render_dialog(GtkWidget *widget, const gchar *path)
{
	CacheOpsData *cd;
	GtkWidget *hbox;
	GtkWidget *label;

	cd = g_new0(CacheOpsData, 1);
	cd->remote = FALSE;

	cd->gd = generic_dialog_new(_("Create thumbnails"),
				    "create_thumbnails",
				    widget, FALSE,
				    nullptr, cd);
	gtk_window_set_default_size(GTK_WINDOW(cd->gd->dialog), PURGE_DIALOG_WIDTH, -1);
	cd->gd->cancel_cb = cache_manager_render_close_cb;
	cd->button_close = generic_dialog_add_button(cd->gd, GQ_ICON_CLOSE, _("Close"),
						     cache_manager_render_close_cb, FALSE);
	cd->button_start = generic_dialog_add_button(cd->gd, GQ_ICON_OK, _("S_tart"),
						     cache_manager_render_start_cb, FALSE);
	cd->button_stop = generic_dialog_add_button(cd->gd, GQ_ICON_STOP, _("Stop"),
						    cache_manager_render_stop_cb, FALSE);
	gtk_widget_set_sensitive(cd->button_stop, FALSE);

	generic_dialog_add_message(cd->gd, nullptr, _("Create thumbnails"), nullptr, FALSE);

	hbox = pref_box_new(cd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);
	cd->group = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	hbox = pref_box_new(cd->group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Folder:"));

	label = tab_completion_new(&cd->entry, path, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(cd->entry,_("Select folder") , TRUE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
	gtk_widget_show(label);

	pref_checkbox_new_int(cd->group, _("Include subfolders"), FALSE, &cd->recurse);

	pref_line(cd->gd->vbox, PREF_PAD_SPACE);
	hbox = pref_box_new(cd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	cd->progress = gtk_entry_new();
	gtk_widget_set_can_focus(cd->progress, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(cd->progress), FALSE);
	gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("click start to begin"));
	gq_gtk_box_pack_start(GTK_BOX(hbox), cd->progress, TRUE, TRUE, 0);
	gtk_widget_show(cd->progress);

	cd->progress_bar = gtk_progress_bar_new();
	gq_gtk_box_pack_start(GTK_BOX(cd->gd->vbox), cd->progress_bar, TRUE, TRUE, 0);
	gtk_widget_show(cd->progress_bar);

	cd->spinner = gtk_spinner_new();
	gq_gtk_box_pack_start(GTK_BOX(hbox), cd->spinner, FALSE, FALSE, 0);
	gtk_widget_show(cd->spinner);

	cd->list = nullptr;

	gtk_widget_show(cd->gd->dialog);
}

/**
 * @brief Create thumbnails
 * @param path Path to image folder
 * @param recurse
 * @param local Create thumbnails in same folder as images
 * @param destroy_func Function called when idle loop function terminates
 *
 *
 */
static void cache_manager_render_remote(const gchar *path, gboolean recurse, gboolean, GSourceFunc destroy_func)
{
	CacheOpsData *cd;

	cd = g_new0(CacheOpsData, 1);
	cd->recurse = recurse;
	cd->remote = TRUE;
	cd->destroy_func = destroy_func;

	cache_manager_render_start_render_remote(cd, path);
}

static void cache_manager_standard_clean_close_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!gtk_widget_get_sensitive(cd->button_close)) return;

	generic_dialog_close(cd->gd);

	thumb_loader_std_thumb_file_validate_cancel(cd->tv);
	filelist_free(cd->list);
	g_free(cd);
}

static void cache_manager_standard_clean_done(CacheOpsData *cd)
{
	if (!cd->remote)
		{
		gtk_widget_set_sensitive(cd->button_stop, FALSE);
		gtk_widget_set_sensitive(cd->button_close, TRUE);

		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cd->progress), 1.0);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cd->progress), _("done"));
		}
	if (cd->idle_id)
		{
		g_source_remove(cd->idle_id);
		cd->idle_id = 0;
		}

	thumb_loader_std_thumb_file_validate_cancel(cd->tv);
	cd->tv = nullptr;

	filelist_free(cd->list);
	cd->list = nullptr;
}

static void cache_manager_standard_clean_stop_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	cache_manager_standard_clean_done(cd);
}

static void cache_manager_standard_clean_valid_cb(const gchar *path, gboolean valid, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (path)
		{
		if (!valid)
			{
			DEBUG_1("thumb cleaned: %s", path);
			unlink_file(path);
			}

		cd->count_done++;
		if (!cd->remote)
			{
			if (cd->count_total != 0)
				{
				gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cd->progress),
							      static_cast<gdouble>(cd->count_done) / cd->count_total);
				}
			}
		}

	cd->tv = nullptr;
	if (cd->list)
		{
		FileData *next_fd;

		next_fd = static_cast<FileData *>(cd->list->data);
		cd->list = g_list_remove(cd->list, next_fd);

		cd->tv = thumb_loader_std_thumb_file_validate(next_fd->path, cd->days,
							      cache_manager_standard_clean_valid_cb, cd);
		file_data_unref(next_fd);
		}
	else
		{
		cache_manager_standard_clean_done(cd);
		}
}

static void cache_manager_standard_clean_start(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!cd->remote)
	{
		if (cd->list || !gtk_widget_get_sensitive(cd->button_start)) return;

		gtk_widget_set_sensitive(cd->button_start, FALSE);
		gtk_widget_set_sensitive(cd->button_stop, TRUE);
		gtk_widget_set_sensitive(cd->button_close, FALSE);

		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cd->progress), _("running..."));
	}

	const auto get_thumbnails_folder_files = [](const gchar *thumb_folder)
	{
		g_autofree gchar *path = g_build_filename(get_thumbnails_standard_cache_dir(), thumb_folder, NULL);
		FileData *dir_fd = file_data_new_dir(path);

		GList *list = nullptr;
		filelist_read(dir_fd, &list, nullptr);
		file_data_unref(dir_fd);

		return list;
	};

	cd->list = get_thumbnails_folder_files(THUMB_FOLDER_NORMAL);
	cd->list = g_list_concat(cd->list, get_thumbnails_folder_files(THUMB_FOLDER_LARGE));

	cd->count_total = g_list_length(cd->list);
	cd->count_done = 0;

	cache_manager_standard_clean_valid_cb(nullptr, TRUE, cd);
}

static void cache_manager_standard_clean_start_cb(GenericDialog *gd, gpointer data)
{
	cache_manager_standard_clean_start(gd, data);
}

static void cache_manager_standard_process(GtkWidget *widget, gboolean)
{
	CacheOpsData *cd;
	const gchar *icon_name;
	const gchar *msg;

	cd = g_new0(CacheOpsData, 1);
	cd->remote = FALSE;

	icon_name = GQ_ICON_CLEAR;
	msg = _("Removing old thumbnails...");

	cd->gd = generic_dialog_new(_("Maintenance"),
				    "standard_maintenance",
				    widget, FALSE,
				    nullptr, cd);
	cd->gd->cancel_cb = cache_manager_standard_clean_close_cb;
	cd->button_close = generic_dialog_add_button(cd->gd, GQ_ICON_CLOSE, _("Close"),
						     cache_manager_standard_clean_close_cb, FALSE);
	cd->button_start = generic_dialog_add_button(cd->gd, GQ_ICON_OK, _("S_tart"),
						     cache_manager_standard_clean_start_cb, FALSE);
	cd->button_stop = generic_dialog_add_button(cd->gd, GQ_ICON_STOP, _("Stop"),
						    cache_manager_standard_clean_stop_cb, FALSE);
	gtk_widget_set_sensitive(cd->button_stop, FALSE);

	generic_dialog_add_message(cd->gd, icon_name, msg, nullptr, FALSE);

	cd->progress = gtk_progress_bar_new();
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cd->progress), _("click start to begin"));
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cd->progress), TRUE);
	gq_gtk_box_pack_start(GTK_BOX(cd->gd->vbox), cd->progress, FALSE, FALSE, 0);
	gtk_widget_show(cd->progress);

	cd->days = 30;
	cd->tv = nullptr;
	cd->idle_id = 0;

	gtk_widget_show(cd->gd->dialog);
}

static void cache_manager_standard_clean_cb(GtkWidget *widget, gpointer)
{
	cache_manager_standard_process(widget, FALSE);
}

static void cache_manager_main_clean_cb(GtkWidget *widget, gpointer)
{
	cache_maintain_home(FALSE, FALSE, widget);
}


static void cache_manager_render_cb(GtkWidget *widget, gpointer)
{
	const gchar *path = layout_get_path(nullptr);

	if (!path || !*path) path = homedir();
	cache_manager_render_dialog(widget, path);
}

static CacheManager *cache_manager = nullptr;

static void cache_manager_close_cb(GenericDialog *gd, gpointer)
{
	generic_dialog_close(gd);

	g_free(cache_manager);
	cache_manager = nullptr;
}

static GtkWidget *cache_manager_location_label(GtkWidget *group, const gchar *subdir)
{
	GtkWidget *label;
	gchar *buf;

	buf = g_strdup_printf(_("Location: %s"), subdir);
	label = pref_label_new(group, buf);
	g_free(buf);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.5);

	return label;
}

static void cache_manager_sim_fill(CacheOpsData *cd);

static void cache_manager_sim_reset(CacheOpsData *cd)
{
	filelist_free(cd->list);
	cd->list = nullptr;

	filelist_free(cd->list_dir);
	cd->list_dir = nullptr;

	for (GList *work = cd->active; work; work = work->next)
		{
		cache_loader_free(static_cast<CacheLoader *>(work->data));
		}
	g_list_free(cd->active);
	cd->active = nullptr;
}

static void cache_manager_sim_close_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!gtk_widget_get_sensitive(cd->button_close)) return;

	cache_manager_sim_reset(cd);
	generic_dialog_close(cd->gd);
	g_free(cd);
}

static void cache_manager_sim_finish(CacheOpsData *cd)
{
	cache_manager_sim_reset(cd);
	if (!cd->remote)
		{
		gtk_spinner_stop(GTK_SPINNER(cd->spinner));

		gtk_widget_set_sensitive(cd->group, TRUE);
		gtk_widget_set_sensitive(cd->button_start, TRUE);
		gtk_widget_set_sensitive(cd->button_stop, FALSE);
		gtk_widget_set_sensitive(cd->button_close, TRUE);
		}
}

static void cache_manager_sim_stop_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("stopped"));
	cache_manager_sim_finish(cd);
}

static void cache_manager_sim_folder(CacheOpsData *cd, FileData *dir_fd)
{
	GList *list_d = nullptr;
	GList *list_f = nullptr;

	if (cd->recurse)
		{
		filelist_read(dir_fd, &list_f, &list_d);
		}
	else
		{
		filelist_read(dir_fd, &list_f, nullptr);
		}

	list_f = filelist_filter(list_f, FALSE);
	list_d = filelist_filter(list_d, TRUE);

	cd->list = g_list_concat(list_f, cd->list);
	cd->list_dir = g_list_concat(list_d, cd->list_dir);
}

static void cache_manager_sim_file_done_cb(CacheLoader *cl, gint, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	cd->active = g_list_remove(cd->active, cl);
	cache_loader_free(cl);

	cache_manager_sim_fill(cd);
}

static void cache_manager_sim_start_sim_remote(CacheOpsData *cd, const gchar *user_path)
{
	gchar *path;

	path = remove_trailing_slash(user_path);
	parse_out_relatives(path);

	if (!isdir(path))
		{
		log_printf("The specified folder can not be found: %s\n", path);
		}
	else
		{
		FileData *dir_fd;

		dir_fd = file_data_new_dir(path);
		cache_manager_sim_folder(cd, dir_fd);
		file_data_unref(dir_fd);
		cache_manager_sim_fill(cd);
		}

	g_free(path);
}

/**
 * @brief Generate .sim files
 * @param path Path to image folder
 * @param recurse
 * @param destroy_func Function called when idle loop function terminates
 *
 *
 */
static void cache_manager_sim_remote(const gchar *path, gboolean recurse, GSourceFunc destroy_func)
{
	CacheOpsData *cd;

	cd = g_new0(CacheOpsData, 1);
	cd->recurse = recurse;
	cd->remote = TRUE;
	cd->destroy_func = destroy_func;

	cache_manager_sim_start_sim_remote(cd, path);
}

/** Up to worker_thread_limit() loaders run at once, as in the duplicates window; each decodes on its own thread. */
static void cache_manager_sim_fill(CacheOpsData *cd)
{
	const guint in_flight_max = worker_thread_limit();

	while (g_list_length(cd->active) < in_flight_max)
		{
		if (cd->list)
			{
			auto fd = static_cast<FileData *>(cd->list->data);
			cd->list = g_list_remove(cd->list, fd);

			const auto load_mask = static_cast<CacheDataType>(CACHE_LOADER_DIMENSIONS | CACHE_LOADER_MD5SUM | CACHE_LOADER_SIMILARITY);
			CacheLoader *cl = cache_loader_new(fd, load_mask, cache_manager_sim_file_done_cb, cd);
			if (cl) cd->active = g_list_prepend(cd->active, cl);

			cd->count_done = cd->count_done + 1;
			if (!cd->remote)
				{
				gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), fd->path);
				gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cd->progress_bar), static_cast<gdouble>(cd->count_done) / cd->count_total);
				}

			file_data_unref(fd);
			continue;
			}

		if (cd->list_dir)
			{
			auto fd = static_cast<FileData *>(cd->list_dir->data);
			cd->list_dir = g_list_remove(cd->list_dir, fd);

			cache_manager_sim_folder(cd, fd);

			file_data_unref(fd);
			continue;
			}

		break;
		}

	if (cd->active) return;

	if (!cd->remote)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("done"));
		}

	cache_manager_sim_finish(cd);

	if (cd->destroy_func)
		{
		g_idle_add(cd->destroy_func, cd);
		}
}

static void cache_manager_sim_start_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);
	gchar *path;
	GList *list_total = nullptr;

	if (!cd->remote)
		{
		if (cd->list || !gtk_widget_get_sensitive(cd->button_start)) return;
		}

	path = remove_trailing_slash((gq_gtk_entry_get_text(GTK_ENTRY(cd->entry))));
	parse_out_relatives(path);

	if (!isdir(path))
		{
		if (!cd->remote)
			{
			warning_dialog(_("Invalid folder"),
			_("The specified folder can not be found."),
			GQ_ICON_DIALOG_WARNING, cd->gd->dialog);
			}
		else
			{
			log_printf("The specified folder can not be found: %s\n", path);
			}
		}
	else
		{
		FileData *dir_fd;
		if(!cd->remote)
			{
			gtk_widget_set_sensitive(cd->group, FALSE);
			gtk_widget_set_sensitive(cd->button_start, FALSE);
			gtk_widget_set_sensitive(cd->button_stop, TRUE);
			gtk_widget_set_sensitive(cd->button_close, FALSE);

			gtk_spinner_start(GTK_SPINNER(cd->spinner));
			}
		dir_fd = file_data_new_dir(path);
		cache_manager_sim_folder(cd, dir_fd);
		list_total = filelist_recursive(dir_fd);
		cd->count_total = g_list_length(list_total);
		file_data_unref(dir_fd);
		g_list_free(list_total);
		cd->count_done = 0;

		cache_manager_sim_fill(cd);
		}

	g_free(path);
}

static void cache_manager_sim_load_dialog(GtkWidget *widget, const gchar *path)
{
	CacheOpsData *cd;
	GtkWidget *hbox;
	GtkWidget *label;

	cd = g_new0(CacheOpsData, 1);
	cd->remote = FALSE;
	cd->recurse = TRUE;

	cd->gd = generic_dialog_new(_("Create similarity data"), "create_sim_files", widget, FALSE, nullptr, cd);
	gtk_window_set_default_size(GTK_WINDOW(cd->gd->dialog), PURGE_DIALOG_WIDTH, -1);
	cd->gd->cancel_cb = cache_manager_sim_close_cb;
	cd->button_close = generic_dialog_add_button(cd->gd, GQ_ICON_CLOSE, _("Close"),
						     cache_manager_sim_close_cb, FALSE);
	cd->button_start = generic_dialog_add_button(cd->gd, GQ_ICON_OK, _("S_tart"),
						     cache_manager_sim_start_cb, FALSE);
	cd->button_stop = generic_dialog_add_button(cd->gd, GQ_ICON_STOP, _("Stop"),
						    cache_manager_sim_stop_cb, FALSE);
	gtk_widget_set_sensitive(cd->button_stop, FALSE);

	generic_dialog_add_message(cd->gd, nullptr, _("Create similarity data recursively"), nullptr, FALSE);

	hbox = pref_box_new(cd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);
	cd->group = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	hbox = pref_box_new(cd->group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Folder:"));

	label = tab_completion_new(&cd->entry, path, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(cd->entry,_("Select folder") , TRUE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
	gtk_widget_show(label);

	pref_line(cd->gd->vbox, PREF_PAD_SPACE);
	hbox = pref_box_new(cd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	cd->progress = gtk_entry_new();
	gtk_widget_set_can_focus(cd->progress, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(cd->progress), FALSE);
	gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("click start to begin"));
	gq_gtk_box_pack_start(GTK_BOX(hbox), cd->progress, TRUE, TRUE, 0);
	gtk_widget_show(cd->progress);

	cd->progress_bar = gtk_progress_bar_new();
	gq_gtk_box_pack_start(GTK_BOX(cd->gd->vbox), cd->progress_bar, TRUE, TRUE, 0);
	gtk_widget_show(cd->progress_bar);

	cd->spinner = gtk_spinner_new();
	gq_gtk_box_pack_start(GTK_BOX(hbox), cd->spinner, FALSE, FALSE, 0);
	gtk_widget_show(cd->spinner);

	cd->list = nullptr;

	gtk_widget_show(cd->gd->dialog);
}

static void cache_manager_sim_load_cb(GtkWidget *widget, gpointer)
{
	const gchar *path = layout_get_path(nullptr);

	if (!path || !*path) path = homedir();
	cache_manager_sim_load_dialog(widget, path);
}

static void cache_manager_cache_maintenance_close_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!gtk_widget_get_sensitive(cd->button_close)) return;

	cache_manager_sim_reset(cd);
	generic_dialog_close(cd->gd);
	g_free(cd);
}

static void cache_manager_cache_maintenance_start_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);
	gchar *path;
	gchar *cmd_line;

	if (!cd->remote)
		{
		if (cd->list || !gtk_widget_get_sensitive(cd->button_start)) return;
		}

	path = remove_trailing_slash((gq_gtk_entry_get_text(GTK_ENTRY(cd->entry))));
	parse_out_relatives(path);

	if (!isdir(path))
		{
		if (!cd->remote)
			{
			warning_dialog(_("Invalid folder"),
			_("The specified folder can not be found."),
			GQ_ICON_DIALOG_WARNING, cd->gd->dialog);
			}
		else
			{
			log_printf("The specified folder can not be found: \"%s\"\n", path);
			}
		}
	else
		{
		cmd_line = g_strdup_printf("%s --cache-maintenance=\"%s\"", gq_executable_path, path);

		g_spawn_command_line_async(cmd_line, nullptr);

		g_free(cmd_line);
		generic_dialog_close(cd->gd);
		cache_manager_sim_reset(cd);
		g_free(cd);
		}

	g_free(path);
}

static void cache_manager_cache_maintenance_load_dialog(GtkWidget *widget, const gchar *path)
{
	CacheOpsData *cd;
	GtkWidget *hbox;
	GtkWidget *label;

	cd = g_new0(CacheOpsData, 1);
	cd->remote = FALSE;
	cd->recurse = TRUE;

	cd->gd = generic_dialog_new(_("Background cache maintenance"), "background_cache_maintenance", widget, FALSE, nullptr, cd);
	gtk_window_set_default_size(GTK_WINDOW(cd->gd->dialog), PURGE_DIALOG_WIDTH, -1);
	cd->gd->cancel_cb = cache_manager_cache_maintenance_close_cb;
	cd->button_close = generic_dialog_add_button(cd->gd, GQ_ICON_CLOSE, _("Close"),
						     cache_manager_cache_maintenance_close_cb, FALSE);
	cd->button_start = generic_dialog_add_button(cd->gd, GQ_ICON_OK, _("S_tart"),
						     cache_manager_cache_maintenance_start_cb, FALSE);

	generic_dialog_add_message(cd->gd, nullptr, _("Recursively delete orphaned thumbnails\nand similarity data, and create new\nthumbnails and similarity data"), nullptr, FALSE);

	hbox = pref_box_new(cd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);
	cd->group = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	hbox = pref_box_new(cd->group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Folder:"));

	label = tab_completion_new(&cd->entry, path, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(cd->entry,_("Select folder") , TRUE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
	gtk_widget_show(label);

	cd->list = nullptr;

	gtk_widget_show(cd->gd->dialog);
}

static void cache_manager_cache_maintenance_load_cb(GtkWidget *widget, gpointer)
{
	const gchar *path = layout_get_path(nullptr);

	if (!path || !*path) path = homedir();
	cache_manager_cache_maintenance_load_dialog(widget, path);
}

void cache_manager_show()
{
	GenericDialog *gd;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *table;
	GtkSizeGroup *sizegroup;
	gchar *path;

	if (cache_manager)
		{
		gtk_window_present(GTK_WINDOW(cache_manager->dialog->dialog));
		return;
		}

	cache_manager = g_new0(CacheManager, 1);

	cache_manager->dialog = generic_dialog_new(_("Cache Maintenance"),
						   "cache_manager",
						   nullptr, FALSE,
						   nullptr, cache_manager);
	gd = cache_manager->dialog;

	gd->cancel_cb = cache_manager_close_cb;
	generic_dialog_add_button(gd, GQ_ICON_CLOSE, _("Close"),
				  cache_manager_close_cb, FALSE);

	generic_dialog_add_message(gd, nullptr, _("Cache and Data Maintenance"), nullptr, FALSE);

	sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	group = pref_group_new(gd->vbox, FALSE, _("Similarity cache"), GTK_ORIENTATION_VERTICAL);

	cache_manager_location_label(group, get_sim_cache_path());

	table = pref_table_new(group, 2, 2, FALSE, FALSE);

	button = pref_table_button(table, 0, 0, GQ_ICON_CLEAR, _("Clean up"),
				   G_CALLBACK(cache_manager_main_clean_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 0, _("Remove similarity data of deleted or changed files."), GTK_ALIGN_START);

	group = pref_group_new(gd->vbox, FALSE, _("Shared thumbnail cache"), GTK_ORIENTATION_VERTICAL);

	path = g_build_filename(get_thumbnails_standard_cache_dir(), NULL);
	cache_manager_location_label(group, path);
	g_free(path);

	table = pref_table_new(group, 2, 2, FALSE, FALSE);

	button = pref_table_button(table, 0, 0, GQ_ICON_CLEAR, _("Clean up"),
				   G_CALLBACK(cache_manager_standard_clean_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 0, _("Remove orphaned or outdated thumbnails."), GTK_ALIGN_START);

	group = pref_group_new(gd->vbox, FALSE, _("Create thumbnails"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);

	button = pref_table_button(table, 0, 1, GQ_ICON_RUN, _("Render"),
				   G_CALLBACK(cache_manager_render_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 1, _("Render thumbnails for a specific folder."), GTK_ALIGN_START);
	gtk_widget_set_sensitive(group, options->thumbnails.enable_caching);

	group = pref_group_new(gd->vbox, FALSE, _("File similarity cache"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 3, 2, FALSE, FALSE);

	button = pref_table_button(table, 0, 0, GQ_ICON_RUN, _("Create"),
				   G_CALLBACK(cache_manager_sim_load_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 0, _("Create similarity data recursively."), GTK_ALIGN_START);
	gtk_widget_set_sensitive(group, options->thumbnails.enable_caching);

	group = pref_group_new(gd->vbox, FALSE, _("Background cache maintenance"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 3, 2, FALSE, FALSE);

	button = pref_table_button(table, 0, 0, GQ_ICON_RUN, _("Select"),
				   G_CALLBACK(cache_manager_cache_maintenance_load_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 0, _("Run cache maintenance as a background job."), GTK_ALIGN_START);
	gtk_widget_set_sensitive(group, options->thumbnails.enable_caching);

	gtk_widget_show(cache_manager->dialog->dialog);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
