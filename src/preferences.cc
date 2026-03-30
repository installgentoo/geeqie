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

#include "preferences.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <config.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib-object.h>

#if HAVE_SPELL
#include <gspell/gspell.h>
#endif

#if HAVE_LCMS
#if HAVE_LCMS2
#include <lcms2.h>
#else
#include <lcms.h>
#endif
#endif

#include <pango/pango.h>

#include "cache.h"
#include "color-man.h"
#include "compat.h"
#include "debug.h"
#include "editors.h"
#include "filedata.h"
#include "filefilter.h"
#include "fullscreen.h"
#include "image-overlay.h"
#include "image.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "main.h"
#include "misc.h"
#include "options.h"
#include "osd.h"
#include "pixbuf-util.h"
#include "rcfile.h"
#include "trash.h"
#include "typedefs.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"
#include "ui-utildlg.h"
#include "utilops.h"
#include "window.h"

namespace
{

constexpr gint PRE_FORMATTED_COLUMNS = 5;

} // namespace

struct ZoneDetect;

enum {
	EDITOR_NAME_MAX_LENGTH = 32,
	EDITOR_COMMAND_MAX_LENGTH = 1024
};

static void image_overlay_set_text_colors();

struct ThumbSize
{
	gint w;
	gint h;
};

static ThumbSize thumb_size_list[] =
{
	{ 24, 24 },
	{ 32, 32 },
	{ 48, 48 },
	{ 64, 64 },
	{ 96, 72 },
	{ 96, 96 },
	{ 128, 96 },
	{ 128, 128 },
	{ 160, 120 },
	{ 160, 160 },
	{ 192, 144 },
	{ 192, 192 },
	{ 256, 192 },
	{ 256, 256 }
};

enum {
	FE_ENABLE,
	FE_EXTENSION,
	FE_DESCRIPTION,
	FE_CLASS
};

enum {
	AE_ACTION,
	AE_KEY,
	AE_TOOLTIP,
	AE_ACCEL,
	AE_ICON
};

enum {
	FILETYPES_COLUMN_ENABLED = 0,
	FILETYPES_COLUMN_FILTER,
	FILETYPES_COLUMN_DESCRIPTION,
	FILETYPES_COLUMN_CLASS,
	FILETYPES_COLUMN_COUNT
};

const gchar *format_class_list[] = {
	N_("Unknown"),
	N_("Image"),
	N_("RAW Image"),
	N_("Video"),
	N_("Document")
	};

/* config memory values */
static ConfOptions *c_options = nullptr;


#ifdef DEBUG
static gint debug_c;
#endif

static GtkWidget *configwindow = nullptr;
static GtkListStore *filter_store = nullptr;
static GtkTreeStore *accel_store = nullptr;

static GtkWidget *safe_delete_path_entry;

static GtkWidget *color_profile_input_file_entry[COLOR_PROFILE_INPUTS];
static GtkWidget *color_profile_input_name_entry[COLOR_PROFILE_INPUTS];
static GtkWidget *color_profile_screen_file_entry;
static GtkWidget *external_preview_select_entry;
static GtkWidget *external_preview_extract_entry;

static GtkWidget *log_window_f1_entry;

enum {
	CONFIG_WINDOW_DEF_WIDTH =		700,
	CONFIG_WINDOW_DEF_HEIGHT =	600
};

/*
 *-----------------------------------------------------------------------------
 * option widget callbacks (private)
 *-----------------------------------------------------------------------------
 */

static void zoom_increment_cb(GtkWidget *spin, gpointer)
{
	c_options->image.zoom_increment = static_cast<gint>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)) * 100.0 + 0.01);
}

/*
 *-----------------------------------------------------------------------------
 * sync program to config window routine (private)
 *-----------------------------------------------------------------------------
 */

/**
 * @brief Reusable helper functions
 */
void config_entry_to_option(GtkWidget *entry, gchar **option, gchar *(*func)(const gchar *))
{
	const gchar *buf;

	g_free(*option);
	*option = nullptr;
	buf = gq_gtk_entry_get_text(GTK_ENTRY(entry));
	if (buf && buf[0] != '\0')
		{
		if (func)
			*option = func(buf);
		else
			*option = g_strdup(buf);
		}
}


static gboolean accel_apply_cb(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
	gchar *accel_path;
	gchar *accel;

	gtk_tree_model_get(model, iter, AE_ACCEL, &accel_path, AE_KEY, &accel, -1);

	if (accel_path && accel_path[0])
		{
		GtkAccelKey key;
		gtk_accelerator_parse(accel, &key.accel_key, &key.accel_mods);
		gtk_accel_map_change_entry(accel_path, key.accel_key, key.accel_mods, TRUE);
		}

	g_free(accel_path);
	g_free(accel);

	return FALSE;
}


static void config_window_apply()
{
	gboolean refresh = FALSE;
#if HAVE_LCMS2
	int i = 0;
#endif

	config_entry_to_option(safe_delete_path_entry, &options->file_ops.safe_delete_path, remove_trailing_slash);

	if (options->file_filter.show_hidden_files != c_options->file_filter.show_hidden_files) refresh = TRUE;
	if (options->file_sort.case_sensitive != c_options->file_sort.case_sensitive) refresh = TRUE;
	if (options->file_filter.disable_file_extension_checks != c_options->file_filter.disable_file_extension_checks) refresh = TRUE;
	if (options->file_filter.disable != c_options->file_filter.disable) refresh = TRUE;

	options->file_ops.confirm_delete = c_options->file_ops.confirm_delete;
	options->file_ops.enable_delete_key = c_options->file_ops.enable_delete_key;
	options->file_ops.use_system_trash = c_options->file_ops.use_system_trash;
	options->file_ops.no_trash = c_options->file_ops.no_trash;
	options->file_ops.safe_delete_folder_maxsize = c_options->file_ops.safe_delete_folder_maxsize;
	options->hide_window_decorations = c_options->hide_window_decorations;
	options->image.scroll_reset_method = c_options->image.scroll_reset_method;
	options->image.zoom_2pass = c_options->image.zoom_2pass;
	options->image.tile_size = c_options->image.tile_size;
	options->progressive_key_scrolling = c_options->progressive_key_scrolling;
	options->keyboard_scroll_step = c_options->keyboard_scroll_step;

	if (options->thumbnails.max_width != c_options->thumbnails.max_width
	    || options->thumbnails.max_height != c_options->thumbnails.max_height
	    || options->thumbnails.quality != c_options->thumbnails.quality)
		{
		thumb_format_changed = TRUE;
		refresh = TRUE;
		options->thumbnails.max_width = c_options->thumbnails.max_width;
		options->thumbnails.max_height = c_options->thumbnails.max_height;
		options->thumbnails.quality = c_options->thumbnails.quality;
		}
	options->thumbnails.enable_caching = c_options->thumbnails.enable_caching;
	options->thumbnails.use_exif = c_options->thumbnails.use_exif;
	options->thumbnails.use_color_management = c_options->thumbnails.use_color_management;
	options->thumbnails.use_ft_metadata = c_options->thumbnails.use_ft_metadata;
	options->thumbnails.spec_standard = c_options->thumbnails.spec_standard;
	options->file_filter.show_hidden_files = c_options->file_filter.show_hidden_files;
	options->file_filter.disable_file_extension_checks = c_options->file_filter.disable_file_extension_checks;

	options->file_sort.case_sensitive = c_options->file_sort.case_sensitive;
	options->file_filter.disable = c_options->file_filter.disable;

	options->mousewheel_scrolls = c_options->mousewheel_scrolls;
	options->image_l_click_video = c_options->image_l_click_video;
	options->image_l_click_video_editor = c_options->image_l_click_video_editor;

	options->image.tile_cache_max = c_options->image.tile_cache_max;
	options->image.image_cache_max = c_options->image.image_cache_max;

	options->image.zoom_quality = c_options->image.zoom_quality;

	options->image.zoom_increment = c_options->image.zoom_increment;

	options->image.zoom_style = c_options->image.zoom_style;

	options->image.enable_read_ahead = c_options->image.enable_read_ahead;


	if (options->image.use_custom_border_color != c_options->image.use_custom_border_color
	    || options->image.use_custom_border_color_in_fullscreen != c_options->image.use_custom_border_color_in_fullscreen
	    || !gdk_rgba_equal(&options->image.border_color, &c_options->image.border_color))
		{
		options->image.use_custom_border_color_in_fullscreen = c_options->image.use_custom_border_color_in_fullscreen;
		options->image.use_custom_border_color = c_options->image.use_custom_border_color;
		options->image.border_color = c_options->image.border_color;
		}

	options->image.alpha_color_1 = c_options->image.alpha_color_1;
	options->image.alpha_color_2 = c_options->image.alpha_color_2;

	options->fullscreen.screen = c_options->fullscreen.screen;
	options->fullscreen.clean_flip = c_options->fullscreen.clean_flip;
	options->fullscreen.above = c_options->fullscreen.above;
	if (c_options->image_overlay.template_string)
		set_image_overlay_template_string(&options->image_overlay.template_string,
						  c_options->image_overlay.template_string);
	if (c_options->image_overlay.font)
		set_image_overlay_font_string(&options->image_overlay.font,
						  c_options->image_overlay.font);
	options->image_overlay.text_red = c_options->image_overlay.text_red;
	options->image_overlay.text_green = c_options->image_overlay.text_green;
	options->image_overlay.text_blue = c_options->image_overlay.text_blue;
	options->image_overlay.text_alpha = c_options->image_overlay.text_alpha;
	options->image_overlay.background_red = c_options->image_overlay.background_red;
	options->image_overlay.background_green = c_options->image_overlay.background_green;
	options->image_overlay.background_blue = c_options->image_overlay.background_blue;
	options->image_overlay.background_alpha = c_options->image_overlay.background_alpha;
	options->update_on_time_change = c_options->update_on_time_change;

	options->duplicates_similarity_threshold = c_options->duplicates_similarity_threshold;
	options->rot_invariant_sim = c_options->rot_invariant_sim;

	options->circular_selection_lists = c_options->circular_selection_lists;

	options->open_recent_list_maxsize = c_options->open_recent_list_maxsize;
	options->dnd_icon_size = c_options->dnd_icon_size;
	options->dnd_default_action = c_options->dnd_default_action;

	options->hide_window_in_fullscreen = c_options->hide_window_in_fullscreen;

	options->external_preview.enable = c_options->external_preview.enable;
	config_entry_to_option(external_preview_select_entry, &options->external_preview.select, nullptr);
	config_entry_to_option(external_preview_extract_entry, &options->external_preview.extract, nullptr);

	options->threads.duplicates = c_options->threads.duplicates > 0 ? c_options->threads.duplicates : -1;

	options->alternate_similarity_algorithm.enabled = c_options->alternate_similarity_algorithm.enabled;
	options->alternate_similarity_algorithm.grayscale = c_options->alternate_similarity_algorithm.grayscale;

#ifdef DEBUG
	set_debug_level(debug_c);
	config_entry_to_option(log_window_f1_entry, &options->log_window.action, nullptr);
#endif

#if HAVE_LCMS
	for (i = 0; i < COLOR_PROFILE_INPUTS; i++)
		{
		config_entry_to_option(color_profile_input_name_entry[i], &options->color_profile.input_name[i], nullptr);
		config_entry_to_option(color_profile_input_file_entry[i], &options->color_profile.input_file[i], nullptr);
		}
	config_entry_to_option(color_profile_screen_file_entry, &options->color_profile.screen_file, nullptr);
	options->color_profile.use_x11_screen_profile = c_options->color_profile.use_x11_screen_profile;
	if (options->color_profile.render_intent != c_options->color_profile.render_intent)
		{
		options->color_profile.render_intent = c_options->color_profile.render_intent;
		color_man_update();
		}
#endif

	image_options_sync();

	if (refresh)
		{
		filter_rebuild();
		layout_refresh(nullptr);
		}

	if (accel_store) gtk_tree_model_foreach(GTK_TREE_MODEL(accel_store), accel_apply_cb, nullptr);
}

