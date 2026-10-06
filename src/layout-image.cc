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

#include <algorithm>
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
#include "filedata.h"
#include "fullscreen.h"
#include "history-list.h"
#include "image-overlay.h"
#include "image.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "menu.h"
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

constexpr gint VIDEO_PREVIEW_FPS = 15;
constexpr gint VIDEO_PREVIEW_MAX_DIMENSION = 1920;

enum class VideoAnimationOperation {
	NONE,
	PROBING,
	READING
};

struct VideoAnimationData
{
	ImageWindow *iw;
	LayoutWindow *lw;
	FileData *fd;
	GSubprocess *process;
	GInputStream *stream;
	GCancellable *cancellable;
	guchar *pixels;
	gsize frame_size;
	gint width;
	gint height;
	guint timer_id;
	VideoAnimationOperation operation;
	gboolean valid;
};

static void video_animation_free(VideoAnimationData *video)
{
	if (!video) return;

	if (video->timer_id) g_source_remove(video->timer_id);
	if (video->pixels) g_free(video->pixels);
	if (video->process) g_object_unref(video->process);
	if (video->stream) g_object_unref(video->stream);
	if (video->cancellable) g_object_unref(video->cancellable);
	file_data_unref(video->fd);
	g_free(video);
}

static void video_animation_stop(LayoutWindow *lw)
{
	if (!lw || !lw->video_animation) return;

	auto video = lw->video_animation;
	lw->video_animation = nullptr;
	video->valid = FALSE;

	if (video->process) g_subprocess_force_exit(video->process);
	g_cancellable_cancel(video->cancellable);

	if (video->timer_id)
		{
		g_source_remove(video->timer_id);
		video->timer_id = 0;
		video_animation_free(video);
		}
	else if (video->operation == VideoAnimationOperation::NONE)
		{
		video_animation_free(video);
		}
}

static void video_animation_finish(VideoAnimationData *video)
{
	if (video->lw && video->lw->video_animation == video)
		video->lw->video_animation = nullptr;
	video->valid = FALSE;
	if (video->process) g_subprocess_force_exit(video->process);
	video_animation_free(video);
}

static void video_animation_frame_free(guchar *pixels, gpointer)
{
	g_free(pixels);
}

static gboolean video_animation_read_next(gpointer data);

static void video_animation_read_cb(GObject *source, GAsyncResult *result, gpointer data)
{
	auto video = static_cast<VideoAnimationData *>(data);
	gsize bytes_read = 0;
	GError *error = nullptr;
	const gboolean complete = g_input_stream_read_all_finish(G_INPUT_STREAM(source), result, &bytes_read, &error);
	guchar *pixels = video->pixels;
	video->pixels = nullptr;
	video->operation = VideoAnimationOperation::NONE;

	if (!video->valid)
		{
		g_free(pixels);
		g_clear_error(&error);
		video_animation_free(video);
		return;
		}

	if (!complete || bytes_read != video->frame_size)
		{
		if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			DEBUG_1("Video preview stopped: %s", error->message);
		g_free(pixels);
		g_clear_error(&error);
		video_animation_finish(video);
		return;
		}
	g_clear_error(&error);

	auto pixbuf = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB, FALSE, 8,
	                                        video->width, video->height, video->width * 3,
	                                        video_animation_frame_free, nullptr);
	if (!pixbuf)
		{
		g_free(pixels);
		video_animation_finish(video);
		return;
		}

	layout_image_animate_update_image(video->lw);
	if (video->iw && image_get_fd(video->iw) == video->fd)
		{
		image_change_pixbuf(video->iw, pixbuf, image_zoom_get(video->iw), FALSE);
		if (video->iw->func_update)
			video->iw->func_update(video->iw, video->iw->data_update);
		}
	else
		{
		g_object_unref(pixbuf);
		video_animation_finish(video);
		return;
		}
	g_object_unref(pixbuf);

	video->timer_id = g_timeout_add(1000 / VIDEO_PREVIEW_FPS, video_animation_read_next, video);
}

static gboolean video_animation_read_next(gpointer data)
{
	auto video = static_cast<VideoAnimationData *>(data);
	video->timer_id = 0;

	if (!video->valid)
		{
		video_animation_free(video);
		return G_SOURCE_REMOVE;
		}

	video->pixels = static_cast<guchar *>(g_try_malloc(video->frame_size));
	if (!video->pixels)
		{
		DEBUG_1("Video preview frame allocation failed");
		video_animation_finish(video);
		return G_SOURCE_REMOVE;
		}

	video->operation = VideoAnimationOperation::READING;
	g_input_stream_read_all_async(video->stream, video->pixels, video->frame_size,
	                              G_PRIORITY_DEFAULT, video->cancellable,
	                              video_animation_read_cb, video);
	return G_SOURCE_REMOVE;
}

