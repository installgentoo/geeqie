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

#ifndef FILEFILTER_H
#define FILEFILTER_H

#include <glib.h>

#include "typedefs.h"

struct FilterEntry {
	gchar *key;
	gchar *description;
	gchar *extensions;
	FileFormatClass file_class;
	gboolean enabled;
};

/**
 * @headerfile filter_get_list
 * you can change, but not add or remove entries from the returned list
 */
GList *filter_get_list();
void filter_remove_entry(FilterEntry *fe);

void filter_add_unique(const gchar *description, const gchar *extensions, FileFormatClass file_class, gboolean, gboolean, gboolean enabled);
void filter_add_defaults();
void filter_reset();
void filter_rebuild();
GList *filter_to_list(const gchar *extensions);

const gchar *registered_extension_from_path(const gchar *name);
gboolean filter_name_exists(const gchar *name);
gboolean filter_file_class(const gchar *name, FileFormatClass file_class);
FileFormatClass filter_file_get_class(const gchar *name);

void filter_write_list(GString *outstr, gint indent);
void filter_load_file_type(const gchar **attribute_names, const gchar **attribute_values);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
