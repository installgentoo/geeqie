/*
 * Copyright (C) 2004 John Ellis
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

#ifndef DUPE_H
#define DUPE_H

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <gtk/gtk.h>

class FileData;
struct ImageSimilarityData;
struct ThumbLoader;

/** @enum DupeMatchType
 *  match methods
 */
enum DupeMatchType
{
	DUPE_MATCH_NONE = 0,
	DUPE_MATCH_NAME = 1 << 0,
	DUPE_MATCH_SUM  = 1 << 1,	/**< MD5sum */
	DUPE_MATCH_SIM_HIGH = 1 << 2,	/**< similarity */
	DUPE_MATCH_SIM_MED  = 1 << 3,
	DUPE_MATCH_SIM_LOW  = 1 << 4,
	DUPE_MATCH_SIM_CUSTOM = 1 << 5,
	DUPE_MATCH_NAME_LOOSE = 1 << 6,	/**< name ignoring case and extension; the highest value, rcfile.cc clamps to it */
};

enum DupeSelectType
{
	DUPE_SELECT_NONE,
	DUPE_SELECT_GROUP1,
	DUPE_SELECT_GROUP2
};

struct DupeItem
{
	FileData *fd;

	gchar *md5sum;
	gint width;
	gint height;

	ImageSimilarityData *simd;

	GdkPixbuf *pixbuf; /**< thumb */

	GList *group;		/**< List of match data (#DupeMatch) */
	gdouble group_rank;	/**< (sum of all child ranks) / n */

	gint second;
};

struct DupeMatch
{
	DupeItem *di;
	gdouble rank;
};

struct DupeWindow
{
	GList *list;	/**< one entry for each dropped file in 1st set window (#DupeItem) */
	GList *dupes;			/**< list of dupes (#DupeItem, grouping the #DupeMatch-es) */
	DupeMatchType match_mask;	/**< mask of things to check for match */

	GtkWidget *window;
	GtkWidget *table;
	GtkWidget *listview;
	GtkWidget *combo;
	GtkWidget *status_label;
	GtkWidget *extra_label; /**< Progress bar widget */
	GtkWidget *button_thumbs;
	GtkWidget *button_rotation_invariant;
	GtkWidget *button_alternate_algorithm;
	GtkWidget *button_grayscale;
	GtkWidget *custom_threshold;
	GList *add_files_queue;
	guint add_files_queue_id;
	GHashTable *list_cache; /**< Caches the #DupeItem-s of all items in list. Used when ensuring #FileData-s are unique */
	GHashTable *second_list_cache; /**< Caches the #DupeItem-s of all items in second_list. Used when ensuring #FileData-s are unique */
	GtkWidget *controls_box;

	gboolean show_thumbs;

	guint idle_id; /**< event source id */
	GList *working;
	gint setup_done; /**< Boolean. Set TRUE when all checksums/similarity data have been read or created */
	gint setup_count; /**< length of set1 or if 2 sets, total length of both */
	gint setup_n;			/**< Set to zero on start/reset. These are merely for speed optimization */
	GList *setup_point;		/**< these are merely for speed optimization */
	DupeMatchType setup_mask;	/**< these are merely for speed optimization */
	guint64 setup_time; /**< Time in µsec since Epoch, restored at each phase of operation */
	guint64 setup_time_count; /**< Time in µsec since time-to-go status display was updated */

	DupeItem *click_item;		/**< for popup menu */

	ThumbLoader *thumb_loader;
	DupeItem *thumb_item;

	GtkTreeSortable *sortable;
	gint set_count; /**< Index/counter for number of duplicate sets found */

	/* second set comparison stuff */

	gboolean second_set;		/**< second set enabled ? */
	GList *second_list;		/**< second set dropped files */
	gboolean second_drop;		/**< drop is on second set */

	GtkWidget *second_vbox;		/**< box of second widgets */
	GtkWidget *second_listview;
	GtkWidget *second_status_label;

	gboolean color_frozen;

	/* required for similarity threads */
	GThreadPool *dupe_comparison_thread_pool;
	GList *search_matches; /**< List of similarity matches (#DupeSearchMatch) */
	GMutex search_matches_mutex;
	GList *search_matches_sorted; /**< \a search_matches sorted by order of entry into thread pool */
	gint queue_count; /**< Incremented each time an item is pushed onto the similarity thread pool */
	gint thread_count; /**< Incremented each time a similarity check thread item is completed */
	GMutex thread_count_mutex;
	gboolean abort; /**< Stop the similarity check thread queue */

	GList *sim_loads; /**< #DupeSimLoad in flight; \a setup_point is the next item not yet started */
};


DupeWindow *dupe_window_new();

void dupe_window_clear(DupeWindow *dw);
void dupe_window_close(DupeWindow *dw);

void dupe_window_add_files(DupeWindow *dw, GList *list, gboolean recurse);

#endif
