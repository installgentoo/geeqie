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

#include "cache.h"
#include "debug.h"
#include "exif.h"
#include "filedata.h"
#include "intl.h"
#include "layout-util.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "rcfile.h"
#include "secure-save.h"
#include "ui-fileops.h"
#include "utilops.h"

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

/* If contents change, keep GuideOptionsMetadata.xml up to date */
/**
 *  @brief Tags that will be written to all files in a group - selected by: options->metadata.sync_grouped_files, Preferences/Metadata/Write The Same Description Tags To All Grouped Sidecars
 */
constexpr std::array<const gchar *, 22> group_keys{
	"Xmp.dc.title",
	"Xmp.photoshop.Urgency",
	"Xmp.photoshop.Category",
	"Xmp.photoshop.SupplementalCategory",
	"Xmp.dc.subject",
	"Xmp.iptc.Location",
	"Xmp.photoshop.Instruction",
	"Xmp.photoshop.DateCreated",
	"Xmp.dc.creator",
	"Xmp.photoshop.AuthorsPosition",
	"Xmp.photoshop.City",
	"Xmp.photoshop.State",
	"Xmp.iptc.CountryCode",
	"Xmp.photoshop.Country",
	"Xmp.photoshop.TransmissionReference",
	"Xmp.photoshop.Headline",
	"Xmp.photoshop.Credit",
	"Xmp.photoshop.Source",
	"Xmp.dc.rights",
	"Xmp.dc.description",
	"Xmp.photoshop.CaptionWriter",
	"Xmp.xmp.Rating",
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

void string_list_free(gpointer data)
{
	g_list_free_full(static_cast<GList *>(data), g_free);
}

} // namespace

static gboolean metadata_write_queue_idle_cb(gpointer data);
static gboolean metadata_legacy_write(FileData *fd);
static void metadata_legacy_delete(FileData *fd, const gchar *except);
static gboolean metadata_file_read(gchar *path, GList **keywords, gchar **comment);


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

static void metadata_cache_remove(FileData *fd, const gchar *key)
{
	GList *work;

	work = g_list_find_custom(fd->cached_metadata, key, reinterpret_cast<GCompareFunc>(metadata_cache_entry_compare_key));
	if (work)
		{
		/* key found */
		auto *entry = static_cast<MetadataCacheEntry *>(work->data);

		metadata_cache_entry_free(entry);
		fd->cached_metadata = g_list_delete_link(fd->cached_metadata, work);
		DEBUG_1("removed %s %s\n", key, fd->path);
		return;
		}
	DEBUG_1("not removed %s %s\n", key, fd->path);
}

void metadata_cache_free(FileData *fd)
{
	if (fd->cached_metadata) DEBUG_1("freed %s\n", fd->path);

	g_list_free_full(fd->cached_metadata, reinterpret_cast<GDestroyNotify>(metadata_cache_entry_free));
	fd->cached_metadata = nullptr;
}


/*
 *-------------------------------------------------------------------
 * write queue
 *-------------------------------------------------------------------
 */

static GList *metadata_write_queue = nullptr;
static guint metadata_write_idle_id = 0; /* event source id */

static void metadata_write_queue_add(FileData *fd)
{
	if (!g_list_find(metadata_write_queue, fd))
		{
		metadata_write_queue = g_list_prepend(metadata_write_queue, fd);
		file_data_ref(fd);

		layout_util_status_update_write_all();
		}

	if (metadata_write_idle_id)
		{
		g_source_remove(metadata_write_idle_id);
		metadata_write_idle_id = 0;
		}

	if (options->metadata.confirm_after_timeout)
		{
		metadata_write_idle_id = g_timeout_add(options->metadata.confirm_timeout * 1000, metadata_write_queue_idle_cb, nullptr);
		}
}


gboolean metadata_write_queue_remove(FileData *fd)
{
	g_hash_table_destroy(fd->modified_xmp);
	fd->modified_xmp = nullptr;

	metadata_write_queue = g_list_remove(metadata_write_queue, fd);

	file_data_increment_version(fd);
	file_data_send_notification(fd, NOTIFY_REREAD);

	file_data_unref(fd);

	layout_util_status_update_write_all();
	return TRUE;
}

void metadata_notify_cb(FileData *fd, NotifyType type, gpointer)
{
	if (type & (NOTIFY_REREAD | NOTIFY_CHANGE))
		{
		metadata_cache_free(fd);

		if (g_list_find(metadata_write_queue, fd))
			{
			DEBUG_1("Notify metadata: %s %04x", fd->path, type);
			if (!isname(fd->path))
				{
				/* ignore deleted files */
				metadata_write_queue_remove(fd);
				}
			}
		}
}

