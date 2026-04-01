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

#include "pixbuf-renderer.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "debug.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "renderer-tiles.h"

/* comment this out if not using this from within Geeqie
 * defining GQ_BUILD does these things:
 *   - Sets the shift-click scroller pixbuf to a nice icon instead of a black box
 */
#define GQ_BUILD 1

#ifdef GQ_BUILD
#include "exif.h"
#include "pixbuf-util.h"
#else
enum ExifOrientationType {
	EXIF_ORIENTATION_UNKNOWN	= 0,
	EXIF_ORIENTATION_TOP_LEFT	= 1,
	EXIF_ORIENTATION_TOP_RIGHT	= 2,
	EXIF_ORIENTATION_BOTTOM_RIGHT	= 3,
	EXIF_ORIENTATION_BOTTOM_LEFT	= 4,
	EXIF_ORIENTATION_LEFT_TOP	= 5,
	EXIF_ORIENTATION_RIGHT_TOP	= 6,
	EXIF_ORIENTATION_RIGHT_BOTTOM	= 7,
	EXIF_ORIENTATION_LEFT_BOTTOM	= 8
};
#endif

namespace
{

/* distance to drag mouse to disable image flip */
constexpr gint PR_DRAG_SCROLL_THRESHHOLD = 4;

/* increase pan rate when holding down shift */
constexpr gint PR_PAN_SHIFT_MULTIPLIER = 6;

} // namespace

/* default min and max zoom */
#define PR_ZOOM_MIN (-32.0)
#define PR_ZOOM_MAX 32.0

/* scroller config */
enum {
	PR_SCROLLER_UPDATES_PER_SEC = 30,
	PR_SCROLLER_DEAD_ZONE = 6
};

enum {
	SIGNAL_ZOOM = 0,
	SIGNAL_CLICKED,
	SIGNAL_SCROLL_NOTIFY,
	SIGNAL_RENDER_COMPLETE,
	SIGNAL_DRAG,
	SIGNAL_UPDATE_PIXEL,
	SIGNAL_COUNT
};

enum {
	PROP_0,
	PROP_ZOOM_MIN,
	PROP_ZOOM_MAX,
	PROP_ZOOM_QUALITY,
	PROP_ZOOM_2PASS,
	PROP_SCROLL_RESET,
	PROP_DELAY_FLIP,
	PROP_LOADING,
	PROP_COMPLETE,
	PROP_CACHE_SIZE_DISPLAY,
	PROP_CACHE_SIZE_TILES
};

enum PrZoomFlags {
	PR_ZOOM_NONE		= 0,
	PR_ZOOM_FORCE 		= 1 << 0,
	PR_ZOOM_NEW		= 1 << 1,
	PR_ZOOM_CENTER		= 1 << 2,
	PR_ZOOM_INVALIDATE	= 1 << 3,
	PR_ZOOM_LAZY		= 1 << 4  /* wait with redraw for pixbuf_renderer_area_changed */
};

static guint signals[SIGNAL_COUNT] = { 0 };
static GtkEventBoxClass *parent_class = nullptr;



static void pixbuf_renderer_class_init(PixbufRendererClass *renderer_class);
static void pixbuf_renderer_init(PixbufRenderer *pr);
static void pixbuf_renderer_finalize(GObject *object);
static void pixbuf_renderer_set_property(GObject *object, guint prop_id,
					 const GValue *value, GParamSpec *pspec);
static void pixbuf_renderer_get_property(GObject *object, guint prop_id,
					 GValue *value, GParamSpec *pspec);
static void pr_scroller_timer_set(PixbufRenderer *pr, gboolean start);


static void pr_source_tile_free_all(PixbufRenderer *pr);

static void pr_zoom_sync(PixbufRenderer *pr, gdouble zoom,
			 PrZoomFlags flags, gint px, gint py);

static void pr_signals_connect(PixbufRenderer *pr);
static void pr_size_cb(GtkWidget *widget, GtkAllocation *allocation, gpointer data);


/*
 *-------------------------------------------------------------------
 * Pixbuf Renderer object
 *-------------------------------------------------------------------
 */

static void pixbuf_renderer_class_init_wrapper(void *g_class, void *)
{
	pixbuf_renderer_class_init(static_cast<PixbufRendererClass *>(g_class));
}

static void pixbuf_renderer_init_wrapper(PixbufRenderer *pr, void *)
{
	pixbuf_renderer_init(pr);
}

GType pixbuf_renderer_get_type()
{
	static const GTypeInfo pixbuf_renderer_info = {
	    sizeof(PixbufRendererClass), /* class_size */
	    nullptr,		/* base_init */
	    nullptr,		/* base_finalize */
	    static_cast<GClassInitFunc>(pixbuf_renderer_class_init_wrapper),
	    nullptr,		/* class_finalize */
	    nullptr,		/* class_data */
	    sizeof(PixbufRenderer), /* instance_size */
	    0,		/* n_preallocs */
	    reinterpret_cast<GInstanceInitFunc>(pixbuf_renderer_init_wrapper), /* instance_init */
	    nullptr,		/* value_table */
	};
	static GType pixbuf_renderer_type = g_type_register_static(GTK_TYPE_EVENT_BOX, "PixbufRenderer",
	                                                           &pixbuf_renderer_info, static_cast<GTypeFlags>(0));

	return pixbuf_renderer_type;
}