/*
 *-----------------------------------------------------------------------------
 * config window main button callbacks (private)
 *-----------------------------------------------------------------------------
 */

static void config_window_close_cb(GtkWidget *, gpointer)
{
	gq_gtk_widget_destroy(configwindow);
	configwindow = nullptr;
	filter_store = nullptr;
}

static gboolean config_window_delete(GtkWidget *, GdkEventAny *, gpointer)
{
	config_window_close_cb(nullptr, nullptr);
	return TRUE;
}

static void config_window_ok_cb(GtkWidget *widget, gpointer data)
{
	auto notebook = static_cast<GtkNotebook *>(data);
	GdkWindow *window;

	window = gtk_widget_get_window(widget);

	main_lw->options.preferences_window.rect = window_get_root_origin_geometry(window);
	main_lw->options.preferences_window.page_number = gtk_notebook_get_current_page(notebook);

	config_window_apply();
	layout_util_sync(main_lw);
	save_options(options);
	config_window_close_cb(nullptr, nullptr);
}

/*
 *-----------------------------------------------------------------------------
 * config window setup (private)
 *-----------------------------------------------------------------------------
 */

static void quality_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)))
		{
		case 0:
		default:
			*option = GDK_INTERP_NEAREST;
			break;
		case 1:
			*option = GDK_INTERP_TILES;
			break;
		case 2:
			*option = GDK_INTERP_BILINEAR;
			break;
		}
}

static void dnd_default_action_selection_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)))
		{
		case 0:
		default:
			*option = DND_ACTION_ASK;
			break;
		case 1:
			*option = DND_ACTION_COPY;
			break;
		case 2:
			*option = DND_ACTION_MOVE;
			break;
		}
}

static void add_quality_menu(GtkWidget *table, gint column, gint row, const gchar *text,
			     guint option, guint *option_c)
{
	GtkWidget *combo;
	gint current = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, GTK_ALIGN_START);

	combo = gtk_combo_box_text_new();

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Nearest (worst, but fastest)"));
	if (option == GDK_INTERP_NEAREST) current = 0;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Tiles"));
	if (option == GDK_INTERP_TILES) current = 1;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Bilinear (best, but slowest)"));
	if (option == GDK_INTERP_BILINEAR) current = 2;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(quality_menu_cb), option_c);

	gq_gtk_grid_attach(GTK_GRID(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void add_dnd_default_action_selection_menu(GtkWidget *table, gint column, gint row, const gchar *text, DnDAction option, DnDAction *option_c)
{
	GtkWidget *combo;
	gint current = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, GTK_ALIGN_START);

	combo = gtk_combo_box_text_new();

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Ask"));
	if (option == DND_ACTION_ASK) current = 0;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Copy"));
	if (option == DND_ACTION_COPY) current = 1;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Move"));
	if (option == DND_ACTION_MOVE) current = 2;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(dnd_default_action_selection_menu_cb), option_c);

	gq_gtk_grid_attach(GTK_GRID(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void zoom_style_selection_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)))
		{
		case 0:
			*option = ZOOM_GEOMETRIC;
			break;
		case 1:
			*option = ZOOM_ARITHMETIC;
			break;
		default:
			*option = ZOOM_GEOMETRIC;
		}
}

static void add_zoom_style_selection_menu(GtkWidget *table, gint column, gint row, const gchar *text, ZoomStyle option, ZoomStyle *option_c)
{
	GtkWidget *combo;
	gint current = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, GTK_ALIGN_START);

	combo = gtk_combo_box_text_new();

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Geometric"));
	if (option == ZOOM_GEOMETRIC) current = 0;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Arithmetic"));
	if (option == ZOOM_ARITHMETIC) current = 1;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed", G_CALLBACK(zoom_style_selection_menu_cb), option_c);

	gq_gtk_grid_attach(GTK_GRID(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void thumb_size_menu_cb(GtkWidget *combo, gpointer)
{
	gint n;

	n = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	if (n < 0) return;

	if (static_cast<guint>(n) < sizeof(thumb_size_list) / sizeof(ThumbSize))
		{
		c_options->thumbnails.max_width = thumb_size_list[n].w;
		c_options->thumbnails.max_height = thumb_size_list[n].h;
		}
	else
		{
		c_options->thumbnails.max_width = options->thumbnails.max_width;
		c_options->thumbnails.max_height = options->thumbnails.max_height;
		}
}

static void add_thumb_size_menu(GtkWidget *table, gint column, gint row, gchar *text)
{
	GtkWidget *combo;
	gint current;
	gint i;

	c_options->thumbnails.max_width = options->thumbnails.max_width;
	c_options->thumbnails.max_height = options->thumbnails.max_height;

	pref_table_label(table, column, row, text, GTK_ALIGN_START);

	combo = gtk_combo_box_text_new();

	current = -1;
	for (i = 0; static_cast<guint>(i) < sizeof(thumb_size_list) / sizeof(ThumbSize); i++)
		{
		gint w;
		gint h;
		gchar *buf;

		w = thumb_size_list[i].w;
		h = thumb_size_list[i].h;

		buf = g_strdup_printf("%d x %d", w, h);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), buf);
		g_free(buf);

		if (w == options->thumbnails.max_width && h == options->thumbnails.max_height) current = i;
		}

	if (current == -1)
		{
		gchar *buf;

		buf = g_strdup_printf("%s %d x %d", _("Custom"), options->thumbnails.max_width, options->thumbnails.max_height);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), buf);
		g_free(buf);

		current = i;
		}

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);
	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(thumb_size_menu_cb), NULL);

	gq_gtk_grid_attach(GTK_GRID(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void video_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gchar **>(data);

	auto ed = static_cast<EditorDescription *>(g_list_nth_data(editor_list_get(), gtk_combo_box_get_active(GTK_COMBO_BOX(combo))));
	*option = ed->key;
}

static void video_menu_populate(gpointer data, gpointer user_data)
{
	auto combo = static_cast<GtkWidget *>(user_data);
	auto ed = static_cast<EditorDescription *>(data);

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), ed->name);
}

static void add_video_menu(GtkWidget *table, gint column, gint row, const gchar *text,
			     gchar *option, gchar **option_c)
{
	GtkWidget *combo;
	gint current;
/* use lists since they are sorted */
	GList *eds = editor_list_get();

	*option_c = option;

	pref_table_label(table, column, row, text, GTK_ALIGN_START);

	combo = gtk_combo_box_text_new();
	g_list_foreach(eds,video_menu_populate,combo);
	current = option ? g_list_index(eds,g_hash_table_lookup(editors,option)): -1;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(video_menu_cb), option_c);

	gq_gtk_grid_attach(GTK_GRID(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}

static void filter_store_populate()
{
	GList *work;

	if (!filter_store) return;

	gtk_list_store_clear(filter_store);

	work = filter_get_list();
	while (work)
		{
		FilterEntry *fe;
		GtkTreeIter iter;

		fe = static_cast<FilterEntry *>(work->data);
		work = work->next;

		gtk_list_store_append(filter_store, &iter);
		gtk_list_store_set(filter_store, &iter, 0, fe, -1);
		}
}

static void filter_store_ext_edit_cb(GtkCellRendererText *, gchar *path_str, gchar *new_text, gpointer data)
{
	auto model = static_cast<GtkWidget *>(data);
	auto fe = static_cast<FilterEntry *>(data);
	GtkTreePath *tpath;
	GtkTreeIter iter;

	if (!new_text || strlen(new_text) < 1) return;

	tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &fe, -1);

	g_free(fe->extensions);
	fe->extensions = g_strdup(new_text);

	gtk_tree_path_free(tpath);
	filter_rebuild();
}

static void filter_store_class_edit_cb(GtkCellRendererText *, gchar *path_str, gchar *new_text, gpointer data)
{
	auto model = static_cast<GtkWidget *>(data);
	auto fe = static_cast<FilterEntry *>(data);
	GtkTreePath *tpath;
	GtkTreeIter iter;
	gint i;

	if (!new_text || !new_text[0]) return;

	tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &fe, -1);

	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		if (strcmp(new_text, _(format_class_list[i])) == 0)
			{
			fe->file_class = static_cast<FileFormatClass>(i);
			break;
			}
		}

	gtk_tree_path_free(tpath);
	filter_rebuild();
}

static void filter_store_desc_edit_cb(GtkCellRendererText *, gchar *path_str, gchar *new_text, gpointer data)
{
	auto model = static_cast<GtkWidget *>(data);
	FilterEntry *fe;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	if (!new_text || !new_text[0]) return;

	tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &fe, -1);

	g_free(fe->description);
	fe->description = g_strdup(new_text);

	gtk_tree_path_free(tpath);
}

static void filter_store_enable_cb(GtkCellRendererToggle *, gchar *path_str, gpointer data)
{
	auto model = static_cast<GtkWidget *>(data);
	FilterEntry *fe;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	tpath = gtk_tree_path_new_from_string(path_str);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &fe, -1);

	fe->enabled = !fe->enabled;

	gtk_tree_path_free(tpath);
	filter_rebuild();
}

