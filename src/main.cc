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

#include "main.h"

#include <sys/types.h>
#include <unistd.h>

#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <config.h>

#if HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#ifdef ENABLE_NLS
#  include <libintl.h>
#endif

#if HAVE_DEVELOPER
#include "third-party/backward.h"
#endif

#include "cache-maint.h"
#include "cache.h"
#include "compat.h"
#include "debug.h"
#include "editors.h"
#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
#include "history-list.h"
#include "image.h"
#include "intl.h"
#include "layout-image.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "rcfile.h"
#include "secure-save.h"
#include "third-party/whereami.h"
#include "thumb-standard.h"
#include "ui-fileops.h"

#if ENABLE_UNIT_TESTS
#  include "gtest/gtest.h"
#endif

gboolean thumb_format_changed = FALSE;

gchar *gq_localedir;
gchar *gq_appdir;
gchar *gq_bindir;
gchar *gq_executable_path;
gchar *desktop_file_template;

#if defined(SA_SIGINFO)
static void sig_handler_cb(int signo, siginfo_t *info, void *)
{
	gchar hex_char[16];
	const gchar *signal_name = nullptr;
	gint i = 0;
	guint64 addr;
	guint64 char_index;
	ssize_t len;
#if HAVE_EXECINFO_H
	gint bt_size;
	void *bt[1024];
#endif
	struct signals
		{
		gint sig_no;
		const gchar *sig_name;
		};
	struct signals signals_list[7];

	signals_list[0].sig_no = SIGABRT;
	signals_list[0].sig_name = "Abort";
	signals_list[1].sig_no = SIGBUS;
	signals_list[1].sig_name = "Bus error";
	signals_list[2].sig_no = SIGFPE;
	signals_list[2].sig_name = "Floating-point exception";
	signals_list[3].sig_no = SIGILL;
	signals_list[3].sig_name = "Illegal instruction";
	signals_list[4].sig_no = SIGIOT;
	signals_list[4].sig_name = "IOT trap";
	signals_list[5].sig_no = SIGSEGV;
	signals_list[5].sig_name = "Invalid memory reference";
	signals_list[6].sig_no = -1;
	signals_list[6].sig_name = "END";

	hex_char[0] = '0';
	hex_char[1] = '1';
	hex_char[2] = '2';
	hex_char[3] = '3';
	hex_char[4] = '4';
	hex_char[5] = '5';
	hex_char[6] = '6';
	hex_char[7] = '7';
	hex_char[8] = '8';
	hex_char[9] = '9';
	hex_char[10] = 'a';
	hex_char[11] = 'b';
	hex_char[12] = 'c';
	hex_char[13] = 'd';
	hex_char[14] = 'e';
	hex_char[15] = 'f';

	signal_name = "Unknown signal";
	while (signals_list[i].sig_no != -1)
		{
		if (signo == signals_list[i].sig_no)
			{
			signal_name = signals_list[i].sig_name;
			break;
			}
		i++;
		}

	len = write(STDERR_FILENO, "Geeqie fatal error\n", 19);
	len = write(STDERR_FILENO, "Signal: ", 8);
	len = write(STDERR_FILENO, signal_name, strlen(signal_name));
	len = write(STDERR_FILENO, "\n", 1);

	len = write(STDERR_FILENO, "Code: ", 6);
	len = write(STDERR_FILENO,  (info->si_code == SEGV_MAPERR) ? "Address not mapped" : "Invalid permissions", strlen((info->si_code == SEGV_MAPERR) ? "Address not mapped" : "Invalid permissions"));
	len = write(STDERR_FILENO, "\n", 1);

	len = write(STDERR_FILENO, "Address: ", 9);

	if (info->si_addr == nullptr)
		{
		len = write(STDERR_FILENO, "0x0\n", 4);
		}
	else
		{
		/* Assume the address is 64-bit */
		len = write(STDERR_FILENO, "0x", 2);
		addr = reinterpret_cast<guint64>(info->si_addr);

		for (i = 0; i < 16; i++)
			{
			char_index = addr & 0xf000000000000000;
			char_index = char_index >> 60;
			addr = addr << 4;

			len = write(STDERR_FILENO, &hex_char[char_index], 1);
			}
		len = write(STDERR_FILENO, "\n", 1);
		}

#if HAVE_EXECINFO_H
	bt_size = backtrace(bt, 1024);
	backtrace_symbols_fd(bt, bt_size, STDERR_FILENO);
#endif

	/* Avoid "not used" warning */
	len++;

	exit(EXIT_FAILURE);
}
#else /* defined(SA_SIGINFO) */
void sig_handler_cb(int)
{
#if HAVE_EXECINFO_H
	gint bt_size;
	void *bt[1024];
#endif

	write(STDERR_FILENO, "Geeqie fatal error\n", 19);
	write(STDERR_FILENO, "Signal: Segmentation fault\n", 27);

#if HAVE_EXECINFO_H
	bt_size = backtrace(bt, 1024);
	backtrace_symbols_fd(bt, bt_size, STDERR_FILENO);
#endif

	exit(EXIT_FAILURE);
}
#endif /* defined(SA_SIGINFO) */