static void pixbuf_renderer_class_init(PixbufRendererClass *renderer_class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(renderer_class);

	parent_class = static_cast<GtkEventBoxClass *>(g_type_class_peek_parent(renderer_class));

	gobject_class->set_property = pixbuf_renderer_set_property;
	gobject_class->get_property = pixbuf_renderer_get_property;

	gobject_class->finalize = pixbuf_renderer_finalize;

	g_object_class_install_property(gobject_class,
					PROP_ZOOM_MIN,
					g_param_spec_double("zoom_min",
							    "Zoom minimum",
							    nullptr,
							    -1000.0,
							    1000.0,
							    PR_ZOOM_MIN,
							    static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_ZOOM_MAX,
					g_param_spec_double("zoom_max",
							    "Zoom maximum",
							    nullptr,
							    -1000.0,
							    1000.0,
							    PR_ZOOM_MIN,
							    static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_ZOOM_QUALITY,
					g_param_spec_uint("zoom_quality",
							  "Zoom quality",
							  nullptr,
							  GDK_INTERP_NEAREST,
							  GDK_INTERP_BILINEAR,
							  GDK_INTERP_BILINEAR,
							  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_ZOOM_2PASS,
					g_param_spec_boolean("zoom_2pass",
							     "2 pass zoom",
							     nullptr,
							     TRUE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
	                                PROP_SCROLL_RESET,
	                                g_param_spec_uint("scroll_reset",
	                                                  "New image scroll reset",
	                                                  nullptr,
	                                                  ScrollReset::TOPLEFT,
	                                                  ScrollReset::NOCHANGE,
	                                                  ScrollReset::TOPLEFT,
	                                                  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_DELAY_FLIP,
					g_param_spec_boolean("delay_flip",
							     "Delay image update",
							     nullptr,
							     FALSE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_LOADING,
					g_param_spec_boolean("loading",
							     "Image actively loading",
							     nullptr,
							     FALSE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_COMPLETE,
					g_param_spec_boolean("complete",
							     "Image rendering complete",
							     nullptr,
							     FALSE,
							     static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_CACHE_SIZE_DISPLAY,
					g_param_spec_uint("cache_display",
							  "Display cache size MiB",
							  nullptr,
							  0,
							  128,
							  PR_CACHE_SIZE_DEFAULT,
							  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	g_object_class_install_property(gobject_class,
					PROP_CACHE_SIZE_TILES,
					g_param_spec_uint("cache_tiles",
							  "Tile cache count",
							  "Number of tiles to retain in memory at any one time.",
							  0,
							  256,
							  PR_CACHE_SIZE_DEFAULT,
							  static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_WRITABLE)));

	signals[SIGNAL_ZOOM] =
		g_signal_new("zoom",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, zoom),
			     nullptr, nullptr,
			     g_cclosure_marshal_VOID__DOUBLE,
			     G_TYPE_NONE, 1,
			     G_TYPE_DOUBLE);

	signals[SIGNAL_CLICKED] =
		g_signal_new("clicked",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, clicked),
			     nullptr, nullptr,
			     g_cclosure_marshal_VOID__BOXED,
			     G_TYPE_NONE, 1,
			     GDK_TYPE_EVENT);

	signals[SIGNAL_SCROLL_NOTIFY] =
		g_signal_new("scroll-notify",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, scroll_notify),
			     nullptr, nullptr,
			     g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);

	signals[SIGNAL_RENDER_COMPLETE] =
		g_signal_new("render-complete",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, render_complete),
			     nullptr, nullptr,
			     g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);

	signals[SIGNAL_DRAG] =
		g_signal_new("drag",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, drag),
			     nullptr, nullptr,
			     g_cclosure_marshal_VOID__BOXED,
			     G_TYPE_NONE, 1,
			     GDK_TYPE_EVENT);

	signals[SIGNAL_UPDATE_PIXEL] =
		g_signal_new("update-pixel",
			     G_OBJECT_CLASS_TYPE(gobject_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET(PixbufRendererClass, update_pixel),
			     nullptr, nullptr,
			     g_cclosure_marshal_VOID__VOID,
			     G_TYPE_NONE, 0);
}

static RendererFuncs *pr_backend_renderer_new(PixbufRenderer *pr)
{
	return renderer_tiles_new(pr);
}


static void pixbuf_renderer_init(PixbufRenderer *pr)
{
	GtkWidget *box;

	box = GTK_WIDGET(pr);

	pr->zoom_min = PR_ZOOM_MIN;
	pr->zoom_max = PR_ZOOM_MAX;
	pr->zoom_quality = GDK_INTERP_BILINEAR;
	pr->zoom_2pass = FALSE;

	pr->zoom = 1.0;
	pr->scale = 1.0;

	pr->scroll_reset = ScrollReset::TOPLEFT;

	pr->scroller_id = 0;
	pr->scroller_overlay = -1;

	pr->x_mouse = -1;
	pr->y_mouse = -1;

	pr->source_tiles = nullptr;

	pr->orientation = 1;

	pr->norm_center_x = 0.5;
	pr->norm_center_y = 0.5;

	pr->color.red =0;
	pr->color.green =0;
	pr->color.blue =0;

	pr->renderer = pr_backend_renderer_new(pr);

	gtk_widget_set_double_buffered(box, FALSE);
	gtk_widget_set_app_paintable(box, TRUE);
	g_signal_connect_after(G_OBJECT(box), "size_allocate",
			       G_CALLBACK(pr_size_cb), pr);

	pr_signals_connect(pr);
}

static void pixbuf_renderer_finalize(GObject *object)
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(object);

	pr->renderer->free(pr->renderer);

	if (pr->pixbuf) g_object_unref(pr->pixbuf);

	pr_scroller_timer_set(pr, FALSE);

	pr_source_tile_free_all(pr);
}

PixbufRenderer *pixbuf_renderer_new()
{
	return static_cast<PixbufRenderer *>(g_object_new(TYPE_PIXBUF_RENDERER, nullptr));
}

static void pixbuf_renderer_set_property(GObject *object, guint prop_id,
					 const GValue *value, GParamSpec *pspec)
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(object);

	switch (prop_id)
		{
		case PROP_ZOOM_MIN:
			pr->zoom_min = g_value_get_double(value);
			break;
		case PROP_ZOOM_MAX:
			pr->zoom_max = g_value_get_double(value);
			break;
		case PROP_ZOOM_QUALITY:
			pr->zoom_quality = static_cast<GdkInterpType>(g_value_get_uint(value));
			break;
		case PROP_ZOOM_2PASS:
			pr->zoom_2pass = g_value_get_boolean(value);
			break;
		case PROP_SCROLL_RESET:
			pr->scroll_reset = static_cast<ScrollReset>(g_value_get_uint(value));
			break;
		case PROP_DELAY_FLIP:
			pr->delay_flip = g_value_get_boolean(value);
			break;
		case PROP_LOADING:
			pr->loading = g_value_get_boolean(value);
			break;
		case PROP_COMPLETE:
			pr->complete = g_value_get_boolean(value);
			break;
		case PROP_CACHE_SIZE_DISPLAY:
			break;
		case PROP_CACHE_SIZE_TILES:
			pr->source_tiles_cache_size = g_value_get_uint(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
		}
}

static void pixbuf_renderer_get_property(GObject *object, guint prop_id,
					 GValue *value, GParamSpec *pspec)
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(object);

	switch (prop_id)
		{
		case PROP_ZOOM_MIN:
			g_value_set_double(value, pr->zoom_min);
			break;
		case PROP_ZOOM_MAX:
			g_value_set_double(value, pr->zoom_max);
			break;
		case PROP_ZOOM_QUALITY:
			g_value_set_uint(value, pr->zoom_quality);
			break;
		case PROP_ZOOM_2PASS:
			g_value_set_boolean(value, pr->zoom_2pass);
			break;
		case PROP_SCROLL_RESET:
			g_value_set_uint(value, pr->scroll_reset);
			break;
		case PROP_DELAY_FLIP:
			g_value_set_boolean(value, pr->delay_flip);
			break;
		case PROP_LOADING:
			g_value_set_boolean(value, pr->loading);
			break;
		case PROP_COMPLETE:
			g_value_set_boolean(value, pr->complete);
			break;
		case PROP_CACHE_SIZE_DISPLAY:
			break;
		case PROP_CACHE_SIZE_TILES:
			g_value_set_uint(value, pr->source_tiles_cache_size);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
		}
}

/*
 *-------------------------------------------------------------------
 * overlays
 *-------------------------------------------------------------------
 */


gint pixbuf_renderer_overlay_add(PixbufRenderer *pr, GdkPixbuf *pixbuf, gint x, gint y,
				 OverlayRendererFlags flags)
{
	return pr->renderer->overlay_add(pr->renderer, pixbuf, x, y, flags);
}

void pixbuf_renderer_overlay_set(PixbufRenderer *pr, gint id, GdkPixbuf *pixbuf, gint x, gint y)
{
	pr->renderer->overlay_set(pr->renderer, id, pixbuf, x, y);
}

gboolean pixbuf_renderer_overlay_get(PixbufRenderer *pr, gint id, GdkPixbuf **pixbuf, gint *x, gint *y)
{
	return pr->renderer->overlay_get(pr->renderer, id, pixbuf, x, y);
}

void pixbuf_renderer_overlay_remove(PixbufRenderer *pr, gint id)
{
	pr->renderer->overlay_set(pr->renderer, id, nullptr, 0, 0);
}

/*
 *-------------------------------------------------------------------
 * scroller overlay
 *-------------------------------------------------------------------
 */


static gboolean pr_scroller_update_cb(gpointer data)
{
	auto pr = static_cast<PixbufRenderer *>(data);
	gint x;
	gint y;
	gint xinc;
	gint yinc;

	/* this was a simple scroll by difference between scroller and mouse position,
	 * but all this math results in a smoother result and accounts for a dead zone.
	 */

	if (abs(pr->scroller_xpos - pr->scroller_x) < PR_SCROLLER_DEAD_ZONE)
		{
		x = 0;
		}
	else
		{
		gint shift = PR_SCROLLER_DEAD_ZONE / 2 * PR_SCROLLER_UPDATES_PER_SEC;
		x = (pr->scroller_xpos - pr->scroller_x) / 2 * PR_SCROLLER_UPDATES_PER_SEC;
		x += (x > 0) ? -shift : shift;
		}

	if (abs(pr->scroller_ypos - pr->scroller_y) < PR_SCROLLER_DEAD_ZONE)
		{
		y = 0;
		}
	else
		{
		gint shift = PR_SCROLLER_DEAD_ZONE / 2 * PR_SCROLLER_UPDATES_PER_SEC;
		y = (pr->scroller_ypos - pr->scroller_y) / 2 * PR_SCROLLER_UPDATES_PER_SEC;
		y += (y > 0) ? -shift : shift;
		}

	if (abs(x) < PR_SCROLLER_DEAD_ZONE * PR_SCROLLER_UPDATES_PER_SEC)
		{
		xinc = x;
		}
	else
		{
		xinc = pr->scroller_xinc;

		if (x >= 0)
			{
			if (xinc < 0) xinc = 0;
			if (x < xinc) xinc = x;
			if (x > xinc) xinc = MIN(xinc + x / PR_SCROLLER_UPDATES_PER_SEC, x);
			}
		else
			{
			if (xinc > 0) xinc = 0;
			if (x > xinc) xinc = x;
			if (x < xinc) xinc = MAX(xinc + x / PR_SCROLLER_UPDATES_PER_SEC, x);
			}
		}

	if (abs(y) < PR_SCROLLER_DEAD_ZONE * PR_SCROLLER_UPDATES_PER_SEC)
		{
		yinc = y;
		}
	else
		{
		yinc = pr->scroller_yinc;

		if (y >= 0)
			{
			if (yinc < 0) yinc = 0;
			if (y < yinc) yinc = y;
			if (y > yinc) yinc = MIN(yinc + y / PR_SCROLLER_UPDATES_PER_SEC, y);
			}
		else
			{
			if (yinc > 0) yinc = 0;
			if (y > yinc) yinc = y;
			if (y < yinc) yinc = MAX(yinc + y / PR_SCROLLER_UPDATES_PER_SEC, y);
			}
		}

	pr->scroller_xinc = xinc;
	pr->scroller_yinc = yinc;

	xinc = xinc / PR_SCROLLER_UPDATES_PER_SEC;
	yinc = yinc / PR_SCROLLER_UPDATES_PER_SEC;

	pixbuf_renderer_scroll(pr, xinc, yinc);

	return TRUE;
}

static void pr_scroller_timer_set(PixbufRenderer *pr, gboolean start)
{
	if (pr->scroller_id)
		{
		g_source_remove(pr->scroller_id);
		pr->scroller_id = 0;
		}

	if (start)
		{
		pr->scroller_id = g_timeout_add(1000 / PR_SCROLLER_UPDATES_PER_SEC,
						pr_scroller_update_cb, pr);
		}
}

static void pr_scroller_start(PixbufRenderer *pr, gint x, gint y)
{
	if (pr->scroller_overlay == -1)
		{
		GdkPixbuf *pixbuf;
		gint w;
		gint h;

#ifdef GQ_BUILD
		pixbuf = gdk_pixbuf_new_from_resource(GQ_RESOURCE_PATH_ICONS "/" PIXBUF_INLINE_SCROLLER ".png", nullptr);
#else
		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 32, 32);
		gdk_pixbuf_fill(pixbuf, 0x000000ff);
#endif
		w = gdk_pixbuf_get_width(pixbuf);
		h = gdk_pixbuf_get_height(pixbuf);

		pr->scroller_overlay = pixbuf_renderer_overlay_add(pr, pixbuf, x - w / 2, y - h / 2, OVL_NORMAL);
		g_object_unref(pixbuf);
		}

	pr->scroller_x = x;
	pr->scroller_y = y;
	pr->scroller_xpos = x;
	pr->scroller_ypos = y;

	pr_scroller_timer_set(pr, TRUE);
}

static void pr_scroller_stop(PixbufRenderer *pr)
{
	if (!pr->scroller_id) return;

	pixbuf_renderer_overlay_remove(pr, pr->scroller_overlay);
	pr->scroller_overlay = -1;

	pr_scroller_timer_set(pr, FALSE);
}

/*
 *-------------------------------------------------------------------
 * borders
 *-------------------------------------------------------------------
 */

/**
 * @brief Background color
 */
void pixbuf_renderer_set_color(PixbufRenderer *pr, GdkRGBA *color)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	if (color)
		{
		pr->color.red = color->red;
		pr->color.green = color->green;
		pr->color.blue = color->blue;
		}
	else
		{
		pr->color.red = 0;
		pr->color.green = 0;
		pr->color.blue = 0;
		}

	pr->renderer->update_viewport(pr->renderer);
}

/*
 *-------------------------------------------------------------------
 * source tiles
 *-------------------------------------------------------------------
 */

static void pr_source_tile_free(SourceTile *st)
{
	if (!st) return;

	if (st->pixbuf) g_object_unref(st->pixbuf);
	g_free(st);
}

static void pr_source_tile_free_all(PixbufRenderer *pr)
{
	g_list_free_full(pr->source_tiles, reinterpret_cast<GDestroyNotify>(pr_source_tile_free));
	pr->source_tiles = nullptr;
}

static void pr_source_tile_unset(PixbufRenderer *pr)
{
	pr_source_tile_free_all(pr);
}

static void pr_zoom_adjust_real(PixbufRenderer *pr, gdouble increment,
				PrZoomFlags flags, gint x, gint y)
{
	gdouble zoom = pr->zoom;

	if (increment == 0.0) return;

	if (zoom == 0.0)
		{
		if (pr->scale < 1.0)
			{
			zoom = 0.0 - 1.0 / pr->scale;
			}
		else
			{
			zoom = pr->scale;
			}
		}

	if (options->image.zoom_style == ZOOM_GEOMETRIC)
		{
		if (increment < 0.0)
			{
			if (zoom >= 1.0)
				{
				if (zoom / -(increment - 1.0) < 1.0)
					{
					zoom = 1.0 / (zoom / (increment - 1.0));
					}
				else
					{
					zoom = zoom / -(increment - 1.0) ;
					}
				}
			else
				{
				zoom = zoom * -(increment - 1.0);
				}
			}
		else
			{
			if (zoom <= -1.0 )
				{
				if (zoom / (increment + 1.0) > -1.0)
					{
					zoom = -(1.0 / (zoom / (increment + 1.0)));
					}
				else
					{
					zoom = zoom / (increment + 1.0) ;
					}
				}
			else
				{
				zoom = zoom * (increment + 1.0);
				}
			}
		}
	else
		{
		if (increment < 0.0)
			{
			if (zoom >= 1.0 && zoom + increment < 1.0)
				{
				zoom = zoom + increment - 2.0;
				}
			else
				{
				zoom = zoom + increment;
				}
			}
		else
			{
			if (zoom <= -1.0 && zoom + increment > -1.0)
				{
				zoom = zoom + increment + 2.0;
				}
			else
				{
				zoom = zoom + increment;
				}
			}
		}

	pr_zoom_sync(pr, zoom, flags, x, y);
}


/*
 *-------------------------------------------------------------------
 * signal emission
 *-------------------------------------------------------------------
 */

static void pr_update_signal(PixbufRenderer *pr)
{
	DEBUG_1("%s pixbuf renderer updated - started drawing %p, img: %dx%d", get_exec_time(), (void *)pr, pr->image_width, pr->image_height);
	pr->debug_updated = TRUE;
}

static void pr_zoom_signal(PixbufRenderer *pr)
{
	g_signal_emit(pr, signals[SIGNAL_ZOOM], 0, pr->zoom);
}

static void pr_clicked_signal(PixbufRenderer *pr, GdkEventButton *bevent)
{
	g_signal_emit(pr, signals[SIGNAL_CLICKED], 0, bevent);
}

static void pr_scroll_notify_signal(PixbufRenderer *pr)
{
	g_signal_emit(pr, signals[SIGNAL_SCROLL_NOTIFY], 0);
}

void pr_render_complete_signal(PixbufRenderer *pr)
{
	if (!pr->complete)
		{
		g_signal_emit(pr, signals[SIGNAL_RENDER_COMPLETE], 0);
		g_object_set(G_OBJECT(pr), "complete", TRUE, NULL);
		}
	if (pr->debug_updated)
		{
		DEBUG_1("%s pixbuf renderer done %p", get_exec_time(), (void *)pr);
		pr->debug_updated = FALSE;
		}
}

static void pr_drag_signal(PixbufRenderer *pr, GdkEventMotion *event)
{
	g_signal_emit(pr, signals[SIGNAL_DRAG], 0, event);
}

static void pr_update_pixel_signal(PixbufRenderer *pr)
{
	g_signal_emit(pr, signals[SIGNAL_UPDATE_PIXEL], 0);
}

/*
 *-------------------------------------------------------------------
 * sync and clamp
 *-------------------------------------------------------------------
 */


void pr_tile_coords_map_orientation(gint orientation,
                                    gdouble tile_x, gdouble tile_y, /* coordinates of the tile */
                                    gdouble image_w, gdouble image_h,
                                    gdouble tile_w, gdouble tile_h,
                                    gdouble &res_x, gdouble &res_y)
{
	res_x = tile_x;
	res_y = tile_y;
	switch (orientation)
		{
		case EXIF_ORIENTATION_TOP_LEFT:
			/* normal -- nothing to do */
			break;
		case EXIF_ORIENTATION_TOP_RIGHT:
			/* mirrored */
			res_x = image_w - tile_x - tile_w;
			break;
		case EXIF_ORIENTATION_BOTTOM_RIGHT:
			/* upside down */
			res_x = image_w - tile_x - tile_w;
			res_y = image_h - tile_y - tile_h;
			break;
		case EXIF_ORIENTATION_BOTTOM_LEFT:
			/* flipped */
			res_y = image_h - tile_y - tile_h;
			break;
		case EXIF_ORIENTATION_LEFT_TOP:
			res_x = tile_y;
			res_y = tile_x;
			break;
		case EXIF_ORIENTATION_RIGHT_TOP:
			/* rotated -90 (270) */
			res_x = tile_y;
			res_y = image_w - tile_x - tile_w;
			break;
		case EXIF_ORIENTATION_RIGHT_BOTTOM:
			res_x = image_h - tile_y - tile_h;
			res_y = image_w - tile_x - tile_w;
			break;
		case EXIF_ORIENTATION_LEFT_BOTTOM:
			/* rotated 90 */
			res_x = image_h - tile_y - tile_h;
			res_y = tile_x;
			break;
		default:
			/* The other values are out of range */
			break;
		}
}

GdkRectangle pr_tile_region_map_orientation(gint orientation,
                                            GdkRectangle area, /* coordinates of the area inside tile */
                                            gint tile_w, gint tile_h)
{
	GdkRectangle res = area;

	switch (orientation)
		{
		case EXIF_ORIENTATION_TOP_LEFT:
			/* normal -- nothing to do */
			break;
		case EXIF_ORIENTATION_TOP_RIGHT:
			/* mirrored */
			res.x = tile_w - area.x - area.width;
			break;
		case EXIF_ORIENTATION_BOTTOM_RIGHT:
			/* upside down */
			res.x = tile_w - area.x - area.width;
			res.y = tile_h - area.y - area.height;
			break;
		case EXIF_ORIENTATION_BOTTOM_LEFT:
			/* flipped */
			res.y = tile_h - area.y - area.height;
			break;
		case EXIF_ORIENTATION_LEFT_TOP:
			res.x = area.y;
			res.y = area.x;
			res.width = area.height;
			res.height = area.width;
			break;
		case EXIF_ORIENTATION_RIGHT_TOP:
			/* rotated -90 (270) */
			res.x = area.y;
			res.y = tile_w - area.x - area.width;
			res.width = area.height;
			res.height = area.width;
			break;
		case EXIF_ORIENTATION_RIGHT_BOTTOM:
			res.x = tile_h - area.y - area.height;
			res.y = tile_w - area.x - area.width;
			res.width = area.height;
			res.height = area.width;
			break;
		case EXIF_ORIENTATION_LEFT_BOTTOM:
			/* rotated 90 */
			res.x = tile_h - area.y - area.height;
			res.y = area.x;
			res.width = area.height;
			res.height = area.width;
			break;
		default:
			/* The other values are out of range */
			break;
		}

	return res;
}

GdkRectangle pr_coords_map_orientation_reverse(gint orientation,
                                               GdkRectangle area,
                                               gint tile_w, gint tile_h)
{
	GdkRectangle res = area;

	switch (orientation)
		{
		case EXIF_ORIENTATION_TOP_LEFT:
			/* normal -- nothing to do */
			break;
		case EXIF_ORIENTATION_TOP_RIGHT:
			/* mirrored */
			res.x = tile_w - area.x - area.width;
			break;
		case EXIF_ORIENTATION_BOTTOM_RIGHT:
			/* upside down */
			res.x = tile_w - area.x - area.width;
			res.y = tile_h - area.y - area.height;
			break;
		case EXIF_ORIENTATION_BOTTOM_LEFT:
			/* flipped */
			res.y = tile_h - area.y - area.height;
			break;
		case EXIF_ORIENTATION_LEFT_TOP:
			res.x = area.y;
			res.y = area.x;
			res.width = area.height;
			res.height = area.width;
			break;
		case EXIF_ORIENTATION_RIGHT_TOP:
			/* rotated -90 (270) */
			res.x = tile_w - area.y - area.height;
			res.y = area.x;
			res.width = area.height;
			res.height = area.width;
			break;
		case EXIF_ORIENTATION_RIGHT_BOTTOM:
			res.x = tile_w - area.y - area.height;
			res.y = tile_h - area.x - area.width;
			res.width = area.height;
			res.height = area.width;
			break;
		case EXIF_ORIENTATION_LEFT_BOTTOM:
			/* rotated 90 */
			res.x = area.y;
			res.y = tile_h - area.x - area.width;
			res.width = area.height;
			res.height = area.width;
			break;
		default:
			/* The other values are out of range */
			break;
		}

	return res;
}

void pr_scale_region(GdkRectangle &region, gdouble scale)
{
	region.x *= scale;
	region.y *= scale;
	region.width *= scale;
	region.height *= scale;
}



static void pixbuf_renderer_sync_scroll_center(PixbufRenderer *pr)
{
	gint src_x;
	gint src_y;
	if (!pr->width || !pr->height) return;

	/*
	 * Update norm_center only if the image is bigger than the window.
	 * With this condition the stored center survives also a temporary display
	 * of the "broken image" icon.
	*/

	if (pr->width > pr->viewport_width)
		{
		src_x = pr->x_scroll + pr->vis_width / 2;
		pr->norm_center_x = static_cast<gdouble>(src_x) / pr->width;
		}

	if (pr->height > pr->viewport_height)
		{
		src_y = pr->y_scroll + pr->vis_height / 2;
		pr->norm_center_y = static_cast<gdouble>(src_y) / pr->height;
		}
}


static gboolean pr_scroll_clamp(PixbufRenderer *pr)
{
	gint old_xs;
	gint old_ys;

	if (pr->zoom == 0.0)
		{
		pr->x_scroll = 0;
		pr->y_scroll = 0;

		return FALSE;
		}

	old_xs = pr->x_scroll;
	old_ys = pr->y_scroll;

	if (pr->x_offset > 0)
		{
		pr->x_scroll = 0;
		}
	else
		{
		pr->x_scroll = CLAMP(pr->x_scroll, 0, pr->width - pr->vis_width);
		}

	if (pr->y_offset > 0)
		{
		pr->y_scroll = 0;
		}
	else
		{
		pr->y_scroll = CLAMP(pr->y_scroll, 0, pr->height - pr->vis_height);
		}

	pixbuf_renderer_sync_scroll_center(pr);

	return (old_xs != pr->x_scroll || old_ys != pr->y_scroll);
}

static gboolean pr_size_clamp(PixbufRenderer *pr)
{
	gint old_vw;
	gint old_vh;

	old_vw = pr->vis_width;
	old_vh = pr->vis_height;

	if (pr->width < pr->viewport_width)
		{
		pr->vis_width = pr->width;
		pr->x_offset = (pr->viewport_width - pr->width) / 2;
		}
	else
		{
		pr->vis_width = pr->viewport_width;
		pr->x_offset = 0;
		}

	if (pr->height < pr->viewport_height)
		{
		pr->vis_height = pr->height;
		pr->y_offset = (pr->viewport_height - pr->height) / 2;
		}
	else
		{
		pr->vis_height = pr->viewport_height;
		pr->y_offset = 0;
		}

	pixbuf_renderer_sync_scroll_center(pr);

	return (old_vw != pr->vis_width || old_vh != pr->vis_height);
}

static gboolean pr_zoom_clamp(PixbufRenderer *pr, gdouble zoom,
			      PrZoomFlags flags)
{
	gint w;
	gint h;
	gdouble scale;
	gboolean force = !!(flags & PR_ZOOM_FORCE);

	zoom = CLAMP(zoom, pr->zoom_min, pr->zoom_max);

	if (pr->zoom == zoom && !force) return FALSE;

	w = pr->image_width;
	h = pr->image_height;

	if (zoom == 0.0 && !pr->pixbuf)
		{
		scale = 1.0;
		}
	else if (zoom == 0.0)
		{
		gint max_w;
		gint max_h;

		max_w = pr->viewport_width;
		max_h = pr->viewport_height;

		if (w > max_w || h > max_h)
			{
			if (static_cast<gdouble>(max_w) / w > static_cast<gdouble>(max_h) / h)
				{
				scale = static_cast<gdouble>(max_h) / h;
				h = max_h;
				w = w * scale + 0.5;
				if (w > max_w) w = max_w;
				}
			else
				{
				scale = static_cast<gdouble>(max_w) / w;
				w = max_w;
				h = h * scale + 0.5;
				if (h > max_h) h = max_h;
				}

			if (w < 1) w = 1;
			if (h < 1) h = 1;
			}
		else
			{
			scale = 1.0;
			}
		}
	else if (zoom > 0.0) /* zoom orig, in */
		{
		scale = zoom;
		w = w * scale;
		h = h * scale;
		}
	else /* zoom out */
		{
		scale = 1.0 / (0.0 - zoom);
		w = w * scale;
		h = h * scale;
		}

	pr->zoom = zoom;
	pr->width = w;
	pr->height = h;
	pr->scale = scale;

	return TRUE;
}

static void pr_zoom_sync(PixbufRenderer *pr, gdouble zoom,
			 PrZoomFlags flags, gint px, gint py)
{
	gdouble old_scale;
	gint old_cx;
	gint old_cy;
	gboolean center_point = !!(flags & PR_ZOOM_CENTER);
	gboolean force = !!(flags & PR_ZOOM_FORCE);
	gboolean new_z = !!(flags & PR_ZOOM_NEW);
	gboolean lazy = !!(flags & PR_ZOOM_LAZY);
	PrZoomFlags clamp_flags = flags;
	gdouble old_center_x = pr->norm_center_x;
	gdouble old_center_y = pr->norm_center_y;

	old_scale = pr->scale;
	if (center_point)
		{
		px = CLAMP(px, 0, pr->width);
		py = CLAMP(py, 0, pr->height);
		old_cx = pr->x_scroll + (px - pr->x_offset);
		old_cy = pr->y_scroll + (py - pr->y_offset);
		}
	else
		{
		px = py = 0;
		old_cx = pr->x_scroll + pr->vis_width / 2;
		old_cy = pr->y_scroll + pr->vis_height / 2;
		}

	if (force) clamp_flags = static_cast<PrZoomFlags>(clamp_flags | PR_ZOOM_INVALIDATE);
	if (!pr_zoom_clamp(pr, zoom, clamp_flags)) return;

	(void) pr_size_clamp(pr);

	if (force && new_z)
		{
		switch (pr->scroll_reset)
			{
			case ScrollReset::NOCHANGE:
				/* maintain old scroll position */
				pr->x_scroll = (static_cast<gdouble>(pr->image_width) * old_center_x * pr->scale) - pr->vis_width / 2.0;
				pr->y_scroll = (static_cast<gdouble>(pr->image_height) * old_center_y * pr->scale) - pr->vis_height / 2.0;
				break;
			case ScrollReset::CENTER:
				/* center new image */
				pr->x_scroll = (static_cast<gdouble>(pr->image_width) / 2.0 * pr->scale) - pr->vis_width / 2.0;
				pr->y_scroll = (static_cast<gdouble>(pr->image_height) / 2.0 * pr->scale) - pr->vis_height / 2.0;
				break;
			case ScrollReset::TOPLEFT:
			default:
				/* reset to upper left */
				pr->x_scroll = 0;
				pr->y_scroll = 0;
				break;
			}
		}
	else
		{
		/* user zoom does not force, so keep visible center point */
		if (center_point)
			{
			pr->x_scroll = old_cx / old_scale * pr->scale - (px - pr->x_offset);
			pr->y_scroll = old_cy / old_scale * pr->scale - (py - pr->y_offset);
			}
		else
			{
			pr->x_scroll = old_cx / old_scale * pr->scale - (pr->vis_width / 2.0);
			pr->y_scroll = old_cy / old_scale * pr->scale - (pr->vis_height / 2.0);
			}
		}

	pr_scroll_clamp(pr);

	pr->renderer->update_zoom(pr->renderer, lazy);

	pr_scroll_notify_signal(pr);
	pr_zoom_signal(pr);
	pr_update_signal(pr);
}

static void pr_size_sync(PixbufRenderer *pr, gint new_width, gint new_height)
{
	gboolean zoom_changed = FALSE;

	gint new_viewport_width = new_width;
	gint new_viewport_height = new_height;

	if (pr->window_width == new_width && pr->window_height == new_height &&
	    pr->viewport_width == new_viewport_width && pr->viewport_height == new_viewport_height) return;

	pr->window_width = new_width;
	pr->window_height = new_height;
	pr->viewport_width = new_viewport_width;
	pr->viewport_height = new_viewport_height;

	if (pr->zoom == 0.0)
		{
		gdouble old_scale = pr->scale;
		pr_zoom_clamp(pr, 0.0, PR_ZOOM_FORCE);
		zoom_changed = (old_scale != pr->scale);
		}

	pr_size_clamp(pr);
	pr_scroll_clamp(pr);

	if (zoom_changed)
		{
		pr->renderer->update_zoom(pr->renderer, FALSE);
		}

	pr->renderer->update_viewport(pr->renderer);


	/* ensure scroller remains visible */
	if (pr->scroller_overlay != -1)
		{
		gboolean update = FALSE;

		if (pr->scroller_x > new_width)
			{
			pr->scroller_x = new_width;
			pr->scroller_xpos = new_width;
			update = TRUE;
			}
		if (pr->scroller_y > new_height)
			{
			pr->scroller_y = new_height;
			pr->scroller_ypos = new_height;
			update = TRUE;
			}

		if (update)
			{
			GdkPixbuf *pixbuf;

			if (pixbuf_renderer_overlay_get(pr, pr->scroller_overlay, &pixbuf, nullptr, nullptr))
				{
				gint w;
				gint h;

				w = gdk_pixbuf_get_width(pixbuf);
				h = gdk_pixbuf_get_height(pixbuf);
				pixbuf_renderer_overlay_set(pr, pr->scroller_overlay, pixbuf,
							    pr->scroller_x - w / 2, pr->scroller_y - h / 2);
				}
			}
		}

	pr_scroll_notify_signal(pr);
	if (zoom_changed) pr_zoom_signal(pr);
	pr_update_signal(pr);
}

static void pr_size_cb(GtkWidget *, GtkAllocation *allocation, gpointer data)
{
	auto pr = static_cast<PixbufRenderer *>(data);

	pr_size_sync(pr, allocation->width, allocation->height);
}

/*
 *-------------------------------------------------------------------
 * scrolling
 *-------------------------------------------------------------------
 */

void pixbuf_renderer_scroll(PixbufRenderer *pr, gint x, gint y)
{
	gint old_x;
	gint old_y;
	gint x_off;
	gint y_off;

	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	if (!pr->pixbuf) return;

	old_x = pr->x_scroll;
	old_y = pr->y_scroll;

	pr->x_scroll += x;
	pr->y_scroll += y;

	pr_scroll_clamp(pr);

	pixbuf_renderer_sync_scroll_center(pr);

	if (pr->x_scroll == old_x && pr->y_scroll == old_y) return;

	pr_scroll_notify_signal(pr);

	x_off = pr->x_scroll - old_x;
	y_off = pr->y_scroll - old_y;

	pr->renderer->scroll(pr->renderer, x_off, y_off);
}

/* get or set coordinates of viewport center in the image, in range 0.0 - 1.0 */

void pixbuf_renderer_get_scroll_center(PixbufRenderer *pr, gdouble *x, gdouble *y)
{
	*x = pr->norm_center_x;
	*y = pr->norm_center_y;
}

void pixbuf_renderer_set_scroll_center(PixbufRenderer *pr, gdouble x, gdouble y)
{
	gdouble dst_x;
	gdouble dst_y;

	dst_x = x * pr->width  - pr->vis_width  / 2.0 - pr->x_scroll + CLAMP(pr->subpixel_x_scroll, -1.0, 1.0);
	dst_y = y * pr->height - pr->vis_height / 2.0 - pr->y_scroll + CLAMP(pr->subpixel_y_scroll, -1.0, 1.0);

	pr->subpixel_x_scroll = dst_x - static_cast<gint>(dst_x);
	pr->subpixel_y_scroll = dst_y - static_cast<gint>(dst_y);

	pixbuf_renderer_scroll(pr, static_cast<gint>(dst_x), static_cast<gint>(dst_y));
}

/*
 *-------------------------------------------------------------------
 * mouse
 *-------------------------------------------------------------------
 */

static gboolean pr_mouse_motion_cb(GtkWidget *widget, GdkEventMotion *event, gpointer)
{
	PixbufRenderer *pr;
	gint accel;
	GdkSeat *seat;
	GdkDevice *device;

	/* This is a hack, but work far the best, at least for single pointer systems.
	 * See https://bugzilla.gnome.org/show_bug.cgi?id=587714 for more. */
	gint x;
	gint y;
	seat = gdk_display_get_default_seat(gdk_window_get_display(event->window));
	device = gdk_seat_get_pointer(seat);

	gdk_window_get_device_position(event->window, device, &x, &y, nullptr);

	event->x = x;
	event->y = y;

	pr = PIXBUF_RENDERER(widget);

	if (pr->scroller_id)
		{
		pr->scroller_xpos = event->x;
		pr->scroller_ypos = event->y;
		}

	pr->x_mouse = event->x;
	pr->y_mouse = event->y;
	pr_update_pixel_signal(pr);

	if (!pr->in_drag || !gdk_pointer_is_grabbed()) return FALSE;

	if (pr->drag_moved < PR_DRAG_SCROLL_THRESHHOLD)
		{
		pr->drag_moved++;
		}
	else
		{
		widget_set_cursor(widget, GDK_FLEUR);
		}

	if (event->state & GDK_CONTROL_MASK)
		{
		accel = PR_PAN_SHIFT_MULTIPLIER;
		}
	else
		{
		accel = 1;
		}

		{
		pixbuf_renderer_scroll(pr, (pr->drag_last_x - event->x) * accel,
					(pr->drag_last_y - event->y) * accel);
		}
	pr_drag_signal(pr, event);

	pr->drag_last_x = event->x;
	pr->drag_last_y = event->y;

	/* This is recommended by the GTK+ documentation, but does not work properly.
	 * Use deprecated way until GTK+ gets a solution for correct motion hint handling:
	 * https://bugzilla.gnome.org/show_bug.cgi?id=587714
	 */
	/* gdk_event_request_motions (event); */
	return FALSE;
}

static gboolean pr_leave_notify_cb(GtkWidget *widget, GdkEventCrossing *, gpointer)
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(widget);
	pr->x_mouse = -1;
	pr->y_mouse = -1;

	pr_update_pixel_signal(pr);
	return FALSE;
}

