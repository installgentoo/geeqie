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

#include "cache.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include <glib/gstdio.h>
#include <sqlite3.h>

#include "debug.h"
#include "filedata.h"
#include "main-defines.h"
#include "options.h"
#include "similar.h"
#include "thumb-standard.h"
#include "ui-fileops.h"

/**
 * @file
 * Similarity cache: one SQLite database, one row per source file.
 *
 *   path    source file path (UTF-8), unique
 *   mtime   source mtime when the row was written; a row whose mtime differs from the file is stale
 *   width, height   image dimensions, NULL if unknown
 *   md5     16-byte digest, NULL if unknown
 *   grid    3072 bytes: the 32x32 avg_r, avg_g, avg_b planes, NULL if unknown
 *
 * The grid is stored raw; image_sim_alternate_processing() is applied after loading.
 */

namespace
{

constexpr gint SIM_GRID_BYTES = 3 * 1024;

gboolean cache_video_tools_available()
{
	static gsize initialized = 0;
	static gboolean available = FALSE;

	if (g_once_init_enter(&initialized))
		{
		g_autofree gchar *ffmpeg = g_find_program_in_path("ffmpeg");
		g_autofree gchar *ffprobe = g_find_program_in_path("ffprobe");

		available = ffmpeg && ffprobe;
		if (!available)
			{
			log_printf("cache: ffmpeg and/or ffprobe not found, video similarity generation disabled\n");
			}

		g_once_init_leave(&initialized, 1);
		}

	return available;
}

gboolean cache_video_run_command(const gchar *command, gchar **stdout_text)
{
	gchar *stderr_text = nullptr;
	gint exit_status = -1;

	if (!g_spawn_command_line_sync(command, stdout_text, &stderr_text, &exit_status, nullptr))
		{
		log_printf("cache: failed to run command: %s\n", command);
		g_free(stderr_text);
		return FALSE;
		}

	if (!g_spawn_check_exit_status(exit_status, nullptr))
		{
		log_printf("cache: command failed: %s\n", command);
		if (stderr_text && *stderr_text) log_printf("cache: stderr: %s\n", stderr_text);
		g_free(stderr_text);
		return FALSE;
		}

	g_free(stderr_text);
	return TRUE;
}

/* ffprobe prints one line per stream/format entry, "N/A" when a container has no value; the first positive one wins. */
gboolean cache_video_parse_positive_double(const gchar *text, gdouble *value)
{
	if (!text || !value) return FALSE;

	g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
	for (gchar **line = lines; line && *line; line++)
		{
		gchar *endptr = nullptr;
		const gchar *trimmed = g_strstrip(*line);
		const gdouble parsed = g_ascii_strtod(trimmed, &endptr);
		if (*trimmed && endptr && *endptr == '\0' && parsed > 0.0)
			{
			*value = parsed;
			return TRUE;
			}
		}

	return FALSE;
}

} // namespace

GdkPixbuf *cache_sim_video_pixbuf(FileData *fd)
{
	if (!fd || !fd->path) return nullptr;
	if (!cache_video_tools_available()) return nullptr;

	g_autofree gchar *video_path = g_shell_quote(fd->path);
	g_autofree gchar *duration_cmd = g_strdup_printf("ffprobe -v error -select_streams v:0 -show_entries stream=duration:format=duration -of default=noprint_wrappers=1:nokey=1 %s", video_path);
	g_autofree gchar *duration_out = nullptr;
	gdouble duration = 0.0;

	if (!cache_video_run_command(duration_cmd, &duration_out) || !cache_video_parse_positive_double(duration_out, &duration))
		{
		log_printf("cache: no duration for %s, cannot sample frames for similarity\n", fd->path);
		return nullptr;
		}

	const gdouble fps_interval = (duration + 1.8) / 36.0;

	gchar *tmp_file = nullptr;
	const gint fd_out = g_file_open_tmp("geeqie-sim-video-XXXXXX.jpeg", &tmp_file, nullptr);
	if (fd_out < 0) return nullptr;
	close(fd_out);

	g_autofree gchar *tmp_path = tmp_file;
	g_autofree gchar *tmp_path_quoted = g_shell_quote(tmp_path);
	g_autofree gchar *ffmpeg_cmd = g_strdup_printf(
		"ffmpeg -hide_banner -loglevel error -i %s -frames:v 1 -vf \"fps=1/%.6f,scale=160:120,tile=6x6\" -an -y %s",
		video_path, fps_interval, tmp_path_quoted);

	GdkPixbuf *pixbuf = nullptr;
	if (cache_video_run_command(ffmpeg_cmd, nullptr))
		{
		GError *error = nullptr;
		pixbuf = gdk_pixbuf_new_from_file(tmp_path, &error);
		if (error)
			{
			log_printf("cache: cannot load generated video similarity pixbuf for %s: %s\n", fd->path, error->message);
			g_error_free(error);
			}
		}

	g_unlink(tmp_path);
	return pixbuf;
}

