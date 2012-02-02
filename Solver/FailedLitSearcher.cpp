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

#include "FailedLitSearcher.h"

#include <iomanip>
#include <utility>
#include <set>
#include <utility>
using std::make_pair;
using std::set;
using std::cout;
using std::endl;

#include "ThreadControl.h"
#include "ClauseCleaner.h"
#include "time_mem.h"
#include "ClauseCleaner.h"
#include "CompleteDetachReattacher.h"

//#define VERBOSE_DEUBUG

/**
@brief Sets up variables that are used between calls to search()
*/
FailedLitSearcher::FailedLitSearcher(ThreadControl* _control):
    control(_control)
    , tmpPs(2)
    , totalTime(0)
    , totalZeroDepthAssigns(0)
    , totalNumFailed(0)
    , totalNumTried(0)
    , totalNumVisited(0)
    , totalAddedBin(0)
    , totalRemovedBin(0)
    , numPropsMultiplier(1.0)
    , lastTimeZeroDepthAssings(0)
    , numCalls(0)
{
}

struct ActSorter
{
    ActSorter(const vector<uint32_t>& _activities) :
        activities(_activities)
    {};

    const vector<uint32_t>& activities;

    bool operator()(uint32_t var1, uint32_t var2) const
    {
        //Least active vars first
        return (activities[var1] < activities[var2]);
    }
};

bool FailedLitSearcher::search()
{
    assert(control->decisionLevel() == 0);

    uint64_t numPropsTodo = 110L*1000L*1000L;

    control->testAllClauseAttach();
    const double myTime = cpuTime();
    const uint32_t origNumUnsetVars = control->getNumUnsetVars();
    origTrailSize = control->trail.size();
    origNumFreeVars = control->getNumFreeVars();
    control->clauseCleaner->removeAndCleanAll();

    //General Stats
    numTried = 0;
    numVisited = 0;
    numFailed = 0;
    numCalls++;
    visitedAlready.clear();
    visitedAlready.resize(control->nVars()*2, 0);
    cacheUpdated.clear();
    cacheUpdated.resize(control->nVars()*2, 0);

    //If failed var searching is going good, do successively more and more of it
    if ((double)lastTimeZeroDepthAssings > (double)control->getNumUnsetVars() * 0.10)
        numPropsMultiplier = std::max(numPropsMultiplier*1.3, 1.6);
    else
        numPropsMultiplier = 1.0;
    numPropsTodo = (uint64_t) ((double)numPropsTodo * numPropsMultiplier * control->conf.failedLitMultiplier);

    //For HyperBin
    addedBin = 0;
    removedBins = 0;

    //For var-based failed literal probing
    vector<uint32_t> actSortedVars(control->nVars());
    for(size_t i = 0; i < actSortedVars.size(); i++) {
        actSortedVars[i] = i;
    }
    std::sort(actSortedVars.begin(), actSortedVars.end(), ActSorter(control->backupActivity));


    origBogoProps = control->bogoProps;
    for (uint32_t i = 0; i < control->nVars(); i++) {
        const Var var = (control->mtrand.randInt() + i) % control->nVars();
        if (control->value(var) != l_Undef || !control->decision_var[var])
            continue;
        if (control->bogoProps + extraTime >= origBogoProps + numPropsTodo)
            break;

        const Lit lit = Lit(var, false);

        //Try one way
        if (!visitedAlready[lit.toInt()]
            && !tryThis(lit)
        ) {
            goto end;
        }

        //We have set it, continue
        if (control->value(var) != l_Undef)
            continue;

        //Try the other way
        if (!visitedAlready[(~lit).toInt()]
            && !tryThis(~lit)
        ) {
            goto end;
        }


    }

    /*for (vector<uint32_t>::const_iterator
            it = actSortedVars.begin(), end = actSortedVars.end()
            ; it != end
            ; it++
    ) {
        const Var var = *it;
        if (control->value(var) != l_Undef || !control->decision_var[var])
            continue;

        if (control->bogoProps >= origBogoProps + numPropsTodo)
            break;
        if (!tryBoth(Lit(var, false)))
            goto end;
    }*/

end:

    //Count how many have been visited
    for(size_t i = 0; i < visitedAlready.size(); i++) {
        if (visitedAlready[i])
            numVisited++;
    }

    //Print & update stats
    if (control->conf.verbosity  >= 1)
        printResults(myTime);

    if (control->ok && numFailed) {
        double time = cpuTime();
        if ((int)origNumUnsetVars - (int)control->getNumUnsetVars() >  (int)origNumUnsetVars/15
            && control->getNumClauses() > 500000
        ) {
            CompleteDetachReatacher reattacher(control);
            reattacher.detachNonBinsNonTris(true);
            const bool ret = reattacher.reattachNonBins();
            release_assert(ret == true);
        } else {
            control->clauseCleaner->removeAndCleanAll();
        }
        if (control->conf.verbosity  >= 1 && numFailed > 100) {
            cout
            << "c Cleaning up after failed var search: "
            << std::setw(8) << std::fixed << std::setprecision(2) << cpuTime() - time << " s "
            << endl;
        }
    }

    //Update stats
    lastTimeZeroDepthAssings = control->trail.size() - origTrailSize;
    totalZeroDepthAssigns += lastTimeZeroDepthAssings;
    totalNumFailed += numFailed;
    totalNumTried += numTried;
    totalNumVisited += numVisited;
    totalTime += cpuTime() - myTime;
    totalAddedBin += addedBin;
    totalRemovedBin += removedBins;

    control->testAllClauseAttach();
    return control->ok;
}

