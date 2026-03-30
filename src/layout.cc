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

#include "layout.h"

#include <unistd.h>

#include <cstring>
#include <utility>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#  include <gdk/gdkx.h>
#endif
#include <glib-object.h>
#include <pango/pango.h>

#include "compat.h"
#include "debug.h"
#include "filedata.h"
#include "history-list.h"
#include "image-overlay.h"
#include "image.h"
#include "intl.h"
#include "layout-image.h"
#include "layout-util.h"
#include "logwindow.h"
#include "main-defines.h"
#include "main.h"
#include "menu.h"
#include "metadata.h"
#include "misc.h"
#include "pixbuf-util.h"
#include "preferences.h"
#include "rcfile.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"
#include "ui-utildlg.h"
#include "view-dir.h"
#include "view-file.h"
#include "window.h"

namespace
{

constexpr gint MAINWINDOW_DEF_WIDTH = 700;
constexpr gint MAINWINDOW_DEF_HEIGHT = 500;

constexpr gint PROGRESS_WIDTH = 150;

constexpr gint ZOOM_LABEL_WIDTH = 120;
} // namespace

LayoutWindow *main_lw = nullptr;

static void layout_list_scroll_to_subpart(LayoutWindow *lw, const gchar *needle);


/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

gboolean layout_valid(LayoutWindow **lw)
{
	if (main_lw) *lw = main_lw;
	return (*lw != nullptr);
}

/*
 *-----------------------------------------------------------------------------
 * menu, toolbar, and dir view
 *-----------------------------------------------------------------------------
 */

static void layout_path_entry_changed_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gchar *buf;

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(widget)) < 0) return;

	buf = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(lw->path_entry)));
	if (!lw->dir_fd || strcmp(buf, lw->dir_fd->path) != 0)
		{
		layout_set_path(lw, buf);
		}

	g_free(buf);
}

static void layout_path_entry_tab_cb(const gchar *path, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gchar *buf;

	buf = g_strdup(path);
	parse_out_relatives(buf);

	if (isdir(buf))
		{
		if ((!lw->dir_fd || strcmp(lw->dir_fd->path, buf) != 0) && layout_set_path(lw, buf))
			{
			gtk_widget_grab_focus(GTK_WIDGET(lw->path_entry));
			gint pos = -1;
			/* put the G_DIR_SEPARATOR back, if we are in tab completion for a dir and result was path change */
			gtk_editable_insert_text(GTK_EDITABLE(lw->path_entry), G_DIR_SEPARATOR_S, -1, &pos);
			gtk_editable_set_position(GTK_EDITABLE(lw->path_entry),
						  strlen(gq_gtk_entry_get_text(GTK_ENTRY(lw->path_entry))));
			}
		}
	else if (lw->dir_fd)
		{
		gchar *base = remove_level_from_path(buf);

		if (strcmp(lw->dir_fd->path, base) == 0)
			{
			layout_list_scroll_to_subpart(lw, filename_from_path(buf));
			}
		g_free(base);
		}

	g_free(buf);
}

static void layout_path_entry_cb(const gchar *path, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gchar *buf;

	buf = g_strdup(path);
	parse_out_relatives(buf);
	layout_set_path(lw, buf);
	g_free(buf);
}

static void layout_vd_select_cb(ViewDir *, FileData *fd, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_set_fd(lw, fd);
}

static void layout_path_entry_tab_append_cb(const gchar *, gpointer data, gint n)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (!lw || !lw->back_button) return;
	if (!layout_valid(&lw)) return;

	/* Enable back button if it makes sense */
	gtk_widget_set_sensitive(lw->back_button, (n > 1));
}

static gboolean path_entry_tooltip_cb(GtkWidget *widget, gpointer)
{
	GList *box_child_list;
	GtkComboBox *path_entry;
	gchar *current_path;

	box_child_list = gtk_container_get_children(GTK_CONTAINER(widget));
	path_entry = static_cast<GtkComboBox *>(box_child_list->data);
	current_path = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(path_entry));
	gtk_widget_set_tooltip_text(GTK_WIDGET(widget), current_path);

	g_free(current_path);
	g_list_free(box_child_list);

	return FALSE;
}

static GtkWidget *layout_tool_setup(LayoutWindow *lw)
{
	GtkWidget *box;
	GtkWidget *menu_bar;
	GtkWidget *menu_toolbar_box;
	GtkWidget *scroll_window;
	GtkWidget *tabcomp;

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	{
		menu_toolbar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		scroll_window = gq_gtk_scrolled_window_new(nullptr, nullptr);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);

		menu_bar = layout_actions_menu_bar(lw);
		gq_gtk_box_pack_start(GTK_BOX(menu_toolbar_box), menu_bar, FALSE, FALSE, 0);

		gq_gtk_container_add(GTK_WIDGET(scroll_window), menu_toolbar_box);
		gq_gtk_box_pack_start(GTK_BOX(box), scroll_window, FALSE, FALSE, 0);

		gq_gtk_widget_show_all(scroll_window);
	}

	tabcomp = tab_completion_new_with_history(&lw->path_entry, nullptr, "path_list", -1, layout_path_entry_cb, lw);
	DEBUG_NAME(tabcomp);
	tab_completion_add_tab_func(lw->path_entry, layout_path_entry_tab_cb, lw);
	tab_completion_add_append_func(lw->path_entry, layout_path_entry_tab_append_cb, lw);

	gq_gtk_box_pack_start(GTK_BOX(box), tabcomp, FALSE, FALSE, 0);

	gtk_widget_show(tabcomp);
	gtk_widget_set_has_tooltip(GTK_WIDGET(tabcomp), TRUE);
	g_signal_connect(G_OBJECT(tabcomp), "query_tooltip", G_CALLBACK(path_entry_tooltip_cb), lw);

	g_signal_connect(G_OBJECT(gtk_widget_get_parent(gtk_widget_get_parent(lw->path_entry))), "changed", G_CALLBACK(layout_path_entry_changed_cb), lw);

	lw->vd = vd_new(lw);

	vd_set_select_func(lw->vd, layout_vd_select_cb, lw);

	lw->dir_view = lw->vd->widget;
	DEBUG_NAME(lw->dir_view);
	gq_gtk_box_pack_start(GTK_BOX(box), lw->dir_view, TRUE, TRUE, 0);
	gtk_widget_show(lw->dir_view);

	gtk_widget_show(box);

	return box;
}

