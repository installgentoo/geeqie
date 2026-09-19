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
#include <memory>
#include <string>

#include <glib-object.h>

#include <config.h>

#include "cache.h"
#include "debug.h"
#include "filedata.h"
#include "image-load.h"
#include "md5-util.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"

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


/*
 * A thumbnail is one job on a worker: validate the cached PNG, or decode, reduce, save the PNG and scale
 * to display size. The main thread stats, queues, and applies the result. Workers never touch a FileData
 * (its refcount and the file pool are not thread-safe); the job carries copies of what it needs, and the
 * ImageLoader that holds the FileData ref is created and freed on the main thread.
 */

struct ThumbTask
{
	virtual ~ThumbTask() = default;
	/** Runs on a worker, then hands the task back to the main thread with g_idle_add. */
	virtual void run() = 0;
};

static GThreadPool *thumb_task_pool = nullptr;

static void thumb_task_push(ThumbTask *task)
{
	const gint threads = worker_thread_limit();

	if (!thumb_task_pool)
		{
		thumb_task_pool = g_thread_pool_new([](gpointer data, gpointer) { static_cast<ThumbTask *>(data)->run(); },
		                                    nullptr, threads, FALSE, nullptr);
		}
	else
		{
		g_thread_pool_set_max_threads(thumb_task_pool, threads, nullptr);
		}

	g_thread_pool_push(thumb_task_pool, task, nullptr);
}

struct ThumbJob : ThumbTask
{
	ThumbLoader *tl = nullptr; /**< main thread only; nullptr once the ThumbLoader let go */
	gint cancelled = 0; /**< atomic; lets a queued job skip its work */

	gchar *path = nullptr;
	gchar *thumb_uri = nullptr;
	gchar *cached_path = nullptr; /**< existing PNG to use instead of decoding */
	ImageLoader *il = nullptr; /**< set only when decoding */
	time_t source_mtime = 0;
	off_t source_size = 0;
	gint save_width = 0;
	gint display_width = 0;
	gboolean cache_enable = FALSE;
	GdkInterpType quality = GDK_INTERP_BILINEAR;

	GdkPixbuf *pixbuf = nullptr; /**< result at display size, or nullptr on failure */
	gboolean cached_invalid = FALSE; /**< cached_path was stale and is removed; the source still needs decoding */

	~ThumbJob() override
	{
		g_free(path);
		g_free(thumb_uri);
		g_free(cached_path);
		image_loader_free(il);
		if (pixbuf) g_object_unref(pixbuf);
	}

	void run() override;
};

ThumbLoader *thumb_loader_new(gint save_width, gint display_width)
{
	ThumbLoader *tl;

	tl = g_new0(ThumbLoader, 1);

	tl->save_width = save_width;
	tl->display_width = display_width;
	tl->cache_enable = options->thumbnails.enable_caching;

	return tl;
}

void thumb_loader_set_callbacks(ThumbLoader *tl,
				    ThumbLoader::Func func_done,
				    ThumbLoader::Func func_error,
				    gpointer data)
{
	if (!tl) return;

	tl->func_done = func_done;
	tl->func_error = func_error;
	tl->data = data;
}

static void thumb_loader_std_reset(ThumbLoader *tl)
{
	if (tl->job)
		{
		g_atomic_int_set(&tl->job->cancelled, 1);
		tl->job->tl = nullptr;
		tl->job = nullptr;
		}

	file_data_unref(tl->fd);
	tl->fd = nullptr;

	g_free(tl->thumb_uri);
	tl->thumb_uri = nullptr;

	tl->source_mtime = 0;
	tl->source_size = 0;
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

static gchar *thumb_cache_path_for_size(const gchar *path, const gchar *uri, gint w, gint h)
{
	const gchar *folder = (w > THUMB_SIZE_NORMAL || h > THUMB_SIZE_NORMAL) ? THUMB_FOLDER_LARGE : THUMB_FOLDER_NORMAL;
	return thumb_std_cache_path(path, uri, folder);
}

static GdkPixbuf *thumb_scale_to(GdkPixbuf *pixbuf, gint size, GdkInterpType quality)
{
	gint w;
	gint h;

	if (pixbuf_scale_aspect(size, size, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), w, h))
		{
		return gdk_pixbuf_scale_simple(pixbuf, w, h, quality);
		}

	return static_cast<GdkPixbuf *>(g_object_ref(pixbuf));
}

