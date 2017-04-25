#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pcre.h>

#include "fmime.h"

#ifndef NDEBUG
#define D(X) (X)
#else
#define D(X)
#endif

#define DEFAULT_PCRE_COMPILE_OPTIONS (PCRE_CASELESS | PCRE_EXTRA)

static const char *identify_boundary_re_str = "boundary\\s*=\\s*(([^\"]\\S*)+|\"([^\"]+)?\");{0,1}";
static pcre *identify_boundary_re =  NULL; 
static pcre_extra *identify_boundary_extra =  NULL; 

static const char *mtype_str = "\\s*(.+)?/([^; ]+)";
static pcre *mtype_re =  NULL; 
static pcre_extra *mtype_extra =  NULL; 

static const char *fname_str = "(file){0,1}name=(\"([^\"]+)\"|([^\"]\\S*));{0,1}";
static pcre *fname_re = NULL;
static pcre_extra *fname_extra = NULL;

static __attribute__ ((used)) size_t _fmime_generic_parse_header(GHashTable *headers, const char *memory, size_t len);
static int _fmime_generic_addheader(GHashTable *headers, const char *header, const char *rawValue);

// You must free the returned string with pcre_free_substring
const char *_fmime_get_boundary(const char *ctype, size_t ctype_len);

static fmime_message_t *_fmime_parse_memory(fmime_message_t *ret, const char *memory, size_t len);

// pass ctype so we can get the boundary from the content type header
static __attribute__ ((used)) fmime_part_t *_fmime_parse_part_memory(const char *memory, size_t len, const char *ctype);
// make sure we don't have especial regexp chars in our boundary
static  __attribute__ ((used)) char *_fmime_escape_boundary(const char *boundary);

static int initialized = 0;

static void fmime_exit(void)
{
	if(identify_boundary_re)
		pcre_free(identify_boundary_re);
	if(identify_boundary_extra)
		pcre_free(identify_boundary_extra);
	if(mtype_re)
		pcre_free(mtype_re);
	if(mtype_extra)
		pcre_free(mtype_extra);
	if(fname_re)
		pcre_free(fname_re);
	if(fname_extra)
		pcre_free(fname_extra);
}

static guint _fmime_str_hsh(gconstpointer key)
{
	char *k;
	guint ret = 0;

	k = g_ascii_strdown(key, strlen(key));
	ret = g_str_hash(k);
	g_free(k);

	return ret;
}

static gboolean _fmime_str_eq(gconstpointer a, gconstpointer b)
{
	return !g_ascii_strcasecmp((const gchar *)a, (const gchar *)b);
}

void fmime_init(int flags)
{
	const char *err;
	int err_off;
	if(initialized) {
		return;
	} 

	identify_boundary_re = pcre_compile(identify_boundary_re_str, DEFAULT_PCRE_COMPILE_OPTIONS, &err, &err_off, NULL);
	if(!identify_boundary_re) {
		fprintf(stderr, "Error compiling `%s`: %s at %i\n", identify_boundary_re_str, err, err_off);
		assert(identify_boundary_re);
	}
	err = NULL;
	identify_boundary_extra = pcre_study(identify_boundary_re, 0, &err);
	if(err) {
		fprintf(stderr, "Error studying `%s`: %s\n", identify_boundary_re_str, err);
		assert(!err);
	}

	mtype_re = pcre_compile(mtype_str, DEFAULT_PCRE_COMPILE_OPTIONS, &err, &err_off, NULL);
	if(!mtype_re) {
		fprintf(stderr, "Error compiling `%s`: %s at %i\n", mtype_str, err, err_off);
		assert(mtype_re);
	}
	err = NULL;
	mtype_extra = pcre_study(mtype_re, 0, &err);
	if(err) {
		fprintf(stderr, "Error studying `%s`: %s\n", mtype_str, err);
		assert(!err);
	}

	fname_re = pcre_compile(fname_str, DEFAULT_PCRE_COMPILE_OPTIONS, &err, &err_off, NULL);
	if(!fname_re) {
		fprintf(stderr, "Error compiling `%s`: %s at %i\n", fname_str, err, err_off);
		assert(fname_re);
	}
	err = NULL;
	fname_extra = pcre_study(fname_re, 0, &err);
	if(err) {
		fprintf(stderr, "Error studying `%s`: %s\n", fname_str, err);
		assert(!err);
	}

	initialized = 1;
	atexit(fmime_exit);
}


