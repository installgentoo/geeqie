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

#include "thumb-standard.h"

#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <glib-object.h>

#include <config.h>

#include "cache.h"
#include "color-man.h"
#include "debug.h"
#include "exif.h"
#include "filedata.h"
#include "image-load.h"
#include "md5-util.h"
#include "metadata.h"
#include "options.h"
#include "pixbuf-util.h"
#include "typedefs.h"
#include "ui-fileops.h"

struct ExifData;

/**
 * @file
 *
 * This thumbnail caching implementation attempts to conform
 * to the Thumbnail Managing Standard proposed on freedesktop.org
 * The standard is documented here: \n
 *   https://www.freedesktop.org/wiki/Specifications/thumbnails/ \n
 *
 * This code attempts to conform to version 0.7.0 of the standard.
 *
 * Notes:
 *   > Validation of the thumb's embedded uri is a simple strcmp between our
 *   > version of the escaped uri and the thumb's escaped uri. But not all uri
 *   > escape functions escape the same set of chars, comparing the unescaped
 *   > versions may be more accurate. \n
 *   > Only Thumb::URI and Thumb::MTime are stored in a thumb at this time.
 *     Storing the Size, Width, Height should probably be implemented.
 */


#define THUMB_SIZE_NORMAL   128
#define THUMB_MARKER_URI    "tEXt::Thumb::URI"
#define THUMB_MARKER_MTIME  "tEXt::Thumb::MTime"
#define THUMB_MARKER_SIZE   "tEXt::Thumb::Size"
#define THUMB_MARKER_WIDTH  "tEXt::Thumb::Image::Width"
#define THUMB_MARKER_HEIGHT "tEXt::Thumb::Image::Height"
#define THUMB_MARKER_APP    "tEXt::Software"

/*
 *-----------------------------------------------------------------------------
 * thumbnail loader
 *-----------------------------------------------------------------------------
 */


static void thumb_loader_std_error_cb(ImageLoader *il, gpointer data);
static gint thumb_loader_std_setup(ThumbLoader *tl, FileData *fd);


ThumbLoader *thumb_loader_std_new(gint save_width, gint display_width)
{
	ThumbLoader *tl;

	tl = g_new0(ThumbLoader, 1);

	tl->standard_loader = TRUE;
	tl->save_width = save_width;
	tl->display_width = display_width;
	tl->cache_enable = options->thumbnails.enable_caching;

	return tl;
}

void thumb_loader_std_set_callbacks(ThumbLoader *tl,
				    ThumbLoader::Func func_done,
				    ThumbLoader::Func func_error,
				    ThumbLoader::Func func_progress,
				    gpointer data)
{
	if (!tl) return;

	tl->func_done = func_done;
	tl->func_error = func_error;
	tl->func_progress = func_progress;
	tl->data = data;
}

static void thumb_loader_std_reset(ThumbLoader *tl)
{
	image_loader_free(tl->il);
	tl->il = nullptr;

	file_data_unref(tl->fd);
	tl->fd = nullptr;

	g_free(tl->thumb_path);
	tl->thumb_path = nullptr;

	g_free(tl->thumb_uri);
	tl->thumb_uri = nullptr;

	tl->cache_hit = FALSE;

	tl->source_mtime = 0;
	tl->source_size = 0;
	tl->source_mode = 0;

	tl->progress = 0.0;
}

static gchar *thumb_std_cache_path(const gchar *path, const gchar *uri, const gchar *cache_subfolder)
{
	gchar *result = nullptr;
	gchar *md5_text;
	gchar *name;

	if (!path || !uri || !cache_subfolder) return nullptr;

	md5_text = md5_get_string(reinterpret_cast<const guchar *>(uri), strlen(uri));

	if (!md5_text) return nullptr;

	name = g_strconcat(md5_text, THUMB_NAME_EXTENSION, NULL);

	result = g_build_filename(get_thumbnails_standard_cache_dir(),
												cache_subfolder, name, NULL);

	g_free(name);
	g_free(md5_text);

	return result;
}

