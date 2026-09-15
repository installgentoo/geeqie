/*
 * Copyright (C) 2024 The Geeqie Team
 *
 * Author: Omari Stephens
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
 *
 *
 * FileList functionality for FileData.
 *
 */


#include "filedata.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

#include <glib.h>
#include "debug.h"
#include "filefilter.h"
#include "main.h"
#include "options.h"
#include "typedefs.h"
#include "ui-fileops.h"


/*
 *-----------------------------------------------------------------------------
 * the main filelist function
 *-----------------------------------------------------------------------------
 */
gboolean FileData::FileList::lists_file(const gchar *name)
{
	return filter_name_exists(name);
}

gboolean FileData::FileList::read_list_real(const gchar *dir_path, GList **files, GList **dirs, gboolean follow_symlinks)
{
	GList *dlist = nullptr;
	GList *flist = nullptr;

	g_assert(files || dirs);

	if (files) *files = nullptr;
	if (dirs) *dirs = nullptr;

	g_autofree gchar *pathl = path_from_utf8(dir_path);
	if (!pathl) return FALSE;

	DIR *dp = opendir(pathl);
	if (dp == nullptr) return FALSE;

	struct dirent *dir;
	while ((dir = readdir(dp)) != nullptr)
		{
		const gchar *name = dir->d_name;

		if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) continue;

		/* The stat is the cost of listing a large folder, so it is skipped for entries this listing would drop
		 * anyway: d_type tells directories apart without one, except DT_UNKNOWN (filesystems that do not fill it)
		 * and symlinks, whose target type needs the stat. */
		const gboolean may_be_dir = dir->d_type == DT_DIR || dir->d_type == DT_UNKNOWN ||
		                            (follow_symlinks && dir->d_type == DT_LNK);
		const gboolean wanted_file = files && dir->d_type != DT_DIR && lists_file(name);
		if (!(dirs && may_be_dir) && !wanted_file) continue;

		struct stat ent_sbuf;
		if (fstatat(dirfd(dp), name, &ent_sbuf, follow_symlinks ? 0 : AT_SYMLINK_NOFOLLOW) < 0)
			{
			if (errno == EOVERFLOW)
				{
				log_printf("stat(): EOVERFLOW, skip '%s/%s'", pathl, name);
				}
			continue;
			}

		if (S_ISDIR(ent_sbuf.st_mode) ? !dirs : !wanted_file) continue;

		g_autofree gchar *filepath = g_build_filename(pathl, name, NULL);
		if (S_ISDIR(ent_sbuf.st_mode))
			{
			dlist = g_list_prepend(dlist, file_data_new_local(filepath, &ent_sbuf));
			}
		else
			{
			flist = g_list_prepend(flist, file_data_new_local(filepath, &ent_sbuf));
			}
		}

	closedir(dp);

	if (dirs) *dirs = dlist;
	if (files) *files = flist;

	return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 * filelist sorting
 *-----------------------------------------------------------------------------
 */


gint FileData::FileList::sort_compare_filedata(
	const FileData *fa, const FileData *fb, SortSettings *settings)
{
	gint ret;
	if (!settings->ascending)
		{
		std::swap(fa, fb);
		}

	switch (settings->method)
		{
		case SORT_NAME:
			break;
		case SORT_SIZE:
			if (fa->size < fb->size) return -1;
			if (fa->size > fb->size) return 1;
			/* fall back to name */
			break;
		case SORT_TIME:
			if (fa->date < fb->date) return -1;
			if (fa->date > fb->date) return 1;
			/* fall back to name */
			break;
		case SORT_CTIME:
			if (fa->cdate < fb->cdate) return -1;
			if (fa->cdate > fb->cdate) return 1;
			/* fall back to name */
			break;
		case SORT_CLASS:
			if (fa->format_class < fb->format_class) return -1;
			if (fa->format_class > fb->format_class) return 1;
			/* fall back to name */
			break;
		case SORT_NUMBER:
			if (settings->case_sensitive)
				{
				ret = strcmp(fa->collate_key_name_natural,
					     fb->collate_key_name_natural);
			} else {
				ret = strcmp(fa->collate_key_name_nocase_natural,
					     fb->collate_key_name_nocase_natural);
			}

			if (ret != 0) return ret;
			/* fall back to name */
			break;
		default:
			break;
		}

	if (settings->case_sensitive)
		ret = strcmp(fa->collate_key_name, fb->collate_key_name);
	else
		ret = strcmp(fa->collate_key_name_nocase, fb->collate_key_name_nocase);

	if (ret != 0) return ret;

	/* do not return 0 unless the files are really the same
	   file_data_pool ensures that original_path is unique
	*/
	return strcmp(fa->original_path, fb->original_path);
}