static void filter_set_func(GtkTreeViewColumn *, GtkCellRenderer *cell,
			    GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	FilterEntry *fe;

	gtk_tree_model_get(tree_model, iter, 0, &fe, -1);

	switch (GPOINTER_TO_INT(data))
		{
		case FE_ENABLE:
			g_object_set(GTK_CELL_RENDERER(cell),
				     "active", fe->enabled, NULL);
			break;
		case FE_EXTENSION:
			g_object_set(GTK_CELL_RENDERER(cell),
				     "text", fe->extensions, NULL);
			break;
		case FE_DESCRIPTION:
			g_object_set(GTK_CELL_RENDERER(cell),
				     "text", fe->description, NULL);
			break;
		case FE_CLASS:
			g_object_set(GTK_CELL_RENDERER(cell),
				     "text", _(format_class_list[fe->file_class]), NULL);
			break;
		default:
			break;
		}
}

static gboolean filter_add_scroll(gpointer data)
{
	GtkTreePath *path;
	GList *list_cells;
	GtkCellRenderer *cell;
	GtkTreeViewColumn *column;
	gint rows;
	GtkTreeIter iter;
	GtkTreeModel *store;
	gboolean valid;
	FilterEntry *filter;

	rows = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(filter_store), nullptr);
	path = gtk_tree_path_new_from_indices(rows-1, -1);

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(data), 0);

	list_cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
	cell = static_cast<GtkCellRenderer *>(g_list_last(list_cells)->data);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(data));
	valid = gtk_tree_model_get_iter_first(store, &iter);

	while (valid)
		{
		gtk_tree_model_get(store, &iter, 0, &filter, -1);

		if (g_strcmp0(filter->extensions, ".new") == 0)
			{
			path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
			break;
			}

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		}

	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(data),
								path, column, FALSE, 0.0, 0.0 );
	gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(data),
								path, column, cell, TRUE);

	gtk_tree_path_free(path);
	g_list_free(list_cells);

	return(G_SOURCE_REMOVE);
}

static void filter_add_cb(GtkWidget *, gpointer data)
{
	filter_add_unique("description", ".new", FORMAT_CLASS_IMAGE, TRUE, FALSE, TRUE);
	filter_store_populate();

	g_idle_add(static_cast<GSourceFunc>(filter_add_scroll), data);
}

static void filter_remove_cb(GtkWidget *, gpointer data)
{
	auto filter_view = static_cast<GtkWidget *>(data);
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	FilterEntry *fe;

	if (!filter_store) return;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(filter_view));
	if (!gtk_tree_selection_get_selected(selection, nullptr, &iter)) return;
	gtk_tree_model_get(GTK_TREE_MODEL(filter_store), &iter, 0, &fe, -1);
	if (!fe) return;

	filter_remove_entry(fe);
	filter_rebuild();
	filter_store_populate();
}

static gboolean filter_default_ok_scroll(gpointer data)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeViewColumn *column;

	gtk_tree_model_get_iter_first(GTK_TREE_MODEL(filter_store), &iter);
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(filter_store), &iter);
	column = gtk_tree_view_get_column(GTK_TREE_VIEW(data),0);

	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(data),
				     path, column,
				     FALSE, 0.0, 0.0);

	gtk_tree_path_free(path);

	return G_SOURCE_REMOVE;
}

static void filter_default_ok_cb(GenericDialog *gd, gpointer)
{
	filter_reset();
	filter_add_defaults();
	filter_rebuild();
	filter_store_populate();

	g_idle_add(filter_default_ok_scroll, gd->data);
}

static void dummy_cancel_cb(GenericDialog *, gpointer)
{
	/* no op, only so cancel button appears */
}

static void filter_default_cb(GtkWidget *widget, gpointer data)
{
	GenericDialog *gd;

	gd = generic_dialog_new(_("Reset filters"),
				"reset_filter", widget, TRUE,
				dummy_cancel_cb, data);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_QUESTION, _("Reset filters"),
				   _("This will reset the file filters to the defaults.\nContinue?"), TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", filter_default_ok_cb, TRUE);
	gtk_widget_show(gd->dialog);
}

static void filter_disable_cb(GtkWidget *widget, gpointer data)
{
	auto frame = static_cast<GtkWidget *>(data);

	gtk_widget_set_sensitive(frame,
				 !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
}

static void safe_delete_view_cb(GtkWidget *, gpointer)
{
	layout_set_path(nullptr, gq_gtk_entry_get_text(GTK_ENTRY(safe_delete_path_entry)));
}

static void safe_delete_clear_ok_cb(GenericDialog *, gpointer)
{
	file_util_trash_clear();
}

static void safe_delete_clear_cb(GtkWidget *widget, gpointer)
{
	GenericDialog *gd;
	GtkWidget *entry;
	gd = generic_dialog_new(_("Clear trash"),
				"clear_trash", widget, TRUE,
				dummy_cancel_cb, nullptr);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_QUESTION, _("Clear trash"),
				    _("This will remove the trash contents."), FALSE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", safe_delete_clear_ok_cb, TRUE);
	entry = gtk_entry_new();
	gtk_widget_set_can_focus(entry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
	if (options->file_ops.safe_delete_path) gq_gtk_entry_set_text(GTK_ENTRY(entry), options->file_ops.safe_delete_path);
	gq_gtk_box_pack_start(GTK_BOX(gd->vbox), entry, FALSE, FALSE, 0);
	gtk_widget_show(entry);
	gtk_widget_show(gd->dialog);
}

static void image_overlay_template_view_changed_cb(GtkWidget *, gpointer data)
{
	GtkWidget *pTextView;
	GtkTextBuffer *pTextBuffer;
	GtkTextIter iStart;
	GtkTextIter iEnd;

	pTextView = GTK_WIDGET(data);

	pTextBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(pTextView));
	gtk_text_buffer_get_start_iter(pTextBuffer, &iStart);
	gtk_text_buffer_get_end_iter(pTextBuffer, &iEnd);

	set_image_overlay_template_string(&c_options->image_overlay.template_string,
					  gtk_text_buffer_get_text(pTextBuffer, &iStart, &iEnd, TRUE));
}

static void image_overlay_default_template_ok_cb(GenericDialog *, gpointer data)
{
	auto text_view = static_cast<GtkTextView *>(data);
	GtkTextBuffer *buffer;

	set_default_image_overlay_template_string(&options->image_overlay.template_string);
	if (!configwindow) return;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
	gtk_text_buffer_set_text(buffer, options->image_overlay.template_string, -1);
}

static void image_overlay_default_template_cb(GtkWidget *widget, gpointer data)
{
	GenericDialog *gd;

	gd = generic_dialog_new(_("Reset image overlay template string"),
				"reset_image_overlay_template_string", widget, TRUE,
				dummy_cancel_cb, data);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_QUESTION, _("Reset image overlay template string"),
				   _("This will reset the image overlay template string to the default.\nContinue?"), TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", image_overlay_default_template_ok_cb, TRUE);
	gtk_widget_show(gd->dialog);
}

static void font_activated_cb(GtkFontChooser *widget, gchar *fontname, gpointer)
{
	g_free(c_options->image_overlay.font);
	c_options->image_overlay.font = g_strdup(fontname);
	g_free(fontname);

	gq_gtk_widget_destroy(GTK_WIDGET(widget));
}

static void font_response_cb(GtkDialog *dialog, gint response_id, gpointer)
{
	gchar *font;

	g_free(c_options->image_overlay.font);
	c_options->image_overlay.font = g_strdup(options->image_overlay.font);

	if (response_id == GTK_RESPONSE_OK)
		{
		font = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(dialog));
		g_free(c_options->image_overlay.font);
		c_options->image_overlay.font = g_strdup(font);
		g_free(font);
		}

	gq_gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void image_overlay_set_font_cb(GtkWidget *widget, gpointer)
{
	GtkWidget *dialog;

	dialog = gtk_font_chooser_dialog_new(_("Image Overlay Font"), GTK_WINDOW(gtk_widget_get_toplevel(widget)));
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_font_chooser_set_font(GTK_FONT_CHOOSER(dialog), options->image_overlay.font);

	g_signal_connect(dialog, "font-activated", G_CALLBACK(font_activated_cb), nullptr);
	g_signal_connect(dialog, "response", G_CALLBACK(font_response_cb), nullptr);

	gtk_widget_show(dialog);
}

static void text_color_activated_cb(GtkColorChooser *chooser, GdkRGBA *color, gpointer)
{
	c_options->image_overlay.text_red = color->red * 255;
	c_options->image_overlay.text_green = color->green * 255;
	c_options->image_overlay.text_blue = color->blue * 255;
	c_options->image_overlay.text_alpha = color->alpha * 255;

	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

static void text_color_response_cb(GtkDialog *dialog, gint response_id, gpointer)
{
	GdkRGBA color;

	c_options->image_overlay.text_red = options->image_overlay.text_red;
	c_options->image_overlay.text_green = options->image_overlay.text_green;
	c_options->image_overlay.text_blue = options->image_overlay.text_blue;
	c_options->image_overlay.text_alpha = options->image_overlay.text_alpha;

	if (response_id == GTK_RESPONSE_OK)
		{
		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &color);
		c_options->image_overlay.text_red = color.red * 255;
		c_options->image_overlay.text_green = color.green * 255;
		c_options->image_overlay.text_blue = color.blue * 255;
		c_options->image_overlay.text_alpha = color.alpha * 255;
		}

	gq_gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void image_overlay_set_text_color_cb(GtkWidget *widget, gpointer)
{
	GtkWidget *dialog;
	GdkRGBA color;

	dialog = gtk_color_chooser_dialog_new(_("Image Overlay Text Color"), GTK_WINDOW(gtk_widget_get_toplevel(widget)));
	color.red = options->image_overlay.text_red;
	color.green = options->image_overlay.text_green;
	color.blue = options->image_overlay.text_blue;
	color.alpha = options->image_overlay.text_alpha;
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog), &color);
	gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dialog), TRUE);

	g_signal_connect(dialog, "color-activated", G_CALLBACK(text_color_activated_cb), nullptr);
	g_signal_connect(dialog, "response", G_CALLBACK(text_color_response_cb), nullptr);

	gtk_widget_show(dialog);
}

static void bg_color_activated_cb(GtkColorChooser *chooser, GdkRGBA *color, gpointer)
{
	c_options->image_overlay.background_red = color->red * 255;
	c_options->image_overlay.background_green = color->green * 255;
	c_options->image_overlay.background_blue = color->blue * 255;
	c_options->image_overlay.background_alpha = color->alpha * 255;

	gq_gtk_widget_destroy(GTK_WIDGET(chooser));
}