static gboolean _fmime_free_foreach(gpointer key, gpointer value, gpointer user_data)
{
	GList *cur;

	g_free(key);
	
	cur = (GList *)value;
	do {
		g_free(cur->data);
	} while((cur = g_list_next(cur)));

	g_list_free((GList *)value);
	return TRUE;
}

void fmime_free(fmime_message_t *msg)
{
	D(fprintf(stderr, "msg->root: %p\n", msg->root));
	if(msg->root) {
		D(fprintf(stderr, "Calling fmime_part_free: %p\n", msg->root));
		fmime_part_free(msg->root);
	}
	if(msg->_destroyCallBack) {
		msg->_destroyCallBack(msg);
	}
	if(msg->headers) {
		g_hash_table_foreach_steal(msg->headers, _fmime_free_foreach, NULL);
		g_hash_table_destroy(msg->headers);
	}
	g_free(msg);
}

const char *fmime_get_header(fmime_message_t *msg, const char *header)
{
	GList *first = g_hash_table_lookup(msg->headers, header);
	if(first) {
		return first->data;
	}
	return NULL;
}

const GList *fmime_get_headers(fmime_message_t *msg, const char *header)
{
	return g_hash_table_lookup(msg->headers, header);
}

int fmime_addheader(fmime_message_t *msg, const char *header, const char *rawValue)
{
	return _fmime_generic_addheader(msg->headers, header, rawValue);
}

const char *fmime_part_get_header(fmime_part_t *msg, const char *header)
{
	GList *first = g_hash_table_lookup(msg->headers, header);
	if(first) {
		return first->data;
	}
	return NULL;
}

const GList *fmime_part_get_headers(fmime_part_t *msg, const char *header)
{
	return g_hash_table_lookup(msg->headers, header);
}

int fmime_part_addheader(fmime_part_t *msg, const char *header, const char *rawValue)
{
	return _fmime_generic_addheader(msg->headers, header, rawValue);
}

static void _fmime_file_destroy(fmime_message_t *msg)
{
	struct fmime_message_fi *fi = msg->_privData;
	munmap(fi->map, fi->map_len);
	close(fi->fd);
	g_free(fi);
}

fmime_message_t *fmime_parse_file(const char *fname)
{
	fmime_message_t *ret = NULL;
	struct fmime_message_fi *fi;
	struct stat st;
	assert(initialized);

	fi = g_malloc0(sizeof(struct fmime_message_fi));
	fi->fd = open(fname, O_RDONLY);
	if(fi->fd<0) {
		g_free(fi);
		return NULL;
	}
	fstat(fi->fd, &st);
	fi->map_len = st.st_size;

	fi->map = mmap(NULL, fi->map_len, PROT_READ, MAP_SHARED, fi->fd, 0);

	ret = g_malloc0(sizeof(fmime_message_t));

	ret->_privData = fi;
	ret->_destroyCallBack = _fmime_file_destroy;

	return _fmime_parse_memory(ret, fi->map, fi->map_len);
}

fmime_message_t *fmime_parse_memory(const char *memory, size_t len)
{
	fmime_message_t *ret = NULL;
	ret = g_malloc0(sizeof(fmime_message_t));

	return _fmime_parse_memory(ret, memory, len);
}

