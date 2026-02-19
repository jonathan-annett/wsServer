#pragma once

#define _GNU_SOURCE  // Required for strcasestr on some Linux systems
#include <string.h>
// belt and suspenders: strcasestr looks like this
extern char *strcasestr(const char *haystack, const char *needle);
// rename contentType for code readability: full header lines can be used to add more context
#define CUSTOM_HEADERS contentType
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

#if WS_STATICS
    #pragma message("wsServer: WS_STATICS enabled (static HTTP assets compiled in)")
#endif

typedef struct ws_static_asset_set {
    uint32_t count;
    const char * const *urls;
	const char * const *contentType;
	const uint8_t * const *content;
    const uint32_t *sizes;
} ws_static_asset_set_t;

const char default_static_html_text[] =
  "<html><head><title>WS STATIC OK</title></head><body>"
  "Success<br>"
  "Built: " __DATE__ " " __TIME__
  "</body></html>";
static const char * const urls[] = { "/" };
static const char * const types[] = { "text/html; charset=utf-8" };
static const uint8_t * const bodies[] = { (const uint8_t*)default_static_html_text };
static const uint32_t sizes[] = { (uint32_t)(sizeof(default_static_html_text)-1) };

static const ws_static_asset_set_t default_set = {
    1, urls, types, bodies, sizes
};
char static_root_alias[256]="/index.html";

ws_static_asset_set_t *g_assets = ( ws_static_asset_set_t *) &default_set;

void ws_set_static_assets(const ws_static_asset_set_t *set) {
    g_assets =  ( ws_static_asset_set_t *) set;
}

const ws_static_asset_set_t *ws_get_static_assets(void) {
	if (g_assets) {
    	return g_assets;
	}
	ws_set_static_assets(&default_set);
	
	return &default_set;
}


 
 
static const char *strstricase_local(const char *haystack, const char *needle)
{
	size_t nlen = strlen(needle);
	if (!nlen) return haystack;

	for (; *haystack; haystack++)
	{
		size_t i;
		for (i = 0; i < nlen; i++)
		{
			unsigned char a = (unsigned char)haystack[i];
			unsigned char b = (unsigned char)needle[i];
			if (!a) return NULL;
			if (tolower(a) != tolower(b)) break;
		}
		if (i == nlen) return haystack;
	}
	return NULL;
}

/* Match the library’s own handshake expectation: WS_HS_REQ is "Sec-WebSocket-Key". */
static int looks_like_ws_upgrade(const char *req)
{
	return strstricase_local(req, WS_HS_REQ) != NULL;
}

static const char *http_reason_phrase(int code)
{
	switch (code)
	{
		case 200: return "OK";
		case 400: return "Bad Request";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		default:  return "OK";
	}
}
bool ends_with(const char *str, const char *suffix);
const char* get_content_between(const char *buffer, const char *start_term, const char *end_term, size_t *foundSize);
extern char *strstr(const char *haystack, const char *needle);

static int http_send_response(struct ws_connection *client,
	int code, const char *content_type, const uint8_t *body, uint32_t body_len)
{
	char hdr[512];
	char customHdrs[512];
	int hdr_len;

	if (!content_type) content_type = "text/plain; charset=utf-8";

	if (content_type) {
		if ( ends_with (content_type,"\r\n") ){
			size_t etagSize = 0;
			if (  get_content_between((char *) content_type,"etag:","\r",&etagSize))   {

				snprintf(customHdrs,sizeof customHdrs,"%s",content_type);			
			} else {
				if (  get_content_between((char *) content_type,"Cache-Control:","\r",&etagSize))   {
					  snprintf(customHdrs,sizeof customHdrs,"%s",content_type);			
				} else {
					snprintf(customHdrs,sizeof customHdrs,"Cache-Control: no-cache\r\n%s",content_type);			
				}
				
			}

		} else {
			// to supply more than just content_type, supply the full header lines, separated by \r\n, ending in \r\n
			snprintf(customHdrs,sizeof(customHdrs),"Cache-Control: no-cache\r\nContent-Type: %s\r\n",content_type);
		}
	}


	hdr_len = snprintf(hdr, sizeof(hdr),
		"HTTP/1.1 %d %s\r\n"
		"Connection: close\r\n"
		"%s"
		"Content-Length: %" PRIu32 "\r\n"
		"\r\n",
		code, http_reason_phrase(code), customHdrs, body_len);

	if (hdr_len < 0 || (size_t)hdr_len >= sizeof(hdr))
		return -1;

	if (SEND(client, hdr, (size_t)hdr_len) < 0)
		return -1;

	if (body && body_len)
		if (SEND(client, body, body_len) < 0)
			return -1;

	return 0;
}

