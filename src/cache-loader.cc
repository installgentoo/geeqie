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


#include "cache-loader.h"

#include <ctime>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>

#include "cache.h"
#include "filedata.h"
#include "image-load.h"
#include "misc.h"
#include "options.h"
#include "similar.h"
#include "typedefs.h"
#include "ui-fileops.h"


static gboolean cache_loader_finish_idle_cb(gpointer data);

/*
 * Everything that takes real time runs off the main thread: a still image is decoded by the ImageLoader on its own
 * pool, and a job on the pool below then does the rest - a video's ffprobe and contact sheet, the similarity grid
 * over every pixel of the image, and the md5 of the whole file. What is left for the main thread is the sqlite
 * write and the caller's callback; a folder-wide pass is only as parallel as that leaves it.
 *
 * A job touches nothing but itself; the loader is reached again from the main thread by idle. Freeing the loader
 * detaches the job (cl = nullptr) and the job then frees itself.
 */

struct CacheLoaderJob {
	FileData *fd;
	CacheLoader *cl; /**< main thread only; nullptr once the loader is gone */
	gint cancelled; /**< atomic; lets a queued job skip the work */

	CacheDataType todo; /**< what this job was asked for */
	CacheDataType done; /**< which of the results below were obtained */
	CacheVideoProbe probe;
	ImageSimilarityData *sim;
	guchar md5sum[16];

	GdkPixbuf *pixbuf; /**< the decoded still image on the way in, a video's contact sheet on the way out */
};

static GThreadPool *cache_loader_pool = nullptr;

static void cache_loader_job_free(CacheLoaderJob *job)
{
	if (job->pixbuf) g_object_unref(job->pixbuf);
	image_sim_free(job->sim);
	file_data_unref(job->fd);
	g_free(job);
}

static gboolean cache_loader_job_done_idle_cb(gpointer data)
{
	auto job = static_cast<CacheLoaderJob *>(data);
	CacheLoader *cl = job->cl;

	if (cl)
		{
		cl->job = nullptr;

		if (job->done & CACHE_LOADER_DIMENSIONS) cache_sim_data_set_dimensions(cl->cd, job->probe.width, job->probe.height);
		if (job->done & CACHE_LOADER_SIMILARITY) cache_sim_data_set_similarity(cl->cd, job->sim);
		if (job->done & CACHE_LOADER_MD5SUM) cache_sim_data_set_md5sum(cl->cd, job->md5sum);

		cl->done_mask = static_cast<CacheDataType>(cl->done_mask | job->done);
		if (job->todo & ~job->done) cl->error = TRUE;

		cl->pixbuf = job->pixbuf;
		job->pixbuf = nullptr;

		cl->idle_id = g_idle_add(cache_loader_finish_idle_cb, cl);
		}

	cache_loader_job_free(job);
	return G_SOURCE_REMOVE;
}

static void cache_loader_job_work(CacheLoaderJob *job)
{
	if (job->fd->format_class == FORMAT_CLASS_VIDEO &&
	    job->todo & (CACHE_LOADER_SIMILARITY | CACHE_LOADER_DIMENSIONS))
		{
		/* the probe is what the contact sheet needs to space its frames, so its size is free even when unasked */
		if (cache_video_probe(job->fd, &job->probe))
			{
			job->done = static_cast<CacheDataType>(job->done | CACHE_LOADER_DIMENSIONS);
			}
		if (job->todo & CACHE_LOADER_SIMILARITY)
			{
			job->pixbuf = cache_sim_video_pixbuf(job->fd, job->probe.duration);
			}
		}

	if (job->todo & CACHE_LOADER_SIMILARITY && job->pixbuf)
		{
		job->sim = image_sim_new_from_pixbuf(job->pixbuf);
		job->done = static_cast<CacheDataType>(job->done | CACHE_LOADER_SIMILARITY);
		}

	if (job->todo & CACHE_LOADER_MD5SUM && md5_get_digest_from_file_utf8(job->fd->path, job->md5sum))
		{
		job->done = static_cast<CacheDataType>(job->done | CACHE_LOADER_MD5SUM);
		}
}

static void cache_loader_job_thread_run(gpointer data, gpointer)
{
	auto job = static_cast<CacheLoaderJob *>(data);

	if (!g_atomic_int_get(&job->cancelled)) cache_loader_job_work(job);

	g_idle_add(cache_loader_job_done_idle_cb, job);
}

/** Hands the worker whatever the loader still needs from it; FALSE if that is nothing. */
static gboolean cache_loader_job_start(CacheLoader *cl)
{
	const gboolean video = cl->fd->format_class == FORMAT_CLASS_VIDEO;
	gint todo = CACHE_LOADER_NONE;

	/* a still image that failed to decode has no pixbuf to take a grid from; a video's sheet is the job's own work */
	if (cl->todo_mask & CACHE_LOADER_SIMILARITY && !cl->cd->similarity && (cl->pixbuf || video)) todo |= CACHE_LOADER_SIMILARITY;
	if (cl->todo_mask & CACHE_LOADER_MD5SUM && !cl->cd->have_md5sum) todo |= CACHE_LOADER_MD5SUM;
	if (cl->todo_mask & CACHE_LOADER_DIMENSIONS && !cl->cd->dimensions && video) todo |= CACHE_LOADER_DIMENSIONS;

	if (todo == CACHE_LOADER_NONE) return FALSE;

	if (!cache_loader_pool)
		{
		const gint threads = worker_thread_limit();
		cache_loader_pool = g_thread_pool_new(cache_loader_job_thread_run, nullptr, threads, FALSE, nullptr);
		}

	auto job = g_new0(CacheLoaderJob, 1);
	job->fd = file_data_ref(cl->fd);
	job->cl = cl;
	job->todo = static_cast<CacheDataType>(todo);
	job->pixbuf = cl->pixbuf;
	cl->pixbuf = nullptr;

	cl->job = job;
	g_thread_pool_push(cache_loader_pool, job, nullptr);

	return TRUE;
}

