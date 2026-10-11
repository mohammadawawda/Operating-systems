#include "MapReduceFramework.h"
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include "Barrier.h"
#include <queue>
#include <algorithm>
#include <atomic>


/**
 * this struct contains intermediate pair and the id of the thread whose intermediate vec
 * the pair came from
 * for use in shuffle phase, merge k sorted arrays
 * */
struct ThreadPair{
    IntermediatePair pair;
    int tid;
    bool operator()(const ThreadPair& a, const ThreadPair& b) const {
        return *a.pair.first < *b.pair.first;
    }
};

/**
 * contains data needed for thread activity and synchronization
 * everything here other than tid and intermediateVec belongs to the job and is shared by its threads
 * */
struct ThreadContext {
    int tid;
    int multiThreadLevel;
    OutputVec * outputVec;
    Barrier* barrier;
    std::atomic<uint64_t>* atomic_counter;
    std::atomic<uint32_t>* nextIndex;
    std::atomic<uint32_t>* intermediateCount;
    pthread_mutex_t * mutex;
    std::vector<IntermediateVec> * shuffled;
    IntermediateVec* intermediateVec;
    const MapReduceClient* client;
    const InputVec * inputVec;
};

/**
 * contains everything that belongs to one job, so a few jobs can run at the same time:
 * thread and thread context arrays, shuffled vector of intermediate vectors (to use during shuffle and reduce
 * stages), the atomic counters, the mutexes and flag for wait for job callers
 *
 * atomic counter keeps the whole job state in one 64 bit number:
 * leftmost 2 bits are the stage, middle 31 bits are how many elements were processed in the stage
 * and rightmost 31 bits are how many elements there are in the stage
 * so one read gives a stage and a percentage that belong together
 *
 * next index gives the threads the next element to work on in map and reduce stages
 * intermediate count is the number of pairs emitted by emit2
 *
 * this struct will be casted to jobHandle
 * */
struct JobContext {
    pthread_t * threads;
    ThreadContext * contexts;
    Barrier* barrier;
    std::vector<IntermediateVec>* shuffled;
    std::atomic<uint64_t> atomic_counter;
    std::atomic<uint32_t> nextIndex;
    std::atomic<uint32_t> intermediateCount;
    pthread_mutex_t threadMutex;
    pthread_mutex_t flagMutex;
    int multiThreadLevel;
    bool waitFlag;
};


/**
 * builds the value of atomic counter out of stage, processed and total
 * */
uint64_t counterValue(stage_t stage, uint64_t processed, uint64_t total){
    return ((uint64_t) stage << 62) + (processed << 31) + total;
}

/**
 * locks mutex, exits if it fails
 * */
void lockMutex(pthread_mutex_t * mutex){
    if (pthread_mutex_lock(mutex) != 0){
        fprintf(stdout, "system error: failed to lock mutex\n");
        exit(1);
    }
}

/**
 * unlocks mutex, exits if it fails
 * */
void unlockMutex(pthread_mutex_t * mutex){
    if (pthread_mutex_unlock(mutex) != 0){
        fprintf(stdout, "system error: failed to unlock mutex\n");
        exit(1);
    }
}

/**
 * allocates job memory, inits counters and mutexes..
 * */
JobContext * setJobContext(int multiThreadLevel){
    try {

        auto jobContext = new JobContext;
        jobContext->waitFlag = false;
        jobContext->multiThreadLevel = multiThreadLevel;
        jobContext->threads = new pthread_t[multiThreadLevel];
        jobContext->contexts = new ThreadContext[multiThreadLevel];
        for (int i = 0; i < multiThreadLevel; i++){
            (jobContext->contexts + i)->intermediateVec = new IntermediateVec;
        }
        jobContext->shuffled = new std::vector<IntermediateVec>;
        jobContext->barrier = new Barrier(multiThreadLevel);

        // undefined stage until the first thread starts the map phase
        jobContext->atomic_counter = 0;
        jobContext->nextIndex = 0;
        jobContext->intermediateCount = 0;

        if (pthread_mutex_init(&jobContext->threadMutex, NULL) != 0){
            fprintf(stdout, "system error: cannot initialize threadMutex\n");
            exit(1);
        }

        if (pthread_mutex_init(&jobContext->flagMutex, NULL) != 0){
            fprintf(stdout, "system error: cannot initialize flagMutex\n");
            exit(1);
        }

        return jobContext;
    }
    catch (std::bad_alloc& ba){
        fprintf(stdout, "system error: bad allocation\n");
        exit(1);
    }
}

/**
 * setup thread context, the shared parts point to the data of the job
 * */
