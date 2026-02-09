/*
 * dir2statics.c
 *
 * Generate a C header containing embedded static files for wsServer statics.h.
 *
 * Usage:
 *   dir2statics <input_dir> <output_header> [url_prefix]
 *
 * Example:
 *   dir2statics ./www ./generated_statics.h
 *   dir2statics ./www ./generated_statics.h /
 *
 * Integration:
 *   // in exactly ONE .c file:
 *   #define WS_STATICS_DATA_IMPLEMENTATION
 *   #include "generated_statics.h"
 *
 *   // everywhere else:
 *   #include "generated_statics.h"
 *
 * Notes:
 * - Non-recursive (top-level only)
 * - URLs are url_prefix + filename (default "/")
 * - Skips directories and non-regular files
 */

#define _POSIX_C_SOURCE 200809L


#define GZIP

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <strings.h>
#include <sha1.h>

#include <stdio.h>
#include <stdlib.h>
#ifdef GZIP
#include <zlib.h>
#define BUFFER_SIZE 8192 // Max line length or buffer size
#endif

/* ---------- small helpers ---------- */

static void die(const char *msg)
{
	perror(msg);
	exit(1);
}

static int is_regular_file(const char *dir, const char *name)
{
	char path[4096];
	struct stat st;
	int n = snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (n < 0 || (size_t)n >= sizeof(path))
		return 0;
	if (stat(path, &st) != 0)
		return 0;
	return S_ISREG(st.st_mode) ? 1 : 0;
}

static char *xstrdup(const char *s)
{
	size_t n = strlen(s);
	char *p = (char *)malloc(n + 1);
	if (!p) die("malloc");
	memcpy(p, s, n + 1);
	return p;
}

static int cmp_strptr(const void *a, const void *b)
{
	const char *sa = *(const char * const *)a;
	const char *sb = *(const char * const *)b;
	return strcmp(sa, sb);
}

static const char *content_type_for_filename(const char *name)
{
	const char *dot = strrchr(name, '.');
	if (!dot || dot == name)
		return 0; // only accept file types in the list below....

	/* lower-case compare without allocating */
	if (!strcasecmp(dot, ".html") || !strcasecmp(dot, ".htm"))
		return "text/html; charset=utf-8";
	if (!strcasecmp(dot, ".js"))
		return "text/javascript; charset=utf-8";
	if (!strcasecmp(dot, ".css"))
		return "text/css; charset=utf-8";
	if (!strcasecmp(dot, ".json"))
		return "application/json; charset=utf-8";
	if (!strcasecmp(dot, ".txt"))
		return "text/plain; charset=utf-8";
	if (!strcasecmp(dot, ".svg"))
		return "image/svg+xml";
	if (!strcasecmp(dot, ".png"))
		return "image/png";
	if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg"))
		return "image/jpeg";
	if (!strcasecmp(dot, ".gif"))
		return "image/gif";
	if (!strcasecmp(dot, ".ico"))
		return "image/x-icon";
	if (!strcasecmp(dot, ".wasm"))
		return "application/wasm";
	if (!strcasecmp(dot, ".woff"))
		return "font/woff";
	if (!strcasecmp(dot, ".woff2"))
		return "font/woff2";

	return 0; // we don't accept this file type
}

/* Turn a filename into a safe C identifier suffix. */
static void ident_from_filename(const char *name, char *out, size_t out_sz)
{
	size_t j = 0;
	for (size_t i = 0; name[i] && j + 2 < out_sz; i++)
	{
		unsigned char c = (unsigned char)name[i];
		if (isalnum(c))
			out[j++] = (char)c;
		else
			out[j++] = '_';
	}
	out[j] = '\0';

	if (j == 0)
	{
		strncpy(out, "file", out_sz);
		out[out_sz - 1] = '\0';
	}
}

/* Read whole file into memory */
static uint8_t *read_file(const char *path, uint32_t *out_size)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;

	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	long sz = ftell(f);
	if (sz < 0) { fclose(f); return NULL; }
	if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

	uint8_t *buf = (uint8_t *)malloc((size_t)sz ? (size_t)sz : 1);
	if (!buf) { fclose(f); return NULL; }

	size_t got = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	if (got != (size_t)sz)
	{
		free(buf);
		return NULL;
	}

	*out_size = (uint32_t)sz;
	return buf;
}


/* Emit bytes as hex array */
static size_t emit_u8_array(FILE *out, const char *ident, const uint8_t *data, uint32_t size)
{
	fprintf(out, "static const uint8_t %s[%u] = {", ident, size);
	for (uint32_t i = 0; i < size; i++)
	{
		if (i % 16 == 0) fprintf(out, "\n/* %04X*/",i);
		fprintf(out, "0x%02x", (unsigned)data[i]);
		if (i + 1 != size) fprintf(out, ", ");
	}
	if (size == 0) fprintf(out, "\n  /* empty */");
	fprintf(out, "\n};\n\n");
	return size;
}

