/*	$NetBSD: rumpfiber.c,v 1.9 2014/12/29 21:50:09 justin Exp $	*/

/*
 * Copyright (c) 2007-2013 Antti Kantee.  All Rights Reserved.
 * Copyright (c) 2014 Justin Cormack.  All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "rumpuser_port.h"

#include <sys/mman.h>
#include <sys/time.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <rump/rumpuser.h>

#include "rumpuser_int.h"
#include "thread.h"

static void printk(const char *s);

static void
printk(const char *msg)
{
	int ret __attribute__((unused));

	ret = write(2, msg, strlen(msg));
}

struct rumpuser_hyperup rumpuser__hyp;

int
rumpuser_init(int version, const struct rumpuser_hyperup *hyp)
{
	int rv;

	if (version != RUMPUSER_VERSION) {
		printk("rumpuser version mismatch\n");
		abort();
	}

	rv = rumpuser__random_init();
	if (rv != 0) {
		ET(rv);
	}

        rumpuser__hyp = *hyp;

	init_sched();

        return 0;
}

int
rumpuser_clock_gettime(int enum_rumpclock, int64_t *sec, long *nsec)
{
	enum rumpclock rclk = enum_rumpclock;
	struct timespec ts;
	clockid_t clk;
	int rv;

	switch (rclk) {
	case RUMPUSER_CLOCK_RELWALL:
		clk = CLOCK_REALTIME;
		break;
	case RUMPUSER_CLOCK_ABSMONO:
		clk = CLOCK_MONOTONIC;
		break;
	default:
		abort();
	}

	if (clock_gettime(clk, &ts) == -1) {
		rv = errno;
	} else {
		*sec = ts.tv_sec;
		*nsec = ts.tv_nsec;
		rv = 0;
	}

	ET(rv);
}

int
rumpuser_clock_sleep(int enum_rumpclock, int64_t sec, long nsec)
{
	enum rumpclock rclk = enum_rumpclock;
	uint64_t msec;
	int nlocks;

	rumpkern_unsched(&nlocks, NULL);
	switch (rclk) {
	case RUMPUSER_CLOCK_RELWALL:
		msec = sec * 1000 + nsec / (1000*1000UL);
		msleep(msec);
		break;
	case RUMPUSER_CLOCK_ABSMONO:
		msec = sec * 1000 + nsec / (1000*1000UL);
		abssleep(msec);
		break;
	}
	rumpkern_sched(nlocks, NULL);

	return 0;
}

int
rumpuser_getparam(const char *name, void *buf, size_t blen)
{
	int rv;
	const char *ncpu = "1";

	if (strcmp(name, RUMPUSER_PARAM_NCPU) == 0) {
		strncpy(buf, ncpu, blen);
		rv = 0;
	} else if (strcmp(name, RUMPUSER_PARAM_HOSTNAME) == 0) {
		strncpy(buf, "rump", blen);
		rv = 0;
	} else if (*name == '_') {
		rv = EINVAL;
	} else {
		char *value = getenv(name);

		if (value != NULL && strlen(value) + 1 <= blen) {
			strncpy(buf, value, blen);
			rv = 0;
		} else
			rv = EINVAL;
	}

	ET(rv);
}

void
rumpuser_putchar(int c)
{

	putchar(c);
}

__attribute__((__noreturn__)) void
rumpuser_exit(int rv)
{

	if (rv == RUMPUSER_PANIC)
		abort();
	else
		exit(rv);
}

void
rumpuser_seterrno(int error)
{

	errno = error;
}

/*
 * This is meant for safe debugging prints from the kernel.
 */
void
rumpuser_dprintf(const char *format, ...)
{
	//va_list ap;

	/* XXX implement support for this in libc */
	printk("something\n");
	//va_start(ap, format);
	//vfprintf(stderr, format, ap);
	//va_end(ap);
}

int
rumpuser_kill(int64_t pid, int rumpsig)
{
	int sig;

	sig = rumpuser__sig_rump2host(rumpsig);
	if (sig > 0)
		raise(sig);
	return 0;
}

int
rumpuser_thread_create(void *(*f)(void *), void *arg, const char *thrname,
        int joinable, int pri, int cpuidx, void **tptr)
{
        struct thread *thr;

        thr = create_thread(thrname, NULL, (void (*)(void *))f, arg, NULL, 0, joinable);

        if (!thr)
                return EINVAL;

        *tptr = thr;
        return 0;
}

void
rumpuser_thread_exit(void)
{

        exit_thread();
}

