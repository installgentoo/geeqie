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

#include "layout-image.h"

#include <array>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <pango/pango.h>

#include <config.h>

#include "compat.h"
#include "debug.h"
#include "dnd.h"
#include "editors.h"
#include "exif.h"
#include "filedata.h"
#include "fullscreen.h"
#include "history-list.h"
#include "image-overlay.h"
#include "image.h"
#include "img-view.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "menu.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-utildlg.h"
#include "uri-utils.h"
#include "utilops.h"
#include "view-file.h"

static GtkWidget *layout_image_pop_menu(LayoutWindow *lw);
static void layout_image_set_buttons(LayoutWindow *lw);
static gboolean layout_image_animate_new_file(LayoutWindow *lw);
static void layout_image_animate_update_image(LayoutWindow *lw);

/*
 *----------------------------------------------------------------------------
 * full screen
 *----------------------------------------------------------------------------
 */

static void layout_image_full_screen_stop_func(FullScreenData *fs, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	/* restore image window */
	if (lw->image == fs->imd)
		lw->image = fs->normal_imd;

	lw->full_screen = nullptr;
}

void layout_image_full_screen_start(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->full_screen) return;

	lw->full_screen = fullscreen_start(lw->window, lw->image,
					   layout_image_full_screen_stop_func, lw);

	/* set to new image window */
	if (lw->full_screen->same_region)
		lw->image = lw->full_screen->imd;

	layout_image_set_buttons(lw);

	layout_actions_add_window(lw, lw->full_screen->window);

	image_osd_copy_status(lw->full_screen->normal_imd, lw->image);
	layout_image_animate_update_image(lw);

	/** @FIXME This is a hack to fix #1037 Fullscreen loads black
	 * The problem occurs when zoom is set to Original Size.
	 * An extra reload is required to force the image to be displayed.
	 * See also image-view.cc real_view_window_new()
	 * This is probably not the correct solution.
	 **/
	image_reload(lw->image);
}

void layout_image_full_screen_stop(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;
	if (!lw->full_screen) return;

	if (lw->image == lw->full_screen->imd)
		image_osd_copy_status(lw->image, lw->full_screen->normal_imd);

	fullscreen_stop(lw->full_screen);

	layout_image_animate_update_image(lw);
}

void layout_image_full_screen_toggle(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;
	if (lw->full_screen)
		{
		layout_image_full_screen_stop(lw);
		}
	else
		{
		layout_image_full_screen_start(lw);
		}
}

gboolean layout_image_full_screen_active(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return (lw->full_screen != nullptr);
}

/*
 *----------------------------------------------------------------------------
 * Animation
 *----------------------------------------------------------------------------
 */

struct AnimationData
{
	ImageWindow *iw;
	LayoutWindow *lw;
	GdkPixbufAnimation *gpa;
	GdkPixbufAnimationIter *iter;
	GdkPixbuf *gpb;
	FileData *data_adr;
	gint delay;
	gboolean valid;
	GCancellable *cancellable;
	GFile *in_file;
	GFileInputStream *gfstream;
};

static void image_animation_data_free(AnimationData *fd)
{
	if(!fd) return;
	if(fd->iter) g_object_unref(fd->iter);
	if(fd->gpa) g_object_unref(fd->gpa);
	if(fd->cancellable) g_object_unref(fd->cancellable);
	g_free(fd);
}

static gboolean animation_should_continue(AnimationData *fd)
{
	if (!fd->valid)
		return FALSE;

	return TRUE;
}