/*
 *-----------------------------------------------------------------------------
 * sort button (and menu)
 *-----------------------------------------------------------------------------
 */

static void layout_sort_menu_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw;
	SortType type;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	lw = static_cast<LayoutWindow *>(submenu_item_get_data(widget));
	if (!lw) return;

	type = static_cast<SortType>GPOINTER_TO_INT(data);

	if (type == SORT_EXIFTIME || type == SORT_EXIFTIMEDIGITIZED)
		{
		vf_read_metadata_in_idle(lw->vf);
		}
	layout_sort_set_files(lw, type, lw->options.file_view_list_sort.ascend, lw->options.file_view_list_sort.case_sensitive);
}

static void layout_sort_menu_ascend_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_sort_set_files(lw, lw->options.file_view_list_sort.method, !lw->options.file_view_list_sort.ascend, lw->options.file_view_list_sort.case_sensitive);
}

static void layout_sort_menu_case_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_sort_set_files(lw, lw->options.file_view_list_sort.method, lw->options.file_view_list_sort.ascend, !lw->options.file_view_list_sort.case_sensitive);
}

static void layout_sort_menu_hide_cb(GtkWidget *widget, gpointer)
{
	/* destroy the menu */
	g_object_unref(widget);
}

static void layout_sort_button_press_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;

	menu = submenu_add_sort(nullptr, G_CALLBACK(layout_sort_menu_cb), lw, FALSE, FALSE, TRUE, lw->options.file_view_list_sort.method);

	/* take ownership of menu */
	g_object_ref_sink(G_OBJECT(menu));

	/* ascending option */
	menu_item_add_divider(menu);
	menu_item_add_check(menu, _("Ascending"), lw->options.file_view_list_sort.ascend, G_CALLBACK(layout_sort_menu_ascend_cb), lw);
	menu_item_add_check(menu, _("Case"), lw->options.file_view_list_sort.case_sensitive, G_CALLBACK(layout_sort_menu_case_cb), lw);

	g_signal_connect(G_OBJECT(menu), "selection_done",
			 G_CALLBACK(layout_sort_menu_hide_cb), NULL);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static GtkWidget *layout_sort_button(LayoutWindow *lw, GtkWidget *box)
{
	GtkWidget *button;
	GtkWidget *frame;
	GtkWidget *image;

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	gq_gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gq_gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);
	gtk_widget_show(frame);

	image = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN, GTK_ICON_SIZE_BUTTON);
	button = gtk_button_new_with_label(sort_type_get_text(lw->options.file_view_list_sort.method));
	gtk_button_set_image(GTK_BUTTON(button), image);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(layout_sort_button_press_cb), lw);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_button_set_image_position(GTK_BUTTON(button), GTK_POS_RIGHT);

	gq_gtk_container_add(GTK_WIDGET(frame), button);

	gtk_widget_show(button);

	return button;
}

static void layout_zoom_menu_cb(GtkWidget *widget, gpointer data)
{
	ZoomMode mode;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	mode = static_cast<ZoomMode>GPOINTER_TO_INT(data);
	options->image.zoom_mode = mode;
}

static void layout_scroll_menu_cb(GtkWidget *widget, gpointer data)
{
	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	options->image.scroll_reset_method = static_cast<ScrollReset>(GPOINTER_TO_UINT(data));
	image_options_sync();
}

static void layout_zoom_menu_hide_cb(GtkWidget *widget, gpointer)
{
	/* destroy the menu */
	g_object_unref(widget);
}

