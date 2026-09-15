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

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

#include <glib.h>
#include "debug.h"
#include "filefilter.h"
#include "main.h"
#include "misc.h"
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

struct FileData::FileList::ListEntry
{
	gsize name_offset; /**< into the listing's name arena */
	guchar d_type;

	/* set by list_entries_make */
	FileData *fd; /**< unregistered (file_data_alloc) */
	struct stat *st_unconverted; /**< instead of fd, when the name needs path_to_utf8, which may show a dialog */
	gint stat_errno;
};

struct FileData::FileList::ListRequest
{
	const gchar *names;
	gint dir_fd;
	gboolean follow_symlinks;
	gboolean want_files;
	gboolean want_dirs;
	const gchar *dir_path;
	const gchar *dir_separator;
	FileDataContext *context;
};

/* Runs on worker threads, so it touches nothing shared: fstatat on a directory fd, g_filename_to_utf8, the
 * extension table and file_data_alloc are thread-safe, while the file pool, path_to_utf8 and log_printf are left
 * to the main thread. */
void FileData::FileList::list_entries_make(ListEntry *begin, ListEntry *end, const ListRequest *request)
{
	for (ListEntry *entry = begin; entry < end; entry++)
		{
		const gchar *name = request->names + entry->name_offset;

		/* The stat is the cost of listing a large folder, so it is skipped for entries this listing would drop
		 * anyway: d_type tells directories apart without one, except DT_UNKNOWN (filesystems that do not fill
		 * it) and symlinks, whose target type needs the stat. */
		const gboolean may_be_dir = entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN ||
		                            (request->follow_symlinks && entry->d_type == DT_LNK);
		const gboolean wanted_file = request->want_files && entry->d_type != DT_DIR && lists_file(name);
		if (!(request->want_dirs && may_be_dir) && !wanted_file) continue;

		struct stat st;
		if (fstatat(request->dir_fd, name, &st, request->follow_symlinks ? 0 : AT_SYMLINK_NOFOLLOW) < 0)
			{
			entry->stat_errno = errno;
			continue;
			}
		if (S_ISDIR(st.st_mode) ? !request->want_dirs : !wanted_file) continue;

		g_autofree gchar *name_utf8 = g_filename_to_utf8(name, -1, nullptr, nullptr, nullptr);
		if (!name_utf8)
			{
			entry->st_unconverted = static_cast<struct stat *>(g_memdup2(&st, sizeof(st)));
			continue;
			}

		g_autofree gchar *path_utf8 = g_strconcat(request->dir_path, request->dir_separator, name_utf8, NULL);
		entry->fd = file_data_alloc(path_utf8, &st, request->context);
		}
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

	const gsize dir_path_len = strlen(dir_path);
	std::vector<ListEntry> entries;
	std::vector<gchar> names;

	struct dirent *dir;
	while ((dir = readdir(dp)) != nullptr)
		{
		const gchar *name = dir->d_name;
		if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) continue;
		if (!dirs && dir->d_type == DT_DIR) continue;

		ListEntry entry{};
		entry.name_offset = names.size();
		entry.d_type = dir->d_type;
		entries.push_back(entry);
		names.insert(names.end(), name, name + strlen(name) + 1);
		}

	const ListRequest request{names.data(), dirfd(dp), follow_symlinks, files != nullptr, dirs != nullptr, dir_path,
	                          (dir_path_len > 0 && dir_path[dir_path_len - 1] == G_DIR_SEPARATOR) ? "" : G_DIR_SEPARATOR_S,
	                          FileData::DefaultFileDataContext()};

	/* Entries are made in parallel: on a cold cache each stat waits for the disk, and SSDs serve concurrent reads
	 * almost as fast as a single one. Small listings stay on this thread, where a thread start would cost more. */
	constexpr gsize ENTRIES_PER_THREAD = 1024;
	const gsize thread_count = CLAMP(entries.size() / ENTRIES_PER_THREAD, 1, static_cast<gsize>(MAX(get_cpu_cores(), 1)));
	const gsize chunk = (entries.size() + thread_count - 1) / thread_count;

	std::vector<std::thread> workers;
	for (gsize t = 1; t < thread_count; t++)
		{
		workers.emplace_back(list_entries_make, entries.data() + std::min(t * chunk, entries.size()),
		                     entries.data() + std::min((t + 1) * chunk, entries.size()), &request);
		}
	list_entries_make(entries.data(), entries.data() + std::min(chunk, entries.size()), &request);
	for (std::thread &worker : workers) worker.join();

	for (ListEntry &entry : entries)
		{
		if (entry.stat_errno == EOVERFLOW)
			{
			log_printf("stat(): EOVERFLOW, skip '%s/%s'", pathl, names.data() + entry.name_offset);
			}

		FileData *fd = entry.fd;
		if (fd)
			{
			struct stat st{};
			st.st_size = fd->size;
			st.st_mtime = fd->date;
			st.st_ctime = fd->cdate;
			st.st_mode = fd->mode;

			FileData *known = file_data_lookup(fd->original_path, &st, request.context);
			if (known)
				{
				file_data_discard(fd);
				fd = known;
				}
			else
				{
				file_data_register(fd);
				}
			}
		else if (entry.st_unconverted)
			{
			g_autofree gchar *name_utf8 = path_to_utf8(names.data() + entry.name_offset);
			g_autofree gchar *path_utf8 = g_strconcat(dir_path, request.dir_separator, name_utf8, NULL);
			fd = file_data_new(path_utf8, entry.st_unconverted);
			g_free(entry.st_unconverted);
			}
		else
			{
			continue;
			}

		if (S_ISDIR(fd->mode))
			{
			dlist = g_list_prepend(dlist, fd);
			}
		else
			{
			flist = g_list_prepend(flist, fd);
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


/* Compares character by character in Unicode code point order, not locale collation: collation needs a
 * transformed copy of every name, which was the main cost of listing a large folder with non-ASCII names.
 * Code point order is deterministic, so the same names always land in the same places. */
gint FileData::FileList::compare_names(const gchar *a, const gchar *b, gboolean case_sensitive, gboolean natural)
{
	const gchar *const a_start = a;

	while (*a && *b)
		{
		/* Equal bytes are equal characters under any comparison mode, so a shared prefix is skipped without
		 * decoding; the first difference is rewound to where its character, and for natural order its digit
		 * run, begins, since only those compare as a whole. */
		if (*a == *b)
			{
			while (*a && *a == *b)
				{
				a++;
				b++;
				}
			while (a > a_start && (static_cast<guchar>(*a) & 0xC0) == 0x80)
				{
				a--;
				b--;
				}
			if (natural)
				{
				while (a > a_start && g_ascii_isdigit(a[-1]))
					{
					a--;
					b--;
					}
				}
			if (!*a || !*b) break;
			}

		if (natural && g_ascii_isdigit(*a) && g_ascii_isdigit(*b))
			{
			/* a digit run compares by value: longer without leading zeros is larger, then digit by digit */
			while (*a == '0') a++;
			while (*b == '0') b++;
			const gchar *a_end = a;
			const gchar *b_end = b;
			while (g_ascii_isdigit(*a_end)) a_end++;
			while (g_ascii_isdigit(*b_end)) b_end++;

			if (a_end - a != b_end - b) return (a_end - a < b_end - b) ? -1 : 1;
			const gint ret = strncmp(a, b, a_end - a);
			if (ret != 0) return ret;

			a = a_end;
			b = b_end;
			continue;
			}

		gunichar ca;
		gunichar cb;
		if (static_cast<guchar>(*a) < 0x80 && static_cast<guchar>(*b) < 0x80)
			{
			ca = static_cast<guchar>(*a++);
			cb = static_cast<guchar>(*b++);
			}
		else
			{
			/* a byte that is not valid UTF-8 compares by its own value */
			ca = g_utf8_get_char_validated(a, -1);
			if (ca >= static_cast<gunichar>(-2)) ca = static_cast<guchar>(*a++); else a = g_utf8_next_char(a);
			cb = g_utf8_get_char_validated(b, -1);
			if (cb >= static_cast<gunichar>(-2)) cb = static_cast<guchar>(*b++); else b = g_utf8_next_char(b);
			}

		if (!case_sensitive)
			{
			ca = g_unichar_tolower(ca);
			cb = g_unichar_tolower(cb);
			}
		if (ca != cb) return (ca < cb) ? -1 : 1;
		}

	return (*a != '\0') - (*b != '\0');
}

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
			ret = compare_names(fa->name, fb->name, settings->case_sensitive, TRUE);
			if (ret != 0) return ret;
			/* fall back to name */
			break;
		default:
			break;
		}

	ret = compare_names(fa->name, fb->name, settings->case_sensitive, FALSE);
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