void setThreadContext(ThreadContext * tc, int tid, const MapReduceClient& client, OutputVec & outputVec,
                      const InputVec& inputVec, JobContext * jobContext){
    tc->tid = tid;
    tc->client = &client;
    tc->inputVec = &inputVec;
    tc->outputVec = &outputVec;
    tc->multiThreadLevel = jobContext->multiThreadLevel;
    tc->barrier = jobContext->barrier;
    tc->shuffled = jobContext->shuffled;
    tc->atomic_counter = &jobContext->atomic_counter;
    tc->nextIndex = &jobContext->nextIndex;
    tc->intermediateCount = &jobContext->intermediateCount;
    tc->mutex = &jobContext->threadMutex;
}

/**
 * map phase of job, some details:
 * the first thread that gets here moves the job from undefined to map stage, with the input size as total
 * next index gives each thread the next element to operate on, so no element is mapped twice
 * after mapping an element the middle 31 bits of atomic counter go up by one (how many elements have been mapped)
 * in the end each thread sorts its own intermediate vec
 * */
void mapPhase(ThreadContext * tc){
    uint64_t inputSize = tc->inputVec->size();
    uint64_t undefined = 0;
    tc->atomic_counter->compare_exchange_strong(undefined, counterValue(MAP_STAGE, 0, inputSize));

    uint32_t old_value = (*(tc->nextIndex))++;
    while (old_value < inputSize){
        tc->client->map((*(tc->inputVec))[old_value].first, (*(tc->inputVec))[old_value].second, tc);
        (*(tc->atomic_counter)) += (uint64_t) 1 << 31;
        old_value = (*(tc->nextIndex))++;
    }
    // thread sorting intermediate vector
    std::sort(tc->intermediateVec->begin(), tc->intermediateVec->end(), [](IntermediatePair a, IntermediatePair b) {
        return *a.first < *b.first;
    });

}

/**
 * uses merge k sorted arrays algorithm:
 * maintain priority queue with the last pair (the biggest key) of each thread's intermediate vec
 * pop the biggest pair into shuffled vector, either to the last vector within if it has the same key, or else
 * generate a new vector with the pair, and push it into shuffled
 * pairs come out from the biggest key to the smallest, so a smaller key than the last one means a new key
 *
 * the middle 31 bits of atomic counter count the shuffled pairs, out of all the intermediate pairs
 * in the end next index and atomic counter are set for reduce stage
 * */
void shufflePhase(ThreadContext * tc) {
    if (tc->tid == 0) {
        uint64_t total = tc->intermediateCount->load();
        (*tc->atomic_counter) = counterValue(SHUFFLE_STAGE, 0, total); // start shuffle phase

        // merge k sorted arrays implementation
        std::priority_queue<ThreadPair, std::vector<ThreadPair>, ThreadPair> queue;
        for (int i = 0; i < tc->multiThreadLevel; i++) {
            if (!(tc + i)->intermediateVec->empty()) {
                queue.push({((tc + i)->intermediateVec->back()), i});
                (tc + i)->intermediateVec->pop_back();
            }
        }
        while (!queue.empty()) {
            auto top = queue.top();
            queue.pop();
            if (tc->shuffled->empty() ||
                ((*(top.pair.first) < *(tc->shuffled->back().back().first)))) {
                tc->shuffled->push_back(IntermediateVec{top.pair});
            } else {
                tc->shuffled->back().push_back(top.pair);
            }
            (*tc->atomic_counter) += (uint64_t) 1 << 31;
            if (!(tc + top.tid)->intermediateVec->empty()) {
                queue.push({((tc + top.tid)->intermediateVec->back()), top.tid});
                (tc + top.tid)->intermediateVec->pop_back();
            }
        }

        // setup counters for reduce stage
        (*tc->nextIndex) = 0;
        (*tc->atomic_counter) = counterValue(REDUCE_STAGE, 0, total);
    }
}

/**
 * some details:
 * next index gives each thread the next vector of shuffled to reduce, starting from the back like a queue
 * the vector is passed to reduce as is, without copying it
 * middle 31 bits of atomic counter keep track of how many shuffled pairs have been processed (updated when a thread
 * finishes handling a vector from shuffled vector of vectors)
 * */
void reducePhase(ThreadContext* tc){
    uint32_t old_value = (*(tc->nextIndex))++;
    while (old_value < tc->shuffled->size()){
        const IntermediateVec* vec = &(*tc->shuffled)[tc->shuffled->size() - old_value - 1];
        tc->client->reduce(vec, tc);
        (*tc->atomic_counter) += (uint64_t) vec->size() << 31;
        old_value = (*(tc->nextIndex))++;
    }
}

