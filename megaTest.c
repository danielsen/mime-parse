#include "fmime.h"

#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

// v31a needs:
// status - simple header
// size - save len
// to
// cc
// from
// replyto
// subject
// popFolder
// tag
// popAccount
// bodyPrev ??? (disabled?)
int CONF_INLINE_MAX_KBYTES=1024;

int part_recurser(fmime_part_t *part, int level);

int hasAttach(fmime_message_t *mmsg);

int _hasAttach(fmime_part_t *part);

int main(int argc, char **argv)
{
	fmime_message_t *msg;
	const GList *headers;
	const char *dir = "testmsgs/";
	DIR *d;
	struct dirent *dent;

	if(argc == 2) {
		dir = argv[1];
	}

	if(chdir(dir)) {
		perror("chdir");
		exit(1);
	}

	fmime_init(0);

	d = opendir(".");
	assert(d);

	while((dent = readdir(d))) {
		struct stat st;

		if(stat(dent->d_name, &st)) {
			perror("stat");
			continue;
		}

		// avoid parsing non rfc msgs
		if(!strcmp("core", dent->d_name) || !strcmp("gmon.out", dent->d_name)) {
			continue;
		}

		if(!S_ISREG(st.st_mode))
			continue;


		fprintf(stderr, "msg: %s\n", dent->d_name);
		msg = fmime_parse_file(dent->d_name);
		assert(msg);
		printf("getheader(Received): %s\n", fmime_get_header(msg, "received"));
		fflush(NULL);
		headers = fmime_get_headers(msg, "rEceived");
		if(headers) {
			printf("getheaders(Received)[0]: %s\n", (char *)headers->data);
		}

		// msglist data
		printf("Status: %s\n", fmime_get_header(msg, "Status"));
		fflush(NULL);

		/*if(msg->root) {
			part_recurser(msg->root, 0);
		}*/
		printf("Has attach: %s\n", (hasAttach(msg)?"Yes":"No"));
		if(msg->root)
			part_recurser(msg->root, 0);
		fmime_free(msg);
	};
	closedir(d);


	return 0;
}

int part_recurser(fmime_part_t *part, int level)
{
	char pre[] = 
	"--------------------------------------------------------------------------------";
	if(level > (sizeof(pre) - 1)) {
		level = sizeof(pre) - 1;
	}
	pre[level] = '\0';
	printf("%s%s\n", pre, fmime_part_get_header(part, "Content-Type"));
	if(part->children) {
		GList *p;
		for(p = part->children;p;p=g_list_next(p)) {
			part_recurser(p->data, level+1);
		}
	}
	return 0;
}

int hasAttach(fmime_message_t *mmsg)
{
	GList *root;
	int ret = 0;

	if(mmsg->root) {
		root = mmsg->root->children;
		for(;root;root = g_list_next(root)) {
			if((ret = _hasAttach((fmime_part_t *)root->data))) {
				break;
			}
		}
	}

	return ret;
}

int _hasAttach(fmime_part_t *part)
{
	int ret = 0;

	// size over CONF_INLINE_MAX_KBYTES
	if(part->len > 1024 * CONF_INLINE_MAX_KBYTES) {
		fprintf(stderr, "size > %li\n", 1024l * CONF_INLINE_MAX_KBYTES);
		ret = 1;
	} else if(fmime_part_is_type(part, "text", "*") || fmime_part_is_type(part, "message", "*")) {
		// Type is text/*, message/*
		char *filename;
		if(fmime_part_is_disposition(part, "attachment")) {
			fprintf(stderr, "disposition = attachment\n");
			// is marked as attachment
			ret = 1;
		} else if((filename = fmime_part_get_filename(part))) {
			fprintf(stderr, "has filename\n");
			// has a filename
			g_free(filename);
			ret = 1;
		}
	} else {
		if(!fmime_part_is_type(part, "multipart", "*")) {
			fprintf(stderr, "content-type != text/* and content-type != multipart/* ret = 1\n");
			ret = 1;
		}
	}
	if(!ret && part->children) {
		GList *child;
		for(child = part->children; !ret && child; child = g_list_next(child)) {
			ret = _hasAttach((fmime_part_t *)child->data);
		}
	}
	return ret;
}