#ifdef GZIP
/* Emit bytes as gzipped hex array */
static size_t emit_u8_gz_array(FILE *out, const char *ident, const uint8_t *data, uint32_t size)
{
    z_stream strm = {0};

    // 15 is the default window size; +16 enables GZIP format
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 
                     15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return 0;
    }

	size_t compressed_size = deflateBound(&strm, size);

	unsigned char *compressed_data = malloc (compressed_size);
	if (!compressed_data) die("malloc");


    strm.next_in   = (Bytef *)data;
    strm.avail_in  = (uInt) size;
    strm.next_out  = (Bytef *)compressed_data;
    strm.avail_out = (uInt) compressed_size;


    // Use Z_FINISH to complete compression in one step if the buffer is large enough
    int ret = deflate(&strm, Z_FINISH);
    
    if (ret == Z_STREAM_END) {
        compressed_size = strm.total_out; // Actual size of compressed data
        deflateEnd(&strm);
		emit_u8_array(out,ident, compressed_data, compressed_size);
		free(compressed_data);
        return compressed_size;
    }
    deflateEnd(&strm);
	free(compressed_data);
    return 0 ;
}
#endif

/* ---------- main ---------- */

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr,
			"Usage: %s <input_dir> <output_header> [url_prefix]\n",
			argv[0]);
		return 2;
	}

	SHA1Context ctx;                  /* SHA-1 Context.                */

	const char hexEncode[16] = "0123456789ABCDEF";
	struct {
			char Etag_Colon_Quote[8]; // 'Etag: \"'
			char hexhash[SHA1HashSize]; // first 20 hex bytes
			unsigned char hash[SHA1HashSize]; // second 20 hex bytes, and binary hash temp
			char Quote_crlf[6]; //  '\"\r\n"
#ifdef GZIP							   //  '12345678901234567890123 4
			char ContentEncoding[26];  //  'Content-Encoding: gzip\r\n'
#endif
			char contentTypeTag[14];  // 'Content-Type: '
			char contentType[64]; //
		} CustomHeader, *cHdr;
	
	char * c;
	c = &CustomHeader.Etag_Colon_Quote[0]; sprintf(c,"Etag: %c",'\\');c[7]='"';
	c = &CustomHeader.Quote_crlf[0];sprintf(c,"%c%c%cr%c",'\\','"','\\','\\');c[5]='n';
#ifdef GZIP
	c = &CustomHeader.ContentEncoding[0];sprintf(c,"Content-Encoding: %s\\r\\","gzip");c[25]='n';