int
rumpuser_thread_join(void *p)
{

        join_thread(p);
        return 0;
}

/* mtx */

struct rumpuser_mtx {
	struct waithead waiters;
	int v;
	int flags;
	struct lwp *o;
};

void
rumpuser_mutex_init(struct rumpuser_mtx **mtxp, int flags)
{
	struct rumpuser_mtx *mtx;

	mtx = calloc(1, sizeof(*mtx));
	if (! mtx) abort();
	mtx->flags = flags;
	TAILQ_INIT(&mtx->waiters);
	*mtxp = mtx;
}

void
rumpuser_mutex_enter(struct rumpuser_mtx *mtx)
{
	int nlocks;

	if (rumpuser_mutex_tryenter(mtx) != 0) {
		rumpkern_unsched(&nlocks, NULL);
		while (rumpuser_mutex_tryenter(mtx) != 0)
			wait(&mtx->waiters, 0);
		rumpkern_sched(nlocks, NULL);
	}
}

void
rumpuser_mutex_enter_nowrap(struct rumpuser_mtx *mtx)
{
	int rv;

	rv = rumpuser_mutex_tryenter(mtx);
	/* one VCPU supported, no preemption => must succeed */
	if (rv != 0) {
		printk("no voi ei\n");
	}
}

int
rumpuser_mutex_tryenter(struct rumpuser_mtx *mtx)
{
	struct lwp *l = get_current()->lwp;

	if (mtx->v && mtx->o != l)
		return EBUSY;

	mtx->v++;
	mtx->o = l;

	return 0;
}

void
rumpuser_mutex_exit(struct rumpuser_mtx *mtx)
{

	assert(mtx->v > 0);
	if (--mtx->v == 0) {
		mtx->o = NULL;
		wakeup_one(&mtx->waiters);
	}
}

void
rumpuser_mutex_destroy(struct rumpuser_mtx *mtx)
{

	assert(TAILQ_EMPTY(&mtx->waiters) && mtx->o == NULL);
	free(mtx);
}

void
rumpuser_mutex_owner(struct rumpuser_mtx *mtx, struct lwp **lp)
{

	*lp = mtx->o;
}

struct rumpuser_rw {
	struct waithead rwait;
	struct waithead wwait;
	int v;
	struct lwp *o;
};

void
rumpuser_rw_init(struct rumpuser_rw **rwp)
{
	struct rumpuser_rw *rw;

	rw = calloc(1, sizeof(*rw));
	if (!rw) abort();
	TAILQ_INIT(&rw->rwait);
	TAILQ_INIT(&rw->wwait);

	*rwp = rw;
}

void
rumpuser_rw_enter(int enum_rumprwlock, struct rumpuser_rw *rw)
{
	enum rumprwlock lk = enum_rumprwlock;
	struct waithead *w = NULL;
	int nlocks;

	switch (lk) {
	case RUMPUSER_RW_WRITER:
		w = &rw->wwait;
		break;
	case RUMPUSER_RW_READER:
		w = &rw->rwait;
		break;
	}

	if (rumpuser_rw_tryenter(enum_rumprwlock, rw) != 0) {
		rumpkern_unsched(&nlocks, NULL);
		while (rumpuser_rw_tryenter(enum_rumprwlock, rw) != 0)
			wait(w, 0);
		rumpkern_sched(nlocks, NULL);
	}
}

int
rumpuser_rw_tryenter(int enum_rumprwlock, struct rumpuser_rw *rw)
{
	enum rumprwlock lk = enum_rumprwlock;
	int rv;

	switch (lk) {
	case RUMPUSER_RW_WRITER:
		if (rw->o == NULL) {
			rw->o = rumpuser_curlwp();
			rv = 0;
		} else {
			rv = EBUSY;
		}
		break;
	case RUMPUSER_RW_READER:
		if (rw->o == NULL && TAILQ_EMPTY(&rw->wwait)) {
			rw->v++;
			rv = 0;
		} else {
			rv = EBUSY;
		}
		break;
	default:
		rv = EINVAL;
	}

	return rv;
}

void
rumpuser_rw_exit(struct rumpuser_rw *rw)
{

	if (rw->o) {
		rw->o = NULL;
	} else {
		rw->v--;
	}

	/* standard procedure, don't let readers starve out writers */
	if (!TAILQ_EMPTY(&rw->wwait)) {
		if (rw->o == NULL)
			wakeup_one(&rw->wwait);
	} else if (!TAILQ_EMPTY(&rw->rwait) && rw->o == NULL) {
		wakeup_all(&rw->rwait);
	}
}

