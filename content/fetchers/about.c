/*
 * Copyright 2011 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf.
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 *
 * URL handling for the "about" scheme.
 *
 * Based on the data fetcher by Rob Kendrick
 * This fetcher provides a simple scheme for the user to access
 * information from the browser from a known, fixed URL.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "utils/log.h"
#include "testament.h"
#include "utils/corestrings.h"
#include "utils/nsoption.h"
#include "utils/utils.h"
#include "utils/messages.h"
#include "utils/ring.h"

#include "content/fetch.h"
#include "content/fetchers.h"
#include "content/fetchers/about.h"
#include "image/image_cache.h"


struct fetch_about_context;

typedef bool (*fetch_about_handler)(struct fetch_about_context *);

/**
 * Context for an about fetch
 */
struct fetch_about_context {
	struct fetch_about_context *r_next, *r_prev;

	struct fetch *fetchh; /**< Handle for this fetch */

	bool aborted; /**< Flag indicating fetch has been aborted */
	bool locked; /**< Flag indicating entry is already entered */

	nsurl *url; /**< The full url the fetch refers to */

	const struct fetch_multipart_data *multipart; /**< post data */

	fetch_about_handler handler;
};

static struct fetch_about_context *ring = NULL;

/**
 * handler info for about scheme
 */
struct about_handlers {
	const char *name; /**< name to match in url */
	int name_len;
	lwc_string *lname; /**< Interned name */
	fetch_about_handler handler; /**< handler for the url */
	bool hidden; /**< If entry should be hidden in listing */
};

/** issue fetch callbacks with locking */
static inline bool fetch_about_send_callback(const fetch_msg *msg,
		struct fetch_about_context *ctx)
{
	ctx->locked = true;
	fetch_send_callback(msg, ctx->fetchh);
	ctx->locked = false;

	return ctx->aborted;
}

static bool
fetch_about_send_header(struct fetch_about_context *ctx, const char *fmt, ...)
{
	char header[64];
	fetch_msg msg;
	va_list ap;

	va_start(ap, fmt);

	vsnprintf(header, sizeof header, fmt, ap);

	va_end(ap);

	msg.type = FETCH_HEADER;
	msg.data.header_or_data.buf = (const uint8_t *) header;
	msg.data.header_or_data.len = strlen(header);

	fetch_about_send_callback(&msg, ctx);

	return ctx->aborted;
}




static bool fetch_about_blank_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;
	const char buffer[2] = { ' ', '\0' };

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, 200);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html"))
		goto fetch_about_blank_handler_aborted;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;
	msg.data.header_or_data.len = strlen(buffer);

	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_blank_handler_aborted;

	msg.type = FETCH_FINISHED;

	fetch_about_send_callback(&msg, ctx);

	return true;

fetch_about_blank_handler_aborted:
	return false;
}


static bool fetch_about_credits_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;

	/* content is going to return redirect */
	fetch_set_http_code(ctx->fetchh, 302);

	msg.type = FETCH_REDIRECT;
	msg.data.redirect = "resource:credits.html";

	fetch_about_send_callback(&msg, ctx);

	return true;
}


static bool fetch_about_licence_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;

	/* content is going to return redirect */
	fetch_set_http_code(ctx->fetchh, 302);

	msg.type = FETCH_REDIRECT;
	msg.data.redirect = "resource:licence.html";

	fetch_about_send_callback(&msg, ctx);

	return true;
}

