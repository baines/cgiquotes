#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <glob.h>
#include "quotes.h"

void check_auth(void){
	const char* auth_recvd = util_getenv("HTTP_AUTHORIZATION");
	const char* auth_legit = util_getenv("QUOTES_AUTH");
	char buf[256];

	if(sscanf(auth_recvd, "Basic %255s", buf) == 1){
		if(strcmp(buf, auth_legit) != 0){
			exit_error(403);
		}
	} else {
		exit_error(400);
	}
}

void handle_get_single(struct qset* q, int id, int type){
	int line = -1;

	sb_each(l, q->lines){
		if(atoi(*l) == id){
			line = l - q->lines;
			break;
		}
	}

	if(line == -1){
		exit_error(404);
	}

	char quote[512];
	char str_id[8];
	size_t epoch;

	if(sscanf(q->lines[line], "%7[0-9],%zu,%511[^\n]", str_id, &epoch, quote) != 3){
		exit_error(404);
	}

	char epoch_str[16] = "";
	snprintf(epoch_str, sizeof(epoch_str), "%zu", epoch);

#if 0
	// TODO: probably need to use meta-<file> file instead of xattr
	const char* display_name = q->name;
	char disp[128];
	{
		ssize_t disp_len = fgetxattr(q->fd, "user.quotes.name", disp, sizeof(disp)-1);
		if(disp_len > 0){
			disp[disp_len] = 0;
			display_name = disp;
		}
	}
#endif

	char date[64] = "";
	{
		struct tm tm = {};
		time_t t = epoch;
		gmtime_r(&t, &tm);
		strftime(date, sizeof(date), "%F", &tm);
	}

	char prev_id[8] = "";
	char next_id[8] = "";
	if(line > 0){
		char* p = memccpy(prev_id, q->lines[line-1], ',', 7);
		if(!p) exit_error(501);
		p[-1] = 0;
	}
	if((size_t)(line+1) < sb_count(q->lines)){
		char* p = memccpy(next_id, q->lines[line+1], ',', 7);
		if(!p) exit_error(501);
		p[-1] = 0;
	}

	const char* tvals[] = {
		"qnum" , str_id,
		"qtext", quote,
		"qts"  , epoch_str,
		"qdate", date,
		"qname", q->name,
		"qdisp", q->name,
		"qprev", *prev_id ? prev_id : "#",
		"qnext", *next_id ? next_id : "#",
		"qpstyle", *prev_id ? "" : "disabled",
		"qnstyle", *next_id ? "" : "disabled",
		NULL,
	};

	static const char tjson[] = "{\"id\": `qnum`, \"ts\": `qts`, \"text\": \"`qtext|j`\"}";
	static const char tcsv[]  = "`qnum`,`qts`,\"`qtext|c`\"";

	struct t {
		const char* template;
		size_t template_sz;
	} templates[] = {
		[RESPONSE_HTML] = { GETBIN(single_html)    },
		[RESPONSE_JSON] = { tjson, sizeof(tjson)-1 },
		[RESPONSE_CSV]  = { tcsv , sizeof(tcsv) -1 },
	};

	if(type == RESPONSE_RAW){
		util_output(q->lines[line], strlen(q->lines[line]), type, q->last_mod);
	} else {
		template_puts(templates[type].template, templates[type].template_sz, tvals, type, q->last_mod);
	}
}

void hande_get_multi(struct qset* q, int type){

	static const char thtml[] =
		"\t\t\t<tr><td><a href=\"`qnum`\">`qnum`</a></td>"
		"<td><a href=\"`qnum`\">`qtext|h`</a></td>"
		"<td>`qdate`</td>\n";
	static const char tjson[] =	"{\"id\": `qnum`, \"ts\": `qts`, \"text\": \"`qtext|j`\"},\n";
	static const char tcsv[]  = "`qnum`,`qts`,\"`qtext|c`\"\n";

	char ts_buf    [32]  = "";
	char date_buf  [32]  = "";
	char id_buf    [32]  = "";
	char quote_buf [512] = "";

	const char* tvals[] = {
		"qnum" , id_buf,
		"qtext", quote_buf,
		"qts"  , ts_buf,
		"qdate", date_buf,
		"qname", q->name,
		"qdata", "",
		NULL
	};

	sb(char) qdata = NULL;

	struct t {
		const char* trow;
		size_t trow_sz;
		const char* tmain;
		size_t tmain_sz;
	} templates[] = {
		[RESPONSE_HTML] = { thtml, sizeof(thtml)-1, GETBIN(multi_html) },
		[RESPONSE_JSON] = { tjson, sizeof(tjson)-1, "[`qdata`]", 9 },
		[RESPONSE_CSV]  = { tcsv , sizeof(tcsv) -1, "`qdata`", 7 },
	};

	if(type == RESPONSE_RAW){
		sb_each(l, q->lines){
			size_t len = strlen(*l);
			memcpy(sb_add(qdata, len), *l, len);
			sb_push(qdata, '\n');
		}
		util_output(qdata, sb_count(qdata), RESPONSE_RAW, q->last_mod);
	} else {
		struct t* t = templates + type;

		sb_each(l, q->lines){
			if(sscanf(*l, "%7[0-9],%31[0-9],%511[^\n]", id_buf, ts_buf, quote_buf) != 3){
				exit_error(501);
			}
			struct tm tm = {};
			time_t tt = strtoul(ts_buf, NULL, 10);
			gmtime_r(&tt, &tm);
			strftime(date_buf, sizeof(date_buf), "%F", &tm);
			template_append(&qdata, t->trow, t->trow_sz, tvals);
		}

		if(type == RESPONSE_JSON){ // remove comma for last json row
			sb_pop(qdata);
			sb_pop(qdata);
		}
		sb_push(qdata, 0);

		tvals[ARRAY_SIZE(tvals)-2] = qdata;
		template_puts(t->tmain, t->tmain_sz, tvals, type, q->last_mod);
	}

	sb_free(qdata);
}