static void layout_zoom_button_press_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;

	menu = submenu_add_zoom(nullptr, G_CALLBACK(layout_zoom_menu_cb),
			lw, FALSE, FALSE, TRUE, options->image.zoom_mode);

	/* take ownership of menu */
	g_object_ref_sink(G_OBJECT(menu));

	menu_item_add_divider(menu);

	menu_item_add_radio(menu, _("Scroll to top left corner"),
	                    GUINT_TO_POINTER(ScrollReset::TOPLEFT),
	                    options->image.scroll_reset_method == ScrollReset::TOPLEFT,
	                    G_CALLBACK(layout_scroll_menu_cb),
	                    GUINT_TO_POINTER(ScrollReset::TOPLEFT));
	menu_item_add_radio(menu, _("Scroll to image center"),
	                    GUINT_TO_POINTER(ScrollReset::CENTER),
	                    options->image.scroll_reset_method == ScrollReset::CENTER,
	                    G_CALLBACK(layout_scroll_menu_cb),
	                    GUINT_TO_POINTER(ScrollReset::CENTER));
	menu_item_add_radio(menu, _("Keep the region from previous image"),
	                    GUINT_TO_POINTER(ScrollReset::NOCHANGE),
	                    options->image.scroll_reset_method == ScrollReset::NOCHANGE,
	                    G_CALLBACK(layout_scroll_menu_cb),
	                    GUINT_TO_POINTER(ScrollReset::NOCHANGE));

	g_signal_connect(G_OBJECT(menu), "selection_done",
			 G_CALLBACK(layout_zoom_menu_hide_cb), NULL);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static GtkWidget *layout_zoom_button(LayoutWindow *lw, GtkWidget *box, gint size, gboolean)
{
	GtkWidget *button;
	GtkWidget *frame;
	GtkWidget *image;

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	if (size) gtk_widget_set_size_request(frame, size, -1);
	gq_gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);

	gq_gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);

	gtk_widget_show(frame);

	image = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN, GTK_ICON_SIZE_BUTTON);
	button = gtk_button_new_with_label("1:1");
	gtk_button_set_image(GTK_BUTTON(button), image);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(layout_zoom_button_press_cb), lw);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_button_set_image_position(GTK_BUTTON(button), GTK_POS_RIGHT);

	gq_gtk_container_add(GTK_WIDGET(frame), button);
	gtk_widget_show(button);

	return button;
}

/*
 *-----------------------------------------------------------------------------
 * status bar
 *-----------------------------------------------------------------------------
 */


void layout_status_update_progress(LayoutWindow *lw, gdouble val, const gchar *text)
{
	static gdouble meta = 0;

	if (!layout_valid(&lw)) return;
	if (!lw->info_progress_bar) return;

	/* Give priority to the loading meta data message
	 */
	if(!g_strcmp0(text, "Loading thumbs..."))
		{
		if (meta)
			{
			return;
			}
		}
	else
		{
		meta = val;
		}

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(lw->info_progress_bar), val);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(lw->info_progress_bar),
									val ? ((text) ? text : " ") : " ");
}

void layout_status_update_info(LayoutWindow *lw, const gchar *text)
{
	gchar *buf = nullptr;

	if (!layout_valid(&lw)) return;

	if (!text)
		{
		guint n;
		gint64 n_bytes = 0;

		n = layout_list_count(lw, &n_bytes);

		if (n)
			{
			guint s;
			gint64 s_bytes = 0;
			gchar *ss;

				{
				ss = g_strdup("");
				}

			s = layout_selection_count(lw, &s_bytes);

			g_autofree gchar *b = text_from_size_abrev(n_bytes);

			if (s > 0)
				{
				g_autofree gchar *sb = text_from_size_abrev(s_bytes);
				buf = g_strdup_printf(_("%s, %d files (%s, %d)%s"), b, n, sb, s, ss);
				}
			else
				{
				buf = g_strdup_printf(_("%s, %d files%s"), b, n, ss);
				}

			g_free(ss);

			text = buf;

			image_osd_update(lw->image);
			}
		else
			{
			text = "";
			}
		}

	if (lw->info_status) gtk_label_set_text(GTK_LABEL(lw->info_status), text);
	g_free(buf);
}

void layout_status_update_image(LayoutWindow *lw)
{
	FileData *fd;
	gint page_total;
	gint page_num;

	if (!layout_valid(&lw) || !lw->image) return;
	if (!lw->info_zoom || !lw->info_details) return;

	if (!lw->image->image_fd)
		{
		gtk_button_set_label(GTK_BUTTON(lw->info_zoom), "");
		gtk_label_set_text(GTK_LABEL(lw->info_details), "");
		}
	else
		{
		gchar *text;
		gchar *b;

		text = image_zoom_get_as_text(lw->image);
		gtk_button_set_label(GTK_BUTTON(lw->info_zoom), text);
		g_free(text);

		b = image_get_fd(lw->image) ? text_from_size(image_get_fd(lw->image)->size) : g_strdup("0");

		if (lw->image->unknown)
			{
			const gchar *filename = image_get_path(lw->image);
			if (filename && !access_file(filename, R_OK))
				{
				text = g_strdup_printf(_("(no read permission) %s bytes"), b);
				}
			else
				{
				text = g_strdup_printf(_("( ? x ? ) %s bytes"), b);
				}
			}
		else
			{
			gint width;
			gint height;
			fd = image_get_fd(lw->image);
			page_total = fd->page_total;
			page_num = fd->page_num + 1;
			image_get_image_size(lw->image, &width, &height);

			if (page_total > 1)
				{
				text = g_strdup_printf(_("( %d x %d ) %s bytes %s%d%s%d%s"), width, height, b, "[", page_num, "/", page_total, "]");
				}
			else
				{
				text = g_strdup_printf(_("( %d x %d ) %s bytes"), width, height, b);
				}
			}

		g_signal_emit_by_name (lw->image->pr, "update-pixel");

		g_free(b);

		gtk_label_set_text(GTK_LABEL(lw->info_details), text);
		g_free(text);
		}
	layout_util_sync_color(lw); /* update color button */
}

void layout_status_update_all(LayoutWindow *lw)
{
	layout_status_update_progress(lw, 0.0, nullptr);
	layout_status_update_info(lw, nullptr);
	layout_status_update_image(lw);
}

