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

void metadata_cache_free(FileData *fd);

gint metadata_queue_length();

GList *metadata_read_list(FileData *fd, const gchar *key, MetadataFormat format);
gchar *metadata_read_string(FileData *fd, const gchar *key, MetadataFormat format);
guint64 metadata_read_int(FileData *fd, const gchar *key, guint64 fallback);

#endif