void handle_get_index(void){

	glob_t glob_data;
	if(glob(QUOTES_ROOT "/data-*", 0, NULL, &glob_data) != 0){
		exit_error(501);
	}

	char* p;
	size_t sz;
	FILE* f = open_memstream(&p, &sz);

	for(size_t i = 0; i < glob_data.gl_pathc; ++i){
		fprintf(f, "\t\t<a href=\"/quotes/%1$s\">%1$s</a>\n", basename(glob_data.gl_pathv[i]) + 5);
	}
	fclose(f);

	struct stat st;
	if(stat(QUOTES_ROOT, &st) == -1){
		exit_error(501);
	}

	const char* vals[] = { "qdata", p, NULL };
	template_puts(GETBIN(index_html), vals, RESPONSE_HTML, st.st_mtim.tv_sec);
	globfree(&glob_data);
}

void handle_get(struct qset* q, int id, int type){
	const char* mod = getenv("HTTP_IF_MODIFIED_SINCE");
	
	if(mod){
		struct stat st;
		struct tm tm = {};
		char* p = strptime(mod, "%a, %d %b %Y %H:%M:%S GMT", &tm);

		if(p && !*p && fstat(q->fd, &st) == 0 && st.st_mtim.tv_sec <= timegm(&tm)){
			puts("Status: 304 Not Modified\r\n\r\n");
			return;
		}
	}

	if(id == Q_ID_RANDOM){
		int new_id = atoi(q->lines[rand()%sb_count(q->lines)]);
		printf("Status: 302 Moved Temporarily\r\nLocation: /quotes/%s/%d\r\n\r\n", q->name, new_id);
	} else if(id != Q_ID_INVALID){
		handle_get_single(q, id, type);
	} else {
		hande_get_multi(q, type);
	}
}

void handle_put(struct qset* q, int id){
	(void)q;
	(void)id;
	puts("Content-Type: text/plain; charset=utf-8\r\n\r");
	puts("put");
}

void handle_delete(struct qset* q, int id){
	(void)q;
	(void)id;
	puts("Content-Type: text/plain; charset=utf-8\r\n\r");
	puts("delete");
}

int get_resp_type(const char* req){
	if(strcasecmp(req, "json") == 0) return RESPONSE_JSON;
	if(strcasecmp(req, "csv" ) == 0) return RESPONSE_CSV;
	if(strcasecmp(req, "raw" ) == 0) return RESPONSE_RAW;
	return RESPONSE_HTML;
}

int main(void){

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	srand(time(NULL) ^ getpid() ^ ts.tv_nsec);

	const char* method = util_getenv("REQUEST_METHOD");
	const char* path   = util_getenv("DOCUMENT_URI");
	char filename[256];
	char req_type[8] = "";
	char id_str[8] = "";
	int  id = Q_ID_INVALID;

	int n = sscanf(path, "/quotes/%255[a-z0-9_]/%7[0-9r].%7s", filename, id_str, req_type);

	if(n <= 0){
		handle_get_index();
		return 0;
	} else if(n == 1){
		sscanf(path, "/quotes/%*[a-z0-9_].%7s", req_type);
	}

	int resp_type = get_resp_type(req_type);

	if(strchr(id_str, 'r')){
		id = Q_ID_RANDOM;
	} else if(*id_str){
		id = atoi(id_str);
	}

	for(char* c = filename; *c; ++c){
		*c = tolower(*c);
	}

	/****/ if(strcmp(method, "GET") == 0){

		struct qset q = qset_open(filename);
		handle_get(&q, id, resp_type);

	} else if(strcmp(method, "PUT") == 0){

		check_auth();

		struct qset q = qset_create(filename);
		handle_put(&q, id);

	} else if(strcmp(method, "DELETE") == 0){

		check_auth();

		struct qset q = qset_open(filename);
		handle_delete(&q, id);

	} else {
		exit_error(400);
	}

	return 0;
}