/*
 *-----------------------------------------------------------------------------
 * command line parser (private) hehe, who needs popt anyway?
 *-----------------------------------------------------------------------------
 */

static void parse_command_line_add_file(const gchar *file_path, gchar **path, gchar **file,
					GList **list)
{
	gchar *path_parsed;

	path_parsed = g_strdup(file_path);
	parse_out_relatives(path_parsed);

		{
		if (!*path) *path = remove_level_from_path(path_parsed);
		if (!*file) *file = g_strdup(path_parsed);
		*list = g_list_prepend(*list, path_parsed);
		}
}

static void parse_command_line_add_dir(const gchar *dir, gchar **, gchar **, GList **)
{
#if 0
	/* This is broken because file filter is not initialized yet.
	*/
	GList *files;
	gchar *path_parsed;
	FileData *dir_fd;

	path_parsed = g_strdup(dir);
	parse_out_relatives(path_parsed);
	dir_fd = file_data_new_dir(path_parsed);


	if (filelist_read(dir_fd, &files, NULL))
		{
		GList *work;

		files = filelist_filter(files, FALSE);
		files = filelist_sort_path(files);

		work = files;
		while (work)
			{
			FileData *fd = static_cast<FileData *>(work->data);
			if (!*path) *path = remove_level_from_path(fd->path);
			if (!*file) *file = g_strdup(fd->path);
			*list = g_list_prepend(*list, fd);

			work = work->next;
			}

		g_list_free(files);
		}

	g_free(path_parsed);
	file_data_unref(dir_fd);
#else
	DEBUG_1("multiple directories specified, ignoring: %s", dir);
#endif
}

static void parse_command_line_process_dir(const gchar *dir, gchar **path, gchar **file,
					   GList **list, gchar **first_dir)
{

	if (!*list && !*first_dir)
		{
		*first_dir = g_strdup(dir);
		}
	else
		{
		if (*first_dir)
			{
			parse_command_line_add_dir(*first_dir, path, file, list);
			g_free(*first_dir);
			*first_dir = nullptr;
			}
		parse_command_line_add_dir(dir, path, file, list);
		}
}

static void parse_command_line_process_file(const gchar *file_path, gchar **path, gchar **file,
					    GList **list, gchar **first_dir)
{

	if (*first_dir)
		{
		parse_command_line_add_dir(*first_dir, path, file, list);
		g_free(*first_dir);
		*first_dir = nullptr;
		}
	parse_command_line_add_file(file_path, path, file, list);
}