static GtkWidget *layout_status_label(const gchar *text, GtkWidget *box, gboolean start, gint size, gboolean expand)
{
	GtkWidget *label;
	GtkWidget *frame;

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	if (size) gtk_widget_set_size_request(frame, size, -1);
	gq_gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	if (start)
		{
		gq_gtk_box_pack_start(GTK_BOX(box), frame, expand, expand, 0);
		}
	else
		{
		gq_gtk_box_pack_end(GTK_BOX(box), frame, expand, expand, 0);
		}
	gtk_widget_show(frame);

	label = gtk_label_new(text ? text : "");
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gq_gtk_container_add(GTK_WIDGET(frame), label);
	gtk_widget_show(label);

	return label;
}

static void layout_status_setup(LayoutWindow *lw, GtkWidget *box, gboolean small_format)
{
	GtkWidget *hbox;

	if (lw->info_box) return;

	if (small_format)
		{
		lw->info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		DEBUG_NAME(lw->info_box);
		}
	else
		{
		lw->info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(lw->info_box);
		}
	gq_gtk_box_pack_end(GTK_BOX(box), lw->info_box, FALSE, FALSE, 0);
	gtk_widget_show(lw->info_box);

	if (small_format)
		{
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(hbox);
		gq_gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
		}
	else
		{
		hbox = lw->info_box;
		}
	lw->info_progress_bar = gtk_progress_bar_new();
	DEBUG_NAME(lw->info_progress_bar);
	gtk_widget_set_size_request(lw->info_progress_bar, PROGRESS_WIDTH, -1);

	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(lw->info_progress_bar), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(lw->info_progress_bar), TRUE);

	gq_gtk_box_pack_start(GTK_BOX(hbox), lw->info_progress_bar, FALSE, FALSE, 0);
	gtk_widget_show(lw->info_progress_bar);

	lw->info_sort = layout_sort_button(lw, hbox);
	gtk_widget_set_tooltip_text(GTK_WIDGET(lw->info_sort), _("Select sort order"));
	gtk_widget_show(lw->info_sort);

	lw->info_status = layout_status_label(nullptr, lw->info_box, TRUE, 0, (!small_format));
	DEBUG_NAME(lw->info_status);
	gtk_widget_set_tooltip_text(GTK_WIDGET(lw->info_status), _("Folder contents (files selected)\nSlideshow [time interval]"));

	if (small_format)
		{
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(hbox);
		gq_gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
		}
	lw->info_details = layout_status_label(nullptr, hbox, TRUE, 0, TRUE);
	DEBUG_NAME(lw->info_details);
	gtk_widget_set_tooltip_text(GTK_WIDGET(lw->info_details), _("(Image dimensions) Image size [page n of m]"));

	lw->info_zoom = layout_zoom_button(lw, hbox, ZOOM_LABEL_WIDTH, TRUE);
	gtk_widget_set_tooltip_text(GTK_WIDGET(lw->info_zoom), _("Select zoom and scroll mode"));
	gtk_widget_show(lw->info_zoom);

	if (small_format)
		{
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(hbox);
		gq_gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
		}
}

/*
 *-----------------------------------------------------------------------------
 * views
 *-----------------------------------------------------------------------------
 */

static GtkWidget *layout_tools_new(LayoutWindow *lw)
{
	lw->dir_view = layout_tool_setup(lw);
	return lw->dir_view;
}

static void layout_list_status_cb(ViewFile *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_status_update_info(lw, nullptr);
}

static void layout_list_thumb_cb(ViewFile *, gdouble val, const gchar *text, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_status_update_progress(lw, val, text);
}

static void layout_list_sync_file_filter(LayoutWindow *lw)
{
	if (lw->vf) vf_file_filter_set(lw->vf, lw->options.show_file_filter);
}

static GtkWidget *layout_list_new(LayoutWindow *lw)
{
	lw->vf = vf_new(nullptr);
	vf_set_layout(lw->vf, lw);

	vf_set_status_func(lw->vf, layout_list_status_cb, lw);
	vf_set_thumb_status_func(lw->vf, layout_list_thumb_cb, lw);

	layout_list_sync_file_filter(lw);

	return lw->vf->widget;
}

static void layout_list_scroll_to_subpart(LayoutWindow *lw, const gchar *)
{
	if (!lw) return;
}

GList *layout_list(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	if (lw->vf) return vf_get_list(lw->vf);

	return nullptr;
}

guint layout_list_count(LayoutWindow *lw, gint64 *bytes)
{
	if (!layout_valid(&lw)) return 0;

	if (lw->vf) return vf_count(lw->vf, bytes);

	return 0;
}

FileData *layout_list_get_fd(LayoutWindow *lw, gint index)
{
	if (!layout_valid(&lw)) return nullptr;

	if (lw->vf) return vf_index_get_data(lw->vf, index);

	return nullptr;
}

gint layout_list_get_index(LayoutWindow *lw, FileData *fd)
{
	if (!layout_valid(&lw) || !fd) return -1;

	if (lw->vf) return vf_index_by_fd(lw->vf, fd);

	return -1;
}

void layout_list_sync_fd(LayoutWindow *lw, FileData *fd)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_by_fd(lw->vf, fd);
}

static void layout_list_sync_sort(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_sort_set(lw->vf, lw->options.file_view_list_sort.method, lw->options.file_view_list_sort.ascend, lw->options.file_view_list_sort.case_sensitive);
}