#endif
	c = &CustomHeader.contentTypeTag[0];sprintf(c,"%s","Content-Type:");c[13]=' ';
	

	

	const char *in_dir = argv[1];
	const char *out_path = argv[2];
	const char *url_prefix = (argc >= 4) ? argv[3] : "/";

	/* normalize url_prefix: if empty, treat as "/" */
	if (!url_prefix || !*url_prefix) url_prefix = "/";

	/* collect filenames */
	DIR *d = opendir(in_dir);
	if (!d) die("opendir");

	size_t cap = 32, count = 0;
	char **names = (char **)malloc(cap * sizeof(char *));
	if (!names) die("malloc");

	cHdr = malloc(cap * sizeof(CustomHeader));
	if (!cHdr) die("malloc");


	struct dirent *de;
	while ((de = readdir(d)) != NULL)
	{
		const char *nm = de->d_name;
		if (!strcmp(nm, ".") || !strcmp(nm, "..")) continue;

		if (!content_type_for_filename(nm)) continue;

		if (!is_regular_file(in_dir, nm))
			continue;

		if (count == cap)
		{
			cap *= 2;
			char **tmp = (char **)realloc(names, cap * sizeof(char *));
			if (!tmp) die("realloc");
			names = tmp;

			tmp = (char **)realloc(cHdr, cap * sizeof(CustomHeader));
			if (!tmp) die("realloc");
			cHdr = (void*) tmp;
		}
		names[count++] = xstrdup(nm);
	}
	closedir(d);

	qsort(names, count, sizeof(char *), cmp_strptr);

	FILE *out = fopen(out_path, "w");
	if (!out) die("fopen(output)");

	/* header prelude */
	fprintf(out,
		"#pragma once\n"
		"#include <stdint.h>\n"
		"\n"
		"#ifdef __cplusplus\n"
		"extern \"C\" {\n"
		"#endif\n"
		"\n"
		"/* Declarations compatible with wsServer statics.h expectations */\n" 		
		"void initEmbeddedAssets(void);"

		"typedef struct ws_static_asset_set {\n"
		"    uint32_t count;\n"
		"    const char * const *urls;\n"
		"    const char * const *contentType;\n"
		"    const uint8_t * const *content;\n"
		"    const uint32_t *sizes;\n"
		"} ws_static_asset_set_t;\n\n"
		"extern char static_root_alias[32];\n\n"


		"void ws_set_static_assets(const ws_static_asset_set_t *set);\n"
		"\n"
		"#ifdef WS_STATICS_DATA_IMPLEMENTATION\n"
		"\n");

	/* emit tables */
	fprintf(out, "const uint32_t static_count = %u;\n\n", (unsigned)count);

	fprintf(out, "const char *static_urls[%u] = {\n", (unsigned)count);
	for (size_t i = 0; i < count; i++)
		fprintf(out, "  \"%s%s\"%s\n", url_prefix, names[i], (i + 1 == count) ? "" : ",");
	fprintf(out, "};\n\n");



	/* emit embedded file arrays */
	for (size_t i = 0; i < count; i++)
	{
		char ident[512];
		char path[4096];
		uint32_t sz = 0;

		ident_from_filename(names[i], ident, sizeof(ident));

		int n = snprintf(path, sizeof(path), "%s/%s", in_dir, names[i]);
		if (n < 0 || (size_t)n >= sizeof(path))
		{
			fprintf(stderr, "Path too long, skipping: %s\n", names[i]);
			continue;
		}

		uint8_t *data = read_file(path, &sz);
		if (!data)
		{
			fprintf(stderr, "Failed to read: %s\n", path);
			continue;
		}


		
		SHA1Reset(&ctx);
		SHA1Input(&ctx, data, sz);
		SHA1Result(&ctx,CustomHeader.hash);
		char * hexOut = &CustomHeader.hexhash[0];
		unsigned char *decIn = &CustomHeader.hash[0];
		for (int x = 0; x < SHA1HashSize; x ++ , decIn++) {
			*(hexOut++) = hexEncode[ *decIn >> 4  ];
			*(hexOut++) = hexEncode[ *decIn & 0xf ];			
		}
		snprintf(CustomHeader.contentType,sizeof (CustomHeader.contentType),"%s\\r\\n", content_type_for_filename(names[i]));

		// copy the entire struct (maps to null termed string)
		memmove( &cHdr[i], &CustomHeader, sizeof(CustomHeader));

		char arrname[600];
		snprintf(arrname, sizeof(arrname), "ws_static_%s", ident);
#ifdef GZIP
		emit_u8_gz_array(out, arrname, data, sz);
#else
		emit_u8_array(out, arrname, data, sz);
#endif
		


		free(data);

	}

	fprintf(out, "const char *static_contentType[%u] = {\n", (unsigned)count);
	for (size_t i = 0; i < count; i++) {
		fprintf(out, "  // %s\n", names[i]);
		fprintf(out, "  \"%s\"%s\n", (char *) &cHdr[i], (i + 1 == count) ? "" : ",");
	}
	fprintf(out, "};\n\n");


	
	fprintf(out, "const uint8_t *static_content[%u] = {\n", (unsigned)count);
	for (size_t i = 0; i < count; i++)
	{
		char ident[512];
		char arrname[600];
		ident_from_filename(names[i], ident, sizeof(ident));
		snprintf(arrname, sizeof(arrname), "ws_static_%s", ident);
		fprintf(out, "  %s%s\n", arrname, (i + 1 == count) ? "" : ",");
	}
	fprintf(out, "};\n\n");

	fprintf(out, "const uint32_t static_content_size[%u] = {\n", (unsigned)count);
	for (size_t i = 0; i < count; i++)
	{
		/* We can safely use sizeof() because the arrays are in this header under IMPLEMENTATION. */
		char ident[512];
		char arrname[600];
		ident_from_filename(names[i], ident, sizeof(ident));
		snprintf(arrname, sizeof(arrname), "ws_static_%s", ident);
		fprintf(out, "  (uint32_t)sizeof(%s)%s\n", arrname, (i + 1 == count) ? "" : ",");
	}
	fprintf(out, "};\n\n");

	fprintf(out,


		"static const ws_static_asset_set_t embedded_assets = {\n"
		"    static_count, static_urls, static_contentType, static_content, static_content_size\n"
		"};\n\n"
		"\n"
		"void initEmbeddedAssets(void){\n"
		"		ws_set_static_assets(&embedded_assets);\n"
		"\n"
		"}\n\n"
		"#endif /* WS_STATICS_DATA_IMPLEMENTATION */\n"
		"\n"
		"#ifdef __cplusplus\n"
		"}\n"
		"#endif\n");

	fclose(out);

	for (size_t i = 0; i < count; i++) free(names[i]);
	free(names);
		
	free(cHdr);

	return 0;
}
