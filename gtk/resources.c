/*
 * Copyright 2015 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Implementation of gtk builtin resource handling.
 *
 * This presents a unified interface to the rest of the codebase to
 * obtain resources. Note this is not anything to do with the resource
 * scheme handling beyond possibly providing the underlying data.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include "utils/log.h"
#include "utils/filepath.h"

#include "gtk/compat.h"
#include "gtk/resources.h"

/** log contents of gresource /org/netsource */
#ifdef WITH_GRESOURCE
#define SHOW_GRESOURCE
#undef SHOW_GRESOURCE
#endif

#ifdef WITH_BUILTIN_PIXBUF
#ifdef __GNUC__
extern const guint8 menu_cursor_pixdata[] __attribute__ ((__aligned__ (4)));
#else
extern const guint8 menu_cursor_pixdata[];
#endif
#endif

/** type of resource entry */
enum nsgtk_resource_type_e {
	NSGTK_RESOURCE_FILE, /**< entry is a file on disc */
	NSGTK_RESOURCE_GLIB, /**< entry is a gresource accessed by path */
	NSGTK_RESOURCE_DIRECT, /**< entry is a gresource accesed by gbytes */
	NSGTK_RESOURCE_INLINE, /**< entry is compiled in accessed by pointer */
};

/** resource entry */
struct nsgtk_resource_s {
	const char *name;
	unsigned int len;
	enum nsgtk_resource_type_e type;
	char *path;
};

#define RES_ENTRY(name) { name, sizeof((name)) - 1, NSGTK_RESOURCE_FILE, NULL }

/** resources that are used for gtk builder */
static struct nsgtk_resource_s ui_resource[] = {
	RES_ENTRY("netsurf"),
	RES_ENTRY("tabcontents"),
	RES_ENTRY("password"),
	RES_ENTRY("login"),
	RES_ENTRY("ssl"),
	RES_ENTRY("toolbar"),
	RES_ENTRY("downloads"),
	RES_ENTRY("history"),
	RES_ENTRY("options"),
	RES_ENTRY("hotlist"),
	RES_ENTRY("cookies"),
	RES_ENTRY("viewdata"),
	RES_ENTRY("warning"),
	{ NULL, 0, NSGTK_RESOURCE_FILE, NULL },
};

/** resources that are used as pixbufs */
static struct nsgtk_resource_s pixbuf_resource[] = {
	RES_ENTRY("favicon.png"),
	RES_ENTRY("netsurf.xpm"),
	RES_ENTRY("menu_cursor.png"),
	RES_ENTRY("throbber/throbber0.png"),
	RES_ENTRY("throbber/throbber1.png"),
	RES_ENTRY("throbber/throbber2.png"),
	RES_ENTRY("throbber/throbber3.png"),
	RES_ENTRY("throbber/throbber4.png"),
	RES_ENTRY("throbber/throbber5.png"),
	RES_ENTRY("throbber/throbber6.png"),
	RES_ENTRY("throbber/throbber7.png"),
	RES_ENTRY("throbber/throbber8.png"),
	{ NULL, 0, NSGTK_RESOURCE_FILE, NULL },
};

/** resources that are used for direct data access */
static struct nsgtk_resource_s direct_resource[] = {
	RES_ENTRY("welcome.html"),
	RES_ENTRY("credits.html"),
	RES_ENTRY("licence.html"),
	RES_ENTRY("maps.html"),
	RES_ENTRY("internal.css"),
	RES_ENTRY("netsurf.png"),
	RES_ENTRY("default.ico"),
	RES_ENTRY("icons/arrow-l.png"),
	RES_ENTRY("icons/content.png"),
	RES_ENTRY("icons/directory2.png"),
	RES_ENTRY("icons/directory.png"),
	RES_ENTRY("icons/hotlist-add.png"),
	RES_ENTRY("icons/hotlist-rmv.png"),
	RES_ENTRY("icons/search.png"),
	{ NULL, 0, NSGTK_RESOURCE_FILE, NULL },
};