static gboolean video_animation_parse_probe(const gchar *text, gint *width, gint *height, gint *rotation)
{
	g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
	for (gchar **line = lines; *line; line++)
		{
		gchar *value = strchr(*line, '=');
		if (!value) continue;
		*value++ = '\0';
		if (strcmp(*line, "width") == 0) *width = atoi(value);
		else if (strcmp(*line, "height") == 0) *height = atoi(value);
		else if (strcmp(*line, "rotation") == 0) *rotation = atoi(value);
		}

	return *width > 0 && *height > 0;
}

static void video_animation_start_decoder(VideoAnimationData *video, gint source_width, gint source_height, gint rotation)
{
	gint normalized_rotation = rotation % 360;
	if (normalized_rotation < 0) normalized_rotation += 360;
	const gboolean transpose = normalized_rotation == 90 || normalized_rotation == 270;
	gint width = transpose ? source_height : source_width;
	gint height = transpose ? source_width : source_height;
	const gint largest_dimension = std::max(width, height);
	if (largest_dimension > VIDEO_PREVIEW_MAX_DIMENSION)
		{
		width = std::max(2, static_cast<gint>((static_cast<gint64>(width) * VIDEO_PREVIEW_MAX_DIMENSION / largest_dimension) & ~1));
		height = std::max(2, static_cast<gint>((static_cast<gint64>(height) * VIDEO_PREVIEW_MAX_DIMENSION / largest_dimension) & ~1));
		}

	const gsize rowstride = static_cast<gsize>(width) * 3;
	if (rowstride > G_MAXSIZE / static_cast<gsize>(height))
		{
		DEBUG_1("Video preview dimensions are too large");
		video_animation_finish(video);
		return;
		}

	video->width = width;
	video->height = height;
	video->frame_size = rowstride * height;

	g_autofree gchar *filter = nullptr;
	if (normalized_rotation == 90)
		filter = g_strdup_printf("fps=%d,transpose=1,scale=%d:%d:flags=bilinear", VIDEO_PREVIEW_FPS, width, height);
	else if (normalized_rotation == 180)
		filter = g_strdup_printf("fps=%d,hflip,vflip,scale=%d:%d:flags=bilinear", VIDEO_PREVIEW_FPS, width, height);
	else if (normalized_rotation == 270)
		filter = g_strdup_printf("fps=%d,transpose=2,scale=%d:%d:flags=bilinear", VIDEO_PREVIEW_FPS, width, height);
	else
		filter = g_strdup_printf("fps=%d,scale=%d:%d:flags=bilinear", VIDEO_PREVIEW_FPS, width, height);

	const gchar *const argv[] = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-stream_loop", "-1",
	                             "-noautorotate", "-i", video->fd->path, "-map", "0:v:0", "-an", "-sn", "-dn",
	                             "-vf", filter, "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1", nullptr};
	g_autoptr(GError) error = nullptr;
	video->process = g_subprocess_newv(argv,
	                                  static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE),
	                                  &error);
	if (!video->process)
		{
		DEBUG_1("Could not start ffmpeg video preview: %s", error->message);
		video_animation_finish(video);
		return;
		}
	video->stream = static_cast<GInputStream *>(g_object_ref(g_subprocess_get_stdout_pipe(video->process)));
	video_animation_read_next(video);
}

static void video_animation_probe_cb(GObject *source, GAsyncResult *result, gpointer data)
{
	auto video = static_cast<VideoAnimationData *>(data);
	gchar *stdout_text = nullptr;
	gchar *stderr_text = nullptr;
	GError *error = nullptr;
	const gboolean complete = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result,
	                                                               &stdout_text, &stderr_text, &error);
	const gboolean successful = complete && g_subprocess_get_successful(G_SUBPROCESS(source));
	video->operation = VideoAnimationOperation::NONE;
	if (video->process)
		{
		g_object_unref(video->process);
		video->process = nullptr;
		}

	if (!video->valid)
		{
		g_free(stdout_text);
		g_free(stderr_text);
		g_clear_error(&error);
		video_animation_free(video);
		return;
		}

	gint width = 0;
	gint height = 0;
	gint rotation = 0;
	const gboolean parsed = successful && stdout_text && video_animation_parse_probe(stdout_text, &width, &height, &rotation);
	if (!parsed)
		{
		if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			DEBUG_1("Could not probe video preview: %s", error->message);
		else if (stderr_text && *stderr_text)
			DEBUG_1("Could not probe video preview: %s", stderr_text);
		g_free(stdout_text);
		g_free(stderr_text);
		g_clear_error(&error);
		video_animation_finish(video);
		return;
		}
	g_free(stdout_text);
	g_free(stderr_text);
	g_clear_error(&error);

	video_animation_start_decoder(video, width, height, rotation);
}