static gboolean show_next_frame(gpointer data)
{
	auto fd = static_cast<AnimationData*>(data);
	int delay;

	if(animation_should_continue(fd)==FALSE)
		{
		image_animation_data_free(fd);
		return FALSE;
		}

	PixbufRenderer *pr = PIXBUF_RENDERER(fd->iw->pr);

	if (gdk_pixbuf_animation_iter_advance(fd->iter,nullptr)==FALSE)
		{
		/* This indicates the animation is complete.
		   Return FALSE here to disable looping. */
		}

	fd->gpb = gdk_pixbuf_animation_iter_get_pixbuf(fd->iter);
	image_change_pixbuf(fd->iw,fd->gpb,pr->zoom,FALSE);

	if (fd->iw->func_update)
		fd->iw->func_update(fd->iw, fd->iw->data_update);

	delay = gdk_pixbuf_animation_iter_get_delay_time(fd->iter);
	if (delay!=fd->delay)
		{
		if (delay>0) /* Current frame not static. */
			{
			fd->delay=delay;
			g_timeout_add(delay,show_next_frame,fd);
			}
		else
			{
			image_animation_data_free(fd);
			}
		return FALSE;
		}

	return TRUE;
}

static gboolean layout_image_animate_check(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	if(!lw->options.animate || lw->image->image_fd == nullptr || lw->image->image_fd->extension == nullptr || (g_ascii_strcasecmp(lw->image->image_fd->extension,".GIF")!=0 && g_ascii_strcasecmp(lw->image->image_fd->extension,".WEBP")!=0))
		{
		if(lw->animation)
			{
			lw->animation->valid = FALSE;
			if (lw->animation->cancellable)
				{
				g_cancellable_cancel(lw->animation->cancellable);
				}
			lw->animation = nullptr;
			}
		return FALSE;
		}

	return TRUE;
}

static void layout_image_animate_update_image(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if(lw->options.animate && lw->animation)
		{
		if (lw->full_screen && lw->image != lw->full_screen->imd)
			lw->animation->iw = lw->full_screen->imd;
		else
			lw->animation->iw = lw->image;
		}
}


static void animation_async_ready_cb(GObject *, GAsyncResult *res, gpointer data)
{
	GError *error = nullptr;
	auto animation = static_cast<AnimationData *>(data);

	if (animation)
		{
		if (g_cancellable_is_cancelled(animation->cancellable))
			{
			gdk_pixbuf_animation_new_from_stream_finish(res, nullptr);
			g_object_unref(animation->in_file);
			g_object_unref(animation->gfstream);
			image_animation_data_free(animation);
			return;
			}

		animation->gpa = gdk_pixbuf_animation_new_from_stream_finish(res, &error);
		if (animation->gpa)
			{
			if (!gdk_pixbuf_animation_is_static_image(animation->gpa))
				{
				animation->iter = gdk_pixbuf_animation_get_iter(animation->gpa, nullptr);
				if (animation->iter)
					{
					animation->data_adr = animation->lw->image->image_fd;
					animation->delay = gdk_pixbuf_animation_iter_get_delay_time(animation->iter);
					animation->valid = TRUE;

					layout_image_animate_update_image(animation->lw);

					g_timeout_add(animation->delay, show_next_frame, animation);
					}
				}
			}
		else
			{
			log_printf("Error reading GIF file: %s\n", error->message);
			}

		g_object_unref(animation->in_file);
		g_object_unref(animation->gfstream);
		}
}

static gboolean layout_image_animate_new_file(LayoutWindow *lw)
{
	GFileInputStream *gfstream;
	GError *error = nullptr;
	AnimationData *animation;
	GFile *in_file;

	if(!layout_image_animate_check(lw)) return FALSE;

	if(lw->animation) lw->animation->valid = FALSE;

	if (lw->animation)
		{
		g_cancellable_cancel(lw->animation->cancellable);
		}

	animation = g_new0(AnimationData, 1);
	lw->animation = animation;
	animation->lw = lw;
	animation->cancellable = g_cancellable_new();

	in_file = g_file_new_for_path(lw->image->image_fd->path);
	animation->in_file = in_file;
	gfstream = g_file_read(in_file, nullptr, &error);
	if (gfstream)
		{
		animation->gfstream = gfstream;
		gdk_pixbuf_animation_new_from_stream_async(G_INPUT_STREAM(gfstream), animation->cancellable, animation_async_ready_cb, animation);
		}
	else
		{
		log_printf("Error reading animation file: %s\nError: %s\n", lw->image->image_fd->path, error->message);
		}

	return TRUE;
}