static void show_invalid_parameters_warning_dialog(const gchar *command_line_errors)
{
	g_autoptr(GtkWidget) dialog_warning = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
	                                                             "%s", _("Invalid parameter(s):"));
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog_warning), "%s", command_line_errors);
	gtk_window_set_title(GTK_WINDOW(dialog_warning), GQ_APPNAME);
	gq_gtk_window_set_keep_above(GTK_WINDOW(dialog_warning), TRUE);
	gtk_dialog_run(GTK_DIALOG(dialog_warning));
}

static void parse_command_line(gint argc, gchar *argv[])
{
	GList *list = nullptr;
	gchar *first_dir = nullptr;
	gchar *geometry = nullptr;

	command_line = g_new0(CommandLine, 1);
	command_line->argc = argc;
	command_line->argv = argv;
	command_line->regexp = nullptr;

	if (argc > 1)
		{
		gchar *base_dir = get_current_dir();
		g_autoptr(GString) command_line_errors = g_string_new(nullptr);
		for (gint i = 1; i < argc; i++)
			{
			gchar *cmd_line = path_to_utf8(argv[i]);
			gchar *cmd_all = g_build_filename(base_dir, cmd_line, NULL);

			if (cmd_line[0] == G_DIR_SEPARATOR && isdir(cmd_line))
				{
				parse_command_line_process_dir(cmd_line, &command_line->path, &command_line->file, &list, &first_dir);
				}
			else if (isdir(cmd_all))
				{
				parse_command_line_process_dir(cmd_all, &command_line->path, &command_line->file, &list, &first_dir);
				}
			else if (cmd_line[0] == G_DIR_SEPARATOR && isfile(cmd_line))
				{
				parse_command_line_process_file(cmd_line, &command_line->path, &command_line->file,
								&list, &first_dir);
				}
			else if (isfile(cmd_all))
				{
				parse_command_line_process_file(cmd_all, &command_line->path, &command_line->file,
								&list, &first_dir);
				}
			else if (strncmp(cmd_line, "--debug", 7) == 0 && (cmd_line[7] == '\0' || cmd_line[7] == '='))
				{
				/* do nothing but do not produce warnings */
				}
			else if (strcmp(cmd_line, "-f") == 0 ||
				 strcmp(cmd_line, "--fullscreen") == 0)
				{
				command_line->startup_full_screen = TRUE;
				}
			else if (strncmp(cmd_line, "--geometry=", 11) == 0)
				{
				if (!command_line->geometry) command_line->geometry = g_strdup(cmd_line + 11);
				}
			else if ((strcmp(cmd_line, "-w") == 0) ||
						strcmp(cmd_line, "--show-log-window") == 0)
				{
				command_line->log_window_show = TRUE;
				}
			else if (strncmp(cmd_line, "-o", 2) == 0)
				{
				command_line->log_file = g_strdup(cmd_line + 2);
				}
			else if (strncmp(cmd_line, "--log-file=", 11) == 0)
				{
				command_line->log_file = g_strdup(cmd_line + 11);
				}
			else if (strncmp(cmd_line, "-g", 2) == 0)
				{
				set_regexp(g_strdup(cmd_line + 2));
				}
			else if (strncmp(cmd_line, "--grep=", 7) == 0)
				{
				set_regexp(g_strdup(cmd_line + 7));
				}
			else if (strcmp(cmd_line, "-v") == 0 ||
				 strcmp(cmd_line, "--version") == 0)
				{
				printf_term(FALSE, "%s %s GTK%u\n", GQ_APPNAME, VERSION, gtk_major_version);
				exit(EXIT_SUCCESS);
				}
			else if (strcmp(cmd_line, "-h") == 0 ||
				 strcmp(cmd_line, "--help") == 0)
				{
				printf_term(FALSE, "%s %s\n", GQ_APPNAME, VERSION);
				printf_term(FALSE, _("Usage: %s [options] [path]\n\n"), GQ_APPNAME_LC);
				print_term(FALSE, _("Valid options:\n"));
				print_term(FALSE, _("      --cache-maintenance=<path>   run cache maintenance in non-GUI mode\n"));
				print_term(FALSE, _("  -f, --fullscreen                 start in full screen mode\n"));
				print_term(FALSE, _("      --geometry=WxH+XOFF+YOFF     set main window location\n"));
				print_term(FALSE, _("  -h, --help                       show this message\n"));
				print_term(FALSE, _("  -o, --log-file=<file>            save log data to file\n"));
				print_term(FALSE, _("  -v, --version                    print version info\n"));
				print_term(FALSE, _("  -w, --show-log-window            show log window\n"));
#ifdef DEBUG
				print_term(FALSE, _("      --debug=[level]              turn on debug output\n"));
				print_term(FALSE, _("  -g, --grep=<regexp>              filter debug output\n"));
#endif

				print_term(FALSE, "\n");
				print_term(FALSE, "* Normally a single set of configuration files is used for all instances.\nHowever, the environment variables XDG_CONFIG_HOME, XDG_CACHE_HOME, XDG_DATA_HOME\ncan be used to modify this behavior on an individual basis e.g.\n\nXDG_CONFIG_HOME=/tmp/a XDG_CACHE_HOME=/tmp/b geeqie\n\n");

				exit(EXIT_SUCCESS);
				}
			else
				{
				g_string_append_printf(command_line_errors, _("%s\n\nThis option is unknown\n"), cmd_line);
				}

			g_free(cmd_all);
			g_free(cmd_line);
			}

		if (command_line_errors->len > 0)
			{
			show_invalid_parameters_warning_dialog(command_line_errors->str);

			exit(EXIT_FAILURE);
			}

		g_free(base_dir);
		parse_out_relatives(command_line->path);
		parse_out_relatives(command_line->file);
		}

	list = g_list_reverse(list);

	if (!command_line->path && first_dir)
		{
		command_line->path = first_dir;
		first_dir = nullptr;

		parse_out_relatives(command_line->path);
		}
	g_free(first_dir);
	g_free(geometry);

	if (list && list->next)
		{
		command_line->cmd_list = list;
		}
	else
		{
		g_list_free_full(list, g_free);
		command_line->cmd_list = nullptr;
		}
}