static gboolean video_animation_new_file(LayoutWindow *lw)
{
	g_autofree gchar *ffmpeg = g_find_program_in_path("ffmpeg");
	g_autofree gchar *ffprobe = g_find_program_in_path("ffprobe");
	if (!ffmpeg || !ffprobe)
		{
		DEBUG_1("ffmpeg and ffprobe are required for video previews");
		return FALSE;
		}

	auto video = g_new0(VideoAnimationData, 1);
	lw->video_animation = video;
	video->lw = lw;
	video->fd = file_data_ref(lw->image->image_fd);
	video->cancellable = g_cancellable_new();
	video->valid = TRUE;
	video->operation = VideoAnimationOperation::PROBING;

	const gchar *const argv[] = {"ffprobe", "-v", "error", "-select_streams", "v:0",
	                             "-show_entries", "stream=width,height:stream_side_data=rotation",
	                             "-of", "default=noprint_wrappers=1", video->fd->path, nullptr};
	g_autoptr(GError) error = nullptr;
	video->process = g_subprocess_newv(argv,
	                                  static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
	                                  &error);
	if (!video->process)
		{
		DEBUG_1("Could not start ffprobe video preview: %s", error->message);
		video_animation_finish(video);
		return FALSE;
		}
	g_subprocess_communicate_utf8_async(video->process, nullptr, video->cancellable,
	                                   video_animation_probe_cb, video);
	return TRUE;
}

/**
 * Starts or stops a video preview to match fullscreen, the other half of the rule in layout_image_animate_check.
 *
 * Only a video is touched here: an animated image plays in either mode, and restarting one would drop it back to
 * its first frame every time fullscreen is entered.
 */
static void layout_image_video_animate_sync(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;
	if (!lw->image->image_fd || lw->image->image_fd->format_class != FORMAT_CLASS_VIDEO) return;

	if (lw->full_screen)
		{
		layout_image_animate_new_file(lw);
		}
	else
		{
		video_animation_stop(lw);
		}
}

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

	/* closing the fullscreen window from the window manager reaches here without layout_image_full_screen_stop,
	 * and the fullscreen ImageWindow is destroyed straight after, so an animation still aimed at it would draw
	 * into freed memory */
	layout_image_animate_update_image(lw);
	layout_image_video_animate_sync(lw);
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
	layout_image_video_animate_sync(lw);

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

static gboolean layout_image_full_screen_active(LayoutWindow *lw)
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

void layout_image_animate_stop(LayoutWindow *lw)
{
	if (!lw) return;

	if (lw->animation)
		{
		lw->animation->valid = FALSE;
		if (lw->animation->cancellable)
			{
			g_cancellable_cancel(lw->animation->cancellable);
			}
		lw->animation = nullptr;
		}
	video_animation_stop(lw);
}

static gboolean layout_image_animate_check(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	const gboolean image_animation = lw->image->image_fd && lw->image->image_fd->extension &&
	                                 (g_ascii_strcasecmp(lw->image->image_fd->extension, ".GIF") == 0 ||
	                                  g_ascii_strcasecmp(lw->image->image_fd->extension, ".WEBP") == 0);
	/* a video preview holds an ffmpeg open for as long as it runs, which is too much to spend on a file that is
	 * merely selected in the list, so it is confined to fullscreen */
	const gboolean video_preview = lw->full_screen && lw->image->image_fd &&
	                              lw->image->image_fd->format_class == FORMAT_CLASS_VIDEO;

	if (!lw->options.animate || (!image_animation && !video_preview))
		{
		layout_image_animate_stop(lw);
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
	if (lw->options.animate && lw->video_animation)
		{
		if (lw->full_screen && lw->image != lw->full_screen->imd)
			lw->video_animation->iw = lw->full_screen->imd;
		else
			lw->video_animation->iw = lw->image;
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

	layout_image_animate_stop(lw);

	if (lw->image->image_fd->format_class == FORMAT_CLASS_VIDEO)
		{
		return video_animation_new_file(lw);
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

static const gchar *layout_image_get_path(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	return image_get_path(lw->image);
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

	menu_item_add_check(menu, _("_Animation"), lw->options.animate, G_CALLBACK(li_pop_menu_animate_cb), lw);

	return menu;
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

FileData *layout_image_get_fd(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	return image_get_fd(lw->image);
}

static gint layout_image_get_index(LayoutWindow *lw)
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

	/* Activate — inlined from layout_image_activate */
	image_set_update_func(imd, layout_image_update_cb, lw);
	layout_image_set_buttons(lw);
	image_set_drag_func(imd, layout_image_drag_cb, lw);

	imd->top_window = lw->window;
	g_free(imd->title);
	imd->title = nullptr;
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