static gchar *thumb_loader_std_cache_path(ThumbLoader *tl, GdkPixbuf *pixbuf)
{
	const gchar *folder;
	gint w;
	gint h;

	if (!tl->fd || !tl->thumb_uri) return nullptr;

	if (pixbuf)
		{
		w = gdk_pixbuf_get_width(pixbuf);
		h = gdk_pixbuf_get_height(pixbuf);
		}
	else
		{
		w = tl->save_width;
		h = tl->save_width;
		}

	if (w > THUMB_SIZE_NORMAL || h > THUMB_SIZE_NORMAL)
		{
		folder = THUMB_FOLDER_LARGE;
		}
	else
		{
		folder = THUMB_FOLDER_NORMAL;
		}

	return thumb_std_cache_path(tl->fd->path, tl->thumb_uri, folder);
}

static gboolean thumb_loader_std_validate(ThumbLoader *tl, GdkPixbuf *pixbuf)
{
	const gchar *valid_uri;
	const gchar *uri;
	const gchar *mtime_str;
	time_t mtime;
	gint w;
	gint h;

	if (!pixbuf) return FALSE;

	w = gdk_pixbuf_get_width(pixbuf);
	h = gdk_pixbuf_get_height(pixbuf);

	if (w != tl->save_width && h != tl->save_width) return FALSE;

	valid_uri = tl->thumb_uri;

	uri = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_URI);
	mtime_str = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_MTIME);

	if (!mtime_str || !uri || !valid_uri) return FALSE;
	if (strcmp(uri, valid_uri) != 0) return FALSE;

	mtime = strtol(mtime_str, nullptr, 10);
	if (tl->source_mtime != mtime) return FALSE;

	return TRUE;
}

static void thumb_loader_std_save(ThumbLoader *tl, GdkPixbuf *pixbuf)
{
	gchar *base_path;
	gchar *tmp_path;

	if (!tl->cache_enable || tl->cache_hit) return;
	if (tl->thumb_path) return;

	if (!pixbuf)
		{
		log_printf("warning: thumbnail generation failed (no fail marker file written): source=%s\n",
		           tl->fd && tl->fd->path ? tl->fd->path : "(null)");
		return;
		}
	else
		{
		g_object_ref(G_OBJECT(pixbuf));
		}

	tl->thumb_path = thumb_loader_std_cache_path(tl, pixbuf);
	if (!tl->thumb_path)
		{
		g_object_unref(G_OBJECT(pixbuf));
		return;
		}

	/* create thumbnail dir if needed */
	base_path = remove_level_from_path(tl->thumb_path);
	recursive_mkdir_if_not_exists(base_path, S_IRWXU);
	g_free(base_path);

	DEBUG_1("thumb saving: %s", tl->fd->path);
	DEBUG_1("       saved: %s", tl->thumb_path);

	/* save thumb, using a temp file then renaming into place */
	tmp_path = unique_filename(tl->thumb_path, ".tmp", "_", 2);
	if (tmp_path)
		{
		const gchar *mark_uri;
		gchar *mark_app;
		gchar *pathl;
		gboolean success;

		mark_uri = tl->thumb_uri;

		mark_app = g_strdup_printf("%s %s", GQ_APPNAME, VERSION);
		const std::string mark_mtime = std::to_string(static_cast<unsigned long long>(tl->source_mtime));
		pathl = path_from_utf8(tmp_path);
		success = gdk_pixbuf_save(pixbuf, pathl, "png", nullptr,
		                          THUMB_MARKER_URI, mark_uri,
		                          THUMB_MARKER_MTIME, mark_mtime.c_str(),
		                          THUMB_MARKER_APP, mark_app,
		                          NULL);
		if (success)
			{
			const auto default_permission = 0600;
			chmod(pathl, default_permission);
			success = rename_file(tmp_path, tl->thumb_path);
			}

		g_free(pathl);
		g_free(mark_app);
		g_free(tmp_path);

		if (!success)
			{
			DEBUG_1("thumb save failed: %s", tl->fd->path);
			DEBUG_1("            thumb: %s", tl->thumb_path);
			}
		}

	g_object_unref(G_OBJECT(pixbuf));
}

