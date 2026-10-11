#include "osm.h"
#include <sys/time.h>

#define UNROLL_BUF 10
#ifndef NULL
#define NULL 0
#endif

/*
 * returns difference after - before, in nanoseconds
*/
double timeDiff (timeval before, timeval after){
    if (after.tv_sec == before.tv_sec){
        return (after.tv_usec - before.tv_usec) * 1000;
    }
    else{
        return (((after.tv_sec - before.tv_sec) * 1000000) + after.tv_usec - before.tv_usec) * 1000;
    }
}

/*
 * empty function for function access time assessment
 */
void emptyFunction(){}


/* Time measurement function for a simple arithmetic operation.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_operation_time(unsigned int iterations){
    if (iterations == 0){
        return -1;
    }
    else {
        int remainder = UNROLL_BUF - iterations % UNROLL_BUF;
        iterations += remainder;
        struct timeval timeBefore;
        struct timeval timeAfter;
        unsigned int t0 = 0, t1 = 0, t2 =0 , t3 = 0, t4 = 0, t5 = 0, t6 = 0, t7 = 0, t8 = 0, t9 = 0, i = 0;
        gettimeofday(&timeBefore, NULL);
        for (; i < iterations; i += UNROLL_BUF) {
            t0 += 1;
            t1 += 1;
            t2 += 1;
            t3 += 1;
            t4 += 1;
            t5 += 1;
            t6 += 1;
            t7 += 1;
            t8 += 1;
            t9 += 1;
        }
        gettimeofday(&timeAfter, NULL);

        /* measuring the runtime of the loop without its content,
        * subtracting that time to get more accurate time.
        */
        struct timeval timeBefore2;
        struct timeval timeAfter2;
        i = 0;
        gettimeofday(&timeBefore2, NULL);
        for (; i < iterations; i += UNROLL_BUF) {}
        gettimeofday(&timeAfter2, NULL);
        return (timeDiff(timeBefore, timeAfter) - timeDiff(timeBefore2, timeAfter2)) / iterations;
    }
}


/* Time measurement function for an empty function call.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_function_time(unsigned int iterations){
    if (iterations == 0){
        return -1;
    }
    else {
        int remainder = UNROLL_BUF - iterations % UNROLL_BUF;
        iterations += remainder;
        struct timeval timeBefore;
        struct timeval timeAfter;
        unsigned int i = 0;
        gettimeofday(&timeBefore, NULL);
        for (; i < iterations; i += UNROLL_BUF) {
            emptyFunction();
            emptyFunction();
            emptyFunction();
            emptyFunction();
            emptyFunction();
            emptyFunction();
            emptyFunction();
            emptyFunction();
            emptyFunction();
            emptyFunction();
        }
        gettimeofday(&timeAfter, NULL);

        /* measuring the runtime of the loop without its content,
         * subtracting that time to get more accurate time.
        */
        struct timeval timeBefore2;
        struct timeval timeAfter2;
        i = 0;
        gettimeofday(&timeBefore2, NULL);
        for (; i < iterations; i += UNROLL_BUF) {}
        gettimeofday(&timeAfter2, NULL);
        return (timeDiff(timeBefore, timeAfter) - timeDiff(timeBefore2, timeAfter2)) / iterations;
    }
}


/* Time measurement function for an empty trap into the operating system.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_syscall_time(unsigned int iterations){
    if (iterations == 0){
        return -1;
    }
    else {
        int remainder = UNROLL_BUF - iterations % UNROLL_BUF;
        iterations += remainder;
        struct timeval timeBefore;
        struct timeval timeAfter;
        unsigned int i = 0;
        gettimeofday(&timeBefore, NULL);
        for (; i < iterations; i += UNROLL_BUF) {
            OSM_NULLSYSCALL;
            OSM_NULLSYSCALL;
            OSM_NULLSYSCALL;
            OSM_NULLSYSCALL;
            OSM_NULLSYSCALL;
            OSM_NULLSYSCALL;
            OSM_NULLSYSCALL;
            OSM_NULLSYSCALL;
            OSM_NULLSYSCALL;
            OSM_NULLSYSCALL;
        }
        gettimeofday(&timeAfter, NULL);

        /* measuring the runtime of the loop without its content,
         * subtracting that time to get more accurate time.
        */
        struct timeval timeBefore2;
        struct timeval timeAfter2;
        i = 0;
        gettimeofday(&timeBefore2, NULL);
        for (; i < iterations; i += UNROLL_BUF) {}
        gettimeofday(&timeAfter2, NULL);
        return (timeDiff(timeBefore, timeAfter) - timeDiff(timeBefore2, timeAfter2)) / iterations;
    }
}
