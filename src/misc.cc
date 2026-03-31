/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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

#include "misc.h"

#include <sys/stat.h>
#include <unistd.h>

#include <config.h>

#if !HAVE__NL_TIME_FIRST_WEEKDAY
#  include <clocale>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <glib-object.h>
#include <grp.h>
#include <langinfo.h>
#include <pwd.h>

#include "debug.h"
#include "options.h"

namespace
{

constexpr int BUFSIZE = 128;

constexpr gint CELL_HEIGHT_OVERRIDE = 512;

} // namespace

gdouble get_zoom_increment()
{
	return ((options->image.zoom_increment != 0) ? static_cast<gdouble>(options->image.zoom_increment) / 100.0 : 1.0);
}

gchar *utf8_validate_or_convert(const gchar *text)
{
	gint len;

	if (!text) return nullptr;

	len = strlen(text);
	if (!g_utf8_validate(text, len, nullptr))
		return g_convert(text, len, "UTF-8", "ISO-8859-1", nullptr, nullptr, nullptr);

	return g_strdup(text);
}

gint utf8_compare(const gchar *s1, const gchar *s2, gboolean case_sensitive)
{
	gchar *s1_key;
	gchar *s2_key;
	gchar *s1_t;
	gchar *s2_t;
	gint ret;

	g_assert(g_utf8_validate(s1, -1, nullptr));
	g_assert(g_utf8_validate(s2, -1, nullptr));

	if (!case_sensitive)
		{
		s1_t = g_utf8_casefold(s1, -1);
		s2_t = g_utf8_casefold(s2, -1);
		}
	else
		{
		s1_t = const_cast<gchar *>(s1);
		s2_t = const_cast<gchar *>(s2);
		}

	s1_key = g_utf8_collate_key(s1_t, -1);
	s2_key = g_utf8_collate_key(s2_t, -1);

	ret = strcmp(s1_key, s2_key);

	g_free(s1_key);
	g_free(s2_key);

	if (!case_sensitive)
		{
		g_free(s1_t);
		g_free(s2_t);
		}

	return ret;
}

gint gq_gtk_tree_iter_utf8_collate(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gint sort_column_id)
{
	g_autofree gchar *str_a = nullptr;
	gtk_tree_model_get(model, a,
	                   sort_column_id, &str_a,
	                   -1);

	g_autofree gchar *str_b = nullptr;
	gtk_tree_model_get(model, b,
	                   sort_column_id, &str_b,
	                   -1);

	if (str_a && str_b) return g_utf8_collate(str_a, str_b);

	if (!str_a && !str_b) return 0;

	return (!str_a) ? -1 : 1;
}

/* Borrowed from gtkfilesystemunix.c */
gchar *expand_tilde(const gchar *filename)
{
#ifndef G_OS_UNIX
	return g_strdup(filename);
#else
	const gchar *notilde;
	const gchar *slash;
	const gchar *home;

	if (filename[0] != '~')
		return g_strdup(filename);

	notilde = filename + 1;
	slash = strchr(notilde, G_DIR_SEPARATOR);
	if (slash == notilde || !*notilde)
		{
		home = g_get_home_dir();
		if (!home)
			return g_strdup(filename);
		}
	else
		{
		gchar *username;
		struct passwd *passwd;

		if (slash)
			username = g_strndup(notilde, slash - notilde);
		else
			username = g_strdup(notilde);

		passwd = getpwnam(username);
		g_free(username);

		if (!passwd)
			return g_strdup(filename);

		home = passwd->pw_dir;
		}

	if (slash)
		return g_build_filename(home, G_DIR_SEPARATOR_S, slash + 1, NULL);

	return g_build_filename(home, G_DIR_SEPARATOR_S, NULL);
#endif
}