static void thumb_loader_std_set_fallback(ThumbLoader *tl)
{
	if (tl->fd->thumb_pixbuf) g_object_unref(tl->fd->thumb_pixbuf);
	tl->fd->thumb_pixbuf = pixbuf_fallback(tl->fd, tl->display_width, tl->display_width);
}

static GdkPixbuf *thumb_loader_std_finish(ThumbLoader *tl, GdkPixbuf *pixbuf)
{
	GdkPixbuf *pixbuf_thumb = nullptr;
	GdkPixbuf *result;
	GdkPixbuf *rotated = nullptr;

	if (!tl->cache_hit && options->image.exif_rotate_enable)
		{
		if (!tl->fd->exif_orientation)
			{
			if (g_strcmp0(tl->fd->format_name, "heif") != 0)
				{
				tl->fd->exif_orientation = metadata_read_int(tl->fd, ORIENTATION_KEY, EXIF_ORIENTATION_TOP_LEFT);
				}
			else
				{
				tl->fd->exif_orientation = EXIF_ORIENTATION_TOP_LEFT;
				}
			}

		if (tl->fd->exif_orientation != EXIF_ORIENTATION_TOP_LEFT)
			{
			rotated = pixbuf_apply_orientation(pixbuf, tl->fd->exif_orientation);
			pixbuf = rotated;
			}
		}

	gint sw = gdk_pixbuf_get_width(pixbuf);
	gint sh = gdk_pixbuf_get_height(pixbuf);
	gint thumb_w;
	gint thumb_h;

	if (tl->cache_enable)
		{
		if (!tl->cache_hit)
			{
			gint cache_w = tl->save_width;

			if (sw > cache_w || sh > cache_w)
				{
				struct stat st;

				if (pixbuf_scale_aspect(cache_w, cache_w, sw, sh,
				                        thumb_w, thumb_h))
					{
					pixbuf_thumb = gdk_pixbuf_scale_simple(pixbuf, thumb_w, thumb_h,
									       static_cast<GdkInterpType>(options->thumbnails.quality));
					}
				else
					{
					pixbuf_thumb = pixbuf;
					g_object_ref(G_OBJECT(pixbuf_thumb));
					}

				/* do not save the thumbnail if the source file has changed meanwhile -
				   the thumbnail is most probably broken */
				if (stat_utf8(tl->fd->path, &st) &&
				    tl->source_mtime == st.st_mtime &&
				    tl->source_size == st.st_size)
					{
					thumb_loader_std_save(tl, pixbuf_thumb);
					}
				}
			}
		}

	if (pixbuf_thumb)
		{
		pixbuf = pixbuf_thumb;
		sw = gdk_pixbuf_get_width(pixbuf);
		sh = gdk_pixbuf_get_height(pixbuf);
		}

	gint req_w = tl->display_width;
	if (pixbuf_scale_aspect(req_w, req_w, sw, sh,
	                        thumb_w, thumb_h))
		{
		result = gdk_pixbuf_scale_simple(pixbuf, thumb_w, thumb_h,
										static_cast<GdkInterpType>(options->thumbnails.quality));
		}
	else
		{
		result = pixbuf;
		g_object_ref(result);
		}

	if (pixbuf_thumb) g_object_unref(pixbuf_thumb);
	if (rotated) g_object_unref(rotated);

	return result;
}

