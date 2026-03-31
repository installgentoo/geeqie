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

#include "metadata.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <glib-object.h>

#include <config.h>

#include "debug.h"
#include "exif.h"
#include "filedata.h"
#include "intl.h"
#include "ui-fileops.h"

struct ExifData;

namespace
{

enum MetadataKey {
	MK_NONE,
	MK_KEYWORDS,
	MK_COMMENT
};

struct MetadataCacheEntry {
	gchar *key;
	GList *values;
};

gint metadata_cache_entry_compare_key(const MetadataCacheEntry *entry, const gchar *key)
{
	return strcmp(entry->key, key);
}

void metadata_cache_entry_free(MetadataCacheEntry *entry)
{
	if (!entry) return;

	g_free(entry->key);
	g_list_free_full(entry->values, g_free);
	g_free(entry);
}

} // namespace


/*
 *-------------------------------------------------------------------
 * long-term cache - keep keywords from whole dir in memory
 *-------------------------------------------------------------------
 */

/* fd->cached_metadata list of MetadataCacheEntry */

static void metadata_cache_update(FileData *fd, const gchar *key, const GList *values)
{
	GList *work;

	work = g_list_find_custom(fd->cached_metadata, key, reinterpret_cast<GCompareFunc>(metadata_cache_entry_compare_key));
	if (work)
		{
		/* key found - just replace values */
		auto *entry = static_cast<MetadataCacheEntry *>(work->data);

		g_list_free_full(entry->values, g_free);
		entry->values = string_list_copy(values);
		DEBUG_1("updated %s %s\n", key, fd->path);
		return;
		}

	/* key not found - prepend new entry */
	auto *entry = g_new0(MetadataCacheEntry, 1);
	entry->key = g_strdup(key);
	entry->values = string_list_copy(values);

	fd->cached_metadata = g_list_prepend(fd->cached_metadata, entry);
	DEBUG_1("added %s %s\n", key, fd->path);
}

static const GList *metadata_cache_get(FileData *fd, const gchar *key)
{
	GList *work;

	work = g_list_find_custom(fd->cached_metadata, key, reinterpret_cast<GCompareFunc>(metadata_cache_entry_compare_key));
	if (work)
		{
		/* key found */
		auto *entry = static_cast<MetadataCacheEntry *>(work->data);

		DEBUG_1("found %s %s\n", key, fd->path);
		return entry->values;
		}
	DEBUG_1("not found %s %s\n", key, fd->path);
	return nullptr;
}

void metadata_cache_free(FileData *fd)
{
	if (fd->cached_metadata) DEBUG_1("freed %s\n", fd->path);

	g_list_free_full(fd->cached_metadata, reinterpret_cast<GDestroyNotify>(metadata_cache_entry_free));
	fd->cached_metadata = nullptr;
}


GList *metadata_read_list(FileData *fd, const gchar *key, MetadataFormat format)
{
	ExifData *exif;
	GList *list = nullptr;
	const GList *cache_values;
	if (!fd) return nullptr;

	if (format == METADATA_PLAIN && strcmp(key, KEYWORD_KEY) == 0
	    && (cache_values = metadata_cache_get(fd, key)))
		{
		return string_list_copy(cache_values);
		}

	exif = exif_read_fd(fd); /* this is cached, thus inexpensive */
	if (!exif) return nullptr;
	list = exif_get_metadata(exif, key, format);
	exif_free_fd(fd, exif);

	if (format == METADATA_PLAIN && strcmp(key, KEYWORD_KEY) == 0)
		{
		metadata_cache_update(fd, key, list);
		}

	return list;
}

gchar *metadata_read_string(FileData *fd, const gchar *key, MetadataFormat format)
{
	GList *string_list = metadata_read_list(fd, key, format);
	if (string_list)
		{
		auto str = static_cast<gchar *>(string_list->data);
		string_list->data = nullptr;
		g_list_free_full(string_list, g_free);
		return str;
		}
	return nullptr;
}

guint64 metadata_read_int(FileData *fd, const gchar *key, guint64 fallback)
{
	guint64 ret;
	gchar *endptr;
	gchar *string = metadata_read_string(fd, key, METADATA_PLAIN);
	if (!string) return fallback;

	ret = g_ascii_strtoull(string, &endptr, 10);
	if (string == endptr) ret = fallback;
	g_free(string);
	return ret;
}