void FailedLitSearcher::printResults(const double myTime) const
{
    cout
    << "c"
    << " 0-depth assigns: " << (control->trail.size() - origTrailSize)
    << " Flit: " << numFailed
    << " Visited: " << numVisited << " / " << (origNumFreeVars*2) // x2 because it's LITERAL visit
    << " tried: " << numTried
    << " Bin:" << addedBin
    << " RemBin:" << removedBins
    << " P: " << std::fixed << std::setprecision(1) << (double)(control->bogoProps - origBogoProps)/1000000.0  << "M"
    << " T: " << std::fixed << std::setprecision(2) << cpuTime() - myTime
    << endl;
}

bool FailedLitSearcher::tryThis(const Lit lit)
{
    //Start-up cleaning
    numTried++;
    assert(uselessBin.empty());

    //Test removal of non-learnt binary clauses
    #ifdef DEBUG_REMOVE_USELESS_BIN
    fillTestUselessBinRemoval(lit);
    #endif

    control->newDecisionLevel();
    control->enqueue(lit);
    #ifdef VERBOSE_DEBUG_FULLPROP
    cout << "Trying " << lit << endl;
    #endif
    const Lit failed = control->propagateFull(uselessBin);
    if (failed != lit_Undef) {
        control->cancelZeroLight();
        numFailed++;
        vector<Lit> lits;
        lits.push_back(~failed);
        control->addClauseInt(lits, true);
        removeUselessBins();
        return control->ok;
    }

    //Fill bothprop, cache
    assert(control->decisionLevel() > 0);
    for (int64_t c = control->trail.size()-1; c != (int64_t)control->trail_lim[0] - 1; c--) {
        const Lit thisLit = control->trail[c];

        visitedAlready[thisLit.toInt()] = 1;

        const Lit ancestor = control->propData[thisLit.var()].ancestor;
        if (control->conf.doCache
            && thisLit != lit
            && cacheUpdated[(~ancestor).toInt()] == 0
        ) {
            //Update stats/markings
            cacheUpdated[(~ancestor).toInt()] = 1;
            extraTime += control->implCache[(~ancestor).toInt()].lits.size()/4;
            extraTime += control->implCache[(~thisLit).toInt()].lits.size()/4;

            const bool learntStep = control->propData[thisLit.var()].learntStep;

            assert(ancestor != lit_Undef);
            control->implCache[(~ancestor).toInt()].merge(
                control->implCache[(~thisLit).toInt()].lits
                , thisLit
                , learntStep
                , ancestor
                , control->seen
            );

            #ifdef VERBOSE_DEBUG_FULLPROP
            cout << "The impl cache of " << (~ancestor) << " is now: ";
            cout << control->implCache[(~ancestor).toInt()] << endl;
            #endif
        }
    }

    control->cancelZeroLight();
    hyperBinResAll();
    removeUselessBins();
    #ifdef DEBUG_REMOVE_USELESS_BIN
    testBinRemoval(lit);
    #endif

    return control->ok;
}