static void bg_color_response_cb(GtkDialog *dialog, gint response_id, gpointer)
{
	GdkRGBA color;

	c_options->image_overlay.background_red = options->image_overlay.background_red;
	c_options->image_overlay.background_green = options->image_overlay.background_green;
	c_options->image_overlay.background_blue = options->image_overlay.background_blue;
	c_options->image_overlay.background_alpha = options->image_overlay.background_alpha;

	if (response_id == GTK_RESPONSE_OK)
		{
		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &color);
		c_options->image_overlay.background_red = color.red * 255;
		c_options->image_overlay.background_green = color.green * 255;
		c_options->image_overlay.background_blue = color.blue * 255;
		c_options->image_overlay.background_alpha = color.alpha * 255;
		}
	gq_gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void image_overlay_set_background_color_cb(GtkWidget *widget, gpointer)
{
	GtkWidget *dialog;
	GdkRGBA color;

	dialog = gtk_color_chooser_dialog_new(_("Image Overlay Background Color"), GTK_WINDOW(gtk_widget_get_toplevel(widget)));
	color.red = options->image_overlay.background_red;
	color.green = options->image_overlay.background_green;
	color.blue = options->image_overlay.background_blue;
	color.alpha = options->image_overlay.background_alpha;
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog), &color);
	gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dialog), TRUE);

	g_signal_connect(dialog, "color-activated", G_CALLBACK(bg_color_activated_cb), nullptr);
	g_signal_connect(dialog, "response", G_CALLBACK(bg_color_response_cb), nullptr);

	gtk_widget_show(dialog);
}

static void accel_store_populate()
{
	GList *groups;
	GList *actions;
	GtkAction *action;
	const gchar *accel_path;
	const gchar *icon_name;
	GtkAccelKey key;
	GtkTreeIter iter;

	if (!accel_store || !main_lw) return;

	gtk_tree_store_clear(accel_store);

	g_assert(main_lw && main_lw->ui_manager);
	groups = gq_gtk_ui_manager_get_action_groups(main_lw->ui_manager);
	while (groups)
		{
		actions = gq_gtk_action_group_list_actions(GTK_ACTION_GROUP(groups->data));
		while (actions)
			{
			action = GTK_ACTION(actions->data);
			accel_path = gq_gtk_action_get_accel_path(action);
			if (accel_path && gtk_accel_map_lookup_entry(accel_path, &key))
				{
				gchar *label;
				gchar *label2;
				gchar *tooltip;
				gchar *accel;
				g_object_get(action,
					     "tooltip", &tooltip,
					     "label", &label,
					     NULL);

				if (pango_parse_markup(label, -1, '_', nullptr, &label2, nullptr, nullptr) && label2)
					{
					g_free(label);
					label = label2;
					}

				accel = gtk_accelerator_name(key.accel_key, key.accel_mods);
				icon_name = gq_gtk_action_get_icon_name(action);

				if (tooltip)
					{
					gtk_tree_store_append(accel_store, &iter, nullptr);
					gtk_tree_store_set(accel_store, &iter,
					                   AE_ACTION, label,
					                   AE_KEY, accel,
					                   AE_TOOLTIP, tooltip,
					                   AE_ACCEL, accel_path,
					                   AE_ICON, icon_name,
					                   -1);
					}

				g_free(accel);
				g_free(label);
				g_free(tooltip);
				}
			actions = actions->next;
			}

		groups = groups->next;
		}
}

static void accel_store_cleared_cb(GtkCellRendererAccel *, gchar *, gpointer)
{

}

static gboolean accel_remove_key_cb(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer data)
{
	auto accel1 = static_cast<gchar *>(data);
	gchar *accel2;
	GtkAccelKey key1;
	GtkAccelKey key2;

	gtk_tree_model_get(model, iter, AE_KEY, &accel2, -1);

	gtk_accelerator_parse(accel1, &key1.accel_key, &key1.accel_mods);
	gtk_accelerator_parse(accel2, &key2.accel_key, &key2.accel_mods);

	if (key1.accel_key == key2.accel_key && key1.accel_mods == key2.accel_mods)
		{
		gtk_tree_store_set(accel_store, iter, AE_KEY, "",  -1);
		DEBUG_1("accelerator key '%s' is already used, removing.", accel1);
		}

	g_free(accel2);

	return FALSE;
}


static void accel_store_edited_cb(GtkCellRendererAccel *, gchar *path_string, guint accel_key, GdkModifierType accel_mods, guint, gpointer)
{
	GtkTreeModel *model = GTK_TREE_MODEL(accel_store);
	GtkTreeIter iter;
	gchar *acc;
	gchar *accel_path;
	GtkAccelKey old_key;
	GtkAccelKey key;
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);

	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, AE_ACCEL, &accel_path, -1);

	/* test if the accelerator can be stored without conflicts*/
	gtk_accel_map_lookup_entry(accel_path, &old_key);

	/* change the key and read it back (change may fail on keys hardcoded in gtk)*/
	gtk_accel_map_change_entry(accel_path, accel_key, accel_mods, TRUE);
	gtk_accel_map_lookup_entry(accel_path, &key);

	/* restore the original for now, the key will be really changed when the changes are confirmed */
	gtk_accel_map_change_entry(accel_path, old_key.accel_key, old_key.accel_mods, TRUE);

	acc = gtk_accelerator_name(key.accel_key, key.accel_mods);
	gtk_tree_model_foreach(GTK_TREE_MODEL(accel_store), accel_remove_key_cb, acc);

	gtk_tree_store_set(accel_store, &iter, AE_KEY, acc, -1);
	gtk_tree_path_free(path);
	g_free(acc);
}

static gboolean accel_default_scroll(gpointer data)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeViewColumn *column;

	gtk_tree_model_get_iter_first(GTK_TREE_MODEL(accel_store), &iter);
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(accel_store), &iter);
	column = gtk_tree_view_get_column(GTK_TREE_VIEW(data),0);

	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(data),
				     path, column,
				     FALSE, 0.0, 0.0);

	gtk_tree_path_free(path);

	return G_SOURCE_REMOVE;
}

static void accel_default_cb(GtkWidget *, gpointer data)
{
	accel_store_populate();

	g_idle_add(accel_default_scroll, data);
}

static void accel_clear_selection(GtkTreeModel *, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
	gtk_tree_store_set(accel_store, iter, AE_KEY, "", -1);
}

static void accel_reset_selection(GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
	GtkAccelKey key;
	gchar *accel_path;
	gchar *accel;

	gtk_tree_model_get(model, iter, AE_ACCEL, &accel_path, -1);
	gtk_accel_map_lookup_entry(accel_path, &key);
	accel = gtk_accelerator_name(key.accel_key, key.accel_mods);

	gtk_tree_model_foreach(GTK_TREE_MODEL(accel_store), accel_remove_key_cb, accel);

	gtk_tree_store_set(accel_store, iter, AE_KEY, accel, -1);
	g_free(accel_path);
	g_free(accel);
}

static void accel_clear_cb(GtkWidget *, gpointer data)
{
	GtkTreeSelection *selection;

	if (!accel_store) return;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
	gtk_tree_selection_selected_foreach(selection, &accel_clear_selection, nullptr);
}

static void accel_reset_cb(GtkWidget *, gpointer data)
{
	GtkTreeSelection *selection;

	if (!accel_store) return;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
	gtk_tree_selection_selected_foreach(selection, &accel_reset_selection, nullptr);
}



static GtkWidget *scrolled_notebook_page(GtkWidget *notebook, const gchar *title)
{
	GtkWidget *label;
	GtkWidget *vbox;
	GtkWidget *scrolled;
	GtkWidget *viewport;

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gtk_container_set_border_width(GTK_CONTAINER(scrolled), PREF_PAD_BORDER);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	label = gtk_label_new(title);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, label);
	gtk_widget_show(scrolled);

	viewport = gtk_viewport_new(nullptr, nullptr);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
	gq_gtk_container_add(GTK_WIDGET(scrolled), viewport);
	gtk_widget_show(viewport);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gq_gtk_container_add(GTK_WIDGET(viewport), vbox);
	gtk_widget_show(vbox);

	return vbox;
}

static void cache_standard_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->thumbnails.spec_standard =TRUE;
		}
}

static void cache_geeqie_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->thumbnails.spec_standard =FALSE;
		}
}

