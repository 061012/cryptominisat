/***********************************************************************
CryptoMiniSat -- Copyright (c) 2009 Mate Soos

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
************************************************************************/

#include "ClauseAllocator.h"

#include <string.h>
#include <limits>
#include "assert.h"
#include "SolverTypes.h"
#include "Clause.h"
#include "Solver.h"
#include "time_mem.h"
#include "Subsumer.h"
#include "XorSubsumer.h"
//#include "VarReplacer.h"
#include "PartHandler.h"
#include "Gaussian.h"

//For mild debug info:
//#define DEBUG_CLAUSEALLOCATOR

//For listing each and every clause location:
//#define DEBUG_CLAUSEALLOCATOR2

#define MIN_LIST_SIZE (300000 * (sizeof(Clause) + 4*sizeof(Lit))/sizeof(uint32_t))
//#define MIN_LIST_SIZE (100 * (sizeof(Clause) + 4*sizeof(Lit))/sizeof(uint32_t))
#define ALLOC_GROW_MULT 4
//We shift stuff around in Watched, so not all of 32 bits are useable.
#define EFFECTIVELY_USEABLE_BITS 30

ClauseAllocator::ClauseAllocator()
    #ifdef USE_BOOST
    : clausePoolBin(sizeof(Clause) + 2*sizeof(Lit))
    #endif //USE_BOOST
{
    assert(MIN_LIST_SIZE < (1 << (EFFECTIVELY_USEABLE_BITS-NUM_BITS_OUTER_OFFSET)));
}

ClauseAllocator::~ClauseAllocator()
{
    for (uint32_t i = 0; i < dataStarts.size(); i++) {
        free(dataStarts[i]);
    }
}

template<class T>
Clause* ClauseAllocator::Clause_new(const T& ps, const unsigned int group, const bool learnt)
{
    assert(ps.size() > 0);
    void* mem = allocEnough(ps.size());
    Clause* real= new (mem) Clause(ps, group, learnt);
    //assert(!(ps.size() == 2 && !real->wasBin()));

    return real;
}
template Clause* ClauseAllocator::Clause_new(const vec<Lit>& ps, const unsigned int group, const bool learnt);
template Clause* ClauseAllocator::Clause_new(const Clause& ps, const unsigned int group, const bool learnt);
template Clause* ClauseAllocator::Clause_new(const XorClause& ps, const unsigned int group, const bool learnt);

template<class T>
XorClause* ClauseAllocator::XorClause_new(const T& ps, const bool inverted, const unsigned int group)
{
    assert(ps.size() > 0);
    void* mem = allocEnough(ps.size());
    XorClause* real= new (mem) XorClause(ps, inverted, group);
    //assert(!(ps.size() == 2 && !real->wasBin()));

    return real;
}
template XorClause* ClauseAllocator::XorClause_new(const vec<Lit>& ps, const bool inverted, const unsigned int group);
template XorClause* ClauseAllocator::XorClause_new(const XorClause& ps, const bool inverted, const unsigned int group);

Clause* ClauseAllocator::Clause_new(Clause& c)
{
    assert(c.size() > 0);
    void* mem = allocEnough(c.size());
    memcpy(mem, &c, sizeof(Clause)+sizeof(Lit)*c.size());
    Clause& c2 = *(Clause*)mem;
    c2.setWasBin(c.size() == 2);
    //assert(!(c.size() == 2 && !c2.wasBin()));
    
    return &c2;
}