static void thumb_loader_std_done_cb(ImageLoader *, gpointer data)
{
	auto tl = static_cast<ThumbLoader *>(data);
	GdkPixbuf *pixbuf;

	DEBUG_1("thumb image done: %s", tl->fd ? tl->fd->path : "???");
	DEBUG_1("            from: %s", image_loader_get_fd(tl->il)->path);

	pixbuf = image_loader_get_pixbuf(tl->il);

	if (tl->thumb_path && (!pixbuf || !thumb_loader_std_validate(tl, pixbuf)))
		{
		/* cached thumb was broken or stale — unlink and load source */
		DEBUG_1("thumb invalid, unlinking: %s", tl->thumb_path);
		unlink_file(tl->thumb_path);
		g_free(tl->thumb_path);
		tl->thumb_path = nullptr;

		image_loader_free(tl->il);
		tl->il = nullptr;

		if (thumb_loader_std_setup(tl, tl->fd)) return;

		thumb_loader_std_set_fallback(tl);
		if (tl->func_error) tl->func_error(tl, tl->data);
		return;
		}

	if (!pixbuf)
		{
		/* source image failed to load */
		DEBUG_1("thumb source error: %s", tl->fd->path);
		thumb_loader_std_set_fallback(tl);
		if (tl->func_error) tl->func_error(tl, tl->data);
		return;
		}

	tl->cache_hit = (tl->thumb_path != nullptr);

	if (tl->fd)
		{
		if (tl->fd->thumb_pixbuf) g_object_unref(tl->fd->thumb_pixbuf);
		tl->fd->thumb_pixbuf = thumb_loader_std_finish(tl, pixbuf);
		}

	if (tl->func_done) tl->func_done(tl, tl->data);
}

static void thumb_loader_std_error_cb(ImageLoader *il, gpointer data)
{
	auto tl = static_cast<ThumbLoader *>(data);

	/* if at least some of the image is available, go to done */
	if (image_loader_get_pixbuf(tl->il) != nullptr)
		{
		thumb_loader_std_done_cb(il, data);
		return;
		}

	DEBUG_1("thumb image error: %s", tl->fd->path);
	DEBUG_1("             from: %s", image_loader_get_fd(tl->il)->path);

	/* pass through done_cb which handles both cached and source failures */
	thumb_loader_std_done_cb(il, data);
}

static void thumb_loader_std_progress_cb(ImageLoader *, gdouble percent, gpointer data)
{
	auto tl = static_cast<ThumbLoader *>(data);

	tl->progress = percent;

	if (tl->func_progress) tl->func_progress(tl, tl->data);
}

static gboolean thumb_loader_std_setup(ThumbLoader *tl, FileData *fd)
{
	tl->il = image_loader_new(fd);
	image_loader_set_priority(tl->il, G_PRIORITY_LOW);

	/* this will speed up jpegs by up to 3x in some cases */
	if (tl->cache_enable)
		image_loader_set_requested_size(tl->il, tl->save_width, tl->save_width);
	else
		image_loader_set_requested_size(tl->il, tl->display_width, tl->display_width);

	g_signal_connect(G_OBJECT(tl->il), "error", (GCallback)thumb_loader_std_error_cb, tl);
	if (tl->func_progress)
		{
		g_signal_connect(G_OBJECT(tl->il), "percent", (GCallback)thumb_loader_std_progress_cb, tl);
		}
	g_signal_connect(G_OBJECT(tl->il), "done", (GCallback)thumb_loader_std_done_cb, tl);

	if (image_loader_start(tl->il))
		{
		return TRUE;
		}

	image_loader_free(tl->il);
	tl->il = nullptr;
	return FALSE;
}

void thumb_loader_std_set_cache(ThumbLoader *tl)
{
	if (!tl) return;

	tl->cache_enable = TRUE;
}