gint FileData::FileList::sort_compare_filedata_full(const FileData *fa, const FileData *fb, SortType method, gboolean ascending, gboolean case_sensitive)
{
	SortSettings settings = {method, ascending, case_sensitive};
	return sort_compare_filedata(fa, fb, &settings);
}

gint FileData::FileList::sort_file_cb(gconstpointer a, gconstpointer b, gpointer data)
{
	return FileData::FileList::sort_compare_filedata(
                static_cast<const FileData *>(a),
                static_cast<const FileData *>(b),
                static_cast<SortSettings *>(data));
}

static GList *sort_full(GList *list, SortType method, gboolean ascending, gboolean case_sensitive, GCompareDataFunc cb)
{
	FileData::FileList::SortSettings settings = {method, ascending, case_sensitive};
	return g_list_sort_with_data(list, cb, &settings);
}

GList *FileData::FileList::sort(GList *list, SortType method, gboolean ascending, gboolean case_sensitive)
{
	return sort_full(list, method, ascending, case_sensitive, sort_file_cb);
}

gboolean FileData::FileList::read_list(FileData *dir_fd, GList **files, GList **dirs)
{
	return read_list_real(dir_fd->path, files, dirs, TRUE);
}

gboolean FileData::FileList::read_list_lstat(FileData *dir_fd, GList **files, GList **dirs)
{
	return read_list_real(dir_fd->path, files, dirs, FALSE);
}

void FileData::FileList::free_list(GList *list)
{
	GList *work;

	work = list;
	while (work)
		{
		::file_data_unref((FileData *)work->data);
		work = work->next;
		}

	g_list_free(list);
}


GList *FileData::FileList::copy(GList *list)
{
	GList *new_list = nullptr;

	for (GList *work = list; work; work = work->next)
		{
		auto fd = static_cast<FileData *>(work->data);

		new_list = g_list_prepend(new_list, ::file_data_ref(fd));
		}

	return g_list_reverse(new_list);
}

GList *FileData::FileList::from_path_list(GList *list)
{
	GList *new_list = nullptr;
	GList *work;

	work = list;
	while (work)
		{
		gchar *path;

		path = static_cast<gchar *>(work->data);
		work = work->next;

		new_list = g_list_prepend(new_list, file_data_new(path));
		}

	return g_list_reverse(new_list);
}

GList *FileData::FileList::to_path_list(GList *list)
{
	GList *new_list = nullptr;
	GList *work;

	work = list;
	while (work)
		{
		FileData *fd;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		new_list = g_list_prepend(new_list, g_strdup(fd->path));
		}

	return g_list_reverse(new_list);
}

GList *FileData::FileList::filter(GList *list, gboolean is_dir_list)
{
	GList *work;

	if (is_dir_list) return list;

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		GList *link = work;
		work = work->next;

		if (!lists_file(fd->name))
			{
			list = g_list_remove_link(list, link);
			::file_data_unref(fd);
			g_list_free(link);
			}
		}

	return list;
}

/*
 *-----------------------------------------------------------------------------
 * filelist recursive
 *-----------------------------------------------------------------------------
 */

gint FileData::FileList::sort_path_cb(gconstpointer a, gconstpointer b)
{
	return CASE_SORT(((FileData *)a)->path, ((FileData *)b)->path);
}

GList *FileData::FileList::sort_path(GList *list)
{
	return g_list_sort(list, sort_path_cb);
}

void FileData::FileList::recursive_append(GList **list, GList *dirs)
{
	GList *work;

	work = dirs;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		GList *f;
		GList *d;

		if (read_list(fd, &f, &d))
			{
			f = filter(f, FALSE);
			f = sort_path(f);
			*list = g_list_concat(*list, f);

			d = filter(d, TRUE);
			d = sort_path(d);
			recursive_append(list, d);
			free_list(d);
			}

		work = work->next;
		}
}

GList *FileData::FileList::recursive(FileData *dir_fd)
{
	GList *list;
	GList *d;

	if (!read_list(dir_fd, &list, &d)) return nullptr;
	list = filter(list, FALSE);
	list = sort_path(list);

	d = filter(d, TRUE);
	d = sort_path(d);
	recursive_append(&list, d);
	free_list(d);

	return list;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