static int http_parse_request_line(char *buf, char **out_method, char **out_path, char **headers)
{
	char *sp1 = strchr(buf, ' ');
	if (!sp1) return -1;
	*sp1 = '\0';

	char *sp2 = strchr(sp1 + 1, ' ');
	if (!sp2) return -1;
	*sp2 = '\0';

	char *sp3;
	if (headers) {
		sp3 =  strchr(sp2 + 1, '\n');
		if (!sp3) return -1;
	}

	*out_method = buf;
	*out_path   = sp1 + 1;
	if (headers) {
		*headers    = sp3 + 1;
	}
	
	return 0;
}



const char* get_content_between(const char *buffer, const char *start_term, const char *end_term, size_t *foundSize) {
    // 1. Find the first occurrence of start_term
    char *start_ptr = strcasestr(buffer, start_term);
    if (!start_ptr) return NULL;

    // 2. Move pointer to the first character AFTER the start_term
    char *content_start = start_ptr + strlen(start_term);

    // 3. Find the first occurrence of end_term starting AFTER the start_term
    char *end_ptr = strcasestr(content_start, end_term);
    if (!end_ptr) return NULL;

    // 4. Null-terminate the buffer at the start of the end_term
	*foundSize = (size_t) (end_ptr - content_start);
	
    return content_start;
}
 

bool ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) {
        return false; // Handle potential null pointers, though standard C functions assume non-null
    }
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len) {
        return false; // A string cannot end with a suffix longer than itself
    }

    // Compare the last 'suffix_len' characters of 'str' with 'suffix'
    // memcmp is efficient for comparing blocks of memory
    return memcmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

bool found_root_alias = false;
static void find_root_alias() {
	if (found_root_alias) return;
		
	found_root_alias = true;   // only call this function once, on first server "hit"
	const char * index_html = "/index.html";
	const char * dot_html = ".html";
	const char * classic_root = "/";
	if (strcmp(static_root_alias,index_html)!=0) return;// if it has already been set to something, abort. 

	// scan the files. if we find /index.html or html, use that entry.
	// otherwise count the .html files. if there is exactly 1, use that
	// otherwise just let it fall to a 404.

	// *note* the default data url is "/" so this loop will always find that.
	const ws_static_asset_set_t *a = ws_get_static_assets();
	
	uint32_t html_count = 0;
	uint32_t i, gotIt=0;
	for (i = 0; i < a->count; i++) {
		if (a->urls[i]) {
			if ( (strcmp(a->urls[i], index_html) == 0)||
			     (strcmp(a->urls[i], classic_root) == 0)
				)  {
				snprintf (static_root_alias,sizeof (static_root_alias),"%s",a->urls[i]);
				printf ("Will use [%s] for default root /\n",static_root_alias );
				
				return;
			}
			if (ends_with(a->urls[i],dot_html)) {
				gotIt = i;
				html_count++;
			}
		}
	}

	if ( html_count==1 ) {
		snprintf (static_root_alias,sizeof (static_root_alias), "%s",a->urls[gotIt]);
		printf ("Will use [%s] for default root /\n",static_root_alias );
	}
}


static int static_find_path(const char *path)
{
	if (!found_root_alias)find_root_alias();
	
	char tmp[512];
	const char *q;
	size_t n;
	uint32_t i;

	if (!path || !*path) path = "/";

	if (path[0]=='/' && path[1]==0) {
		path = static_root_alias;
	}

	q = strchr(path, '?');
	n = q ? (size_t)(q - path) : strlen(path);
	if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
	memcpy(tmp, path, n);
	tmp[n] = '\0';

	 const ws_static_asset_set_t *a = ws_get_static_assets();


	for (i = 0; i < a->count; i++)
		if (a->urls[i] && strcmp(a->urls[i], tmp) == 0)
			return (int)i;

	return -1;
}