GList *layout_selection_list(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	if (lw->vf) return vf_selection_get_list(lw->vf);

	return nullptr;
}

GList *layout_selection_list_by_index(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	if (lw->vf) return vf_selection_get_list_by_index(lw->vf);

	return nullptr;
}

guint layout_selection_count(LayoutWindow *lw, gint64 *bytes)
{
	if (!layout_valid(&lw)) return 0;

	if (lw->vf) return vf_selection_count(lw->vf, bytes);

	return 0;
}

void layout_select_all(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_all(lw->vf);
}

void layout_select_none(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_none(lw->vf);
}

void layout_select_invert(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_invert(lw->vf);
}

/*
 *-----------------------------------------------------------------------------
 * access
 *-----------------------------------------------------------------------------
 */

const gchar *layout_get_path(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;
	return lw->dir_fd ? lw->dir_fd->path : nullptr;
}

static void layout_sync_path(LayoutWindow *lw)
{
	if (!lw->dir_fd) return;

	if (lw->path_entry) gq_gtk_entry_set_text(GTK_ENTRY(lw->path_entry), lw->dir_fd->path);

	if (lw->vd) vd_set_fd(lw->vd, lw->dir_fd);
	if (lw->vf) vf_set_fd(lw->vf, lw->dir_fd);
}

gboolean layout_set_path(LayoutWindow *lw, const gchar *path)
{
	FileData *fd;
	gboolean ret;

	if (!path) return FALSE;

	fd = file_data_new_group(path);
	ret = layout_set_fd(lw, fd);
	file_data_unref(fd);
	return ret;
}


gboolean layout_set_fd(LayoutWindow *lw, FileData *fd)
{
	gboolean have_file = FALSE;

	if (!layout_valid(&lw)) return FALSE;

	if (!fd || !isname(fd->path)) return FALSE;
	if (lw->dir_fd && fd == lw->dir_fd)
		{
		return TRUE;
		}

	/* remember current image for this directory (session-only) */
	if (lw->dir_fd && image_get_fd(lw->image))
		{
		history_list_add_to_key("image_list", image_get_fd(lw->image)->path, 20);
		}

	if (isdir(fd->path))
		{
		if (lw->dir_fd)
			{
			file_data_unregister_real_time_monitor(lw->dir_fd);
			file_data_unref(lw->dir_fd);
			}
		lw->dir_fd = file_data_ref(fd);
		file_data_register_real_time_monitor(fd);

		gchar *last_image = get_recent_viewed_folder_image(fd->path);
		if (last_image)
			{
			fd = file_data_new_group(last_image);
			g_free(last_image);
			if (isfile(fd->path)) have_file = TRUE;
			}
		}
	else
		{
		gchar *base;

		base = remove_level_from_path(fd->path);
		if (lw->dir_fd && strcmp(lw->dir_fd->path, base) == 0)
			{
			g_free(base);
			}
		else if (isdir(base))
			{
			if (lw->dir_fd)
				{
				file_data_unregister_real_time_monitor(lw->dir_fd);
				file_data_unref(lw->dir_fd);
				}
			lw->dir_fd = file_data_new_dir(base);
			file_data_register_real_time_monitor(lw->dir_fd);
			g_free(base);
			}
		else
			{
			g_free(base);
			return FALSE;
			}
		if (isfile(fd->path)) have_file = TRUE;
		}

	if (lw->path_entry)
		{
		history_chain_append_end(lw->dir_fd->path);
		tab_completion_append_to_history(lw->path_entry, lw->dir_fd->path);
		}
	layout_sync_path(lw);
	layout_list_sync_sort(lw);

	if (have_file)
		{
		gint row;

		row = layout_list_get_index(lw, fd);
		if (row >= 0)
			{
			layout_image_set_index(lw, row);
			}
		else
			{
			layout_image_set_fd(lw, fd);
			}
		}
	else if (!options->lazy_image_sync)
		{
		gint count = layout_list_count(lw, nullptr);
		layout_image_set_index(lw, count > 0 ? count - 1 : 0);
		}

	if (lw->vf && (lw->options.file_view_list_sort.method == SORT_EXIFTIME || lw->options.file_view_list_sort.method == SORT_EXIFTIMEDIGITIZED))
		{
		vf_read_metadata_in_idle(lw->vf);
		}

	return TRUE;
}

static void layout_refresh_lists(LayoutWindow *lw)
{
	if (lw->vd) vd_refresh(lw->vd);

	if (lw->vf)
		{
		vf_refresh(lw->vf);
		vf_thumb_update(lw->vf);
		}
}

void layout_refresh(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	DEBUG_1("layout refresh");

	layout_refresh_lists(lw);

	if (lw->image) layout_image_refresh(lw);
}

void layout_file_filter_set(LayoutWindow *lw, gboolean enable)
{
	if (!layout_valid(&lw)) return;

	if (lw->options.show_file_filter == enable) return;

	lw->options.show_file_filter = enable;

	layout_util_sync_file_filter(lw);
	layout_list_sync_file_filter(lw);
}

void layout_sort_set_files(LayoutWindow *lw, SortType type, gboolean ascend, gboolean case_sensitive)
{
	if (!layout_valid(&lw)) return;
	if (lw->options.file_view_list_sort.method == type && lw->options.file_view_list_sort.ascend == ascend && lw->options.file_view_list_sort.case_sensitive == case_sensitive) return;

	lw->options.file_view_list_sort.method = type;
	lw->options.file_view_list_sort.ascend = ascend;
	lw->options.file_view_list_sort.case_sensitive = case_sensitive;

	if (lw->info_sort) gtk_button_set_label(GTK_BUTTON(lw->info_sort), sort_type_get_text(type));
	layout_list_sync_sort(lw);
}