static fmime_message_t *_fmime_parse_memory(fmime_message_t *ret, const char *memory, size_t len)
{
	size_t i;
	const char *ctype;
	assert(initialized);

	ret->len = len;

	ret->headers = g_hash_table_new_full(
		_fmime_str_hsh, _fmime_str_eq,
		g_free, NULL
	);

	i = _fmime_generic_parse_header(ret->headers, memory, len);

	if((ctype = fmime_get_header(ret, "Content-Type"))) {
		for(;*ctype && isspace(*ctype); ctype++) {
			// do nothing
		}
		if(!strncasecmp("multipart/", ctype, strlen("multipart/"))) {
			int r;
			const char *boundary = NULL;
			char *copyheaders[] = {
				"Content-Type",
				"Content-Disposition",
				NULL
			};
			// ok we got a mime multipart msg;
			ret->root = g_malloc0(sizeof(fmime_part_t));
			ret->root->len = len - i;
			ret->root->begin = memory+i;
			ret->root->headers = g_hash_table_new_full(
					_fmime_str_hsh, _fmime_str_eq,
					g_free, NULL
				);
			D(fprintf(stderr, "Adding part %p\n", ret->root));


			for(;i< len && isspace(*(ret->root->begin));ret->root->begin++, i++) {
				D(fprintf(stderr, "skiping: %i\n", *(ret->root->begin)));
			}
			ret->root->len = len - i;
			//D(fprintf(stderr, "Will parse: \n-------------\n%s\n----------------------\n", ret->root->begin));

			for(r=0;copyheaders[r];r++) {
				const char *h = fmime_get_header(ret, copyheaders[r]);
				if(h) {
					fmime_part_addheader(ret->root, copyheaders[r], h);
				}
			}

			if((boundary = _fmime_get_boundary(ctype, strlen(ctype)))) {
				const char *begin = memory + i;
				const int blen = strlen(boundary);
				D(fprintf(stderr, "**** Boundary: %s\n", boundary));
				while((begin = g_strstr_len(begin, len - (begin - memory), boundary))) {
					const char *data = begin +  blen + 1;
					const char *end = g_strstr_len(data, len - (data - memory), boundary);

					if(end) {
						size_t data_len;
						fmime_part_t *part;

						data_len = end - data;
						D(fprintf(stderr, "Got a part with %zi bytes\n", data_len));

						part = _fmime_parse_part_memory(data, data_len, ctype);
						D(fprintf(stderr, "Adding subpart: %p\n", part));
						ret->root->children = g_list_append(ret->root->children, part);

						if(*(end + blen) == '-' &&
								*(end + blen + 1) == '-' ) {

							D(fprintf(stderr, "LAST PART DONE\n"));
							break;
						}
						begin = end;
					} else {
						fprintf(stderr, "MISSING LAST PART\n");
						break;
					}
				}
				D(fprintf(stderr, "**** Done searching for %s\n", boundary));
				pcre_free_substring(boundary);
			}
		}
	} else {
		// single part text only
	}
	
	D(fprintf(stderr, "Root: %p\n", ret->root));
	return ret;
}