static void parse_command_line_for_debug_option(gint argc, gchar *argv[])
{
#ifdef DEBUG
	const gchar *debug_option = "--debug";
	const gint len = strlen(debug_option);

	for (gint i = 1; i < argc; i++)
		{
		// TODO(xsdg): Replace this with a regex match.  Simpler and less error-prone.
		const gchar *cmd_line = argv[i];
		if (strncmp(cmd_line, debug_option, len) == 0)
			{
			const gint cmd_line_len = strlen(cmd_line);

			/* we now increment the debug state for verbosity */
			if (cmd_line_len == len)
				debug_level_add(1);
			else if (cmd_line[len] == '=' && g_ascii_isdigit(cmd_line[len+1]))
				{
				gint n = atoi(cmd_line + len + 1);
				if (n < 0) n = 1;
				debug_level_add(n);
				}
			}
		}

	DEBUG_1("debugging output enabled (level %d)", get_debug_level());
#endif
}

static gboolean search_command_line_for_option(const gint argc, const gchar* const argv[], const gchar* option_name)
{
	const gint name_len = strlen(option_name);

	for (gint i = 1; i < argc; i++)
		{
		const gchar *current_arg = argv[i];
		// TODO(xsdg): This actually only checks prefixes.  We should
		// probably replace this with strcmp, since strlen already has
		// the shortcomings of strcmp (as compared to strncmp).
		//
		// That said, people may be unknowingly relying on the lenience
		// of this parsing strategy, so that's also something to consider.
		if (strncmp(current_arg, option_name, name_len) == 0)
			{
			return TRUE;
			}
		}

	return FALSE;
}

static gboolean search_command_line_for_unit_test_option(gint argc, gchar *argv[])
{
	return search_command_line_for_option(argc, argv, "--run-unit-tests");
}