gboolean layout_sort_get(LayoutWindow *lw, SortType *type, gboolean *ascend, gboolean *case_sensitive)
{
	if (!layout_valid(&lw)) return FALSE;

	if (type) *type = lw->options.file_view_list_sort.method;
	if (ascend) *ascend = lw->options.file_view_list_sort.ascend;
	if (case_sensitive) *case_sensitive = lw->options.file_view_list_sort.case_sensitive;

	return TRUE;
}

static gboolean layout_geometry_get(LayoutWindow *lw, GdkRectangle &rect)
{
	GdkWindow *window;
	if (!layout_valid(&lw)) return FALSE;

	window = gtk_widget_get_window(lw->window);
	rect = window_get_root_origin_geometry(window);

	return TRUE;
}

gboolean layout_geometry_get_dividers(LayoutWindow *lw, gint *h, gint *v)
{
	GtkAllocation h_allocation;
	GtkAllocation v_allocation;

	if (!layout_valid(&lw)) return FALSE;

	if (lw->h_pane)
		{
		GtkWidget *child = gtk_paned_get_child1(GTK_PANED(lw->h_pane));
		gtk_widget_get_allocation(child, &h_allocation);
		}

	if (lw->v_pane)
		{
		GtkWidget *child = gtk_paned_get_child1(GTK_PANED(lw->v_pane));
		gtk_widget_get_allocation(child, &v_allocation);
		}

	if (lw->h_pane && h_allocation.x >= 0)
		{
		*h = h_allocation.width;
		}
	else if (h != &lw->options.main_window.hdivider_pos)
		{
		*h = lw->options.main_window.hdivider_pos;
		}

	if (lw->v_pane && v_allocation.x >= 0)
		{
		*v = v_allocation.height;
		}
	else if (v != &lw->options.main_window.vdivider_pos)
		{
		*v = lw->options.main_window.vdivider_pos;
		}

	return TRUE;
}

void layout_views_set_sort_dir(LayoutWindow *lw, SortType method, gboolean ascend, gboolean case_sensitive)
{
	if (!layout_valid(&lw)) return;

	if (lw->options.dir_view_list_sort.method == method && lw->options.dir_view_list_sort.ascend == ascend && lw->options.dir_view_list_sort.case_sensitive == case_sensitive) return;

	lw->options.dir_view_list_sort.method = method;
	lw->options.dir_view_list_sort.ascend = ascend;
	lw->options.dir_view_list_sort.case_sensitive = case_sensitive;
}

static gboolean layout_geometry_get_log_window(LayoutWindow *lw, GdkRectangle &log_window)
{
	GdkWindow *window;

	if (!layout_valid(&lw)) return FALSE;

	if (!lw->log_window)
		{
		return FALSE;
		}

	window = gtk_widget_get_window(lw->log_window);
	log_window = window_get_root_origin_geometry(window);

	return TRUE;
}

static void layout_grid_setup(LayoutWindow *lw)
{
	GtkWidget *tools;
	GtkWidget *files;

	layout_actions_setup(lw);

	lw->group_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(lw->group_box);
		{
		gq_gtk_box_pack_end(GTK_BOX(lw->main_box), lw->group_box, TRUE, TRUE, 0);
		}
	gtk_widget_show(lw->group_box);

	/* Create image widget for fullscreen; not shown in the main layout. */
	if (!lw->image)
		{
		layout_image_init(lw);
		}

	tools = layout_tools_new(lw);
	DEBUG_NAME(tools);
	files = layout_list_new(lw);
	DEBUG_NAME(files);

	layout_status_setup(lw, lw->group_box, FALSE);

	/* Hardcoded layout: tools left, files right */
	lw->h_pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	DEBUG_NAME(lw->h_pane);
	lw->v_pane = nullptr;

	gq_gtk_box_pack_start(GTK_BOX(lw->group_box), lw->h_pane, TRUE, TRUE, 0);

	gtk_paned_pack1(GTK_PANED(lw->h_pane), tools, FALSE, TRUE);
	gtk_paned_pack2(GTK_PANED(lw->h_pane), files, TRUE, TRUE);

	gtk_widget_show(tools);
	gtk_widget_show(files);
	gtk_widget_show(lw->h_pane);

	gtk_paned_set_position(GTK_PANED(lw->h_pane), lw->options.main_window.hdivider_pos);

	image_grab_focus(lw->image);
}

/*
 *-----------------------------------------------------------------------------
 * base
 *-----------------------------------------------------------------------------
 */

void layout_sync_options_with_current_state(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	lw->options.main_window.maximized =  window_maximized(lw->window);
	if (!lw->options.main_window.maximized)
		{
		layout_geometry_get(lw, lw->options.main_window.rect);
		}

	layout_geometry_get_dividers(lw, &lw->options.main_window.hdivider_pos, &lw->options.main_window.vdivider_pos);

	lw->options.image_overlay.state = image_osd_get(lw->image);

	layout_geometry_get_log_window(lw, lw->options.log_window);
}