void* ClauseAllocator::allocEnough(const uint32_t size)
{
    assert(sizes.size() == dataStarts.size());
    assert(maxSizes.size() == dataStarts.size());
    assert(origClauseSizes.size() == dataStarts.size());

    assert(sizeof(Clause)%sizeof(uint32_t) == 0);
    assert(sizeof(Lit)%sizeof(uint32_t) == 0);

    if (dataStarts.size() == (1<<NUM_BITS_OUTER_OFFSET)) {
        std::cerr << "Memory manager cannot handle the load. Sorry. Exiting." << std::endl;
        exit(-1);
    }

    if (size == 2) {
        #ifdef USE_BOOST
        return clausePoolBin.malloc();
        #else
        return malloc(sizeof(Clause) + 2*sizeof(Lit));
        #endif
    }
    
    uint32_t needed = sizeof(Clause)+sizeof(Lit)*size;
    bool found = false;
    uint32_t which = std::numeric_limits<uint32_t>::max();
    for (uint32_t i = 0; i < sizes.size(); i++) {
        if (sizes[i] + needed < maxSizes[i]) {
            found = true;
            which = i;
            break;
        }
    }

    if (!found) {
        uint32_t nextSize; //number of BYTES to allocate
        if (maxSizes.size() != 0)
            nextSize = std::min(maxSizes[maxSizes.size()-1]*ALLOC_GROW_MULT, (unsigned long)1 << (EFFECTIVELY_USEABLE_BITS - NUM_BITS_OUTER_OFFSET));
        else
            nextSize = MIN_LIST_SIZE;
        assert(needed <  nextSize);

        #ifdef DEBUG_CLAUSEALLOCATOR
        std::cout << "c New list in ClauseAllocator. Size: " << nextSize
        << " (maxSize: " << ((unsigned long)1 << (EFFECTIVELY_USEABLE_BITS - NUM_BITS_OUTER_OFFSET))
        << ")" << std::endl;
        #endif //DEBUG_CLAUSEALLOCATOR
        
        uint32_t *dataStart = (uint32_t*)malloc(nextSize*sizeof(uint32_t));
        assert(dataStart != NULL);
        dataStarts.push(dataStart);
        sizes.push(0);
        maxSizes.push(nextSize);
        origClauseSizes.push();
        currentlyUsedSizes.push(0);
        which = dataStarts.size()-1;
    }
    #ifdef DEBUG_CLAUSEALLOCATOR2
    std::cout
    << "selected list = " << which
    << " size = " << sizes[which]
    << " maxsize = " << maxSizes[which]
    << " diff = " << maxSizes[which] - sizes[which] << std::endl;
    #endif //DEBUG_CLAUSEALLOCATOR

    assert(which != std::numeric_limits<uint32_t>::max());
    Clause* pointer = (Clause*)(dataStarts[which] + sizes[which]);
    sizes[which] += needed/sizeof(uint32_t);
    currentlyUsedSizes[which] += needed/sizeof(uint32_t);
    origClauseSizes[which].push(needed/sizeof(uint32_t));

    return pointer;
}

const ClauseOffset ClauseAllocator::getOffset(const Clause* ptr) const
{
    uint32_t outerOffset = getOuterOffset(ptr);
    uint32_t interOffset = getInterOffset(ptr, outerOffset);
    return combineOuterInterOffsets(outerOffset, interOffset);
}

inline const ClauseOffset ClauseAllocator::combineOuterInterOffsets(const uint32_t outerOffset, const uint32_t interOffset) const
{
    return (outerOffset | (interOffset << NUM_BITS_OUTER_OFFSET));
}

inline uint32_t ClauseAllocator::getOuterOffset(const Clause* ptr) const
{
    uint32_t which = std::numeric_limits<uint32_t>::max();
    for (uint32_t i = 0; i < sizes.size(); i++) {
        if ((uint32_t*)ptr >= dataStarts[i] && (uint32_t*)ptr < dataStarts[i] + maxSizes[i]) {
            which = i;
            break;
        }
    }
    assert(which != std::numeric_limits<uint32_t>::max());

    return which;
}

inline uint32_t ClauseAllocator::getInterOffset(const Clause* ptr, uint32_t outerOffset) const
{
    return ((uint32_t*)ptr - dataStarts[outerOffset]);
}

