#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "quotes.h"

static uint32_t murmur2(const void* key, int len, uint32_t seed){
	const uint32_t m = 0x5bd1e995;
	const int r = 24;
	const uint8_t* data = (const uint8_t*)key;
	uint32_t h = seed ^ len;

	while(len >= 4){
		uint32_t k = *(uint32_t*)data;
		k *= m; 
		k ^= k >> r; 
		k *= m; 
		h *= m; 
		h ^= k;
		data += 4;
		len -= 4;
	}

	switch(len){
		case 3: h ^= data[2] << 16;
		case 2: h ^= data[1] << 8;
		case 1: h ^= data[0];
		        h *= m;
	};

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;
	return h;
}

#define NHASHES 4

static bool rating_test_mask(const uint32_t mask[static RATING_BLOOM_QUADS], struct rating* r){
	for(int i = 0; i < RATING_BLOOM_QUADS; ++i){
		if((r->bloom[i] & mask[i]) != mask[i])
			return false;
	}
	return true;
}

void rating_init(DIR* dir, struct qset* q){
	if(strpbrk(q->name, "./<>&'\"#;:@=")){
		exit_error(400);
	}

	char buf[256];
	snprintf(buf, sizeof(buf), "%s.ratings", q->name);

	q->rating_fd = openat(dirfd(dir), buf, O_RDWR | O_CREAT, 0666);
}

void rating_get(const struct qset* q, struct rating* r, uint32_t id){
	memset(r, 0, sizeof(*r));

	if(q->rating_fd > 0){
		pread(q->rating_fd, r, sizeof(*r), id * sizeof(*r));
	}

	r->id = id;

	if(r->nvotes == 0){
		r->rating = 5.0f;
	}
}

void rating_set(const struct qset* q, struct rating* r, const char* ip, int vote){
	const size_t len = strlen(ip);
	uint32_t mask[RATING_BLOOM_QUADS] = {};

	for(int i = 0; i < NHASHES; ++i){
		uint32_t h = murmur2(ip, len, i) % (RATING_BLOOM_QUADS * 32);
		int i = (h / 32);
		int j = (h % 32);
		mask[i] |= (1 << j);
	}

	if(!rating_test_mask(mask, r)){
		float new_rating = ((r->rating * r->nvotes) + vote) / (float)(r->nvotes + 1);
		r->nvotes++;
		r->rating = new_rating;

		for(int i = 0; i < RATING_BLOOM_QUADS; ++i){
			r->bloom[i] |= mask[i];
		}

		if(q->rating_fd > 0){
			pwrite(q->rating_fd, r, sizeof(*r), r->id * sizeof(*r));
			fdatasync(q->rating_fd);
		}
	}
}
