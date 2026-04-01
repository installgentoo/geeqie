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

#include "image-overlay.h"

#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <pango/pango.h>

#include "debug.h"
#include "exif.h"
#include "filedata.h"
#include "image-load.h"
#include "image.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "options.h"
#include "osd.h"
#include "pixbuf-renderer.h"
#include "pixbuf-util.h"
#include "typedefs.h"

struct OverlayStateData {
	ImageWindow *imd;
	ImageState changed_states;
	NotifyType notify;

	gboolean show;
	OverlayRendererFlags origin;

	gint ovl_info;

	gint x;
	gint y;

	guint idle_id; /* event source id */
	gulong destroy_id;
};


void set_image_overlay_template_string(gchar **template_string, const gchar *value)
{
	g_assert(template_string);

	g_free(*template_string);
	*template_string = g_strdup(value);
}

void set_image_overlay_font_string(gchar **font_string, const gchar *value)
{
	g_assert(font_string);

	g_free(*font_string);
	*font_string = g_strdup(value);
}

static OverlayStateData *image_get_osd_data(ImageWindow *imd)
{
	OverlayStateData *osd;

	if (!imd) return nullptr;

	g_assert(imd->pr);

	osd = static_cast<OverlayStateData *>(g_object_get_data(G_OBJECT(imd->pr), "IMAGE_OVERLAY_DATA"));
	return osd;
}

static void image_set_osd_data(ImageWindow *imd, OverlayStateData *osd)
{
	g_assert(imd);
	g_assert(imd->pr);
	g_object_set_data(G_OBJECT(imd->pr), "IMAGE_OVERLAY_DATA", osd);
}

static GdkPixbuf *image_osd_info_render(OverlayStateData *osd)
{
	GdkPixbuf *pixbuf = nullptr;
	gint width;
	gint height;
	PangoLayout *layout;
	const gchar *name;
	gchar *text;
	ImageWindow *imd = osd->imd;
	FileData *fd = image_get_fd(imd);
	PangoFontDescription *font_desc;

	if (!fd) return nullptr;

	name = image_get_name(imd);
	if (name)
	{
		text = image_osd_mkinfo(options->image_overlay.template_string, imd);
	} else {
		/* When does this occur ?? */
		text = g_markup_escape_text(_("Untitled"), -1);
	}

	font_desc = pango_font_description_from_string(options->image_overlay.font);
	layout = gtk_widget_create_pango_layout(imd->pr, nullptr);
	pango_layout_set_font_description(layout, font_desc);

	pango_layout_set_markup(layout, text, -1);
	g_free(text);

	pango_layout_get_pixel_size(layout, &width, &height);
	/* with empty text width is set to 0, but not height) */
	if (width == 0)
		height = 0;
	else if (height == 0)
		width = 0;
	if (width > 0) width += 10;
	if (height > 0) height += 10;

	if (width > 0 && height > 0)
		{
		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
		pixbuf_set_rect_fill(pixbuf, 3, 3, width-6, height-6, options->image_overlay.background_red, options->image_overlay.background_green,
															options->image_overlay.background_blue, options->image_overlay.background_alpha);
		pixbuf_set_rect(pixbuf, 0, 0, width, height, 240, 240, 240, 80, 1, 1, 1, 1);
		pixbuf_set_rect(pixbuf, 1, 1, width-2, height-2, 240, 240, 240, 130, 1, 1, 1, 1);
		pixbuf_set_rect(pixbuf, 2, 2, width-4, height-4, 240, 240, 240, 180, 1, 1, 1, 1);
		pixbuf_pixel_set(pixbuf, 0, 0, 0, 0, 0, 0);
		pixbuf_pixel_set(pixbuf, width - 1, 0, 0, 0, 0, 0);
		pixbuf_pixel_set(pixbuf, 0, height - 1, 0, 0, 0, 0);
		pixbuf_pixel_set(pixbuf, width - 1, height - 1, 0, 0, 0, 0);

		pixbuf_draw_layout(pixbuf, layout, 5, 5,
		                   options->image_overlay.text_red, options->image_overlay.text_green, options->image_overlay.text_blue, options->image_overlay.text_alpha);
	}

	g_object_unref(G_OBJECT(layout));

	return pixbuf;
}

static gint image_overlay_add(ImageWindow *imd, GdkPixbuf *pixbuf, gint x, gint y,
			      OverlayRendererFlags flags)
{
	return pixbuf_renderer_overlay_add(PIXBUF_RENDERER(imd->pr), pixbuf, x, y, flags);
}