/**
 * Handler to generate about:cache page.
 *
 * Shows details of current image cache.
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_imagecache_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;
	char buffer[2048]; /* output buffer */
	int code = 200;
	int slen;
	unsigned int cent_loop = 0;
	int res = 0;

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, code);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html"))
		goto fetch_about_imagecache_handler_aborted;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;

	/* page head */
	slen = snprintf(buffer, sizeof buffer,
			"<html>\n<head>\n"
			"<title>NetSurf Browser Image Cache Status</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body id =\"cachelist\">\n"
			"<p class=\"banner\">"
			"<a href=\"http://www.netsurf-browser.org/\">"
			"<img src=\"resource:netsurf.png\" alt=\"NetSurf\"></a>"
			"</p>\n"
			"<h1>NetSurf Browser Image Cache Status</h1>\n"	);
	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_imagecache_handler_aborted;

	/* image cache summary */
	slen = image_cache_snsummaryf(buffer, sizeof(buffer),
		"<p>Configured limit of %a hysteresis of %b</p>\n"
		"<p>Total bitmap size in use %c (in %d)</p>\n"
		"<p>Age %es</p>\n"
		"<p>Peak size %f (in %g)</p>\n"
		"<p>Peak image count %h (size %i)</p>\n"
		"<p>Cache total/hit/miss/fail (counts) %j/%k/%l/%m "
				"(%pj%%/%pk%%/%pl%%/%pm%%)</p>\n"
		"<p>Cache total/hit/miss/fail (size) %n/%o/%q/%r "
				"(%pn%%/%po%%/%pq%%/%pr%%)</p>\n"
		"<p>Total images never rendered: %s "
				"(includes %t that were converted)</p>\n"
		"<p>Total number of excessive conversions: %u "
				"(from %v images converted more than once)"
				"</p>\n"
		"<p>Bitmap of size %w had most (%x) conversions</p>\n"
		"<h2>Current image cache contents</h2>\n");
	if (slen >= (int) (sizeof(buffer)))
		goto fetch_about_imagecache_handler_aborted; /* overflow */

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_imagecache_handler_aborted;


	/* image cache entry table */
	slen = snprintf(buffer, sizeof buffer,
			"<p class=\"imagecachelist\">\n"
			"<strong>"
			"<span>Entry</span>"
			"<span>Content Key</span>"
			"<span>Redraw Count</span>"
			"<span>Conversion Count</span>"
			"<span>Last Redraw</span>"
			"<span>Bitmap Age</span>"
			"<span>Bitmap Size</span>"
			"<span>Source</span>"
			"</strong>\n");
	do {
		res = image_cache_snentryf(buffer + slen, sizeof buffer - slen,
				cent_loop,
				"<a href=\"%U\">"
				"<span>%e</span>"
				"<span>%k</span>"
				"<span>%r</span>"
				"<span>%c</span>"
				"<span>%a</span>"
				"<span>%g</span>"
				"<span>%s</span>"
				"<span>%o</span>"
				"</a>\n");
		if (res <= 0)
			break; /* last option */

		if (res >= (int) (sizeof buffer - slen)) {
			/* last entry would not fit in buffer, submit buffer */
			msg.data.header_or_data.len = slen;
			if (fetch_about_send_callback(&msg, ctx))
				goto fetch_about_imagecache_handler_aborted;
			slen = 0;
		} else {
			/* normal addition */
			slen += res;
			cent_loop++;
		}
	} while (res > 0);

	slen += snprintf(buffer + slen, sizeof buffer - slen,
			 "</p>\n</body>\n</html>\n");

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_imagecache_handler_aborted;

	msg.type = FETCH_FINISHED;
	fetch_about_send_callback(&msg, ctx);

	return true;

fetch_about_imagecache_handler_aborted:
	return false;
}

/**
 * Handler to generate about scheme config page
 */
static bool fetch_about_config_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;
	char buffer[1024];
	int code = 200;
	int slen;
	unsigned int opt_loop = 0;
	int res = 0;

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, code);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html"))
		goto fetch_about_config_handler_aborted;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;

	slen = snprintf(buffer, sizeof buffer,
			"<html>\n<head>\n"
			"<title>NetSurf Browser Config</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body id =\"configlist\">\n"
			"<p class=\"banner\">"
			"<a href=\"http://www.netsurf-browser.org/\">"
			"<img src=\"resource:netsurf.png\" alt=\"NetSurf\"></a>"
			"</p>\n"
			"<h1>NetSurf Browser Config</h1>\n"
			"<table class=\"config\">\n"
			"<tr><th>Option</th><th>Type</th><th>Provenance</th><th>Setting</th></tr>\n");

	do {
		res = nsoption_snoptionf(buffer + slen,
					 sizeof buffer - slen,
					 opt_loop,
					 "<tr><th>%k</th><td>%t</td><td>%p</td><td>%V</td></tr>\n");
		if (res <= 0)
			break; /* last option */

		if (res >= (int) (sizeof buffer - slen)) {
			/* last entry would not fit in buffer, submit buffer */
			msg.data.header_or_data.len = slen;
			if (fetch_about_send_callback(&msg, ctx))
				goto fetch_about_config_handler_aborted;
			slen = 0;
		} else {
			/* normal addition */
			slen += res;
			opt_loop++;
		}
	} while (res > 0);

	slen += snprintf(buffer + slen, sizeof buffer - slen,
			 "</table>\n</body>\n</html>\n");

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_config_handler_aborted;

	msg.type = FETCH_FINISHED;
	fetch_about_send_callback(&msg, ctx);

	return true;

