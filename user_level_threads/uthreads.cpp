#include "uthreads.h"
#include <cstdio>
#include <list>
#include <signal.h>
#include <sys/time.h>
#include <setjmp.h>
#include <unistd.h>
#include <cstdlib>
#include <algorithm>
#include <map>


#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}


#endif

#define SUCCESS 0
#define ERROR (-1)


int seconds, microseconds;
char* threads[MAX_THREAD_NUM] = {nullptr};  // this will later contain pointers to threads stack
sigjmp_buf env[MAX_THREAD_NUM];
std::list<int> ready_que;
bool blocked_threads[MAX_THREAD_NUM] = {false};
int quantum_counter = 0;
int thread_quantum_counter[MAX_THREAD_NUM] = {0};
std::map<int, int> sleeper;

struct sigaction sa = {0};
struct itimerval timer;
sigset_t set, pending;

typedef unsigned long address_t;

/**
 * with every SIGVALRM, we go through sleeping threads, check if a thread has finished
 * sleeping and if so, return it to end of ready_que. if thread it blocked we do nothing
 * if not, we decrement remaining sleeping quantums.
 * */
void update_sleeper(){
    auto it = sleeper.begin();
    while (it != sleeper.end()){
        if (!blocked_threads[it->first]){
            if (it->second == 0){
                ready_que.push_back(it->first);
                sleeper.erase(it++);
                continue;
            }
            else{
                it->second --;
            }
        }
        ++it;
    }
}

/**
 * resets timer to the given quantum during init, and start the timer
 * */
void start_timer(){
    timer.it_value.tv_sec = seconds;
    timer.it_value.tv_usec = microseconds;
    // start timer
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)){
        fprintf(stderr, "system error: setitimer error\n");
    }
}

/**
 * disarms the time, stops it from running
 */
void disarm_timer(){
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    // start timer
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)){
        fprintf(stderr, "system error: setitimer error\n");
    }
}

/**
 * handler function for SIGVTALRM signal
 * this method handles end of quantum for the running thread:
 * increases quantum counter, updates sleeping threads status, if the threads go back to ready (non negative
 * tid) , pushes it back tp end of ready queue
 * if more than one thread is present in ready, will save env, jump to the next thread, and initiate a new single shot
 * timer.
 * */
void timer_handler(int sig) {
    quantum_counter += 1;
    update_sleeper();
    int current = ready_que.front();
    ready_que.pop_front();
    if (current >= 0) { // if thread has not been blocked or terminated, add to end of ready queue
        ready_que.push_back(current);
    }
    current = abs(current);
    int next = ready_que.front();
    thread_quantum_counter[next] += 1;
    if (current != next) {  // only jump threads when there is more than the one main thread
        if (sigsetjmp(env[current], 1) == 0) {
            start_timer();
            siglongjmp(env[next], 1);
        }
    }
    else {
        start_timer();  //  restart timer
    }
}


/**
 * Initializes itimer to quantum length,
 * and sets timer_handler as handler for SIGVTALRM signal
 *
 * called once during initialization
 * */
void config_timer(){
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
        fprintf(stderr, "system error: sigaction error\n");
        exit(1);
    }

    // timer value will be configured in start timer

    // this timer is single shot
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
}


/**
 * search thread array for available location,
 * if fails returns -1
 * */
int generate_thread_id(){
    for (int i = 1; i < MAX_THREAD_NUM; i++){
        if (threads[i] == nullptr){
            return i;
        }
    }
    return -1;
}

/**
 * block SIGVALRM signals
 * */
void mask_SIGVTALRM(){
    if (sigprocmask(SIG_BLOCK, &set, NULL) < 0){
        fprintf(stderr, "thread library error: error during masking SIGVTALRM\n");
    }
}

/**
 * unblock SIGVALRM signals
 * */
void unmask_SIGVTALRM(){
    if (sigprocmask(SIG_UNBLOCK, &set, NULL) < 0){
        fprintf(stderr, "thread library error: error during unmasking SIGVTALRM\n");
    }
}

/**
 * check whether a SIGVALRM is currently in pending queue
 * */