static fmime_part_t *_fmime_parse_part_memory(const char *memory, size_t len, const char *pctype)
{
	size_t i;
	const char *ctype;
	fmime_part_t *ret;
	assert(initialized);

	ret = g_malloc0(sizeof(fmime_part_t));
	ret->len = len;
	ret->begin = memory;

	ret->headers = g_hash_table_new_full(
		//g_str_hash, g_str_equal,
		_fmime_str_hsh, _fmime_str_eq,
		g_free, NULL
	);

	i = _fmime_generic_parse_header(ret->headers, memory, len);

	if((ctype = fmime_part_get_header(ret, "Content-Type"))) {
		for(;*ctype && isspace(*ctype); ctype++) {
			// do nothing
		}
		if(!strncasecmp("multipart/", ctype, strlen("multipart/"))) {
			const char *boundary = NULL;
			// ok we got a mime multipart msg;

			if((boundary = _fmime_get_boundary(ctype, strlen(ctype)))) {
				const char *begin = memory + i;
				const int blen = strlen(boundary);
				while((begin = g_strstr_len(begin, len - (begin - memory), boundary))) {
					const char *data = begin +  blen + 1;
					const char *end = g_strstr_len(data, len - (data - memory), boundary);
					if(end) {
						size_t data_len;
						fmime_part_t *part;

						data_len = end - data;
						D(fprintf(stderr, "Got a part with %zi bytes\n", data_len));

						part = _fmime_parse_part_memory(data, data_len, ctype);
						ret->children = g_list_append(ret->children, part);

						// XXX: todo: look for other parts inside this one in a recursive function

						if(*(end + strlen(boundary)) == '-' &&
								*(end + strlen(boundary) + 1) == '-' ) {

							D(fprintf(stderr, "LAST PART DONE\n"));
							break;
						}
						begin = end;
					} else {
						// XXX: is this valid?
						// What should we do? consider memory + len as the end of this part?
						fprintf(stderr, "MISSING LAST PART\n");
						break;
					}
				}
				fprintf(stderr, "**** Done searching for %s\n", boundary);
				pcre_free_substring(boundary);
			}
		}
	}

	return ret;
}


static char * _fmime_escape_boundary(const char *boundary)
{
	GString *escaped = g_string_new(NULL);
	for(;boundary && *boundary; boundary++) {
		switch(*boundary) {
			case '.':
			case '+':
			case '*':
			case '(':
			case ')':
			case '{':
			case '}':
			case '[':
			case ']':
			case '?':
			case '\\':
				g_string_append_c(escaped, '\\');
			default:
				g_string_append_c(escaped, *boundary);
		}
	}

	return g_string_free(escaped, FALSE);
}


int _fmime_generic_addheader(GHashTable *headers, const char *header, const char *rawValue)
{
	GList *values;
	//fprintf(stderr, "header: '%s' value: '%s'\n", header, rawValue);

	values = g_hash_table_lookup(headers, header);
	values = g_list_append(values, g_strdup(rawValue));

	g_hash_table_insert(headers, g_strdup(header), values);
	return 0;
}


static size_t _fmime_generic_parse_header(GHashTable *headers, const char *memory, size_t len)
{
	size_t i;
	int inHeaders = 1;
	int begin_off = 0;
	int headersDone = 0;
	char *current_header = NULL;
	GString *value = NULL;
	assert(initialized);

	for(i=0;i<len && !headersDone;i++) {
		if(inHeaders) {
			if(memory[i] == ':') {
				inHeaders = 0;
				current_header = g_strndup(memory + begin_off, i - begin_off);
				if(value) {
					D(fprintf(stderr, "** value not null: %s!!!!\n", value->str));
					g_string_free(value, TRUE);
				}
				assert(value==NULL);

				value = g_string_new(NULL);
				//fprintf(stderr, "-- Header: %s\n", current_header);
				begin_off = i+1;
				if(i+1 < len && isspace(memory[i+1])) {
					begin_off ++;
				}
			}
		} else {
			if((memory[i] == '\n') && ((i+1) < len)) {
				//fprintf(stderr, "nextchar: 0x%02x '%c'\n", memory[i+1], memory[i+1]);
				switch(memory[i+1]) {
					case ' ':
					case '\t':
						D(fprintf(stderr, "** multiline header\n"));
						break;
					case '\n':
					case '\r':
						D(fprintf(stderr, "** end of headers\n"));
						headersDone = 1;
						// add last header
					default:
						// add header
						g_string_append_len(value, memory + begin_off, i - begin_off);
						_fmime_generic_addheader(headers, current_header, value->str);
						g_string_free(value, TRUE); value = NULL;
						g_free(current_header); current_header = NULL;
						inHeaders = 1;
						begin_off = i + 1;
						break;
				}
			}
		}
	}
	if(value != NULL) {
		D(fprintf(stderr, "Error parsing: value: '%s'\n", value->str));
		D(fprintf(stderr, "Error parsing: memory: '%s'\n", memory));
		g_string_free(value, TRUE);
	}
	assert(value == NULL);
	return i;
}