/*
 *-------------------------------------------------------------------
 * database
 *-------------------------------------------------------------------
 */

namespace
{

/* One connection; every statement runs under the mutex, so any thread may call in. */
struct SimDb
{
	GMutex mutex;
	sqlite3 *db = nullptr;
	sqlite3_stmt *select = nullptr;
	sqlite3_stmt *upsert = nullptr;
	sqlite3_stmt *remove = nullptr;
	sqlite3_stmt *move = nullptr;
};

gboolean sim_db_exec(sqlite3 *db, const gchar *sql)
{
	gchar *error = nullptr;
	if (sqlite3_exec(db, sql, nullptr, nullptr, &error) == SQLITE_OK) return TRUE;

	log_printf("similarity cache: %s: %s\n", sql, error);
	sqlite3_free(error);
	return FALSE;
}

gboolean sim_db_prepare(sqlite3 *db, const gchar *sql, sqlite3_stmt **stmt)
{
	if (sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, stmt, nullptr) == SQLITE_OK) return TRUE;

	log_printf("similarity cache: %s: %s\n", sql, sqlite3_errmsg(db));
	return FALSE;
}

/* Opened on first use; nullptr if the database cannot be opened, in which case nothing is cached. */
SimDb *sim_db()
{
	static SimDb *instance = nullptr;
	static gsize initialized = 0;

	if (g_once_init_enter(&initialized))
		{
		const gchar *path = get_sim_cache_path();
		g_autofree gchar *dir = g_path_get_dirname(path);
		g_autofree gchar *pathl = path_from_utf8(path);
		sqlite3 *db = nullptr;

		recursive_mkdir_if_not_exists(dir, 0755);

		/* NOMUTEX: SimDb::mutex already serialises every use of the connection.
		 * The busy timeout comes first: switching to WAL takes a lock another geeqie instance may hold. */
		if (sqlite3_open_v2(pathl, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr) == SQLITE_OK &&
		    sqlite3_busy_timeout(db, 5000) == SQLITE_OK &&
		    sim_db_exec(db, "PRAGMA journal_mode=WAL;"
		                    "PRAGMA synchronous=NORMAL;"
		                    "CREATE TABLE IF NOT EXISTS sim("
		                    "path TEXT NOT NULL UNIQUE, mtime INTEGER NOT NULL,"
		                    "width INTEGER, height INTEGER, md5 BLOB, grid BLOB);"))
			{
			auto s = new SimDb();
			g_mutex_init(&s->mutex);
			s->db = db;
			if (sim_db_prepare(db, "SELECT mtime, width, height, md5, grid FROM sim WHERE path = ?", &s->select) &&
			    sim_db_prepare(db, "INSERT OR REPLACE INTO sim(path, mtime, width, height, md5, grid) VALUES(?, ?, ?, ?, ?, ?)", &s->upsert) &&
			    sim_db_prepare(db, "DELETE FROM sim WHERE path = ?", &s->remove) &&
			    sim_db_prepare(db, "UPDATE OR REPLACE sim SET path = ? WHERE path = ?", &s->move))
				{
				instance = s;
				}
			}
		else
			{
			log_printf("similarity cache: cannot open %s: %s\n", path, db ? sqlite3_errmsg(db) : "out of memory");
			sqlite3_close(db);
			}

		g_once_init_leave(&initialized, 1);
		}

	return instance;
}

