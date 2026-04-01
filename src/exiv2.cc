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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <config.h>

#include <exiv2/exiv2.hpp>
#include <glib.h>
#ifdef ENABLE_NLS
#  include <libintl.h>
#endif

#include "debug.h"
#include "filedata.h"
#include "filefilter.h"
#include "misc.h"
#include "typedefs.h"
#include "ui-fileops.h"

struct ExifItem;

#if EXIV2_TEST_VERSION(0,27,0)
#define HAVE_EXIV2_ERROR_CODE
#endif

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
	Exiv2::ExifData::const_iterator exifIter; /* for exif_get_next_item */
	Exiv2::IptcData::const_iterator iptcIter; /* for exif_get_next_item */
	Exiv2::XmpData::const_iterator xmpIter; /* for exif_get_next_item */

	virtual ~ExifData() = default;

	virtual void writeMetadata(gchar * = nullptr)
	{
		g_critical("Unsupported method of writing metadata");
	}

	virtual ExifData *original()
	{
		return nullptr;
	}

	virtual Exiv2::Image *image() = 0;

	virtual Exiv2::ExifData &exifData() = 0;

	virtual Exiv2::IptcData &iptcData() = 0;

	virtual Exiv2::XmpData &xmpData() = 0;

	virtual void add_jpeg_color_profile(unsigned char *cp_data, guint cp_length) = 0;

	virtual guchar *get_jpeg_color_profile(guint *data_len) = 0;

	virtual std::string image_comment() const = 0;

	virtual void set_image_comment(const std::string& comment) = 0;
};

// This allows read-only access to the original metadata
struct ExifDataOriginal : public ExifData
{
protected:
	Exiv2::Image::AutoPtr image_;

	/* the icc profile in jpeg is not technically exif - store it here */
	unsigned char *cp_data_;
	guint cp_length_;
	gboolean valid_;
	gchar *pathl_;

	Exiv2::ExifData emptyExifData_;
	Exiv2::IptcData emptyIptcData_;
	Exiv2::XmpData emptyXmpData_;

public:
	ExifDataOriginal(Exiv2::Image::AutoPtr image)
	{
		cp_data_ = nullptr;
		cp_length_ = 0;
        	image_ = std::move(image);
		valid_ = TRUE;
	}

	ExifDataOriginal(gchar *path)
	{
		cp_data_ = nullptr;
		cp_length_ = 0;
		valid_ = TRUE;

		pathl_ = path_from_utf8(path);
		try
			{
			image_ = Exiv2::ImageFactory::open(pathl_);
			image_->readMetadata();

			if (image_->mimeType() == "application/rdf+xml")
				{
				//Exiv2 sidecar converts xmp to exif and iptc, we don't want it.
				image_->clearExifData();
				image_->clearIptcData();
				}

			if (image_->mimeType() == "image/jpeg")
				{
				/* try to get jpeg color profile */
				Exiv2::BasicIo &io = image_->io();
				gint open = io.isopen();
				if (!open) io.open();
				if (io.isopen())
					{
					auto mapped = static_cast<unsigned char*>(io.mmap());
					if (mapped) exif_jpeg_parse_color(this, mapped, io.size());
					io.munmap();
					}
				if (!open) io.close();
				}
			}
		catch (Exiv2::AnyError& e)
			{
			valid_ = FALSE;
			}
	}

	~ExifDataOriginal() override
	{
		g_free(cp_data_);
		g_free(pathl_);
	}

	Exiv2::Image *image() override
	{
		if (!valid_) return nullptr;
		return image_.get();
	}

	Exiv2::ExifData &exifData () override
	{
		if (!valid_) return emptyExifData_;
		return image_->exifData();
	}

	Exiv2::IptcData &iptcData () override
	{
		if (!valid_) return emptyIptcData_;
		return image_->iptcData();
	}

	Exiv2::XmpData &xmpData () override
	{
		if (!valid_) return emptyXmpData_;
		return image_->xmpData();
	}

	void add_jpeg_color_profile(unsigned char *cp_data, guint cp_length) override
	{
		g_free(cp_data_);
		cp_data_ = cp_data;
		cp_length_ = cp_length;
	}