/* Run a command like system() but may output debug messages. */
int runcmd(const gchar *cmd)
{
#if 1
	return system(cmd);
	return 0;
#else
	/* For debugging purposes */
	int retval = -1;
	FILE *in;

	DEBUG_1("Running command: %s", cmd);

	in = popen(cmd, "r");
	if (in)
		{
		int status;
		const gchar *msg;
		gchar buf[2048];

		while (fgets(buf, sizeof(buf), in) != NULL )
			{
			DEBUG_1("Output: %s", buf);
			}

		status = pclose(in);

		if (WIFEXITED(status))
			{
			msg = "Command terminated with exit code";
			retval = WEXITSTATUS(status);
			}
		else if (WIFSIGNALED(status))
			{
			msg = "Command was killed by signal";
			retval = WTERMSIG(status);
			}
		else
			{
			msg = "pclose() returned";
			retval = status;
			}

		DEBUG_1("%s : %d\n", msg, retval);
	}

	return retval;
#endif
}

gint get_cpu_cores()
{
    return sysconf(_SC_NPROCESSORS_ONLN);
}

void convert_gdkcolor_to_gdkrgba(gpointer data, GdkRGBA *gdk_rgba)
{
	auto gdk_color = static_cast<GdkColor *>(data);

	gdk_rgba->red = CLAMP((double)gdk_color->red / 65535.0, 0.0, 1.0);
	gdk_rgba->green = CLAMP((double)gdk_color->green / 65535.0, 0.0, 1.0);
	gdk_rgba->blue = CLAMP((double)gdk_color->blue / 65535.0, 0.0, 1.0);
	gdk_rgba->alpha = 1.0;
}

void gq_gtk_entry_set_text(GtkEntry *entry, const gchar *text)
{
	GtkEntryBuffer *buffer;

	buffer = gtk_entry_get_buffer(entry);
	gtk_entry_buffer_set_text(buffer, text, static_cast<gint>(g_utf8_strlen(text, -1)));
}

const gchar *gq_gtk_entry_get_text(GtkEntry *entry)
{
	GtkEntryBuffer *buffer;

	buffer = gtk_entry_get_buffer(entry);
	return gtk_entry_buffer_get_text(buffer);
}

void gq_gtk_grid_attach(GtkGrid *grid, GtkWidget *child, guint left_attach, guint right_attach, guint top_attach, guint bottom_attach, GtkAttachOptions, GtkAttachOptions, guint, guint)
{
	gtk_grid_attach(grid, child, left_attach, top_attach, right_attach - left_attach, bottom_attach - top_attach);
}

void gq_gtk_grid_attach_default(GtkGrid *grid, GtkWidget *child, guint left_attach, guint right_attach, guint top_attach, guint bottom_attach )
{
	gtk_grid_attach(grid, child, left_attach, top_attach, right_attach - left_attach, bottom_attach - top_attach);
}

/**
 * @brief This overrides the low default of a GtkCellRenderer from 100 to CELL_HEIGHT_OVERRIDE, something sane for our purposes
 */
void cell_renderer_height_override(GtkCellRenderer *renderer)
{
	GParamSpec *spec;

	spec = g_object_class_find_property(G_OBJECT_GET_CLASS(G_OBJECT(renderer)), "height");
	if (spec && G_IS_PARAM_SPEC_INT(spec))
		{
		GParamSpecInt *spec_int;

		spec_int = G_PARAM_SPEC_INT(spec);
		if (spec_int->maximum < CELL_HEIGHT_OVERRIDE) spec_int->maximum = CELL_HEIGHT_OVERRIDE;
		}
}

/**
 * @brief Set cursor for widget's window.
 * @param widget Widget for which cursor is set
 * @param icon Cursor type from GdkCursorType.
 *        Value -1 means using the cursor of its parent window.
 * @todo Use std::optional for icon since C++17 instead of special -1 value
 */
void widget_set_cursor(GtkWidget *widget, gint icon)
{
	GdkWindow *window = gtk_widget_get_window(widget);
	if (!window) return;

	GdkCursor *cursor = nullptr;

	if (icon != -1)
		{
		GdkDisplay *display = gdk_display_get_default();
		cursor = gdk_cursor_new_for_display(display, static_cast<GdkCursorType>(icon));
		}

	gdk_window_set_cursor(window, cursor);

	if (cursor) g_object_unref(cursor);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
