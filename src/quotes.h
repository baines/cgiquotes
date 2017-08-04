#ifndef QUOTES_H_
#define QUOTES_H_
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include "stb_sb.h"

#define QUOTES_ROOT "/var/www/quotes/data"
#define exit_error(c) ({ syslog(LOG_NOTICE, "Error %d: %s:%d", c, __func__, __LINE__); util_exit(c); })

struct qset;

void         util_exit       (int http_code);
const char*  util_getenv     (const char* name);
void         util_output     (const char* data, size_t len, int type, time_t mod);

struct qset  qset_open       (const char* name);
struct qset  qset_create     (const char* name);
void         qset_free       (struct qset*);

sb(char)     template_bake   (const char* data, size_t, const char** subst);
void         template_append (sb(char)* out, const char* data, size_t len, const char** subst);
void         template_puts   (const char* data, size_t len, const char** subst, int type, time_t mod);

void         escape_html     (sb(char)* out, const char* in);
void         escape_json     (sb(char)* out, const char* in);
void         escape_csv      (sb(char)* out, const char* in);

struct qset {
	char*           mem;
	sb(const char*) lines;
	const char*     name;
	const char*     display; // TODO;
	int             fd;
	time_t          last_mod;
};

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
