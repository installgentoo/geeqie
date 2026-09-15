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
#include <utime.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <config.h>

#include <glib/gstdio.h>

#include "debug.h"
#include "filedata.h"
#include "intl.h"
#include "main-defines.h"
#include "md5-util.h"
#include "options.h"
#include "secure-save.h"
#include "similar.h"
#include "thumb-standard.h"
#include "ui-fileops.h"


/**
 * @file
 *-------------------------------------------------------------------
 * Cache data file format:
 *-------------------------------------------------------------------
 *
 * SIMcache \n
 * #comment \n
 * URI=<file:// URI of the source; the cache file is named by its md5, so without this line the file is an orphan> \n
 * Dimensions=[<width> x <height>] \n
 * Date=[<value in time_t format, or -1 if no embedded date>] \n
 * MD5sum=[<32 character ascii text digest>] \n
 * SimilarityGrid[32 x 32]=<3072 bytes of data (1024 pixels in RGB format, 1 pixel is 24bits)>
 *
 * The first line (9 bytes) indicates it is a SIMcache format file. (new line char must exist) \n
 * Comment lines starting with a # are ignored up to a new line. \n
 * All data lines should end with a new line char. \n
 * Format is very strict, data must begin with the char immediately following '='. \n
 * Currently SimilarityGrid is always assumed to be 32 x 32 RGB. \n
 */

namespace
{

/* Names the cache file (md5 of this) and is stored inside it; the two must agree or maintenance cannot trace the file back. */
gchar *cache_source_uri(const gchar *source)
{
	g_autofree gchar *source_path = path_from_utf8(source);
	return g_filename_to_uri(source_path, nullptr, nullptr);
}

struct CachePathParts
{
	CachePathParts(CacheType cache_type)
	{
		rc = get_thumbnails_cache_dir();

		switch (cache_type)
			{
			case CACHE_TYPE_THUMB:
				ext = GQ_CACHE_EXT_THUMB;
				break;
			case CACHE_TYPE_SIM:
				ext = GQ_CACHE_EXT_SIM;
				break;
			}
	}

	gchar *build_path_rc(const gchar *source) const
	{
		g_autofree gchar *uri = cache_source_uri(source);
		if (!uri) return nullptr;

		g_autofree gchar *md5_text = md5_get_string(reinterpret_cast<const guchar *>(uri), strlen(uri));
		if (!md5_text) return nullptr;

		g_autofree gchar *name = g_strconcat(md5_text, ext, nullptr);
		return g_build_filename(rc, name, nullptr);
	}