fetch_about_config_handler_aborted:
	return false;
}


/** Generate the text of a Choices file which represents the current
 * in use options.
 */
static bool fetch_about_choices_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;
	char buffer[1024];
	int code = 200;
	int slen;
	unsigned int opt_loop = 0;
	int res = 0;

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, code);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/plain"))
		goto fetch_about_choices_handler_aborted;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;

	slen = snprintf(buffer, sizeof buffer,
		 "# Automatically generated current NetSurf browser Choices\n");

	do {
		res = nsoption_snoptionf(buffer + slen,
				sizeof buffer - slen,
				opt_loop,
				"%k:%v\n");
		if (res <= 0)
			break; /* last option */

		if (res >= (int) (sizeof buffer - slen)) {
			/* last entry would not fit in buffer, submit buffer */
			msg.data.header_or_data.len = slen;
			if (fetch_about_send_callback(&msg, ctx))
				goto fetch_about_choices_handler_aborted;
			slen = 0;
		} else {
			/* normal addition */
			slen += res;
			opt_loop++;
		}
	} while (res > 0);

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_choices_handler_aborted;

	msg.type = FETCH_FINISHED;
	fetch_about_send_callback(&msg, ctx);

	return true;

fetch_about_choices_handler_aborted:
	return false;
}

/** Generate the text of an svn testament which represents the current
 * build-tree status
 */
typedef struct { const char *leaf; const char *modtype; } modification_t;
static bool fetch_about_testament_handler(struct fetch_about_context *ctx)
{
	static modification_t modifications[] = WT_MODIFICATIONS;
	fetch_msg msg;
	char buffer[1024];
	int code = 200;
	int slen;
	int i;


	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, code);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/plain"))
		goto fetch_about_testament_handler_aborted;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;

	slen = snprintf(buffer, sizeof buffer,
		 "# Automatically generated by NetSurf build system\n\n");

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_testament_handler_aborted;

	slen = snprintf(buffer, sizeof buffer,
#if defined(WT_BRANCHISTRUNK) || defined(WT_BRANCHISMASTER)
			"# This is a *DEVELOPMENT* build from the main line.\n\n"
#elif defined(WT_BRANCHISTAG) && (WT_MODIFIED == 0)
			"# This is a tagged build of NetSurf\n"
#ifdef WT_TAGIS
			"#      The tag used was '" WT_TAGIS "'\n\n"
#else
			"\n"
#endif
#elif defined(WT_NO_SVN) || defined(WT_NO_GIT)
			"# This NetSurf was built outside of our revision "
			"control environment.\n"
			"# This testament is therefore not very useful.\n\n"
#else
			"# This NetSurf was built from a branch (" WT_BRANCHPATH ").\n\n"
#endif
#if defined(CI_BUILD)
			"# This build carries the CI build number '" CI_BUILD "'\n\n"
#endif
			);

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_testament_handler_aborted;


	slen = snprintf(buffer, sizeof buffer,
			"Built by %s (%s) from %s at revision %s on %s\n\n",
			GECOS, USERNAME, WT_BRANCHPATH, WT_REVID, WT_COMPILEDATE);

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_testament_handler_aborted;

	slen = snprintf(buffer, sizeof buffer,
			"Built on %s in %s\n\n",
			WT_HOSTNAME, WT_ROOT);

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_testament_handler_aborted;

	if (WT_MODIFIED > 0) {
		slen = snprintf(buffer, sizeof buffer,
				"Working tree has %d modification%s\n\n",
				WT_MODIFIED, WT_MODIFIED == 1 ? "" : "s");
	} else {
		slen = snprintf(buffer, sizeof buffer,
				"Working tree is not modified.\n");
	}

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_testament_handler_aborted;

	for (i = 0; i < WT_MODIFIED; ++i) {
		slen = snprintf(buffer, sizeof buffer,
				"  %s  %s\n",
				modifications[i].modtype,
				modifications[i].leaf);
		msg.data.header_or_data.len = slen;
		if (fetch_about_send_callback(&msg, ctx))
			goto fetch_about_testament_handler_aborted;

	}

	msg.type = FETCH_FINISHED;
	fetch_about_send_callback(&msg, ctx);

	return true;