	guchar *get_jpeg_color_profile(guint *data_len) override
	{
		if (cp_data_)
		{
			if (data_len) *data_len = cp_length_;
#if GLIB_CHECK_VERSION(2,68,0)
			return static_cast<unsigned char *>(g_memdup2(cp_data_, cp_length_));
#else
			return static_cast<unsigned char *>(g_memdup(cp_data_, cp_length_));
#endif
		}
		return nullptr;
	}

	std::string image_comment() const override
	{
		return image_.get() ? image_->comment() : "";
	}

	void set_image_comment(const std::string& comment) override
	{
		if (image_.get())
			image_->setComment(comment);
	}
};

// This allows read-write access to the metadata
struct ExifDataProcessed : public ExifData
{
protected:
	std::unique_ptr<ExifDataOriginal> imageData_;
	std::unique_ptr<ExifDataOriginal> sidecarData_;

	Exiv2::ExifData exifData_;
	Exiv2::IptcData iptcData_;
	Exiv2::XmpData xmpData_;

public:
	ExifDataProcessed(gchar *path, gchar *, GHashTable *)
	{
		imageData_ = std::make_unique<ExifDataOriginal>(path);
			{
			xmpData_ = imageData_->xmpData();
			}

		exifData_ = imageData_->exifData();
		iptcData_ = imageData_->iptcData();
		try
			{
			syncExifWithXmp(exifData_, xmpData_);
			}
		catch (...)
			{
			DEBUG_1("Exiv2: Catching bug\n");
			}
	}

	ExifData *original() override
	{
		return imageData_.get();
	}

	void writeMetadata(gchar *) override
	{
	}

	Exiv2::Image *image() override
	{
		return imageData_->image();
	}

	Exiv2::ExifData &exifData () override
	{
		return exifData_;
	}

	Exiv2::IptcData &iptcData () override
	{
		return iptcData_;
	}

	Exiv2::XmpData &xmpData () override
	{
		return xmpData_;
	}

	void add_jpeg_color_profile(unsigned char *cp_data, guint cp_length) override
	{
		imageData_->add_jpeg_color_profile(cp_data, cp_length);
	}

	guchar *get_jpeg_color_profile(guint *data_len) override
	{
		return imageData_->get_jpeg_color_profile(data_len);
	}

	std::string image_comment() const override
	{
		return imageData_->image_comment();
	}

