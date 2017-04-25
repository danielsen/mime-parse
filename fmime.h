#ifndef __LIBFMIME_H__
#define __LIBFMIME_H__
#include <glib.h>


struct fmime_part {
	const char *begin;
	int start_off;
	int len;
	GHashTable *headers;
	GList *children;
};

struct fmime_message {
	GHashTable *headers;
	void (*_destroyCallBack)(struct fmime_message *);
	void *_privData;
	struct fmime_part *root;
	size_t len;
};

struct fmime_message_fi {
	int fd;
	void *map;
	size_t map_len;
};

typedef struct fmime_message fmime_message_t;
typedef struct fmime_part fmime_part_t;

#ifdef __cplusplus
extern "C" {
#endif

void fmime_init(int flags);
void fmime_free(fmime_message_t *msg);

int fmime_addheader(fmime_message_t *msg, const char *header, const char *rawValue);

// Parses a msgfile and returns a newly allocated fmime_message_t pointer
// It used mmap internaly and will munmap the file when fmime_free is called
fmime_message_t *fmime_parse_file(const char *fname);

// Parses a buffer and returns a newly allocated fmime_message_t pointer
// It does not copy the suplied memory, so operations on mime parts
// are dangerous if the buffer passed has been freed.
fmime_message_t *fmime_parse_memory(const char *memory, size_t len);

// Return a GList object wich data pointer points to the raw value of the header, minus line breaks
const GList *fmime_get_headers(fmime_message_t *msg, const char *header);
// Return a GList object wich data pointer points to the raw value of the header, minus line breaks
const char *fmime_get_header(fmime_message_t *msg, const char *header);

const GList *fmime_part_get_headers(fmime_part_t *msg, const char *header);
// Return a GList object wich data pointer points to the raw value of the header, minus line breaks
const char *fmime_part_get_header(fmime_part_t *msg, const char *header);

void fmime_part_free(fmime_part_t *part);
int fmime_part_is_type(fmime_part_t *part, const char *type, const char *subtype);
int fmime_part_is_disposition(fmime_part_t *part, const char *desiredDisposition);

// free with g_free
char *fmime_part_get_filename(fmime_part_t *part);

#ifdef __cplusplus
};
#endif

#endif