static gboolean parse_command_line_for_cache_maintenance_option(gint argc, gchar *argv[])
{
	const gchar *cache_maintenance_option = "--cache-maintenance=";
	const gint len = strlen(cache_maintenance_option);

	if (argc >= 2)
		{
		const gchar *cmd_line = argv[1];
		if (strncmp(cmd_line, cache_maintenance_option, len) == 0)
			{
			return TRUE;
			}
		}

	return FALSE;
}

static void process_command_line_for_cache_maintenance_option(gint argc, gchar *argv[])
{
	if (argc < 2)
		{
		print_term(TRUE, _("No path parameter given\n"));
		exit(EXIT_FAILURE);
		}

	const gchar *cache_maintenance_option = "--cache-maintenance=";
	const gint len = strlen(cache_maintenance_option);

	g_autofree gchar *folder_path = expand_tilde(argv[1] + len);
	if (!isdir(folder_path))
		{
		print_term(TRUE, g_strconcat(argv[1] + len, _(" is not a folder\n"), NULL));
		exit(EXIT_FAILURE);
		}

	g_autofree gchar *rc_path = g_build_filename(get_rc_dir(), RC_FILE_NAME, NULL);
	if (!isfile(rc_path))
		{
		print_term(TRUE, g_strconcat(_("Configuration file path "), rc_path, _(" is not a file\n"), NULL));
		exit(EXIT_FAILURE);
		}

	g_autofree gchar *buf_config_file = nullptr;
	gsize size;
	if (!g_file_get_contents(rc_path, &buf_config_file, &size, nullptr))
		{
		print_term(TRUE, g_strconcat(_("Cannot load "), rc_path, "\n", NULL));
		exit(EXIT_FAILURE);
		}

	/* Load only the <global> section */
	const gchar *global_tag_end = "</global>";
	const gint global_tag_end_len = strlen(global_tag_end);

	gsize global_section_size = size;
	for (gsize i = 0; i < size; i++)
		{
		if (strncmp(global_tag_end, buf_config_file + i, global_tag_end_len) == 0)
			{
			global_section_size = i + global_tag_end_len;
			break;
			}
		}

	load_config_from_buf(buf_config_file, global_section_size, FALSE);

	if (!options->thumbnails.enable_caching)
		{
		print_term(TRUE, "Caching not enabled\n");
		exit(EXIT_FAILURE);
		}

	cache_maintenance(folder_path);
}

/*
 *-----------------------------------------------------------------------------
 * startup, init, and exit
 *-----------------------------------------------------------------------------
 */

#define RC_HISTORY_NAME "history"

static void setup_env_path()
{
	const gchar *old_path = g_getenv("PATH");
	g_autofree gchar *path = g_strconcat(gq_bindir, ":", old_path, NULL);
	g_setenv("PATH", path, TRUE);
}

static const gchar *get_history_path()
{
#if USE_XDG
	static gchar *history_path = g_build_filename(xdg_data_home_get(), GQ_APPNAME_LC, RC_HISTORY_NAME, NULL);
#else
	static gchar *history_path = g_build_filename(get_rc_dir(), RC_HISTORY_NAME, NULL);
#endif

	return history_path;
}

static void keys_load()
{
	const gchar *path = get_history_path();

	history_list_load(path);
}

static void keys_save()
{
	const gchar *path = get_history_path();

	history_list_save(path);
}

static void mkdir_if_not_exists(const gchar *path)
{
	if (isdir(path)) return;

	log_printf(_("Creating %s dir:%s\n"), GQ_APPNAME, path);

	if (!recursive_mkdir_if_not_exists(path, 0755))
		{
		log_printf(_("Could not create dir:%s\n"), path);
		}
}


/* We add to duplicate and modify  gtk_accel_map_print() and gtk_accel_map_save()
 * to improve the reliability in special cases (especially when disk is full)
 * These functions are now using secure saving stuff.
 */