static void cache_loader_job_cancel(CacheLoader *cl)
{
	if (!cl->job) return;

	g_atomic_int_set(&cl->job->cancelled, 1);
	cl->job->cl = nullptr;
	cl->job = nullptr;
}

/** The job reports by idle, so finishing must not also be scheduled when one was started. */
static void cache_loader_decoded(CacheLoader *cl)
{
	cl->idle_id = cache_loader_job_start(cl) ? 0 : g_idle_add(cache_loader_finish_idle_cb, cl);
}

/*
 * Still images: the ImageLoader decodes on its own thread pool and signals back on the main thread.
 */

static void cache_loader_decode_done_cb(ImageLoader *, gpointer data)
{
	auto cl = static_cast<CacheLoader *>(data);

	cl->pixbuf = image_loader_get_pixbuf(cl->il);
	if (cl->pixbuf) g_object_ref(cl->pixbuf);
	cache_loader_decoded(cl);
}

static void cache_loader_decode_error_cb(ImageLoader *, gpointer data)
{
	auto cl = static_cast<CacheLoader *>(data);

	cl->error = TRUE;
	cache_loader_decoded(cl);
}

static gboolean cache_loader_decode_process(CacheLoader *cl)
{
	/* a video is not decoded here: its similarity data comes from a contact sheet the job renders itself */
	if (cl->todo_mask & CACHE_LOADER_SIMILARITY && !cl->cd->similarity && !cl->error &&
	    cl->fd->format_class != FORMAT_CLASS_VIDEO)
		{
		cl->il = image_loader_new(cl->fd);
		g_signal_connect(G_OBJECT(cl->il), "error", (GCallback)cache_loader_decode_error_cb, cl);
		g_signal_connect(G_OBJECT(cl->il), "done", (GCallback)cache_loader_decode_done_cb, cl);
		if (image_loader_start(cl->il))
			{
			cl->idle_id = 0;
			return G_SOURCE_REMOVE;
			}

		cl->error = TRUE;
		}

	cache_loader_decoded(cl);

	return G_SOURCE_REMOVE;
}

static gboolean cache_loader_finish_process(CacheLoader *cl)
{
	/* a video's pixbuf is the contact sheet, not the frame; its dimensions mean nothing */
	if (cl->pixbuf && !cl->cd->dimensions && cl->fd->format_class != FORMAT_CLASS_VIDEO)
		{
		cache_sim_data_set_dimensions(cl->cd, gdk_pixbuf_get_width(cl->pixbuf),
						      gdk_pixbuf_get_height(cl->pixbuf));
		if (cl->todo_mask & CACHE_LOADER_DIMENSIONS)
			{
			cl->done_mask = static_cast<CacheDataType>(cl->done_mask | CACHE_LOADER_DIMENSIONS);
			}
		}

	image_loader_free(cl->il);
	cl->il = nullptr;
	if (cl->pixbuf) g_object_unref(cl->pixbuf);
	cl->pixbuf = nullptr;

	/* nothing was decoded, so the size comes from the header, which image_load_dimensions reads and no more */
	if (cl->todo_mask & CACHE_LOADER_DIMENSIONS && !cl->cd->dimensions)
		{
		if (!cl->error &&
		    image_load_dimensions(cl->fd, &cl->cd->width, &cl->cd->height))
			{
			cl->cd->dimensions = TRUE;
			cl->done_mask = static_cast<CacheDataType>(cl->done_mask | CACHE_LOADER_DIMENSIONS);
			}
		else
			{
			cl->error = TRUE;
			}
		}

	if (cache_sim_data_use_cache(cl->fd) &&
	    cl->done_mask != CACHE_LOADER_NONE)
		{
		cache_sim_data_save(cl->fd, cl->cd);
		}

	cl->idle_id = 0;

	if (cl->done_func)
		{
		cl->done_func(cl, cl->error, cl->done_data);
		}

	return G_SOURCE_REMOVE;
}

static gboolean cache_loader_decode_idle_cb(gpointer data)
{
	auto cl = static_cast<CacheLoader *>(data);

	return cache_loader_decode_process(cl);
}

static gboolean cache_loader_finish_idle_cb(gpointer data)
{
	auto cl = static_cast<CacheLoader *>(data);

	return cache_loader_finish_process(cl);
}

CacheLoader *cache_loader_new(FileData *fd, CacheDataType load_mask,
			      CacheLoader::DoneFunc done_func, gpointer done_data)
{
	CacheLoader *cl;

	if (!fd || !isfile(fd->path)) return nullptr;

	cl = g_new0(CacheLoader, 1);
	cl->fd = file_data_ref(fd);

	cl->done_func = done_func;
	cl->done_data = done_data;

	cl->cd = cache_sim_data_load(cl->fd);
	if (!cl->cd) cl->cd = cache_sim_data_new();

	cl->todo_mask = load_mask;
	cl->done_mask = CACHE_LOADER_NONE;

	cl->idle_id = g_idle_add(cache_loader_decode_idle_cb, cl);

	return cl;
}

void cache_loader_free(CacheLoader *cl)
{
	if (!cl) return;

	if (cl->idle_id)
		{
		g_source_remove_by_user_data(cl);
		cl->idle_id = 0;
		}

	cache_loader_job_cancel(cl);
	image_loader_free(cl->il);
	if (cl->pixbuf) g_object_unref(cl->pixbuf);
	cache_sim_data_free(cl->cd);

	file_data_unref(cl->fd);
	g_free(cl);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