	const gchar *rc = nullptr;
	const gchar *ext = nullptr;
};

constexpr gint CACHE_LOAD_LINE_NOISE = 8;

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

namespace
{

gchar *cache_get_location(CacheType type, const gchar *source, gint include_name, mode_t *mode)
{
	if (!source) return nullptr;

	const CachePathParts cache{type};
	if (mode) *mode = 0755;
	if (!include_name) return g_strdup(cache.rc);
	return cache.build_path_rc(source);
}

} // namespace

/*
 *-------------------------------------------------------------------
 * sim cache data
 *-------------------------------------------------------------------
 */

CacheData *cache_sim_data_new()
{
	CacheData *cd;

	cd = g_new0(CacheData, 1);

	return cd;
}

void cache_sim_data_free(CacheData *cd)
{
	if (!cd) return;

	g_free(cd->path);
	g_free(cd->uri);
	image_sim_free(cd->sim);
	g_free(cd);
}

/*
 *-------------------------------------------------------------------
 * sim cache write
 *-------------------------------------------------------------------
 */

static gboolean cache_sim_write_dimensions(SecureSaveInfo *ssi, CacheData *cd)
{
	if (!cd || !cd->dimensions) return FALSE;

	secure_fprintf(ssi, "Dimensions=[%d x %d]\n", cd->width, cd->height);

	return TRUE;
}

static gboolean cache_sim_write_md5sum(SecureSaveInfo *ssi, CacheData *cd)
{
	gchar *text;

	if (!cd || !cd->have_md5sum) return FALSE;

	text = md5_digest_to_text(cd->md5sum);
	secure_fprintf(ssi, "MD5sum=[%s]\n", text);
	g_free(text);

	return TRUE;
}

static gboolean cache_sim_write_similarity(SecureSaveInfo *ssi, CacheData *cd)
{
	guint x;
	guint y;
	guint8 buf[3 * 32];

	if (!cd || !cd->similarity || !cd->sim || !cd->sim->filled) return FALSE;

	secure_fprintf(ssi, "SimilarityGrid[32 x 32]=");
	for (y = 0; y < 32; y++)
		{
		guint s = y * 32;
		guint8 *avg_r = &cd->sim->avg_r[s];
		guint8 *avg_g = &cd->sim->avg_g[s];
		guint8 *avg_b = &cd->sim->avg_b[s];
		guint n = 0;

		for (x = 0; x < 32; x++)
			{
			buf[n++] = avg_r[x];
			buf[n++] = avg_g[x];
			buf[n++] = avg_b[x];
			}

		secure_fwrite(buf, sizeof(buf), 1, ssi);
		}

	secure_fputc(ssi, '\n');

	return TRUE;
}

gboolean cache_sim_data_save(CacheData *cd)
{
	SecureSaveInfo *ssi;
	gchar *pathl;

	if (!cd || !cd->path) return FALSE;

	pathl = path_from_utf8(cd->path);
	ssi = secure_open(pathl);
	g_free(pathl);

	if (!ssi)
		{
		log_printf("Unable to save sim cache data: %s\n", cd->path);
		return FALSE;
		}

	secure_fprintf(ssi, "SIMcache\n#%s %s\n", PACKAGE, VERSION);
	if (cd->uri) secure_fprintf(ssi, "URI=%s\n", cd->uri);
	cache_sim_write_dimensions(ssi, cd);
	cache_sim_write_md5sum(ssi, cd);
	cache_sim_write_similarity(ssi, cd);

	if (secure_close(ssi))
		{
		log_printf(_("error saving sim cache data: %s\nerror: %s\n"), cd->path,
			    secsave_strerror(secsave_errno));
		return FALSE;
		}

	return TRUE;
}

/*
 *-------------------------------------------------------------------
 * sim cache read
 *-------------------------------------------------------------------
 */

static gboolean cache_sim_read_skipline(FILE *f, gint s)
{
	if (!f) return FALSE;

	if (fseek(f, 0 - s, SEEK_CUR) == 0)
		{
		gchar b;
		while (fread(&b, sizeof(b), 1, f) == 1)
			{
			if (b == '\n') return TRUE;
			}
		return TRUE;
		}

	return FALSE;
}

static gboolean cache_sim_read_uri(FILE *f, gchar *buf, gint s, CacheData *cd)
{
	if (!f || !buf || !cd) return FALSE;

	if (s < 4 || strncmp("URI=", buf, 4) != 0) return FALSE;

	if (fseek(f, 4 - s, SEEK_CUR) != 0) return FALSE;

	GString *uri = g_string_new(nullptr);
	gchar b;
	while (fread(&b, sizeof(b), 1, f) == 1 && b != '\n')
		{
		g_string_append_c(uri, b);
		}

	g_free(cd->uri);
	cd->uri = g_string_free(uri, FALSE);
	return TRUE;
}

static gboolean cache_sim_read_dimensions(FILE *f, gchar *buf, gint s, CacheData *cd)
{
	if (!f || !buf || !cd) return FALSE;

	if (s < 10 || strncmp("Dimensions", buf, 10) != 0) return FALSE;

	if (fseek(f, - s, SEEK_CUR) == 0)
		{
		gchar b;
		gchar buf[1024];
		gsize p = 0;
		gint w;
		gint h;

		b = 'X';
		while (b != '[')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			}
		while (b != ']' && p < sizeof(buf) - 1)
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			buf[p] = b;
			p++;
			}

