/*****************************************************************************

 Copyright (c) 2008-2014  Monavacon Limited <http://www.monavacon.com/>
 Copyright (c) 2001-2008  OpenSS7 Corporation <http://www.openss7.com/>
 Copyright (c) 1997-2001  Brian F. G. Bidulock <bidulock@openss7.org>

 All Rights Reserved.

 This program is free software: you can redistribute it and/or modify it under
 the terms of the GNU General Public License as published by the Free Software
 Foundation, version 3 of the license.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 details.

 You should have received a copy of the GNU General Public License along with
 this program.  If not, see <http://www.gnu.org/licenses/>, or write to the
 Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 -----------------------------------------------------------------------------

 U.S. GOVERNMENT RESTRICTED RIGHTS.  If you are licensing this Software on
 behalf of the U.S. Government ("Government"), the following provisions apply
 to you.  If the Software is supplied by the Department of Defense ("DoD"), it
 is classified as "Commercial Computer Software" under paragraph 252.227-7014
 of the DoD Supplement to the Federal Acquisition Regulations ("DFARS") (or any
 successor regulations) and the Government is acquiring only the license rights
 granted herein (the license rights customarily provided to non-Government
 users).  If the Software is supplied to any unit or agency of the Government
 other than DoD, it is classified as "Restricted Computer Software" and the
 Government's rights in the Software are defined in paragraph 52.227-19 of the
 Federal Acquisition Regulations ("FAR") (or any successor regulations) or, in
 the cases of NASA, in paragraph 18.52.227-86 of the NASA Supplement to the FAR
 (or any successor regulations).

 -----------------------------------------------------------------------------

 Commercial licensing and support of this software is available from OpenSS7
 Corporation at a fee.  See http://www.openss7.com/

 *****************************************************************************/

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "xde-logout.h"

#include <dbus/dbus-glib.h>

#ifdef _GNU_SOURCE
#include <getopt.h>
#endif

typedef enum _LogoSide {
	LOGO_SIDE_LEFT,
	LOGO_SIDE_TOP,
	LOGO_SIDE_RIGHT,
	LOGO_SIDE_BOTTOM,
} LogoSide;

typedef struct {
	int output;
	int debug;
	Bool dryrun;
	char *lockscreen;
	char *banner;
	char *prompt;
	LogoSide side;
	Bool noask;
	char *icon_theme;
	char *gtk2_theme;
	Bool usexde;
	unsigned int timeout;
} Options;

Options options = {
	.output = 1,
	.debug = 0,
	.dryrun = False,
	.lockscreen = NULL,
	.banner = NULL,
	.prompt = NULL,
	.side = LOGO_SIDE_LEFT,
	.noask = False,
	.icon_theme = NULL,
	.gtk2_theme = NULL,
	.usexde = False,
	.timeout = 15,
};

typedef enum {
	LOGOUT_ACTION_POWEROFF,		/* power of the computer */
	LOGOUT_ACTION_REBOOT,		/* reboot the computer */
	LOGOUT_ACTION_SUSPEND,		/* suspend the computer */
	LOGOUT_ACTION_HIBERNATE,	/* hibernate the computer */
	LOGOUT_ACTION_HYBRIDSLEEP,	/* hybrid sleep the computer */
	LOGOUT_ACTION_SWITCHUSER,	/* switch users */
	LOGOUT_ACTION_SWITCHDESK,	/* switch desktops */
	LOGOUT_ACTION_LOCKSCREEN,	/* lock screen */
	LOGOUT_ACTION_LOGOUT,		/* logout of current session */
	LOGOUT_ACTION_RESTART,		/* restart current session */
	LOGOUT_ACTION_CANCEL,		/* cancel logout */
	LOGOUT_ACTION_COUNT,
} LogoutActionResult;

LogoutActionResult action_result;
LogoutActionResult logout_result = LOGOUT_ACTION_CANCEL;

typedef enum {
	AvailStatusUndef,		/* undefined */
	AvailStatusUnknown,		/* not known */
	AvailStatusNa,			/* not available */
	AvailStatusNo,			/* available not permitted */
	AvailStatusChallenge,		/* available with password */
	AvailStatusYes,			/* available and permitted */
} AvailStatus;

AvailStatus
status_of_string(const char *string)
{
	if (!string)
		return AvailStatusUndef;
	if (!string || !strcmp(string, "na"))
		return AvailStatusNa;
	if (!strcmp(string, "no"))
		return AvailStatusNo;
	if (!strcmp(string, "yes"))
		return AvailStatusYes;
	if (!strcmp(string, "challenge"))
		return AvailStatusChallenge;
	EPRINTF("unknown availability status %s\n", string);
	return AvailStatusUnknown;
}

static AvailStatus action_can[LOGOUT_ACTION_COUNT] = {
	/* *INDENT-OFF* */
	[LOGOUT_ACTION_POWEROFF]	= AvailStatusUndef,
	[LOGOUT_ACTION_REBOOT]		= AvailStatusUndef,
	[LOGOUT_ACTION_SUSPEND]		= AvailStatusUndef,
	[LOGOUT_ACTION_HIBERNATE]	= AvailStatusUndef,
	[LOGOUT_ACTION_HYBRIDSLEEP]	= AvailStatusUndef,
	[LOGOUT_ACTION_SWITCHUSER]	= AvailStatusUndef,
	[LOGOUT_ACTION_SWITCHDESK]	= AvailStatusUndef,
	[LOGOUT_ACTION_LOCKSCREEN]	= AvailStatusUndef,
	[LOGOUT_ACTION_LOGOUT]		= AvailStatusUndef,
	[LOGOUT_ACTION_RESTART]		= AvailStatusUndef,
	[LOGOUT_ACTION_CANCEL]		= AvailStatusUndef,
	/* *INDENT-ON* */
};

char **
get_data_dirs(int *np)
{
	char *home, *xhome, *xdata, *dirs, *pos, *end, **xdg_dirs;
	int len, n;

	home = getenv("HOME") ? : ".";
	xhome = getenv("XDG_DATA_HOME");
	xdata = getenv("XDG_DATA_DIRS") ? : "/usr/local/share:/usr/share";

	len = (xhome ? strlen(xhome) : strlen(home) + strlen("/.local/share")) + strlen(xdata) + 2;
	dirs = calloc(len, sizeof(*dirs));
	if (xhome)
		strcpy(dirs, xhome);
	else {
		strcpy(dirs, home);
		strcat(dirs, "/.local/share");
	}
	strcat(dirs, ":");
	strcat(dirs, xdata);
	end = dirs + strlen(dirs);
	for (n = 0, pos = dirs; pos < end;
	     n++, *strchrnul(pos, ':') = '\0', pos += strlen(pos) + 1) ;
	xdg_dirs = calloc(n + 1, sizeof(*xdg_dirs));
	for (n = 0, pos = dirs; pos < end; n++, pos += strlen(pos) + 1)
		xdg_dirs[n] = strdup(pos);
	free(dirs);
	if (np)
		*np = n;
	return (xdg_dirs);
}

char **
get_config_dirs(int *np)
{
	char *home, *xhome, *xconf, *dirs, *pos, *end, **xdg_dirs;
	int len, n;

	home = getenv("HOME") ? : ".";
	xhome = getenv("XDG_CONFIG_HOME");
	xconf = getenv("XDG_CONFIG_DIRS") ? : "/etc/xdg";

	len = (xhome ? strlen(xhome) : strlen(home) + strlen("/.config")) + strlen(xconf) + 2;
	dirs = calloc(len, sizeof(*dirs));
	if (xhome)
		strcpy(dirs, xhome);
	else {
		strcpy(dirs, home);
		strcat(dirs, "/.config");
	}
	strcat(dirs, ":");
	strcat(dirs, xconf);
	end = dirs + strlen(dirs);
	for (n = 0, pos = dirs; pos < end;
	     n++, *strchrnul(pos, ':') = '\0', pos += strlen(pos) + 1) ;
	xdg_dirs = calloc(n + 1, sizeof(*xdg_dirs));
	for (n = 0, pos = dirs; pos < end; n++, pos += strlen(pos) + 1)
		xdg_dirs[n] = strdup(pos);
	free(dirs);
	if (np)
		*np = n;
	return (xdg_dirs);
}

