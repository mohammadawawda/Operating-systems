#include "VirtualMemory.h"
#include "PhysicalMemory.h"

#define SUCCESS 1
#define FAILURE 0
#define EMPTY 0

struct frameCandidates{
    uint64_t page, emptyFrame, maxFrame, maxCyclicPageFrame, parent, maxCyclicDist, maxCyclicPage;
};

/**
 * return minimal cyclic distance between page p1 and page p2
 * */
uint64_t getCyclicDist(uint64_t p1, uint64_t p2){
    if (p2 >= p1){
        uint64_t lhs = p2 - p1;
        uint64_t rhs = p1 + NUM_PAGES - p2;
        if (lhs < rhs) {return lhs;}
        else {return rhs;}
    }
    else{
        return getCyclicDist(p2, p1);
    }
}

/**
 * checks if frame is all 0
 * */
int isFrameEmpty(uint64_t frame){
    word_t value;
    for (int i = 0; i < PAGE_SIZE; i++){
        PMread(frame * PAGE_SIZE + i, &value);
        if (value != 0){
            return 0;
        }
    }
    return 1;
}

/**
 * changes all entries of frame to 0
 * */
void clearFrame(uint64_t frame){
    for (int i = 0; i < PAGE_SIZE; ++i){
        PMwrite(frame * PAGE_SIZE + i, EMPTY);
    }
}

/**
 * This function is called along page table tree traverse, it keep information required for frame selection
 * keeps max frame traversed, an empty frame if found one, and if frame is a leaf, it will track whether its cyclic
 * distance from the page we wish to read/write is maximal.
 * */
void frameFinderFunc(void * frameCandidates, int currDepth, uint64_t currFrame, uint64_t virtualAddress){
    auto fc = static_cast<struct frameCandidates *>(frameCandidates);
    // update max frame
    if (fc->maxFrame < currFrame){
        fc->maxFrame = currFrame;
    }
    // keep if frame is empty
    if (isFrameEmpty(currFrame) and fc->parent != currFrame and currDepth != 0){
        fc->emptyFrame = currFrame;
    }
    // keep max cyclic
    if (currDepth == 0){
        auto dist = getCyclicDist(virtualAddress, fc->page);
        if (dist > fc->maxCyclicDist){
            fc->maxCyclicPageFrame = currFrame;
            fc->maxCyclicDist = dist;
            fc->maxCyclicPage = virtualAddress;
        }
    }

}

/**
 * This function is called along page table tree traverse, remove references to frame which we have evicted
 * */
void rmRefToFrameFunc(void * frameToRemove, int currDepth, uint64_t currFrame, uint64_t virtualAddress){
    auto ftr = static_cast<uint64_t *>(frameToRemove);
    word_t value;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        PMread(currFrame * PAGE_SIZE + i, &value);
        if ((currDepth != 0) & ((uint64_t)value == *ftr)){
            PMwrite(currFrame * PAGE_SIZE + i, EMPTY);
        }
    }
}

/** recursive tree traversing, at every frame this function executes a given function func, namely
 * frameFinderFunc and rmRefToFrameFunc*/
void traversePTTree(int currDepth, uint64_t currFrame, uint64_t virtualAddress, void (*func)(void *, int, uint64_t, uint64_t), void* args){
    func(args, currDepth, currFrame, virtualAddress);
    if (currDepth > 0){
        word_t successor;
        for (int i = 0; i < PAGE_SIZE; ++i){
            PMread(currFrame * PAGE_SIZE + i, &successor);
            if (successor != 0){
                traversePTTree(currDepth - 1, successor, (virtualAddress << OFFSET_WIDTH) + i, func, args);
            }
        }
    }
}

/**
 * This function allocates a frame, whether empty or evicted, according to defined priorities
 * */
uint64_t getAvailableFrame(uint64_t page, uint64_t parent){
    uint64_t frame;
    frameCandidates fc = {page, NUM_FRAMES, 0,0, parent, 0, 0};
    traversePTTree(TABLES_DEPTH, 0, 0,&frameFinderFunc, (void *) &fc);
    if (fc.emptyFrame < NUM_FRAMES){
        frame = fc.emptyFrame;
        traversePTTree(TABLES_DEPTH, 0, 0,&rmRefToFrameFunc, (void *) &frame);
    }
    else{
        if (fc.maxFrame + 1 < NUM_FRAMES){
            frame = fc.maxFrame + 1;
        }
        else{
            frame = fc.maxCyclicPageFrame;
            PMevict(frame, fc.maxCyclicPage);
            clearFrame(frame);
            traversePTTree(TABLES_DEPTH, 0, 0, &rmRefToFrameFunc, (void *) &frame);
        }
    }
    return frame;
}

/**
 * Recursive function that translates a virtual address to a physical address
 * */
uint64_t translate(uint64_t address, int depth, uint64_t page){
    if (depth == 0){ // we have reached top-most page table
        return address;
    }
    auto index = address & (PAGE_SIZE - 1);
    address = address >> OFFSET_WIDTH;
    word_t value;
    auto physicalAddress = translate(address, depth - 1, page);
    PMread(physicalAddress, &value);
    if (value == 0){ // need to find a frame to use
        auto parent = physicalAddress / PAGE_SIZE;
        auto frame = getAvailableFrame(page, parent);
        if (depth == TABLES_DEPTH){
            PMrestore(frame, page);
        }
        else{
            clearFrame(frame);
        }
        PMwrite(physicalAddress, frame);
        value = frame;
    }
    return value * PAGE_SIZE + index;
}

/***
 * writes 0 to first PAGE_SIZE addresses in PM
 */
void VMinitialize(){
    // clear frame 0. seems that physical memory inits itself
    clearFrame(0);
}

/***
 * note: uint64_t does not overflow undefined.
 */
int VMread(uint64_t virtualAddress, word_t* value){
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE){
        return FAILURE;
    }
    auto physicalAddress = translate(virtualAddress, TABLES_DEPTH, virtualAddress >> OFFSET_WIDTH);
    PMread(physicalAddress, value);
    return SUCCESS;
}

/* Writes a word to the given virtual address.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMwrite(uint64_t virtualAddress, word_t value){
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE){
        return FAILURE;
    }
    auto physicalAddress = translate(virtualAddress, TABLES_DEPTH, virtualAddress >> OFFSET_WIDTH);
    PMwrite(physicalAddress, value);
    return SUCCESS;
}