	void set_image_comment(const std::string& comment) override
	{
		imageData_->set_image_comment(comment);
	}
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

ExifData *exif_read(gchar *path, gchar *, GHashTable *)
{
	DEBUG_1("exif read %s", path);
	try {
		return new ExifDataProcessed(path, nullptr, nullptr);
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}

}

void exif_free(ExifData *exif)
{
	if (!exif) return;
	g_assert(dynamic_cast<ExifDataProcessed *>(exif)); // this should not be called on ExifDataOriginal
	delete exif;
}

ExifItem *exif_get_item(ExifData *exif, const gchar *key)
{
	try {
		Exiv2::Metadatum *item = nullptr;
		try {
			Exiv2::ExifKey ekey(key);
			auto pos = exif->exifData().findKey(ekey);
			if (pos == exif->exifData().end()) return nullptr;
			item = &*pos;
		}
		catch (Exiv2::AnyError& e) {
			try {
				Exiv2::IptcKey ekey(key);
				auto pos = exif->iptcData().findKey(ekey);
				if (pos == exif->iptcData().end()) return nullptr;
				item = &*pos;
			}
			catch (Exiv2::AnyError& e) {
				Exiv2::XmpKey ekey(key);
				auto pos = exif->xmpData().findKey(ekey);
				if (pos == exif->xmpData().end()) return nullptr;
				item = &*pos;
			}
		}
		return reinterpret_cast<ExifItem *>(item);
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}

char *exif_item_get_data(ExifItem *item, guint *data_len)
{
	try {
		if (!item) return nullptr;
		auto md = reinterpret_cast<Exiv2::Metadatum *>(item);
		if (data_len) *data_len = md->size();
		auto data = static_cast<char *>(g_malloc(md->size()));
		auto res = md->copy(reinterpret_cast<Exiv2::byte *>(data), Exiv2::littleEndian /* should not matter */);
		g_assert(res == md->size());
		return data;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}

/*
invalidTypeId, unsignedByte, asciiString, unsignedShort,
  unsignedLong, unsignedRational, signedByte, undefined,
  signedShort, signedLong, signedRational, string,
  date, time, comment, directory,
  xmpText, xmpAlt, xmpBag, xmpSeq,
  langAlt, lastTypeId
*/

static guint format_id_trans_tbl [] = {
	EXIF_FORMAT_UNKNOWN,
	EXIF_FORMAT_BYTE_UNSIGNED,
	EXIF_FORMAT_STRING,
	EXIF_FORMAT_SHORT_UNSIGNED,
	EXIF_FORMAT_LONG_UNSIGNED,
	EXIF_FORMAT_RATIONAL_UNSIGNED,
	EXIF_FORMAT_BYTE,
	EXIF_FORMAT_UNDEFINED,
	EXIF_FORMAT_SHORT,
	EXIF_FORMAT_LONG,
	EXIF_FORMAT_RATIONAL,
	EXIF_FORMAT_STRING,
	EXIF_FORMAT_STRING,
	EXIF_FORMAT_STRING,
	EXIF_FORMAT_UNDEFINED,
	EXIF_FORMAT_STRING,
	EXIF_FORMAT_STRING,
	EXIF_FORMAT_STRING,
	EXIF_FORMAT_STRING
	};

guint exif_item_get_format_id(ExifItem *item)
{
	try {
		if (!item) return EXIF_FORMAT_UNKNOWN;
		guint id = (reinterpret_cast<Exiv2::Metadatum *>(item))->typeId();
		if (id >= (sizeof(format_id_trans_tbl) / sizeof(format_id_trans_tbl[0])) ) return EXIF_FORMAT_UNKNOWN;
		return format_id_trans_tbl[id];
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return EXIF_FORMAT_UNKNOWN;
	}
}

void exif_add_jpeg_color_profile(ExifData *exif, unsigned char *cp_data, guint cp_length)
{
	exif->add_jpeg_color_profile(cp_data, cp_length);
}

guchar *exif_get_color_profile(ExifData *exif, guint *data_len)
{
	guchar *ret = exif->get_jpeg_color_profile(data_len);
	if (ret) return ret;

	ExifItem *prof_item = exif_get_item(exif, "Exif.Image.InterColorProfile");
	if (prof_item && exif_item_get_format_id(prof_item) == EXIF_FORMAT_UNDEFINED)
		ret = reinterpret_cast<guchar *>(exif_item_get_data(prof_item, data_len));
	return ret;
}

static gint exif_orientation_validate(gint orientation, gint fallback)
{
	if (orientation < EXIF_ORIENTATION_TOP_LEFT || orientation > EXIF_ORIENTATION_LEFT_BOTTOM) return fallback;
	return orientation;
}

gint exif_read_orientation(FileData *fd, gint fallback)
{
	ExifData *exif = exif_read_fd(fd);
	if (!exif) return fallback;

	gint orientation = fallback;

	try
		{
		const Exiv2::ExifData &ed = exif->exifData();
		const auto exif_it = ed.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
		if (exif_it != ed.end())
			{
#if EXIV2_TEST_VERSION(0,28,0)
			orientation = static_cast<gint>(exif_it->toInt64());
#else
			orientation = static_cast<gint>(exif_it->toLong());
#endif
			exif_free_fd(fd, exif);
			return exif_orientation_validate(orientation, fallback);
			}

		const Exiv2::XmpData &xd = exif->xmpData();
		const auto xmp_it = xd.findKey(Exiv2::XmpKey("Xmp.tiff.Orientation"));
		if (xmp_it != xd.end())
			{
#if EXIV2_TEST_VERSION(0,28,0)
			orientation = static_cast<gint>(xmp_it->toInt64());
#else
			orientation = static_cast<gint>(xmp_it->toLong());
#endif
			}
		}
	catch (Exiv2::AnyError& e)
		{
		debug_exception(e);
		orientation = fallback;
		}

	exif_free_fd(fd, exif);
	return exif_orientation_validate(orientation, fallback);
}

ExifColorSpaceType exif_read_colorspace(FileData *fd)
{
	ExifData *exif = exif_read_fd(fd);
	if (!exif) return EXIF_COLORSPACE_NONE;

	ExifColorSpaceType color_space = EXIF_COLORSPACE_NONE;

	try
		{
		const Exiv2::ExifData &ed = exif->exifData();

		const auto interop_it = ed.findKey(Exiv2::ExifKey("Exif.Iop.InteroperabilityIndex"));
		if (interop_it != ed.end())
			{
			const std::string interop = interop_it->toString();
			if (interop == "R98")
				{
				color_space = EXIF_COLORSPACE_SRGB;
				}
			else if (interop == "R03")
				{
				color_space = EXIF_COLORSPACE_ADOBERGB;
				}
			}

		if (color_space == EXIF_COLORSPACE_NONE)
			{
			const auto cs_it = ed.findKey(Exiv2::ExifKey("Exif.Photo.ColorSpace"));
			if (cs_it != ed.end())
				{
#if EXIV2_TEST_VERSION(0,28,0)
				const gint cs = static_cast<gint>(cs_it->toInt64());
#else
				const gint cs = static_cast<gint>(cs_it->toLong());
#endif
				if (cs == 1) color_space = EXIF_COLORSPACE_SRGB; /* EXIF 2.2 */
				else if (cs == 2) color_space = EXIF_COLORSPACE_ADOBERGB; /* non-standard */
				}
			}
		}
	catch (Exiv2::AnyError& e)
		{
		debug_exception(e);
		color_space = EXIF_COLORSPACE_NONE;
		}

	exif_free_fd(fd, exif);
	return color_space;
}

gchar *exif_get_all_exif_as_text(ExifData *exif)
{
	if (!exif) return g_strdup("");

	try {
		GString *str = g_string_new(nullptr);
		const Exiv2::ExifData &ed = exif->exifData();

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
		const Exiv2::XmpData &xd = exif->xmpData();
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
		const Exiv2::IptcData &id = exif->iptcData();
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
		std::string comment = exif->image_comment();
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

guchar *exif_get_preview(ExifData *exif, guint *data_len, gint requested_width, gint requested_height)
{
	if (!exif) return nullptr;

	if (!exif->image()) return nullptr;

	std::string const path = exif->image()->io().path();
	/* given image pathname, first do simple (and fast) file extension test */
	gboolean is_raw = filter_file_class(path.c_str(), FORMAT_CLASS_RAWIMAGE);

	if (!is_raw && requested_width == 0) return nullptr;

	try {

		Exiv2::PreviewManager pm(*exif->image());

		Exiv2::PreviewPropertiesList list = pm.getPreviewProperties();

		if (!list.empty())
			{
			Exiv2::PreviewPropertiesList::iterator pos;
			auto last = --list.end();

			if (requested_width == 0)
				{
				pos = last; // the largest
				}
			else
				{
				pos = list.begin();
				while (pos != last)
					{
					if (pos->width_ >= static_cast<uint32_t>(requested_width) &&
					    pos->height_ >= static_cast<uint32_t>(requested_height)) break;
					++pos;
					}

				// we are not interested in smaller thumbnails in normal image formats - we can use full image instead
				if (!is_raw)
					{
					if (pos->width_ < static_cast<uint32_t>(requested_width) || pos->height_ < static_cast<uint32_t>(requested_height)) return nullptr;
					}
				}

			Exiv2::PreviewImage image = pm.getPreviewImage(*pos);

			// Let's not touch data_len until we finish copy.
			// Just in case we run into OOM.
			size_t img_sz = image.size();
			auto* b = new Exiv2::byte[img_sz];
			std::copy_n(image.pData(), img_sz, b);
			*data_len = img_sz;
			return b;
			}
		return nullptr;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}

void exif_free_preview(const guchar *buf)
{
	delete[] static_cast<const Exiv2::byte*>(buf);
}