static void serve_static_http(struct ws_frame_data *wfd)
{
	/* Must have full headers */
	if (!strstr((char *)wfd->frm, "\r\n\r\n"))
	{
		static const uint8_t msg[] = "Bad Request\n";
		http_send_response(wfd->client, 400, NULL, msg, (uint32_t)sizeof(msg) - 1);
		return;
	}

	char *method = NULL;
	char *path = NULL;
	char *headers = NULL;

	/* Parse request line in-place (fine: we’re about to close anyway) */
	if (http_parse_request_line((char *)wfd->frm, &method, &path, &headers) < 0)
	{
		static const uint8_t msg[] = "Bad Request\n";
		http_send_response(wfd->client, 400, NULL, msg, (uint32_t)sizeof(msg) - 1);
		return;
	}

	if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0)
	{
		static const uint8_t msg[] = "Method Not Allowed\n";
		http_send_response(wfd->client, 405, NULL, msg, (uint32_t)sizeof(msg) - 1);
		return;
	}
	const ws_static_asset_set_t *a = ws_get_static_assets();
	int idx = static_find_path(path);
	if (idx < 0)
	{
		static const uint8_t msg[] = "Not Found\n";
		http_send_response(wfd->client, 404, NULL, msg, (uint32_t)sizeof(msg) - 1);
		return;
	}

	size_t etagSize = 0;
	// do we normally send an etag for this item? if so it will be in our custom headers field
	const char *etag = get_content_between((char *) a->CUSTOM_HEADERS[idx],"etag: \"","\"",&etagSize);
	if (etag && etagSize == 40) {
		// make a searchable etag from the etag in the headers we normally send out
		char findEtag[41]; memcpy(findEtag,etag,40); findEtag[40]=0;
		size_t gap = 0;
		const char * ifnonematch  = get_content_between(headers,"if-none-match:",findEtag,&gap);
		if (ifnonematch && gap < 5)	 {
			http_send_response(wfd->client, 304, a->CUSTOM_HEADERS[idx], NULL, a->sizes[idx]);
			return;
		}
	}

	
	if (strcmp(method, "HEAD") == 0)
	{
		http_send_response(wfd->client, 200, a->CUSTOM_HEADERS[idx], NULL, a->sizes[idx]);
		return;
	}

	http_send_response(wfd->client, 200, a->CUSTOM_HEADERS[idx],a->content[idx],a->sizes[idx]);
}


static int do_handshake(struct ws_frame_data *wfd)
{
	char *response;
	char *p;
	ssize_t n;
	
	if ((n = RECV(wfd->client, wfd->frm, sizeof(wfd->frm) - 1)) < 0)
		return (-1);

	wfd->frm[n] = '\0';
	wfd->amt_read = (size_t)n;


	p = strstr((const char *)wfd->frm, "\r\n\r\n");
	if (p == NULL)
	{
		/* Could also just return -1, but a 400 is nicer */
		static const uint8_t msg[] = "Bad Request\n";
		http_send_response(wfd->client, 400, NULL, msg, (uint32_t)sizeof(msg) - 1);
		return (-1);
	}

	/* If it doesn’t even look like WS (per WS_HS_REQ), serve static and stop. */
	if (!looks_like_ws_upgrade((const char *)wfd->frm))
	{
		serve_static_http(wfd);
		return (-1); /* ws_establishconnection will proceed to close */
	}

	/* WS attempt: copy because get_handshake_response() mutates input via strtok_r */
	char *req_copy = malloc((size_t)n + 1);
	if (!req_copy)
		return (-1);
	memcpy(req_copy, wfd->frm, (size_t)n + 1);

	/* Keep original buffer intact for next_byte() continuation */
	wfd->cur_pos = (size_t)((ptrdiff_t)(p - (char *)wfd->frm)) + 4;

	if (get_handshake_response(req_copy, &response) < 0)
	{
		free(req_copy);
		/* Malformed WS attempt */
		static const uint8_t msg[] = "Bad WebSocket handshake\n";
		http_send_response(wfd->client, 400, NULL, msg, (uint32_t)sizeof(msg) - 1);
		return (-1);
	}
	free(req_copy);

	if (SEND(wfd->client, response, strlen(response)) < 0)
	{
		free(response);
		return (-1);
	}

	set_client_state(wfd->client, WS_STATE_OPEN);
	wfd->client->ws_srv.evs.onopen(wfd->client->client_id);
	free(response);
	return (0);
}

#undef CUSTOM_HEADERS