void layout_image_animate_toggle(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw) return;

	lw->options.animate = !lw->options.animate;

	action = gq_gtk_action_group_get_action(lw->action_group, "Animate");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.animate);

	layout_image_animate_new_file(lw);
}

/*
 *----------------------------------------------------------------------------
 * pop-up menus
 *----------------------------------------------------------------------------
 */

static void li_pop_menu_zoom_in_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_adjust(lw, get_zoom_increment());
}

static void li_pop_menu_zoom_out_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_image_zoom_adjust(lw, -get_zoom_increment());
}

static void li_pop_menu_zoom_1_1_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 1.0);
}

static void li_pop_menu_zoom_fit_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 0.0);
}

static void li_pop_menu_edit_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw;
	auto key = static_cast<const gchar *>(data);

	lw = static_cast<LayoutWindow *>(submenu_item_get_data(widget));

	if (!editor_window_flag_set(key))
		{
		layout_image_full_screen_stop(lw);
		}
	file_util_start_editor_from_file(key, layout_image_get_fd(lw), lw->window);
}

static GtkWidget *li_pop_menu_click_parent(GtkWidget *widget, LayoutWindow *lw)
{
	GtkWidget *menu;
	GtkWidget *parent;

	menu = gtk_widget_get_toplevel(widget);
	if (!menu) return nullptr;

	parent = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(menu), "click_parent"));

	if (!parent && lw->full_screen)
		{
		parent = lw->full_screen->imd->widget;
		}

	return parent;
}

static void li_pop_menu_copy_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_copy(layout_image_get_fd(lw), nullptr, nullptr,
		       li_pop_menu_click_parent(widget, lw));
}

static void li_pop_menu_copy_path_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_copy_path_to_clipboard(layout_image_get_fd(lw), TRUE);
}

static void li_pop_menu_move_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_move(layout_image_get_fd(lw), nullptr, nullptr,
		       li_pop_menu_click_parent(widget, lw));
}

static void li_pop_menu_rename_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_rename(layout_image_get_fd(lw), nullptr,
			 li_pop_menu_click_parent(widget, lw));
}

static void li_pop_menu_delete_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_delete(layout_image_get_fd(lw), nullptr,
			 li_pop_menu_click_parent(widget, lw));
}

static void li_pop_menu_full_screen_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_full_screen_toggle(lw);
}

static void li_pop_menu_animate_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_animate_toggle(lw);
}

static void layout_image_popup_menu_destroy_cb(GtkWidget *, gpointer data)
{
	auto editmenu_fd_list = static_cast<GList *>(data);

	filelist_free(editmenu_fd_list);
}

static GList *layout_image_get_fd_list(LayoutWindow *lw)
{
	GList *list = nullptr;
	FileData *fd = layout_image_get_fd(lw);

	if (fd)
		{
		if (lw->vf)
			/* optionally include sidecars if the filelist entry is not expanded */
			list = vf_selection_get_one(lw->vf, fd);
		else
			list = g_list_append(nullptr, file_data_ref(fd));
		}

	return list;
}

