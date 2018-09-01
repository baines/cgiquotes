#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <glob.h>
#include <errno.h>
#include <math.h>
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

	static const char tjson[] =	"{\"id\": `qnum`, \"ts\": `qts`, \"text\": \"`qtext|j`\"},\n";
	static const char tcsv[]  = "`qnum`,`qts`,\"`qtext|c`\"\n";

	char ts_buf    [32]  = "";
	char date_buf  [32]  = "";
	char id_buf    [32]  = "";
	char rate_buf  [32]  = "";
	char quote_buf [512] = "";

	char th_buf  [4][64];
	memset(th_buf, 0, sizeof(th_buf));

	const char* tvals[] = {
		"qnum" , id_buf,
		"qtext", quote_buf,
		"qts"  , ts_buf,
		"qdate", date_buf,
		"qname", q->name,
		"qrate", rate_buf,
		"th0"  , th_buf[0],
		"th1"  , th_buf[1],
		"th2"  , th_buf[2],
		"th3"  , th_buf[3],
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
		[RESPONSE_HTML] = { GETBIN(multi_row_html), GETBIN(multi_html) },
		[RESPONSE_JSON] = { tjson, sizeof(tjson)-1, "[`qdata`]", 9 },
		[RESPONSE_CSV]  = { tcsv , sizeof(tcsv) -1, "`qdata`", 7 },
	};

	// sorting by query param
	{
		int ordering[4];
		qset_sort(q, ordering);

		static const struct th {
			const char* id;
			const char* heading;
		} th[] = {
			{ "id"    , "ID" },
			{ "text"  , "Quote" },
			{ "date"  , "Date" },
			{ "rating", "Rating" },
		};

		for(int i = 0; i < 4; ++i){
			char dir = '-';
			const char* sym = "";

			if(ordering[i] > 0){
				sym = " &#x23F7;";
				dir = '+';
			} else if(ordering[i] < 0){
				sym = " &#x23F6;";
			}

			snprintf(th_buf[i], sizeof(th_buf[i]), "<a href=\"?sort=%c%s\">%s%s</a>", dir, th[i].id, th[i].heading, sym);
		}
	}

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

			struct rating r;
			uint32_t id = strtoul(id_buf, NULL, 10);
			rating_get(q, &r, id);

			snprintf(rate_buf, sizeof(rate_buf), "r%1X", (int)roundf(r.rating));

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

void handle_get_index(int type){

	struct stat dir_st;
	if(stat(QUOTES_ROOT, &dir_st) == -1){
		exit_error(501);
	}

	glob_t glob_data;
	if(glob(QUOTES_ROOT "/#*", 0, NULL, &glob_data) != 0){
		exit_error(501);
	}

	if(type == RESPONSE_RAW){
		const char* enc = getenv("HTTP_ACCEPT_ENCODING");
		printf("Vary: Accept-Encoding\r\n");
		if(!enc || !strstr(enc, "gzip")){
			exit_error(406);
		}

		util_headers(RESPONSE_RAW, dir_st.st_mtim.tv_sec);

		for(size_t i = 0; i < glob_data.gl_pathc; ++i){
			int fd = open(glob_data.gl_pathv[i], O_RDONLY | O_NOFOLLOW);
			struct stat st;

			if(fd == -1 || fstat(fd, &st) == -1){
				exit_error(501);
			}

			printf("%s\n", basename(glob_data.gl_pathv[i]));
			fflush(stdout);
			splice(fd, NULL, STDOUT_FILENO, NULL, st.st_size, 0);
			fflush(stdout);

			close(fd);
		}
	} else {
		char* p;
		size_t sz;
		FILE* f = open_memstream(&p, &sz);

		for(size_t i = 0; i < glob_data.gl_pathc; ++i){
			fprintf(f, "\t\t<a href=\"/quotes/%1$s\">%1$s</a>\n", basename(glob_data.gl_pathv[i]) + 1);
		}
		fclose(f);

		const char* vals[] = { "qdata", p, NULL };
		template_puts(GETBIN(index_html), vals, RESPONSE_HTML, dir_st.st_mtim.tv_sec);
	}

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
		printf("Status: 302 Found\r\nLocation: /quotes/%s/%d\r\n\r\n", q->name, new_id);
	} else if(id != Q_ID_INVALID){
		handle_get_single(q, id, type);
	} else {
		hande_get_multi(q, type);
	}
}