gboolean metadata_write_queue_confirm(gboolean force_dialog, FileUtilDoneFunc done_func, gpointer done_data)
{
	GList *work;
	GList *to_approve = nullptr;

	work = metadata_write_queue;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (!isname(fd->path))
			{
			/* ignore deleted files */
			metadata_write_queue_remove(fd);
			continue;
			}

		if (fd->change) continue; /* another operation in progress, skip this file for now */

		to_approve = g_list_prepend(to_approve, file_data_ref(fd));
		}

	file_util_write_metadata(nullptr, to_approve, nullptr, force_dialog, done_func, done_data);

	return (metadata_write_queue != nullptr);
}

static gboolean metadata_write_queue_idle_cb(gpointer)
{
	metadata_write_queue_confirm(FALSE, nullptr, nullptr);
	metadata_write_idle_id = 0;
	return FALSE;
}

gboolean metadata_write_perform(FileData *fd)
{
	gboolean success;
	ExifData *exif;
	guint lf;

	g_assert(fd->change);

	lf = strlen(GQ_CACHE_EXT_METADATA);
	if (fd->change->dest &&
	    g_ascii_strncasecmp(fd->change->dest + strlen(fd->change->dest) - lf, GQ_CACHE_EXT_METADATA, lf) == 0)
		{
		success = metadata_legacy_write(fd);
		if (success) metadata_legacy_delete(fd, fd->change->dest);
		return success;
		}

	/* write via exiv2 */
	/*  we can either use cached metadata which have fd->modified_xmp already applied
	                             or read metadata from file and apply fd->modified_xmp
	    metadata are read also if the file was modified meanwhile */
	exif = exif_read_fd(fd);
	if (!exif) return FALSE;

	success = (fd->change->dest) ? exif_write_sidecar(exif, fd->change->dest) : exif_write(exif); /* write modified metadata */
	exif_free_fd(fd, exif);

	if (fd->change->dest)
		/* this will create a FileData for the sidecar and link it to the main file
		   (we can't wait until the sidecar is discovered by directory scanning because
		    exif_read_fd is called before that and it would read the main file only and
		    store the metadata in the cache)
		*/
		/**
		@FIXME this does not catch new sidecars created by independent external programs
		*/
		file_data_unref(file_data_new_group(fd->change->dest));

	if (success) metadata_legacy_delete(fd, fd->change->dest);
	return success;
}

gint metadata_queue_length()
{
	return g_list_length(metadata_write_queue);
}

gboolean metadata_write_revert(FileData *fd, const gchar *key)
{
	if (!fd->modified_xmp) return FALSE;

	g_hash_table_remove(fd->modified_xmp, key);

	if (g_hash_table_size(fd->modified_xmp) == 0)
		{
		metadata_write_queue_remove(fd);
		}
	else
		{
		/* reread the metadata to restore the original value */
		file_data_increment_version(fd);
		file_data_send_notification(fd, NOTIFY_REREAD);
		}
	return TRUE;
}

gboolean metadata_write_list(FileData *fd, const gchar *key, const GList *values)
{
	if (!fd->modified_xmp)
		{
		fd->modified_xmp = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, string_list_free);
		}
	g_hash_table_insert(fd->modified_xmp, g_strdup(key), string_list_copy(const_cast<GList *>(values)));

	metadata_cache_remove(fd, key);

	if (fd->exif)
		{
		exif_update_metadata(fd->exif, key, values);
		}
	metadata_write_queue_add(fd);
	file_data_increment_version(fd);
	file_data_send_notification(fd, NOTIFY_METADATA);

	auto metadata_check_key = [key](const gchar *k) { return strcmp(key, k) == 0; };
	if (options->metadata.sync_grouped_files && std::any_of(group_keys.cbegin(), group_keys.cend(), metadata_check_key))
		{
		GList *work = fd->sidecar_files;

		while (work)
			{
			auto sfd = static_cast<FileData *>(work->data);
			work = work->next;

			if (sfd->format_class == FORMAT_CLASS_META) continue;

			metadata_write_list(sfd, key, values);
			}
		}


	return TRUE;
}