static void gq_accel_map_print(
		    gpointer 	data,
		    const gchar	*accel_path,
		    guint	accel_key,
		    GdkModifierType accel_mods,
		    gboolean	changed)
{
	GString *gstring = g_string_new(changed ? nullptr : "; ");
	auto ssi = static_cast<SecureSaveInfo *>(data);
	gchar *tmp;
	gchar *name;

	g_string_append(gstring, "(gtk_accel_path \"");

	tmp = g_strescape(accel_path, nullptr);
	g_string_append(gstring, tmp);
	g_free(tmp);

	g_string_append(gstring, "\" \"");

	name = gtk_accelerator_name(accel_key, accel_mods);
	tmp = g_strescape(name, nullptr);
	g_free(name);
	g_string_append(gstring, tmp);
	g_free(tmp);

	g_string_append(gstring, "\")\n");

	secure_fwrite(gstring->str, sizeof(*gstring->str), gstring->len, ssi);

	g_string_free(gstring, TRUE);
}

static gboolean gq_accel_map_save(const gchar *path)
{
	gchar *pathl;
	SecureSaveInfo *ssi;
	GString *gstring;

	pathl = path_from_utf8(path);
	ssi = secure_open(pathl);
	g_free(pathl);
	if (!ssi)
		{
		log_printf(_("error saving file: %s\n"), path);
		return FALSE;
		}

	gstring = g_string_new("; ");
	if (g_get_prgname())
		g_string_append(gstring, g_get_prgname());
	g_string_append(gstring, " GtkAccelMap rc-file         -*- scheme -*-\n");
	g_string_append(gstring, "; this file is an automated accelerator map dump\n");
	g_string_append(gstring, ";\n");

	secure_fwrite(gstring->str, sizeof(*gstring->str), gstring->len, ssi);

	g_string_free(gstring, TRUE);

	gtk_accel_map_foreach(ssi, gq_accel_map_print);

	if (secure_close(ssi))
		{
		log_printf(_("error saving file: %s\nerror: %s\n"), path,
			   secsave_strerror(secsave_errno));
		return FALSE;
		}

	return TRUE;
}

static gchar *accep_map_filename()
{
	return g_build_filename(get_rc_dir(), "accels", NULL);
}

static void accel_map_save()
{
	gchar *path;

	path = accep_map_filename();
	gq_accel_map_save(path);
	g_free(path);
}

static void accel_map_load()
{
	gchar *path;
	gchar *pathl;

	path = accep_map_filename();
	pathl = path_from_utf8(path);
	gtk_accel_map_load(pathl);
	g_free(pathl);
	g_free(path);
}

static void gtkrc_load()
{
	gchar *path;
	gchar *pathl;

	/* If a gtkrc file exists in the rc directory, add it to the
	 * list of files to be parsed at the end of gtk_init() */
	path = g_build_filename(get_rc_dir(), "gtkrc", NULL);
	pathl = path_from_utf8(path);
	if (access(pathl, R_OK) == 0)
		gtk_rc_add_default_file(pathl);
	g_free(pathl);
	g_free(path);
}

static void exit_program_final()
{
	layout_editors_reload_finish();

	save_options(options);
	keys_save();
	accel_map_save();

	secure_close(command_line->ssi);

	gtk_main_quit();
}

void exit_program()
{
	layout_image_full_screen_stop(nullptr);
	exit_program_final();
}

#if !HAVE_DEVELOPER
static void setup_sig_handler()
{
	struct sigaction sigsegv_action;
	sigfillset(&sigsegv_action.sa_mask);
	sigsegv_action.sa_sigaction = sig_handler_cb;
	sigsegv_action.sa_flags = SA_SIGINFO;

	sigaction(SIGABRT, &sigsegv_action, nullptr);
	sigaction(SIGBUS, &sigsegv_action, nullptr);
	sigaction(SIGFPE, &sigsegv_action, nullptr);
	sigaction(SIGILL, &sigsegv_action, nullptr);
	sigaction(SIGIOT, &sigsegv_action, nullptr);
	sigaction(SIGSEGV, &sigsegv_action, nullptr);
}
#endif

