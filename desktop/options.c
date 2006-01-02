/*
 * This file is part of NetSurf, http://netsurf.sourceforge.net/
 * Licensed under the GNU General Public License,
 *                http://www.opensource.org/licenses/gpl-license
 * Copyright 2003 Phil Mellor <monkeyson@users.sourceforge.net>
 * Copyright 2003 John M Bell <jmb202@ecs.soton.ac.uk>
 * Copyright 2004 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2005 Richard Wilson <info@tinct.net>
 */

/** \file
 * Option reading and saving (implementation).
 *
 * Options are stored in the format key:value, one per line. For bool options,
 * value is "0" or "1".
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "libxml/HTMLparser.h"
#include "libxml/HTMLtree.h"
#include "netsurf/css/css.h"
#include "netsurf/desktop/options.h"
#include "netsurf/desktop/tree.h"
#include "netsurf/utils/log.h"
#include "netsurf/utils/messages.h"
#include "netsurf/utils/utils.h"

#ifdef riscos
#include "netsurf/riscos/options.h"
#else
#define EXTRA_OPTION_DEFINE
#define EXTRA_OPTION_TABLE
#endif


/** An HTTP proxy should be used. */
bool option_http_proxy = false;
/** Hostname of proxy. */
char *option_http_proxy_host = 0;
/** Proxy port. */
int option_http_proxy_port = 8080;
/** Proxy authentication method. */
int option_http_proxy_auth = OPTION_HTTP_PROXY_AUTH_NONE;
/** Proxy authentication user name */
char *option_http_proxy_auth_user = 0;
/** Proxy authentication password */
char *option_http_proxy_auth_pass = 0;
/** Default font size / 0.1pt. */
int option_font_size = 100;
/** Minimum font size. */
int option_font_min_size = 70;
/** Accept-Language header. */
char *option_accept_language = 0;
/** Enable verification of SSL certificates. */
bool option_ssl_verify_certificates = true;
/** Preferred maximum size of memory cache / bytes. */
int option_memory_cache_size = 2 * 1024 * 1024;
/** Whether to block advertisements */
bool option_block_ads = false;
/** Minimum GIF animation delay */
int option_minimum_gif_delay = 10;
/** Whether to send the referer HTTP header */
bool option_send_referer = true;
/** Whether to animate images */
bool option_animate_images = true;
/** How many days to retain URL data for */
int option_expire_url = 28;
/** Default font family */
int option_font_default = CSS_FONT_FAMILY_SANS_SERIF;

EXTRA_OPTION_DEFINE


struct {
	const char *key;
	enum { OPTION_BOOL, OPTION_INTEGER, OPTION_STRING } type;
	void *p;
} option_table[] = {
	{ "http_proxy",      OPTION_BOOL,    &option_http_proxy },
	{ "http_proxy_host", OPTION_STRING,  &option_http_proxy_host },
	{ "http_proxy_port", OPTION_INTEGER, &option_http_proxy_port },
	{ "http_proxy_auth", OPTION_BOOL,    &option_http_proxy_auth },
	{ "http_proxy_auth_user", OPTION_STRING, &option_http_proxy_auth_user },
	{ "http_proxy_auth_pass", OPTION_STRING, &option_http_proxy_auth_pass },
	{ "font_size",       OPTION_INTEGER, &option_font_size },
	{ "font_min_size",   OPTION_INTEGER, &option_font_min_size },
	{ "accept_language", OPTION_STRING,  &option_accept_language },
	{ "ssl_verify_certificates", OPTION_BOOL, &option_ssl_verify_certificates },
	{ "memory_cache_size", OPTION_INTEGER, &option_memory_cache_size },
	{ "block_advertisements", OPTION_BOOL, &option_block_ads },
	{ "minimum_gif_delay",      OPTION_INTEGER, &option_minimum_gif_delay },
	{ "send_referer",    OPTION_BOOL,    &option_send_referer },
	{ "animate_images",  OPTION_BOOL,    &option_animate_images },
	{ "expire_url",	     OPTION_INTEGER, &option_expire_url },
	{ "font_default",           OPTION_INTEGER, &option_font_default },
	EXTRA_OPTION_TABLE
};