static gboolean pr_mouse_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer)
{
	PixbufRenderer *pr;
	GtkWidget *parent;

	pr = PIXBUF_RENDERER(widget);

	if (pr->scroller_id) return TRUE;

	switch (bevent->button)
		{
		case MOUSE_BUTTON_LEFT:
			pr->in_drag = TRUE;
			pr->drag_last_x = bevent->x;
			pr->drag_last_y = bevent->y;
			pr->drag_moved = 0;
			gdk_pointer_grab(gtk_widget_get_window(widget), FALSE,
					 static_cast<GdkEventMask>(GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_BUTTON_RELEASE_MASK),
					 nullptr, nullptr, bevent->time);
			gtk_grab_add(widget);
			break;
		case MOUSE_BUTTON_MIDDLE:
			pr->drag_moved = 0;
			break;
		case MOUSE_BUTTON_RIGHT:
			pr_clicked_signal(pr, bevent);
			break;
		default:
			break;
		}

	parent = gtk_widget_get_parent(widget);
	if (widget && gtk_widget_get_can_focus(parent))
		{
		gtk_widget_grab_focus(parent);
		}

	return FALSE;
}

static gboolean pr_mouse_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer)
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(widget);

	if (pr->scroller_id)
		{
		pr_scroller_stop(pr);
		return TRUE;
		}

	if (gdk_pointer_is_grabbed() && gtk_widget_has_grab(GTK_WIDGET(pr)))
		{
		gtk_grab_remove(widget);
		gdk_pointer_ungrab(bevent->time);
		widget_set_cursor(widget, -1);
		}

	if (pr->drag_moved < PR_DRAG_SCROLL_THRESHHOLD)
		{
		if (bevent->button == MOUSE_BUTTON_LEFT && (bevent->state & GDK_CONTROL_MASK))
			{
			pr_scroller_start(pr, bevent->x, bevent->y);
			}
		else if (bevent->button == MOUSE_BUTTON_LEFT || bevent->button == MOUSE_BUTTON_MIDDLE)
			{
			pr_clicked_signal(pr, bevent);
			}
		}

	pr->in_drag = FALSE;

	return FALSE;
}

