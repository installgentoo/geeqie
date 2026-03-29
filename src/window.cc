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

#include "window.h"

#include <array>
#include <cstdio>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>

#include <config.h>

#include "debug.h"
#include "intl.h"
#include "main-defines.h"
#include "main.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-utildlg.h"

GtkWidget *window_new(const gchar *role, const gchar *icon, const gchar *icon_file, const gchar *subtitle)
{
	gchar *title;
	GtkWidget *window;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	if (!window) return nullptr;

	if (subtitle)
		{
		title = g_strdup_printf("%s - %s", subtitle, GQ_APPNAME);
		}
	else
		{
		title = g_strdup_printf("%s", GQ_APPNAME);
		}

	gtk_window_set_title(GTK_WINDOW(window), title);
	g_free(title);

	window_set_icon(window, icon, icon_file);
	gtk_window_set_role(GTK_WINDOW(window), role);

	if (options->hide_window_decorations)
		{
		gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
		}

	return window;
}

void window_set_icon(GtkWidget *window, const gchar *icon, const gchar *file)
{
	if (!icon && !file) icon = PIXBUF_INLINE_ICON;

	if (icon)
		{
		GdkPixbuf *pixbuf;

		pixbuf = pixbuf_inline(icon);
		if (pixbuf)
			{
			gtk_window_set_icon(GTK_WINDOW(window), pixbuf);
			g_object_unref(pixbuf);
			}
		}
	else
		{
		gtk_window_set_icon_from_file(GTK_WINDOW(window), file, nullptr);
		}
}

gboolean window_maximized(GtkWidget *window)
{
	GdkWindowState state;

	if (!window || !gtk_widget_get_window(window)) return FALSE;

	state = gdk_window_get_state(gtk_widget_get_window(window));
	return !!(state & GDK_WINDOW_STATE_MAXIMIZED);
}
