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

#include "filefilter.h"

#include <cstring>
#include <algorithm>
#include <functional>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <config.h>

#include "debug.h"
#include "options.h"
#include "rcfile.h"
#include "ui-fileops.h"

/*
 *-----------------------------------------------------------------------------
 * file filtering
 *-----------------------------------------------------------------------------
 */

static GList *filter_list = nullptr;
static GList *extension_list = nullptr;

static GList *file_class_extension_list[FILE_FORMAT_CLASSES];

/* The lists above as a lookup table: lowercased extension -> EXTENSION_LISTED | a bit per class listing it.
 * An extension matches as a plain case-insensitive suffix of the name, not necessarily after a dot, so a lookup
 * tries every distinct extension length. */
static GHashTable *extension_table = nullptr;
static std::vector<gsize> extension_lengths; /**< distinct, longest first */
constexpr guint EXTENSION_LISTED = 1u << 31;

static FilterEntry *filter_entry_new(const gchar *key, const gchar *description,
				     const gchar *extensions, FileFormatClass file_class,
				     gboolean, gboolean, gboolean enabled)
{
	FilterEntry *fe;

	fe = g_new0(FilterEntry, 1);
	fe->key = g_strdup(key);
	fe->description = g_strdup(description);
	fe->extensions = g_strdup(extensions);
	fe->enabled = enabled;
	fe->file_class = file_class;

	return fe;
}

static void filter_entry_free(FilterEntry *fe)
{
	if (!fe) return;

	g_free(fe->key);
	g_free(fe->description);
	g_free(fe->extensions);
	g_free(fe);
}

GList *filter_get_list()
{
	return filter_list;
}

void filter_remove_entry(FilterEntry *fe)
{
	if (!g_list_find(filter_list, fe)) return;

	filter_list = g_list_remove(filter_list, fe);
	filter_entry_free(fe);
}

static FilterEntry *filter_get_by_key(const gchar *key)
{
	if (!key) return nullptr;

	const auto filter_entry_compare_key = [](gconstpointer data, gconstpointer user_data)
	{
		return g_strcmp0(static_cast<const FilterEntry *>(data)->key, static_cast<const gchar *>(user_data));
	};

	GList *work = g_list_find_custom(filter_list, key, filter_entry_compare_key);
	return work ? static_cast<FilterEntry *>(work->data) : nullptr;
}

static gboolean filter_key_exists(const gchar *key)
{
	return (filter_get_by_key(key) != nullptr);
}

static void filter_add(const gchar *key, const gchar *description, const gchar *extensions, FileFormatClass file_class, gboolean, gboolean, gboolean enabled)
{
	filter_list = g_list_append(filter_list, filter_entry_new(key, description, extensions, file_class, FALSE, FALSE, enabled));
}

void filter_add_unique(const gchar *description, const gchar *extensions, FileFormatClass file_class, gboolean, gboolean, gboolean enabled)
{
	gchar *key;
	guint n;

	key = g_strdup("user0");
	n = 1;
	while (filter_key_exists(key))
		{
		g_free(key);
		if (n > 999) return;
		key = g_strdup_printf("user%d", n);
		n++;
		}

	filter_add(key, description, extensions, file_class, FALSE, FALSE, enabled);
	g_free(key);
}

static void filter_add_if_missing(const gchar *key, const gchar *description, const gchar *extensions, FileFormatClass file_class, gboolean, gboolean, gboolean enabled)
{
	if (!key) return;

	FilterEntry *fe = filter_get_by_key(key);
	if (fe)
		{
		if (fe->file_class == FORMAT_CLASS_UNKNOWN)
			fe->file_class = file_class;	/* for compatibility */

		return;
		}

	filter_add(key, description, extensions, file_class, FALSE, FALSE, enabled);
}

void filter_reset()
{
	g_list_free_full(filter_list, reinterpret_cast<GDestroyNotify>(filter_entry_free));
	filter_list = nullptr;
}

