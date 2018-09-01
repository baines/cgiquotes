#ifndef QUOTES_H_
#define QUOTES_H_
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <dirent.h>
#include "stb_sb.h"

#define QUOTES_ROOT "/var/www/quotes/data"
#define exit_error(c) ({ syslog(LOG_NOTICE, "Error %d: %s:%d", c, __func__, __LINE__); util_exit(c); })

struct qset;
struct rating;

void         util_exit       (int http_code);
const char*  util_getenv     (const char* name);
void         util_output     (const char* data, size_t len, int type, time_t mod);
void         util_headers    (int type, time_t mod);

struct qset  qset_open       (const char* name);
struct qset  qset_create     (const char* name);
void         qset_sort       (struct qset*, int ordering[static 4]);
void         qset_free       (struct qset*);

sb(char)     template_bake   (const char* data, size_t, const char** subst);
void         template_append (sb(char)* out, const char* data, size_t len, const char** subst);
void         template_puts   (const char* data, size_t len, const char** subst, int type, time_t mod);

void         escape_html     (sb(char)* out, const char* in);
void         escape_json     (sb(char)* out, const char* in);
void         escape_csv      (sb(char)* out, const char* in);

void         rating_init     (DIR*, struct qset*);
void         rating_get      (const struct qset*, struct rating*, uint32_t id);
void         rating_set      (const struct qset*, struct rating*, const char* ip, int rating);

struct qset {
	char*           mem;
	sb(const char*) lines;
	const char*     name;
	const char*     display; // TODO;
	int             fd;
	char*           path;
	time_t          last_mod;
	int             rating_fd;
};

#define RATING_BLOOM_QUADS 16

struct rating {
	uint32_t id;
	uint32_t nvotes;
	float    rating;
	uint32_t _pad1;
	uint32_t bloom[RATING_BLOOM_QUADS];
	uint32_t _pad2[28 - RATING_BLOOM_QUADS];
};

_Static_assert(sizeof(struct rating) == 128, "Rating should be 128 bytes?!");

enum {
	RESPONSE_HTML,
	RESPONSE_CSV,
	RESPONSE_JSON,
	RESPONSE_RAW,
};

enum {
	Q_ID_INVALID = -1,
	Q_ID_RANDOM  = -2,
};

extern char _binary_single_html_start[];
extern char _binary_single_html_end[];

extern char _binary_multi_html_start[];
extern char _binary_multi_html_end[];

extern char _binary_multi_row_html_start[];
extern char _binary_multi_row_html_end[];

extern char _binary_index_html_start[];
extern char _binary_index_html_end[];

extern char _binary_error_html_start[];
extern char _binary_error_html_end[];

#define GETBIN(name) (const char*)_binary_##name##_start, (size_t)((char*)(&_binary_##name##_end) - (char*)(&_binary_##name##_start))

#define ARRAY_SIZE(x)({                                          \
	_Static_assert(                                              \
		!__builtin_types_compatible_p(typeof(x), typeof(&x[0])), \
		"!!!! ARRAY_SIZE used on a pointer !!!!"                 \
	);                                                           \
	sizeof(x) / sizeof(*x);                                      \
})

#endif