gboolean thumb_loader_std_start(ThumbLoader *tl, FileData *fd)
{
	struct stat st;

	if (!tl || !fd) return FALSE;

	thumb_loader_std_reset(tl);


	tl->fd = file_data_ref(fd);
	if (!stat_utf8(fd->path, &st) || (tl->fd->format_class != FORMAT_CLASS_IMAGE && tl->fd->format_class != FORMAT_CLASS_RAWIMAGE && tl->fd->format_class != FORMAT_CLASS_VIDEO && tl->fd->format_class != FORMAT_CLASS_DOCUMENT && !options->file_filter.disable))
		{
		thumb_loader_std_set_fallback(tl);
		return FALSE;
		}
	tl->source_mtime = st.st_mtime;
	tl->source_size = st.st_size;
	tl->source_mode = st.st_mode;

	static const gchar *thumb_cache = get_thumbnails_standard_cache_dir();

	if (strncmp(tl->fd->path, thumb_cache, strlen(thumb_cache)) != 0)
		{
		gchar *pathl;

		pathl = path_from_utf8(fd->path);
		tl->thumb_uri = g_filename_to_uri(pathl, nullptr, nullptr);
		g_free(pathl);
		}

	if (tl->cache_enable)
		{
		struct stat thumb_st;

		tl->thumb_path = thumb_loader_std_cache_path(tl, nullptr);

		/* stat-based pre-check: skip loading thumbs older than the source */
		gboolean found = (stat_utf8(tl->thumb_path, &thumb_st) && S_ISREG(thumb_st.st_mode));
		if (found && thumb_st.st_mtime >= tl->source_mtime)
			{
			FileData *fd = file_data_new(tl->thumb_path, &thumb_st);
			if (thumb_loader_std_setup(tl, fd))
				{
				file_data_unref(fd);
				return TRUE;
				}
			file_data_unref(fd);
			}

		/* cached thumb missing or stale — clean up and load source directly */
		if (found) unlink_file(tl->thumb_path);
		g_free(tl->thumb_path);
		tl->thumb_path = nullptr;
		}

	if (!thumb_loader_std_setup(tl, tl->fd))
		{
		thumb_loader_std_set_fallback(tl);
		return FALSE;
		}

	return TRUE;
}

void thumb_loader_std_free(ThumbLoader *tl)
{
	if (!tl) return;

	thumb_loader_std_reset(tl);
	g_free(tl);
}

GdkPixbuf *thumb_loader_std_get_pixbuf(ThumbLoader *tl)
{
	GdkPixbuf *pixbuf;

	if (tl && tl->fd && tl->fd->thumb_pixbuf)
		{
		pixbuf = tl->fd->thumb_pixbuf;
		g_object_ref(pixbuf);
		}
	else
		{
		pixbuf = pixbuf_fallback(nullptr, tl->display_width, tl->display_width);
		}

	return pixbuf;
}


struct ThumbValidate
{
	ThumbLoader *tl;
	gchar *path;
	gint days;

	void (*func_valid)(const gchar *path, gboolean valid, gpointer data);
	gpointer data;

	guint idle_id; /* event source id */
};

static void thumb_loader_std_thumb_file_validate_free(ThumbValidate *tv)
{
	thumb_loader_std_free(tv->tl);
	g_free(tv->path);
	g_free(tv);
}

void thumb_loader_std_thumb_file_validate_cancel(ThumbLoader *tl)
{
	ThumbValidate *tv;

	if (!tl) return;

	tv = static_cast<ThumbValidate *>(tl->data);

	if (tv->idle_id)
		{
		g_source_remove(tv->idle_id);
		tv->idle_id = 0;
		}

	thumb_loader_std_thumb_file_validate_free(tv);
}

static void thumb_loader_std_thumb_file_validate_finish(ThumbValidate *tv, gboolean valid)
{
	if (tv->func_valid) tv->func_valid(tv->path, valid, tv->data);

	thumb_loader_std_thumb_file_validate_free(tv);
}