void FailedLitSearcher::hyperBinResAll()
{
    for(std::set<BinaryClause>::const_iterator it = control->needToAddBinClause.begin(), end = control->needToAddBinClause.end(); it != end; it++) {
        tmpPs[0] = it->getLit1();
        tmpPs[1] = it->getLit2();
        Clause* cl = control->addClauseInt(tmpPs, true);
        assert(cl == NULL);
        assert(control->ok);
        addedBin++;
    }
}

void FailedLitSearcher::removeUselessBins()
{
    if (control->conf.doRemUselessBins) {
        for(std::set<BinaryClause>::iterator
            it = uselessBin.begin()
            , end = uselessBin.end()
            ; it != end
            ; it++
        ) {
            //cout << "Removing binary clause: " << *it << endl;
            removeWBin(control->watches, it->getLit1(), it->getLit2(), it->getLearnt());
            removeWBin(control->watches, it->getLit2(), it->getLit1(), it->getLearnt());

            //Update stats
            control->numBins--;
            if (it->getLearnt())
                control->learntsLits -= 2;
            else
                control->clausesLits -= 2;
            removedBins++;

            #ifdef VERBOSE_DEBUG_FULLPROP
            cout << "Removed bin: "
            << it->getLit1() << " , " << it->getLit2()
            << " , learnt: " << it->getLearnt() << endl;
            #endif
        }
    }
    uselessBin.clear();
}

size_t FailedLitSearcher::getTotalZeroDepthAssigns() const
{
    return totalZeroDepthAssigns;
}

size_t FailedLitSearcher::getTotalNumFailed() const
{
    return totalNumFailed;
}

size_t FailedLitSearcher::getTotalAddedBin() const
{
    return totalAddedBin;
}

size_t FailedLitSearcher::getTotalRemovedBin() const
{
    return totalRemovedBin;
}

#ifdef DEBUG_REMOVE_USELESS_BIN
void FailedLitSearcher::fillTestUselessBinRemoval(const Lit lit)
{
    origNLBEnqueuedVars.clear();
    control->newDecisionLevel();
    control->enqueue(lit);
    failed = (!control->propagateNonLearntBin().isNULL());
    for (int c = control->trail.size()-1; c >= (int)control->trail_lim[0]; c--) {
        Var x = control->trail[c].var();
        origNLBEnqueuedVars.push_back(x);
    }
    control->cancelZeroLight();

    origEnqueuedVars.clear();
    control->newDecisionLevel();
    control->enqueue(lit);
    failed = (!control->propagate(false).isNULL());
    for (int c = control->trail.size()-1; c >= (int)control->trail_lim[0]; c--) {
        Var x = control->trail[c].var();
        origEnqueuedVars.push_back(x);
    }
    control->cancelZeroLight();
}

void FailedLitSearcher::testBinRemoval(const Lit origLit)
{
    control->newDecisionLevel();
    control->enqueue(origLit);
    bool ok = control->propagate().isNULL();
    assert(ok && "Prop failed after hyper-bin adding&bin removal. We never reach this point in that case.");
    bool wrong = false;
    for (vector<Var>::const_iterator it = origEnqueuedVars.begin(), end = origEnqueuedVars.end(); it != end; it++) {
        if (control->value(*it) == l_Undef) {
            cout << "Value of var " << Lit(*it, false) << " is unset, but was set before!" << endl;
            wrong = true;
        }
    }
    assert(!wrong && "Learnt/Non-learnt bin removal is incorrect");
    control->cancelZeroLight();

    control->newDecisionLevel();
    control->enqueue(origLit);
    ok = control->propagateNonLearntBin().isNULL();
    assert(ok && "Prop failed after hyper-bin adding&bin removal. We never reach this point in that case.");
    for (vector<Var>::const_iterator it = origNLBEnqueuedVars.begin(), end = origNLBEnqueuedVars.end(); it != end; it++) {
        if (control->value(*it) == l_Undef) {
            cout << "Value of var " << Lit(*it, false) << " is unset, but was set before when propagating non-learnt!" << endl;
            wrong = true;
        }
    }
    assert(!wrong && "Non-learnt bin removal is incorrect");
    control->cancelZeroLight();
}
#endif

