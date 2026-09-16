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

/* Every entry counts, not only the file types geeqie lists: a trashed file it does not list still holds its number. */
static gint file_util_safe_number(const gchar *trash_path)
{
	g_autofree gchar *trash_path_l = path_from_utf8(trash_path);
	g_autoptr(GDir) dir = g_dir_open(trash_path_l, 0, nullptr);
	if (!dir) return 0;

	gint n = 0;
	while (const gchar *name = g_dir_read_name(dir))
		{
		const auto v = static_cast<gint>(strtol(name, nullptr, 10));
		if (v >= n) n = v + 1;
		}

	return n;
}

/**
 * The trash is listed once for its next number, then counted on here; listing it for every file made deleting
 * n files take n^2 time. A number taken since, by another geeqie or by hand, is skipped: rename() would replace it.
 */
static gchar *file_util_safe_dest(const gchar *path)
{
	static gchar *counted_path = nullptr;
	static gint next = 0;

	const gchar *trash_path = options->file_ops.safe_delete_path;
	if (g_strcmp0(counted_path, trash_path) != 0)
		{
		g_free(counted_path);
		counted_path = g_strdup(trash_path);
		next = file_util_safe_number(trash_path);
		}

	while (true)
		{
		g_autofree gchar *prefix = g_strdup_printf("%06d_", next++);
		g_autofree gchar *base = filename_shorten(filename_from_path(path), strlen(prefix));
		g_autofree gchar *name = g_strconcat(prefix, base, nullptr);
		gchar *dest = g_build_filename(trash_path, name, NULL);
		if (!isname(dest)) return dest;
		g_free(dest);
		}
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