/* Runs one bound statement to completion; the caller holds the mutex. */
gboolean sim_db_step_done(SimDb *s, sqlite3_stmt *stmt)
{
	const gboolean ok = sqlite3_step(stmt) == SQLITE_DONE;
	if (!ok) log_printf("similarity cache: %s\n", sqlite3_errmsg(s->db));

	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return ok;
}

} // namespace

const gchar *get_sim_cache_path()
{
#if USE_XDG
	static gchar *path = g_build_filename(xdg_cache_home_get(), GQ_APPNAME_LC, "similarity.db", NULL);
#else
	static gchar *path = g_build_filename(get_rc_dir(), "similarity.db", NULL);
#endif

	return path;
}

const gchar *get_thumbnails_standard_cache_dir()
{
	static gchar *thumbnails_standard_cache_dir = g_build_filename(xdg_cache_home_get(),
	                                                               THUMB_FOLDER_GLOBAL, NULL);

	return thumbnails_standard_cache_dir;
}

/*
 *-------------------------------------------------------------------
 * sim cache data
 *-------------------------------------------------------------------
 */

CacheData *cache_sim_data_new()
{
	return g_new0(CacheData, 1);
}

void cache_sim_data_free(CacheData *cd)
{
	if (!cd) return;

	image_sim_free(cd->sim);
	g_free(cd);
}

void cache_sim_data_set_dimensions(CacheData *cd, gint w, gint h)
{
	if (!cd) return;

	cd->width = w;
	cd->height = h;
	cd->dimensions = TRUE;
}

void cache_sim_data_set_md5sum(CacheData *cd, const guchar digest[16])
{
	gint i;

	if (!cd) return;

	for (i = 0; i < 16; i++)
		{
		cd->md5sum[i] = digest[i];
		}
	cd->have_md5sum = TRUE;
}

void cache_sim_data_set_similarity(CacheData *cd, ImageSimilarityData *sd)
{
	if (!cd || !sd || !sd->filled) return;

	if (!cd->sim) cd->sim = image_sim_new();

	memcpy(cd->sim->avg_r, sd->avg_r, 1024);
	memcpy(cd->sim->avg_g, sd->avg_g, 1024);
	memcpy(cd->sim->avg_b, sd->avg_b, 1024);
	cd->sim->filled = TRUE;

	cd->similarity = TRUE;
}

gboolean cache_sim_data_filled(ImageSimilarityData *sd)
{
	if (!sd) return FALSE;
	return sd->filled;
}

CacheData *cache_sim_data_load(FileData *fd)
{
	if (!fd || !fd->path) return nullptr;

	SimDb *s = sim_db();
	if (!s) return nullptr;

	const time_t mtime = filetime(fd->path);
	CacheData *cd = nullptr;

	g_mutex_lock(&s->mutex);
	sqlite3_bind_text(s->select, 1, fd->path, -1, SQLITE_STATIC);

	if (sqlite3_step(s->select) == SQLITE_ROW && sqlite3_column_int64(s->select, 0) == mtime)
		{
		cd = cache_sim_data_new();

		if (sqlite3_column_type(s->select, 1) != SQLITE_NULL)
			{
			cache_sim_data_set_dimensions(cd, sqlite3_column_int(s->select, 1), sqlite3_column_int(s->select, 2));
			}

		if (sqlite3_column_bytes(s->select, 3) == 16)
			{
			cache_sim_data_set_md5sum(cd, static_cast<const guchar *>(sqlite3_column_blob(s->select, 3)));
			}

		if (sqlite3_column_bytes(s->select, 4) == SIM_GRID_BYTES)
			{
			auto grid = static_cast<const guint8 *>(sqlite3_column_blob(s->select, 4));
			cd->sim = image_sim_new();
			memcpy(cd->sim->avg_r, grid, 1024);
			memcpy(cd->sim->avg_g, grid + 1024, 1024);
			memcpy(cd->sim->avg_b, grid + 2048, 1024);
			cd->sim->filled = TRUE;
			cd->similarity = TRUE;
			}
		}

	sqlite3_reset(s->select);
	sqlite3_clear_bindings(s->select);
	g_mutex_unlock(&s->mutex);

	return cd;
}

