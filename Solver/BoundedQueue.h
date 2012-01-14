/*****************************************************************************************[Queue.h]
MiniSat -- Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
2008 - Gilles Audemard, Laurent Simon
CryptoMiniSat -- Copyright (c) 2009 Mate Soos

Original code by MiniSat and glucose authors are under an MIT licence.
Modifications for CryptoMiniSat are under GPLv3 licence.
**************************************************************************************************/

#ifndef BOUNDEDQUEUE_H
#define BOUNDEDQUEUE_H

#ifdef _MSC_VER
#include <msvc/stdint.h>
#else
#include <stdint.h>
#endif //_MSC_VER

#include "Vec.h"

template <class T>
class bqueue {
    std::vector<T>  elems;
    uint32_t first;
    uint32_t last;
    int64_t  sumofqueue;
    int64_t  sumOfAllElems;
    uint64_t totalNumElems;
    uint32_t maxsize;
    uint32_t queuesize; // Number of current elements (must be < maxsize !)

public:
    bqueue(void) :
        first(0)
        , last(0)
        , sumofqueue(0)
        , sumOfAllElems(0)
        , totalNumElems(0)
        , maxsize(0)
        , queuesize(0)
    {}

    void push(const T x) {
        if (queuesize==maxsize) {
            assert(last==first); // The queue is full, next value to enter will replace oldest one
            sumofqueue -= elems[last];
            if ((++last) == maxsize) last = 0;
        } else
            queuesize++;
        sumofqueue += x;
        sumOfAllElems += x;
        totalNumElems++;
        elems[first] = x;
        if ((++first) == maxsize) first = 0;
    }

    const T peek() const { assert(queuesize>0); return elems[last]; }
    void pop() {sumofqueue-=elems[last]; queuesize--; if ((++last) == maxsize) last = 0;}

    int64_t getsum() const
    {
        return sumofqueue;
    }

    uint32_t getAvgUInt() const
    {
        assert(isvalid());
        return (uint64_t)sumofqueue/(uint64_t)queuesize;
    }

    double getAvgDouble() const
    {
        assert(isvalid());
        return (double)sumofqueue/(double)queuesize;
    }

    double getAvgAllDouble() const
    {
        if (totalNumElems == 0)
            return 0;

        return (double)sumOfAllElems/(double)totalNumElems;
    }

    uint64_t getTotalNumeElems() const
    {
        return totalNumElems;
    }

    bool isvalid() const
    {
        return (queuesize==maxsize);
    }

    void resize(const uint32_t size)
    {
        elems.resize(size);
        first=0; maxsize=size; queuesize = 0;
        for(uint32_t i=0;i<size;i++) elems[i]=0;
    }

    void fastclear() {
        //Discard the queue, but not the SUMs
        first = 0;
        last = 0;
        queuesize=0;
        sumofqueue=0;
    }

    int  size(void)
    {
        return queuesize;

    }

    void clear()   {
        elems.clear();
        first = 0;
        last = 0;
        maxsize=0;
        queuesize=0;
        sumofqueue=0;

        totalNumElems = 0;
        sumOfAllElems = 0;
    }
};

#endif //BOUNDEDQUEUE_H