int is_SIGVTALRM_pending(){
    if (sigpending(&pending) < 0){
        fprintf(stderr, "thread library error: error during getting pending signals\n");
    }
    int res = sigismember(&pending, SIGVTALRM);
    if (res == -1){
        fprintf(stderr, "thread library error: error during checking pending signals\n");
    }
    return res;
}



/**
 *
 * inits quantum, pushes main thread onto ready queue
 * configures timer signals and quantum length
 * starts timer and with it - main thread initial running quantum
 * inits quantum counters
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs){
    if (quantum_usecs <= 0){
        fprintf(stderr, "thread library error: non-positive quantum\n");
        return ERROR;
    }
    // create set with SIGVALRM
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);

    seconds = quantum_usecs / 1000000 , microseconds = quantum_usecs - seconds * 1000000;

    // push main thread to queue
    ready_que.push_back(0);

    // configure timer
    config_timer();

    // counter inits:
    thread_quantum_counter[0] = 1;
    quantum_counter = 1;

    // start timer
    start_timer();

    return SUCCESS;
}

/**
 * masks SIGVALRM
 * first - check that entry point is valid
 * check if there is an availabe thread id
 * allocate memory to new thread stack
 * set env array to contain new array's env
 * add to ready que
*/
int uthread_spawn(thread_entry_point entry_point){
    mask_SIGVTALRM();
    // check that entry point is valid
    if (entry_point == nullptr) {
        fprintf(stderr, "thread library error: no entry point given during thread initialization\n");
        unmask_SIGVTALRM();
        return ERROR;
    }

    // get thread id if available, mark as used
    int tid = generate_thread_id();
    if (tid == -1){  // thread overflow
        fprintf(stderr, "thread library error: thread overflow\n");
        unmask_SIGVTALRM();
        return ERROR;
    }

    // allocate new thread's stack
    threads[tid] = (char *) (malloc(STACK_SIZE));
    if (threads[tid] == nullptr){
        fprintf(stderr, "thread library error: new thread Memory allocation failed\n");
        unmask_SIGVTALRM();
        return ERROR;
    }

    // load sp & pc onto thread env
    auto pc = (address_t) entry_point;
    auto sp = (address_t) threads[tid] + STACK_SIZE - sizeof(address_t);

    // load thread to ready queue
    ready_que.push_back(tid);

    // verify thread quantum counter is 0
    thread_quantum_counter[tid] = 0;

    sigsetjmp(env[tid], 1);  // initial thread jump location
    (env[tid]->__jmpbuf)[JB_SP] = translate_address(sp);
    (env[tid]->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&env[tid]->__saved_mask);

    unmask_SIGVTALRM();
    return tid;
}


