/*
 * Copyright (C) 2004 John Ellis
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

#ifndef SIMILAR_H
#define SIMILAR_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include "typedefs.h"

/* avg_r/g/b must stay adjacent: the compare treats them as one 3072-byte block. */
struct ImageSimilarityData
{
	guint8 avg_r[1024];
	guint8 avg_g[1024];
	guint8 avg_b[1024];

	gboolean filled;

	guint8 (*transforms)[3 * 1024]; /**< the 7 non-identity isometries of the grid, see image_sim_needle_prepare */
};


ImageSimilarityData *image_sim_new();
void image_sim_free(ImageSimilarityData *sd);

void image_sim_fill_data(ImageSimilarityData *sd, GdkPixbuf *pixbuf);
ImageSimilarityData *image_sim_new_from_pixbuf(GdkPixbuf *pixbuf);

/**
 * Whether a pair may also match flipped or rotated. Never for a video: its grid is a contact sheet laid out
 * in time, so an isometry reorders the frames and only gives unrelated videos more chances to match.
 */
gboolean image_sim_isometries_allowed(FileFormatClass a, FileFormatClass b);

gdouble image_sim_compare(ImageSimilarityData *a, ImageSimilarityData *b, gboolean isometries);
gdouble image_sim_compare_fast(ImageSimilarityData *a, ImageSimilarityData *b, gdouble min, gboolean isometries);

/**
 * Rotation-invariant comparison needs the 8 isometries of the second argument. Preparing them
 * once is what makes comparing one needle against a whole list cheap; an unprepared needle
 * has them built on the stack for every call.
 */
void image_sim_needle_prepare(ImageSimilarityData *sd);
void image_sim_needle_release(ImageSimilarityData *sd);


void image_sim_alternate_set(gboolean enable);
void image_sim_alternate_processing(ImageSimilarityData *sd);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