LayoutWindow *layout_new_with_geometry(FileData *dir_fd, LayoutOptions *lop,
				       const gchar *geometry)
{
	LayoutWindow *lw;
	GdkGeometry hint;

	DEBUG_1("%s layout_new: start", get_exec_time());
	lw = g_new0(LayoutWindow, 1);

	if (lop)
		copy_layout_options(&lw->options, lop);
	else
		init_layout_options(&lw->options);

	/* window */

	lw->window = window_new(GQ_APPNAME_LC, nullptr, nullptr, nullptr);
	DEBUG_NAME(lw->window);
	gtk_window_set_resizable(GTK_WINDOW(lw->window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(lw->window), 0);

	hint.min_width = 32;
	hint.min_height = 32;
	hint.base_width = 0;
	hint.base_height = 0;
	gtk_window_set_geometry_hints(GTK_WINDOW(lw->window), nullptr, &hint,
				      static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE | GDK_HINT_USER_POS));

	gtk_window_set_default_size(GTK_WINDOW(lw->window), lw->options.main_window.rect.width, lw->options.main_window.rect.height);
	gq_gtk_window_move(GTK_WINDOW(lw->window), lw->options.main_window.rect.x, lw->options.main_window.rect.y);
	if (lw->options.main_window.maximized) gtk_window_maximize(GTK_WINDOW(lw->window));
	g_signal_connect(G_OBJECT(lw->window), "delete_event",
			 G_CALLBACK(exit_program), lw);

	lw->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(lw->main_box);
	gq_gtk_container_add(GTK_WIDGET(lw->window), lw->main_box);
	gtk_widget_show(lw->main_box);

	layout_grid_setup(lw);

	layout_util_sync(lw);
	layout_status_update_all(lw);

	if (dir_fd)
		{
		layout_set_fd(lw, dir_fd);
		}
	else
		{
		GdkPixbuf *pixbuf;

		pixbuf = pixbuf_inline(PIXBUF_INLINE_LOGO);

		/** @FIXME the zoom value set here is the value, which is then copied again and again
		   in 'Leave Zoom at previous setting' mode. This is not ideal.  */
		image_change_pixbuf(lw->image, pixbuf, 0.0, FALSE);
		g_object_unref(pixbuf);
		}

	if (geometry)
		{
		if (!gtk_window_parse_geometry(GTK_WINDOW(lw->window), geometry))
			{
			log_printf("%s", _("Invalid geometry\n"));
			}
		}

	gtk_widget_show(lw->window);

	image_osd_set(lw->image, static_cast<OsdShowFlags>(lw->options.image_overlay.state));

	main_lw = lw;

	file_data_register_notify_func(layout_image_notify_cb, lw, NOTIFY_PRIORITY_LOW);

	DEBUG_1("%s layout_new: end", get_exec_time());

	return lw;
}

void layout_write_attributes(LayoutOptions *layout, GString *outstr, gint indent)
{
	WRITE_NL(); WRITE_UINT(*layout, file_view_list_sort.method);
	WRITE_NL(); WRITE_BOOL(*layout, file_view_list_sort.ascend);
	WRITE_NL(); WRITE_BOOL(*layout, file_view_list_sort.case_sensitive);

	WRITE_NL(); WRITE_UINT(*layout, dir_view_list_sort.method);
	WRITE_NL(); WRITE_BOOL(*layout, dir_view_list_sort.ascend);
	WRITE_NL(); WRITE_BOOL(*layout, dir_view_list_sort.case_sensitive);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_INT_FULL("main_window.x", layout->main_window.rect.x);
	WRITE_NL(); WRITE_INT_FULL("main_window.y", layout->main_window.rect.y);
	WRITE_NL(); WRITE_INT_FULL("main_window.w", layout->main_window.rect.width);
	WRITE_NL(); WRITE_INT_FULL("main_window.h", layout->main_window.rect.height);
	WRITE_NL(); WRITE_BOOL(*layout, main_window.maximized);
	WRITE_NL(); WRITE_INT(*layout, main_window.hdivider_pos);
	WRITE_NL(); WRITE_INT(*layout, main_window.vdivider_pos);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_UINT(*layout, image_overlay.state);

	WRITE_NL(); WRITE_INT(*layout, log_window.x);
	WRITE_NL(); WRITE_INT(*layout, log_window.y);
	WRITE_NL(); WRITE_INT(*layout, log_window.width);
	WRITE_NL(); WRITE_INT(*layout, log_window.height);

	WRITE_NL(); WRITE_INT_FULL("preferences_window.x", layout->preferences_window.rect.x);
	WRITE_NL(); WRITE_INT_FULL("preferences_window.y", layout->preferences_window.rect.y);
	WRITE_NL(); WRITE_INT_FULL("preferences_window.w", layout->preferences_window.rect.width);
	WRITE_NL(); WRITE_INT_FULL("preferences_window.h", layout->preferences_window.rect.height);
	WRITE_NL(); WRITE_INT(*layout, preferences_window.page_number);

	WRITE_NL(); WRITE_INT(*layout, search_window.x);
	WRITE_NL(); WRITE_INT(*layout, search_window.y);
	WRITE_NL(); WRITE_INT_FULL("search_window.w", layout->search_window.width);
	WRITE_NL(); WRITE_INT_FULL("search_window.h", layout->search_window.height);

	WRITE_NL(); WRITE_INT(*layout, dupe_window.x);
	WRITE_NL(); WRITE_INT(*layout, dupe_window.y);
	WRITE_NL(); WRITE_INT_FULL("dupe_window.w", layout->dupe_window.width);
	WRITE_NL(); WRITE_INT_FULL("dupe_window.h", layout->dupe_window.height);

	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(*layout, animate);
}