fetch_about_testament_handler_aborted:
	return false;
}

static bool fetch_about_logo_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;

	/* content is going to return redirect */
	fetch_set_http_code(ctx->fetchh, 302);

	msg.type = FETCH_REDIRECT;
	msg.data.redirect = "resource:netsurf.png";

	fetch_about_send_callback(&msg, ctx);

	return true;
}

static bool fetch_about_welcome_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;

	/* content is going to return redirect */
	fetch_set_http_code(ctx->fetchh, 302);

	msg.type = FETCH_REDIRECT;
	msg.data.redirect = "resource:welcome.html";

	fetch_about_send_callback(&msg, ctx);

	return true;
}

static bool fetch_about_maps_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;

	/* content is going to return redirect */
	fetch_set_http_code(ctx->fetchh, 302);

	msg.type = FETCH_REDIRECT;
	msg.data.redirect = "resource:maps.html";

	fetch_about_send_callback(&msg, ctx);

	return true;
}


/**
 * generate a 500 server error respnse
 */
static bool fetch_about_srverror(struct fetch_about_context *ctx)
{
	char buffer[256];
	int slen;
	fetch_msg msg;

	fetch_set_http_code(ctx->fetchh, 500);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/plain"))
		return false;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;
	slen = snprintf(buffer, sizeof buffer, "Server error 500");

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		return false;

	msg.type = FETCH_FINISHED;
	fetch_about_send_callback(&msg, ctx);

	return true;
}


/**
 * generate the description of the login request
 */
static nserror
get_login_description(struct nsurl *url,
		      const char *realm,
		      const char *username,
		      const char *password,
		      char **out_str)
{
	char *url_s;
	size_t url_l;
	nserror res;
	char *str = NULL;
	int slen;
	const char *key;

	res = nsurl_get(url, NSURL_SCHEME | NSURL_HOST, &url_s, &url_l);
	if (res != NSERROR_OK) {
		return res;
	}

	if ((*username == 0) && (*password == 0)) {
		key = "LoginDescription";
	} else {
		key = "LoginAgain";
	}

	str = messages_get_buff(key, url_s, realm);
	NSLOG(netsurf, INFO,
	      "key:%s url:%s realm:%s str:%s", key, url_s, realm, str);

	if ((str != NULL) && (strcmp(key, str) != 0)) {
		*out_str = str;
	} else {
		/* no message so fallback */
		const char *fmt = "The site %s is requesting your username and password. The realm is \"%s\"";
		slen = snprintf(str, 0, fmt, url_s, realm) + 1;
		str = malloc(slen);
		if (str == NULL) {
			res = NSERROR_NOMEM;
		} else {
			snprintf(str, slen, fmt, url_s, realm);
			*out_str = str;
		}
	}

	free(url_s);

	return res;
}


/**
 * Handler to generate about scheme authorisation query page
 */