gboolean cache_sim_data_save(FileData *fd, CacheData *cd)
{
	if (!fd || !fd->path || !cd) return FALSE;

	SimDb *s = sim_db();
	if (!s) return FALSE;

	guint8 grid[SIM_GRID_BYTES];
	const gboolean have_grid = cd->similarity && cd->sim && cd->sim->filled;
	if (have_grid)
		{
		memcpy(grid, cd->sim->avg_r, 1024);
		memcpy(grid + 1024, cd->sim->avg_g, 1024);
		memcpy(grid + 2048, cd->sim->avg_b, 1024);
		}

	const time_t mtime = filetime(fd->path);

	g_mutex_lock(&s->mutex);
	sqlite3_bind_text(s->upsert, 1, fd->path, -1, SQLITE_STATIC);
	sqlite3_bind_int64(s->upsert, 2, mtime);
	if (cd->dimensions)
		{
		sqlite3_bind_int(s->upsert, 3, cd->width);
		sqlite3_bind_int(s->upsert, 4, cd->height);
		}
	if (cd->have_md5sum) sqlite3_bind_blob(s->upsert, 5, cd->md5sum, 16, SQLITE_STATIC);
	if (have_grid) sqlite3_bind_blob(s->upsert, 6, grid, SIM_GRID_BYTES, SQLITE_STATIC);

	const gboolean ok = sim_db_step_done(s, s->upsert);
	g_mutex_unlock(&s->mutex);

	return ok;
}

gboolean cache_sim_data_use_cache(FileData *fd)
{
	return options->thumbnails.enable_caching ||
	       (fd && fd->format_class == FORMAT_CLASS_VIDEO);
}

void cache_sim_moved(const gchar *source, const gchar *dest)
{
	SimDb *s = sim_db();
	if (!s || !source || !dest) return;

	g_mutex_lock(&s->mutex);
	sqlite3_bind_text(s->move, 1, dest, -1, SQLITE_STATIC);
	sqlite3_bind_text(s->move, 2, source, -1, SQLITE_STATIC);
	sim_db_step_done(s, s->move);
	g_mutex_unlock(&s->mutex);
}

void cache_sim_removed(const gchar *path)
{
	SimDb *s = sim_db();
	if (!s || !path) return;

	g_mutex_lock(&s->mutex);
	sqlite3_bind_text(s->remove, 1, path, -1, SQLITE_STATIC);
	sim_db_step_done(s, s->remove);
	g_mutex_unlock(&s->mutex);
}

gint cache_sim_clean()
{
	SimDb *s = sim_db();
	if (!s) return 0;

	struct Row
	{
		std::string path;
		time_t mtime;
	};
	std::vector<Row> rows;

	g_mutex_lock(&s->mutex);
	sqlite3_stmt *all = nullptr;
	if (sim_db_prepare(s->db, "SELECT path, mtime FROM sim", &all))
		{
		while (sqlite3_step(all) == SQLITE_ROW)
			{
			rows.push_back({reinterpret_cast<const char *>(sqlite3_column_text(all, 0)), sqlite3_column_int64(all, 1)});
			}
		sqlite3_finalize(all);
		}
	g_mutex_unlock(&s->mutex);

	/* the stat pass is the slow part and runs without the lock, so loads and saves are not held up */
	std::vector<const Row *> stale;
	for (const Row &row : rows)
		{
		struct stat st;
		if (!stat_utf8(row.path.c_str(), &st) || st.st_mtime != row.mtime) stale.push_back(&row);
		}

	if (stale.empty()) return 0;

	g_mutex_lock(&s->mutex);
	sim_db_exec(s->db, "BEGIN");
	for (const Row *row : stale)
		{
		sqlite3_bind_text(s->remove, 1, row->path.c_str(), -1, SQLITE_STATIC);
		sim_db_step_done(s, s->remove);
		}
	sim_db_exec(s->db, "COMMIT");
	sim_db_exec(s->db, "VACUUM");
	g_mutex_unlock(&s->mutex);

	return static_cast<gint>(stale.size());
}