static gboolean pr_mouse_leave_cb(GtkWidget *widget, GdkEventCrossing *, gpointer)
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(widget);

	if (pr->scroller_id)
		{
		pr->scroller_xpos = pr->scroller_x;
		pr->scroller_ypos = pr->scroller_y;
		pr->scroller_xinc = 0;
		pr->scroller_yinc = 0;
		}

	return FALSE;
}

static void pr_mouse_drag_cb(GtkWidget *widget, GdkDragContext *, gpointer)
{
	PixbufRenderer *pr;

	pr = PIXBUF_RENDERER(widget);

	pr->drag_moved = PR_DRAG_SCROLL_THRESHHOLD;
}

static void pr_signals_connect(PixbufRenderer *pr)
{
	g_signal_connect(G_OBJECT(pr), "motion_notify_event",
			 G_CALLBACK(pr_mouse_motion_cb), pr);
	g_signal_connect(G_OBJECT(pr), "button_press_event",
			 G_CALLBACK(pr_mouse_press_cb), pr);
	g_signal_connect(G_OBJECT(pr), "button_release_event",
			 G_CALLBACK(pr_mouse_release_cb), pr);
	g_signal_connect(G_OBJECT(pr), "leave_notify_event",
			 G_CALLBACK(pr_mouse_leave_cb), pr);
	g_signal_connect(G_OBJECT(pr), "leave_notify_event",
			 G_CALLBACK(pr_leave_notify_cb), pr);

	gtk_widget_set_events(GTK_WIDGET(pr), GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK |
					      static_cast<GdkEventMask>(GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK |
					      GDK_LEAVE_NOTIFY_MASK));

	g_signal_connect(G_OBJECT(pr), "drag_begin",
			 G_CALLBACK(pr_mouse_drag_cb), pr);

}

