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

#ifndef THUMB_STANDARD_H
#define THUMB_STANDARD_H

#include <sys/types.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include <config.h>

#include "main-defines.h"
#include "typedefs.h"

class FileData;
struct ImageLoader;

#if GLIB_CHECK_VERSION (2, 34, 0)
#define THUMB_FOLDER_GLOBAL "thumbnails"
#else
#define THUMB_FOLDER_GLOBAL ".thumbnails"
#endif
#define THUMB_FOLDER_NORMAL "normal"
#define THUMB_FOLDER_LARGE  "large"
#define THUMB_NAME_EXTENSION ".png"


struct ThumbLoader
{
	gboolean standard_loader;

	ImageLoader *il;
	FileData *fd;

	time_t source_mtime;
	off_t source_size;
	mode_t source_mode;

	gchar *thumb_path;
	gchar *thumb_uri;

	gint save_width;
	gint display_width;

	gboolean cache_enable;
	gboolean cache_hit;

	gdouble progress;

	using Func = void (*)(ThumbLoader *, gpointer);
	Func func_done;
	Func func_error;
	Func func_progress;

	gpointer data;
};


ThumbLoader *thumb_loader_new(gint save_width, gint display_width);
void thumb_loader_set_callbacks(ThumbLoader *tl,
				    ThumbLoader::Func func_done,
				    ThumbLoader::Func func_error,
				    ThumbLoader::Func func_progress,
				    gpointer data);
void thumb_loader_set_cache(ThumbLoader *tl);
gboolean thumb_loader_start(ThumbLoader *tl, FileData *fd);
void thumb_loader_free(ThumbLoader *tl);

GdkPixbuf *thumb_loader_get_pixbuf(ThumbLoader *tl);

ThumbLoader *thumb_loader_std_thumb_file_validate(const gchar *thumb_path, gint allowed_days,
						     void (*func_valid)(const gchar *path, gboolean valid, gpointer data),
						     gpointer data);
void thumb_loader_std_thumb_file_validate_cancel(ThumbLoader *tl);


void thumb_std_maint_removed(const gchar *source);
void thumb_std_maint_moved(const gchar *source, const gchar *dest);

void thumb_notify_cb(FileData *fd, NotifyType type, gpointer data);

#endif