gboolean metadata_write_string(FileData *fd, const gchar *key, const char *value)
{
	GList *list = g_list_append(nullptr, g_strdup(value));
	gboolean ret = metadata_write_list(fd, key, list);
	g_list_free_full(list, g_free);
	return ret;
}

gboolean metadata_write_int(FileData *fd, const gchar *key, guint64 value)
{
	return metadata_write_string(fd, key, std::to_string(static_cast<unsigned long long>(value)).c_str());
}

/*
 *-------------------------------------------------------------------
 * keyword / comment read/write
 *-------------------------------------------------------------------
 */

static gboolean metadata_file_write(gchar *path, const GList *keywords, const gchar *comment)
{
	SecureSaveInfo *ssi;

	ssi = secure_open(path);
	if (!ssi) return FALSE;

	secure_fprintf(ssi, "#%s comment (%s)\n\n", GQ_APPNAME, VERSION);

	secure_fprintf(ssi, "[keywords]\n");
	while (keywords && secsave_errno == SS_ERR_NONE)
		{
		auto word = static_cast<const gchar *>(keywords->data);
		keywords = keywords->next;

		secure_fprintf(ssi, "%s\n", word);
		}
	secure_fputc(ssi, '\n');

	secure_fprintf(ssi, "[comment]\n");
	secure_fprintf(ssi, "%s\n", (comment) ? comment : "");

	secure_fprintf(ssi, "#end\n");

	return (secure_close(ssi) == 0);
}

static gboolean metadata_legacy_write(FileData *fd)
{
	gboolean success = FALSE;
	gchar *metadata_pathl;
	gpointer keywords;
	gpointer comment_l;
	gboolean have_keywords;
	gboolean have_comment;
	const gchar *comment;
	GList *orig_keywords = nullptr;
	gchar *orig_comment = nullptr;

	g_assert(fd->change && fd->change->dest);

	DEBUG_1("Saving comment: %s", fd->change->dest);

	if (!fd->modified_xmp) return TRUE;

	metadata_pathl = path_from_utf8(fd->change->dest);

	have_keywords = g_hash_table_lookup_extended(fd->modified_xmp, KEYWORD_KEY, nullptr, &keywords);
	have_comment = g_hash_table_lookup_extended(fd->modified_xmp, COMMENT_KEY, nullptr, &comment_l);
	comment = static_cast<const gchar *>((have_comment && comment_l) ? (static_cast<GList *>(comment_l))->data : nullptr);

	if (!have_keywords || !have_comment) metadata_file_read(metadata_pathl, &orig_keywords, &orig_comment);

	success = metadata_file_write(metadata_pathl,
				      have_keywords ? static_cast<GList *>(keywords) : orig_keywords,
				      have_comment ? comment : orig_comment);

	g_free(metadata_pathl);
	g_free(orig_comment);
	g_list_free_full(orig_keywords, g_free);

	return success;
}

static gboolean metadata_file_read(gchar *path, GList **keywords, gchar **comment)
{
	FILE *f;
	gchar s_buf[1024];
	MetadataKey key = MK_NONE;
	GList *list = nullptr;
	GString *comment_build = nullptr;

	f = fopen(path, "r");
	if (!f) return FALSE;

	while (fgets(s_buf, sizeof(s_buf), f))
		{
		gchar *ptr = s_buf;

		if (*ptr == '#') continue;
		if (*ptr == '[' && key != MK_COMMENT)
			{
			gchar *keystr = ++ptr;

			key = MK_NONE;
			while (*ptr != ']' && *ptr != '\n' && *ptr != '\0') ptr++;

			if (*ptr == ']')
				{
				*ptr = '\0';
				if (g_ascii_strcasecmp(keystr, "keywords") == 0)
					key = MK_KEYWORDS;
				else if (g_ascii_strcasecmp(keystr, "comment") == 0)
					key = MK_COMMENT;
				}
			continue;
			}

		switch (key)
			{
			case MK_NONE:
				break;
			case MK_KEYWORDS:
				{
				while (*ptr != '\n' && *ptr != '\0') ptr++;
				*ptr = '\0';
				if (s_buf[0] != '\0')
					{
					gchar *kw = utf8_validate_or_convert(s_buf);

					list = g_list_prepend(list, kw);
					}
				}
				break;
			case MK_COMMENT:
				if (!comment_build) comment_build = g_string_new("");
				g_string_append(comment_build, s_buf);
				break;
			}
		}

	fclose(f);

	if (keywords)
		{
		*keywords = g_list_reverse(list);
		}
	else
		{
		g_list_free_full(list, g_free);
		}

	if (comment_build)
		{
		if (comment)
			{
			gint len;
			gchar *ptr = comment_build->str;

			/* strip leading and trailing newlines */
			while (*ptr == '\n') ptr++;
			len = strlen(ptr);
			while (len > 0 && ptr[len - 1] == '\n') len--;
			if (ptr[len] == '\n') len++; /* keep the last one */
			if (len > 0)
				{
				gchar *text = g_strndup(ptr, len);

				*comment = utf8_validate_or_convert(text);
				g_free(text);
				}
			}
		g_string_free(comment_build, TRUE);
		}

	return TRUE;
}