static void config_tab_general(GtkWidget *notebook)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *group;
	GtkWidget *group_frame;
	GtkWidget *subgroup;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *table;

	vbox = scrolled_notebook_page(notebook, _("General"));

	group = pref_group_new(vbox, FALSE, _("Thumbnails"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 2, 2, FALSE, FALSE);
	add_thumb_size_menu(table, 0, 0, _("Size:"));
	add_quality_menu(table, 0, 1, _("Quality:"), options->thumbnails.quality, &c_options->thumbnails.quality);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Custom size: "));
	pref_spin_new_int(hbox, _("Width:"), nullptr, 1, 512, 1, options->thumbnails.max_width, &c_options->thumbnails.max_width);
	pref_spin_new_int(hbox, _("Height:"), nullptr, 1, 512, 1, options->thumbnails.max_height, &c_options->thumbnails.max_height);

	ct_button = pref_checkbox_new_int(group, _("Cache thumbnails and sim. files"),
					  options->thumbnails.enable_caching, &c_options->thumbnails.enable_caching);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	pref_checkbox_link_sensitivity(ct_button, subgroup);

	c_options->thumbnails.spec_standard = options->thumbnails.spec_standard;
	group_frame = pref_frame_new(subgroup, TRUE, _("Use Geeqie thumbnail style and cache"),
										GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	button = pref_radiobutton_new(group_frame, nullptr,  get_thumbnails_cache_dir(),
							!options->thumbnails.spec_standard,
							G_CALLBACK(cache_geeqie_cb), nullptr);

	group_frame = pref_frame_new(subgroup, TRUE,
							_("Use standard thumbnail style and cache, shared with other applications"),
							GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	pref_radiobutton_new(group_frame, button, get_thumbnails_standard_cache_dir(),
							options->thumbnails.spec_standard,
							G_CALLBACK(cache_standard_cb), nullptr);

	pref_checkbox_new_int(group, _("Use EXIF thumbnails when available (EXIF thumbnails may be outdated)"),
			      options->thumbnails.use_exif, &c_options->thumbnails.use_exif);

	pref_checkbox_new_int(group, _("Thumbnail color management"),
				options->thumbnails.use_color_management, &c_options->thumbnails.use_color_management);

#if HAVE_FFMPEGTHUMBNAILER_METADATA
	pref_checkbox_new_int(group, _("Use embedded metadata in video files as thumbnails when available"),
			      options->thumbnails.use_ft_metadata, &c_options->thumbnails.use_ft_metadata);
#endif

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Image loading and caching"), GTK_ORIENTATION_VERTICAL);

	pref_spin_new_int(group, _("Decoded image cache size (MiB):"), nullptr,
			  0, 99999, 1, options->image.image_cache_max, &c_options->image.image_cache_max);
	pref_checkbox_new_int(group, _("Preload next image"),
			      options->image.enable_read_ahead, &c_options->image.enable_read_ahead);

	pref_checkbox_new_int(group, _("Refresh on file change"),
			      options->update_on_time_change, &c_options->update_on_time_change);
}

/* image tab */
static void config_tab_image(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *table;
	GtkWidget *spin;

	vbox = scrolled_notebook_page(notebook, _("Image"));

	group = pref_group_new(vbox, FALSE, _("Zoom"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_quality_menu(table, 0, 0, _("Quality:"), options->image.zoom_quality, &c_options->image.zoom_quality);

	pref_checkbox_new_int(group, _("Two pass rendering (apply HQ zoom and color correction in second pass)"),
			      options->image.zoom_2pass, &c_options->image.zoom_2pass);

	c_options->image.zoom_increment = options->image.zoom_increment;
	spin = pref_spin_new(group, _("Zoom increment:"), nullptr,
			     0.01, 4.0, 0.01, 2, static_cast<gdouble>(options->image.zoom_increment) / 100.0,
			     G_CALLBACK(zoom_increment_cb), nullptr);
	gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_ALWAYS);

	c_options->image.zoom_style = options->image.zoom_style;
	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_zoom_style_selection_menu(table, 0, 0, _("Zoom style:"), options->image.zoom_style, &c_options->image.zoom_style);

	group = pref_group_new(vbox, FALSE, _("Tile size"), GTK_ORIENTATION_VERTICAL);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	spin = pref_spin_new_int(hbox, _("Pixels"), _("(Requires restart)"),
				 128, 4096, 128,
				 options->image.tile_size, &c_options->image.tile_size);
	gtk_widget_set_tooltip_text(GTK_WIDGET(hbox), _("This value changes the size of the tiles large images are split into. Increasing the size of the tiles will reduce the tiling effect seen on image changes, but will also slightly increase the delay before the first part of a large image is seen."));

	group = pref_group_new(vbox, FALSE, _("Appearance"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Use custom border color in window mode"),
			      options->image.use_custom_border_color, &c_options->image.use_custom_border_color);

	pref_checkbox_new_int(group, _("Use custom border color in fullscreen mode"),
			      options->image.use_custom_border_color_in_fullscreen, &c_options->image.use_custom_border_color_in_fullscreen);

	pref_color_button_new(group, _("Border color"), &options->image.border_color,
			      G_CALLBACK(pref_color_button_set_cb), &c_options->image.border_color);

	c_options->image.border_color = options->image.border_color;

	pref_color_button_new(group, _("Alpha channel color 1"), &options->image.alpha_color_1,
			      G_CALLBACK(pref_color_button_set_cb), &c_options->image.alpha_color_1);

	pref_color_button_new(group, _("Alpha channel color 2"), &options->image.alpha_color_2,
			      G_CALLBACK(pref_color_button_set_cb), &c_options->image.alpha_color_2);

	c_options->image.alpha_color_1 = options->image.alpha_color_1;
	c_options->image.alpha_color_2 = options->image.alpha_color_2;
}

static void config_tab_windows(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *widget;

	vbox = scrolled_notebook_page(notebook, _("Windows"));

	group = pref_group_new(vbox, FALSE, _("State"), GTK_ORIENTATION_VERTICAL);

	widget = pref_checkbox_new_int(group, _("Hide window decorations"),
			      options->hide_window_decorations, &c_options->hide_window_decorations);
	gtk_widget_set_tooltip_text(widget, _("Remove borders and title bar from windows. A restart of Geeqie is required for this feature to take effect on the main layout window"));

	group = pref_group_new(vbox, FALSE, _("Full screen"), GTK_ORIENTATION_VERTICAL);

	c_options->fullscreen.screen = options->fullscreen.screen;
	c_options->fullscreen.above = options->fullscreen.above;
	hbox = fullscreen_prefs_selection_new(_("Location:"), &c_options->fullscreen.screen, &c_options->fullscreen.above);
	gq_gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	pref_checkbox_new_int(group, _("Smooth image flip"),
			      options->fullscreen.clean_flip, &c_options->fullscreen.clean_flip);
}

static void config_tab_osd(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *image_overlay_template_view;
	GtkWidget *scrolled;
	GtkWidget *scrolled_pre_formatted;
	GtkTextBuffer *buffer;
	GtkWidget *label;
	GtkWidget *subgroup;

	vbox = scrolled_notebook_page(notebook, _("OSD"));

	image_overlay_template_view = gtk_text_view_new();

	group = pref_group_new(vbox, FALSE, _("Overlay Screen Display"), GTK_ORIENTATION_VERTICAL);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	scrolled_pre_formatted = osd_new(PRE_FORMATTED_COLUMNS, image_overlay_template_view);
	gtk_widget_set_size_request(scrolled_pre_formatted, 200, 150);
	gq_gtk_box_pack_start(GTK_BOX(subgroup), scrolled_pre_formatted, FALSE, FALSE, 0);
	gtk_widget_show(scrolled_pre_formatted);
	gtk_widget_show(subgroup);

	pref_line(group, PREF_PAD_GAP);

	pref_label_new(group, _("Image overlay template"));

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gtk_widget_set_size_request(scrolled, 200, 150);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
									GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_box_pack_start(GTK_BOX(group), scrolled, TRUE, TRUE, 5);
	gtk_widget_show(scrolled);

	gtk_widget_set_tooltip_markup(image_overlay_template_view,
					_("Extensive formatting options are shown in the Help file"));

	gq_gtk_container_add(GTK_WIDGET(scrolled), image_overlay_template_view);
	gtk_widget_show(image_overlay_template_view);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(nullptr, GQ_ICON_SELECT_FONT, _("Font"),
				 G_CALLBACK(image_overlay_set_font_cb), notebook);

	gq_gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_SELECT_COLOR, _("Text"), G_CALLBACK(image_overlay_set_text_color_cb), nullptr);
	gq_gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_SELECT_COLOR, _("Background"), G_CALLBACK(image_overlay_set_background_color_cb), nullptr);
	gq_gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
	image_overlay_set_text_colors();

	button = pref_button_new(nullptr, nullptr, _("Defaults"),
				 G_CALLBACK(image_overlay_default_template_cb), image_overlay_template_view);
	gq_gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(image_overlay_template_view));
	if (options->image_overlay.template_string) gtk_text_buffer_set_text(buffer, options->image_overlay.template_string, -1);
	g_signal_connect(G_OBJECT(buffer), "changed",
			 G_CALLBACK(image_overlay_template_view_changed_cb), image_overlay_template_view);

	pref_line(group, PREF_PAD_GAP);

	group = pref_group_new(vbox, FALSE, _("Exif, XMP or IPTC tags"), GTK_ORIENTATION_VERTICAL);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
	label = gtk_label_new(_("%Exif.Image.Orientation%"));
	gq_gtk_box_pack_start(GTK_BOX(hbox),label, FALSE,FALSE,0);
	gtk_widget_show(label);
	pref_spacer(group,TRUE);

	group = pref_group_new(vbox, FALSE, _("Field separators"), GTK_ORIENTATION_VERTICAL);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
	label = gtk_label_new(_("Separator shown only if both fields are non-null:\n%formatted.ShutterSpeed%|%formatted.ISOSpeedRating%"));
	gq_gtk_box_pack_start(GTK_BOX(hbox),label, FALSE,FALSE,0);
	gtk_widget_show(label);
	pref_spacer(group,TRUE);

	group = pref_group_new(vbox, FALSE, _("Field maximum length"), GTK_ORIENTATION_VERTICAL);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
	label = gtk_label_new(_("%path:39%"));
	gq_gtk_box_pack_start(GTK_BOX(hbox),label, FALSE,FALSE,0);
	gtk_widget_show(label);
	pref_spacer(group,TRUE);

	group = pref_group_new(vbox, FALSE, _("Pre- and post- text"), GTK_ORIENTATION_VERTICAL);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
	label = gtk_label_new(_("Text shown only if the field is non-null:\n%formatted.Aperture:F no. * setting%\n %formatted.Aperture:10:F no. * setting%"));
	gq_gtk_box_pack_start(GTK_BOX(hbox),label, FALSE,FALSE,0);
	gtk_widget_show(label);
	pref_spacer(group,TRUE);

	group = pref_group_new(vbox, FALSE, _("Pango markup"), GTK_ORIENTATION_VERTICAL);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
	label = gtk_label_new(_("<b>bold</b>\n<u>underline</u>\n<i>italic</i>\n<s>strikethrough</s>"));
	gq_gtk_box_pack_start(GTK_BOX(hbox),label, FALSE,FALSE,0);
	gtk_widget_show(label);
}

static GtkTreeModel *create_class_model()
{
	GtkListStore *model;
	GtkTreeIter iter;
	gint i;

	/* create list store */
	model = gtk_list_store_new(1, G_TYPE_STRING);
	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, _(format_class_list[i]), -1);
		}
	return GTK_TREE_MODEL (model);
}


/* filtering tab */
static gint filter_table_sort_cb(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	gint n = GPOINTER_TO_INT(data);
	gint ret = 0;
	FilterEntry *filter_a;
	FilterEntry *filter_b;

	gtk_tree_model_get(model, a, 0, &filter_a, -1);
	gtk_tree_model_get(model, b, 0, &filter_b, -1);

	switch (n)
		{
		case FILETYPES_COLUMN_ENABLED:
			{
			ret = filter_a->enabled - filter_b->enabled;
			break;
			}
		case FILETYPES_COLUMN_FILTER:
			{
			ret = g_utf8_collate(filter_a->extensions, filter_b->extensions);
			break;
			}
		case FILETYPES_COLUMN_DESCRIPTION:
			{
			ret = g_utf8_collate(filter_a->description, filter_b->description);
			break;
			}
		case FILETYPES_COLUMN_CLASS:
			{
			ret = g_strcmp0(format_class_list[filter_a->file_class], format_class_list[filter_b->file_class]);
			break;
			}
		default:
			g_return_val_if_reached(0);
		}

	return ret;
}

static gboolean search_function_cb(GtkTreeModel *model, gint, const gchar *key, GtkTreeIter *iter, gpointer)
{
	FilterEntry *fe;
	gboolean ret = TRUE;

	gtk_tree_model_get(model, iter, 0, &fe, -1);

	if (g_strstr_len(fe->extensions, -1, key))
		{
		ret = FALSE;
		}

	return ret;
}