static void image_overlay_set(ImageWindow *imd, gint id, GdkPixbuf *pixbuf, gint x, gint y)
{
	pixbuf_renderer_overlay_set(PIXBUF_RENDERER(imd->pr), id, pixbuf, x, y);
}

static void image_overlay_remove(ImageWindow *imd, gint id)
{
	pixbuf_renderer_overlay_remove(PIXBUF_RENDERER(imd->pr), id);
}

static void image_osd_info_show(OverlayStateData *osd, GdkPixbuf *pixbuf)
{
	if (osd->ovl_info == 0)
		{
		osd->ovl_info = image_overlay_add(osd->imd, pixbuf, osd->x, osd->y, osd->origin);
		}
	else
		{
		image_overlay_set(osd->imd, osd->ovl_info, pixbuf, osd->x, osd->y);
		}
}

static void image_osd_info_hide(OverlayStateData *osd)
{
	if (osd->ovl_info == 0) return;

	image_overlay_remove(osd->imd, osd->ovl_info);
	osd->ovl_info = 0;
}

static gboolean image_osd_update_cb(gpointer data)
{
	auto osd = static_cast<OverlayStateData *>(data);

	if (osd->show)
		{
		/* redraw when the image was changed */
		if (osd->changed_states & IMAGE_STATE_IMAGE)
			{
			GdkPixbuf *pixbuf;

			pixbuf = image_osd_info_render(osd);
			if (pixbuf)
				{
				image_osd_info_show(osd, pixbuf);
				g_object_unref(pixbuf);
				}
			else
				{
				image_osd_info_hide(osd);
				}
			}
		}
	else
		{
		image_osd_info_hide(osd);
		}

	osd->changed_states = IMAGE_STATE_NONE;
	osd->notify = static_cast<NotifyType>(0);
	osd->idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void image_osd_update_schedule(OverlayStateData *osd, gboolean force)
{
	if (force) osd->changed_states = static_cast<ImageState>(osd->changed_states | IMAGE_STATE_IMAGE);

	if (!osd->idle_id)
		{
		osd->idle_id = g_idle_add_full(G_PRIORITY_HIGH, image_osd_update_cb, osd, nullptr);
		}
}

void image_osd_update(ImageWindow *imd)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd) return;

	image_osd_update_schedule(osd, TRUE);
}

static void image_osd_state_cb(ImageWindow *, ImageState state, gpointer data)
{
	auto osd = static_cast<OverlayStateData *>(data);

	osd->changed_states = static_cast<ImageState>(osd->changed_states | state);
	image_osd_update_schedule(osd, FALSE);
}

static void image_osd_free(OverlayStateData *osd)
{
	if (!osd) return;

	if (osd->idle_id) g_source_remove(osd->idle_id);

	if (osd->imd)
		{
		image_set_osd_data(osd->imd, nullptr);
		g_signal_handler_disconnect(osd->imd->pr, osd->destroy_id);

		image_set_state_func(osd->imd, nullptr, nullptr);

		image_osd_info_hide(osd);
		}

	g_free(osd);
}

static void image_osd_destroy_cb(GtkWidget *, gpointer data)
{
	auto osd = static_cast<OverlayStateData *>(data);

	osd->imd = nullptr;
	image_osd_free(osd);
}

static void image_osd_enable(ImageWindow *imd, gboolean show)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd)
		{
		osd = g_new0(OverlayStateData, 1);
		osd->imd = imd;
		osd->show = FALSE;
		osd->x = options->image_overlay.x;
		osd->y = options->image_overlay.y;
		osd->origin = OVL_RELATIVE;

		osd->destroy_id = g_signal_connect(G_OBJECT(imd->pr), "destroy",
						   G_CALLBACK(image_osd_destroy_cb), osd);
		image_set_osd_data(imd, osd);

		image_set_state_func(osd->imd, image_osd_state_cb, osd);
		}

	if (show != osd->show)
		image_osd_update_schedule(osd, TRUE);

	osd->show = show;
}

void image_osd_set(ImageWindow *imd, gboolean show)
{
	if (!imd) return;

	image_osd_enable(imd, show);
}

gboolean image_osd_get(ImageWindow *imd)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	return osd ? osd->show : FALSE;
}

void image_osd_copy_status(ImageWindow *src, ImageWindow *dest)
{
	image_osd_set(dest, image_osd_get(src));
}