/* exported interface documented in gtk/resources.h */
GdkCursor *nsgtk_create_menu_cursor(void)
{
	GdkCursor *cursor = NULL;
	GdkPixbuf *pixbuf;
	nserror res;
	const char *resname = "menu_cursor.png";

	res = nsgdk_pixbuf_new_from_resname(resname, &pixbuf);
	if (res == NSERROR_OK) {
		cursor = gdk_cursor_new_from_pixbuf(gdk_display_get_default(),
						    pixbuf, 0, 3);
		g_object_unref(pixbuf);
	}

	return cursor;
}


/**
 * locate a resource
 *
 * The way GTK accesses resource files has changed greatly between
 * releases. This initilises the interface that hides all the
 * implementation details from the rest of the code.
 *
 * If the GResource is not enabled or the item cannot be found in the
 * compiled in resources the files will be loaded directly from disc
 * instead.
 *
 * \param respath A string vector containing the valid resource search paths
 * \param resource A resource entry to initialise
 */
static nserror
init_resource(char **respath, struct nsgtk_resource_s *resource)
{
	char *resname;
#ifdef WITH_GRESOURCE
	int resnamelen;
	gboolean present;
	const gchar * const *langv;
	int langc = 0;

	langv = g_get_language_names();

	while (langv[langc] != NULL) {
		resnamelen = snprintf(NULL, 0,
				      "/org/netsurf/%s/%s",
				      langv[langc], resource->name);

		resname = malloc(resnamelen + 1);
		if (resname == NULL) {
			return NSERROR_NOMEM;
		}
		snprintf(resname, resnamelen + 1,
			 "/org/netsurf/%s/%s",
			 langv[langc], resource->name);

		present = g_resources_get_info(resname,
					       G_RESOURCE_LOOKUP_FLAGS_NONE,
					       NULL, NULL, NULL);
		if (present == TRUE) {
			/* found an entry in the resources */
			resource->path = resname;
			resource->type = NSGTK_RESOURCE_GLIB;
			LOG("Found gresource path %s", resource->path);
			return NSERROR_OK;
		}
		/*LOG("gresource \"%s\" not found", resname);*/
		free(resname);

		langc++;
	}
	resnamelen = snprintf(NULL, 0, "/org/netsurf/%s", resource->name);

	resname = malloc(resnamelen + 1);
	if (resname == NULL) {
		return NSERROR_NOMEM;
	}
	snprintf(resname, resnamelen + 1, "/org/netsurf/%s", resource->name);

	present = g_resources_get_info(resname,
				       G_RESOURCE_LOOKUP_FLAGS_NONE,
				       NULL, NULL, NULL);
	if (present == TRUE) {
		/* found an entry in the resources */
		resource->path = resname;
		resource->type = NSGTK_RESOURCE_GLIB;
		LOG("Found gresource path %s", resource->path);
		return NSERROR_OK;
	}
	/*LOG("gresource \"%s\" not found", resname);*/
	free(resname);

#endif

	resname = filepath_find(respath, resource->name);
	if (resname == NULL) {
		LOG("Unable to find resource %s on resource path",
		    resource->name);
		return NSERROR_NOT_FOUND;
	}

	/* found an entry on the path */
	resource->path = resname;
	resource->type = NSGTK_RESOURCE_FILE;

	LOG("Found file resource path %s", resource->path);
	return NSERROR_OK;
}

/**
 * locate and setup a direct resource
 *
 * Direct resources have general type of NSGTK_RESOURCE_GLIB but have
 *  g_resources_lookup_data() applied and the result stored so the data
 *  can be directly accessed without additional processing.
 *
 * \param respath A string vector containing the valid resource search paths
 * \param resource A resource entry to initialise
 */
static nserror
init_direct_resource(char **respath, struct nsgtk_resource_s *resource)
{
	nserror res;
	GBytes *data;

	res = init_resource(respath, resource);
	if ((res == NSERROR_OK) &&
	    (resource->type == NSGTK_RESOURCE_GLIB)) {
		/* found gresource we can convert */
		data = g_resources_lookup_data(resource->path,
					       G_RESOURCE_LOOKUP_FLAGS_NONE,
					       NULL);
		if (data != NULL) {
			resource->type = NSGTK_RESOURCE_DIRECT;
			resource->path = (char *)data;
		}
	}

	return res;
}