static GtkWidget *layout_image_pop_menu(LayoutWindow *lw)
{
	GtkWidget *menu;
	GtkWidget *item;
	GtkWidget *submenu;
	const gchar *path;
	gboolean fullscreen;
	GList *editmenu_fd_list;
	GtkAccelGroup *accel_group;

	path = layout_image_get_path(lw);
	fullscreen = layout_image_full_screen_active(lw);

	menu = popup_menu_short_lived();

	accel_group = gtk_accel_group_new();
	gtk_menu_set_accel_group(GTK_MENU(menu), accel_group);

	g_object_set_data(G_OBJECT(menu), "window_keys", nullptr);
	g_object_set_data(G_OBJECT(menu), "accel_group", accel_group);

	menu_item_add_icon(menu, _("Zoom _in"), GQ_ICON_ZOOM_IN, G_CALLBACK(li_pop_menu_zoom_in_cb), lw);
	menu_item_add_icon(menu, _("Zoom _out"), GQ_ICON_ZOOM_OUT, G_CALLBACK(li_pop_menu_zoom_out_cb), lw);
	menu_item_add_icon(menu, _("Zoom _1:1"), GQ_ICON_ZOOM_100, G_CALLBACK(li_pop_menu_zoom_1_1_cb), lw);
	menu_item_add_icon(menu, _("Zoom to fit"), GQ_ICON_ZOOM_FIT, G_CALLBACK(li_pop_menu_zoom_fit_cb), lw);
	menu_item_add_divider(menu);

	editmenu_fd_list = layout_image_get_fd_list(lw);
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(layout_image_popup_menu_destroy_cb), editmenu_fd_list);
	submenu = submenu_add_edit(menu, &item, G_CALLBACK(li_pop_menu_edit_cb), lw, editmenu_fd_list);
	if (!path) gtk_widget_set_sensitive(item, FALSE);
	menu_item_add_divider(submenu);

	item = menu_item_add_icon(menu, _("_Copy..."), GQ_ICON_COPY, G_CALLBACK(li_pop_menu_copy_cb), lw);
	if (!path) gtk_widget_set_sensitive(item, FALSE);
	item = menu_item_add(menu, _("_Move..."), G_CALLBACK(li_pop_menu_move_cb), lw);
	if (!path) gtk_widget_set_sensitive(item, FALSE);
	item = menu_item_add(menu, _("_Rename..."), G_CALLBACK(li_pop_menu_rename_cb), lw);
	if (!path) gtk_widget_set_sensitive(item, FALSE);
	item = menu_item_add(menu, _("_Copy to clipboard"), G_CALLBACK(li_pop_menu_copy_path_cb), lw);
	menu_item_add_divider(menu);

	item = menu_item_add_icon(menu,
				options->file_ops.confirm_delete ? _("_Delete...") :
					_("_Delete"), GQ_ICON_DELETE_SHRED,
								G_CALLBACK(li_pop_menu_delete_cb), lw);
	if (!path) gtk_widget_set_sensitive(item, FALSE);
	menu_item_add_divider(menu);

	if (!fullscreen)
		{
		menu_item_add_icon(menu, _("_Full screen"), GQ_ICON_FULLSCREEN, G_CALLBACK(li_pop_menu_full_screen_cb), lw);
		}
	else
		{
		menu_item_add_icon(menu, _("Exit _full screen"), GQ_ICON_LEAVE_FULLSCREEN, G_CALLBACK(li_pop_menu_full_screen_cb), lw);
		}

	menu_item_add_check(menu, _("GIF _animation"), lw->options.animate, G_CALLBACK(li_pop_menu_animate_cb), lw);

	return menu;
}

void layout_image_menu_popup(LayoutWindow *lw)
{
	GtkWidget *menu;

	menu = layout_image_pop_menu(lw);
	gtk_menu_popup_at_widget(GTK_MENU(menu), lw->image->widget, GDK_GRAVITY_EAST, GDK_GRAVITY_CENTER, nullptr);
}

/*
 *----------------------------------------------------------------------------
 * misc
 *----------------------------------------------------------------------------
 */

void layout_image_to_root(LayoutWindow *lw)
{
	image_to_root_window(lw->image, (image_zoom_get(lw->image) == 0));
}

/*
 *----------------------------------------------------------------------------
 * manipulation + accessors
 *----------------------------------------------------------------------------
 */