#define option_table_entries (sizeof option_table / sizeof option_table[0])


static void options_load_tree_directory(xmlNode *ul, struct node *directory);
static void options_load_tree_entry(xmlNode *li, struct node *directory);
xmlNode *options_find_tree_element(xmlNode *node, const char *name);
bool options_save_tree_directory(struct node *directory, xmlNode *node);
bool options_save_tree_entry(struct node *entry, xmlNode *node);


/**
 * Read options from a file.
 *
 * \param  path  name of file to read options from
 *
 * Option variables corresponding to lines in the file are updated. Missing
 * options are unchanged. If the file fails to open, options are unchanged.
 */

void options_read(const char *path)
{
	char s[100];
	FILE *fp;

	fp = fopen(path, "r");
	if (!fp) {
		LOG(("failed to open file '%s'", path));
		return;
	}

	while (fgets(s, 100, fp)) {
		char *colon, *value;
		unsigned int i;

		if (s[0] == 0 || s[0] == '#')
			continue;
		colon = strchr(s, ':');
		if (colon == 0)
			continue;
		s[strlen(s) - 1] = 0;  /* remove \n at end */
		*colon = 0;  /* terminate key */
		value = colon + 1;

		for (i = 0; i != option_table_entries; i++) {
			if (strcasecmp(s, option_table[i].key) != 0)
				continue;

			switch (option_table[i].type) {
				case OPTION_BOOL:
					*((bool *) option_table[i].p) =
							value[0] == '1';
					break;

				case OPTION_INTEGER:
					*((int *) option_table[i].p) =
							atoi(value);
					break;

				case OPTION_STRING:
					free(*((char **) option_table[i].p));
					*((char **) option_table[i].p) =
							strdup(value);
					break;
			}
			break;
		}
	}

	fclose(fp);

	if (option_font_size < 50)
		option_font_size = 50;
	if (1000 < option_font_size)
		option_font_size = 1000;
	if (option_font_min_size < 10)
		option_font_min_size = 10;
	if (500 < option_font_min_size)
		option_font_min_size = 500;

	if (option_memory_cache_size < 0)
		option_memory_cache_size = 0;
}


/**
 * Save options to a file.
 *
 * \param  path  name of file to write options to
 *
 * Errors are ignored.
 */

void options_write(const char *path)
{
	unsigned int i;
	FILE *fp;

	fp = fopen(path, "w");
	if (!fp) {
		LOG(("failed to open file '%s' for writing", path));
		return;
	}

	for (i = 0; i != option_table_entries; i++) {
		fprintf(fp, "%s:", option_table[i].key);
		switch (option_table[i].type) {
			case OPTION_BOOL:
				fprintf(fp, "%c", *((bool *) option_table[i].p) ?
						'1' : '0');
				break;

			case OPTION_INTEGER:
				fprintf(fp, "%i", *((int *) option_table[i].p));
				break;

			case OPTION_STRING:
				if (*((char **) option_table[i].p))
					fprintf(fp, "%s", *((char **) option_table[i].p));
				break;
		}
		fprintf(fp, "\n");
        }

	fclose(fp);
}

/**
 * Dump user options to stderr
 */
void options_dump(void)
{
	unsigned int i;

	for (i = 0; i != option_table_entries; i++) {
		fprintf(stderr, "%s:", option_table[i].key);
		switch (option_table[i].type) {
			case OPTION_BOOL:
				fprintf(stderr, "%c",
					*((bool *) option_table[i].p) ?
						'1' : '0');
				break;

			case OPTION_INTEGER:
				fprintf(stderr, "%i",
					*((int *) option_table[i].p));
				break;

			case OPTION_STRING:
				if (*((char **) option_table[i].p))
					fprintf(stderr, "%s",
						*((char **) option_table[i].p));
				break;
		}
		fprintf(stderr, "\n");
	}
}