static bool fetch_about_query_auth_handler(struct fetch_about_context *ctx)
{
	nserror res;
	fetch_msg msg;
	char buffer[1024];
	int slen;
	char *url_s;
	size_t url_l;
	const char *realm = "";
	const char *username = "";
	const char *password = "";
	const char *title;
	char *description = NULL;
	struct nsurl *siteurl = NULL;
	const struct fetch_multipart_data *curmd; /* mutipart data iterator */

	/* extract parameters from multipart post data */
	curmd = ctx->multipart;
	while (curmd != NULL) {
		if (strcmp(curmd->name, "siteurl") == 0) {
			res = nsurl_create(curmd->value, &siteurl);
			if (res != NSERROR_OK) {
				return fetch_about_srverror(ctx);
			}
		} else if (strcmp(curmd->name, "realm") == 0) {
			realm = curmd->value;
		} else if (strcmp(curmd->name, "username") == 0) {
			username = curmd->value;
		} else if (strcmp(curmd->name, "password") == 0) {
			password = curmd->value;
		}
		curmd = curmd->next;
	}

	if (siteurl == NULL) {
		return fetch_about_srverror(ctx);
	}

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, 200);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html; charset=utf-8"))
		goto fetch_about_query_auth_handler_aborted;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;

	title = messages_get("LoginTitle");
	slen = snprintf(buffer, sizeof buffer,
			"<html>\n<head>\n"
			"<title>%s</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body id =\"authentication\">\n"
			"<h1>%s</h1>\n",
			title, title);

	res = get_login_description(siteurl,
				    realm,
				    username,
				    password,
				    &description);
	if (res == NSERROR_OK) {
		slen += snprintf(buffer + slen, sizeof(buffer) - slen,
				 "<p>%s</p>",
				 description);
		free(description);
	}

	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			 "<form method=\"post\" enctype=\"multipart/form-data\">");

	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			 "<div>"
			 "<label for=\"name\">%s:</label>"
			 "<input type=\"text\" id=\"username\" "
			 "name=\"username\" value=\"%s\">"
			 "</div>",
			 messages_get("Username"), username);

	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			 "<div>"
			 "<label for=\"password\">%s:</label>"
			 "<input type=\"password\" id=\"password\" "
			 "name=\"password\" value=\"%s\">"
			 "</div>",
			 messages_get("Password"), password);

	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			 "<div>"
			 "<input type=\"submit\" id=\"cancel\" name=\"cancel\" "
			 "value=\"%s\">"
			 "<input type=\"submit\" id=\"login\" name=\"login\" "
			 "value=\"%s\">"
			 "</div>",
			 messages_get("Cancel"),
			 messages_get("Login"));

	res = nsurl_get(siteurl, NSURL_COMPLETE, &url_s, &url_l);
	if (res != NSERROR_OK) {
		url_s = strdup("");
	}
	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			 "<input type=\"hidden\" name=\"siteurl\" value=\"%s\">",
			 url_s);
	free(url_s);

	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			 "<input type=\"hidden\" name=\"realm\" value=\"%s\">",
			 realm);

	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			"</form></body>\n</html>\n");

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_query_auth_handler_aborted;

	msg.type = FETCH_FINISHED;
	fetch_about_send_callback(&msg, ctx);

	nsurl_unref(siteurl);

	return true;

fetch_about_query_auth_handler_aborted:

	nsurl_unref(siteurl);

	return false;
}

/**
 * Handler to generate about scheme ssl query page
 */
static bool fetch_about_query_ssl_handler(struct fetch_about_context *ctx)
{
	nserror res;
	fetch_msg msg;
	char buffer[1024];
	int slen;
	char *url_s;
	size_t url_l;
	const char *reason = "";
	const char *title;
	struct nsurl *siteurl = NULL;
	const struct fetch_multipart_data *curmd; /* mutipart data iterator */

	/* extract parameters from multipart post data */
	curmd = ctx->multipart;
	while (curmd != NULL) {
		if (strcmp(curmd->name, "siteurl") == 0) {
			res = nsurl_create(curmd->value, &siteurl);
			if (res != NSERROR_OK) {
				return fetch_about_srverror(ctx);
			}
		} else if (strcmp(curmd->name, "reason") == 0) {
			reason = curmd->value;
		}
		curmd = curmd->next;
	}

	if (siteurl == NULL) {
		return fetch_about_srverror(ctx);
	}

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, 200);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html; charset=utf-8"))
		goto fetch_about_query_ssl_handler_aborted;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;

	title = messages_get("PrivacyTitle");
	slen = snprintf(buffer, sizeof buffer,
			"<html>\n<head>\n"
			"<title>%s</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body id =\"privacy\">\n"
			"<h1>%s</h1>\n",
			title, title);

	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			 "<p>%s</p>",
			 reason);

	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			 "<form method=\"post\" enctype=\"multipart/form-data\">");


	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			 "<div>"
			 "<input type=\"submit\" id=\"back\" name=\"back\" "
			 "value=\"%s\">"
			 "<input type=\"submit\" id=\"proceed\" name=\"proceed\" "
			 "value=\"%s\">"
			 "</div>",
			 messages_get("Backtosafety"),
			 messages_get("Proceed"));

	res = nsurl_get(siteurl, NSURL_COMPLETE, &url_s, &url_l);
	if (res != NSERROR_OK) {
		url_s = strdup("");
	}
	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			 "<input type=\"hidden\" name=\"siteurl\" value=\"%s\">",
			 url_s);
	free(url_s);

	slen += snprintf(buffer + slen, sizeof(buffer) - slen,
			"</form></body>\n</html>\n");

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_query_ssl_handler_aborted;

	msg.type = FETCH_FINISHED;
	fetch_about_send_callback(&msg, ctx);

	nsurl_unref(siteurl);

	return true;

fetch_about_query_ssl_handler_aborted:
	nsurl_unref(siteurl);

	return false;
}