/*
 * Determine whether we have been invoked under a session running lxsession(1).
 * When that is the case, we simply execute lxsession-logout(1) with the
 * appropriate parameters for branding.  In that case, this method does not
 * return (executes lxsession-logout directly).  Otherwise the method returns.
 * This method is currently unused and is deprecated.
 */
void
lxsession_check()
{
}

struct prog_cmd {
	char *name;
	char *cmd;
};

/** @brief test availability of a screen locker program
  *
  * Test to see whether the caller specified a lock screen program. If not,
  * search through a short list of known screen lockers, searching PATH for an
  * executable of the corresponding name, and when one is found, set the screen
  * locking program to that function.  We could probably easily write our own
  * little screen locker here, but I don't have the time just now...
  *
  * These are hardcoded.  Sorry.  Later we can try to design a reliable search
  * for screen locking programs in the XDG applications directory or create a
  * "sensible-" or "preferred-" screen locker shell program.  That would be
  * useful for menus too.
  *
  * Note that if a screen saver is registered with the X Server then we can
  * likely simply ask the screen saver to lock the screen.
  *
  * Note also that we can use DBUS interface to systemd logind service to
  * request that a session manager lock the screen.
  */
void
test_lock_screen_program()
{
	static const struct prog_cmd progs[6] = {
		/* *INDENT-OFF* */
		{"xlock",	    "xlock -mode blank &"   },
		{"slock",	    "slock &"		    },
		{"slimlock",	    "slimlock &"	    },
		{"i3lock",	    "i3lock -c 000000 &"    },
		{"xscreensaver",    "xscreensaver -lock &"  },
		{ NULL,		     NULL		    }
		/* *INDENT-ON* */
	};
	const struct prog_cmd *prog;

	if (!options.lockscreen) {
		for (prog = progs; prog->name; prog++) {
			char *paths = strdup(getenv("PATH") ? : "");
			char *p = paths - 1;
			char *e = paths + strlen(paths);
			char *b, *path;
			struct stat st;
			int status;
			int len;

			while ((b = p + 1) < e) {
				*(p = strchrnul(b, ':')) = '\0';
				len = strlen(b) + 1 + strlen(prog->name) + 1;
				path = calloc(len, sizeof(*path));
				strncpy(path, b, len);
				strncat(path, "/", len);
				strncat(path, prog->name, len);
				status = stat(path, &st);
				free(path);
				if (status == 0 && S_ISREG(st.st_mode) && (st.st_mode & S_IXOTH)) {
					options.lockscreen = strdup(prog->cmd);
					goto done;
				}
			}
		}
	}
      done:
	if (options.lockscreen)
		action_can[LOGOUT_ACTION_LOCKSCREEN] = AvailStatusYes;
	return;
}

/** @brief test availability of power functions
  *
  * Uses DBUsGProxy and the login1 service to test for available power
  * functions.  The results of the test are stored in the corresponding booleans
  * in the available functions structure.
  */
void
test_power_functions()
{
	GError *error = NULL;
	DBusGConnection *bus;
	DBusGProxy *proxy;
	gchar *value = NULL;
	gboolean ok;

	if (!(bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error)) || error) {
		EPRINTF("cannot access system bus\n");
		return;
	} else
		error = NULL;
	proxy = dbus_g_proxy_new_for_name(bus,
					  "org.freedesktop.login1",
					  "/org/freedesktop/login1",
					  "org.freedesktop.login1.Manager");
	if (!proxy) {
		EPRINTF("cannot create DBUS proxy\n");
		return;
	}
	value = NULL;
	ok = dbus_g_proxy_call(proxy, "CanPowerOff",
			       &error, G_TYPE_INVALID, G_TYPE_STRING, &value, G_TYPE_INVALID);
	if (ok && !error) {
		DPRINTF("CanPowerOff status is %s\n", value);
		action_can[LOGOUT_ACTION_POWEROFF] = status_of_string(value);
		g_free(value);
		value = NULL;
	} else
		error = NULL;
	ok = dbus_g_proxy_call(proxy, "CanReboot",
			       &error, G_TYPE_INVALID, G_TYPE_STRING, &value, G_TYPE_INVALID);
	if (ok && !error) {
		DPRINTF("CanReboot status is %s\n", value);
		action_can[LOGOUT_ACTION_REBOOT] = status_of_string(value);
		g_free(value);
		value = NULL;
	} else
		error = NULL;
	ok = dbus_g_proxy_call(proxy, "CanSuspend",
			       &error, G_TYPE_INVALID, G_TYPE_STRING, &value, G_TYPE_INVALID);
	if (ok && !error) {
		DPRINTF("CanSuspend status is %s\n", value);
		action_can[LOGOUT_ACTION_SUSPEND] = status_of_string(value);
		g_free(value);
		value = NULL;
	} else
		error = NULL;
	ok = dbus_g_proxy_call(proxy, "CanHibernate",
			       &error, G_TYPE_INVALID, G_TYPE_STRING, &value, G_TYPE_INVALID);
	if (ok && !error) {
		DPRINTF("CanHibernate status is %s\n", value);
		action_can[LOGOUT_ACTION_HIBERNATE] = status_of_string(value);
		g_free(value);
		value = NULL;
	} else
		error = NULL;
	ok = dbus_g_proxy_call(proxy, "CanHybridSleep",
			       &error, G_TYPE_INVALID, G_TYPE_STRING, &value, G_TYPE_INVALID);
	if (ok && !error) {
		DPRINTF("CanHybridSleep status is %s\n", value);
		action_can[LOGOUT_ACTION_HYBRIDSLEEP] = status_of_string(value);
		g_free(value);
		value = NULL;
	} else
		error = NULL;
	g_object_unref(G_OBJECT(proxy));
}

/** @brief test availability of user functions
  *
  * For now, do not let the user switch users or window managers.
  */
void
test_user_functions()
{
	action_can[LOGOUT_ACTION_SWITCHUSER] = AvailStatusNa;
	action_can[LOGOUT_ACTION_SWITCHDESK] = AvailStatusNa;
}

/** @brief test availability of session functions
  *
  * For now, always let the user logout or cancel, but not restart the current
  * session.
  */
void
test_session_functions()
{
	action_can[LOGOUT_ACTION_LOGOUT] = AvailStatusYes;
	action_can[LOGOUT_ACTION_RESTART] = AvailStatusNa;
	action_can[LOGOUT_ACTION_CANCEL] = AvailStatusYes;
}

/** @brief transform a window into a grabbed window
  *
  * Transform the a window into a window that has a grab on the pointer on a
  * GtkWindow and restricts pointer movement to the window boundary.
  */