static void metadata_legacy_delete(FileData *fd, const gchar *except)
{
	gchar *metadata_path;
	gchar *metadata_pathl;
	if (!fd) return;

	metadata_path = cache_find_location(CACHE_TYPE_METADATA, fd->path);
	if (metadata_path && (!except || strcmp(metadata_path, except) != 0))
		{
		metadata_pathl = path_from_utf8(metadata_path);
		unlink(metadata_pathl);
		g_free(metadata_pathl);
		g_free(metadata_path);
		}

#if HAVE_EXIV2
	/* without exiv2: do not delete xmp metadata because we are not able to convert it,
	   just ignore it */
	metadata_path = cache_find_location(CACHE_TYPE_XMP_METADATA, fd->path);
	if (metadata_path && (!except || strcmp(metadata_path, except) != 0))
		{
		metadata_pathl = path_from_utf8(metadata_path);
		unlink(metadata_pathl);
		g_free(metadata_pathl);
		g_free(metadata_path);
		}
#endif
}

static gboolean metadata_legacy_read(FileData *fd, GList **keywords, gchar **comment)
{
	gchar *metadata_path;
	gchar *metadata_pathl;
	gboolean success = FALSE;

	if (!fd) return FALSE;

	metadata_path = cache_find_location(CACHE_TYPE_METADATA, fd->path);
	if (!metadata_path) return FALSE;

	metadata_pathl = path_from_utf8(metadata_path);

	success = metadata_file_read(metadata_pathl, keywords, comment);

	g_free(metadata_pathl);
	g_free(metadata_path);

	return success;
}

static GList *remove_duplicate_strings_from_list(GList *list)
{
	GList *work = list;
	GHashTable *hashtable = g_hash_table_new(g_str_hash, g_str_equal);
	GList *newlist = nullptr;

	while (work)
		{
		auto key = static_cast<gchar *>(work->data);

		if (g_hash_table_lookup(hashtable, key) == nullptr)
			{
			g_hash_table_insert(hashtable, key, GINT_TO_POINTER(1));
			newlist = g_list_prepend(newlist, key);
			}
		work = work->next;
		}

	g_hash_table_destroy(hashtable);
	g_list_free(list);

	return g_list_reverse(newlist);
}