		while (b != '\n')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) break;
			}

		buf[p] = '\0';
		if (sscanf(buf, "%d x %d", &w, &h) != 2) return FALSE;

		cd->width = w;
		cd->height = h;
		cd->dimensions = TRUE;

		return TRUE;
		}

	return FALSE;
}

static gboolean cache_sim_read_md5sum(FILE *f, gchar *buf, gint s, CacheData *cd)
{
	if (!f || !buf || !cd) return FALSE;

	if (s < 8 || strncmp("MD5sum", buf, 6) != 0) return FALSE;

	if (fseek(f, - s, SEEK_CUR) == 0)
		{
		gchar b;
		gchar buf[64];
		gsize p = 0;

		b = 'X';
		while (b != '[')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			}
		while (b != ']' && p < sizeof(buf) - 1)
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			buf[p] = b;
			p++;
			}
		while (b != '\n')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) break;
			}

		buf[p] = '\0';
		cd->have_md5sum = md5_digest_from_text(buf, cd->md5sum);

		return TRUE;
		}

	return FALSE;
}

static gboolean cache_sim_read_similarity(FILE *f, gchar *buf, gint s, CacheData *cd)
{
	if (!f || !buf || !cd) return FALSE;

	if (s < 11 || strncmp("Similarity", buf, 10) != 0) return FALSE;

	if (strncmp("Grid[32 x 32]", buf + 10, 13) != 0) return FALSE;

	if (fseek(f, - s, SEEK_CUR) == 0)
		{
		gchar b;
		guint8 pixel_buf[3];
		ImageSimilarityData *sd;
		gint x;
		gint y;

		b = 'X';
		while (b != '=')
			{
			if (fread(&b, sizeof(b), 1, f) != 1) return FALSE;
			}

		if (cd->sim)
			{
			/* use current sim that may already contain data we will not touch here */
			sd = cd->sim;
			cd->sim = nullptr;
			cd->similarity = FALSE;
			}
		else
			{
			sd = image_sim_new();
			}

		for (y = 0; y < 32; y++)
			{
			gint s = y * 32;
			for (x = 0; x < 32; x++)
				{
				if (fread(&pixel_buf, sizeof(pixel_buf), 1, f) != 1)
					{
					image_sim_free(sd);
					return FALSE;
					}
				sd->avg_r[s + x] = pixel_buf[0];
				sd->avg_g[s + x] = pixel_buf[1];
				sd->avg_b[s + x] = pixel_buf[2];
				}
			}

		if (fread(&b, sizeof(b), 1, f) == 1)
			{
			if (b != '\n') fseek(f, -1, SEEK_CUR);
			}

		cd->sim = sd;
		cd->sim->filled = TRUE;
		cd->similarity = TRUE;

		return TRUE;
		}

	return FALSE;
}

CacheData *cache_sim_data_load(const gchar *path)
{
	FILE *f;
	CacheData *cd = nullptr;
	gchar buf[32];
	gint success = CACHE_LOAD_LINE_NOISE;
	gchar *pathl;

	if (!path) return nullptr;

	pathl = path_from_utf8(path);
	f = fopen(pathl, "r");
	g_free(pathl);

	if (!f) return nullptr;

	cd = cache_sim_data_new();
	cd->path = g_strdup(path);

	if (fread(&buf, sizeof(gchar), 9, f) != 9 ||
	    strncmp(buf, "SIMcache", 8) != 0)
		{
		DEBUG_1("%s is not a cache file", cd->path);
		success = 0;
		}

	while (success > 0)
		{
		gint s;
		s = fread(&buf, sizeof(gchar), sizeof(buf), f);

		if (s < 1)
			{
			success = 0;
			}
		else
			{
			if (!cache_sim_read_uri(f, buf, s, cd) &&
			    !cache_sim_read_dimensions(f, buf, s, cd) &&
			    !cache_sim_read_md5sum(f, buf, s, cd) &&
			    !cache_sim_read_similarity(f, buf, s, cd))
				{
				if (!cache_sim_read_skipline(f, s))
					{
					success = 0;
					}
				else
					{
					success--;
					}
				}
			else
				{
				success = CACHE_LOAD_LINE_NOISE;
				}
			}
		}

	fclose(f);

	if (!cd->dimensions &&
	    !cd->have_md5sum &&
	    !cd->similarity)
		{
		cache_sim_data_free(cd);
		cd = nullptr;
		}

	return cd;
}