/**
 * Loads a hotlist as a tree from a specified file.
 *
 * \param  filename  name of file to read
 * \return the hotlist file represented as a tree, or NULL on failure
 */
struct tree *options_load_tree(const char *filename) {
	xmlDoc *doc;
	xmlNode *html, *body, *ul;
	struct tree *tree;

	doc = htmlParseFile(filename, "iso-8859-1");
	if (!doc) {
		warn_user("HotlistLoadError", messages_get("ParsingFail"));
		return NULL;
	}

	html = options_find_tree_element((xmlNode *) doc, "html");
	body = options_find_tree_element(html, "body");
	ul = options_find_tree_element(body, "ul");
	if (!ul) {
		xmlFreeDoc(doc);
		warn_user("HotlistLoadError",
				"(<html>...<body>...<ul> not found.)");
		return NULL;
	}

	tree = calloc(sizeof(struct tree), 1);
	if (!tree) {
		xmlFreeDoc(doc);
		warn_user("NoMemory", 0);
		return NULL;
	}
	tree->root = tree_create_folder_node(NULL, "Root");
	if (!tree->root) return NULL;

	options_load_tree_directory(ul, tree->root);
	tree->root->expanded = true;
	tree_initialise(tree);

	xmlFreeDoc(doc);
	return tree;
}


/**
 * Parse a directory represented as a ul.
 *
 * \param  ul         xmlNode for parsed ul
 * \param  directory  directory to add this directory to
 */
void options_load_tree_directory(xmlNode *ul, struct node *directory) {
	char *title;
	struct node *dir;
	xmlNode *n;

	assert(ul);
	assert(directory);

	for (n = ul->children; n; n = n->next) {
		/* The ul may contain entries as a li, or directories as
		 * an h4 followed by a ul. Non-element nodes may be present
		 * (eg. text, comments), and are ignored. */

		if (n->type != XML_ELEMENT_NODE)
			continue;

		if (strcmp(n->name, "li") == 0) {
			/* entry */
			options_load_tree_entry(n, directory);

		} else if (strcmp(n->name, "h4") == 0) {
			/* directory */
			title = (char *) xmlNodeGetContent(n);
			if (!title) {
				warn_user("HotlistLoadError", "(Empty <h4> "
						"or memory exhausted.)");
				return;
			}

			for (n = n->next;
					n && n->type != XML_ELEMENT_NODE;
					n = n->next)
				;
			if (!n || strcmp(n->name, "ul") != 0) {
				/* next element isn't expected ul */
				free(title);
				warn_user("HotlistLoadError", "(Expected "
						"<ul> not present.)");
				return;
			}

			dir = tree_create_folder_node(directory, title);
			if (!dir)
				return;
			options_load_tree_directory(n, dir);
		}
	}
}


/**
 * Parse an entry represented as a li.
 *
 * \param  li         xmlNode for parsed li
 * \param  directory  directory to add this entry to
 */
void options_load_tree_entry(xmlNode *li, struct node *directory) {
	char *url = 0;
	char *title = 0;
	struct node *entry;
	xmlNode *n;
	struct url_content *data;

	for (n = li->children; n; n = n->next) {
		/* The li must contain an "a" element */
		if (n->type == XML_ELEMENT_NODE &&
				strcmp(n->name, "a") == 0) {
			url = (char *) xmlGetProp(n, (const xmlChar *) "href");
			title = (char *) xmlNodeGetContent(n);
		}
	}

	if (!url || !title) {
		warn_user("HotlistLoadError", "(Missing <a> in <li> or "
				"memory exhausted.)");
		return;
	}

	data = url_store_find(url);
	if (!data)
		return;
	if (!data->title)
		data->title = strdup(title);
	entry = tree_create_URL_node(directory, data, title);
	xmlFree(url);
	xmlFree(title);
}