GList *metadata_read_list(FileData *fd, const gchar *key, MetadataFormat format)
{
	ExifData *exif;
	GList *list = nullptr;
	const GList *cache_values;
	if (!fd) return nullptr;

	/* unwritten data override everything */
	if (fd->modified_xmp && format == METADATA_PLAIN)
		{
		list = static_cast<GList *>(g_hash_table_lookup(fd->modified_xmp, key));
		if (list) return string_list_copy(list);
		}


	if (format == METADATA_PLAIN && strcmp(key, KEYWORD_KEY) == 0
	    && (cache_values = metadata_cache_get(fd, key)))
		{
		return string_list_copy(cache_values);
		}

	/*
	    Legacy metadata file is the primary source if it exists.
	    Merging the lists does not make much sense, because the existence of
	    legacy metadata file indicates that the other metadata sources are not
	    writable and thus it would not be possible to delete the keywords
	    that comes from the image file.
	*/
	if (strcmp(key, KEYWORD_KEY) == 0)
		{
		if (metadata_legacy_read(fd, &list, nullptr))
			{
			if (format == METADATA_PLAIN)
				{
				metadata_cache_update(fd, key, list);
				}
			return list;
			}
		}
	else if (strcmp(key, COMMENT_KEY) == 0)
		{
		gchar *comment = nullptr;
	        if (metadata_legacy_read(fd, nullptr, &comment)) return g_list_append(nullptr, comment);
	        }
	else if (strncmp(key, "file.", 5) == 0)
		{
	        return g_list_append(nullptr, metadata_file_info(fd, key, format));
		}
#if HAVE_LUA
	else if (strncmp(key, "lua.", 4) == 0)
		{
		return g_list_append(nullptr, metadata_lua_info(fd, key, format));
		}
#endif

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

gdouble metadata_read_GPS_coord(FileData *fd, const gchar *key, gdouble fallback)
{
	gdouble coord;
	gchar *endptr;
	gdouble deg;
	gdouble min;
	gdouble sec;
	gboolean ok = FALSE;
	gchar *string = metadata_read_string(fd, key, METADATA_PLAIN);
	if (!string) return fallback;

	deg = g_ascii_strtod(string, &endptr);
	if (*endptr == ',')
		{
		min = g_ascii_strtod(endptr + 1, &endptr);
		if (*endptr == ',')
			sec = g_ascii_strtod(endptr + 1, &endptr);
		else
			sec = 0.0;


		if (*endptr == 'S' || *endptr == 'W' || *endptr == 'N' || *endptr == 'E')
			{
			coord = deg + min /60.0 + sec / 3600.0;
			ok = TRUE;
			if (*endptr == 'S' || *endptr == 'W') coord = -coord;
			}
		}

	if (!ok)
		{
		coord = fallback;
		log_printf("unable to parse GPS coordinate '%s'\n", string);
		}

	g_free(string);
	return coord;
}

gdouble metadata_read_GPS_direction(FileData *fd, const gchar *key, gdouble fallback)
{
	gchar *endptr;
	gdouble deg;
	gboolean ok = FALSE;
	gchar *string = metadata_read_string(fd, key, METADATA_PLAIN);
	if (!string) return fallback;

	DEBUG_3("GPS_direction: %s\n", string);
	deg = g_ascii_strtod(string, &endptr);

	/* Expected text string is of the format e.g.:
	 * 18000/100
	 */
	if (*endptr == '/')
		{
		deg = deg/100;
		ok = TRUE;
		}

	if (!ok)
		{
		deg = fallback;
		log_printf("unable to parse GPS direction '%s: %f'\n", string, deg);
		}

	g_free(string);

	return deg;
}

gboolean metadata_append_string(FileData *fd, const gchar *key, const char *value)
{
	gchar *str = metadata_read_string(fd, key, METADATA_PLAIN);

	if (!str)
		{
		return metadata_write_string(fd, key, value);
		}

	gchar *new_string = g_strconcat(str, value, NULL);
	gboolean ret = metadata_write_string(fd, key, new_string);
	g_free(str);
	g_free(new_string);
	return ret;
}

gboolean metadata_write_GPS_coord(FileData *fd, const gchar *key, gdouble value)
{
	gint deg;
	gdouble min;
	gdouble param;
	char *coordinate;
	const char *ref;
	gboolean ok = TRUE;
	char *old_locale;
	char *saved_locale;

	param = value;
	if (param < 0)
		param = -param;
	deg = param;
	min = (param * 60) - (deg * 60);
	if (g_strcmp0(key, "Xmp.exif.GPSLongitude") == 0)
		if (value < 0)
			ref = "W";
		else
			ref = "E";
	else if (g_strcmp0(key, "Xmp.exif.GPSLatitude") == 0)
		if (value < 0)
			ref = "S";
		else
			ref = "N";
	else
		{
		log_printf("unknown GPS parameter key '%s'\n", key);
		ok = FALSE;
		}

	if (ok)
		{
		/* Avoid locale problems with commas and decimal points in numbers */
		old_locale = setlocale(LC_ALL, nullptr);
		saved_locale = strdup(old_locale);
		if (saved_locale == nullptr)
			{
			return FALSE;
			}
		setlocale(LC_ALL, "C");

		coordinate = g_strdup_printf("%i,%f,%s", deg, min, ref);
		metadata_write_string(fd, key, coordinate );

		setlocale(LC_ALL, saved_locale);
		free(saved_locale);
		g_free(coordinate);
		}

	return ok;
}

gboolean metadata_append_list(FileData *fd, const gchar *key, const GList *values)
{
	GList *list = metadata_read_list(fd, key, METADATA_PLAIN);

	if (!list)
		{
		return metadata_write_list(fd, key, values);
		}

	gboolean ret;
	list = g_list_concat(list, string_list_copy(values));
	list = remove_duplicate_strings_from_list(list);

	ret = metadata_write_list(fd, key, list);
	g_list_free_full(list, g_free);
	return ret;
}