void layout_write_config(LayoutWindow *lw, GString *outstr, gint indent)
{
	layout_sync_options_with_current_state(lw);
	WRITE_NL(); WRITE_STRING("<layout");
	layout_write_attributes(&lw->options, outstr, indent + 1);
	WRITE_STRING(">");

	WRITE_SEPARATOR();
	generic_dialog_windows_write_config(outstr, indent + 1);

	WRITE_NL(); WRITE_STRING("</layout>");
}

void layout_load_attributes(LayoutOptions *layout, const gchar **attribute_names, const gchar **attribute_values)
{
	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_UINT_ENUM(*layout, file_view_list_sort.method)) continue;
		if (READ_BOOL(*layout, file_view_list_sort.ascend)) continue;
		if (READ_BOOL(*layout, file_view_list_sort.case_sensitive)) continue;
		if (READ_UINT_ENUM(*layout, dir_view_list_sort.method)) continue;
		if (READ_BOOL(*layout, dir_view_list_sort.ascend)) continue;
		if (READ_BOOL(*layout, dir_view_list_sort.case_sensitive)) continue;

		/* window positions */

		if (READ_INT_FULL("main_window.x", layout->main_window.rect.x)) continue;
		if (READ_INT_FULL("main_window.y", layout->main_window.rect.y)) continue;
		if (READ_INT_FULL("main_window.w", layout->main_window.rect.width)) continue;
		if (READ_INT_FULL("main_window.h", layout->main_window.rect.height)) continue;
		if (READ_BOOL(*layout, main_window.maximized)) continue;
		if (READ_INT(*layout, main_window.hdivider_pos)) continue;
		if (READ_INT(*layout, main_window.vdivider_pos)) continue;

		if (READ_UINT(*layout, image_overlay.state)) continue;

		if (READ_INT(*layout, log_window.x)) continue;
		if (READ_INT(*layout, log_window.y)) continue;
		if (READ_INT(*layout, log_window.width)) continue;
		if (READ_INT(*layout, log_window.height)) continue;

		if (READ_INT_FULL("preferences_window.x", layout->preferences_window.rect.x)) continue;
		if (READ_INT_FULL("preferences_window.y", layout->preferences_window.rect.y)) continue;
		if (READ_INT_FULL("preferences_window.w", layout->preferences_window.rect.width)) continue;
		if (READ_INT_FULL("preferences_window.h", layout->preferences_window.rect.height)) continue;
		if (READ_INT(*layout, preferences_window.page_number)) continue;

		if (READ_INT(*layout, search_window.x)) continue;
		if (READ_INT(*layout, search_window.y)) continue;
		if (READ_INT_FULL("search_window.w", layout->search_window.width)) continue;
		if (READ_INT_FULL("search_window.h", layout->search_window.height)) continue;

		if (READ_INT(*layout, dupe_window.x)) continue;
		if (READ_INT(*layout, dupe_window.y)) continue;
		if (READ_INT_FULL("dupe_window.w", layout->dupe_window.width)) continue;
		if (READ_INT_FULL("dupe_window.h", layout->dupe_window.height)) continue;

		if (READ_BOOL(*layout, animate)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}
}

static void layout_config_commandline(gchar **path)
{
	if (command_line->file)
		{
		*path = g_strdup(command_line->file);
		}
	else if (command_line->path)
		{
		*path = g_strdup(command_line->path);
		}
	else
		{
		*path = get_current_dir();
		}
}

static gboolean first_found = FALSE;

LayoutWindow *layout_new_from_config(const gchar **attribute_names, const gchar **attribute_values, gboolean use_commandline)
{
	LayoutOptions lop;
	LayoutWindow *lw;
	gchar *path = nullptr;

	init_layout_options(&lop);

	if (attribute_names) layout_load_attributes(&lop, attribute_names, attribute_values);

	if (use_commandline && !first_found)
		{
		first_found = TRUE;
		layout_config_commandline(&path);
		}
	else
		{
		path = get_current_dir();
		}

	lw = layout_new_with_geometry(nullptr, &lop, use_commandline ? command_line->geometry : nullptr);
	layout_sort_set_files(lw, lw->options.file_view_list_sort.method, lw->options.file_view_list_sort.ascend, lw->options.file_view_list_sort.case_sensitive);


	layout_set_path(lw, path);

	if (use_commandline && command_line->startup_full_screen) layout_image_full_screen_start(lw);
	if (use_commandline && command_line->log_window_show) log_window_new(lw);

	g_free(path);
	return lw;
}

void layout_update_from_config(LayoutWindow *, const gchar **attribute_names, const gchar **attribute_values)
{
	LayoutOptions lop;

	init_layout_options(&lop);

	if (attribute_names) layout_load_attributes(&lop, attribute_names, attribute_values);
}

LayoutWindow *layout_new_from_default()
{
	gboolean success;
	gchar *default_path;

	default_path = g_build_filename(get_rc_dir(), DEFAULT_WINDOW_LAYOUT, NULL);
	success = load_config_from_file(default_path, TRUE);
	g_free(default_path);

	if (!success)
		{
		main_lw = layout_new_from_config(nullptr, nullptr, TRUE);
		}

	return main_lw;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */

