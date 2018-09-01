#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
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
