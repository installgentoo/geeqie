/*
 * Copyright (C) 2004 John Ellis
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

#include "jpeg-parser.h"

#include <cstring>

#include "debug.h"

gboolean is_jpeg_container(const guchar *data, guint size)
{
	return data != nullptr && size >= 2
	    && data[0] == JPEG_MARKER
	    && data[1] == JPEG_MARKER_SOI;
}

gboolean jpeg_segment_find(const guchar *data, guint size,
			    guchar app_marker, const gchar *magic, guint magic_len,
			    guint *seg_offset, guint *seg_length)
{
	guchar marker = 0;
	guint offset = 0;
	guint length = 0;

	while (marker != JPEG_MARKER_EOI)
		{
		offset += length;
		length = 2;

		if (offset + 2 >= size ||
		    data[offset] != JPEG_MARKER) return FALSE;

		marker = data[offset + 1];
		if (marker != JPEG_MARKER_SOI &&
		    marker != JPEG_MARKER_EOI)
			{
			if (offset + 4 >= size) return FALSE;
			length += (static_cast<guint>(data[offset + 2]) << 8) + data[offset + 3];

			if (marker == app_marker &&
			    offset + length < size &&
			    length >= 4 + magic_len &&
			    memcmp(data + offset + 4, magic, magic_len) == 0)
				{
				*seg_offset = offset + 4;
				*seg_length = length - 4;
				return TRUE;
				}
			}
		}
	return FALSE;
}