void filter_add_defaults()
{
	/* formats supported by custom loaders */
	filter_add_if_missing("dds", "DirectDraw Surface", ".dds", FORMAT_CLASS_IMAGE, FALSE, FALSE, TRUE);
#if HAVE_PDF
	filter_add_if_missing("pdf", "Portable Document Format", ".pdf", FORMAT_CLASS_DOCUMENT, FALSE, FALSE, TRUE);
#endif
#if HAVE_HEIF
	filter_add_if_missing("heif/avif", "HEIF/AVIF Image", ".heif;.heic;.avif", FORMAT_CLASS_IMAGE, FALSE, TRUE, TRUE);
#endif
#if HAVE_WEBP
	filter_add_if_missing("webp", "WebP Format", ".webp", FORMAT_CLASS_IMAGE, TRUE, FALSE, TRUE);
#endif
#if HAVE_DJVU
	filter_add_if_missing("djvu", "DjVu Format", ".djvu;.djv", FORMAT_CLASS_DOCUMENT, FALSE, FALSE, TRUE);
#endif
#if HAVE_JPEGXL
	filter_add_if_missing("jxl", "JXL", ".jxl", FORMAT_CLASS_IMAGE, FALSE, TRUE, TRUE);
#endif
#if HAVE_J2K
	filter_add_if_missing("jp2", "JPEG 2000", ".jp2", FORMAT_CLASS_IMAGE, FALSE, FALSE, TRUE);
#endif
	filter_add_if_missing("scr", "ZX Spectrum screen Format", ".scr", FORMAT_CLASS_IMAGE, FALSE, FALSE, TRUE);
	filter_add_if_missing("psd", "Adobe Photoshop Document", ".psd", FORMAT_CLASS_IMAGE, FALSE, FALSE, TRUE);
	filter_add_if_missing("apng", "Animated Portable Network Graphic", ".apng", FORMAT_CLASS_IMAGE, FALSE, FALSE, TRUE);

	/* formats supported by gdk-pixbuf */
	GSList *list = gdk_pixbuf_get_formats();
	for (GSList *work = list; work; work = work->next)
		{
		auto *format = static_cast<GdkPixbufFormat *>(work->data);

		g_autofree gchar *name = gdk_pixbuf_format_get_name(format);
		if (strcmp(name, "Digital camera RAW") == 0)
			{
			DEBUG_1("Skipped '%s' from loader", name);
			continue;
			}

		g_autofree gchar *desc = gdk_pixbuf_format_get_description(format);

		g_autoptr(GString) filter = g_string_new(nullptr);
		g_auto(GStrv) extensions = gdk_pixbuf_format_get_extensions(format);

		const guint extensions_count = g_strv_length(extensions);
		for (guint i = 0; i < extensions_count; i++)
			{
			if (filter->len > 0)
				{
				filter = g_string_append_c(filter, ';');
				}
			g_string_append_printf(filter, ".%s", extensions[i]);
			}

		DEBUG_1("loader reported [%s] [%s] [%s]", name, desc, filter->str);

		filter_add_if_missing(name, desc, filter->str, FORMAT_CLASS_IMAGE, TRUE, FALSE, TRUE);
		}
	g_slist_free(list);

	/* add defaults even if gdk-pixbuf does not have them, but disabled */
	filter_add_if_missing("jpeg", "JPEG group", ".jpg;.jpeg;.jpe", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("png", "Portable Network Graphic", ".png", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("tiff", "Tiff", ".tif;.tiff", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("pnm", "Packed Pixel formats", ".pbm;.pgm;.pnm;.ppm", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("gif", "Graphics Interchange Format", ".gif", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("xbm", "X bitmap", ".xbm", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("xpm", "X pixmap", ".xpm", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("bmp", "Bitmap", ".bmp", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("ico", "Icon file", ".ico;.cur", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("ras", "Raster", ".ras", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("svg", "Scalable Vector Graphics", ".svg", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);

	/* special formats for stereo */
	filter_add_if_missing("jps", "Stereo side-by-side jpeg", ".jps", FORMAT_CLASS_IMAGE, TRUE, FALSE, TRUE);
	filter_add_if_missing("mpo", "Stereo multi-image jpeg", ".mpo", FORMAT_CLASS_IMAGE, FALSE, TRUE, TRUE);


	/* video files */
	filter_add_if_missing("mp4", "MP4 video file", ".mp4;.m4v;.3gp;.3g2", FORMAT_CLASS_VIDEO, FALSE, FALSE, TRUE);
	filter_add_if_missing("3gp", "3GP video file", ".3gp;.3g2", FORMAT_CLASS_VIDEO, FALSE, FALSE, TRUE);
	filter_add_if_missing("mov", "MOV video file", ".mov;.qt", FORMAT_CLASS_VIDEO, FALSE, FALSE, TRUE);
	filter_add_if_missing("avi", "AVI video file", ".avi", FORMAT_CLASS_VIDEO, FALSE, FALSE, TRUE);
	filter_add_if_missing("mpg", "MPG video file", ".mpg;.mpeg;.mts;.m2ts", FORMAT_CLASS_VIDEO, FALSE, FALSE, TRUE);
	filter_add_if_missing("mkv", "Matroska video file", ".mkv;.webm", FORMAT_CLASS_VIDEO, FALSE, FALSE, TRUE);
	filter_add_if_missing("wmv", "Windows Media Video file", ".wmv;.asf", FORMAT_CLASS_VIDEO, FALSE, FALSE, TRUE);
	filter_add_if_missing("flv", "Flash Video file", ".flv", FORMAT_CLASS_VIDEO, FALSE, FALSE, TRUE);
}

GList *filter_to_list(const gchar *extensions)
{
	GList *list = nullptr;
	const gchar *p;

	if (!extensions) return nullptr;

	p = extensions;
	while (*p != '\0')
		{
		const gchar *b;
		gchar *ext;
		gint file_class = -1;
		guint l = 0;

		b = p;
		while (*p != '\0' && *p != ';')
			{
			p++;
			l++;
			}

		ext = g_strndup(b, l);

		if (g_ascii_strcasecmp(ext, "%image") == 0) file_class = FORMAT_CLASS_IMAGE;
		else if (g_ascii_strcasecmp(ext, "%unknown") == 0) file_class = FORMAT_CLASS_UNKNOWN;

		if (file_class == -1)
			{
			list = g_list_append(list, ext);
			}
		else
			{
			list = g_list_concat(list, string_list_copy(file_class_extension_list[file_class]));
			g_free(ext);
			}

		if (*p == ';') p++;
		}

	return list;
}

static gint filter_sort_ext_len_cb(gconstpointer a, gconstpointer b)
{
	auto sa = static_cast<const gchar *>(a);
	auto sb = static_cast<const gchar *>(b);

	gint len_a = strlen(sa);
	gint len_b = strlen(sb);

	if (len_a > len_b) return -1;
	if (len_a < len_b) return 1;
	return 0;
}


void filter_rebuild()
{
	GList *work;
	guint i;

	g_list_free_full(extension_list, g_free);
	extension_list = nullptr;

	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		g_list_free_full(file_class_extension_list[i], g_free);
		file_class_extension_list[i] = nullptr;
		}

	work = filter_list;
	while (work)
		{
		FilterEntry *fe;

		fe = static_cast<FilterEntry *>(work->data);
		work = work->next;

		if (fe->enabled)
			{
			GList *ext;

			ext = filter_to_list(fe->extensions);
			if (ext) extension_list = g_list_concat(extension_list, ext);

			if (fe->file_class < FILE_FORMAT_CLASSES)
				{
				ext = filter_to_list(fe->extensions);
				if (ext) file_class_extension_list[fe->file_class] = g_list_concat(file_class_extension_list[fe->file_class], ext);
				}
			else
				{
				log_printf("WARNING: invalid file class %d\n", fe->file_class);
				}
			}
		}

	/* make sure registered_extension_from_path finds the longer match first */
	extension_list = g_list_sort(extension_list, filter_sort_ext_len_cb);

	if (extension_table) g_hash_table_destroy(extension_table);
	extension_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);
	extension_lengths.clear();

	const auto add = [](const gchar *ext, guint bits)
		{
		gchar *key = g_ascii_strdown(ext, -1);
		const guint old = GPOINTER_TO_UINT(g_hash_table_lookup(extension_table, key));
		g_hash_table_insert(extension_table, key, GUINT_TO_POINTER(old | EXTENSION_LISTED | bits));

		const gsize len = strlen(ext);
		if (std::find(extension_lengths.begin(), extension_lengths.end(), len) == extension_lengths.end())
			{
			extension_lengths.push_back(len);
			}
		};

	for (work = extension_list; work; work = work->next) add(static_cast<const gchar *>(work->data), 0);
	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		for (work = file_class_extension_list[i]; work; work = work->next) add(static_cast<const gchar *>(work->data), 1u << i);
		}
	std::sort(extension_lengths.begin(), extension_lengths.end(), std::greater<>());
}

/* Returns the longest listed extension the name ends with, or nullptr, and ORs the class bits of every listed
 * extension the name ends with into *classes. */
static const gchar *filter_extension_lookup(const gchar *name, guint *classes)
{
	*classes = 0;
	if (!extension_table) return nullptr;

	const gsize name_len = strlen(name);
	const gchar *longest = nullptr;
	gchar stack_key[32];

	for (const gsize len : extension_lengths)
		{
		if (len > name_len) continue;

		const gchar *suffix = name + name_len - len;
		gchar *key = len < sizeof(stack_key) ? stack_key : static_cast<gchar *>(g_malloc(len + 1));
		for (gsize i = 0; i < len; i++) key[i] = g_ascii_tolower(suffix[i]);
		key[len] = '\0';

		const guint bits = GPOINTER_TO_UINT(g_hash_table_lookup(extension_table, key));
		if (key != stack_key) g_free(key);

		if (!(bits & EXTENSION_LISTED)) continue;
		if (!longest) longest = suffix;
		*classes |= bits & ~EXTENSION_LISTED;
		}

	return longest;
}

const gchar *registered_extension_from_path(const gchar *name)
{
	guint classes;
	return filter_extension_lookup(name, &classes);
}

gboolean filter_name_exists(const gchar *name)
{
	if (!extension_list || options->file_filter.disable) return TRUE;

	guint classes;
	return !!filter_extension_lookup(name, &classes);
}

static FileFormatClass filter_class_from_bits(guint classes)
{
	for (const FileFormatClass file_class : {FORMAT_CLASS_IMAGE, FORMAT_CLASS_VIDEO, FORMAT_CLASS_DOCUMENT})
		{
		if (classes & (1u << file_class)) return file_class;
		}
	return FORMAT_CLASS_UNKNOWN;
}

gboolean filter_file_class(const gchar *name, FileFormatClass file_class)
{
	if (file_class >= FILE_FORMAT_CLASSES)
		{
		log_printf("WARNING: invalid file class %d\n", file_class);
		return FALSE;
		}

	guint classes;
	filter_extension_lookup(name, &classes);
	return (classes & (1u << file_class)) != 0;
}

FileFormatClass filter_file_get_class(const gchar *name)
{
	guint classes;
	filter_extension_lookup(name, &classes);
	return filter_class_from_bits(classes);
}

const gchar *registered_extension_and_class(const gchar *name, FileFormatClass *file_class)
{
	guint classes;
	const gchar *extension = filter_extension_lookup(name, &classes);
	*file_class = filter_class_from_bits(classes);
	return extension;
}

void filter_write_list(GString *outstr, gint indent)
{
	GList *work;

	WRITE_NL(); WRITE_STRING("<filter>");
	indent++;

	work = filter_list;
	while (work)
		{
		auto fe = static_cast<FilterEntry *>(work->data);
		work = work->next;

		WRITE_NL(); WRITE_STRING("<file_type ");
		WRITE_CHAR(*fe, key);
		WRITE_BOOL(*fe, enabled);
		WRITE_CHAR(*fe, extensions);
		WRITE_CHAR(*fe, description);
		write_char_option(outstr, indent, "file_class", format_class_list[fe->file_class]);
		WRITE_STRING("/>");
		}
	indent--;
	WRITE_NL(); WRITE_STRING("</filter>");
}

void filter_load_file_type(const gchar **attribute_names, const gchar **attribute_values)
{
	FilterEntry fe;
	FilterEntry *old_fe;
	memset(&fe, 0, sizeof(fe));
	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR(fe, key)) continue;
		if (READ_BOOL(fe, enabled)) continue;
		if (READ_CHAR(fe, extensions)) continue;
		if (READ_CHAR(fe, description)) continue;

		if (g_strcmp0(option, "file_class") == 0)
			{
			gboolean found = FALSE;
			for (int i = 0; i < FILE_FORMAT_CLASSES; i++)
				{
				if (g_strcmp0(value, format_class_list[i]) == 0)
					{
					fe.file_class = static_cast<FileFormatClass>(i);
					found = TRUE;
					break;
					}
				}
			if (!found) fe.file_class = FORMAT_CLASS_UNKNOWN;
			continue;
			}

		log_printf("unknown attribute %s = %s\n", option, value);
		}
	if (fe.file_class >= FILE_FORMAT_CLASSES) fe.file_class = FORMAT_CLASS_UNKNOWN;

	if (fe.key && fe.key[0] != 0)
		{
		old_fe = filter_get_by_key(fe.key);

		if (old_fe != nullptr) filter_remove_entry(old_fe);
		filter_add(fe.key, fe.description, fe.extensions, fe.file_class, FALSE, FALSE, fe.enabled);
		}
	g_free(fe.key);
	g_free(fe.extensions);
	g_free(fe.description);
}