/**
 * threads entry point function
 * arg is thread's context
 * uses the barrier twice to separate stages.
 * */
void*  entryPoint(void* arg) {
    auto tc = static_cast<ThreadContext *>(arg);

    try {
        mapPhase(tc);

        tc->barrier->barrier();

        shufflePhase(tc);

        tc->barrier->barrier();

        reducePhase(tc);
    }
    catch (std::bad_alloc& ba){
        fprintf(stdout, "system error: bad allocation\n");
        exit(1);
    }

    return nullptr;
}

/**
 * allocates required memory
 * and then creates threads
 *
 * returns the allocated memory as jobhandle
 * */
JobHandle startMapReduceJob(const MapReduceClient& client,
                            const InputVec& inputVec, OutputVec& outputVec,
                            int multiThreadLevel) {

    auto jobContext = setJobContext(multiThreadLevel);

    for (int i = 0; i < multiThreadLevel; ++i) {

        setThreadContext(jobContext->contexts + i, i, client, outputVec, inputVec, jobContext);

        if (pthread_create(jobContext->threads + i, NULL, &entryPoint, jobContext->contexts + i) != 0){
            fprintf(stdout, "system error: failed to create thread %d\n", i);
            exit(1);
        }
    }

    return static_cast<JobHandle>(jobContext);
}


/**
 * add the pair created in map stage to intermediate vec of calling thread
 * and count it in intermediate count, which is the total of shuffle and reduce stages
 * */
void emit2 (K2* key, V2* value, void* context){
    auto tc = static_cast<ThreadContext *>(context);
    (*(tc->intermediateVec)).push_back({key, value});
    (*(tc->intermediateCount))++;
}

/**
 * add the pair generated in reduce stage to output vec.
 * since output vec is shared, this is done under thread mutex lock of the job
 * */
void emit3 (K3* key, V3* value, void* context){
    auto tc = static_cast<ThreadContext *>(context);
    lockMutex(tc->mutex);
    (*tc->outputVec).push_back({key, value});
    unlockMutex(tc->mutex);
}

/**
 * gets job state
 * atomic counter is read once, stage according to leftmost 2 bits,
 * and percentage is the middle 31 bits (processed) out of the rightmost 31 bits (total)
 * if there is nothing to process in the stage it's already done, so percentage is 100
 * (unless the job did not start yet, then it's 0)
 * */
void getJobState(JobHandle job, JobState* state){
    if (job != nullptr) {
        auto jobContext = static_cast<JobContext *>(job);
        uint64_t counter = jobContext->atomic_counter.load();
        uint64_t processed = (counter >> 31) & (0x7fffffff);
        uint64_t total = counter & (0x7fffffff);
        state->stage = static_cast<stage_t>(counter >> 62);
        if (total == 0) {
            state->percentage = state->stage == UNDEFINED_STAGE ? 0 : 100;
        }
        else {
            state->percentage = (float) processed / (float) total * 100;
        }
    }
}

/**
 * waits for job threads
 * if wait flag is false pthread join will be called for each thread
 * this is done under flag mutex lock of the job to make sure pthread join is called once per thread
 * if method is called again, it waits for the lock until the threads are joined, and then does nothing
 * */
void waitForJob(JobHandle job){
    if (job != nullptr) {
        auto jobContext = static_cast<JobContext *>(job);
        lockMutex(&jobContext->flagMutex);
        if (!jobContext->waitFlag) {
            jobContext->waitFlag = true;
            for (int i = 0; i < jobContext->multiThreadLevel; i++) {
                if (pthread_join(jobContext->threads[i], NULL) != 0) {
                    fprintf(stdout, "system error: failed to join threads\n");
                    exit(1);
                }
            }
        }
        unlockMutex(&jobContext->flagMutex);
    }
}

/**
 * waits for the job to finish, then releases all memory and resources of the job (also destroys mutexes)
 * */
void closeJobHandle(JobHandle job){
    if (job != nullptr){
        waitForJob(job);
        auto jobContext = static_cast<JobContext *>(job);
        for (int i = 0; i < jobContext->multiThreadLevel; i++){
            delete (jobContext->contexts + i)->intermediateVec;
        }
        delete jobContext->barrier;
        delete [] jobContext->contexts;
        delete [] jobContext->threads;
        delete jobContext->shuffled;

        if (pthread_mutex_destroy(&jobContext->threadMutex) != 0){
            fprintf(stdout, "system error: failed to destroy threadMutex\n");
            exit(1);
        }

        if (pthread_mutex_destroy(&jobContext->flagMutex) != 0){
            fprintf(stdout, "system error: failed to destroy flagMutex\n");
            exit(1);
        }

        delete jobContext;
    }
}