/*
 *-------------------------------------------------------------------
 * public
 *-------------------------------------------------------------------
 */
static void pr_pixbuf_size_sync(PixbufRenderer *pr)
{
	if (!pr->pixbuf) return;
	switch (pr->orientation)
		{
		case EXIF_ORIENTATION_LEFT_TOP:
		case EXIF_ORIENTATION_RIGHT_TOP:
		case EXIF_ORIENTATION_RIGHT_BOTTOM:
		case EXIF_ORIENTATION_LEFT_BOTTOM:
			pr->image_width = gdk_pixbuf_get_height(pr->pixbuf);
			pr->image_height = gdk_pixbuf_get_width(pr->pixbuf);

			break;
		default:
			pr->image_width = gdk_pixbuf_get_width(pr->pixbuf);
			pr->image_height = gdk_pixbuf_get_height(pr->pixbuf);
		}
}

static void pr_set_pixbuf(PixbufRenderer *pr, GdkPixbuf *pixbuf, gdouble zoom, PrZoomFlags flags)
{
	if (pixbuf) g_object_ref(pixbuf);
	if (pr->pixbuf) g_object_unref(pr->pixbuf);
	pr->pixbuf = pixbuf;

	if (!pr->pixbuf)
		{
		/* no pixbuf so just clear the window */
		pr->image_width = 0;
		pr->image_height = 0;
		pr->scale = 1.0;
		pr->zoom = zoom; /* don't throw away the zoom value, it is set by pixbuf_renderer_move, among others,
				    and used for pixbuf_renderer_zoom_get */

		pr->renderer->update_pixbuf(pr->renderer, flags & PR_ZOOM_LAZY);

		pr_update_signal(pr);

		return;
		}

	pr_pixbuf_size_sync(pr);
	pr->renderer->update_pixbuf(pr->renderer, flags & PR_ZOOM_LAZY);
	pr_zoom_sync(pr, zoom, static_cast<PrZoomFlags>(flags | PR_ZOOM_FORCE | PR_ZOOM_NEW), 0, 0);
}

