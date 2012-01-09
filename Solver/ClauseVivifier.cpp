/*
 * CryptoMiniSat
 *
 * Copyright (c) 2009-2011, Mate Soos and collaborators. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301  USA
*/

#include "ClauseVivifier.h"
#include "ClauseCleaner.h"
#include "time_mem.h"
#include "ThreadControl.h"
#include <iomanip>

//#define ASSYM_DEBUG

#ifdef VERBOSE_DEBUG
#define VERBOSE_SUBSUME_NONEXIST
#endif

//#define VERBOSE_SUBSUME_NONEXIST

const bool ClauseVivifier::SortBySize::operator()(const Clause* x, const Clause* y)
{
    return (x->size() > y->size());
}

ClauseVivifier::ClauseVivifier(ThreadControl* _control) :
    numCalls(0)
    , control(_control)
{}

const bool ClauseVivifier::vivify()
{
    assert(control->ok);
    #ifdef VERBOSE_DEBUG
    std::cout << "c clauseVivifier started" << std::endl;
    //control->printAllClauses();
    #endif //VERBOSE_DEBUG

    control->clauseCleaner->cleanClauses(control->clauses, ClauseCleaner::clauses);
    numCalls++;

    if (control->conf.doCache) {
        if (!vivifyClausesCache(control->clauses)) return false;
        if (!vivifyClausesCache(control->learnts)) return false;
    }

    if (!vivifyClausesNormal()) return false;

    return true;
}

struct BinSorter2 {
    const bool operator()(const Watched& first, const Watched& second)
    {
        if (!first.isBinary() && !second.isBinary()) return false;
        if (first.isBinary() && !second.isBinary()) return true;
        if (second.isBinary() && !first.isBinary()) return false;

        if (!first.getLearnt() && second.getLearnt()) return true;
        if (first.getLearnt() && !second.getLearnt()) return false;
        return false;
    };
};

void ClauseVivifier::makeNonLearntBin(const Lit lit1, const Lit lit2)
{
    findWatchedOfBin(control->watches, lit1 ,lit2, true).setLearnt(false);
    findWatchedOfBin(control->watches, lit2 ,lit1, true).setLearnt(false);
    control->learntsLits -= 2;
    control->clausesLits += 2;
}

/**
@brief Performs clause vivification (by Hamadi et al.)

This is the only thing that does not fit under the aegis of tryBoth(), since
it is not part of failed literal probing, really. However, it is here because
it seems to be a function that fits into the idology of failed literal probing.
Maybe I am off-course and it should be in another class, or a class of its own.
*/
const bool ClauseVivifier::vivifyClausesNormal()
{
    assert(control->ok);

    bool failed;
    uint32_t effective = 0;
    uint32_t effectiveLit = 0;
    double myTime = cpuTime();
    uint64_t maxNumProps = 35*1000*1000;
    if (control->clausesLits + control->learntsLits < 500000)
        maxNumProps *=2;
    uint64_t extraDiff = 0;
    uint64_t oldBogoProps = control->bogoProps;
    bool needToFinish = false;
    uint32_t checkedClauses = 0;
    uint32_t potentialClauses = control->clauses.size();
    vector<Lit> lits;
    vector<Lit> unused;

    if (control->clauses.size() < 1000000) {
        //if too many clauses, random order will do perfectly well
        std::sort(control->clauses.begin(), control->clauses.end(), SortBySize());
    }

    uint32_t queueByBy = 2;
    if (numCalls > 8
        && (control->clausesLits + control->learntsLits < 4000000)
        && (control->clauses.size() < 50000))
        queueByBy = 1;

    vector<Clause*>::iterator i, j;
    i = j = control->clauses.begin();
    for (vector<Clause*>::iterator end = control->clauses.end(); i != end; i++) {
        if (needToFinish) {
            *j++ = *i;
            continue;
        }

        //if done enough, stop doing it
        if (control->bogoProps-oldBogoProps + extraDiff > maxNumProps) {
            //std::cout << "Need to finish -- ran out of prop" << std::endl;
            needToFinish = true;
        }

        Clause& c = **i;
        extraDiff += c.size();
        checkedClauses++;

        assert(c.size() > 2);
        assert(!c.learnt());

        unused.clear();
        lits.resize(c.size());
        std::copy(c.begin(), c.end(), lits.begin());

        failed = false;
        uint32_t done = 0;
        control->newDecisionLevel();
        for (; done < lits.size();) {
            uint32_t i2 = 0;
            for (; (i2 < queueByBy) && ((done+i2) < lits.size()); i2++) {
                lbool val = control->value(lits[done+i2]);
                if (val == l_Undef) {
                    control->enqueue(~lits[done+i2]);
                } else if (val == l_False) {
                    unused.push_back(lits[done+i2]);
                }
            }
            done += i2;
            failed = (!control->propagate(false).isNULL());
            if (failed) break;
        }
        control->cancelZeroLight();
        assert(control->ok);

        if (unused.size() > 0 || (failed && done < lits.size())) {
            effective++;
            uint32_t origSize = lits.size();
            #ifdef ASSYM_DEBUG
            std::cout << "Assym branch effective." << std::endl;
            std::cout << "-- Orig clause:"; c.plainPrint();
            #endif
            control->detachClause(c);

            lits.resize(done);
            for (uint32_t i2 = 0; i2 < unused.size(); i2++) {
                remove(lits, unused[i2]);
            }

            Clause *c2 = control->addClauseInt(lits);
            #ifdef ASSYM_DEBUG
            std::cout << "-- Origsize:" << origSize << " newSize:" << (c2 == NULL ? 0 : c2->size()) << " toRemove:" << c.size() - done << " unused.size():" << unused.size() << std::endl;
            #endif
            extraDiff += 20;
            //TODO cheating here: we don't detect a NULL return that is in fact a 2-long clause
            effectiveLit += origSize - (c2 == NULL ? 0 : c2->size());
            control->clAllocator->clauseFree(&c);

            if (c2 != NULL) {
                #ifdef ASSYM_DEBUG
                std::cout << "-- New clause:"; c2->plainPrint();
                #endif
                *j++ = c2;
            }

            if (!control->ok) needToFinish = true;
        } else {
            *j++ = *i;
        }
    }
    control->clauses.resize(control->clauses.size()- (i-j));

    if (control->conf.verbosity  >= 1) {
        std::cout << "c asymm "
        << " cl-useful: " << effective << "/" << checkedClauses << "/" << potentialClauses
        << " lits-rem:" << effectiveLit
        << " time: " << cpuTime() - myTime
        << std::endl;
    }

    return control->ok;
}