static void config_tab_files(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *frame;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *scrolled;
	GtkWidget *filter_view;
	GtkCellRenderer *renderer;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;

	vbox = scrolled_notebook_page(notebook, _("File Filters"));

	group = pref_box_new(vbox, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	pref_checkbox_new_int(group, _("Show hidden files or folders"),
			      options->file_filter.show_hidden_files, &c_options->file_filter.show_hidden_files);
	pref_checkbox_new_int(group, _("Case sensitive sort (Search and Collection windows, and tab completion)"), options->file_sort.case_sensitive, &c_options->file_sort.case_sensitive);
	pref_checkbox_new_int(group, _("Disable file extension checks"),
			      options->file_filter.disable_file_extension_checks, &c_options->file_filter.disable_file_extension_checks);

	ct_button = pref_checkbox_new_int(group, _("Disable File Filtering"),
					  options->file_filter.disable, &c_options->file_filter.disable);


	group = pref_group_new(vbox, TRUE, _("File types"), GTK_ORIENTATION_VERTICAL);

	frame = pref_group_parent(group);
	g_signal_connect(G_OBJECT(ct_button), "toggled",
			 G_CALLBACK(filter_disable_cb), frame);
	gtk_widget_set_sensitive(frame, !options->file_filter.disable);

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gq_gtk_box_pack_start(GTK_BOX(group), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	filter_store = gtk_list_store_new(1, G_TYPE_POINTER);
	filter_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(filter_store));
	g_object_unref(filter_store);
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(filter_view));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_SINGLE);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(filter_view), FALSE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Enabled"));
	gtk_tree_view_column_set_resizable(column, TRUE);

	renderer = gtk_cell_renderer_toggle_new();
	g_signal_connect(G_OBJECT(renderer), "toggled",
			 G_CALLBACK(filter_store_enable_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_ENABLE), nullptr);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(filter_store), FILETYPES_COLUMN_ENABLED, filter_table_sort_cb, GINT_TO_POINTER(FILETYPES_COLUMN_ENABLED), nullptr);
	gtk_tree_view_column_set_sort_column_id(column, FILETYPES_COLUMN_ENABLED);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Filter"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(filter_store), FILETYPES_COLUMN_FILTER, filter_table_sort_cb, GINT_TO_POINTER(FILETYPES_COLUMN_FILTER), nullptr);
	gtk_tree_view_column_set_sort_column_id(column, FILETYPES_COLUMN_FILTER);

	renderer = gtk_cell_renderer_text_new();
	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(filter_store_ext_edit_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	g_object_set(G_OBJECT(renderer), "editable", static_cast<gboolean>TRUE, NULL);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_EXTENSION), nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(filter_view), TRUE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(filter_view), FILETYPES_COLUMN_FILTER);
	gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(filter_view), search_function_cb, nullptr, nullptr);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Description"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_column_set_fixed_width(column, 200);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);

	renderer = gtk_cell_renderer_text_new();
	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(filter_store_desc_edit_cb), filter_store);
	g_object_set(G_OBJECT(renderer), "editable", static_cast<gboolean>TRUE, NULL);
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_DESCRIPTION), nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(filter_store), FILETYPES_COLUMN_DESCRIPTION, filter_table_sort_cb, GINT_TO_POINTER(FILETYPES_COLUMN_DESCRIPTION), nullptr);
	gtk_tree_view_column_set_sort_column_id(column, FILETYPES_COLUMN_DESCRIPTION);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Class"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	renderer = gtk_cell_renderer_combo_new();
	g_object_set(G_OBJECT(renderer), "editable", static_cast<gboolean>TRUE,
					 "model", create_class_model(),
					 "text-column", 0,
					 "has-entry", FALSE,
					 NULL);

	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(filter_store_class_edit_cb), filter_store);
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, filter_set_func,
						GINT_TO_POINTER(FE_CLASS), nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(filter_view), column);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(filter_store), FILETYPES_COLUMN_CLASS, filter_table_sort_cb, GINT_TO_POINTER(FILETYPES_COLUMN_CLASS), nullptr);
	gtk_tree_view_column_set_sort_column_id(column, FILETYPES_COLUMN_CLASS);

	filter_store_populate();
	gq_gtk_container_add(GTK_WIDGET(scrolled), filter_view);
	gtk_widget_show(filter_view);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(nullptr, nullptr, _("Defaults"),
				 G_CALLBACK(filter_default_cb), filter_view);
	gq_gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_REMOVE, _("Remove"),
				 G_CALLBACK(filter_remove_cb), filter_view);
	gq_gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_ADD, _("Add"),
				 G_CALLBACK(filter_add_cb), filter_view);
	gq_gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
}

/* metadata tab */
#if HAVE_LCMS
static void intent_menu_cb(GtkWidget *combo, gpointer data)
{
	auto option = static_cast<gint *>(data);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)))
		{
		case 0:
		default:
			*option = INTENT_PERCEPTUAL;
			break;
		case 1:
			*option = INTENT_RELATIVE_COLORIMETRIC;
			break;
		case 2:
			*option = INTENT_SATURATION;
			break;
		case 3:
			*option = INTENT_ABSOLUTE_COLORIMETRIC;
			break;
		}
}

static void add_intent_menu(GtkWidget *table, gint column, gint row, const gchar *text,
			     gint option, gint *option_c)
{
	GtkWidget *combo;
	gint current = 0;

	*option_c = option;

	pref_table_label(table, column, row, text, GTK_ALIGN_START);

	combo = gtk_combo_box_text_new();

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Perceptual"));
	if (option == INTENT_PERCEPTUAL) current = 0;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Relative Colorimetric"));
	if (option == INTENT_RELATIVE_COLORIMETRIC) current = 1;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Saturation"));
	if (option == INTENT_SATURATION) current = 2;
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Absolute Colorimetric"));
	if (option == INTENT_ABSOLUTE_COLORIMETRIC) current = 3;

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);

	gtk_widget_set_tooltip_text(combo,_("Refer to the lcms documentation for the defaults used when the selected Intent is not available"));

	g_signal_connect(G_OBJECT(combo), "changed",
			 G_CALLBACK(intent_menu_cb), option_c);

	gq_gtk_grid_attach(GTK_GRID(table), combo, column + 1, column + 2, row, row + 1, GTK_SHRINK, static_cast<GtkAttachOptions>(0), 0, 0);
	gtk_widget_show(combo);
}
#endif

static void config_tab_color(GtkWidget *notebook)
{
	GtkWidget *label;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *tabcomp;
	GtkWidget *table;
	gint i;

	vbox = scrolled_notebook_page(notebook, _("Color management"));

	group =  pref_group_new(vbox, FALSE, _("Input profiles"), GTK_ORIENTATION_VERTICAL);
#if !HAVE_LCMS
	gtk_widget_set_sensitive(pref_group_parent(group), FALSE);
#endif

	table = pref_table_new(group, 3, COLOR_PROFILE_INPUTS + 1, FALSE, FALSE);
	gtk_grid_set_column_spacing(GTK_GRID(table), PREF_PAD_GAP);

	label = pref_table_label(table, 0, 0, _("Type"), GTK_ALIGN_START);
	pref_label_bold(label, TRUE, FALSE);

	label = pref_table_label(table, 1, 0, _("Menu name"), GTK_ALIGN_START);
	pref_label_bold(label, TRUE, FALSE);

	label = pref_table_label(table, 2, 0, _("File"), GTK_ALIGN_START);
	pref_label_bold(label, TRUE, FALSE);

	for (i = 0; i < COLOR_PROFILE_INPUTS; i++)
		{
		GtkWidget *entry;
		gchar *buf;

		buf = g_strdup_printf(_("Input %d:"), i + COLOR_PROFILE_FILE);
		pref_table_label(table, 0, i + 1, buf, GTK_ALIGN_END);
		g_free(buf);

		entry = gtk_entry_new();
		gtk_entry_set_max_length(GTK_ENTRY(entry), EDITOR_NAME_MAX_LENGTH);
		if (options->color_profile.input_name[i])
			{
			gq_gtk_entry_set_text(GTK_ENTRY(entry), options->color_profile.input_name[i]);
			}
		gq_gtk_grid_attach(GTK_GRID(table), entry, 1, 2, i + 1, i + 2, static_cast<GtkAttachOptions>(GTK_FILL | GTK_EXPAND), static_cast<GtkAttachOptions>(0), 0, 0);
		gtk_widget_show(entry);
		color_profile_input_name_entry[i] = entry;

		tabcomp = tab_completion_new(&entry, options->color_profile.input_file[i], nullptr, ".icc", "ICC Files", nullptr);
		tab_completion_add_select_button(entry, _("Select color profile"), FALSE);
		gtk_widget_set_size_request(entry, 160, -1);
		gq_gtk_grid_attach(GTK_GRID(table), tabcomp, 2, 3, i + 1, i + 2, static_cast<GtkAttachOptions>(GTK_FILL | GTK_EXPAND), static_cast<GtkAttachOptions>(0), 0, 0);
		gtk_widget_show(tabcomp);
		color_profile_input_file_entry[i] = entry;
		}

	group =  pref_group_new(vbox, FALSE, _("Screen profile"), GTK_ORIENTATION_VERTICAL);
#if !HAVE_LCMS
	gtk_widget_set_sensitive(pref_group_parent(group), FALSE);
#endif
	pref_checkbox_new_int(group, _("Use system screen profile if available"),
			      options->color_profile.use_x11_screen_profile, &c_options->color_profile.use_x11_screen_profile);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);

	pref_table_label(table, 0, 0, _("Screen:"), GTK_ALIGN_END);
	tabcomp = tab_completion_new(&color_profile_screen_file_entry,
				     options->color_profile.screen_file, nullptr, ".icc", "ICC Files", nullptr);
	tab_completion_add_select_button(color_profile_screen_file_entry, _("Select color profile"), FALSE);
	gtk_widget_set_size_request(color_profile_screen_file_entry, 160, -1);
#if HAVE_LCMS
	add_intent_menu(table, 0, 1, _("Render Intent:"), options->color_profile.render_intent, &c_options->color_profile.render_intent);
#endif
	gq_gtk_grid_attach(GTK_GRID(table), tabcomp, 1, 2, 0, 1, static_cast<GtkAttachOptions>(GTK_FILL | GTK_EXPAND), static_cast<GtkAttachOptions>(0), 0, 0);

	gtk_widget_show(tabcomp);
}

/* advanced entry tab */
static void use_geeqie_trash_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->file_ops.use_system_trash = FALSE;
		c_options->file_ops.no_trash = FALSE;
		}
}

static void use_system_trash_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->file_ops.use_system_trash = TRUE;
		c_options->file_ops.no_trash = FALSE;
		}
}

static void use_no_cache_cb(GtkWidget *widget, gpointer)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		c_options->file_ops.no_trash = TRUE;
		}
}