/**
 * @brief Display a pixbuf
 */
void pixbuf_renderer_set_pixbuf(PixbufRenderer *pr, GdkPixbuf *pixbuf, gdouble zoom)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr_source_tile_unset(pr);

	pr_set_pixbuf(pr, pixbuf, zoom, PR_ZOOM_NONE);

	pr_update_signal(pr);
}

/**
 * @brief Same as pixbuf_renderer_set_pixbuf but waits with redrawing for pixbuf_renderer_area_changed
 */
void pixbuf_renderer_set_pixbuf_lazy(PixbufRenderer *pr, GdkPixbuf *pixbuf, gdouble zoom, gint orientation)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr_source_tile_unset(pr);

	pr->orientation = orientation;
	pr_set_pixbuf(pr, pixbuf, zoom, PR_ZOOM_LAZY);

	pr_update_signal(pr);
}

GdkPixbuf *pixbuf_renderer_get_pixbuf(PixbufRenderer *pr)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), NULL);

	return pr->pixbuf;
}

void pixbuf_renderer_set_orientation(PixbufRenderer *pr, gint orientation)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr->orientation = orientation;

	pr_pixbuf_size_sync(pr);
	pr_zoom_sync(pr, pr->zoom, PR_ZOOM_FORCE, 0, 0);
}

