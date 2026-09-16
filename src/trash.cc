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

#include "trash.h"

#include <cstdlib>
#include <cstring>

#include "debug.h"
#include "editors.h"
#include "filedata.h"
#include "intl.h"
#include "main-defines.h"
#include "options.h"
#include "typedefs.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"
#include "utilops.h"

/*
 *--------------------------------------------------------------------------
 * Safe Delete
 *--------------------------------------------------------------------------
 */

static gint file_util_safe_number()
{
	gint n = 0;
	GList *list;
	GList *work;
	FileData *dir_fd;

	dir_fd = file_data_new_dir(options->file_ops.safe_delete_path);
	if (!filelist_read(dir_fd, &list, nullptr))
		{
		file_data_unref(dir_fd);
		return 0;
		}
	file_data_unref(dir_fd);

	work = list;
	while (work)
		{
		FileData *fd;
		gint v;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		v = static_cast<gint>(strtol(fd->name, nullptr, 10));
		if (v >= n) n = v + 1;
		}

	filelist_free(list);

	return n;
}

static gchar *file_util_safe_dest(const gchar *path)
{
	gint n;
	gchar *name;
	gchar *dest;

	n = file_util_safe_number();
	g_autofree gchar *prefix = g_strdup_printf("%06d_", n);
	g_autofree gchar *base = filename_shorten(filename_from_path(path), strlen(prefix));
	name = g_strconcat(prefix, base, nullptr);
	dest = g_build_filename(options->file_ops.safe_delete_path, name, NULL);
	g_free(name);

	return dest;
}

gboolean file_util_move_to_trash(const gchar *path)
{
	static GenericDialog *gd = nullptr;
	const gchar *result = nullptr;

	if (!isfile(path)) return FALSE;

	if (!isdir(options->file_ops.safe_delete_path))
		{
		DEBUG_1("creating trash: %s", options->file_ops.safe_delete_path);
		if (!options->file_ops.safe_delete_path || !mkdir_utf8(options->file_ops.safe_delete_path, 0755))
			{
			result = _("Could not create folder");
			}
		}

	if (!result)
		{
		g_autofree gchar *dest = file_util_safe_dest(path);
		DEBUG_1("safe deleting %s to %s", path, dest);
		if (move_file(path, dest)) return TRUE;

		if (!access_file(path, W_OK)) result = _("Permission denied");
		}

	if (result && !gd)
		{
		g_autofree gchar *buf = g_strdup_printf(_("Unable to access or create the trash folder.\n\"%s\""), options->file_ops.safe_delete_path);
		gd = file_util_warning_dialog(result, buf, GQ_ICON_DIALOG_WARNING, nullptr);
		}

	return FALSE;
}

gchar *file_util_delete_status()
{
	if (is_valid_editor_command(CMD_DELETE)) return g_strdup(_("Deletion by external command"));
	if (options->file_ops.use_trash) return g_strdup(_("Using Geeqie Trash bin"));
	return g_strdup(_("Deleting without trash"));
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