/**
 * masks SIGVALRM
 * first, if terminationg main thread, all allocation will be freed, and program will exit on 0
 * else, if illegal id was given, an error will be raised
 * then, removing allocation of appropriate if, and removing from ready queue (block queue?)
 * if a non zero thread terminates itself, a dummy -1 value will be pushed to the ready queue
 * and a SIGVALRM will be sent such that the handler will jump to next running thread.
*/
int uthread_terminate(int tid){
    mask_SIGVTALRM();
    if (tid < 0){
        fprintf(stderr, "thread library error: cannot remove non existing thread with negative ID %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }
    if (tid >= MAX_THREAD_NUM){
        fprintf(stderr, "thread library error: cannot terminate non existing thread %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }
    if (tid == 0){
        for (int i = 1; i< MAX_THREAD_NUM; i++){
            if (threads[i] != nullptr){
                free(threads[i]);
                threads[i] = nullptr;
            }
        }
        unmask_SIGVTALRM();
        exit(0);
    }
    if (threads[tid] == nullptr){
        fprintf(stderr, "thread library error: cannot remove non existing thread %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }


    free(threads[tid]);
    threads[tid] = nullptr;
    sleeper.erase(tid);
    if (ready_que.front() == tid){
        // need to move onto next thread try ->
        // reset timer and signal SIGVTALRM
        ready_que.pop_front();
        ready_que.push_front(-tid);
        if (!is_SIGVTALRM_pending()) { // if there isn't a pending SIGVALRM
            disarm_timer();
            kill(getpid(), SIGVTALRM);
        }
    }
    else {
        ready_que.remove(tid);
    }
    unmask_SIGVTALRM();
    return SUCCESS;

}


/**
 * masks SIGVALRM
 * checks if tid is valid, and existing, if no will raise error and return -1, else,
 * if the thread is running will erase it from running queue, add to blocked threads set,
 * push dummy value to ready and send SIGVALRM signal for running swith
 * if thread is not currently running it will just remove from ready queue and add to blocked set
 * blocking twice shouldn't make a difference
 *
*/
int uthread_block(int tid){
    mask_SIGVTALRM();
    if (tid <= 0){
        if (tid == 0) {
            fprintf(stderr, "thread library error: cannot block main thread\n");
        }
        else{
            fprintf(stderr, "thread library error: cannot block non existing thread %d\n", tid);
        }
        unmask_SIGVTALRM();
        return ERROR;
    }
    if (tid >= MAX_THREAD_NUM){
        fprintf(stderr, "thread library error: cannot block non existing thread %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }
    if (threads[tid] == nullptr){
        fprintf(stderr, "thread library error: cannot block non existing thread %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }

    // block is  running:
    if (ready_que.front() == tid) {
        ready_que.pop_front();
        blocked_threads[tid] = true;
        ready_que.push_front(-tid);
        if (!is_SIGVTALRM_pending()) {
            disarm_timer();
            kill(getpid(), SIGVTALRM);
        }
    }
    else {
        if (sleeper.find(tid) == sleeper.end()) {
            ready_que.remove(tid);
        }
        blocked_threads[tid] = true;
    }
    unmask_SIGVTALRM();
    return SUCCESS;

}


/**
 * masks SIGVALRM
 * checks the usual tid validity
 * if valid will remove from blocked set (if present)
 * then function will search for thread in running queue, and if it is absent, will push it last
*/
int uthread_resume(int tid){
    mask_SIGVTALRM();
    if (tid < 0){
        fprintf(stderr, "thread library error: cannot resume non - existing thread %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }
    if (tid >= MAX_THREAD_NUM){
        fprintf(stderr, "thread library error: cannot resume non existing thread %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }
    if (tid > 0 and threads[tid] == nullptr){
        fprintf(stderr, "thread library error: cannot resume non - existing thread %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }
    blocked_threads[tid] = false;

    // if tid is not in ready que, and it is not sleeping, add it.
    if (sleeper.find(tid) == sleeper.end()) {
        std::list<int>::iterator it;
        it = std::find(ready_que.begin(), ready_que.end(), tid);
        if (it == ready_que.end()) {
            ready_que.push_back(tid);
        }
    }
    unmask_SIGVTALRM();
    return SUCCESS;
}


/**
 * masks SIGVALRM
 * puts the running thread sleeping map, with num of quantum as value, the stop timer and
 * sends SIGVMALRM signal to go to handler
*/
int uthread_sleep(int num_quantums){
    mask_SIGVTALRM();
    if (ready_que.front() == 0){
        fprintf(stderr, "thread library error: main thread cannot sleep\n");
        unmask_SIGVTALRM();
        return ERROR;
    }
    if (num_quantums <= 0){
        fprintf(stderr, "thread library error: cannot sleep non positive quantums\n");
        unmask_SIGVTALRM();
        return ERROR;
    }

    int tid = ready_que.front();
    sleeper[tid] = num_quantums;
    ready_que.pop_front();
    ready_que.push_front(-tid);
    if (!is_SIGVTALRM_pending()) {
        disarm_timer();
        kill(getpid(), SIGVTALRM);
    }
    unmask_SIGVTALRM();
    return SUCCESS;
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid(){
    return ready_que.front();
}


/**
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums(){
    return quantum_counter;
}


/**
 * masks SIGVALRM
 * checks tid validity
 * returns executed quantum of tid
*/
int uthread_get_quantums(int tid){
    mask_SIGVTALRM();
    if (tid < 0){
        fprintf(stderr, "thread library error: cannot count non valid thread %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }
    if (tid >= MAX_THREAD_NUM){
        fprintf(stderr, "thread library error: cannot fetch non existing thread %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }
    if (threads[tid] == nullptr and tid != 0){
        fprintf(stderr, "thread library error: cannot count non valid thread %d\n", tid);
        unmask_SIGVTALRM();
        return ERROR;
    }
    unmask_SIGVTALRM();
    return thread_quantum_counter[tid];
}
