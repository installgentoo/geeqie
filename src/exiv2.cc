/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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

#include "exif.h"

#include <cstring>
#include <memory>
#include <set>
#include <string>

#include <config.h>

#include <exiv2/exiv2.hpp>
#include <glib.h>
#ifdef ENABLE_NLS
#  include <libintl.h>
#endif

#include "debug.h"
#include "misc.h"
#include "ui-fileops.h"

#if EXIV2_TEST_VERSION(0,27,0)
#define EXV_PACKAGE "exiv2"
#endif

#if EXIV2_TEST_VERSION(0,28,0)
#define AnyError Error
#define AutoPtr UniquePtr
#endif

static void _debug_exception(const char* file,
                             int line,
                             const char* func,
                             Exiv2::AnyError& e)
{
	gchar *str = g_locale_from_utf8(e.what(), -1, nullptr, nullptr, nullptr);
	DEBUG_1("%s:%d:%s:Exiv2: %s", file, line, func, str);
	g_free(str);
}

#define debug_exception(e) _debug_exception(__FILE__, __LINE__, __func__, e)

struct ExifData
{
	Exiv2::Image::AutoPtr image;

	/* copies, so that XMP values can be folded into EXIF without touching the image */
	Exiv2::ExifData exifData;
	Exiv2::IptcData iptcData;
	Exiv2::XmpData xmpData;
};

void exif_init()
{
#ifdef EXV_ENABLE_NLS
	bind_textdomain_codeset (EXV_PACKAGE, "UTF-8");
#endif

#if !EXIV2_TEST_VERSION(0,28,3)
#ifdef EXV_ENABLE_BMFF
	Exiv2::enableBMFF(true);
#endif
#endif
}

ExifData *exif_read(const gchar *path)
{
	DEBUG_1("exif read %s", path);

	g_autofree gchar *pathl = path_from_utf8(path);
	try {
		auto exif = std::make_unique<ExifData>();
		exif->image = Exiv2::ImageFactory::open(pathl);
		exif->image->readMetadata();

		exif->exifData = exif->image->exifData();
		exif->iptcData = exif->image->iptcData();
		exif->xmpData = exif->image->xmpData();
		try
			{
			syncExifWithXmp(exif->exifData, exif->xmpData);
			}
		catch (...)
			{
			DEBUG_1("Exiv2: Catching bug\n");
			}

		return exif.release();
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}

void exif_free(ExifData *exif)
{
	delete exif;
}

gchar *exif_get_all_exif_as_text(ExifData *exif)
{
	if (!exif) return g_strdup("");

	try {
		GString *str = g_string_new(nullptr);
		const Exiv2::ExifData &ed = exif->exifData;

		for (const auto &entry : ed)
			{
			if (entry.size() > 1024) continue; /* skip large binary blobs */

			std::string val = entry.print(&ed);
			if (val.empty()) continue;
			if (val.length() > 256)
				{
				val.erase(256);
				val.append("...");
				}

			gchar *label = utf8_validate_or_convert(entry.tagLabel().c_str());
			gchar *value = utf8_validate_or_convert(val.c_str());
			g_string_append_printf(str, "%s\t%s\n", label, value);
			g_free(label);
			g_free(value);
			}

		return g_string_free(str, FALSE);
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return g_strdup("");
	}
}

gchar *exif_get_all_xmp_as_text(ExifData *exif)
{
	if (!exif) return g_strdup("");

	try {
		GString *str = g_string_new(nullptr);
		std::set<std::string> seen_labels;

		/* XMP entries first */
		const Exiv2::XmpData &xd = exif->xmpData;
		for (const auto &entry : xd)
			{
			std::string val = entry.print();
			if (val.empty()) continue;
			if (val.length() > 256)
				{
				val.erase(256);
				val.append("...");
				}

			std::string label = entry.tagLabel();
			seen_labels.insert(label);

			gchar *ulabel = utf8_validate_or_convert(label.c_str());
			gchar *value = utf8_validate_or_convert(val.c_str());
			g_string_append_printf(str, "%s\t%s\n", ulabel, value);
			g_free(ulabel);
			g_free(value);
			}

		/* IPTC entries — only if label not already covered by XMP */
		const Exiv2::IptcData &id = exif->iptcData;
		for (const auto &entry : id)
			{
			std::string label = entry.tagLabel();
			if (seen_labels.count(label)) continue;

			std::string val = entry.print();
			if (val.empty()) continue;
			if (val.length() > 256)
				{
				val.erase(256);
				val.append("...");
				}

			seen_labels.insert(label);

			gchar *ulabel = utf8_validate_or_convert(label.c_str());
			gchar *value = utf8_validate_or_convert(val.c_str());
			g_string_append_printf(str, "%s\t%s\n", ulabel, value);
			g_free(ulabel);
			g_free(value);
			}

		/* JPEG comment */
		std::string comment = exif->image ? exif->image->comment() : "";
		if (!comment.empty())
			{
			gchar *value = utf8_validate_or_convert(comment.c_str());
			g_string_append_printf(str, "JPEG comment\t%s\n", value);
			g_free(value);
			}

		return g_string_free(str, FALSE);
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return g_strdup("");
	}
}

gchar *exif_get_all_metadata_as_text(ExifData *exif)
{
	gchar *exif_text = exif_get_all_exif_as_text(exif);
	gchar *xmp_text = exif_get_all_xmp_as_text(exif);

	/* collect xmp lines into a set for dedup */
	std::set<std::string> xmp_lines;
	gchar *p = xmp_text;
	while (p && *p)
		{
		gchar *nl = strchr(p, '\n');
		if (nl)
			{
			xmp_lines.emplace(p, nl - p);
			p = nl + 1;
			}
		else
			{
			xmp_lines.emplace(p);
			break;
			}
		}

	/* build result: exif lines first (skipping any that appear in xmp), then all xmp */
	GString *str = g_string_new(nullptr);

	p = exif_text;
	while (p && *p)
		{
		gchar *nl = strchr(p, '\n');
		std::string line;
		if (nl)
			{
			line.assign(p, nl - p);
			p = nl + 1;
			}
		else
			{
			line.assign(p);
			p = nullptr;
			}

		if (!line.empty() && !xmp_lines.count(line))
			{
			g_string_append(str, line.c_str());
			g_string_append_c(str, '\n');
			}
		}

	g_string_append(str, xmp_text);

	g_free(exif_text);
	g_free(xmp_text);

	return g_string_free(str, FALSE);
}