static void set_theme_bg_color()
{
	GdkRGBA bg_color;
	GdkRGBA theme_color;
	GtkStyleContext *style_context;

	if (!options->image.use_custom_border_color)
		{
		style_context = gtk_widget_get_style_context(main_lw->window);
		gtk_style_context_get_background_color(style_context, GTK_STATE_FLAG_NORMAL, &bg_color);

		theme_color.red = bg_color.red  ;
		theme_color.green = bg_color.green  ;
		theme_color.blue = bg_color.blue ;

		image_background_set_color(main_lw->image, &theme_color);
		}
}

static gboolean theme_change_cb(GObject *, GParamSpec *, gpointer)
{
	set_theme_bg_color();

	return FALSE;
}

static void create_application_paths()
{
	gchar *dirname;
	gint length;
	gchar *path;

	length = wai_getExecutablePath(nullptr, 0, nullptr);
	path = static_cast<gchar *>(malloc(length + 1));
	wai_getExecutablePath(path, length, nullptr);
	path[length] = '\0';

	gq_executable_path = g_strdup(path);
	dirname = g_path_get_dirname(gq_executable_path);
	gchar *gq_prefix = g_path_get_dirname(dirname);

	gq_localedir = g_build_filename(gq_prefix, GQ_LOCALEDIR, NULL);
	gq_appdir = g_build_filename(gq_prefix, GQ_APPDIR, NULL);
	gq_bindir = g_build_filename(gq_prefix, GQ_BINDIR, NULL);
	desktop_file_template = g_build_filename(gq_appdir, "org.geeqie.template.desktop", NULL);

	g_free(gq_prefix);
	g_free(dirname);
	g_free(path);
}

