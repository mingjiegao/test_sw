#include "postgres.h"

#include "access/slru.h"
#include "access/transam.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/shmem.h"
#include "utils/builtins.h"

#include "utils/hsearch.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"
#include "funcapi.h"
#include "stdlib.h"
#include "string.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <semaphore.h>
#include <ucontext.h>


#include <emmintrin.h>
#include <nmmintrin.h>




PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(test_sw_01);
PG_FUNCTION_INFO_V1(test_sw_02);
PG_FUNCTION_INFO_V1(test_sw_03);



typedef struct
{
	int				m_cnt;
	pthread_mutex_t	m_mutex;
	pthread_cond_t	m_cond;
	bool			m_timeout;
} ThreadSema;

#define LOOPS 1000000

pg_atomic_uint64 	cnt_atomic;
int64 				cnt_regular;


static void
tsem_init(ThreadSema *sema, int32 init, bool timeout)
{
	sema->m_cnt = init;
	sema->m_timeout = timeout;
	pthread_mutex_init(&sema->m_mutex, 0);
	pthread_cond_init(&sema->m_cond, 0);
}

static int
tsem_wait(ThreadSema *sema)
{
	int rc = 0;
	struct timespec ts;

	if (sema->m_timeout)
	{
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 1;
	}

	(void) pthread_mutex_lock(&sema->m_mutex);

	if (--(sema->m_cnt) < 0) 
	{
		if (sema->m_timeout)
		{
			rc = pthread_cond_timedwait(&sema->m_cond, &sema->m_mutex, &ts);
			if (rc != 0)
				sema->m_cnt++;
		}
		else
			rc = pthread_cond_wait(&sema->m_cond, &sema->m_mutex);
	}

	(void) pthread_mutex_unlock(&sema->m_mutex);

	return rc;
}

static void
tsem_post(ThreadSema *sema)
{
	(void) pthread_mutex_lock(&sema->m_mutex);
	if ((sema->m_cnt)++ < 0) 
		(void) pthread_cond_signal(&sema->m_cond);
	(void) pthread_mutex_unlock(&sema->m_mutex);
}

/*************************************/
/** Thread semaphore implementation **/
/*************************************/

ThreadSema 			tsema;
ThreadSema 			tsemb;

static void *
thread_a_01(void* arg)
{
	for (int i = 0; i < LOOPS; i++)
	{
		tsem_wait(&tsema);
		cnt_regular++;
		// pg_atomic_write_u64(&cnt_atomic, pg_atomic_read_u64(&cnt_atomic) + 1);
		tsem_post(&tsemb);
	}
	return NULL;
}

static void *
thread_b_01(void* arg)
{
	for (int i = 0; i < LOOPS; i++)
	{
		tsem_wait(&tsemb);
		cnt_regular++;
		// pg_atomic_write_u64(&cnt_atomic, pg_atomic_read_u64(&cnt_atomic) + 1);
		tsem_post(&tsema);
	}
	return NULL;
}


Datum
test_sw_01(PG_FUNCTION_ARGS)
{
	pthread_t tA, tB;

	tsem_init(&tsema, 0, true);
	tsem_init(&tsemb, 0, true);

	pg_atomic_init_u64(&cnt_atomic, 0);
	cnt_regular = 0;

	tsem_post(&tsema);

	pthread_create(&tA, NULL, thread_a_01, NULL);
	pthread_create(&tB, NULL, thread_b_01, NULL);
	pthread_join(tA, NULL);
	pthread_join(tB, NULL);

	elog(WARNING, "cnt_regular: %ld, cnt_atomic: %ld", cnt_regular, pg_atomic_read_u64(&cnt_atomic));

	PG_RETURN_VOID();
}



/*************************************/
/** Native semaphore implementation */
/*************************************/
typedef struct
{
	sem_t			sem;
	bool			m_timeout;
} NativeSema;

NativeSema 		sema, semb;


static void
nsem_init(NativeSema *sema, int32 init, bool timeout)
{
	sem_init(&sema->sem, 0, init);
	sema->m_timeout = timeout;
}

static int
nsem_wait(NativeSema *sema)
{
	int rc = 0;
	struct timespec ts;

	if (sema->m_timeout)
	{
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 1;

		rc = sem_timedwait(&sema->sem, &ts);
	}
	else
	{
		rc = sem_wait(&sema->sem);
	}

	return rc;
}

static void
nsem_post(NativeSema *sema)
{
	sem_post(&sema->sem);
}

static void *
thread_a_02(void* arg)
{
	for (int i = 0; i < LOOPS; i++)
	{
		nsem_wait(&sema);
		cnt_regular++;
		nsem_post(&semb);
	}
	return NULL;
}

static void *
thread_b_02(void* arg)
{
	for (int i = 0; i < LOOPS; i++)
	{
		nsem_wait(&semb);
		cnt_regular++;
		nsem_post(&sema);
	}
	return NULL;
}


Datum
test_sw_02(PG_FUNCTION_ARGS)
{
	pthread_t tA, tB;

	nsem_init(&sema, 0, true);
	nsem_init(&semb, 0, true);

	pg_atomic_init_u64(&cnt_atomic, 0);
	cnt_regular = 0;

	nsem_post(&sema);

	pthread_create(&tA, NULL, thread_a_02, NULL);
	pthread_create(&tB, NULL, thread_b_02, NULL);
	pthread_join(tA, NULL);
	pthread_join(tB, NULL);

	elog(WARNING, "cnt_regular: %ld, cnt_atomic: %ld", cnt_regular, pg_atomic_read_u64(&cnt_atomic));

	PG_RETURN_VOID();
}




/*************************************/
/** swapcontext */
/*************************************/
#define STACK_SIZE (64 * 1024)
char stack_a[STACK_SIZE];
char stack_b[STACK_SIZE];
ucontext_t ctx_main, ctx_a, ctx_b;

static void
func_a(void)
{
	for (int i = 0; i < LOOPS; i++)
	{
		cnt_regular++;
		swapcontext(&ctx_a, &ctx_b);
	}
}

static void
func_b(void)
{
	for (int i = 0; i < LOOPS; i++)
	{
		cnt_regular++;
		swapcontext(&ctx_b, &ctx_a);
	}
}




Datum
test_sw_03(PG_FUNCTION_ARGS)
{
	pg_atomic_init_u64(&cnt_atomic, 0);
	cnt_regular = 0;

	getcontext(&ctx_a);
	ctx_a.uc_stack.ss_sp = stack_a;
	ctx_a.uc_stack.ss_size = sizeof(stack_a);
	ctx_a.uc_link = &ctx_main;
	makecontext(&ctx_a, func_a, 0);

	getcontext(&ctx_b);
	ctx_b.uc_stack.ss_sp = stack_b;
	ctx_b.uc_stack.ss_size = sizeof(stack_b);
	ctx_b.uc_link = &ctx_main;
	makecontext(&ctx_b, func_b, 0);

	swapcontext(&ctx_main, &ctx_a);

	elog(WARNING, "cnt_regular: %ld, cnt_atomic: %ld", cnt_regular, pg_atomic_read_u64(&cnt_atomic));

	PG_RETURN_VOID();
}
