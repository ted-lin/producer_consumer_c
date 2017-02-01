#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#if RB_DEBUG
#define rb_debug(fmt...) { \
			printf(fmt); \
		}
#else
#define rb_debug(fmt...)
#endif
#define rb_log(fmt...) printf(fmt);
#define rb_error(fmt...) fprintf(stderr, fmt);

#define RBF_SIZE 32 // ring buffer size
#define PRODUCER_COUNT 3 // num of producer
#define CONSUMER_COUNT 3 // num of consumer

/* ring buffer struct */
typedef struct ringbuf {
	int data[RBF_SIZE];
	pthread_mutex_t lock;
	int in;
	int out;
	int terminate;
} rbf, *rbfp;

/* producer pool */
typedef struct producer_pool {
	struct _producer {
		pthread_t pid;
	} p[PRODUCER_COUNT];
} pp, *ppp;

/* consumer pool */
typedef struct consumer_pool {
	struct _consumer {
		pthread_t cid;
	} c[CONSUMER_COUNT];
} cp, *cpp;

/* make rb global */
rbfp p;

inline int rb_next(int idx) {
	return (idx + 1) % RBF_SIZE;
}

int rb_lock(rbfp p, int idx, const char *who) {
#if RB_DEBUG
	int ret = pthread_mutex_lock(&p->lock);
	rb_debug ("rb locked by %s: %d\n", who, idx);
	return ret;
#else
	return pthread_mutex_lock(&p->lock);
#endif
}

int rb_unlock(rbfp p, int idx, const char *who) {
#if RB_DEBUG
	int ret = pthread_mutex_unlock(&p->lock);
	rb_debug ("rb unlocked by %s: %d\n", who, idx);
	return ret;
#else
	return pthread_mutex_unlock(&p->lock);
#endif
}

int rb_inc(rbfp p, int idx, int pid) {
	if (p->data[idx] != 0) {
		rb_error (
			"rb error: idx %d, data %d, caused by producer %d\n",
			idx, p->data[idx], pid);
		return -1;
	}
	p->data[idx]++;
	p->in = rb_next(idx);
	return 0;
}

int rb_dec(rbfp p, int idx, int cid) {
	if (p->data[idx] != 1) {
		rb_error (
			"rb error: idx %d, data %d, caused by consumer %d\n",
			idx, p->data[idx], cid);
		return -1;
	}
	p->data[idx]--;
	p->out = rb_next(idx);
	return 0;
}

void rb_dump(rbfp p) {
	unsigned int i = 0;
	int in = p->in, out = p->out;

	rb_log("in %d out %d   [", in, out);
	for (i = 0; i < RBF_SIZE; ++i)
		rb_log ("%d ", p->data[i]);
	rb_log("]\n");
}

void *producer(void *arg) {
	int pid = (int) arg;
	int in = 0, out = 0;

	while (1) {
		if (p->terminate)
			break;
		rb_lock (p, pid, __func__);
		in = p->in; out = p->out;
		if (((in > out) && (in - out == RBF_SIZE - 1)) ||
			((in < out) && (in - out == - 1))) {
			rb_debug ("rb full\n");
		} else {
			if (!rb_inc (p, in, pid))
				rb_debug ("rb %d produced by producer %d\n", in, pid);
		}
		rb_unlock (p, pid, __func__);
#if RB_DEBUG
		sleep (1);
#endif
	}

	return NULL;
}

void *consumer(void *arg) {
	int cid = (int) arg;
	int in = 0, out = 0;

	while (1) {
		if (p->terminate)
			break;
		rb_lock (p, cid, __func__);
		in = p->in; out = p->out;
		if (in == out) {
			rb_debug ("rb empty\n");
		} else {
			if (!rb_dec(p, out, cid))
				rb_debug ("rb %d consumed by consumer %d\n", out, cid);
		}
		rb_unlock (p, cid, __func__);
#if RB_DEBUG
		sleep (1);
#endif
	}

	return NULL;
}

void *monitor(void *arg) {

	while (1) {
		if (p->terminate)
			break;
		rb_lock (p, -1, __func__);
		rb_dump (p);
		rb_unlock (p, -1, __func__);
		sleep (1);
	}

	return NULL;
}

rbfp rb_init(void) {
	rbfp p = malloc(sizeof(rbf));
	if (p) {
		memset(p, 0, sizeof(rbf));
		if (pthread_mutex_init(&p->lock, NULL)) {
			rb_error("mutex init failed\n");
			free(p);
			return NULL;
		}
		p->in = 0;
		p->out = 0;
		p->terminate = 0;
		return p;
	}
	rb_error("init rb failed\n");
	return NULL;
}

void rb_clean(rbfp p) {
	if (p) {
		pthread_mutex_destroy(&p->lock);
		free (p);
	}
}

void rb_producer_init (ppp _p) {
	unsigned int i = 0;
	int err = 0;

	for (i = 0; i < PRODUCER_COUNT; ++i) {
		rb_log ("producer %d init\n", i);
		err = pthread_create (&(_p->p[i].pid), NULL, &producer, (void *) i);
		if (err)
			rb_error("producer %d init failed\n", i);
	}
}

void rb_consumer_init (cpp _c) {
	unsigned int i = 0;
	int err = 0;

	for (i = 0; i < CONSUMER_COUNT; ++i) {
		rb_log ("consumer %d init\n", i);
		err = pthread_create (&(_c->c[i].cid), NULL, &consumer, (void *) i);
		if (err)
			rb_error ("consumer %d init failed\n", i);
	}
}

void rb_monitor_init (pthread_t *_m) {
	pthread_create (_m, NULL, &monitor, NULL);
}

void rb_join(ppp _p, cpp _c, pthread_t *_m) {
	unsigned int i = 0;

	rb_log ("wait for rb thread join\n");
	for (i = 0; i < CONSUMER_COUNT; ++i) {
		pthread_join (_c->c[i].cid, NULL);
		rb_debug ("consuer %d joined\n", i);
	}
	for (i = 0; i < PRODUCER_COUNT; ++i) {
		pthread_join (_p->p[i].pid, NULL);
		rb_debug ("producer %d joined\n", i);
	}
	pthread_join (*_m, NULL);
	rb_log ("joined\n");
}

void sighdl(int signum) {
	rb_lock (p, -2, __func__);
	rb_log ("terminate\n");
	p->terminate = 1;
	rb_unlock (p, -2, __func__);
}

int main(void) {
	pp _p;
	cp _c;
	pthread_t _m;

	/* register sig hdl */
	signal(SIGINT, sighdl);

	/* init ring buffer */
	p = rb_init();
	if (!p)
		return -1;

	/* main loop */
	rb_monitor_init (&_m);
	rb_producer_init (&_p);
	rb_consumer_init (&_c);

	/* finish */
	sleep (300);
	rb_join(&_p, &_c, &_m);

	rb_clean(p);
	return 0;
}