void
grabbed_window(GtkWindow *window)
{
	GdkWindow *win = gtk_widget_get_window(GTK_WIDGET(window));
	GdkEventMask mask = GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK;

	gdk_window_set_override_redirect(win, TRUE);
	gdk_window_set_focus_on_map(win, TRUE);
	gdk_window_set_accept_focus(win, TRUE);
	gdk_window_set_keep_above(win, TRUE);
	gdk_window_set_modal_hint(win, TRUE);
	gdk_window_stick(win);
	gdk_window_deiconify(win);
	gdk_window_show(win);
	gdk_window_focus(win, GDK_CURRENT_TIME);
	if (gdk_keyboard_grab(win, TRUE, GDK_CURRENT_TIME) != GDK_GRAB_SUCCESS)
		EPRINTF("Could not grab keyboard!\n");
	if (gdk_pointer_grab(win, TRUE, mask, win, NULL, GDK_CURRENT_TIME) != GDK_GRAB_SUCCESS)
		EPRINTF("Could not grab pointer!\n");
}

/** @brief transform a window away from a grabbed window
  *
  * Transform the window back into a regular window, releasing the pointer and
  * keyboard grab and motion restriction.  window is the GtkWindow that
  * previously had the grabbed_window() method called on it.
  */
void
ungrabbed_window(GtkWindow *window)
{
	GdkWindow *win = gtk_widget_get_window(GTK_WIDGET(window));

	gdk_keyboard_ungrab(GDK_CURRENT_TIME);
	gdk_pointer_ungrab(GDK_CURRENT_TIME);
	gdk_window_hide(win);
}

/** @brief ask the user if they are sure
  * @param window - grabbed window
  * @param message - dialog message
  *
  * Simple dialog prompting the user with a yes or no question; however, the
  * window is one that was previously grabbed using grabbed_window().  This
  * method hands the focus grab to the dialog and back to the window on exit.
  * Returns the response to the dialog.
  */