/* Forward declaration because this handler requires the handler table. */
static bool fetch_about_about_handler(struct fetch_about_context *ctx);

/**
 * List of about paths and their handlers
 */
struct about_handlers about_handler_list[] = {
	{
		"credits",
		SLEN("credits"),
		NULL,
		fetch_about_credits_handler,
		false
	},
	{
		"licence",
		SLEN("licence"),
		NULL,
		fetch_about_licence_handler,
		false
	},
	{
		"license",
		SLEN("license"),
		NULL,
		fetch_about_licence_handler,
		true
	},
	{
		"welcome",
		SLEN("welcome"),
		NULL,
		fetch_about_welcome_handler,
		false
	},
	{
		"maps",
		SLEN("maps"),
		NULL,
		fetch_about_maps_handler,
		false
	},
	{
		"config",
		SLEN("config"),
		NULL,
		fetch_about_config_handler,
		false
	},
	{
		"Choices",
		SLEN("Choices"),
		NULL,
		fetch_about_choices_handler,
		false
	},
	{
		"testament",
		SLEN("testament"),
		NULL,
		fetch_about_testament_handler,
		false
	},
	{
		"about",
		SLEN("about"),
		NULL,
		fetch_about_about_handler,
		true
	},
	{
		"logo",
		SLEN("logo"),
		NULL,
		fetch_about_logo_handler,
		true
	},
	{
		/* details about the image cache */
		"imagecache",
		SLEN("imagecache"),
		NULL,
		fetch_about_imagecache_handler,
		true
	},
	{
		/* The default blank page */
		"blank",
		SLEN("blank"),
		NULL,
		fetch_about_blank_handler,
		true
	},
	{
		"query/auth",
		SLEN("query/auth"),
		NULL,
		fetch_about_query_auth_handler,
		true
	},
	{
		"query/ssl",
		SLEN("query/ssl"),
		NULL,
		fetch_about_query_ssl_handler,
		true
	}
};

#define about_handler_list_len (sizeof(about_handler_list) /		\
		sizeof(struct about_handlers))

/**
 * List all the valid about: paths available
 *
 * \param ctx The fetch context.
 * \return true for sucess or false to generate an error.
 */
static bool fetch_about_about_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;
	char buffer[1024];
	int code = 200;
	int slen;
	unsigned int abt_loop = 0;
	int res = 0;

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, code);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html"))
		goto fetch_about_config_handler_aborted;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;

	slen = snprintf(buffer, sizeof buffer,
			"<html>\n<head>\n"
			"<title>NetSurf List of About pages</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body id =\"aboutlist\">\n"
			"<p class=\"banner\">"
			"<a href=\"http://www.netsurf-browser.org/\">"
			"<img src=\"resource:netsurf.png\" alt=\"NetSurf\"></a>"
			"</p>\n"
			"<h1>NetSurf List of About pages</h1>\n"
			"<ul>\n");

	for (abt_loop = 0; abt_loop < about_handler_list_len; abt_loop++) {

		/* Skip over hidden entries */
		if (about_handler_list[abt_loop].hidden)
			continue;

		res = snprintf(buffer + slen, sizeof buffer - slen,
			       "<li><a href=\"about:%s\">about:%s</a></li>\n",
			       about_handler_list[abt_loop].name,
			       about_handler_list[abt_loop].name);
		if (res <= 0)
			break; /* last option */

		if (res >= (int)(sizeof buffer - slen)) {
			/* last entry would not fit in buffer, submit buffer */
			msg.data.header_or_data.len = slen;
			if (fetch_about_send_callback(&msg, ctx))
				goto fetch_about_config_handler_aborted;
			slen = 0;
		} else {
			/* normal addition */
			slen += res;
		}
	}

	slen += snprintf(buffer + slen, sizeof buffer - slen,
			 "</ul>\n</body>\n</html>\n");

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_config_handler_aborted;

	msg.type = FETCH_FINISHED;
	fetch_about_send_callback(&msg, ctx);

	return true;

fetch_about_config_handler_aborted:
	return false;
}