void layout_image_scroll(LayoutWindow *lw, gint x, gint y, gboolean connect_scroll)
{
	if (!layout_valid(&lw)) return;

	image_scroll(lw->image, x, y);

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_scroll(lw->full_screen->imd, x, y);
		}

	if (!connect_scroll) return;

}

void layout_image_zoom_adjust(LayoutWindow *lw, gdouble increment)
{
	if (!layout_valid(&lw)) return;

	image_zoom_adjust(lw->image, increment);

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_zoom_adjust(lw->full_screen->imd, increment);
		}
}

void layout_image_zoom_adjust_at_point(LayoutWindow *lw, gdouble increment, gint x, gint y)
{
	if (!layout_valid(&lw)) return;

	image_zoom_adjust_at_point(lw->image, increment, x, y);

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_zoom_adjust_at_point(lw->full_screen->imd, increment, x, y);
		}
}

void layout_image_zoom_set(LayoutWindow *lw, gdouble zoom)
{
	if (!layout_valid(&lw)) return;

	image_zoom_set(lw->image, zoom);

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_zoom_set(lw->full_screen->imd, zoom);
		}
}

void layout_image_zoom_set_fill_geometry(LayoutWindow *lw, gboolean vertical)
{
	if (!layout_valid(&lw)) return;

	image_zoom_set_fill_geometry(lw->image, vertical);

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_zoom_set_fill_geometry(lw->full_screen->imd, vertical);
		}
}

void layout_image_reset_orientation(LayoutWindow *lw)
{
	ImageWindow *imd= lw->image;

	if (!layout_valid(&lw)) return;
	if (!imd || !imd->pr || !imd->image_fd) return;

	if (imd->orientation < 1 || imd->orientation > 8) imd->orientation = 1;

	if (options->image.exif_rotate_enable)
		{
		if (g_strcmp0(imd->image_fd->format_name, "heif") != 0)
			{
			imd->orientation = metadata_read_int(imd->image_fd, ORIENTATION_KEY, EXIF_ORIENTATION_TOP_LEFT);
			}
		else
			{
			imd->orientation = EXIF_ORIENTATION_TOP_LEFT;
			}
		}
	else
		{
		imd->orientation = 1;
		}

	if (imd->image_fd->user_orientation != 0)
		{
		 imd->orientation = imd->image_fd->user_orientation;
		}

	pixbuf_renderer_set_orientation(PIXBUF_RENDERER(imd->pr), imd->orientation);
}

void layout_image_set_desaturate(LayoutWindow *lw, gboolean desaturate)
{
	if (!layout_valid(&lw)) return;

	image_set_desaturate(lw->image, desaturate);
}

gboolean layout_image_get_desaturate(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return image_get_desaturate(lw->image);
}

void layout_image_set_overunderexposed(LayoutWindow *lw, gboolean overunderexposed)
{
	if (!layout_valid(&lw)) return;

	image_set_overunderexposed(lw->image, overunderexposed);
}

const gchar *layout_image_get_path(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	return image_get_path(lw->image);
}

FileData *layout_image_get_fd(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	return image_get_fd(lw->image);
}

gint layout_image_get_index(LayoutWindow *lw)
{
	return layout_list_get_index(lw, image_get_fd(lw->image));
}

/*
 *----------------------------------------------------------------------------
 * image changers
 *----------------------------------------------------------------------------
 */

void layout_image_set_fd(LayoutWindow *lw, FileData *fd)
{
	if (!layout_valid(&lw)) return;

	image_change_fd(lw->image, fd, image_zoom_get_default(lw->image));

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_change_fd(lw->full_screen->imd, fd, image_zoom_get_default(lw->full_screen->imd));
		}


	layout_list_sync_fd(lw, fd);
	layout_image_animate_new_file(lw);
}

