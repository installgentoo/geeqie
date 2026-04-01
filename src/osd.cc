/*
 * Copyright (C) 2018 The Geeqie Team
 *
 * Author: Colin Clark
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

/* Routines for creating the Overlay Screen Display text. Also
 * used for the same purposes by the Print routines
 */

#include "osd.h"

#include <cmath>
#include <string>
#include <string_view>
#include <unordered_map>

#include <gdk/gdk.h>
#include <glib-object.h>

#include <config.h>

#include "compat.h"
#include "dnd.h"
#include "exif.h"
#include "filedata.h"
#include "image-load.h"
#include "image.h"
#include "intl.h"
#include "layout.h"
#include "typedefs.h"
#include "ui-misc.h"
#include "ui-fileops.h"

#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>

namespace {

struct TagData
{
	gchar *key;
	gchar *title;
};

const gchar *predefined_tags[][2] = {
	{"%name%",							N_("Name")},
	{"%path:60%",						N_("Path")},
	{"%date%",							N_("Date")},
	{"%size%",							N_("Size")},
	{"%dimensions%",					N_("Dimensions")},
	{"%number%",						N_("Image index")},
	{"%total%",							N_("Images total")},
	{"%file.link%",						N_("File link")},
	{"%file.ctime%",					N_("File ctime")},
	{"%file.mode%",						N_("File mode")},
	{"%file.owner%",					N_("File owner")},
	{"%file.page_no%",					N_("File page no.")},
	{"%exif%",							N_("EXIF data")},
	{"%xmp%",							N_("XMP / IPTC data")},
	{"%metadata%",						N_("All EXIF / XMP / IPTC data")},
	{"%zoom%",							N_("Zoom")},
	{nullptr, nullptr}};

constexpr std::array<GtkTargetEntry, 1> osd_drag_types{{
	{ const_cast<gchar *>("text/plain"), GTK_TARGET_SAME_APP, TARGET_TEXT_PLAIN }
}};

struct OsdValueCache
{
	std::unordered_map<std::string, std::string> values;
	bool list_info_loaded = false;
	bool exif_loaded = false;
};

static void osd_load_list_info(OsdValueCache &cache)
{
	if (cache.list_info_loaded) return;

	gint n;
	gint t;

	if (main_lw)
		{
		t = layout_list_count(main_lw, nullptr);
		n = layout_list_get_index(main_lw, image_get_fd(main_lw->image)) + 1;
		}
	else
		{
		t = 1;
		n = 1;
		}

	if (n < 1) n = 1;
	if (t < 1) t = 1;

	cache.values["number"] = std::to_string(n);
	cache.values["total"] = std::to_string(t);
	cache.list_info_loaded = true;
}

static void osd_load_exif_data(FileData *fd, OsdValueCache &cache)
{
	if (cache.exif_loaded || !fd) return;

	ExifData *exif = exif_read_fd(fd);
	if (exif)
		{
		g_autofree gchar *exif_text = exif_get_all_exif_as_text(exif);
		g_autofree gchar *xmp_text = exif_get_all_xmp_as_text(exif);
		g_autofree gchar *metadata_text = exif_get_all_metadata_as_text(exif);
		cache.values["exif"] = exif_text ? exif_text : "";
		cache.values["xmp"] = xmp_text ? xmp_text : "";
		cache.values["metadata"] = metadata_text ? metadata_text : "";
		exif_free_fd(fd, exif);
		}

	cache.exif_loaded = true;
}

static std::string osd_mode_to_text(mode_t mode)
{
	static constexpr char rwx[] = {'r', 'w', 'x'};
	std::string out(9, '-');

	for (int i = 0; i < 9; i++)
		{
		if (mode & (1 << (8 - i))) out[i] = rwx[i % 3];
		}

	return out;
}

static std::string osd_read_link_target(const gchar *path)
{
	if (!path || !islink(path)) return "";

	struct stat st{};
	if (lstat(path, &st) != 0 || st.st_size <= 0) return "";

	std::string target(static_cast<size_t>(st.st_size), '\0');
	const ssize_t len = readlink(path, target.data(), target.size());
	if (len <= 0) return "";
	target.resize(static_cast<size_t>(len));
	return target;
}

static void osd_load_file_tag(const std::string &tag, FileData *fd, std::string &value)
{
	if (!fd) return;

	if (tag == "file.ctime")
		{
		value = FileData::text_from_time(fd->cdate);
		}
	else if (tag == "file.mode")
		{
		value = osd_mode_to_text(fd->mode);
		}
	else if (tag == "file.owner")
		{
		struct stat st{};
		if (stat(fd->path, &st) == 0)
			{
			std::string user, group;
			if (struct passwd *pw = getpwuid(st.st_uid); pw && pw->pw_name) user = pw->pw_name;
			else user = std::to_string(st.st_uid);
			if (struct group *gr = getgrgid(st.st_gid); gr && gr->gr_name) group = gr->gr_name;
			else group = std::to_string(st.st_gid);
			value = user + "/" + group;
			}
		}
	else if (tag == "file.link")
		{
		value = osd_read_link_target(fd->path);
		}
	else if (tag == "file.page_no")
		{
		if (fd->page_total > 1)
			{
			g_autofree gchar *page_no = g_strdup_printf("%d/%d", fd->page_num + 1, fd->page_total);
			value = page_no ? page_no : "";
			}
		else if (fd->page_num > 0) value = std::to_string(fd->page_num + 1);
		}
}

static const std::string &osd_lookup_value(const std::string &tag, ImageWindow *imd, OsdValueCache &cache)
{
	const auto it = cache.values.find(tag);
	if (it != cache.values.end()) return it->second;

	const auto empty_it = cache.values.emplace(tag, "").first;
	auto &value = empty_it->second;
	FileData *fd = image_get_fd(imd);

	if (tag == "number" || tag == "total")
		{
		osd_load_list_info(cache);
		return cache.values[tag];
		}

	if (tag == "name")
		{
		const gchar *name = image_get_name(imd);
		if (name) value = name;
		}
	else if (tag == "path")
		{
		const gchar *path = image_get_path(imd);
		if (path) value = path;
		}
	else if (tag == "date")
		{
		if (fd) value = text_from_time(fd->date);
		}
	else if (tag == "size")
		{
		if (fd)
			{
			g_autofree gchar *size = text_from_size_abrev(fd->size);
			if (size) value = size;
			}
		}
	else if (tag == "zoom")
		{
		g_autofree gchar *zoom = image_zoom_get_as_text(imd);
		if (zoom) value = zoom;
		}
	else if (tag == "dimensions")
		{
		if (!imd->unknown)
			{
			gint w;
			gint h;
			GdkPixbuf *load_pixbuf = image_loader_get_pixbuf(imd->il);

			if (imd->delay_flip &&
			    imd->il && load_pixbuf &&
			    image_get_pixbuf(imd) != load_pixbuf)
				{
				w = gdk_pixbuf_get_width(load_pixbuf);
				h = gdk_pixbuf_get_height(load_pixbuf);
				}
			else
				{
				image_get_image_size(imd, &w, &h);
				}

			g_autofree gchar *dimensions = g_strdup_printf("%d × %d", w, h);
			if (dimensions) value = dimensions;
			}
		}
	else if (tag == "exif" || tag == "xmp" || tag == "metadata")
		{
		osd_load_exif_data(fd, cache);
		return cache.values[tag];
		}
	else if (tag.rfind("file.", 0) == 0)
		{
		osd_load_file_tag(tag, fd, value);
		}

	return value;
}

} // namespace

