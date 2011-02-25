#ifndef __RIPOSTE_COMMON
#define __RIPOSTE_COMMON

#include <stdint.h>
#include <string>
#include <sstream>
#include <stdexcept>
#include <sys/time.h>

#define TIMING

static inline std::string intToStr( int64_t n )
{
    std::ostringstream result;
    result << n;
    return result.str();
}

static inline std::string intToHexStr( int64_t n )
{
    std::ostringstream result;
    result << std::hex << n;
    return result.str();
}

static inline std::string doubleToStr( double n )
{
    std::ostringstream result;
    result << n;
    return result.str();
}

static inline double time_diff (
    timespec const& end, timespec const& begin)
{
#ifdef TIMING
    double result;

    result = end.tv_sec - begin.tv_sec;
    result += (end.tv_nsec - begin.tv_nsec) / (double)1000000000;

    return result;
#else
    return 0;
#endif
}

static inline void get_time (timespec& ts)
{
#ifdef TIMING
    volatile long noskip;
    #if _POSIX_TIMERS > 0
        clock_gettime(CLOCK_REALTIME, &ts);
    #else
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ts.tv_sec = tv.tv_sec;
        ts.tv_nsec = tv.tv_usec*1000;
    #endif
    noskip = ts.tv_nsec;
#endif
}

static inline timespec get_time()
{
#ifdef TIMING
    timespec t;
    get_time(t);
    return t;
#endif
}

static inline double time_elapsed(timespec const& begin)
{
#ifdef TIMING
    timespec now;
    get_time(now);
    return time_diff(now, begin);
#else
    return 0;
#endif
}

static inline void print_time (char const* prompt, timespec const& begin, timespec const& end)
{
#ifdef TIMING
    printf("%s : %.3f\n", prompt, time_diff(end, begin));
#endif
}

static inline void print_time_elapsed (char const* prompt, timespec const& begin)
{
#ifdef TIMING
    printf("%s : %.3f\n", prompt, time_elapsed(begin));
#endif
}


#endif