void layout_image_set_with_ahead(LayoutWindow *lw, FileData *fd, FileData *read_ahead_fd)
{
	if (!layout_valid(&lw)) return;

/** @FIXME This should be handled at the caller: in vflist_select_image
	if (path)
		{
		const gchar *old_path;

		old_path = layout_image_get_path(lw);
		if (old_path && strcmp(path, old_path) == 0) return;
		}
*/
	layout_image_set_fd(lw, fd);
	if (options->image.enable_read_ahead) image_prebuffer_set(lw->image, read_ahead_fd);
}

void layout_image_set_index(LayoutWindow *lw, gint index)
{
	FileData *fd;
	FileData *read_ahead_fd;
	gint old;

	if (!layout_valid(&lw)) return;

	old = layout_list_get_index(lw, layout_image_get_fd(lw));
	fd = layout_list_get_fd(lw, index);

	if (old > index)
		{
		read_ahead_fd = layout_list_get_fd(lw, index - 1);
		}
	else
		{
		read_ahead_fd = layout_list_get_fd(lw, index + 1);
		}

	if (layout_selection_count(lw, nullptr) > 1)
		{
		GList *x = layout_selection_list_by_index(lw);
		GList *y;
		GList *last;

		for (last = y = x; y; y = y->next)
			last = y;
		for (y = x; y && (GPOINTER_TO_INT(y->data)) != index; y = y->next)
			;

		if (y)
			{
			gint newindex;

			if ((index > old && (index != GPOINTER_TO_INT(last->data) || old != GPOINTER_TO_INT(x->data)))
			    || (old == GPOINTER_TO_INT(last->data) && index == GPOINTER_TO_INT(x->data)))
				{
				if (y->next)
					newindex = GPOINTER_TO_INT(y->next->data);
				else
					newindex = GPOINTER_TO_INT(x->data);
				}
			else
				{
				if (y->prev)
					newindex = GPOINTER_TO_INT(y->prev->data);
				else
					newindex = GPOINTER_TO_INT(last->data);
				}

			read_ahead_fd = layout_list_get_fd(lw, newindex);
			}

		while (x)
			x = g_list_remove(x, x->data);
		}

	layout_image_set_with_ahead(lw, fd, read_ahead_fd);
}

void layout_image_refresh(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	image_reload(lw->image);
}

void layout_image_color_profile_set(LayoutWindow *lw, gint input_type, gboolean use_image)
{
	if (!layout_valid(&lw)) return;

	image_color_profile_set(lw->image, input_type, use_image);
}

gboolean layout_image_color_profile_get(LayoutWindow *lw, gint &input_type, gboolean &use_image)
{
	if (!layout_valid(&lw)) return FALSE;

	return image_color_profile_get(lw->image, input_type, use_image);
}

void layout_image_color_profile_set_use(LayoutWindow *lw, gboolean enable)
{
	if (!layout_valid(&lw)) return;

	image_color_profile_set_use(lw->image, enable);
}

gboolean layout_image_color_profile_get_use(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return image_color_profile_get_use(lw->image);
}

gboolean layout_image_color_profile_get_status(LayoutWindow *lw, gchar **image_profile, gchar **screen_profile)
{
	if (!layout_valid(&lw)) return FALSE;

	return image_color_profile_get_status(lw->image, image_profile, screen_profile);
}

/*
 *----------------------------------------------------------------------------
 * list walkers
 *----------------------------------------------------------------------------
 */

