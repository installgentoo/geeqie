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

#include "thumb.h"

#include "debug.h"
#include "filedata.h"
#include "typedefs.h"


ThumbLoader *thumb_loader_new(gint save_width, gint display_width)
{
	return thumb_loader_std_new(save_width, display_width);
}

void thumb_loader_set_callbacks(ThumbLoader *tl,
				ThumbLoader::Func func_done,
				ThumbLoader::Func func_error,
				ThumbLoader::Func func_progress,
				gpointer data)
{
	thumb_loader_std_set_callbacks(tl, func_done, func_error, func_progress, data);
}

void thumb_loader_set_cache(ThumbLoader *tl)
{
	thumb_loader_std_set_cache(tl);
}

gboolean thumb_loader_start(ThumbLoader *tl, FileData *fd)
{
	return thumb_loader_std_start(tl, fd);
}

void thumb_loader_free(ThumbLoader *tl)
{
	thumb_loader_std_free(tl);
}

GdkPixbuf *thumb_loader_get_pixbuf(ThumbLoader *tl)
{
	return thumb_loader_std_get_pixbuf(tl);
}

/* release thumb_pixbuf on file change - this forces reload. */
void thumb_notify_cb(FileData *fd, NotifyType type, gpointer)
{
	if ((type & (NOTIFY_REREAD | NOTIFY_CHANGE)) && fd->thumb_pixbuf)
		{
		DEBUG_1("Notify thumb: %s %04x", fd->path, type);
		g_object_unref(fd->thumb_pixbuf);
		fd->thumb_pixbuf = nullptr;
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