gboolean
areyousure(GtkWindow *window, char *message)
{
	GtkWidget *d;
	gint result;

	ungrabbed_window(window);
	d = gtk_message_dialog_new(window, GTK_DIALOG_MODAL,
				   GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", message);
	gtk_window_set_title(GTK_WINDOW(d), "Are you sure?");
	gtk_window_set_modal(GTK_WINDOW(d), TRUE);
	gtk_window_set_gravity(GTK_WINDOW(d), GDK_GRAVITY_CENTER);
	gtk_window_set_type_hint(GTK_WINDOW(d), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
	gtk_window_set_skip_pager_hint(GTK_WINDOW(d), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(d), TRUE);
	gtk_window_set_position(GTK_WINDOW(d), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_widget_show_all(GTK_WIDGET(d));
	gtk_widget_show_now(GTK_WIDGET(d));
	grabbed_window(GTK_WINDOW(d));
	result = gtk_dialog_run(GTK_DIALOG(d));
	ungrabbed_window(GTK_WINDOW(d));
	gtk_widget_destroy(GTK_WIDGET(d));
	if (result == GTK_RESPONSE_YES)
		grabbed_window(window);
	return ((result == GTK_RESPONSE_YES) ? TRUE : FALSE);
}

gboolean
on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	logout_result = LOGOUT_ACTION_CANCEL;
	gtk_main_quit();
	return TRUE;		/* propagate */
}

gboolean
on_expose_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	GdkPixbuf *p = GDK_PIXBUF(data);
	GdkWindow *w = gtk_widget_get_window(GTK_WIDGET(widget));
	cairo_t *cr = gdk_cairo_create(GDK_DRAWABLE(w));

	gdk_cairo_set_source_pixbuf(cr, p, 0, 0);
	cairo_paint(cr);
	GdkColor color = {.red = 0,.green = 0,.blue = 0,.pixel = 0, };
	gdk_cairo_set_source_color(cr, &color);
	cairo_paint_with_alpha(cr, 0.7);
	return FALSE;
}

GtkWidget *buttons[LOGOUT_ACTION_COUNT] = { NULL, };

static const char *button_tips[LOGOUT_ACTION_COUNT] = {
	/* *INDENT-OFF* */
	[LOGOUT_ACTION_POWEROFF]	= "Shutdown the computer.",
	[LOGOUT_ACTION_REBOOT]		= "Reboot the computer.",
	[LOGOUT_ACTION_SUSPEND]		= "Suspend the computer.",
	[LOGOUT_ACTION_HIBERNATE]	= "Hibernate the computer.",
	[LOGOUT_ACTION_HYBRIDSLEEP]	= "Hybrid sleep the computer.",
	[LOGOUT_ACTION_SWITCHUSER]	= "Switch users.",
	[LOGOUT_ACTION_SWITCHDESK]	= "Switch window managers.",
	[LOGOUT_ACTION_LOCKSCREEN]	= "Lock the screen.",
	[LOGOUT_ACTION_LOGOUT]		= "Log out of current session.",
	[LOGOUT_ACTION_RESTART]		= "Restart current session.",
	[LOGOUT_ACTION_CANCEL]		= "Cancel",
	/* *INDENT-ON* */
};

static const char *button_labels[LOGOUT_ACTION_COUNT] = {
	/* *INDENT-OFF* */
	[LOGOUT_ACTION_POWEROFF]	= "Power Off",
	[LOGOUT_ACTION_REBOOT]		= "Reboot",
	[LOGOUT_ACTION_SUSPEND]		= "Suspend",
	[LOGOUT_ACTION_HIBERNATE]	= "Hibernate",
	[LOGOUT_ACTION_HYBRIDSLEEP]	= "Hybrid Sleep",
	[LOGOUT_ACTION_SWITCHUSER]	= "Switch User",
	[LOGOUT_ACTION_SWITCHDESK]	= "Switch Desktop",
	[LOGOUT_ACTION_LOCKSCREEN]	= "Lock Screen",
	[LOGOUT_ACTION_LOGOUT]		= "Logout",
	[LOGOUT_ACTION_RESTART]		= "Restart",
	[LOGOUT_ACTION_CANCEL]		= "Cancel",
	/* *INDENT-ON* */
};

/* TODO: we should actually build an icon factory with stock icons */

static const char *button_icons[LOGOUT_ACTION_COUNT][3] = {
	/* *INDENT-OFF* */
	[LOGOUT_ACTION_POWEROFF]	= { "gtk-stop",		    "system-shutdown",		    "gnome-session-halt"	},
	[LOGOUT_ACTION_REBOOT]		= { "gtk-refresh",	    "system-reboot",		    "gnome-session-reboot"	},
	[LOGOUT_ACTION_SUSPEND]		= { "gtk_save",		    "system-suspend",		    "gnome-session-suspend"	},
	[LOGOUT_ACTION_HIBERNATE]	= { "gtk-save-as",	    "system-suspend-hybernate",	    "gnome-session-hybernate"	},
	[LOGOUT_ACTION_HYBRIDSLEEP]	= { "gtk-revert-to-saved",  "system-suspend-hybernate",	    "gnome-session-hybernate"	},
	[LOGOUT_ACTION_SWITCHUSER]	= { "gtk-quit",		    "system-switch-user",	    "gnome-session-switch"	},
	[LOGOUT_ACTION_SWITCHDESK]	= { "gtk-quit",		    "system-switch-user",	    "gnome-session-switch"	},
	[LOGOUT_ACTION_LOCKSCREEN]	= { "gtk-redo",		    "gtk-refresh",		    "gtk-refresh"		},
	[LOGOUT_ACTION_LOGOUT]		= { "gtk-missing-image",    "system-lock-screen",	    "gnome-lock-screen"		},
	[LOGOUT_ACTION_RESTART]		= { "gtk-quit",		    "system-log-out",		    "gnome-logout"		},
	[LOGOUT_ACTION_CANCEL]		= { "gtk-cancel",	    "gtk-cancel",		    "gtk-cancel"		},
	/* *INDENT-ON* */
};

/** @brief find an icon
  * @param size - icon size
  * @param icons - choices (fallback stock icon name first)
  * @param count - count of choices
  *
  * Retrieve a GtkImage for a specified icon with size.  Possible names, in
  * order of preference (with fallback stock name first) are provides along with
  * the count of choices.
  */
static GtkWidget *
get_icon(GtkIconSize size, const char *icons[], int count)
{
	GtkIconTheme *theme = gtk_icon_theme_get_default();
	GtkWidget *image = NULL;
	int i;

	for (i = 1; i < count; i++)
		if (icons[i] && gtk_icon_theme_has_icon(theme, icons[i])
		    && (image = gtk_image_new_from_icon_name(icons[i], size)))
			break;
	if (!image && icons[0])
		image = gtk_image_new_from_stock(icons[0], size);
	return (image);
}

static void action_PowerOff(void);
static void action_Reboot(void);
static void action_Suspend(void);
static void action_Hibernate(void);
static void action_HybridSleep(void);
static void action_SwitchUser(void);
static void action_SwitchDesk(void);
static void action_LockScreen(void);
static void action_Logout(void);
static void action_Restart(void);
static void action_Cancel(void);

typedef void (*ActionFunctionPointer) (void);

static const ActionFunctionPointer logout_actions[LOGOUT_ACTION_COUNT] = {
	/* *INDENT-OFF* */
	[LOGOUT_ACTION_POWEROFF]	= &action_PowerOff,
	[LOGOUT_ACTION_REBOOT]		= &action_Reboot,
	[LOGOUT_ACTION_SUSPEND]		= &action_Suspend,
	[LOGOUT_ACTION_HIBERNATE]	= &action_Hibernate,
	[LOGOUT_ACTION_HYBRIDSLEEP]	= &action_HybridSleep,
	[LOGOUT_ACTION_SWITCHUSER]	= &action_SwitchUser,
	[LOGOUT_ACTION_SWITCHDESK]	= &action_SwitchDesk,
	[LOGOUT_ACTION_LOCKSCREEN]	= &action_LockScreen,
	[LOGOUT_ACTION_LOGOUT]		= &action_Logout,
	[LOGOUT_ACTION_RESTART]		= &action_Restart,
	[LOGOUT_ACTION_CANCEL]		= &action_Cancel,
	/* *INDENT-ON* */
};

static const char *check_message[LOGOUT_ACTION_COUNT] = {
	/* *INDENT-OFF* */
	[LOGOUT_ACTION_POWEROFF]	= "power off",
	[LOGOUT_ACTION_REBOOT]		= "reboot",
	[LOGOUT_ACTION_SUSPEND]		= "suspend",
	[LOGOUT_ACTION_HIBERNATE]	= "hibernate",
	[LOGOUT_ACTION_HYBRIDSLEEP]	= "hybrid sleep",
	[LOGOUT_ACTION_SWITCHUSER]	= NULL,
	[LOGOUT_ACTION_SWITCHDESK]	= NULL,
	[LOGOUT_ACTION_LOCKSCREEN]	= NULL,
	[LOGOUT_ACTION_LOGOUT]		= NULL,
	[LOGOUT_ACTION_RESTART]		= NULL,
	[LOGOUT_ACTION_CANCEL]		= NULL,
	/* *INDENT-ON* */
};

static void
on_clicked(GtkButton *button, gpointer data)
{
	LogoutActionResult action = (typeof(action)) (long) data;

	if (action_can[action] < AvailStatusChallenge) {
		EPRINTF("Button %s is disabled!\n", button_labels[action]);
		return;
	}
	if (check_message[action] && action_can[action] != AvailStatusChallenge) {
		gchar *message;
		GtkWindow *top;
		gboolean result;

		message = g_strdup_printf("Are you sure you want to %s the computer?",
					  check_message[action]);
		top = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
		result = areyousure(top, message);
		g_free(message);

		if (!result)
			return;
	}
	action_result = action;
	gtk_main_quit();
}

LogoutActionResult
make_logout_choice()
{
	gtk_init(NULL, NULL);

	GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	g_signal_connect(G_OBJECT(w), "delete-event", G_CALLBACK(on_delete_event), (gpointer) NULL);
	gtk_window_set_wmclass(GTK_WINDOW(w), "xde-logout", "XDE-Logout");
	gtk_window_set_title(GTK_WINDOW(w), "XDE Session Logout");
	gtk_window_set_modal(GTK_WINDOW(w), TRUE);
	gtk_window_set_gravity(GTK_WINDOW(w), GDK_GRAVITY_CENTER);
	gtk_window_set_type_hint(GTK_WINDOW(w), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
	gtk_window_set_icon_name(GTK_WINDOW(w), "xdm");
	gtk_container_set_border_width(GTK_CONTAINER(w), 15);
	gtk_window_set_skip_pager_hint(GTK_WINDOW(w), TRUE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(w), TRUE);
	gtk_window_set_position(GTK_WINDOW(w), GTK_WIN_POS_CENTER_ALWAYS);
	gtk_window_fullscreen(GTK_WINDOW(w));
	gtk_window_set_decorated(GTK_WINDOW(w), FALSE);

	GdkScreen *scrn = gdk_screen_get_default();
	GdkWindow *root = gdk_screen_get_root_window(scrn);
	gint width = gdk_screen_get_width(scrn);
	gint height = gdk_screen_get_height(scrn);

	gtk_window_set_default_size(GTK_WINDOW(w), width, height);
	gtk_widget_set_app_paintable(GTK_WIDGET(w), TRUE);

	GdkPixbuf *pixbuf = gdk_pixbuf_get_from_drawable(NULL, GDK_DRAWABLE(root),
							 NULL, 0, 0, 0, 0, width, height);

	GtkWidget *a = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);

	gtk_container_add(GTK_CONTAINER(w), GTK_WIDGET(a));

	GtkWidget *e = gtk_event_box_new();

	gtk_container_add(GTK_CONTAINER(a), GTK_WIDGET(e));
	gtk_widget_set_size_request(GTK_WIDGET(e), -1, -1);
	g_signal_connect(G_OBJECT(w), "expose-event", G_CALLBACK(on_expose_event),
			 (gpointer) pixbuf);

	GtkWidget *v = gtk_vbox_new(FALSE, 5);

	gtk_container_set_border_width(GTK_CONTAINER(v), 15);
	gtk_container_add(GTK_CONTAINER(e), GTK_WIDGET(v));

	GtkWidget *h = gtk_hbox_new(FALSE, 5);

	gtk_container_add(GTK_CONTAINER(v), GTK_WIDGET(h));

	if (options.banner) {
		GtkWidget *f = gtk_frame_new(NULL);

		gtk_frame_set_shadow_type(GTK_FRAME(f), GTK_SHADOW_ETCHED_IN);
		gtk_box_pack_start(GTK_BOX(h), GTK_WIDGET(f), FALSE, FALSE, 0);

		GtkWidget *v = gtk_vbox_new(FALSE, 5);

		gtk_container_set_border_width(GTK_CONTAINER(v), 10);
		gtk_container_add(GTK_CONTAINER(f), GTK_WIDGET(v));

		GtkWidget *s = gtk_image_new_from_file(options.banner);

		if (s)
			gtk_container_add(GTK_CONTAINER(v), GTK_WIDGET(s));
		else
			gtk_widget_destroy(GTK_WIDGET(f));
	}

	GtkWidget *f = gtk_frame_new(NULL);

	gtk_frame_set_shadow_type(GTK_FRAME(f), GTK_SHADOW_ETCHED_IN);
	gtk_box_pack_start(GTK_BOX(h), GTK_WIDGET(f), TRUE, TRUE, 0);
	v = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(v), 10);
	gtk_container_add(GTK_CONTAINER(f), GTK_WIDGET(v));

	GtkWidget *l = gtk_label_new(NULL);

	gtk_label_set_markup(GTK_LABEL(l), options.prompt);
	gtk_box_pack_start(GTK_BOX(v), GTK_WIDGET(l), FALSE, TRUE, 0);

	GtkWidget *bb = gtk_vbutton_box_new();

	gtk_container_set_border_width(GTK_CONTAINER(bb), 5);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bb), GTK_BUTTONBOX_SPREAD);
	gtk_box_set_spacing(GTK_BOX(bb), 5);
	gtk_box_pack_end(GTK_BOX(v), GTK_WIDGET(bb), FALSE, TRUE, 0);

	/* FIXME: fill out buttons */
	GtkWidget *b, *im;
	int i;

	for (i = 0; i < LOGOUT_ACTION_COUNT; i++) {
		b = buttons[i] = gtk_button_new();
		gtk_container_set_border_width(GTK_CONTAINER(b), 2);
		gtk_button_set_image_position(GTK_BUTTON(b), GTK_POS_LEFT);
		gtk_button_set_alignment(GTK_BUTTON(b), 0.0, 0.5);
		im = get_icon(GTK_ICON_SIZE_BUTTON, button_icons[i], 3);
		gtk_button_set_image(GTK_BUTTON(b), GTK_WIDGET(im));
		gtk_button_set_label(GTK_BUTTON(b), button_labels[i]);
		g_signal_connect(G_OBJECT(b), "clicked", G_CALLBACK(on_clicked),
				 (gpointer) (long) i);
		gtk_widget_set_sensitive(GTK_WIDGET(b), (action_can[i] >= AvailStatusChallenge));
		gtk_widget_set_tooltip_text(GTK_WIDGET(b), button_tips[i]);
		gtk_box_pack_start(GTK_BOX(bb), GTK_WIDGET(b), TRUE, TRUE, 5);
	}
	// gtk_window_set_default_size(GTK_WINDOW(w), -1, 350);

	/* most of this is just in case override-redirect fails */
	gtk_window_set_focus_on_map(GTK_WINDOW(w), TRUE);
	gtk_window_set_accept_focus(GTK_WINDOW(w), TRUE);
	gtk_window_set_keep_above(GTK_WINDOW(w), TRUE);
	gtk_window_set_modal(GTK_WINDOW(w), TRUE);
	gtk_window_stick(GTK_WINDOW(w));
	gtk_window_deiconify(GTK_WINDOW(w));
	gtk_widget_show_all(GTK_WIDGET(w));
	gtk_widget_grab_focus(GTK_WIDGET(buttons[LOGOUT_ACTION_LOGOUT]));

	gtk_widget_realize(GTK_WIDGET(w));
	GdkWindow *win = gtk_widget_get_window(GTK_WIDGET(w));

	gdk_window_set_override_redirect(win, TRUE);
	grabbed_window(GTK_WINDOW(w));

	/* TODO: we should really set a timeout and if no user interaction has
	   occured before the timeout, we should continue if we have a viable
	   default or choice (such as logout or lockscreen). */
	gtk_main();
	gtk_widget_destroy(GTK_WIDGET(w));
	return action_result;
}