void layout_image_next(LayoutWindow *lw)
{
	gint current;

	if (!layout_valid(&lw)) return;

	if (layout_selection_count(lw, nullptr) > 1)
		{
		GList *x = layout_selection_list_by_index(lw);
		gint old = layout_list_get_index(lw, layout_image_get_fd(lw));
		GList *y;

		for (y = x; y && (GPOINTER_TO_INT(y->data)) != old; y = y->next)
			;
		if (y)
			{
			if (y->next)
				layout_image_set_index(lw, GPOINTER_TO_INT(y->next->data));
			else
				{
				if (options->circular_selection_lists)
					{
					layout_image_set_index(lw, GPOINTER_TO_INT(x->data));
					}
				}
			}
		while (x)
			x = g_list_remove(x, x->data);
		if (y) /* not dereferenced */
			return;
		}

	current = layout_image_get_index(lw);

	if (current >= 0)
		{
		if (static_cast<guint>(current) < layout_list_count(lw, nullptr) - 1)
			{
			layout_image_set_index(lw, current + 1);
			}
		else
			{
			image_osd_icon(lw->image, IMAGE_OSD_LAST, -1);
			}
		}
	else
		{
		layout_image_set_index(lw, 0);
		}
}

void layout_image_prev(LayoutWindow *lw)
{
	gint current;

	if (!layout_valid(&lw)) return;

	if (layout_selection_count(lw, nullptr) > 1)
		{
		GList *x = layout_selection_list_by_index(lw);
		gint old = layout_list_get_index(lw, layout_image_get_fd(lw));
		GList *y;
		GList *last;

		for (last = y = x; y; y = y->next)
			last = y;
		for (y = x; y && (GPOINTER_TO_INT(y->data)) != old; y = y->next)
			;
		if (y)
			{
			if (y->prev)
				layout_image_set_index(lw, GPOINTER_TO_INT(y->prev->data));
			else
				{
				if (options->circular_selection_lists)
					{
					layout_image_set_index(lw, GPOINTER_TO_INT(last->data));
					}
				}
			}
		while (x)
			x = g_list_remove(x, x->data);
		if (y) /* not dereferenced */
			return;
		}

	current = layout_image_get_index(lw);

	if (current >= 0)
		{
		if (current > 0)
			{
			layout_image_set_index(lw, current - 1);
			}
		else
			{
			image_osd_icon(lw->image, IMAGE_OSD_FIRST, -1);
			}
		}
	else
		{
		layout_image_set_index(lw, layout_list_count(lw, nullptr) - 1);
		}
}

void layout_image_first(LayoutWindow *lw)
{
	gint current;

	if (!layout_valid(&lw)) return;

	current = layout_image_get_index(lw);
	if (current != 0 && layout_list_count(lw, nullptr) > 0)
		{
		layout_image_set_index(lw, 0);
		}
}

void layout_image_last(LayoutWindow *lw)
{
	gint current;
	gint count;

	if (!layout_valid(&lw)) return;

	current = layout_image_get_index(lw);
	count = layout_list_count(lw, nullptr);
	if (current != count - 1 && count > 0)
		{
		layout_image_set_index(lw, count - 1);
		}
}

/*
 *----------------------------------------------------------------------------
 * mouse callbacks
 *----------------------------------------------------------------------------
 */

static void layout_image_button_cb(ImageWindow *imd, GdkEventButton *event, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;

	switch (event->button)
		{
		case MOUSE_BUTTON_LEFT:
			if (event->type == GDK_2BUTTON_PRESS)
				{
				layout_image_full_screen_toggle(lw);
				}
			else if (options->image_l_click_video && options->image_l_click_video_editor && imd-> image_fd && imd->image_fd->format_class == FORMAT_CLASS_VIDEO)
				{
				start_editor_from_file(options->image_l_click_video_editor, imd->image_fd);
				}
			break;
		case MOUSE_BUTTON_RIGHT:
			menu = layout_image_pop_menu(lw);
			if (imd == lw->image)
				{
				g_object_set_data(G_OBJECT(menu), "click_parent", imd->widget);
				}
			gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
			break;
		default:
			break;
		}
}