/**
 * Search the children of an xmlNode for an element.
 *
 * \param  node  xmlNode to search children of, or 0
 * \param  name  name of element to find
 * \return  first child of node which is an element and matches name, or
 *          0 if not found or parameter node is 0
 */
xmlNode *options_find_tree_element(xmlNode *node, const char *name) {
	xmlNode *n;
	if (!node)
		return 0;
	for (n = node->children;
			n && !(n->type == XML_ELEMENT_NODE &&
			strcmp(n->name, name) == 0);
			n = n->next)
		;
	return n;
}


/**
 * Perform a save to a specified file
 *
 * /param  filename  the file to save to
 */
bool options_save_tree(struct tree *tree, const char *filename, const char *page_title) {
	int res;
	xmlDoc *doc;
	xmlNode *html, *head, *title, *body;

	/* Unfortunately the Browse Hotlist format is invalid HTML,
	 * so this is a lie. */
	doc = htmlNewDoc("http://www.w3.org/TR/html4/strict.dtd",
			"-//W3C//DTD HTML 4.01//EN");
	if (!doc) {
		warn_user("NoMemory", 0);
		return false;
	}

	html = xmlNewNode(NULL, "html");
	if (!html) {
		warn_user("NoMemory", 0);
		xmlFreeDoc(doc);
		return false;
	}
	xmlDocSetRootElement(doc, html);

	head = xmlNewChild(html, NULL, "head", NULL);
	if (!head) {
		warn_user("NoMemory", 0);
		xmlFreeDoc(doc);
		return false;
	}

	title  = xmlNewTextChild(head, NULL, "title", page_title);
	if (!title) {
		warn_user("NoMemory", 0);
		xmlFreeDoc(doc);
		return false;
	}

	body = xmlNewChild(html, NULL, "body", NULL);
	if (!body) {
		warn_user("NoMemory", 0);
		xmlFreeDoc(doc);
		return false;
	}

	if (!options_save_tree_directory(tree->root, body)) {
		warn_user("NoMemory", 0);
		xmlFreeDoc(doc);
		return false;
	}

	doc->charset = XML_CHAR_ENCODING_UTF8;
	res = htmlSaveFileEnc(filename, doc, "iso-8859-1");
	if (res == -1) {
		warn_user("HotlistSaveError", 0);
		xmlFreeDoc(doc);
		return false;
	}

	xmlFreeDoc(doc);
	return true;
}


/**
 * Add a directory to the HTML tree for saving.
 *
 * \param  directory  hotlist directory to add
 * \param  node       node to add ul to
 * \return  true on success, false on memory exhaustion
 */
bool options_save_tree_directory(struct node *directory, xmlNode *node) {
	struct node *child;
	xmlNode *ul, *h4;

	ul = xmlNewChild(node, NULL, "ul", NULL);
	if (!ul)
		return false;

	for (child = directory->child; child; child = child->next) {
		if (!child->folder) {
			/* entry */
			if (!options_save_tree_entry(child, ul))
				return false;
		} else {
			/* directory */
			/* invalid HTML */
			h4 = xmlNewTextChild(ul, NULL, "h4", child->data.text);
			if (!h4)
				return false;

			if (!options_save_tree_directory(child, ul))
				return false;
		}	}

	return true;
}


/**
 * Add an entry to the HTML tree for saving.
 *
 * The node must contain a sequence of node_elements in the following order:
 *
 * \param  entry  hotlist entry to add
 * \param  node   node to add li to
 * \return  true on success, false on memory exhaustion
 */
bool options_save_tree_entry(struct node *entry, xmlNode *node) {
	xmlNode *li, *a;
	xmlAttr *href;
	struct node_element *element;

	li = xmlNewChild(node, NULL, "li", NULL);
	if (!li)
		return false;

	a = xmlNewTextChild(li, NULL, "a", entry->data.text);
	if (!a)
		return false;

	element = tree_find_element(entry, TREE_ELEMENT_URL);
	if (!element)
		return false;
	href = xmlNewProp(a, "href", element->text);
	if (!href)
		return false;
	return true;
}