static void config_tab_behavior(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *tabcomp;
	GtkWidget *ct_button;
	GtkWidget *spin;
	GtkWidget *table;
	GtkWidget *hide_window_in_fullscreen;
	GtkWidget *tmp;

	vbox = scrolled_notebook_page(notebook, _("Behavior"));

	group = pref_group_new(vbox, FALSE, _("Delete"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Confirm permanent file delete"),
			      options->file_ops.confirm_delete, &c_options->file_ops.confirm_delete);
	pref_checkbox_new_int(group, _("Enable Delete key"),
			      options->file_ops.enable_delete_key, &c_options->file_ops.enable_delete_key);

	ct_button = pref_radiobutton_new(group, nullptr, _("Use Geeqie trash location"),
					!options->file_ops.use_system_trash && !options->file_ops.no_trash, G_CALLBACK(use_geeqie_trash_cb),nullptr);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_checkbox_link_sensitivity(ct_button, hbox);

	pref_spacer(hbox, PREF_PAD_INDENT - PREF_PAD_SPACE);
	pref_label_new(hbox, _("Folder:"));

	tabcomp = tab_completion_new(&safe_delete_path_entry, options->file_ops.safe_delete_path, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(safe_delete_path_entry, nullptr, TRUE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), tabcomp, TRUE, TRUE, 0);
	gtk_widget_show(tabcomp);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);
	pref_checkbox_link_sensitivity(ct_button, hbox);

	pref_spacer(hbox, PREF_PAD_INDENT - PREF_PAD_GAP);
	spin = pref_spin_new_int(hbox, _("Maximum size:"), _("MiB"),
				 0, 2048, 1, options->file_ops.safe_delete_folder_maxsize, &c_options->file_ops.safe_delete_folder_maxsize);
	gtk_widget_set_tooltip_markup(spin, _("Set to 0 for unlimited size"));
	button = pref_button_new(nullptr, nullptr, _("View"),
				 G_CALLBACK(safe_delete_view_cb), nullptr);
	gq_gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_CLEAR, nullptr,
				 G_CALLBACK(safe_delete_clear_cb), nullptr);
	gq_gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);

	c_options->file_ops.no_trash = options->file_ops.no_trash;
	c_options->file_ops.use_system_trash = options->file_ops.use_system_trash;

	pref_radiobutton_new(group, ct_button, _("Use system Trash bin"),
					options->file_ops.use_system_trash && !options->file_ops.no_trash, G_CALLBACK(use_system_trash_cb), nullptr);

	pref_radiobutton_new(group, ct_button, _("Use no trash at all"),
			options->file_ops.no_trash, G_CALLBACK(use_no_cache_cb), nullptr);

	gtk_widget_show(button);

	pref_spacer(group, PREF_PAD_GROUP);


	group = pref_group_new(vbox, FALSE, _("Behavior"), GTK_ORIENTATION_VERTICAL);

	tmp = pref_checkbox_new_int(group, _("Circular selection lists"),
			      options->circular_selection_lists, &c_options->circular_selection_lists);
	gtk_widget_set_tooltip_text(tmp, _("Traverse selection lists in a circular manner"));

	hide_window_in_fullscreen = pref_checkbox_new_int(group, _("Hide window in fullscreen"),
				options->hide_window_in_fullscreen, &c_options->hide_window_in_fullscreen);
	gtk_widget_set_tooltip_text(hide_window_in_fullscreen, _("When alt-tabbing, prevent Geeqie window showing twice"));

	pref_spin_new_int(group, _("Recent folder list maximum size"), nullptr,
			  1, 50, 1, options->open_recent_list_maxsize, &c_options->open_recent_list_maxsize);

	pref_spin_new_int(group, _("Drag'n drop icon size"), nullptr,
			  16, 256, 16, options->dnd_icon_size, &c_options->dnd_icon_size);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_dnd_default_action_selection_menu(table, 0, 0, _("Drag`n drop default action:"), options->dnd_default_action, &c_options->dnd_default_action);

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Navigation"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Progressive keyboard scrolling"),
			      options->progressive_key_scrolling, &c_options->progressive_key_scrolling);
	pref_spin_new_int(group, _("Keyboard scrolling step multiplier:"), nullptr,
			  1, 32, 1, options->keyboard_scroll_step, reinterpret_cast<int *>(&c_options->keyboard_scroll_step));
	pref_checkbox_new_int(group, _("Mouse wheel scrolls image"),
			      options->mousewheel_scrolls, &c_options->mousewheel_scrolls);
	pref_checkbox_new_int(group, _("Play video by left click on image"),
			      options->image_l_click_video, &c_options->image_l_click_video);
	table = pref_table_new(group, 2, 1, FALSE, FALSE);
	add_video_menu(table, 0, 0, _("Play with:"), options->image_l_click_video_editor, &c_options->image_l_click_video_editor);

#ifdef DEBUG
	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("Debugging"), GTK_ORIENTATION_VERTICAL);

	pref_spin_new_int(group, _("Debug level:"), nullptr,
			  DEBUG_LEVEL_MIN, DEBUG_LEVEL_MAX, 1, get_debug_level(), &debug_c);

	pref_checkbox_new_int(group, _("Timer data"),
			options->log_window.timer_data, &c_options->log_window.timer_data);

	pref_spin_new_int(group, _("Log Window max. lines:"), nullptr,
			  1, 99999, 1, options->log_window_lines, &options->log_window_lines);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Log Window F1 command: "));
	log_window_f1_entry = gtk_entry_new();
	gq_gtk_entry_set_text(GTK_ENTRY(log_window_f1_entry), options->log_window.action);
	gq_gtk_box_pack_start(GTK_BOX(hbox), log_window_f1_entry, FALSE, FALSE, 0);
	gtk_entry_set_width_chars(GTK_ENTRY(log_window_f1_entry), 15);
	gtk_widget_show(log_window_f1_entry);
#endif
}

/* accelerators tab */

static gboolean accel_search_function_cb(GtkTreeModel *model, gint column, const gchar *key, GtkTreeIter *iter, gpointer)
{
	gboolean ret = TRUE;
	gchar *key_nocase;
	gchar *text;
	gchar *text_nocase;

	gtk_tree_model_get(model, iter, column, &text, -1);
	text_nocase = g_utf8_casefold(text, -1);
	key_nocase = g_utf8_casefold(key, -1);

	if (g_strstr_len(text_nocase, -1, key_nocase))
		{
		ret = FALSE;
		}

	g_free(key_nocase);
	g_free(text);
	g_free(text_nocase);

	return ret;
}

static void accel_row_activated_cb(GtkTreeView *tree_view, GtkTreePath *, GtkTreeViewColumn *column, gpointer)
{
	GList *list;
	gint col_num = 0;

	list = gtk_tree_view_get_columns(tree_view);
	col_num = g_list_index(list, column);

	g_list_free(list);

	gtk_tree_view_set_search_column(tree_view, col_num);
}

static void config_tab_accelerators(GtkWidget *notebook)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *scrolled;
	GtkWidget *accel_view;
	GtkCellRenderer *renderer;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;

	vbox = scrolled_notebook_page(notebook, _("Keyboard"));

	group = pref_group_new(vbox, TRUE, _("Accelerators"), GTK_ORIENTATION_VERTICAL);

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gq_gtk_box_pack_start(GTK_BOX(group), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	accel_store = gtk_tree_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	accel_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(accel_store));
	g_object_unref(accel_store);
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(accel_view));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_MULTIPLE);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(accel_view), FALSE);

	renderer = gtk_cell_renderer_text_new();

	column = gtk_tree_view_column_new_with_attributes(_("Action"),
							  renderer,
							  "text", AE_ACTION,
							  NULL);

	gtk_tree_view_column_set_sort_column_id(column, AE_ACTION);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accel_view), column);


	renderer = gtk_cell_renderer_accel_new();
	g_signal_connect(G_OBJECT(renderer), "accel-cleared",
			 G_CALLBACK(accel_store_cleared_cb), accel_store);
	g_signal_connect(G_OBJECT(renderer), "accel-edited",
			 G_CALLBACK(accel_store_edited_cb), accel_store);


	g_object_set (renderer,
		      "editable", TRUE,
		      "accel-mode", GTK_CELL_RENDERER_ACCEL_MODE_OTHER,
		      NULL);

	column = gtk_tree_view_column_new_with_attributes(_("KEY"),
							  renderer,
							  "text", AE_KEY,
							  NULL);

	gtk_tree_view_column_set_sort_column_id(column, AE_KEY);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accel_view), column);

	renderer = gtk_cell_renderer_text_new();

	column = gtk_tree_view_column_new_with_attributes(_("Tooltip"),
							  renderer,
							  "text", AE_TOOLTIP,
							  NULL);

	gtk_tree_view_column_set_sort_column_id(column, AE_TOOLTIP);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accel_view), column);

	renderer = gtk_cell_renderer_text_new();

	column = gtk_tree_view_column_new_with_attributes("Accel",
							  renderer,
							  "text", AE_ACCEL,
							  NULL);

	gtk_tree_view_column_set_sort_column_id(column, AE_ACCEL);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accel_view), column);

	column = gtk_tree_view_column_new_with_attributes("Icon",
							  renderer,
							  "text", AE_ICON,
							  NULL);

	gtk_tree_view_column_set_sort_column_id(column, AE_ICON);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accel_view), column);

	/* Search on text in column */
	gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(accel_view), TRUE);
	g_signal_connect(accel_view, "row_activated", G_CALLBACK(accel_row_activated_cb), accel_store);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(accel_view), TRUE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(accel_view), AE_TOOLTIP);
	gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(accel_view), accel_search_function_cb, nullptr, nullptr);

	accel_store_populate();
	gq_gtk_container_add(GTK_WIDGET(scrolled), accel_view);
	gtk_widget_show(accel_view);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(nullptr, nullptr, _("Defaults"),
				 G_CALLBACK(accel_default_cb), accel_view);
	gq_gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, nullptr, _("Reset selected"),
				 G_CALLBACK(accel_reset_cb), accel_view);
	gtk_widget_set_tooltip_text(button, _("Will only reset changes made before the settings are saved"));
	gq_gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, nullptr, _("Clear selected"),
				 G_CALLBACK(accel_clear_cb), accel_view);
	gq_gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
}