/**
 * locate a pixbuf resource
 *
 * Pixbuf resources can be compiled inline
 *
 * \param respath A string vector containing the valid resource search paths
 * \param resource A resource entry to initialise
 */
static nserror
init_pixbuf_resource(char **respath, struct nsgtk_resource_s *resource)
{
#ifdef WITH_BUILTIN_PIXBUF
	if (strncmp(resource->name, "menu_cursor.png", resource->len) == 0) {
		resource->path = (char *)&menu_cursor_pixdata[0];
		resource->type = NSGTK_RESOURCE_INLINE;
		LOG("Found builtin for %s", resource->name);
		return NSERROR_OK;
	}
#endif
	return init_resource(respath, resource);
}

/**
 * locate a ui resource
 *
 * UI resources need their resource name changing to account for gtk versions
 *
 * \param respath A string vector containing the valid resource search paths
 * \param ui_res A resource entry to initialise
 */
static nserror init_ui_resource(char **respath, struct nsgtk_resource_s *ui_res)
{
#if GTK_CHECK_VERSION(3,0,0)
	int gtkv = 3;
#else
	int gtkv = 2;
#endif
	int resnamelen;
	char *resname;
	struct nsgtk_resource_s resource;
	nserror res;

	resnamelen = ui_res->len + 10; /* allow for the expanded ui name */

	resname = malloc(resnamelen);
	if (resname == NULL) {
		return NSERROR_NOMEM;
	}
	snprintf(resname, resnamelen, "%s.gtk%d.ui", ui_res->name, gtkv);
	resource.name = resname;
	resource.len = ui_res->len;
	resource.path = NULL;

	res = init_resource(respath, &resource);

	ui_res->path = resource.path;
	ui_res->type = resource.type;

	free(resname);

	return res;
}

/**
 * Find a resource entry by name.
 *
 * \param resname The resource name to match.
 * \param resource The list of resources entries to search.
 */
static struct nsgtk_resource_s *
find_resource_from_name(const char *resname, struct nsgtk_resource_s *resource)
{
	/* find resource from name */
	while ((resource->name != NULL) &&
	       ((resname[0] != resource->name[0]) ||
		(strncmp(resource->name, resname, resource->len) != 0))) {
		resource++;
	}
	return resource;
}

#ifdef SHOW_GRESOURCE
/**
 * Debug dump of all resources compile din via GResource.
 */
static void list_gresource(void)
{
	const char *nspath = "/org/netsurf";
	char **reslist;
	char **cur;
	GError* gerror = NULL;
	reslist = g_resources_enumerate_children(nspath,
						 G_RESOURCE_LOOKUP_FLAGS_NONE,
						 &gerror);
	if (gerror) {
		LOG("gerror %s", gerror->message);
		g_error_free(gerror);

	} else {
		cur = reslist;
		while (cur != NULL && *cur != NULL) {
			LOG("gres %s", *cur);
			cur++;
		}
		g_strfreev(reslist);
	}
}
#endif

/**
 * Initialise UI resource table
 *
 */
/* exported interface documented in gtk/resources.h */
nserror nsgtk_init_resources(char **respath)
{
	struct nsgtk_resource_s *resource;
	nserror res;

#ifdef SHOW_GRESOURCE
	list_gresource();
#endif

	/* iterate the ui resource table and initialise all its members */
	resource = &ui_resource[0];
	while (resource->name != NULL) {
		res = init_ui_resource(respath, resource);
		if (res != NSERROR_OK) {
			return res;
		}
		resource++;
	}

	/* iterate the pixbuf resource table and initialise all its members */
	resource = &pixbuf_resource[0];
	while (resource->name != NULL) {
		res = init_pixbuf_resource(respath, resource);
		if (res != NSERROR_OK) {
			return res;
		}
		resource++;
	}

	/* iterate the direct resource table and initialise all its members */
	resource = &direct_resource[0];
	while (resource->name != NULL) {
		res = init_direct_resource(respath, resource);
		if (res != NSERROR_OK) {
			return res;
		}
		resource++;
	}

	return NSERROR_OK;
}