static void thumb_loader_std_thumb_file_validate_done_cb(ThumbLoader *, gpointer data)
{
	auto tv = static_cast<ThumbValidate *>(data);
	GdkPixbuf *pixbuf;
	gboolean valid = FALSE;

	/* get the original thumbnail pixbuf (unrotated, with original options)
	   this is called from image_loader done callback, so tv->tl->il must exist*/
	pixbuf = image_loader_get_pixbuf(tv->tl->il);
	if (pixbuf)
		{
		const gchar *uri;
		const gchar *mtime_str;

		uri = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_URI);
		mtime_str = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_MTIME);
		if (uri && mtime_str)
			{
			if (strncmp(uri, "file:", strlen("file:")) == 0)
				{
				struct stat st;
				gchar *target;

				target = g_filename_from_uri(uri, nullptr, nullptr);
				if (stat(target, &st) == 0 &&
				    st.st_mtime == strtol(mtime_str, nullptr, 10))
					{
					valid = TRUE;
					}
				g_free(target);
				}
			else
				{
				struct stat st;

				DEBUG_1("thumb uri foreign, doing day check: %s", uri);

				if (stat_utf8(tv->path, &st))
					{
					time_t now;

					now = time(nullptr);
					if (st.st_atime >= now - static_cast<time_t>(tv->days) * 24 * 60 * 60)
						{
						valid = TRUE;
						}
					}
				}
			}
		else
			{
			DEBUG_1("invalid image found in std cache: %s", tv->path);
			}
		}

	thumb_loader_std_thumb_file_validate_finish(tv, valid);
}

static void thumb_loader_std_thumb_file_validate_error_cb(ThumbLoader *, gpointer data)
{
	auto tv = static_cast<ThumbValidate *>(data);

	thumb_loader_std_thumb_file_validate_finish(tv, FALSE);
}

static gboolean thumb_loader_std_thumb_file_validate_idle_cb(gpointer data)
{
	auto tv = static_cast<ThumbValidate *>(data);

	tv->idle_id = 0;
	thumb_loader_std_thumb_file_validate_finish(tv, FALSE);

	return G_SOURCE_REMOVE;
}

/**
 * @brief Validates a non local thumbnail file,
 * calling func_valid with the information when app is idle
 * for thumbnail's without a file: uri, validates against allowed_age in days
 */
ThumbLoader *thumb_loader_std_thumb_file_validate(const gchar *thumb_path, gint allowed_days,
						     void (*func_valid)(const gchar *path, gboolean valid, gpointer data),
						     gpointer data)
{
	ThumbValidate *tv;

	tv = g_new0(ThumbValidate, 1);

	tv->tl = thumb_loader_std_new(tv->tl->save_width, tv->tl->display_width);
	thumb_loader_std_set_callbacks(tv->tl,
				       thumb_loader_std_thumb_file_validate_done_cb,
				       thumb_loader_std_thumb_file_validate_error_cb,
				       nullptr,
				       tv);
	thumb_loader_std_reset(tv->tl);

	tv->path = g_strdup(thumb_path);
	tv->days = allowed_days;
	tv->func_valid = func_valid;
	tv->data = data;

	FileData *fd = file_data_new(thumb_path);
	if (!thumb_loader_std_setup(tv->tl, fd))
		{
		tv->idle_id = g_idle_add(thumb_loader_std_thumb_file_validate_idle_cb, tv);
		}
	else
		{
		tv->idle_id = 0;
		}

	file_data_unref(fd);
	return tv->tl;
}

static void thumb_std_maint_remove_one(const gchar *source, const gchar *uri, const gchar *subfolder)
{
	gchar *thumb_path;

	thumb_path = thumb_std_cache_path(source, uri, subfolder);
	if (isfile(thumb_path))
		{
		DEBUG_1("thumb removing: %s", thumb_path);
		unlink_file(thumb_path);
		}
	g_free(thumb_path);
}

/* this also removes local thumbnails (the source is gone so it makes sense) */
void thumb_std_maint_removed(const gchar *source)
{
	gchar *uri;
	gchar *sourcel;

	sourcel = path_from_utf8(source);
	uri = g_filename_to_uri(sourcel, nullptr, nullptr);
	g_free(sourcel);

	/* all this to remove a thumbnail? */

	thumb_std_maint_remove_one(source, uri, THUMB_FOLDER_NORMAL);
	thumb_std_maint_remove_one(source, uri, THUMB_FOLDER_LARGE);

	g_free(uri);
}