static void tag_button_cb(GtkWidget *widget, gpointer data)
{
	auto image_overlay_template_view = static_cast<GtkTextView *>(data);
	GtkTextBuffer *buffer;
	TagData *td;

	buffer = gtk_text_view_get_buffer(image_overlay_template_view);
	td = static_cast<TagData *>(g_object_get_data(G_OBJECT(widget), "tag_data"));
	gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(buffer), td->key, -1);

	gtk_widget_grab_focus(GTK_WIDGET(image_overlay_template_view));
}

static void osd_dnd_get_cb(GtkWidget *btn, GdkDragContext *, GtkSelectionData *selection_data, guint, guint, gpointer data)
{
	TagData *td;
	auto image_overlay_template_view = static_cast<GtkTextView *>(data);

	td = static_cast<TagData *>(g_object_get_data(G_OBJECT(btn), "tag_data"));
	gtk_selection_data_set_text(selection_data, td->key, -1);

	gtk_widget_grab_focus(GTK_WIDGET(image_overlay_template_view));
}

static void tag_data_free(gpointer data)
{
	auto *td = static_cast<TagData *>(data);

	g_free(td->key);
	g_free(td->title);
	g_free(td);
}

static void set_osd_button(GtkGrid *grid, const gint rows, const gint cols, const gchar *key, const gchar *title, GtkWidget *template_view)
{
	GtkWidget *new_button;
	TagData *td;

	new_button = gtk_button_new_with_label(title);
	g_signal_connect(G_OBJECT(new_button), "clicked", G_CALLBACK(tag_button_cb), template_view);
	gtk_widget_show(new_button);

	td = g_new0(TagData, 1);
	td->key = g_strdup(key);
	td->title = g_strdup(title);

	g_object_set_data_full(G_OBJECT(new_button), "tag_data", td, tag_data_free);

	gtk_drag_source_set(new_button, GDK_BUTTON1_MASK, osd_drag_types.data(), osd_drag_types.size(), GDK_ACTION_COPY);
	g_signal_connect(G_OBJECT(new_button), "drag_data_get",
							G_CALLBACK(osd_dnd_get_cb), template_view);

	gtk_grid_attach(grid, new_button, cols, rows, 1, 1);

}

