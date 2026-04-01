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

#ifndef PIXBUF_RENDERER_H
#define PIXBUF_RENDERER_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "typedefs.h"

struct PixbufRenderer;

#define TYPE_PIXBUF_RENDERER		(pixbuf_renderer_get_type())
#define PIXBUF_RENDERER(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), TYPE_PIXBUF_RENDERER, PixbufRenderer))
#define PIXBUF_RENDERER_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), TYPE_PIXBUF_RENDERER, PixbufRendererClass))
#define IS_PIXBUF_RENDERER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), TYPE_PIXBUF_RENDERER))
#define IS_PIXBUF_RENDERER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), TYPE_PIXBUF_RENDERER))
#define PIXBUF_RENDERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), TYPE_PIXBUF_RENDERER, PixbufRendererClass))

/**
 * @def PR_ALPHA_CHECK_SIZE
 * alpha channel checkerboard (same as gimp)
 */
#define PR_ALPHA_CHECK_SIZE 16

/**
 * @def PR_MIN_SCALE_SIZE
 * when scaling image to below this size, use nearest pixel for scaling
 * (below about 4, the other scale types become slow generating their conversion tables)
 */
#define PR_MIN_SCALE_SIZE 8

/**
 * @def PR_CACHE_SIZE_DEFAULT
 * default size of tile cache (MiB)
 */
#define PR_CACHE_SIZE_DEFAULT 8

/**
 * @def ROUND_UP
 * round A up to integer count of B
 */
#define ROUND_UP(A,B)   ((gint)(((A)+(B)-1)/(B))*(B))

/**
 * @def ROUND_DOWN
 * round A down to integer count of B
 */
#define ROUND_DOWN(A,B) ((gint)(((A))/(B))*(B))


using PixbufRendererTileRequestFunc = gint (*)(PixbufRenderer *, gint, gint, gint, gint, GdkPixbuf *, gpointer);
using PixbufRendererTileDisposeFunc = void (*)(PixbufRenderer *, gint, gint, gint, gint, GdkPixbuf *, gpointer);

using PixbufRendererPostProcessFunc = void (*)(PixbufRenderer *, GdkPixbuf **, gint, gint, gint, gint, gpointer);

enum ImageRenderType {
	TILE_RENDER_NONE = 0, /**< do nothing */
	TILE_RENDER_AREA, /**< render an area of the tile */
	TILE_RENDER_ALL /**< render the whole tile */
};

enum OverlayRendererFlags {
	OVL_NORMAL 	= 0,
	OVL_RELATIVE 	= 1 << 0, /**< x,y coordinates are relative, negative values start bottom right */
	/* OVL_HIDE_ON_SCROLL = 1 << 1*/ /**< hide temporarily when scrolling (not yet implemented) */
};

struct RendererFuncs
{
	void (*area_changed)(void *renderer, gint src_x, gint src_y, gint src_w, gint src_h); /**< pixbuf area changed */
	void (*invalidate_region)(void *renderer, GdkRectangle region);
	void (*scroll)(void *renderer, gint x_off, gint y_off); /**< scroll */
	void (*update_viewport)(void *renderer); /**< window / wiewport / border color has changed */
	void (*update_pixbuf)(void *renderer, gboolean lazy); /**< pixbuf has changed */
	void (*update_zoom)(void *renderer, gboolean lazy); /**< zoom has changed */

	gint (*overlay_add)(void *renderer, GdkPixbuf *pixbuf, gint x, gint y, OverlayRendererFlags flags);
	void (*overlay_set)(void *renderer, gint id, GdkPixbuf *pixbuf, gint x, gint y);
	gboolean (*overlay_get)(void *renderer, gint id, GdkPixbuf **pixbuf, gint *x, gint *y);

	void (*free)(void *renderer);
};

struct PixbufRenderer
{
	GtkEventBox eventbox;

	gint image_width;	/**< image actual dimensions (pixels) */
	gint image_height;

	GdkPixbuf *pixbuf;

	gint window_width;	/**< allocated size of window (drawing area) */
	gint window_height;

	gint viewport_width;	/**< allocated size of viewport (same as window for normal mode, half of window for SBS mode) */
	gint viewport_height;

	gint x_offset;		/**< offset of image start (non-zero when viewport < window) */
	gint y_offset;

	gint x_mouse; /**< coordinates of the mouse taken from GtkEvent */
	gint y_mouse;

	gint vis_width;		/**< dimensions of visible part of image */
	gint vis_height;

	gint width;		/**< size of scaled image (result) */
	gint height;

	gint x_scroll;		/**< scroll offset of image (into width, height to start drawing) */
	gint y_scroll;

	gdouble norm_center_x;	/**< coordinates of viewport center in the image, in range 0.0 - 1.0 */
	gdouble norm_center_y;  /**< these coordinates are used for ScrollReset::NOCHANGE and should be preserved over periods with NULL pixbuf */

	gdouble subpixel_x_scroll; /**< subpixel scroll alignment, used to prevent accumulation of rounding errors */
	gdouble subpixel_y_scroll;

	gdouble zoom_min;
	gdouble zoom_max;
	gdouble zoom;		/**< zoom we want (0 is auto) */
	gdouble scale;		/**< zoom we got (should never be 0) */

	GdkInterpType zoom_quality;
	gboolean zoom_2pass;

	ScrollReset scroll_reset;

	gboolean has_frame;

	GdkRGBA color;

	/*< private >*/
	gboolean in_drag;
	gint drag_last_x;
	gint drag_last_y;
	gint drag_moved;

	gint source_tiles_cache_size;

	GList *source_tiles;	/**< list of active source tiles */
	gint source_tile_width;
	gint source_tile_height;

	PixbufRendererTileRequestFunc func_tile_request;
	PixbufRendererTileDisposeFunc func_tile_dispose;

	gpointer func_tile_data;

	PixbufRendererPostProcessFunc func_post_process;
	gpointer post_process_user_data;
	gint post_process_slow;

	gboolean delay_flip;
	gboolean loading;
	gboolean complete;
	gboolean debug_updated; /**< debug only */

	guint scroller_id; /**< event source id */
	gint scroller_overlay;
	gint scroller_x;
	gint scroller_y;
	gint scroller_xpos;
	gint scroller_ypos;
	gint scroller_xinc;
	gint scroller_yinc;

	gint orientation;

	RendererFuncs *renderer;
};

struct PixbufRendererClass
{
	GtkEventBoxClass parent_class;

	void (*zoom)(PixbufRenderer *pr, gdouble zoom);
	void (*clicked)(PixbufRenderer *pr, GdkEventButton *event);
	void (*scroll_notify)(PixbufRenderer *pr);
	void (*update_pixel)(PixbufRenderer *pr);

	void (*render_complete)(PixbufRenderer *pr);
	void (*drag)(PixbufRenderer *pr, GdkEventMotion *event);
};




GType pixbuf_renderer_get_type();

PixbufRenderer *pixbuf_renderer_new();

void pixbuf_renderer_set_pixbuf(PixbufRenderer *pr, GdkPixbuf *pixbuf, gdouble zoom);

void pixbuf_renderer_set_pixbuf_lazy(PixbufRenderer *pr, GdkPixbuf *pixbuf, gdouble zoom, gint orientation);


GdkPixbuf *pixbuf_renderer_get_pixbuf(PixbufRenderer *pr);

void pixbuf_renderer_set_orientation(PixbufRenderer *pr, gint orientation);

void pixbuf_renderer_set_post_process_func(PixbufRenderer *pr, PixbufRendererPostProcessFunc func, gpointer user_data, gboolean slow);

void pixbuf_renderer_move(PixbufRenderer *pr, PixbufRenderer *source);
void pixbuf_renderer_copy(PixbufRenderer *pr, PixbufRenderer *source);

void pixbuf_renderer_area_changed(PixbufRenderer *pr, gint x, gint y, gint width, gint height);

/* scrolling */

void pixbuf_renderer_scroll(PixbufRenderer *pr, gint x, gint y);

void pixbuf_renderer_get_scroll_center(PixbufRenderer *pr, gdouble *x, gdouble *y);
void pixbuf_renderer_set_scroll_center(PixbufRenderer *pr, gdouble x, gdouble y);
/* zoom */

void pixbuf_renderer_zoom_adjust(PixbufRenderer *pr, gdouble increment);
void pixbuf_renderer_zoom_adjust_at_point(PixbufRenderer *pr, gdouble increment, gint x, gint y);

void pixbuf_renderer_zoom_set(PixbufRenderer *pr, gdouble zoom);
gdouble pixbuf_renderer_zoom_get(PixbufRenderer *pr);
gdouble pixbuf_renderer_zoom_get_scale(PixbufRenderer *pr);

/* sizes */

gboolean pixbuf_renderer_get_image_size(PixbufRenderer *pr, gint *width, gint *height);
gboolean pixbuf_renderer_get_scaled_size(PixbufRenderer *pr, gint *width, gint *height);

void pixbuf_renderer_set_color(PixbufRenderer *pr, GdkRGBA *color);

/* overlay */

gint pixbuf_renderer_overlay_add(PixbufRenderer *pr, GdkPixbuf *pixbuf, gint x, gint y,
				 OverlayRendererFlags flags);
void pixbuf_renderer_overlay_set(PixbufRenderer *pr, gint id, GdkPixbuf *pixbuf, gint x, gint y);
void pixbuf_renderer_overlay_remove(PixbufRenderer *pr, gint id);

void pixbuf_renderer_set_size_early(PixbufRenderer *pr, guint width, guint height);

/**
 * @struct SourceTile
 * protected - for renderer use only
 */
struct SourceTile
{
	gint x;
	gint y;
	GdkPixbuf *pixbuf;
	gboolean blank;
};


void pr_render_complete_signal(PixbufRenderer *pr);

void pr_tile_coords_map_orientation(gint orientation,
                                    gdouble tile_x, gdouble tile_y, /**< coordinates of the tile */
                                    gdouble image_w, gdouble image_h,
                                    gdouble tile_w, gdouble tile_h,
                                    gdouble &res_x, gdouble &res_y);
GdkRectangle pr_tile_region_map_orientation(gint orientation,
                                            GdkRectangle area, /**< coordinates of the area inside tile */
                                            gint tile_w, gint tile_h);
GdkRectangle pr_coords_map_orientation_reverse(gint orientation,
                                               GdkRectangle area,
                                               gint tile_w, gint tile_h);
void pr_scale_region(GdkRectangle &region, gdouble scale);
#endif