void
run_logout(int argc, char *argv[])
{
	LogoutActionResult result;
	int i;

	/* determine which functions are available */
	test_power_functions();
	test_lock_screen_program();
	test_user_functions();
	test_session_functions();

	/* adjust the tooltips for the functions */
	if (options.debug) {
		for (i = 0; i < LOGOUT_ACTION_COUNT; i++) {
			switch (action_can[i]) {
			case AvailStatusUndef:
				button_tips[i] = g_strdup_printf("\nFunction undefined."
								 "\nCan value was %s",
								 "(undefined)");
				break;
			case AvailStatusUnknown:
				button_tips[i] = g_strdup_printf("\nFunction unknown."
								 "\nCan value was %s", "(unknown)");
				break;
			case AvailStatusNa:
				button_tips[i] = g_strdup_printf("\nFunction not available."
								 "\nCan value was %s", "na");
				break;
			case AvailStatusNo:
				button_tips[i] = g_strdup_printf("\n%s"
								 "\nCan value was %s",
								 button_tips[i], "no");
				break;
			case AvailStatusChallenge:
				button_tips[i] = g_strdup_printf("\n%s"
								 "\nCan value was %s",
								 button_tips[i], "challenge");
				break;
			case AvailStatusYes:
				button_tips[i] = g_strdup_printf("\n%s"
								 "\nCan value was %s",
								 button_tips[i], "yes");
				break;
			}
		}
	}
	result = make_logout_choice(argc, argv);
	if (logout_actions[result])
		(*logout_actions[result]) ();
	else {
		EPRINTF("No action for choice %d\n", result);
		exit(EXIT_FAILURE);
	}
}

static void
action_PowerOff(void)
{
	DBusGConnection *bus;
	DBusGProxy *proxy;
	gboolean ok;

	if (!(bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL))) {
		EPRINTF("cannot access the system bus\n");
		return;
	}
	proxy = dbus_g_proxy_new_for_name(bus,
					  "org.freedesktop.login1",
					  "/org/freedesktp/login1",
					  "org.freedesktop.login1.Manager");
	if (!proxy) {
		EPRINTF("cannot create DBUS proxy\n");
		return;
	}
	ok = dbus_g_proxy_call(proxy, "PowerOff", NULL,
			       G_TYPE_BOOLEAN, TRUE, G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ok) {
		EPRINTF("call to PowerOff failed\n");
		g_object_unref(G_OBJECT(proxy));
		return;
	}
	g_object_unref(G_OBJECT(proxy));
}

static void
action_Reboot(void)
{
	DBusGConnection *bus;
	DBusGProxy *proxy;
	gboolean ok;

	if (!(bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL))) {
		EPRINTF("cannot access the system bus\n");
		return;
	}
	proxy = dbus_g_proxy_new_for_name(bus,
					  "org.freedesktop.login1",
					  "/org/freedesktp/login1",
					  "org.freedesktop.login1.Manager");
	if (!proxy) {
		EPRINTF("cannot create DBUS proxy\n");
		return;
	}
	ok = dbus_g_proxy_call(proxy, "Reboot", NULL,
			       G_TYPE_BOOLEAN, TRUE, G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ok) {
		EPRINTF("call to Reboot failed\n");
		g_object_unref(G_OBJECT(proxy));
		return;
	}
	g_object_unref(G_OBJECT(proxy));
}

static void
action_Suspend(void)
{
	DBusGConnection *bus;
	DBusGProxy *proxy;
	gboolean ok;

	if (!(bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL))) {
		EPRINTF("cannot access the system bus\n");
		return;
	}
	proxy = dbus_g_proxy_new_for_name(bus,
					  "org.freedesktop.login1",
					  "/org/freedesktp/login1",
					  "org.freedesktop.login1.Manager");
	if (!proxy) {
		EPRINTF("cannot create DBUS proxy\n");
		return;
	}
	ok = dbus_g_proxy_call(proxy, "Suspend", NULL,
			       G_TYPE_BOOLEAN, TRUE, G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ok) {
		EPRINTF("call to Suspend failed\n");
		g_object_unref(G_OBJECT(proxy));
		return;
	}
	g_object_unref(G_OBJECT(proxy));
}

static void
action_Hibernate(void)
{
	DBusGConnection *bus;
	DBusGProxy *proxy;
	gboolean ok;

	if (!(bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL))) {
		EPRINTF("cannot access the system bus\n");
		return;
	}
	proxy = dbus_g_proxy_new_for_name(bus,
					  "org.freedesktop.login1",
					  "/org/freedesktp/login1",
					  "org.freedesktop.login1.Manager");
	if (!proxy) {
		EPRINTF("cannot create DBUS proxy\n");
		return;
	}
	ok = dbus_g_proxy_call(proxy, "Hibernate", NULL,
			       G_TYPE_BOOLEAN, TRUE, G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ok) {
		EPRINTF("call to Hibernate failed\n");
		g_object_unref(G_OBJECT(proxy));
		return;
	}
	g_object_unref(G_OBJECT(proxy));
}