/* exported interface documented in gtk/resources.h */
nserror
nsgdk_pixbuf_new_from_resname(const char *resname, GdkPixbuf **pixbuf_out)
{
	struct nsgtk_resource_s *resource;
	GdkPixbuf *new_pixbuf = NULL;
	GError* error = NULL;

	resource = find_resource_from_name(resname, &pixbuf_resource[0]);
	if (resource->name == NULL) {
		return NSERROR_NOT_FOUND;
	}

	switch (resource->type) {
	case NSGTK_RESOURCE_FILE:
		new_pixbuf = gdk_pixbuf_new_from_file(resource->path, &error);
		break;

	case NSGTK_RESOURCE_GLIB:
		new_pixbuf = gdk_pixbuf_new_from_resource(resource->path, &error);
		break;

	case NSGTK_RESOURCE_INLINE:
#ifdef WITH_BUILTIN_PIXBUF
		new_pixbuf = gdk_pixbuf_new_from_inline(-1, (const guint8 *)resource->path, FALSE, &error);
#endif
		break;

	case NSGTK_RESOURCE_DIRECT:
		/* pixbuf resources are not currently direct */
		break;
	}
	if (new_pixbuf == NULL) {
		LOG("Unable to create pixbuf from file for %s with path %s \"%s\"",
			    resource->name, resource->path, error->message);
		g_error_free(error);
		return NSERROR_INIT_FAILED;
	}
	*pixbuf_out = new_pixbuf;

	return NSERROR_OK;
}

/* exported interface documented in gtk/resources.h */
nserror
nsgtk_builder_new_from_resname(const char *resname, GtkBuilder **builder_out)
{
	GtkBuilder *new_builder;
	struct nsgtk_resource_s *ui_res;
	GError* error = NULL;

	ui_res = find_resource_from_name(resname, &ui_resource[0]);
	if (ui_res->name == NULL) {
		return NSERROR_NOT_FOUND;
	}

	new_builder = gtk_builder_new();

	if (ui_res->type == NSGTK_RESOURCE_FILE) {
		if (!gtk_builder_add_from_file(new_builder,
					       ui_res->path,
					       &error)) {
			LOG("Unable to add UI builder from file for %s with path %s \"%s\"",
			    ui_res->name, ui_res->path, error->message);
			g_error_free(error);
			g_object_unref(G_OBJECT(new_builder));
			return NSERROR_INIT_FAILED;
		}
	} else {
		if (!nsgtk_builder_add_from_resource(new_builder,
						     ui_res->path,
						     &error)) {
			LOG("Unable to add UI builder from resource for %s with path %s \"%s\"",
			    ui_res->name, ui_res->path, error->message);
			g_error_free(error);
			g_object_unref(G_OBJECT(new_builder));
			return NSERROR_INIT_FAILED;
		}
	}

	*builder_out = new_builder;

	return NSERROR_OK;
}

/* exported interface documented in gtk/resources.h */
nserror
nsgtk_data_from_resname(const char *resname,
			const uint8_t ** data_out,
			size_t *data_size_out)
{
	struct nsgtk_resource_s *resource;
	GBytes *data;
	const gchar *buffer;
	gsize buffer_length;

	resource = find_resource_from_name(resname, &direct_resource[0]);
	if ((resource->name == NULL) ||
	    (resource->type != NSGTK_RESOURCE_DIRECT)) {
		return NSERROR_NOT_FOUND;
	}

	data = (GBytes *)resource->path;

	buffer_length = 0;
	buffer = g_bytes_get_data(data, &buffer_length);

	if (buffer == NULL) {
		return NSERROR_NOMEM;
	}

	*data_out = (const uint8_t *)buffer;
	*data_size_out = (size_t)buffer_length;

	return NSERROR_OK;
}