/** callback to initialise the about fetcher. */
static bool fetch_about_initialise(lwc_string *scheme)
{
	unsigned int abt_loop = 0;
	lwc_error error;

	for (abt_loop = 0; abt_loop < about_handler_list_len; abt_loop++) {
		error = lwc_intern_string(about_handler_list[abt_loop].name,
					about_handler_list[abt_loop].name_len,
					&about_handler_list[abt_loop].lname);
		if (error != lwc_error_ok) {
			while (abt_loop-- != 0) {
				lwc_string_unref(about_handler_list[abt_loop].lname);
			}
			return false;
		}
	}

	return true;
}

/** callback to finalise the about fetcher. */
static void fetch_about_finalise(lwc_string *scheme)
{
	unsigned int abt_loop = 0;
	for (abt_loop = 0; abt_loop < about_handler_list_len; abt_loop++) {
		lwc_string_unref(about_handler_list[abt_loop].lname);
	}
}

static bool fetch_about_can_fetch(const nsurl *url)
{
	return true;
}

/**
 * callback to set up a about scheme fetch.
 *
 * \param post_urlenc post data in urlenc format, owned by the llcache object
 *                        hence valid the entire lifetime of the fetch.
 * \param post_multipart post data in multipart format, owned by the llcache
 *                        object hence valid the entire lifetime of the fetch.
 */
static void *
fetch_about_setup(struct fetch *fetchh,
		  nsurl *url,
		  bool only_2xx,
		  bool downgrade_tls,
		  const char *post_urlenc,
		  const struct fetch_multipart_data *post_multipart,
		  const char **headers)
{
	struct fetch_about_context *ctx;
	unsigned int handler_loop;
	lwc_string *path;
	bool match;

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	path = nsurl_get_component(url, NSURL_PATH);

	for (handler_loop = 0;
	     handler_loop < about_handler_list_len;
	     handler_loop++) {
		ctx->handler = about_handler_list[handler_loop].handler;
		if (lwc_string_isequal(path,
				       about_handler_list[handler_loop].lname,
				       &match) == lwc_error_ok && match) {
			break;
		}
	}

	if (path != NULL)
		lwc_string_unref(path);

	ctx->fetchh = fetchh;
	ctx->url = nsurl_ref(url);
	ctx->multipart = post_multipart;

	RING_INSERT(ring, ctx);

	return ctx;
}

/** callback to free a about fetch */
static void fetch_about_free(void *ctx)
{
	struct fetch_about_context *c = ctx;
	nsurl_unref(c->url);
	RING_REMOVE(ring, c);
	free(ctx);
}

/** callback to start a about fetch */
static bool fetch_about_start(void *ctx)
{
	return true;
}

/** callback to abort a about fetch */
static void fetch_about_abort(void *ctx)
{
	struct fetch_about_context *c = ctx;

	/* To avoid the poll loop having to deal with the fetch context
	 * disappearing from under it, we simply flag the abort here.
	 * The poll loop itself will perform the appropriate cleanup.
	 */
	c->aborted = true;
}


/** callback to poll for additional about fetch contents */
static void fetch_about_poll(lwc_string *scheme)
{
	struct fetch_about_context *c, *next;

	if (ring == NULL) return;

	/* Iterate over ring, processing each pending fetch */
	c = ring;
	do {
		/* Ignore fetches that have been flagged as locked.
		 * This allows safe re-entrant calls to this function.
		 * Re-entrancy can occur if, as a result of a callback,
		 * the interested party causes fetch_poll() to be called
		 * again.
		 */
		if (c->locked == true) {
			next = c->r_next;
			continue;
		}

		/* Only process non-aborted fetches */
		if (c->aborted == false) {
			/* about fetches can be processed in one go */
			c->handler(c);
		}

		/* Compute next fetch item at the last possible moment
		 * as processing this item may have added to the ring
		 */
		next = c->r_next;

		fetch_remove_from_queues(c->fetchh);
		fetch_free(c->fetchh);

		/* Advance to next ring entry, exiting if we've reached
		 * the start of the ring or the ring has become empty
		 */
	} while ( (c = next) != ring && ring != NULL);
}

nserror fetch_about_register(void)
{
	lwc_string *scheme = lwc_string_ref(corestring_lwc_about);
	const struct fetcher_operation_table fetcher_ops = {
		.initialise = fetch_about_initialise,
		.acceptable = fetch_about_can_fetch,
		.setup = fetch_about_setup,
		.start = fetch_about_start,
		.abort = fetch_about_abort,
		.free = fetch_about_free,
		.poll = fetch_about_poll,
		.finalise = fetch_about_finalise
	};

	return fetcher_add(scheme, &fetcher_ops);
}