struct TMaintMove
{
	gchar *source;
	gchar *dest;

	ThumbLoader *tl;
	gchar *source_uri;
	gchar *thumb_path;

	gint pass;
};

static GList *thumb_std_maint_move_list = nullptr;
static GList *thumb_std_maint_move_tail = nullptr;


static void thumb_std_maint_move_step(TMaintMove *tm);
static gboolean thumb_std_maint_move_idle(gpointer data);


static void thumb_std_maint_move_step(TMaintMove *tm)
{
	if (tm->dest && tm->source)
	{
		DEBUG_1("thumb move attempting rename:");

		auto* uri = g_filename_to_uri(tm->source, nullptr, nullptr);
		auto* new_uri = g_filename_to_uri(tm->dest, nullptr, nullptr);
		auto* thumb_path = thumb_std_cache_path(tm->source, uri, THUMB_FOLDER_NORMAL);
		auto* new_thumb_path = thumb_std_cache_path(tm->dest, new_uri, THUMB_FOLDER_NORMAL);

		gboolean success = rename_file(thumb_path, new_thumb_path);

		if (!success)
			{
			DEBUG_1("thumb move failed: %s", tm->dest);
			DEBUG_1("            thumb: %s", new_thumb_path);
			}

		g_free(uri);
		g_free(new_uri);
		g_free(thumb_path);
		g_free(new_thumb_path);

		g_free(tm->source);
		g_free(tm->dest);
		g_free(tm->source_uri);
		g_free(tm->thumb_path);
		g_free(tm);

	}

	if (thumb_std_maint_move_list)
	{
	g_idle_add_full(G_PRIORITY_LOW, thumb_std_maint_move_idle, nullptr, nullptr);
	}
}

static gboolean thumb_std_maint_move_idle(gpointer)
{
	TMaintMove *tm;
	gchar *pathl;

	if (!thumb_std_maint_move_list) return G_SOURCE_REMOVE;

	tm = static_cast<TMaintMove *>(thumb_std_maint_move_list->data);

	thumb_std_maint_move_list = g_list_remove(thumb_std_maint_move_list, tm);
	if (!thumb_std_maint_move_list) thumb_std_maint_move_tail = nullptr;

	pathl = path_from_utf8(tm->source);
	tm->source_uri = g_filename_to_uri(pathl, nullptr, nullptr);
	g_free(pathl);

	tm->pass = 0;

	thumb_std_maint_move_step(tm);

	return G_SOURCE_REMOVE;
}

/* This will schedule a move of the thumbnail for source image to dest when idle.
 * We do this so that file renaming or moving speed is not sacrificed by
 * moving the thumbnails at the same time because:
 *
 * This cache design requires the tedious task of loading the png thumbnails and saving them.
 *
 * The thumbnails are processed when the app is idle. If the app
 * exits early well too bad - they can simply be regenerated from scratch.
 */
/** @FIXME This does not manage local thumbnails (fixme ?)
 */
void thumb_std_maint_moved(const gchar *source, const gchar *dest)
{
	TMaintMove *tm;

	tm = g_new0(TMaintMove, 1);
	tm->source = g_strdup(source);
	tm->dest = g_strdup(dest);

	if (!thumb_std_maint_move_list)
		{
		g_idle_add_full(G_PRIORITY_LOW, thumb_std_maint_move_idle, nullptr, nullptr);
		}

	if (thumb_std_maint_move_tail)
		{
		thumb_std_maint_move_tail = g_list_append(thumb_std_maint_move_tail, tm);
		thumb_std_maint_move_tail = thumb_std_maint_move_tail->next;
		}
	else
		{
		thumb_std_maint_move_list = g_list_append(thumb_std_maint_move_list, tm);
		thumb_std_maint_move_tail = thumb_std_maint_move_list;
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
