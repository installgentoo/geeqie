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

#include <sys/types.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

struct ImageSimilarityData;
class FileData;

#define GQ_CACHE_THUMB		"thumbnails"

#define GQ_CACHE_EXT_THUMB      ".png"
#define GQ_CACHE_EXT_SIM        ".sim"


enum CacheType {
	CACHE_TYPE_THUMB,
	CACHE_TYPE_SIM
};

struct CacheData
{
	gchar *path;
	gchar *uri; /**< source file; the cache file name is its md5, so this is the only way back */
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

gboolean cache_sim_data_save(CacheData *cd);
CacheData *cache_sim_data_load(const gchar *path);

void cache_sim_data_set_dimensions(CacheData *cd, gint w, gint h);
void cache_sim_data_set_md5sum(CacheData *cd, const guchar digest[16]);
void cache_sim_data_set_similarity(CacheData *cd, ImageSimilarityData *sd);
gint cache_sim_data_filled(ImageSimilarityData *sd);
CacheData *cache_sim_data_load_from_file(FileData *fd);
gboolean cache_sim_data_save_to_file(FileData *fd, CacheData *cd);
gboolean cache_sim_data_use_cache(FileData *fd);
gboolean cache_sim_file_valid(const gchar *cache_path);

/**
 * Contact sheet of evenly spaced frames, the image a video's similarity data is computed from.
 * Blocking (spawns ffprobe/ffmpeg); touches nothing but fd->path, so safe off the main thread.
 */
GdkPixbuf *cache_sim_video_pixbuf(FileData *fd);

gchar *cache_create_location(CacheType cache_type, const gchar *source);
gchar *cache_get_location(CacheType cache_type, const gchar *source);
gchar *cache_find_location(CacheType type, const gchar *source);

const gchar *get_thumbnails_cache_dir();
const gchar *get_thumbnails_standard_cache_dir();

#endif