static void layout_image_scroll_cb(ImageWindow *imd, GdkEventScroll *event, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if ((event->state & GDK_CONTROL_MASK) || imd->mouse_wheel_mode)
		{
		switch (event->direction)
			{
			case GDK_SCROLL_UP:
				layout_image_zoom_adjust_at_point(lw, get_zoom_increment(), event->x, event->y);
				break;
			case GDK_SCROLL_DOWN:
				layout_image_zoom_adjust_at_point(lw, -get_zoom_increment(), event->x, event->y);
				break;
			default:
				break;
			}
		}
	else if (options->mousewheel_scrolls)
		{
		switch (event->direction)
			{
			case GDK_SCROLL_UP:
				image_scroll(imd, 0, -MOUSEWHEEL_SCROLL_SIZE);
				break;
			case GDK_SCROLL_DOWN:
				image_scroll(imd, 0, MOUSEWHEEL_SCROLL_SIZE);
				break;
			case GDK_SCROLL_LEFT:
				image_scroll(imd, -MOUSEWHEEL_SCROLL_SIZE, 0);
				break;
			case GDK_SCROLL_RIGHT:
				image_scroll(imd, MOUSEWHEEL_SCROLL_SIZE, 0);
				break;
			default:
				break;
			}
		}
	else
		{
		switch (event->direction)
			{
			case GDK_SCROLL_UP:
				layout_image_prev(lw);
				break;
			case GDK_SCROLL_DOWN:
				layout_image_next(lw);
				break;
			default:
				break;
			}
		}
}

static void layout_image_drag_cb(ImageWindow *imd, GdkEventMotion *event, gdouble dx, gdouble dy, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gdouble sx;
	gdouble sy;

	if (lw->full_screen && lw->image != lw->full_screen->imd &&
	    imd != lw->full_screen->imd)
		{
		if (event->state & GDK_CONTROL_MASK)
			{
			image_get_scroll_center(imd, &sx, &sy);
			}
		else
			{
			image_get_scroll_center(lw->full_screen->imd, &sx, &sy);
			sx += dx;
			sy += dy;
			}
		image_set_scroll_center(lw->full_screen->imd, sx, sy);
		}
}


static void layout_image_set_buttons(LayoutWindow *lw)
{
	image_set_button_func(lw->image, layout_image_button_cb, lw);
	image_set_scroll_func(lw->image, layout_image_scroll_cb, lw);
}

/*
 *----------------------------------------------------------------------------
 * setup
 *----------------------------------------------------------------------------
 */

static void layout_image_update_cb(ImageWindow *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_status_update_image(lw);
}


void layout_image_init(LayoutWindow *lw)
{
	ImageWindow *imd = image_new(TRUE);

	lw->image = imd;

	g_object_ref(imd->widget);

	image_background_set_color_from_options(imd, FALSE);
	image_auto_refresh_enable(imd, TRUE);

	image_color_profile_set(imd,
				options->color_profile.input_type,
				options->color_profile.use_image);
	image_color_profile_set_use(imd, options->color_profile.enabled);

	/* Activate — inlined from layout_image_activate */
	image_set_update_func(imd, layout_image_update_cb, lw);
	layout_image_set_buttons(lw);
	image_set_drag_func(imd, layout_image_drag_cb, lw);
}


/*
 *-----------------------------------------------------------------------------
 * maintenance (for rename, move, remove)
 *-----------------------------------------------------------------------------
 */

static void layout_image_maint_renamed(LayoutWindow *lw, FileData *fd)
{
	if (fd == layout_image_get_fd(lw))
		{
		image_set_fd(lw->image, fd);
		}
}

void layout_image_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (!(type & NOTIFY_CHANGE) || !fd->change) return;

	DEBUG_1("Notify layout_image: %s %04x", fd->path, type);

	switch (fd->change->type)
		{
		case FILEDATA_CHANGE_MOVE:
		case FILEDATA_CHANGE_RENAME:
			layout_image_maint_renamed(lw, fd);
			break;
		case FILEDATA_CHANGE_DELETE:
		case FILEDATA_CHANGE_COPY:
		case FILEDATA_CHANGE_UNSPECIFIED:
			break;
		}

}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