void handle_post(struct qset* q, int id){
	if(id == Q_ID_RANDOM){
		exit_error(400);
	}

	int len = atoi(util_getenv("CONTENT_LENGTH"));
	if(len <= 0 || len >= 510){
		exit_error(400);
	}

	// read post data from stdin
	char buf[512];
	ssize_t n = TEMP_FAILURE_RETRY(read(STDIN_FILENO, buf, len));

	if(n < 0 || q->fd == -1){
		exit_error(501);
	}
	buf[n] = '\0';

	// remove chars that would interfere with the file format
	for(char* c = buf; c < (buf+len); ++c){
		if(*c == '\r' || *c == '\n') *c = ' ';
	}

	if(id == Q_ID_INVALID){

		// creating new quote
		time_t now = time(NULL);
		int id = 0;
		sb_each(l, q->lines){
			int i = atoi(*l);
			if(i >= id) id = (i+1);
		}

		lseek(q->fd, 0, SEEK_END);
		dprintf(q->fd, "%d,%zu,%s\n", id, (size_t)now, buf);

		printf("Content-Type: text/plain; charset=utf-8\r\n\r\n%d,%zu\n", id, (size_t)now);
	} else {

		// updating existing quote
		size_t epoch = 0;
		int text_offset = -1;

		// make sure post data is of the form '[epoch]:[text]'
		bool valid = sscanf(buf, "%zu:%n", &epoch, &text_offset) == 1 && text_offset != -1;
		if(!valid && *buf == ':'){
			text_offset = 1;
		} else if(!valid){
			exit_error(400);
		}

		// find the line with corresponding id
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

		// get the values to be written, might might be carried over from the current line
		const char* text;
		{
			int old_off = 0;
			size_t old_epoch;
			if(sscanf(q->lines[line], "%*d,%zu,%n", &old_epoch, &old_off) != 1 || old_off <= 0){
				exit_error(501);
			}

			text = !buf[text_offset] ? q->lines[line] + old_off : buf + text_offset;
			if(epoch == 0){
				epoch = old_epoch;
			}
		}

		// write new stuff to the file
		ftruncate(q->fd, 0);
		lseek(q->fd, 0, SEEK_SET);

		sb_each(l, q->lines){
			if(l - q->lines == line){
				dprintf(q->fd, "%d,%zu,%s\n", id, epoch, text);
			} else {
				dprintf(q->fd, "%s\n", *l);
			}
		}

		puts("Content-Type: text/plain; charset=utf-8\r\n\r");
	}

	fsync(q->fd);
}

void handle_delete(struct qset* q, int id){
	if(id < 0){
		exit_error(400);
	}

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

	if(sb_count(q->lines) == 1){
		unlink(q->path);
	} else {
		ftruncate(q->fd, 0);
		lseek(q->fd, 0, SEEK_SET);

		sb_each(l, q->lines){
			if((l - q->lines) != line){
				dprintf(q->fd, "%s\n", *l);
			}
		}

		fsync(q->fd);
	}

	puts("Content-Type: text/plain; charset=utf-8\r\n\r");
}

void handle_rate(struct qset* q){
	char buf[256] = "";
	fgets(buf, sizeof(buf), stdin);

	int qnum, x;
	if(sscanf(buf, "q%d.x=%d", &qnum, &x) != 2){
		exit_error(400);
	}

	if(x < 0 || x > 100 || qnum < 0){
		exit_error(400);
	}

	int rating = roundf(x / 20.0f) * 2;

	bool found = false;
	sb_each(l, q->lines){
		int n = atoi(*l);
		if(n == qnum){
			found = true;
			break;
		}
	}

	if(!found){
		exit_error(404);
	}

	struct rating r;
	rating_get(q, &r, qnum);

	const char* ip = util_getenv("REMOTE_ADDR");
	rating_set(q, &r, ip, rating);

	printf("Status: 303 See Other\r\nLocation: /quotes/%s\r\n\r\n", q->name);
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
		if(strcmp(method, "GET") == 0){
			sscanf(path, "/quotes/.%7s", req_type);
			handle_get_index(get_resp_type(req_type));
			return 0;
		} else {
			exit_error(400);
		}
	} else if(n == 1){
		sscanf(path, "/quotes/%*[a-z0-9_].%7s", req_type);
	}

	int resp_type = get_resp_type(req_type);

	for(char* c = filename; *c; ++c){
		*c = tolower(*c);
	}

	if(strchr(id_str, 'r')){
		id = Q_ID_RANDOM;

		if(strcmp(method, "POST") == 0){
			struct qset q = qset_open(filename);
			handle_rate(&q);
			return 0;
		}

	} else if(*id_str){
		id = atoi(id_str);
	}

	/****/ if(strcmp(method, "GET") == 0){

		struct qset q = qset_open(filename);
		handle_get(&q, id, resp_type);

	} else if(strcmp(method, "POST") == 0){

		check_auth();

		struct qset q = qset_create(filename);
		handle_post(&q, id);

	} else if(strcmp(method, "DELETE") == 0){

		check_auth();

		struct qset q = qset_open(filename);
		handle_delete(&q, id);

	} else {
		exit_error(400);
	}

	return 0;
}
