/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: John Ellis, Laurent Monin
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

#ifndef METADATA_H
#define METADATA_H

#include <glib.h>
#include <gtk/gtk.h>

#include "typedefs.h"

class FileData;

#define COMMENT_KEY "Xmp.dc.description"
#define KEYWORD_KEY "Xmp.dc.subject"
#define ORIENTATION_KEY "Xmp.tiff.Orientation"
#define RATING_KEY "Xmp.xmp.Rating"

void metadata_cache_free(FileData *fd);

gboolean metadata_write_queue_remove(FileData *fd);
gboolean metadata_write_perform(FileData *fd);
gboolean metadata_write_queue_confirm(gboolean force_dialog, FileUtilDoneFunc done_func, gpointer done_data);
void metadata_notify_cb(FileData *fd, NotifyType type, gpointer data);

gint metadata_queue_length();

gboolean metadata_write_revert(FileData *fd, const gchar *key);
gboolean metadata_write_list(FileData *fd, const gchar *key, const GList *values);
gboolean metadata_write_string(FileData *fd, const gchar *key, const char *value);
gboolean metadata_write_int(FileData *fd, const gchar *key, guint64 value);

GList *metadata_read_list(FileData *fd, const gchar *key, MetadataFormat format);
gchar *metadata_read_string(FileData *fd, const gchar *key, MetadataFormat format);
guint64 metadata_read_int(FileData *fd, const gchar *key, guint64 fallback);
gdouble metadata_read_GPS_coord(FileData *fd, const gchar *key, gdouble fallback);
gdouble metadata_read_GPS_direction(FileData *fd, const gchar *key, gdouble fallback);
gboolean metadata_write_GPS_coord(FileData *fd, const gchar *key, gdouble value);

gboolean metadata_append_string(FileData *fd, const gchar *key, const char *value);
gboolean metadata_append_list(FileData *fd, const gchar *key, const GList *values);

#endif
