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

#ifndef CACHE_H
#define CACHE_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

struct ImageSimilarityData;
class FileData;

struct CacheData
{
	gint width;
	gint height;
	guchar md5sum[16];
	ImageSimilarityData *sim;

	gboolean dimensions;
	gboolean have_md5sum;
	gboolean similarity;
};


CacheData *cache_sim_data_new();
void cache_sim_data_free(CacheData *cd);

void cache_sim_data_set_dimensions(CacheData *cd, gint w, gint h);
void cache_sim_data_set_md5sum(CacheData *cd, const guchar digest[16]);
void cache_sim_data_set_similarity(CacheData *cd, ImageSimilarityData *sd);
gint cache_sim_data_filled(ImageSimilarityData *sd);

/* The similarity database; all of these are safe to call from any thread. */
CacheData *cache_sim_data_load(FileData *fd); /**< nullptr if there is no row or the file changed since it was written */
gboolean cache_sim_data_save(FileData *fd, CacheData *cd); /**< replaces the whole row */
gboolean cache_sim_data_use_cache(FileData *fd);
void cache_sim_moved(const gchar *source, const gchar *dest);
void cache_sim_removed(const gchar *path);
gint cache_sim_clean(); /**< drops rows for files that are gone or changed; returns how many; blocking */

/* The video functions block on ffprobe/ffmpeg and touch nothing but fd->path, so they are safe off the main thread. */

struct CacheVideoProbe
{
	gint width; /**< as displayed, rotation applied */
	gint height;
	gdouble duration; /**< seconds, 0 if unknown */
};

gboolean cache_video_probe(FileData *fd, CacheVideoProbe *probe); /**< FALSE if the size is unknown */

/** Contact sheet of evenly spaced frames, the image a video's similarity data is computed from. */
GdkPixbuf *cache_sim_video_pixbuf(FileData *fd, gdouble duration);

const gchar *get_sim_cache_path();
const gchar *get_thumbnails_standard_cache_dir();

#endif
