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

#ifndef OPTIONS_H
#define OPTIONS_H

#include <cairo.h>
#include <gdk/gdk.h>
#include <glib.h>

#include "image.h"
#include "typedefs.h"

enum TextPosition : gint;

struct SecureSaveInfo;

#define COLOR_PROFILE_INPUTS 4

/**
 * @enum DnDAction
 * drag and drop default action
 */
enum DnDAction {
	DND_ACTION_ASK,
	DND_ACTION_COPY,
	DND_ACTION_MOVE
};

enum ZoomStyle {
	ZOOM_GEOMETRIC	= 0,
	ZOOM_ARITHMETIC	= 1
};

struct ConfOptions
{
	/* ui */
	gboolean progressive_key_scrolling;
	guint keyboard_scroll_step;
	gboolean place_dialogs_under_mouse;
	gboolean mousewheel_scrolls;
	gboolean image_l_click_video;
	gchar *image_l_click_video_editor;
	gboolean show_icon_names;
	gboolean overunderexposed;

	gboolean circular_selection_lists;

	gboolean lazy_image_sync;
	gboolean update_on_time_change;

	guint duplicates_similarity_threshold;
	guint duplicates_match;
	gboolean duplicates_thumbnails;
	guint duplicates_select_type;
	gboolean rot_invariant_sim;
	gboolean sort_totals;

	gint open_recent_list_maxsize;
	gint dnd_icon_size;
	DnDAction dnd_default_action;

	gboolean hide_window_decorations;

	gint log_window_lines;

	gboolean hide_window_in_fullscreen;

	/* file ops */
	struct {
		gboolean confirm_delete;
		gboolean enable_delete_key;
		gboolean safe_delete_enable;
		gboolean use_system_trash;
		gchar *safe_delete_path;
		gint safe_delete_folder_maxsize;
		gboolean no_trash;
	} file_ops;

	/* image */
	struct {
		gboolean exif_rotate_enable;
		ScrollReset scroll_reset_method;

		gint tile_cache_max;	/**< in megabytes */
		gint image_cache_max;   /**< in megabytes */
		gboolean enable_read_ahead;

		ZoomMode zoom_mode;
		gboolean zoom_2pass;
		gboolean zoom_to_fit_allow_expand;
		guint zoom_quality;
		gint zoom_increment;	/**< 100 is 1.0, 5 is 0.05, 200 is 2.0, etc. */
		ZoomStyle zoom_style;

		gboolean use_custom_border_color_in_fullscreen;
		gboolean use_custom_border_color;
		GdkRGBA border_color;
		GdkRGBA alpha_color_1;
		GdkRGBA alpha_color_2;

		gint tile_size;
	} image;

	/* thumbnails */
	struct {
		gint max_width;
		gint max_height;
		gboolean enable_caching;
		gboolean cache_into_dirs;
		gboolean use_xvpics;
		gboolean spec_standard;
		guint quality;
		gboolean use_exif;
		gboolean use_color_management;
		gboolean use_ft_metadata;
		gint collection_preview;
	} thumbnails;

	/* file filtering */
	struct {
		gboolean show_hidden_files;
		gboolean show_parent_directory;
		gboolean show_dot_directory;
		gboolean disable_file_extension_checks;
		gboolean disable;
	} file_filter;

	/* collections */
	struct {
		gboolean rectangular_selection;
	} collections;

	/* shell */
	struct {
		gchar *path;
		gchar *options;
	} shell;

	/* file sorting */
	struct {
		gboolean case_sensitive; /**< file sorting method (case) */
	} file_sort;

	/* fullscreen */
	struct {
		gint screen;
		gboolean clean_flip;
		gboolean above;
	} fullscreen;

	/* image overlay */
	struct {
		gchar *template_string;
		gint x;
		gint y;
		guint16 text_red;
		guint16 text_green;
		guint16 text_blue;
		guint16 text_alpha;
		guint16 background_red;
		guint16 background_green;
		guint16 background_blue;
		guint16 background_alpha;
		gchar *font;
	} image_overlay;

	/* properties dialog */
	struct {
		gchar *tabs_order;
	} properties;

	/* color profiles */
	struct {
		gboolean enabled;
		gint input_type;
		gchar *input_file[COLOR_PROFILE_INPUTS];
		gchar *input_name[COLOR_PROFILE_INPUTS];
		gchar *screen_file;
		gboolean use_image;
		gboolean use_x11_screen_profile;
		gint render_intent;
	} color_profile;

	/* External preview extraction */
	struct {
		gboolean enable;
		gchar *select; /**< path to executable */
		gchar *extract; /**< path to executable */
	} external_preview;

	/**
	 * @struct cp_mv_rn
	 * copy move rename
	 */
	struct {
		gint auto_start;
		gchar *auto_end;
		gint auto_padding;
		gint formatted_start;
	} cp_mv_rn;

	/* log window */
	struct {
		gboolean paused;
		gboolean line_wrap;
		gboolean timer_data;
		gchar *action; /** Used with F1 key */
	} log_window;

	/* Printer */
	struct {
		gchar *image_font;
		gchar *page_font;
		gboolean show_image_text;
		gboolean show_page_text;
		gchar *page_text;
		TextPosition image_text_position;
		TextPosition page_text_position;
		gchar *template_string;
	} printer;

	/* Threads */
	struct {
		gint duplicates;
	} threads;

	/* Alternate similarity algorithm */
	struct {
		gboolean enabled;
		gboolean grayscale; /**< convert fingerprint to greyscale */
	} alternate_similarity_algorithm;

	gboolean class_filter[FILE_FORMAT_CLASSES]; /**< class file filter */

	GList *disabled_plugins;
};

struct CommandLine
{
	int argc;
	gchar **argv;
	gboolean startup_full_screen;
	gboolean log_window_show;
	gchar *path;
	gchar *file;
	GList *cmd_list;
	gchar *geometry;
	gchar *regexp;
	gchar *log_file;
	SecureSaveInfo *ssi;
};

extern ConfOptions *options;
extern CommandLine *command_line;

ConfOptions *init_options(ConfOptions *options);
void setup_default_options(ConfOptions *options);
void save_options(ConfOptions *options);
gboolean load_options(ConfOptions *options);


enum SortActionType {
	BAR_SORT_COPY = 0,
	BAR_SORT_MOVE,
	BAR_SORT_FILTER,
	BAR_SORT_ACTION_COUNT
};

enum SortSelectionType {
	BAR_SORT_SELECTION_IMAGE = 0,
	BAR_SORT_SELECTION_SELECTED,
	BAR_SORT_SELECTION_COUNT
};

struct LayoutOptions
{
	struct SortParams
	{
		SortType method;
		gboolean ascend;
		gboolean case_sensitive;
	};
	SortParams dir_view_list_sort;
	SortParams file_view_list_sort;

	gboolean show_file_filter;

	struct {
		GdkRectangle rect;
		gboolean maximized;
		gint hdivider_pos;
		gint vdivider_pos;
	} main_window;

	struct {
		guint state;
	} image_overlay;

	GdkRectangle log_window;

	struct {
		GdkRectangle rect;
		gint page_number;
	} preferences_window;

	GdkRectangle search_window;

	GdkRectangle dupe_window;

	gboolean animate;

	SortActionType action;
	SortSelectionType selection;
	gchar *filter_key;
};

void copy_layout_options(LayoutOptions *dest, const LayoutOptions *src);
LayoutOptions *init_layout_options(LayoutOptions *options);

#endif /* OPTIONS_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