const bool ClauseVivifier::vivifyClausesCache(vector<Clause*>& clauses)
{
    assert(control->ok);

    //Stats
    uint32_t litsRem = 0;
    uint32_t clShrinked = 0;
    uint32_t clRemoved = 0;
    uint64_t countTime = 0;
    uint64_t maxCountTime = 500000000;
    if (control->clausesLits + control->learntsLits < 300000)
        maxCountTime *= 2;
    uint32_t clTried = 0;
    double myTime = cpuTime();

    //Temps
    vector<Lit> lits;
    vector<char> seen(control->nVars()*2); //For strengthening
    bool needToFinish = false;

    vector<Clause*>::iterator i = clauses.begin();
    vector<Clause*>::iterator j = i;
    for (vector<Clause*>::iterator end = clauses.end(); i != end; i++) {
        //Check status
        if (needToFinish) {
            *j++ = *i;
            continue;
        }
        if (countTime > maxCountTime)
            needToFinish = true;

        //Setup
        Clause& cl = **i;
        assert(cl.size() > 2);
        countTime += cl.size()*2;
        clTried++;
        bool isSubsumed = false;

        //Fill 'seen'
        for (uint32_t i2 = 0; i2 < cl.size(); i2++)
            seen[cl[i2].toInt()] = 1;

        //Go through each literal and subsume/strengthen with it
        for (const Lit *l = cl.begin(), *end = cl.end(); l != end; l++) {
            //If already removed, we cannot strengthen with it
            if (seen[l->toInt()] == 0)
                continue;

            //Setup
            const Lit lit = *l;

            //Go through the watchlist
            const vec<Watched>& thisW = control->watches[(~lit).toInt()];
            countTime += thisW.size();
            for(vec<Watched>::const_iterator wit = thisW.begin(), wend = thisW.end(); wit != wend; wit++) {
                //Strengthening
                if (wit->isBinary())
                    seen[(~wit->getOtherLit()).toInt()] = 0;

                if (wit->isTriClause()) {
                    if (seen[(wit->getOtherLit()).toInt()])
                        seen[(~wit->getOtherLit2()).toInt()] = 0;
                    else if (seen[wit->getOtherLit2().toInt()])
                        seen[(~wit->getOtherLit()).toInt()] = 0;
                }

                //Subsumption
                if (wit->isBinary() &&
                    seen[wit->getOtherLit().toInt()]
                ) {
                    isSubsumed = true;
                    //If subsuming non-learnt with learnt, make the learnt into non-learnt
                    if (wit->getLearnt() && !cl.learnt())
                        makeNonLearntBin(lit, wit->getOtherLit());
                    break;
                }

                if (wit->isTriClause()
                    && cl.learnt() //We cannot distinguish between learnt and non-learnt, so we have to do with only learnt here
                    && seen[wit->getOtherLit().toInt()]
                    && seen[wit->getOtherLit2().toInt()]
                ) {
                    isSubsumed = true;
                    //If subsuming non-learnt with learnt, make the learnt into non-learnt
                    break;
                }
            }
            if (isSubsumed)
                break;

            //Go through the cache
            countTime += control->implCache[l->toInt()].lits.size();
            for (vector<LitExtra>::const_iterator it2 = control->implCache[lit.toInt()].lits.begin()
                , end2 = control->implCache[lit.toInt()].lits.end(); it2 != end2; it2++
            ) {
                seen[(~(it2->getLit())).toInt()] = 0;
            }
        }

        //Clear 'seen' and fill new clause data
        lits.clear();
        for (const Lit *it2 = cl.begin(), *end2 = cl.end(); it2 != end2; it2++) {
            if (seen[it2->toInt()]) lits.push_back(*it2);
            else litsRem++;
            seen[it2->toInt()] = 0;
        }

        //If nothing to do, then move along
        if (lits.size() == cl.size() && !isSubsumed) {
            *j++ = *i;
            continue;
        }

        //Else either remove or shrink
        countTime += cl.size()*10;
        control->detachClause(cl);
        if (isSubsumed) {
            clRemoved++;
            control->clAllocator->clauseFree(&cl);
        } else {
            clShrinked++;
            Clause* c2 = control->addClauseInt(lits, cl.learnt(), cl.getGlue());
            control->clAllocator->clauseFree(&cl);

            if (c2 != NULL) *j++ = c2;
            if (!control->ok) needToFinish = true;
        }
    }

    clauses.resize(clauses.size() - (i-j));

    if (control->conf.verbosity >= 1) {
        std::cout << "c vivif2 -- "
        << " cl tried " << std::setw(8) << clTried
        << " cl-sh " << std::setw(7) << clShrinked
        << " cl-rem " << std::setw(7) << clRemoved
        << " lit-rem " << std::setw(7) << litsRem
        << " time: " << (cpuTime() - myTime)
        << std::endl;
    }

    return control->ok;
}
