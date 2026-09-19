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

#ifndef CACHE_LOADER_H
#define CACHE_LOADER_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

struct CacheData;
struct CacheLoaderJob;
class FileData;
struct ImageLoader;

enum CacheDataType {
	CACHE_LOADER_NONE       = 0,
	CACHE_LOADER_DIMENSIONS = 1 << 0,
	CACHE_LOADER_MD5SUM     = 1 << 2,
	CACHE_LOADER_SIMILARITY = 1 << 3
};

struct CacheLoader {
	FileData *fd;
	CacheData *cd;

	CacheDataType todo_mask; /**< what was asked for, unchanged for the loader's life */
	CacheDataType done_mask; /**< what has been obtained so far */

	using DoneFunc = void (*)(CacheLoader *, gint, gpointer);
	DoneFunc done_func;
	gpointer done_data;

	gboolean error;

	ImageLoader *il; /**< still images decode here */
	CacheLoaderJob *job; /**< the worker computing similarity data, md5 and a video's contact sheet */
	GdkPixbuf *pixbuf; /**< the decoded image or contact sheet, owned by the job while one runs */
	guint idle_id; /**< event source id */
};


CacheLoader *cache_loader_new(FileData *fd, CacheDataType load_mask,
			      CacheLoader::DoneFunc done_func, gpointer done_data);

void cache_loader_free(CacheLoader *cl);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