/*
 *-------------------------------------------------------------------
 * sim cache setting
 *-------------------------------------------------------------------
 */

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

CacheData *cache_sim_data_load_from_file(FileData *fd)
{
	if (!fd || !fd->path) return nullptr;

	g_autofree gchar *path = cache_find_location(CACHE_TYPE_SIM, fd->path);
	if (!path) return nullptr;
	if (filetime(fd->path) != filetime(path)) return nullptr;

	return cache_sim_data_load(path);
}

gboolean cache_sim_data_save_to_file(FileData *fd, CacheData *cd)
{
	if (!fd || !fd->path || !cd) return FALSE;

	g_autofree gchar *base = cache_create_location(CACHE_TYPE_SIM, fd->path);
	if (!base) return FALSE;

	g_free(cd->path);
	cd->path = cache_get_location(CACHE_TYPE_SIM, fd->path);
	g_free(cd->uri);
	cd->uri = cache_source_uri(fd->path);
	if (!cache_sim_data_save(cd)) return FALSE;

	filetime_set(cd->path, filetime(fd->path));
	return TRUE;
}

gboolean cache_sim_data_use_cache(FileData *fd)
{
	return options->thumbnails.enable_caching ||
	       (fd && fd->format_class == FORMAT_CLASS_VIDEO);
}

gboolean cache_sim_file_valid(const gchar *cache_path)
{
	CacheData *cd = cache_sim_data_load(cache_path);
	if (!cd) return FALSE;

	gboolean valid = FALSE;
	if (cd->uri)
		{
		g_autofree gchar *source = g_filename_from_uri(cd->uri, nullptr, nullptr);
		g_autofree gchar *source_utf8 = source ? path_to_utf8(source) : nullptr;
		valid = source_utf8 && isfile(source_utf8) && filetime(source_utf8) == filetime(cache_path);
		}

	cache_sim_data_free(cd);
	return valid;
}

/*
 *-------------------------------------------------------------------
 * cache path location utils
 *-------------------------------------------------------------------
 */

gchar *cache_create_location(CacheType cache_type, const gchar *source)
{
	mode_t mode = 0755;
	g_autofree gchar *path = cache_get_location(cache_type, source, FALSE, &mode);

	if (!recursive_mkdir_if_not_exists(path, mode))
		{
		log_printf("Failed to create cache dir %s\n", path);
		return nullptr;
		}

	return g_steal_pointer(&path);
}

gchar *cache_get_location(CacheType cache_type, const gchar *source)
{
	return cache_get_location(cache_type, source, TRUE, nullptr);
}

gchar *cache_find_location(CacheType type, const gchar *source)
{
	gchar *path;

	if (!source) return nullptr;

	const CachePathParts cache{type};
	path = cache.build_path_rc(source);
	if (!path) return nullptr;

	if (!isfile(path))
		{
		g_free(path);
		path = nullptr;
		}

	return path;
}

const gchar *get_thumbnails_cache_dir()
{
#if USE_XDG
	static gchar *thumbnails_cache_dir = g_build_filename(xdg_cache_home_get(), GQ_APPNAME_LC, GQ_CACHE_THUMB, NULL);
#else
	static gchar *thumbnails_cache_dir = g_build_filename(get_rc_dir(), GQ_CACHE_THUMB, NULL);
#endif

	return thumbnails_cache_dir;
}

const gchar *get_thumbnails_standard_cache_dir()
{
	static gchar *thumbnails_standard_cache_dir = g_build_filename(xdg_cache_home_get(),
	                                                               THUMB_FOLDER_GLOBAL, NULL);

	return thumbnails_standard_cache_dir;
}