GtkWidget *osd_new(gint max_cols, GtkWidget *template_view)
{
	GtkWidget *vbox;
	gint i = 0;
	gint rows = 0;
	gint max_rows = 0;
	gint cols = 0;
	gdouble entries;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	pref_label_new(vbox, _("To include predefined tags in the template, click a button or drag-and-drop"));


	entries = ((gdouble)sizeof(predefined_tags) / sizeof(predefined_tags[0])) - 1;
	max_rows = ceil(entries / max_cols);

	GtkGrid *grid;
	grid = GTK_GRID(gtk_grid_new());
	gq_gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(grid), FALSE, FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(grid), PREF_PAD_BORDER);
	gtk_widget_show(GTK_WIDGET(grid));

	for (rows = 0; rows < max_rows; rows++)
		{
		cols = 0;

		while (cols < max_cols && predefined_tags[i][0])
			{
			set_osd_button(grid, rows, cols, predefined_tags[i][0], predefined_tags[i][1], template_view);
			i = i + 1;
			cols++;
			}
		}
	return vbox;
}

gchar *image_osd_mkinfo(const gchar *str, ImageWindow *imd)
{
	if (!str || !*str || !imd) return g_strdup("");

	std::string output;
	std::string_view template_text(str);
	gsize line_start = 0;
	OsdValueCache cache;

	while (line_start <= template_text.size())
		{
		const gsize line_end = template_text.find('\n', line_start);
		const std::string_view line = (line_end == std::string::npos)
					      ? template_text.substr(line_start)
					      : template_text.substr(line_start, line_end - line_start);
		std::string rendered_line;

		for (gsize i = 0; i < line.size();)
			{
			if (line[i] != '%')
				{
				rendered_line += line[i++];
				continue;
				}

			const gsize end = line.find('%', i + 1);
			if (end == std::string::npos)
				{
				rendered_line.append(line.substr(i));
				break;
				}

			const std::string token(line.substr(i + 1, end - i - 1));
			const gsize colon = token.find(':');
			std::string name = (colon == std::string::npos) ? token : token.substr(0, colon);
			guint limit = 0;

			if (colon != std::string::npos)
				{
				gsize cursor = colon + 1;
				const gsize digits_start = cursor;
				while (cursor < token.size() && std::isdigit(static_cast<unsigned char>(token[cursor])))
					{
					cursor++;
					}
				if (cursor > digits_start)
					{
					limit = static_cast<guint>(std::atoi(token.substr(digits_start, cursor - digits_start).c_str()));
					}
				}

			std::string data = osd_lookup_value(name, imd, cache);

			if (limit > 0 && data.size() > limit + 3)
				{
				data = data.substr(0, limit) + "...";
				}

			if (!data.empty())
				{
				g_autofree gchar *escaped = g_markup_escape_text(data.c_str(), -1);
				data = escaped ? escaped : "";
				rendered_line += data;
				}

			i = end + 1;
			}

		if (!rendered_line.empty())
			{
			if (!output.empty()) output += '\n';
			output += rendered_line;
			}

		if (line_end == std::string::npos) break;
		line_start = line_end + 1;
		}

	gchar *ret = g_strdup(output.c_str());
	return g_strchomp(ret);
}