/*
	GHashTable *headers;
	GList *children;
*/

void fmime_part_free(fmime_part_t *part)
{
	D(fprintf(stderr, "%s: %p\n", __func__, part));
	if(part) {
		if(part->children) {
			GList *child;
			for(child = part->children ; child; child = g_list_next(child)) {
				fmime_part_free(child->data);
			}
			g_list_free(child);
		}
		if(part->headers) {
			g_hash_table_foreach_steal(part->headers, _fmime_free_foreach, NULL);
			g_hash_table_destroy(part->headers);
		}
		g_free(part);
	}
}

int fmime_part_is_type(fmime_part_t *part, const char *type, const char *subtype)
{
	int t = 0;
	int s = 0;
	int ovector[30];
	int r;
	const char *mtype = fmime_part_get_header(part, "Content-Type");

	mtype = mtype ? mtype : "text/plain;";

	if((r = pcre_exec(mtype_re, mtype_extra,
			mtype, strlen(mtype), 0, 
			0, ovector, 30)) != PCRE_ERROR_NOMATCH) {
		if(type && type[0] == '*') {
			t = 1;
		} else {
			const char *type2;
			pcre_get_substring(mtype, ovector, r, 1, &type2);
			t = !strcasecmp(type, type2);
			pcre_free_substring(type2);
		}
		if(subtype && subtype[0] == '*') {
			s = 1;
		} else {
			const char *stype2;
			pcre_get_substring(mtype, ovector, r, 2, &stype2);
			s = !strcasecmp(subtype, stype2);
			pcre_free_substring(stype2);
		}

	}
	return t && s;
}

int fmime_part_is_disposition(fmime_part_t *part, const char *desiredDisposition)
{
	const char *disp = fmime_part_get_header(part, "Content-Disposition");
	if(disp && desiredDisposition) {
		return strcasestr(disp, desiredDisposition) ? 1 : 0;
	}
	return 0;
}

char *fmime_part_get_filename(fmime_part_t *part)
{
	int i;
	const char *headers[] = {
		"Content-Type",
		"Content-Disposition",
		NULL
	};
	char *ret = NULL;
	for(i=0;headers[i];i++) {
		int c;
		int ovector[30];
		const char *h = fmime_part_get_header(part, headers[i]);
		//D(fprintf(stderr, "*** header: %s: %s\n", headers[i], h));
		if(h && ((c = pcre_exec(fname_re, fname_extra, h, strlen(h), 0, 0, ovector, 30))!=PCRE_ERROR_NOMATCH)) {
			char filename[1024] = "";
			pcre_copy_substring(h, ovector, c, 3, filename, sizeof(filename));
			if(filename[0]) {
				ret = g_strdup(filename);
				break;
			}
			pcre_copy_substring(h, ovector, c, 4, filename, sizeof(filename));
			if(filename[0]) {
				ret = g_strdup(filename);
				break;
			}
		}
	}

#ifndef NDEBUG
	if(ret) {
		D(fprintf(stderr, "*** filename: %s\n", ret));
	}
#endif
	return ret;
}

const char *_fmime_get_boundary(const char *ctype, size_t ctype_len)
{
	int ovector[30];
	int r;
	const char *boundary = NULL;

	if((r = pcre_exec(identify_boundary_re, identify_boundary_extra, ctype, ctype_len, 0, 0, ovector, 30))!=PCRE_ERROR_NOMATCH) {
		D(fprintf(stderr, "r: %i\n", r));
		pcre_get_substring(ctype, ovector, r, 2, (const char **)&boundary);
		if(boundary && !boundary[0]) {
			pcre_free_substring(boundary);
			boundary = NULL;
		}
		if(!boundary) {
			pcre_get_substring(ctype, ovector, r, 3, (const char **)&boundary);
		}
	}
	return boundary;
}