static void
action_HybridSleep(void)
{
	DBusGConnection *bus;
	DBusGProxy *proxy;
	gboolean ok;

	if (!(bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL))) {
		EPRINTF("cannot access the system bus\n");
		return;
	}
	proxy = dbus_g_proxy_new_for_name(bus,
					  "org.freedesktop.login1",
					  "/org/freedesktp/login1",
					  "org.freedesktop.login1.Manager");
	if (!proxy) {
		EPRINTF("cannot create DBUS proxy\n");
		return;
	}
	ok = dbus_g_proxy_call(proxy, "HybridSleep", NULL,
			       G_TYPE_BOOLEAN, TRUE, G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ok) {
		EPRINTF("call to HybridSleep failed\n");
		g_object_unref(G_OBJECT(proxy));
		return;
	}
	g_object_unref(G_OBJECT(proxy));
}

static void
action_SwitchUser(void)
{
	/* not implemented yet */
	return;
}

static void
action_SwitchDesk(void)
{
	/* not implemented yet */
	return;
}

static void
action_LockScreen(void)
{
}

/** @brief perform the logout action
  *
  * Performs a complicated sequence of checks to log out of the current session.
  * This method supports more than just XDE sessions (L<lxsession(1)> and other
  * sessions are supported).
  *
  */
static void
action_Logout(void)
{
	const char *env;
	pid_t pid;

	/* Check for _LXSESSION_PID, _FBSESSION_PID, XDG_SESSION_PID.  When one 
	   of these exists, logging out of the session consists of sending a
	   TERM signal to the pid concerned.  When none of these exists, then
	   we can check to see if there is information on the root window. */
	if ((env = getenv("XDG_SESSION_PID")) && (pid = atoi(env))) {
		/* NOTE: we might actually be killing ourselves here...  */
		if (kill(pid, SIGTERM) == 0)
			return;
		fprintf(stderr, "kill: could not kill XDG_SESSION_PID %d: %s\n", (int) pid,
			strerror(errno));
	}
	if ((env = getenv("_FBSESSION_PID")) && (pid = atoi(env))) {
		/* NOTE: we might actually be killing ourselves here...  */
		if (kill(pid, SIGTERM) == 0)
			return;
		fprintf(stderr, "kill: could not kill _FBSESSION_PID %d: %s\n", (int) pid,
			strerror(errno));
	}
	if ((env = getenv("_LXSESSION_PID")) && (pid = atoi(env))) {
		/* NOTE: we might actually be killing ourselves here...  */
		if (kill(pid, SIGTERM) == 0)
			return;
		fprintf(stderr, "kill: could not kill _LXSESSION_PID %d: %s\n", (int) pid,
			strerror(errno));
	}

	GdkDisplayManager *mgr = gdk_display_manager_get();
	GdkDisplay *disp = gdk_display_manager_get_default_display(mgr);
	GdkScreen *scrn = gdk_display_get_default_screen(disp);
	GdkWindow *root = gdk_screen_get_root_window(scrn);

	/* When the _BLACKBOX_PID atom is set on the desktop, that is the PID
	   of the FLUXBOX window manager.  Actually it is me that is setting
	   _BLACKBOX_PID using the fluxbox init file rootCommand resource. */
	GdkAtom atom, actual;
	gint format, length;
	guchar *data;

	if ((atom = gdk_atom_intern("_BLACKBOX_PID", TRUE)) != GDK_NONE) {
		if (gdk_property_get
		    (root, atom, GDK_NONE, 0, 1, FALSE, &actual, &format, &length, &data)
		    && format == 32 && length >= 1) {
			if ((pid = *(unsigned long *) data)) {
				/* NOTE: we might actually be killing ourselves 
				   here...  */
				if (kill(pid, SIGTERM) == 0)
					return;
				fprintf(stderr, "kill: could not kill _BLACKBOX_PID %d: %s\n",
					(int) pid, strerror(errno));
			}
		}

	}
	/* Openbox sets _OPENBOX_PID atom on the desktop.  It also sets
	   _OB_THEME to the theme name, _OB_CONFIG_FILE to the configuration
	   file in use, and _OB_VERSION to the version of openbox.
	   __NET_SUPPORTING_WM_CHECK is set to the WM window, which has
	   _NET_WM_NAME set to "Openbox". */
	if ((atom = gdk_atom_intern("_OPENBOX_PID", TRUE)) != GDK_NONE) {
		if (gdk_property_get
		    (root, atom, GDK_NONE, 0, 1, FALSE, &actual, &format, &length, &data)
		    && format == 32 && length >= 1) {
			if ((pid = *(unsigned long *) data)) {
				/* NOTE: we might actually be killing ourselves 
				   here...  */
				if (kill(pid, SIGTERM) == 0)
					return;
				fprintf(stderr, "kill: could not kill _OPENBOX_PID %d: %s\n",
					(int) pid, strerror(errno));
			}
		}
	}
	/* IceWM-Session does not set environment variables nor elements on the 
	   root. _NET_SUPPORTING_WM_CHECK is set to the WM window, which has a
	   _NET_WM_NAME set to "IceWM 1.3.7 (Linux 3.4.0-ARCH/x86_64)" but also 
	   has _NET_WM_PID set to the pid of "icewm". Note that this is not the 
	   pid of icewm-session when that is running. */
	if ((atom = gdk_atom_intern("_NET_SUPPORTING_WM_CHECK", TRUE)) != GDK_NONE) {
		if (gdk_property_get
		    (root, atom, GDK_NONE, 0, 1, FALSE, &actual, &format, &length, &data)
		    && format == 32 && length >= 1) {
			Window xid = *(unsigned long *) data;
			GdkWindow *win = gdk_window_foreign_new(xid);

			if ((atom = gdk_atom_intern("_NET_WM_PID", TRUE)) != GDK_NONE) {
				if (gdk_property_get
				    (win, atom, GDK_NONE, 0, 1, FALSE, &actual, &format, &length,
				     &data) && format == 32 && length >= 1) {
					if ((pid = *(unsigned long *) data)) {
						/* NOTE: we might actually be
						   killing ourselves here...  */
						if (kill(pid, SIGTERM) == 0) {
							g_object_unref(G_OBJECT(win));
							return;
						}
						fprintf(stderr,
							"kill: could not kill _NET_WM_PID %d: %s\n",
							(int) pid, strerror(errno));
					}
				}
			}
			g_object_unref(G_OBJECT(win));
		}
	}
	/* Some window managers set _NET_WM_PID on the root window... */
	if ((atom = gdk_atom_intern("_NET_WM_PID", TRUE)) != GDK_NONE) {
		if (gdk_property_get
		    (root, atom, GDK_NONE, 0, 1, FALSE, &actual, &format, &length, &data)
		    && format == 32 && length >= 1) {
			if ((pid = *(unsigned long *) data)) {
				/* NOTE: we might actually be killing ourselves 
				   here... */
				if (kill(pid, SIGTERM) == 0)
					return;
				fprintf(stderr, "kill: could not kill _NET_WM_PID %d: %s\n",
					(int) pid, strerror(errno));
			}
		}
	}
	/* We set _XDE_WM_PID on the root window when we were invoked properly
	   to the window manager PID.  Try that next. */
	if ((atom = gdk_atom_intern("_XDE_WM_PID", TRUE)) != GDK_NONE) {
		if (gdk_property_get
		    (root, atom, GDK_NONE, 0, 1, FALSE, &actual, &format, &length, &data)
		    && format == 32 && length >= 1) {
			if ((pid = *(unsigned long *) data)) {
				/* NOTE: we might actually be killing ourselves 
				   here... */
				if (kill(pid, SIGTERM) == 0)
					return;
				fprintf(stderr, "kill: could not kill _XDE_WM_PID %d: %s\n",
					(int) pid, strerror(errno));
			}
		}
	}
	/* FIXME: we can do much better than this.  See libxde.  In fact, maybe 
	   we should use libxde. */
	fprintf(stderr, "ERROR: cannot find session or window manager PID!\n");
	return;
}