// void FailedLitSearcher::fillToTry(vector<Var>& toTry)
// {
//     uint32_t max = std::min(control->negPosDist.size()-1, (size_t)300);
//     while(true) {
//         Var var = control->negPosDist[control->mtrand.randInt(max)].var;
//         if (control->value(var) != l_Undef
//             || (control->varData[var].elimed != ELIMED_NONE
//                 && control->varData[var].elimed != ELIMED_QUEUED_VARREPLACER)
//             ) continue;
//
//         bool OK = true;
//         for (uint32_t i = 0; i < toTry.size(); i++) {
//             if (toTry[i] == var) {
//                 OK = false;
//                 break;
//             }
//         }
//         if (OK) {
//             toTry.push_back(var);
//             return;
//         }
//     }
// }
//
// const bool FailedLitSearcher::tryMultiLevelAll()
// {
//     assert(control->ok);
//     uint32_t backupNumUnits = control->trail.size();
//     double myTime = cpuTime();
//     uint32_t numTries = 0;
//     uint32_t finished = 0;
//     uint64_t oldBogoProps = control->bogoProps;
//     uint32_t enqueued = 0;
//     uint32_t numFailed = 0;
//
//     if (control->negPosDist.size() < 30) return true;
//
//     propagated.resize(control->nVars(), 0);
//     propagated2.resize(control->nVars(), 0);
//     propValue.resize(control->nVars(), 0);
//     assert(propagated.isZero());
//     assert(propagated2.isZero());
//
//     vector<Var> toTry;
//     while(control->bogoProps < oldBogoProps + 300*1000*1000) {
//         toTry.clear();
//         for (uint32_t i = 0; i < 3; i++) {
//             fillToTry(toTry);
//         }
//         numTries++;
//         if (!tryMultiLevel(toTry, enqueued, finished, numFailed)) goto end;
//     }
//
//     end:
//     assert(propagated.isZero());
//     assert(propagated2.isZero());
//
//     cout
//     << "c multiLevelBoth tried " <<  numTries
//     << " finished: " << finished
//     << " units: " << (control->trail.size() - backupNumUnits)
//     << " enqueued: " << enqueued
//     << " numFailed: " << numFailed
//     << " time: " << (cpuTime() - myTime)
//     << endl;
//
//     return control->ok;
// }
//
// const bool FailedLitSearcher::tryMultiLevel(const vector<Var>& vars, uint32_t& enqueued, uint32_t& finished, uint32_t& numFailed)
// {
//     assert(control->ok);
//
//     vector<Lit> toEnqueue;
//     bool first = true;
//     bool last = false;
//     //cout << "//////////////////" << endl;
//     for (uint32_t comb = 0; comb < (1U << vars.size()); comb++) {
//         last = (comb == (1U << vars.size())-1);
//         control->newDecisionLevel();
//         for (uint32_t i = 0; i < vars.size(); i++) {
//             control->enqueue(Lit(vars[i], comb&(0x1 << i)));
//             //cout << "lit: " << Lit(vars[i], comb&(1U << i)) << endl;
//         }
//         //cout << "---" << endl;
//         bool failed = !(control->propagate().isNULL());
//         if (failed) {
//             control->cancelZeroLight();
//             if (!first) propagated.setZero();
//             numFailed++;
//             return true;
//         }
//
//         for (int sublevel = control->trail.size()-1; sublevel > (int)control->trail_lim[0]; sublevel--) {
//             Var x = control->trail[sublevel].var();
//             if (first) {
//                 propagated.setBit(x);
//                 if (control->assigns[x].getBool()) propValue.setBit(x);
//                 else propValue.clearBit(x);
//             } else if (last) {
//                 if (propagated[x] && control->assigns[x].getBool() == propValue[x])
//                     toEnqueue.push_back(Lit(x, !propValue[x]));
//             } else {
//                 if (control->assigns[x].getBool() == propValue[x]) {
//                     propagated2.setBit(x);
//                 }
//             }
//         }
//         control->cancelZeroLight();
//         if (!first && !last) propagated &= propagated2;
//         propagated2.setZero();
//         if (propagated.isZero()) return true;
//         first = false;
//     }
//     propagated.setZero();
//     finished++;
//
//     for (vector<Lit>::iterator l = toEnqueue.begin(), end = toEnqueue.end(); l != end; l++) {
//         enqueued++;
//         control->enqueue(*l);
//     }
//     control->ok = control->propagate().isNULL();
//     //exit(-1);
//
//     return control->ok;
// }
