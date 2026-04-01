/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
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

#include <config.h>

#ifdef __linux__
#define _XOPEN_SOURCE
#endif

#include <sys/stat.h>
#include <sys/types.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <glib.h>

#if HAVE_LCMS
/*** color support enabled ***/
#  if HAVE_LCMS2
#    include <lcms2.h>
#  else
#    include <lcms.h>
#  endif
#endif

#include "debug.h"
#include "exif.h"
#include "filecache.h"
#include "filedata.h"
#include "intl.h"
#include "jpeg-parser.h"

struct ExifData;
struct ExifItem;
struct FileCacheData;

static FileCacheData *exif_cache;

static void exif_release_cb(FileData *fd)
{
	exif_free(fd->exif);
	fd->exif = nullptr;
}

static void exif_init_cache()
{
	g_assert(!exif_cache);
	exif_cache = file_cache_new(exif_release_cb, 4);
}

ExifData *exif_read_fd(FileData *fd)
{
	if (!exif_cache) exif_init_cache();

	if (!fd) return nullptr;

	if (file_cache_get(exif_cache, fd)) return fd->exif;
	g_assert(fd->exif == nullptr);

	fd->exif = exif_read(fd->path, nullptr, nullptr);

	file_cache_put(exif_cache, fd, 1);
	return fd->exif;
}


void exif_free_fd(FileData *fd, ExifData *exif)
{
	if (!fd) return;
	g_assert(fd->exif == exif);
}

/* embedded icc in jpeg */

gboolean exif_jpeg_parse_color(ExifData *exif, guchar *data, guint size)
{
	guint seg_offset = 0;
	guint seg_length = 0;
	guint chunk_offset[255];
	guint chunk_length[255];
	guint chunk_count = 0;

	/* For jpeg/jfif, ICC color profile data can be in more than one segment.
	   the data is in APP2 data segments that start with "ICC_PROFILE\x00\xNN\xTT"
	   NN = segment number for data
	   TT = total number of ICC segments (TT in each ICC segment should match)
	 */

	while (jpeg_segment_find(data + seg_offset + seg_length,
				      size - seg_offset - seg_length,
				      JPEG_MARKER_APP2,
				      "ICC_PROFILE\x00", 12,
				      &seg_offset, &seg_length))
		{
		guchar chunk_num;
		guchar chunk_tot;

		if (seg_length < 14) return FALSE;

		chunk_num = data[seg_offset + 12];
		chunk_tot = data[seg_offset + 13];

		if (chunk_num == 0 || chunk_tot == 0) return FALSE;

		if (chunk_count == 0)
			{
			guint i;

			chunk_count = static_cast<guint>(chunk_tot);
			for (i = 0; i < chunk_count; i++) chunk_offset[i] = 0;
			for (i = 0; i < chunk_count; i++) chunk_length[i] = 0;
			}

		if (chunk_tot != chunk_count ||
		    chunk_num > chunk_count) return FALSE;

		chunk_num--;
		chunk_offset[chunk_num] = seg_offset + 14;
		chunk_length[chunk_num] = seg_length - 14;
		}

	if (chunk_count > 0)
		{
		guchar *cp_data;
		guint cp_length = 0;
		guint i;

		for (i = 0; i < chunk_count; i++) cp_length += chunk_length[i];
		cp_data = static_cast<guchar *>(g_malloc(cp_length));

		for (i = 0; i < chunk_count; i++)
			{
			if (chunk_offset[i] == 0)
				{
				/* error, we never saw this chunk */
				g_free(cp_data);
				return FALSE;
				}
			memcpy(cp_data, data + chunk_offset[i], chunk_length[i]);
			}
		DEBUG_1("Found embedded icc profile in jpeg");
		exif_add_jpeg_color_profile(exif, cp_data, cp_length);

		return TRUE;
		}

	return FALSE;
}