gint main(gint argc, gchar *argv[])
{
	// We handle unit tests here because it takes the place of running the
	// rest of the app.
	if (search_command_line_for_unit_test_option(argc, argv))
	{
#if ENABLE_UNIT_TESTS
		testing::InitGoogleTest(&argc, argv);
		return RUN_ALL_TESTS();
#else
		fprintf(stderr, "Unit tests are not enabled in this build.\n");
		return 1;
#endif
	}

	gboolean single_dir = TRUE;
	GdkScreen *screen;
	GtkCssProvider *provider;
	GtkSettings *default_settings;
	LayoutWindow *lw;

	gdk_set_allowed_backends("x11,*");

	gdk_threads_init();
	gdk_threads_enter();

	/* seg. fault handler */
#if HAVE_DEVELOPER
	backward::SignalHandling sh{};
#else
	setup_sig_handler();
#endif

	/* init execution time counter (debug only) */
	init_exec_time();

	create_application_paths();

	/* setup locale, i18n */
	setlocale(LC_ALL, "");

#ifdef ENABLE_NLS
	bindtextdomain(PACKAGE, gq_localedir);
	bind_textdomain_codeset(PACKAGE, "UTF-8");
	textdomain(PACKAGE);
#endif

	exif_init();

	/* setup random seed for random slideshow */
	srand(time(nullptr));

	/* register global notify functions */
	file_data_register_notify_func(cache_notify_cb, nullptr, NOTIFY_PRIORITY_HIGH);
	file_data_register_notify_func(thumb_notify_cb, nullptr, NOTIFY_PRIORITY_HIGH);


	gtkrc_load();

	parse_command_line_for_debug_option(argc, argv);
	DEBUG_1("%s main: gtk_init", get_exec_time());
	gtk_init(&argc, &argv);

	if (gtk_major_version < GTK_MAJOR_VERSION ||
	    (gtk_major_version == GTK_MAJOR_VERSION && gtk_minor_version < GTK_MINOR_VERSION) )
		{
		log_printf("!!! This is a friendly warning.\n");
		log_printf("!!! The version of GTK+ in use now is older than when %s was compiled.\n", GQ_APPNAME);
		log_printf("!!!  compiled with GTK+-%d.%d\n", GTK_MAJOR_VERSION, GTK_MINOR_VERSION);
		log_printf("!!!   running with GTK+-%u.%u\n", gtk_major_version, gtk_minor_version);
		log_printf("!!! %s may quit unexpectedly with a relocation error.\n", GQ_APPNAME);
		}

	DEBUG_1("%s main: pixbuf_inline_register_stock_icons", get_exec_time());
	gtk_icon_theme_add_resource_path(gtk_icon_theme_get_default(), GQ_RESOURCE_PATH_ICONS);
	pixbuf_inline_register_stock_icons();

	DEBUG_1("%s main: setting default options before commandline handling", get_exec_time());
	options = init_options(nullptr);
	setup_default_options(options);

	DEBUG_1("%s main: mkdir_if_not_exists", get_exec_time());
	/* these functions don't depend on config file */
	mkdir_if_not_exists(get_rc_dir());
	mkdir_if_not_exists(get_thumbnails_cache_dir());

	setup_env_path();

	screen = gdk_screen_get_default();
	provider = gtk_css_provider_new();
	gtk_css_provider_load_from_resource(provider, GQ_RESOURCE_PATH_UI "/custom.css");
	gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	if (parse_command_line_for_cache_maintenance_option(argc, argv))
		{
		process_command_line_for_cache_maintenance_option(argc, argv);
		}
	else
		{
		DEBUG_1("%s main: parse_command_line", get_exec_time());
		parse_command_line(argc, argv);

		keys_load();
		accel_map_load();

		/* restore session from the config file */


		DEBUG_1("%s main: load_options", get_exec_time());
		if (!load_options(options))
			{
			/* load_options calls these functions after it parses global options, we have to call it here if it fails */
			filter_add_defaults();
			filter_rebuild();
			}

		/* Load plugin desktop files so the Plugins menu is populated */
		editor_table_clear();
		GList *desktop_files = editor_get_desktop_files();
		while (desktop_files)
			{
			editor_read_desktop_file(static_cast<const gchar *>(desktop_files->data));
			g_free(desktop_files->data);
			desktop_files = g_list_delete_link(desktop_files, desktop_files);
			}
		editor_table_finish();

		/* handle missing config file and commandline additions*/
		if (!main_lw)
		{
			layout_new_from_default();
		}

		layout_editors_reload_start();

		if (command_line->log_file)
			{
			gchar *pathl;
			gchar *path = g_strdup(command_line->log_file);

			pathl = path_from_utf8(path);
			command_line->ssi = secure_open(pathl);
			}

		/* If there is a files list on the command line and no --list option,
		 * check if they are all in the same folder
		 */
		if (command_line->cmd_list)
			{
			GList *work;
			gchar *path = nullptr;

			work = command_line->cmd_list;

			while (work && single_dir)
				{
				gchar *dirname;

				dirname = g_path_get_dirname(static_cast<const gchar *>(work->data));
				if (!path)
					{
					path = g_strdup(dirname);
					}
				else
					{
					if (g_strcmp0(path, dirname) != 0)
						{
						single_dir = FALSE;
						}
					}
				g_free(dirname);
				work = work->next;
				}
			g_free(path);
			}

		/* Files from multiple folders, or --list option given
		 * then open an unnamed collection and insert all files
		 */
		 {}

		/* If the files on the command line are from one folder, select those files
		 * unless it is a command line collection - then leave focus on collection window
		 */
		lw = nullptr;
		layout_valid(&lw);

		default_settings = gtk_settings_get_default();
		g_signal_connect(default_settings, "notify::gtk-theme-name", G_CALLBACK(theme_change_cb), NULL);
		set_theme_bg_color();
		}

	DEBUG_1("%s main: gtk_main", get_exec_time());
	gtk_main();

	gdk_threads_leave();
	return 0;
}