void ClauseAllocator::clauseFree(Clause* c)
{
    if (c->wasBin()) {
        #ifdef USE_BOOST
        clausePoolBin.free(c);
        #else
        free(c);
        #endif
    } else {
        c->setFreed();
        uint32_t outerOffset = getOuterOffset(c);
        //uint32_t interOffset = getInterOffset(c, outerOffset);
        currentlyUsedSizes[outerOffset] -= (sizeof(Clause) + c->size()*sizeof(Lit))/sizeof(uint32_t);
        //above should be
        //origClauseSizes[outerOffset][interOffset]
        //but it cannot be :(
    }
}

struct NewPointerAndOffset {
    Clause* newPointer;
    uint32_t newOffset;
};

void ClauseAllocator::consolidate(Solver* solver)
{
    double myTime = cpuTime();
    
    //if (dataStarts.size() > 2) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < sizes.size(); i++) {
        sum += currentlyUsedSizes[i];
    }
    uint32_t sumAlloc = 0;
    for (uint32_t i = 0; i < sizes.size(); i++) {
        sumAlloc += sizes[i];
    }

    #ifdef DEBUG_CLAUSEALLOCATOR
    std::cout << "c ratio:" << (double)sum/(double)sumAlloc << std::endl;
    #endif //DEBUG_CLAUSEALLOCATOR

    //If re-allocation is not really neccessary, don't do it
    //Neccesities:
    //1) There is too much memory allocated. Re-allocation will save space
    //   Avoiding segfault (max is 16 outerOffsets, more than 10 is near)
    //2) There is too much empty, unused space (>30%)
    if ((double)sum/(double)sumAlloc > 0.7 && sizes.size() < 10) {
        if (solver->verbosity >= 3) {
            std::cout << "c Not consolidating memory." << std::endl;
        }
        return;
    }

    #ifdef DEBUG_CLAUSEALLOCATOR
    std::cout << "c ------ Consolidating Memory ------------" << std::endl;
    #endif //DEBUG_CLAUSEALLOCATOR
    int64_t newMaxSizeNeed = sum + MIN_LIST_SIZE;
    vec<uint32_t> newMaxSizes;
    for (uint32_t i = 0; i < (1 << NUM_BITS_OUTER_OFFSET); i++) {
        if (newMaxSizeNeed > 0) {
            uint32_t thisMaxSize = std::min(newMaxSizeNeed, (int64_t)1<<(EFFECTIVELY_USEABLE_BITS-NUM_BITS_OUTER_OFFSET));
            if (i == 0) {
                thisMaxSize = std::max(thisMaxSize, (uint32_t)MIN_LIST_SIZE);
            } else {
                assert(i > 0);
                thisMaxSize = std::max(thisMaxSize, newMaxSizes[i-1]/2);
            }
            newMaxSizeNeed -= thisMaxSize;
            newMaxSizes.push(thisMaxSize);
            //because the clauses don't always fit
            //it might occur that there is enough place in total
            //but the very last clause would need to be fragmented
            //over multiple lists' ends :O
            //So this "magic" constant could take care of that....
            //or maybe not (if _very_ large clauses are used, always
            //bad chance, etc. :O )
            newMaxSizeNeed += 1000;
        } else {
            break;
        }
        #ifdef DEBUG_CLAUSEALLOCATOR
        std::cout << "c NEW MaxSizes:" << newMaxSizes[i] << std::endl;
        #endif //DEBUG_CLAUSEALLOCATOR
    }
    #ifdef DEBUG_CLAUSEALLOCATOR
    std::cout << "c ------------------" << std::endl;
    #endif //DEBUG_CLAUSEALLOCATOR

    if (newMaxSizeNeed > 0) {
        std::cerr << "We cannot handle the memory need load. Exiting." << std::endl;
        exit(-1);
    }

    vec<uint32_t> newSizes;
    vec<vec<uint32_t> > newOrigClauseSizes;
    vec<uint32_t*> newDataStartsPointers;
    vec<uint32_t*> newDataStarts;
    for (uint32_t i = 0; i < (1 << NUM_BITS_OUTER_OFFSET); i++) {
        if (newMaxSizes[i] == 0) break;
        newSizes.push(0);
        newOrigClauseSizes.push();
        uint32_t* pointer = (uint32_t*)malloc(newMaxSizes[i]*sizeof(uint32_t));
        if (pointer == 0) {
            std::cerr << "Cannot allocate enough memory!" << std::endl;
            exit(-1);
        }
        newDataStartsPointers.push(pointer);
        newDataStarts.push(pointer);
    }

    map<Clause*, Clause*> oldToNewPointer;
    map<uint32_t, uint32_t> oldToNewOffset;

    uint32_t outerPart = 0;
    for (uint32_t i = 0; i < dataStarts.size(); i++) {
        uint32_t currentLoc = 0;
        for (uint32_t i2 = 0; i2 < origClauseSizes[i].size(); i2++) {
            Clause* oldPointer = (Clause*)(dataStarts[i] + currentLoc);
            if (!oldPointer->freed()) {
                uint32_t sizeNeeded = (sizeof(Clause) + oldPointer->size()*sizeof(Lit))/sizeof(uint32_t);
                if (newSizes[outerPart] + sizeNeeded > newMaxSizes[outerPart]) {
                    outerPart++;
                }
                memcpy(newDataStartsPointers[outerPart], dataStarts[i] + currentLoc, sizeNeeded*sizeof(uint32_t));

                oldToNewPointer[oldPointer] = (Clause*)newDataStartsPointers[outerPart];
                oldToNewOffset[combineOuterInterOffsets(i, currentLoc)] = combineOuterInterOffsets(outerPart, newSizes[outerPart]);

                newSizes[outerPart] += sizeNeeded;
                newOrigClauseSizes[outerPart].push(sizeNeeded);
                newDataStartsPointers[outerPart] += sizeNeeded;
            }

            currentLoc += origClauseSizes[i][i2];
        }
    }
    //assert(newSize < newMaxSize);
    //assert(newSize <= newMaxSize/2);

    updateOffsets(solver->watches, oldToNewOffset);

    updatePointers(solver->clauses, oldToNewPointer);
    updatePointers(solver->learnts, oldToNewPointer);
    updatePointers(solver->binaryClauses, oldToNewPointer);
    updatePointers(solver->xorclauses, oldToNewPointer);
    updatePointers(solver->freeLater, oldToNewPointer);

    //No need to update varreplacer, since it only stores binary clauses that
    //must have been allocated such as to use the pool
    //updatePointers(solver->varReplacer->clauses, oldToNewPointer);
    updatePointers(solver->partHandler->clausesRemoved, oldToNewPointer);
    updatePointers(solver->partHandler->xorClausesRemoved, oldToNewPointer);
    for(map<Var, vector<Clause*> >::iterator it = solver->subsumer->elimedOutVar.begin(); it != solver->subsumer->elimedOutVar.end(); it++) {
        updatePointers(it->second, oldToNewPointer);
    }
    for(map<Var, vector<XorClause*> >::iterator it = solver->xorSubsumer->elimedOutVar.begin(); it != solver->xorSubsumer->elimedOutVar.end(); it++) {
        updatePointers(it->second, oldToNewPointer);
    }

    #ifdef USE_GAUSS
    for (uint32_t i = 0; i < solver->gauss_matrixes.size(); i++) {
        updatePointers(solver->gauss_matrixes[i]->xorclauses, oldToNewPointer);
        updatePointers(solver->gauss_matrixes[i]->clauses_toclear, oldToNewPointer);
    }
    #endif //USE_GAUSS

    vec<PropagatedFrom>& reason = solver->reason;
    for (PropagatedFrom *it = reason.getData(), *end = reason.getDataEnd(); it != end; it++) {
        if (it->isClause() && !it->isNULL()) {
            if (oldToNewPointer.find(it->getClause()) != oldToNewPointer.end()) {
                *it = PropagatedFrom(oldToNewPointer[it->getClause()]);
            }
        }
    }

    for (uint32_t i = 0; i < dataStarts.size(); i++)
        free(dataStarts[i]);

    dataStarts.clear();
    maxSizes.clear();
    sizes.clear();
    origClauseSizes.clear();
    currentlyUsedSizes.clear();
    origClauseSizes.clear();

    for (uint32_t i = 0; i < (1 << NUM_BITS_OUTER_OFFSET); i++) {
        if (newMaxSizes[i] == 0) break;
        dataStarts.push(newDataStarts[i]);
        maxSizes.push(newMaxSizes[i]);
        sizes.push(newSizes[i]);
        currentlyUsedSizes.push(newSizes[i]);
    }
    newOrigClauseSizes.moveTo(origClauseSizes);

    if (solver->verbosity >= 3) {
        std::cout << "c Consolidated memory. Time: "
        << cpuTime() - myTime << std::endl;
    }
}