void
rumpuser_rw_destroy(struct rumpuser_rw *rw)
{

	free(rw);
}

void
rumpuser_rw_held(int enum_rumprwlock, struct rumpuser_rw *rw, int *rvp)
{
	enum rumprwlock lk = enum_rumprwlock;

	switch (lk) {
	case RUMPUSER_RW_WRITER:
		*rvp = rw->o == rumpuser_curlwp();
		break;
	case RUMPUSER_RW_READER:
		*rvp = rw->v > 0;
		break;
	}
}

void
rumpuser_rw_downgrade(struct rumpuser_rw *rw)
{

	assert(rw->o == rumpuser_curlwp());
	rw->v = -1;
}

int
rumpuser_rw_tryupgrade(struct rumpuser_rw *rw)
{

	if (rw->v == -1) {
		rw->v = 1;
		rw->o = rumpuser_curlwp();
		return 0;
	}

	return EBUSY;
}

struct rumpuser_cv {
	struct waithead waiters;
	int nwaiters;
};

void
rumpuser_cv_init(struct rumpuser_cv **cvp)
{
	struct rumpuser_cv *cv;

	cv = calloc(1, sizeof(*cv));
	if (!cv) abort();
	TAILQ_INIT(&cv->waiters);
	*cvp = cv;
}

void
rumpuser_cv_destroy(struct rumpuser_cv *cv)
{

	assert(cv->nwaiters == 0);
	free(cv);
}

static void
cv_unsched(struct rumpuser_mtx *mtx, int *nlocks)
{

	rumpkern_unsched(nlocks, mtx);
	rumpuser_mutex_exit(mtx);
}

static void
cv_resched(struct rumpuser_mtx *mtx, int nlocks)
{

	/* see rumpuser(3) */
	if ((mtx->flags & (RUMPUSER_MTX_KMUTEX | RUMPUSER_MTX_SPIN)) ==
	    (RUMPUSER_MTX_KMUTEX | RUMPUSER_MTX_SPIN)) {
		rumpkern_sched(nlocks, mtx);
		rumpuser_mutex_enter_nowrap(mtx);
	} else {
		rumpuser_mutex_enter_nowrap(mtx);
		rumpkern_sched(nlocks, mtx);
	}
}

void
rumpuser_cv_wait(struct rumpuser_cv *cv, struct rumpuser_mtx *mtx)
{
	int nlocks;

	cv->nwaiters++;
	cv_unsched(mtx, &nlocks);
	wait(&cv->waiters, 0);
	cv_resched(mtx, nlocks);
	cv->nwaiters--;
}

void
rumpuser_cv_wait_nowrap(struct rumpuser_cv *cv, struct rumpuser_mtx *mtx)
{

	cv->nwaiters++;
	rumpuser_mutex_exit(mtx);
	wait(&cv->waiters, 0);
	rumpuser_mutex_enter_nowrap(mtx);
	cv->nwaiters--;
}

int
rumpuser_cv_timedwait(struct rumpuser_cv *cv, struct rumpuser_mtx *mtx,
	int64_t sec, int64_t nsec)
{
	int nlocks;
	int rv;

	cv->nwaiters++;
	cv_unsched(mtx, &nlocks);
	rv = wait(&cv->waiters, sec * 1000 + nsec / (1000*1000));
	cv_resched(mtx, nlocks);
	cv->nwaiters--;

	return rv;
}

void
rumpuser_cv_signal(struct rumpuser_cv *cv)
{

	wakeup_one(&cv->waiters);
}

void
rumpuser_cv_broadcast(struct rumpuser_cv *cv)
{

	wakeup_all(&cv->waiters);
}

void
rumpuser_cv_has_waiters(struct rumpuser_cv *cv, int *rvp)
{

	*rvp = cv->nwaiters != 0;
}

/*
 * curlwp
 */

void
rumpuser_curlwpop(int enum_rumplwpop, struct lwp *l)
{
	struct thread *thread;
	enum rumplwpop op = enum_rumplwpop;

	switch (op) {
	case RUMPUSER_LWP_CREATE:
	case RUMPUSER_LWP_DESTROY:
		break;
	case RUMPUSER_LWP_SET:
		thread = get_current();
		thread->lwp = l;
		break;
	case RUMPUSER_LWP_CLEAR:
		thread = get_current();
		assert(thread->lwp == l);
		thread->lwp = NULL;
		break;
	}
}

struct lwp *
rumpuser_curlwp(void)
{

	return get_current()->lwp;
}
