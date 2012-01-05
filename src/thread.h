
#ifndef THREAD_H
#define THREAD_H

#include <pthread.h>
#include <time.h>

static inline int fetch_and_add( int * variable, int value ){
	asm volatile( 
			"lock; xaddl %%eax, %2;"
			:"=a" (value)                   //Output
			: "a" (value), "m" (*variable)  //Input
			:"memory" );
	return value;
}

class Lock
{
    pthread_mutex_t m;
public:
    Lock() {
        pthread_mutex_init(&m, NULL);
    }

    ~Lock() {
        pthread_mutex_destroy(&m);
    }

    void acquire() {
        pthread_mutex_lock(&m);
    }

    void release() {
        pthread_mutex_unlock(&m);
    }
};

#endif