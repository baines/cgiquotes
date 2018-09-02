#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <envz.h>
#include "quotes.h"

static void qset_load_lines(struct qset* q){
	struct stat st;

	if(fstat(q->fd, &st) == -1){
		exit_error(501);
	}

	q->mem = malloc(st.st_size+1);
	q->mem[st.st_size] = 0;
	{
		char* p = q->mem;
		char* e = q->mem + st.st_size;

		ssize_t n;
		while(p != e && (n = TEMP_FAILURE_RETRY(read(q->fd, p, e-p))) > 0){
			p += n;
		}
		if(n < 0) exit_error(501);
	}

	char* state;
	for(char* line = strtok_r(q->mem, "\r\n", &state); line; line = strtok_r(NULL, "\r\n", &state)){
		sb_push(q->lines, line);
	}
}

struct qset qset_open(const char* name){
	struct qset qset = {
		.name = strdup(name),
		.fd = -1,
	};

	DIR* dir = opendir(QUOTES_ROOT);
	if(!dir){
		exit_error(501);
	}

	struct dirent* e;
	while((e = readdir(dir))){
		if(e->d_name[0] != '#') continue;

		if(strcmp(e->d_name + 1, name) == 0 && e->d_type == DT_REG){
			qset.fd = openat(dirfd(dir), e->d_name, O_RDWR);
			if(asprintf(&qset.path, QUOTES_ROOT "/%s", e->d_name) == -1){
				exit_error(501);
			}
			break;
		}
	}

	if(qset.fd == -1){
		exit_error(404);
	}

	struct stat st;
	if(fstat(qset.fd, &st) == -1){
		exit_error(501);
	}

	qset.last_mod = st.st_mtim.tv_sec;
	qset_load_lines(&qset);

	rating_init(dir, &qset);

	closedir(dir);

	return qset;
}

struct qset qset_create(const char* name){
	if(strpbrk(name, "./<>&'\"#;:@=")){
		exit_error(400);
	}

	struct qset qset = {
		.name = strdup(name),
	};

	if(asprintf(&qset.path, QUOTES_ROOT "/#%s", name) == -1 || !qset.path){
		exit_error(501);
	}

	int flags = O_RDWR | O_CREAT | O_EXCL;

retry_open:;

	// create / open the file
	qset.fd = open(qset.path, flags, 0666);
	if(qset.fd == -1){
		if(errno == EEXIST){
			flags = O_RDWR;
			goto retry_open;
		} else {
			exit_error(501);
		}
	}

	// make sure the file we opened is, in fact, a plain old file.
	struct stat st;
	if(fstat(qset.fd, &st) == -1 || !S_ISREG(st.st_mode)){
		exit_error(501);
	}
	qset.last_mod = st.st_mtim.tv_sec;

	qset_load_lines(&qset);

	return qset;
}

void qset_free(struct qset* q){
	free(q->mem);
	free(q->path);
	sb_free(q->lines);
	if(q->fd != -1){
		close(q->fd);
	}
	memset(q, 0, sizeof(*q));
}

enum {
	QSET_SORT_ID,
	QSET_SORT_TEXT,
	QSET_SORT_DATE,
	QSET_SORT_RATING,
};

struct sort_data {
	struct qset* q;
	int type;
	bool desc;
};

static int qset_sort_fn(char** a, char** b, struct sort_data* data){
	int a_id, a_epoch, a_text_off = 0;
	int b_id, b_epoch, b_text_off = 0;

	if(sscanf(*a, "%d,%d,%n", &a_id, &a_epoch, &a_text_off) != 2 || !a_text_off){
		exit_error(501);
	}

	if(sscanf(*b, "%d,%d,%n", &b_id, &b_epoch, &b_text_off) != 2 || !b_text_off){
		exit_error(501);
	}

	int ret = 0;

	if(data->type == QSET_SORT_ID){
		ret = a_id - b_id;
	}

	else if(data->type == QSET_SORT_TEXT){
		ret = strcmp(*a + a_text_off, *b + b_text_off);
	}

	else if(data->type == QSET_SORT_DATE){
		ret = a_epoch - b_epoch;
	}

	else if(data->type == QSET_SORT_RATING){
		struct rating a_rating, b_rating;
		rating_get(data->q, &a_rating, a_id);
		rating_get(data->q, &b_rating, b_id);
		ret = (a_rating.rating * 100) - (b_rating.rating * 100);
	}

	if(data->desc){
		ret = -ret;
	}

	return ret;
}

void qset_sort(struct qset* q, int ordering[static 4]){
	memset(ordering, 0, 4*sizeof(int));
	
	const char* query = getenv("QUERY_STRING");
	if(!query){
		return;
	}

	char* argz;
	size_t lenz;

	if(argz_create_sep(query, '&', &argz, &lenz) != 0){
		return;
	}

	char* val = envz_get(argz, lenz, "sort");
	if(!val){
		free(argz);
		return;
	}

	struct sort_data data = {
		.q = q,
		.type = QSET_SORT_ID,
		.desc = false,
	};

	if(*val == '+'){
		++val;
	} else if(*val == '-'){
		++val;
		data.desc = true;
	}

	/****/ if(strcasecmp(val, "text") == 0){
		data.type = QSET_SORT_TEXT;
	} else if(strcasecmp(val, "rating") == 0){
		data.type = QSET_SORT_RATING;
	} else if(strcasecmp(val, "date") == 0){
		data.type = QSET_SORT_DATE;
	}

	qsort_r(q->lines, sb_count(q->lines), sizeof(char*), (int(*)())&qset_sort_fn, &data);
	free(argz);

	ordering[data.type] = (2*data.desc) - 1;
}