/* advanced tab */
static void config_tab_advanced(GtkWidget *notebook)
{
	GList *extensions_list = nullptr;
	GtkWidget *alternate_checkbox;
	GtkWidget *dupes_threads_spin;
	GtkWidget *group;
	GtkWidget *subgroup;
	GtkWidget *tabcomp;
	GtkWidget *threads_string_label;
	GtkWidget *types_string_label;
	GtkWidget *vbox;

	vbox = scrolled_notebook_page(notebook, _("Advanced"));
	group = pref_group_new(vbox, FALSE, _("External preview extraction"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new_int(group, _("Use external preview extraction -  Requires restart"), options->external_preview.enable, &c_options->external_preview.enable);

	pref_spacer(group, PREF_PAD_GROUP);

	GSList *formats_list = gdk_pixbuf_get_formats();
	for (GSList *work = formats_list; work; work = work->next)
		{
		auto *fm = static_cast<GdkPixbufFormat *>(work->data);
		g_auto(GStrv) extensions = gdk_pixbuf_format_get_extensions(fm);
		const guint extensions_count = g_strv_length(extensions);

		for (guint i = 0; i < extensions_count; i++)
			{
			extensions_list = g_list_insert_sorted(extensions_list, g_strdup(extensions[i]), reinterpret_cast<GCompareFunc>(g_strcmp0));
			}
		}
	g_slist_free(formats_list);

	g_autoptr(GString) types_string = g_string_new(nullptr);
	for (GList *work = extensions_list; work; work = work->next)
		{
		if (types_string->len > 0)
			{
			types_string = g_string_append(types_string, ", ");
			}
		types_string = g_string_append(types_string, static_cast<gchar *>(work->data));
		}
	g_list_free_full(extensions_list, g_free);

	types_string = g_string_prepend(types_string, _("Usable file types:\n"));
	types_string_label = pref_label_new(group, types_string->str);
	gtk_label_set_line_wrap(GTK_LABEL(types_string_label), TRUE);

	pref_spacer(group, PREF_PAD_GROUP);

	group = pref_group_new(vbox, FALSE, _("File identification tool"), GTK_ORIENTATION_VERTICAL);
	external_preview_select_entry = gtk_entry_new();
	tabcomp = tab_completion_new(&external_preview_select_entry, options->external_preview.select, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(external_preview_select_entry, _("Select file identification tool"), FALSE);
	gq_gtk_box_pack_start(GTK_BOX(group), tabcomp, TRUE, TRUE, 0);
	gtk_widget_show(tabcomp);

	group = pref_group_new(vbox, FALSE, _("Preview extraction tool"), GTK_ORIENTATION_VERTICAL);
	external_preview_extract_entry = gtk_entry_new();
	tabcomp = tab_completion_new(&external_preview_extract_entry, options->external_preview.extract, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(external_preview_extract_entry, _("Select preview extraction tool"), FALSE);
	gq_gtk_box_pack_start(GTK_BOX(group), tabcomp, TRUE, TRUE, 0);
	gtk_widget_show(tabcomp);

	gtk_widget_show(vbox);

	pref_spacer(group, PREF_PAD_GROUP);

	pref_line(vbox, PREF_PAD_SPACE);
	group = pref_group_new(vbox, FALSE, _("Thread pool limits"), GTK_ORIENTATION_VERTICAL);

	threads_string_label = pref_label_new(group, _("This option limits the number of threads (or cpu cores) that Geeqie will use when running duplicate checks.\nThe value 0 means all available cores will be used."));
	gtk_label_set_line_wrap(GTK_LABEL(threads_string_label), TRUE);

	pref_spacer(vbox, PREF_PAD_GROUP);

	dupes_threads_spin = pref_spin_new_int(vbox, _("Duplicate check:"), _("max. threads"), 0, get_cpu_cores(), 1, options->threads.duplicates, &c_options->threads.duplicates);
	gtk_widget_set_tooltip_markup(dupes_threads_spin, _("Set to 0 for unlimited"));

	pref_spacer(group, PREF_PAD_GROUP);

	pref_line(vbox, PREF_PAD_SPACE);

	group = pref_group_new(vbox, FALSE, _("Alternate similarity alogorithm"), GTK_ORIENTATION_VERTICAL);

	alternate_checkbox = pref_checkbox_new_int(group, _("Enable alternate similarity algorithm"), options->alternate_similarity_algorithm.enabled, &c_options->alternate_similarity_algorithm.enabled);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	pref_checkbox_link_sensitivity(alternate_checkbox, subgroup);

	alternate_checkbox = pref_checkbox_new_int(subgroup, _("Use grayscale"), options->alternate_similarity_algorithm.grayscale, &c_options->alternate_similarity_algorithm.grayscale);
	gtk_widget_set_tooltip_text(alternate_checkbox, _("Reduce fingerprint to grayscale"));
}

/* Main preferences window */
static void config_window_create(LayoutWindow *lw)
{
	GtkWidget *win_vbox;
	GtkWidget *hbox;
	GtkWidget *notebook;
	GtkWidget *button;
	GtkWidget *ct_button;

	if (!c_options) c_options = init_options(nullptr);

	configwindow = window_new("preferences", PIXBUF_INLINE_ICON_CONFIG, nullptr, _("Preferences"));
	DEBUG_NAME(configwindow);
	gtk_window_set_type_hint(GTK_WINDOW(configwindow), GDK_WINDOW_TYPE_HINT_DIALOG);
	g_signal_connect(G_OBJECT(configwindow), "delete_event",
			 G_CALLBACK(config_window_delete), NULL);
	gtk_window_resize(GTK_WINDOW(configwindow), lw->options.preferences_window.rect.width, lw->options.preferences_window.rect.height);
	gq_gtk_window_move(GTK_WINDOW(configwindow), lw->options.preferences_window.rect.x, lw->options.preferences_window.rect.y);
	gtk_window_set_resizable(GTK_WINDOW(configwindow), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(configwindow), PREF_PAD_BORDER);

	win_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	gq_gtk_container_add(GTK_WIDGET(configwindow), win_vbox);
	gtk_widget_show(win_vbox);

	notebook = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_LEFT);
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gq_gtk_box_pack_start(GTK_BOX(win_vbox), notebook, TRUE, TRUE, 0);

	config_tab_general(notebook);
	config_tab_image(notebook);
	config_tab_osd(notebook);
	config_tab_windows(notebook);
	config_tab_accelerators(notebook);
	config_tab_files(notebook);
	config_tab_color(notebook);
	config_tab_behavior(notebook);
	config_tab_advanced(notebook);

	gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), lw->options.preferences_window.page_number);

	hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(hbox), PREF_PAD_BUTTON_GAP);
	gq_gtk_box_pack_end(GTK_BOX(win_vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = pref_button_new(nullptr, GQ_ICON_OK, "OK",
				 G_CALLBACK(config_window_ok_cb), notebook);
	gq_gtk_container_add(GTK_WIDGET(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_grab_default(button);
	gtk_widget_show(button);

	ct_button = button;

	button = pref_button_new(nullptr, GQ_ICON_CANCEL, _("Cancel"),
				 G_CALLBACK(config_window_close_cb), nullptr);
	gq_gtk_container_add(GTK_WIDGET(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	if (!generic_dialog_get_alternative_button_order(configwindow))
		{
		gtk_box_reorder_child(GTK_BOX(hbox), ct_button, -1);
		}

	gtk_widget_show(notebook);

	gtk_widget_show(configwindow);
}

/*
 *-----------------------------------------------------------------------------
 * config window show (public)
 *-----------------------------------------------------------------------------
 */

void show_config_window(LayoutWindow *lw)
{
	if (configwindow)
		{
		gtk_window_present(GTK_WINDOW(configwindow));
		return;
		}

	config_window_create(lw);
}

/*
 *-----------------
 * about window
 *-----------------
 */

void show_about_window(LayoutWindow *lw)
{
	GDataInputStream *data_stream;
	GInputStream *in_stream_authors;
	GInputStream *in_stream_translators;
	GString *copyright;
	gchar *author_line;
	gchar *authors[1000];
	gchar *comment;
	gchar *translators;
	gint i_authors = 0;
	gint n = 0;
	gsize bytes_read;
	gsize length;
	gsize size;
	guint32 flags;

	copyright = g_string_new(_("This program comes with absolutely no warranty.\nGNU General Public License, version 2 or later.\nSee https://www.gnu.org/licenses/old-licenses/gpl-2.0.html\n\n"));
	copyright = g_string_append(copyright, _("\n\nSome icons by https://www.flaticon.com"));

	in_stream_authors = g_resources_open_stream(GQ_RESOURCE_PATH_CREDITS "/authors", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);

	data_stream = g_data_input_stream_new(in_stream_authors);

	authors[0] = nullptr;
	while ((author_line = g_data_input_stream_read_line(G_DATA_INPUT_STREAM(data_stream), &length, nullptr, nullptr)))
		{
		authors[i_authors] = g_strdup(author_line);
		i_authors++;
		g_free(author_line);
		}
	authors[i_authors] = nullptr;

	g_input_stream_close(in_stream_authors, nullptr, nullptr);

	constexpr auto translators_path = GQ_RESOURCE_PATH_CREDITS "/translators";

	g_resources_get_info(translators_path, G_RESOURCE_LOOKUP_FLAGS_NONE, &size, &flags, nullptr);

	in_stream_translators = g_resources_open_stream(translators_path, G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
	translators = static_cast<gchar *>(g_malloc0(size));
	g_input_stream_read_all(in_stream_translators, translators, size, &bytes_read, nullptr, nullptr);
	g_input_stream_close(in_stream_translators, nullptr, nullptr);

	comment = g_strconcat(_("Project created by John Ellis\nGQview 1998\nGeeqie 2007\n\n\nDevelopment and bug reports:\n"), GQ_EMAIL_ADDRESS, _("\nhttps://github.com/BestImageViewer/geeqie/issues"), NULL);

	const gchar *artists[2] = {
	    "Néstor Díaz Valencia <nestor@estudionexos.com>",
	    nullptr,
	};

	gtk_show_about_dialog(GTK_WINDOW(lw->window),
	                      "title", _("About Geeqie"),
	                      "resizable", TRUE,
	                      "program-name", GQ_APPNAME,
	                      "version", VERSION,
	                      "logo-icon-name", PIXBUF_INLINE_LOGO,
	                      "icon-name", PIXBUF_INLINE_ICON,
	                      "website", GQ_WEBSITE,
	                      "website-label", _("Website"),
	                      "comments", comment,
	                      "artists", artists,
	                      "authors", authors,
	                      "translator-credits", translators,
	                      "wrap-license", TRUE,
	                      "license", copyright->str,
	                      NULL);

	g_string_free(copyright, TRUE);

	while(n < i_authors)
		{
		g_free(authors[n]);
		n++;
		}

	g_free(comment);
	g_free(translators);
	g_object_unref(data_stream);
	g_object_unref(in_stream_authors);
	g_object_unref(in_stream_translators);
}

static void image_overlay_set_text_colors()
{
	c_options->image_overlay.text_red = options->image_overlay.text_red;
	c_options->image_overlay.text_green = options->image_overlay.text_green;
	c_options->image_overlay.text_blue = options->image_overlay.text_blue;
	c_options->image_overlay.text_alpha = options->image_overlay.text_alpha;
	c_options->image_overlay.background_red = options->image_overlay.background_red;
	c_options->image_overlay.background_green = options->image_overlay.background_green;
	c_options->image_overlay.background_blue = options->image_overlay.background_blue;
	c_options->image_overlay.background_alpha = options->image_overlay.background_alpha;
}