void pixbuf_renderer_set_post_process_func(PixbufRenderer *pr, PixbufRendererPostProcessFunc func, gpointer user_data, gboolean slow)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr->func_post_process = func;
	pr->post_process_user_data = user_data;
	pr->post_process_slow = func && slow;

}

/**
 * @brief Move image data from source to pr, source is then set to NULL image
 */
void pixbuf_renderer_move(PixbufRenderer *pr, PixbufRenderer *source)
{
	GObject *object;
	ScrollReset scroll_reset;

	g_return_if_fail(IS_PIXBUF_RENDERER(pr));
	g_return_if_fail(IS_PIXBUF_RENDERER(source));

	if (pr == source) return;

	object = G_OBJECT(pr);

	g_object_set(object, "zoom_min", source->zoom_min, NULL);
	g_object_set(object, "zoom_max", source->zoom_max, NULL);
	g_object_set(object, "loading", source->loading, NULL);

	pr->complete = source->complete;

	pr->x_scroll = source->x_scroll;
	pr->y_scroll = source->y_scroll;
	pr->x_mouse  = source->x_mouse;
	pr->y_mouse  = source->y_mouse;

	scroll_reset = pr->scroll_reset;
	pr->scroll_reset = ScrollReset::NOCHANGE;

	pr->func_post_process = source->func_post_process;
	pr->post_process_user_data = source->post_process_user_data;
	pr->post_process_slow = source->post_process_slow;
	pr->orientation = source->orientation;

	pixbuf_renderer_set_pixbuf(pr, source->pixbuf, source->zoom);

	pr->scroll_reset = scroll_reset;

	pixbuf_renderer_set_pixbuf(source, nullptr, source->zoom);
}