static void
action_Restart(void)
{
	/* not implemented yet */
	return;
}

static void
action_Cancel(void)
{
	/* do nothing for now */
	return;
}

static void
copying(int argc, char *argv[])
{
	if (!options.output && !options.debug)
		return;
	(void) fprintf(stdout, "\
--------------------------------------------------------------------------------\n\
%1$s\n\
--------------------------------------------------------------------------------\n\
Copyright (c) 2008-2014  Monavacon Limited <http://www.monavacon.com/>\n\
Copyright (c) 2001-2008  OpenSS7 Corporation <http://www.openss7.com/>\n\
Copyright (c) 1997-2001  Brian F. G. Bidulock <bidulock@openss7.org>\n\
\n\
All Rights Reserved.\n\
--------------------------------------------------------------------------------\n\
This program is free software: you can  redistribute it  and/or modify  it under\n\
the terms of the  GNU Affero  General  Public  License  as published by the Free\n\
Software Foundation, version 3 of the license.\n\
\n\
This program is distributed in the hope that it will  be useful, but WITHOUT ANY\n\
WARRANTY; without even  the implied warranty of MERCHANTABILITY or FITNESS FOR A\n\
PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.\n\
\n\
You should have received a copy of the  GNU Affero General Public License  along\n\
with this program.   If not, see <http://www.gnu.org/licenses/>, or write to the\n\
Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.\n\
--------------------------------------------------------------------------------\n\
U.S. GOVERNMENT RESTRICTED RIGHTS.  If you are licensing this Software on behalf\n\
of the U.S. Government (\"Government\"), the following provisions apply to you. If\n\
the Software is supplied by the Department of Defense (\"DoD\"), it is classified\n\
as \"Commercial  Computer  Software\"  under  paragraph  252.227-7014  of the  DoD\n\
Supplement  to the  Federal Acquisition Regulations  (\"DFARS\") (or any successor\n\
regulations) and the  Government  is acquiring  only the  license rights granted\n\
herein (the license rights customarily provided to non-Government users). If the\n\
Software is supplied to any unit or agency of the Government  other than DoD, it\n\
is  classified as  \"Restricted Computer Software\" and the Government's rights in\n\
the Software  are defined  in  paragraph 52.227-19  of the  Federal  Acquisition\n\
Regulations (\"FAR\")  (or any successor regulations) or, in the cases of NASA, in\n\
paragraph  18.52.227-86 of  the  NASA  Supplement  to the FAR (or any  successor\n\
regulations).\n\
--------------------------------------------------------------------------------\n\
", NAME " " VERSION);
}

static void
version(int argc, char *argv[])
{
	if (!options.output && !options.debug)
		return;
	(void) fprintf(stdout, "\
%1$s (OpenSS7 %2$s) %3$s\n\
Written by Brian Bidulock.\n\
\n\
Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013, 2014  Monavacon Limited.\n\
Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008  OpenSS7 Corporation.\n\
Copyright (c) 1997, 1998, 1999, 2000, 2001  Brian F. G. Bidulock.\n\
This is free software; see the source for copying conditions.  There is NO\n\
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\
\n\
Distributed by OpenSS7 under GNU Affero General Public License Version 3,\n\
with conditions, incorporated herein by reference.\n\
\n\
See `%1$s --copying' for copying permissions.\n\
", NAME, PACKAGE, VERSION);
}

static void
usage(int argc, char *argv[])
{
	if (!options.output && !options.debug)
		return;
	(void) fprintf(stderr, "\
Usage:\n\
    %1$s [options]\n\
    %1$s {-h|--help}\n\
    %1$s {-V|--version}\n\
    %1$s {-C|--copying}\n\
", argv[0]);
}

static const char *
show_side(LogoSide side)
{
	switch (side) {
	case LOGO_SIDE_LEFT:
		return ("left");
	case LOGO_SIDE_TOP:
		return ("top");
	case LOGO_SIDE_RIGHT:
		return ("right");
	case LOGO_SIDE_BOTTOM:
		return ("bottom");
	}
	return ("unknown");
}

static const char *
show_bool(Bool val)
{
	if (val)
		return ("true");
	return ("false");
}

static void
help(int argc, char *argv[])
{
	if (!options.output && !options.debug)
		return;
	(void) fprintf(stdout, "\
Usage:\n\
    %1$s [options]\n\
    %1$s {-h|--help}\n\
    %1$s {-V|--version}\n\
    %1$s {-C|--copying}\n\
Command options:\n\
    -h, --help, -?, --?\n\
        print this usage information and exit\n\
    -V, --version\n\
        print version and exit\n\
    -C, --copying\n\
        print copying permission and exit\n\
General options:\n\
    -p, --prompt PROMPT        (%2$s)\n\
        specifies a custom prompt message\n\
    -b, --banner BANNER        (%3$s)\n\
        specify custom login branding\n\
    -s, --side {l|t|r|b}       (%4$s)\n\
        specify side  of dialog for logo placement\n\
    -n, --noask                (%5$s)\n\
        do not ask what to do, just logout\n\
    -i, --icons THEME          (%6$s)\n\
        set the icon theme to use\n\
    -t, --theme THEME          (%7$s)\n\
        set the gtk+ theme to use\n\
    -x, --xde-theme            (%8$s)\n\
        use the XDE desktop theme for the selection window\n\
    -T, --timeout SECONDS      (%9$u sec)\n\
        set dialog timeout\n\
    -N, --dry-run              (%10$s)\n\
        do not do anything: just print it\n\
    -D, --debug [LEVEL]        (%11$d)\n\
        increment or set debug LEVEL\n\
        this option may be repeated.\n\
    -v, --verbose [LEVEL]      (%12$d)\n\
        increment or set output verbosity LEVEL\n\
        this option may be repeated.\n\
", argv[0], options.prompt, options.banner, show_side(options.side), show_bool(options.noask), options.usexde ? "xde" : (options.icon_theme ? : "auto"), options.usexde ? "xde" : (options.gtk2_theme ? : "auto"), show_bool(options.usexde), options.timeout, show_bool(options.dryrun), options.debug, options.output);
}

void
set_default_prompt(void)
{
	char *session = NULL, *prompt, *p;
	const char *s;
	int i, len;

	prompt = calloc(PATH_MAX, sizeof(*prompt));

	if ((s = getenv("XDG_CURRENT_DESKTOP")) && *s) {
		session = strdup(s);
		while ((p = strchr(session, ';')))
			*p = ':';
	} else if ((s = getenv("XDG_VENDOR_ID")) && *s) {
		session = strdup(s);
	} else if ((s = getenv("XDG_MENU_PREFIX")) && *s) {
		session = strdup(s);
		p = session + strlen(session) - 1;
		if (*p == '-')
			*p = '\0';
	} else {
		session = strdup("XDE");
	}
	len = strlen(session);
	for (i = 0, p = session; i < len; i++, p++)
		*p = toupper(*p);
	snprintf(prompt, PATH_MAX - 1, "Logout of <b>%s</b> session?", session);
	options.prompt = strdup(prompt);
	free(session);
	free(prompt);
}