static gboolean thumb_job_cached_valid(const ThumbJob *job, GdkPixbuf *pixbuf)
{
	if (gdk_pixbuf_get_width(pixbuf) != job->save_width && gdk_pixbuf_get_height(pixbuf) != job->save_width) return FALSE;

	const gchar *uri = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_URI);
	const gchar *mtime_str = gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_MTIME);

	if (!mtime_str || !uri || !job->thumb_uri) return FALSE;
	if (strcmp(uri, job->thumb_uri) != 0) return FALSE;

	return job->source_mtime == strtol(mtime_str, nullptr, 10);
}

static void thumb_job_save(const ThumbJob *job, GdkPixbuf *pixbuf)
{
	g_autofree gchar *thumb_path = thumb_cache_path_for_size(job->path, job->thumb_uri,
	                                                         gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
	if (!thumb_path) return;

	g_autofree gchar *base_path = remove_level_from_path(thumb_path);
	recursive_mkdir_if_not_exists(base_path, S_IRWXU);

	DEBUG_1("thumb saving: %s", job->path);
	DEBUG_1("       saved: %s", thumb_path);

	/* save thumb, using a temp file then renaming into place */
	g_autofree gchar *tmp_path = unique_filename(thumb_path, ".tmp", "_", 2);
	if (!tmp_path) return;

	g_autofree gchar *mark_app = g_strdup_printf("%s %s", GQ_APPNAME, VERSION);
	const std::string mark_mtime = std::to_string(static_cast<unsigned long long>(job->source_mtime));
	g_autofree gchar *pathl = path_from_utf8(tmp_path);
	gboolean success = gdk_pixbuf_save(pixbuf, pathl, "png", nullptr,
	                                   THUMB_MARKER_URI, job->thumb_uri,
	                                   THUMB_MARKER_MTIME, mark_mtime.c_str(),
	                                   THUMB_MARKER_APP, mark_app,
	                                   NULL);
	if (success)
		{
		const auto default_permission = 0600;
		chmod(pathl, default_permission);
		success = rename_file(tmp_path, thumb_path);
		}

	if (!success)
		{
		DEBUG_1("thumb save failed: %s", job->path);
		DEBUG_1("            thumb: %s", thumb_path);
		}
}

/* Returns the display-size thumbnail for a freshly decoded source, saving the cache-size one on the way. */
static GdkPixbuf *thumb_job_from_decoded(const ThumbJob *job, GdkPixbuf *decoded)
{
	g_autoptr(GdkPixbuf) cache_pixbuf = nullptr;
	GdkPixbuf *source = decoded;

	if (job->cache_enable)
		{
		const gint sw = gdk_pixbuf_get_width(decoded);
		const gint sh = gdk_pixbuf_get_height(decoded);

		/* >= because the loader already reduced large sources to exactly save_width on the long side;
		 * a source that started smaller than the thumbnail is still not cached. */
		if (sw >= job->save_width || sh >= job->save_width)
			{
			cache_pixbuf = thumb_scale_to(decoded, job->save_width, job->quality);
			source = cache_pixbuf;

			/* do not save the thumbnail if the source file has changed meanwhile -
			   the thumbnail is most probably broken */
			struct stat st;
			if (stat_utf8(job->path, &st) &&
			    job->source_mtime == st.st_mtime &&
			    job->source_size == st.st_size)
				{
				thumb_job_save(job, cache_pixbuf);
				}
			}
		}

	return thumb_scale_to(source, job->display_width, job->quality);
}

static gboolean thumb_job_done_idle_cb(gpointer data);

void ThumbJob::run()
{
	if (!g_atomic_int_get(&cancelled))
		{
		if (cached_path)
			{
			g_autofree gchar *pathl = path_from_utf8(cached_path);
			g_autoptr(GdkPixbuf) cached = gdk_pixbuf_new_from_file(pathl, nullptr);

			if (cached && thumb_job_cached_valid(this, cached))
				{
				pixbuf = thumb_scale_to(cached, display_width, quality);
				}
			else
				{
				DEBUG_1("thumb invalid, unlinking: %s", cached_path);
				unlink_file(cached_path);
				cached_invalid = TRUE;
				}
			}
		else if (image_loader_load_sync(il))
			{
			pixbuf = thumb_job_from_decoded(this, image_loader_get_pixbuf(il));
			}
		}

	g_idle_add(thumb_job_done_idle_cb, this);
}

static void thumb_loader_std_set_fallback(ThumbLoader *tl)
{
	if (tl->fd->thumb_pixbuf) g_object_unref(tl->fd->thumb_pixbuf);
	tl->fd->thumb_pixbuf = pixbuf_fallback(tl->fd, tl->display_width, tl->display_width);
}

static void thumb_job_start(ThumbLoader *tl, const gchar *cached_path)
{
	auto job = new ThumbJob();

	job->tl = tl;
	job->path = g_strdup(tl->fd->path);
	job->thumb_uri = g_strdup(tl->thumb_uri);
	job->cached_path = g_strdup(cached_path);
	job->source_mtime = tl->source_mtime;
	job->source_size = tl->source_size;
	job->save_width = tl->save_width;
	job->display_width = tl->display_width;
	job->cache_enable = tl->cache_enable;
	job->quality = static_cast<GdkInterpType>(options->thumbnails.quality);

	if (!cached_path)
		{
		job->il = image_loader_new(tl->fd);
		image_loader_set_priority(job->il, G_PRIORITY_LOW);

		/* this will speed up jpegs by up to 3x in some cases */
		const gint size = tl->cache_enable ? tl->save_width : tl->display_width;
		image_loader_set_requested_size(job->il, size, size);
		}

	tl->job = job;
	thumb_task_push(job);
}

static gboolean thumb_job_done_idle_cb(gpointer data)
{
	std::unique_ptr<ThumbJob> job(static_cast<ThumbJob *>(data));
	ThumbLoader *tl = job->tl;

	if (!tl) return G_SOURCE_REMOVE;
	tl->job = nullptr;

	if (job->cached_invalid)
		{
		thumb_job_start(tl, nullptr);
		return G_SOURCE_REMOVE;
		}

	GdkPixbuf *pixbuf = g_steal_pointer(&job->pixbuf);
	job.reset();

	/* the callbacks may free tl, so they come last */
	if (pixbuf)
		{
		if (tl->fd->thumb_pixbuf) g_object_unref(tl->fd->thumb_pixbuf);
		tl->fd->thumb_pixbuf = pixbuf;
		if (tl->func_done) tl->func_done(tl, tl->data);
		}
	else
		{
		DEBUG_1("thumb source error: %s", tl->fd->path);
		thumb_loader_std_set_fallback(tl);
		if (tl->func_error) tl->func_error(tl, tl->data);
		}

	return G_SOURCE_REMOVE;
}

void thumb_loader_set_cache(ThumbLoader *tl)
{
	if (!tl) return;

	tl->cache_enable = TRUE;
}

gboolean thumb_loader_start(ThumbLoader *tl, FileData *fd)
{
	struct stat st;

	if (!tl || !fd) return FALSE;

	thumb_loader_std_reset(tl);

	tl->fd = file_data_ref(fd);
	if (!stat_utf8(fd->path, &st) || (tl->fd->format_class != FORMAT_CLASS_IMAGE && tl->fd->format_class != FORMAT_CLASS_VIDEO && tl->fd->format_class != FORMAT_CLASS_DOCUMENT && !options->file_filter.disable))
		{
		thumb_loader_std_set_fallback(tl);
		return FALSE;
		}
	tl->source_mtime = st.st_mtime;
	tl->source_size = st.st_size;

	static const gchar *thumb_cache = get_thumbnails_standard_cache_dir();

	if (strncmp(tl->fd->path, thumb_cache, strlen(thumb_cache)) != 0)
		{
		g_autofree gchar *pathl = path_from_utf8(fd->path);
		tl->thumb_uri = g_filename_to_uri(pathl, nullptr, nullptr);
		}

	g_autofree gchar *cached_path = nullptr;
	if (tl->cache_enable && tl->thumb_uri)
		{
		cached_path = thumb_cache_path_for_size(tl->fd->path, tl->thumb_uri, tl->save_width, tl->save_width);

		/* stat-based pre-check: a thumb older than the source is stale without reading it */
		struct stat thumb_st;
		const gboolean found = stat_utf8(cached_path, &thumb_st) && S_ISREG(thumb_st.st_mode);
		if (found && thumb_st.st_mtime < tl->source_mtime) unlink_file(cached_path);
		if (!found || thumb_st.st_mtime < tl->source_mtime) g_clear_pointer(&cached_path, g_free);
		}

	thumb_job_start(tl, cached_path);
	return TRUE;
}

void thumb_loader_free(ThumbLoader *tl)
{
	if (!tl) return;

	thumb_loader_std_reset(tl);
	g_free(tl);
}

GdkPixbuf *thumb_loader_get_pixbuf(ThumbLoader *tl)
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


struct ThumbValidate : ThumbTask
{
	gchar *path = nullptr;
	gint days = 0;
	void (*func_valid)(const gchar *path, gboolean valid, gpointer data) = nullptr;
	gpointer data = nullptr;

	gint cancelled = 0; /**< atomic; also stops the result being delivered */
	gboolean valid = FALSE;

	~ThumbValidate() override
	{
		g_free(path);
	}

	void run() override;
};

static gboolean thumb_validate_done_idle_cb(gpointer data)
{
	std::unique_ptr<ThumbValidate> tv(static_cast<ThumbValidate *>(data));

	if (!g_atomic_int_get(&tv->cancelled) && tv->func_valid) tv->func_valid(tv->path, tv->valid, tv->data);

	return G_SOURCE_REMOVE;
}

void ThumbValidate::run()
{
	g_autofree gchar *pathl = g_atomic_int_get(&cancelled) ? nullptr : path_from_utf8(path);
	g_autoptr(GdkPixbuf) pixbuf = pathl ? gdk_pixbuf_new_from_file(pathl, nullptr) : nullptr;

	const gchar *uri = pixbuf ? gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_URI) : nullptr;
	const gchar *mtime_str = pixbuf ? gdk_pixbuf_get_option(pixbuf, THUMB_MARKER_MTIME) : nullptr;

	if (uri && mtime_str)
		{
		struct stat st;

		if (strncmp(uri, "file:", strlen("file:")) == 0)
			{
			g_autofree gchar *target = g_filename_from_uri(uri, nullptr, nullptr);
			valid = target && stat(target, &st) == 0 && st.st_mtime == strtol(mtime_str, nullptr, 10);
			}
		else
			{
			DEBUG_1("thumb uri foreign, doing day check: %s", uri);
			valid = stat_utf8(path, &st) && st.st_atime >= time(nullptr) - static_cast<time_t>(days) * 24 * 60 * 60;
			}
		}
	else if (pixbuf)
		{
		DEBUG_1("invalid image found in std cache: %s", path);
		}

	g_idle_add(thumb_validate_done_idle_cb, this);
}

void thumb_loader_std_thumb_file_validate_cancel(ThumbValidate *tv)
{
	if (tv) g_atomic_int_set(&tv->cancelled, 1);
}

/**
 * @brief Validates a thumbnail file on a worker, calling func_valid on the main thread;
 * a thumbnail without a file: uri is validated against allowed_days
 */
ThumbValidate *thumb_loader_std_thumb_file_validate(const gchar *thumb_path, gint allowed_days,
                                                    void (*func_valid)(const gchar *path, gboolean valid, gpointer data),
                                                    gpointer data)
{
	auto tv = new ThumbValidate();

	tv->path = g_strdup(thumb_path);
	tv->days = allowed_days;
	tv->func_valid = func_valid;
	tv->data = data;

	thumb_task_push(tv);
	return tv;
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