void pixbuf_renderer_copy(PixbufRenderer *pr, PixbufRenderer *source)
{
	GObject *object;
	ScrollReset scroll_reset;

	g_return_if_fail(IS_PIXBUF_RENDERER(pr));
	g_return_if_fail(IS_PIXBUF_RENDERER(source));

	if (pr == source) return;

	object = G_OBJECT(pr);

	g_object_set(object, "zoom_min", source->zoom_min, NULL);
	g_object_set(object, "zoom_max", source->zoom_max, NULL);
	g_object_set(object, "loading", source->loading, NULL);

	pr->complete = source->complete;

	pr->x_scroll = source->x_scroll;
	pr->y_scroll = source->y_scroll;
	pr->x_mouse  = source->x_mouse;
	pr->y_mouse  = source->y_mouse;

	scroll_reset = pr->scroll_reset;
	pr->scroll_reset = ScrollReset::NOCHANGE;

	pr->orientation = source->orientation;

	pixbuf_renderer_set_pixbuf(pr, source->pixbuf, source->zoom);

	pr->scroll_reset = scroll_reset;
}

/**
 * @brief Update region of existing image
 */
void pixbuf_renderer_area_changed(PixbufRenderer *pr, gint x, gint y, gint w, gint h)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr->renderer->area_changed(pr->renderer, x, y, w, h);
}

void pixbuf_renderer_zoom_adjust(PixbufRenderer *pr, gdouble increment)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr_zoom_adjust_real(pr, increment, PR_ZOOM_NONE, 0, 0);
}

void pixbuf_renderer_zoom_adjust_at_point(PixbufRenderer *pr, gdouble increment, gint x, gint y)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr_zoom_adjust_real(pr, increment, PR_ZOOM_CENTER, x, y);
}

void pixbuf_renderer_zoom_set(PixbufRenderer *pr, gdouble zoom)
{
	g_return_if_fail(IS_PIXBUF_RENDERER(pr));

	pr_zoom_sync(pr, zoom, PR_ZOOM_NONE, 0, 0);
}

gdouble pixbuf_renderer_zoom_get(PixbufRenderer *pr)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), 1.0);

	return pr->zoom;
}

gdouble pixbuf_renderer_zoom_get_scale(PixbufRenderer *pr)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), 1.0);

	return pr->scale;
}

gboolean pixbuf_renderer_get_image_size(PixbufRenderer *pr, gint *width, gint *height)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);
	g_return_val_if_fail(width != nullptr && height != nullptr, FALSE);

	if (!pr->pixbuf && (!pr->image_width || !pr->image_height))
		{
		*width = 0;
		*height = 0;
		return FALSE;
		}

	*width = pr->image_width;
	*height = pr->image_height;
	return TRUE;
}

gboolean pixbuf_renderer_get_scaled_size(PixbufRenderer *pr, gint *width, gint *height)
{
	g_return_val_if_fail(IS_PIXBUF_RENDERER(pr), FALSE);
	g_return_val_if_fail(width != nullptr && height != nullptr, FALSE);

	if (!pr->pixbuf && (!pr->image_width || !pr->image_height))
		{
		*width = 0;
		*height = 0;
		return FALSE;
		}

	*width = pr->width;
	*height = pr->height;
	return TRUE;
}

void pixbuf_renderer_set_size_early(PixbufRenderer *, guint, guint)
{
#if 0
	/** @FIXME this function does not consider the image orientation,
	so it probably only breaks something */
	gdouble zoom;
	gint w, h;

	zoom = pixbuf_renderer_zoom_get(pr);
	pr->image_width = width;
	pr->image_height = height;

	pr_zoom_clamp(pr, zoom, PR_ZOOM_FORCE, NULL);
#endif
}