template<class T>
void ClauseAllocator::updateOffsets(vec<vec<T> >& watches, const map<ClauseOffset, ClauseOffset>& oldToNewOffset)
{
    for (uint32_t i = 0;  i < watches.size(); i++) {
        vec<T>& list = watches[i];
        for (T *it = list.getData(), *end = list.getDataEnd(); it != end; it++) {
            if (!it->isClause() && !it->isXorClause()) continue;
            
            map<ClauseOffset, ClauseOffset>::const_iterator it2 = oldToNewOffset.find(it->getOffset());
            assert(it2 != oldToNewOffset.end());
            it->setOffset(it2->second);
        }
    }
}

template<class T>
void ClauseAllocator::updatePointers(vec<T*>& toUpdate, const map<Clause*, Clause*>& oldToNewPointer)
{
    for (T **it = toUpdate.getData(), **end = toUpdate.getDataEnd(); it != end; it++) {
        if (!(*it)->wasBin()) {
            //assert(oldToNewPointer.find((TT*)*it) != oldToNewPointer.end());
            map<Clause*, Clause*>::const_iterator it2 = oldToNewPointer.find((Clause*)*it);
            *it = (T*)it2->second;
        }
    }
}

void ClauseAllocator::updatePointers(vector<Clause*>& toUpdate, const map<Clause*, Clause*>& oldToNewPointer)
{
    for (vector<Clause*>::iterator it = toUpdate.begin(), end = toUpdate.end(); it != end; it++) {
        if (!(*it)->wasBin()) {
            //assert(oldToNewPointer.find((TT*)*it) != oldToNewPointer.end());
            map<Clause*, Clause*>::const_iterator it2 = oldToNewPointer.find((Clause*)*it);
            *it = it2->second;
        }
    }
}

void ClauseAllocator::updatePointers(vector<XorClause*>& toUpdate, const map<Clause*, Clause*>& oldToNewPointer)
{
    for (vector<XorClause*>::iterator it = toUpdate.begin(), end = toUpdate.end(); it != end; it++) {
        if (!(*it)->wasBin()) {
            //assert(oldToNewPointer.find((TT*)*it) != oldToNewPointer.end());
            map<Clause*, Clause*>::const_iterator it2 = oldToNewPointer.find((Clause*)*it);
            *it = (XorClause*)it2->second;
        }
    }
}

void ClauseAllocator::updatePointers(vector<pair<Clause*, uint32_t> >& toUpdate, const map<Clause*, Clause*>& oldToNewPointer)
{
    for (vector<pair<Clause*, uint32_t> >::iterator it = toUpdate.begin(), end = toUpdate.end(); it != end; it++) {
        if (!(it->first)->wasBin()) {
            //assert(oldToNewPointer.find((TT*)*it) != oldToNewPointer.end());
            map<Clause*, Clause*>::const_iterator it2 = oldToNewPointer.find(it->first);
            it->first = (Clause*)it2->second;
        }
    }
}
