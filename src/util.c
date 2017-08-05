#include "quotes.h"
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

void util_exit(int http_code){
	const char* msg = "Internal Server Error";

	switch(http_code){
		case 400:
			msg = "Bad Request"; break;
		case 403:
			msg = "Unauthorized"; break;
		case 404:
			msg = "Not Found"; break;
	}

	char errbuf[8] = "";
	snprintf(errbuf, sizeof(errbuf), "%d", http_code);

	const char* vals[] = {
		"errno" , errbuf,
		"errmsg", msg,
		NULL
	};

	printf("Status: %d %s\r\n", http_code, msg);
	template_puts(GETBIN(error_html), vals, RESPONSE_HTML, 0);
	fflush(stdout);
	exit(0);
}

const char* util_getenv(const char* name){
	const char* result = getenv(name);
	if (!result) exit_error(400);
	return result;
}

static const char* response_mime[] = {
	[RESPONSE_HTML] = "text/html",
	[RESPONSE_JSON] = "application/json",
	[RESPONSE_CSV]  = "text/csv",
	[RESPONSE_RAW]  = "text/plain",
};

void util_output(const char* data, size_t len, int type, time_t mod){
	printf("Content-Type: %s; charset=utf-8\r\n", response_mime[type]);

	if(mod){
		char time_buf[256] = "";
		struct tm tm = {};
		gmtime_r(&mod, &tm);
		strftime(time_buf, sizeof(time_buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);

		if(*time_buf){
			printf("Last-Modified: %s\r\n", time_buf);
		}
	}

	//printf("Cache-Control: public, max-age=31536000\r\n");
	printf("\r\n%.*s\n", (int)len, data);
}