void
set_default_banner(void)
{
	static const char *exts[] = { ".xpm", ".png", ".jpg", ".svg" };
	char **xdg_dirs, **dirs, *file, *pfx, *suffix;
	int i, j, n = 0;

	if (!(xdg_dirs = get_data_dirs(&n)) || !n)
		return;

	pfx = getenv("XDG_MENU_PREFIX") ? : "";

	free(options.banner);
	options.banner = NULL;

	file = calloc(PATH_MAX + 1, sizeof(*file));

	for (i = 0, dirs = &xdg_dirs[i]; i < n; i++, dirs++) {
		strncpy(file, *dirs, PATH_MAX);
		strncat(file, "/images/", PATH_MAX);
		strncat(file, pfx, PATH_MAX);
		strncat(file, "banner", PATH_MAX);
		suffix = file + strnlen(file, PATH_MAX);

		for (j = 0; j < sizeof(exts) / sizeof(exts[0]); j++) {
			strcpy(suffix, exts[j]);
			if (!access(file, R_OK)) {
				options.banner = strdup(file);
				break;
			}
		}
		if (options.banner)
			break;
	}

	free(file);

	for (i = 0; i < n; i++)
		free(xdg_dirs[i]);
	free(xdg_dirs);
}

void
set_defaults(void)
{
	set_default_prompt();
	set_default_banner();
}

typedef enum {
	COMMAND_LOGOUT,
	COMMAND_HELP,
	COMMAND_VERSION,
	COMMAND_COPYING,
} CommandType;

int
main(int argc, char *argv[])
{
	CommandType command = COMMAND_LOGOUT;

	set_defaults();

	while (1) {
		int c, val;

#ifdef _GNU_SOURCE
		int option_index = 0;
		/* *INDENT-OFF* */
		static struct option long_options[] = {
			{"prompt",	required_argument,	NULL, 'p'},
			{"banner",	required_argument,	NULL, 'b'},
			{"side",	required_argument,	NULL, 's'},
			{"noask",	no_argument,		NULL, 'n'},
			{"icons",	required_argument,	NULL, 'i'},
			{"theme",	required_argument,	NULL, 't'},
			{"xde-theme",	no_argument,		NULL, 'x'},
			{"timeout",	required_argument,	NULL, 'T'},

			{"dry-run",	no_argument,		NULL, 'N'},
			{"debug",	optional_argument,	NULL, 'D'},
			{"verbose",	optional_argument,	NULL, 'v'},
			{"help",	no_argument,		NULL, 'h'},
			{"version",	no_argument,		NULL, 'V'},
			{"copying",	no_argument,		NULL, 'C'},
			{"?",		no_argument,		NULL, 'H'},
			{ 0, }
		};
		/* *INDENT-ON* */

		c = getopt_long_only(argc, argv, "p:b:s:nD::v::hVCH?", long_options, &option_index);
#else				/* defined _GNU_SOURCE */
		c = getopt(argc, argv, "p:b:s:nDvhVC?");
#endif				/* defined _GNU_SOURCE */
		if (c == -1) {
			if (options.debug)
				fprintf(stderr, "%s: done options processing\n", argv[0]);
			break;
		}
		switch (c) {
		case 0:
			goto bad_usage;

		case 'p':	/* -p, --prompt PROPMT */
			free(options.prompt);
			options.prompt = strdup(optarg);
			break;
		case 'b':	/* -b, --banner BANNER */
			free(options.banner);
			options.banner = strdup(optarg);
			break;
		case 's':	/* -s, --side {top|bottom|left|right} */
			if (!strncasecmp(optarg, "left", strlen(optarg))) {
				options.side = LOGO_SIDE_LEFT;
				break;
			}
			if (!strncasecmp(optarg, "top", strlen(optarg))) {
				options.side = LOGO_SIDE_TOP;
				break;
			}
			if (!strncasecmp(optarg, "right", strlen(optarg))) {
				options.side = LOGO_SIDE_RIGHT;
				break;
			}
			if (!strncasecmp(optarg, "bottom", strlen(optarg))) {
				options.side = LOGO_SIDE_BOTTOM;
				break;
			}
			goto bad_option;
		case 'n':	/* -n, --noask */
			options.noask = True;
			break;
		case 'i':	/* -i, --icons THEME */
			free(options.icon_theme);
			options.icon_theme = strdup(optarg);
			break;
		case 't':	/* -t, --theme THEME */
			free(options.gtk2_theme);
			options.gtk2_theme = strdup(optarg);
			break;
		case 'x':
			options.usexde = True;
			break;
		case 'T':
			options.timeout = strtoul(optarg, NULL, 0);
			break;

		case 'N':	/* -n, --dry-run */
			options.dryrun = True;
			break;
		case 'D':	/* -D, --debug [level] */
			if (options.debug)
				fprintf(stderr, "%s: increasing debug verbosity\n", argv[0]);
			if (optarg == NULL) {
				options.debug++;
			} else {
				if ((val = strtol(optarg, NULL, 0)) < 0)
					goto bad_option;
				options.debug = val;
			}
			break;
		case 'v':	/* -v, --verbose [level] */
			if (options.debug)
				fprintf(stderr, "%s: increasing output verbosity\n", argv[0]);
			if (optarg == NULL) {
				options.output++;
				break;
			}
			if ((val = strtol(optarg, NULL, 0)) < 0)
				goto bad_option;
			options.output = val;
			break;
		case 'h':	/* -h, --help */
		case 'H':	/* -H, --? */
			if (command != COMMAND_LOGOUT)
				goto bad_option;
			command = COMMAND_HELP;
			break;
		case 'V':	/* -V, --version */
			if (command != COMMAND_LOGOUT)
				goto bad_option;
			command = COMMAND_VERSION;
			break;
		case 'C':	/* -C, --copying */
			if (command != COMMAND_LOGOUT)
				goto bad_option;
			command = COMMAND_COPYING;
			break;
		case '?':
		default:
		      bad_option:
			optind--;
			goto bad_nonopt;
		      bad_nonopt:
			if (options.output || options.debug) {
				if (optind < argc) {
					fprintf(stderr, "%s: syntax error near '", argv[0]);
					while (optind < argc)
						fprintf(stderr, "%s ", argv[optind++]);
					fprintf(stderr, "'\n");
				} else {
					fprintf(stderr, "%s: missing option or argument", argv[0]);
					fprintf(stderr, "\n");
				}
				fflush(stderr);
			      bad_usage:
				usage(argc, argv);
			}
			exit(2);
		}
	}
	if (options.debug) {
		fprintf(stderr, "%s: option index = %d\n", argv[0], optind);
		fprintf(stderr, "%s: option count = %d\n", argv[0], argc);
	}
	if (optind < argc) {
		goto bad_nonopt;
	}
	switch (command) {
	case COMMAND_LOGOUT:
		if (options.debug)
			fprintf(stderr, "%s: running logout\n", argv[0]);
		run_logout(argc, argv);
		break;
	case COMMAND_HELP:
		if (options.debug)
			fprintf(stderr, "%s: printing help message\n", argv[0]);
		help(argc, argv);
		break;
	case COMMAND_VERSION:
		if (options.debug)
			fprintf(stderr, "%s: printing version message\n", argv[0]);
		version(argc, argv);
		break;
	case COMMAND_COPYING:
		if (options.debug)
			fprintf(stderr, "%s: printing copying message\n", argv[0]);
		copying(argc, argv);
		break;
	}
	exit(0);
}

// vim: set sw=8 tw=80 com=srO\:/**,mb\:*,ex\:*/,srO\:/*,mb\:*,ex\:*/,b\:TRANS foldmarker=@{,@} foldmethod=marker:
