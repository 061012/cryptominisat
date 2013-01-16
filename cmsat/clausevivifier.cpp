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

#include "clausevivifier.h"
#include "clausecleaner.h"
#include "time_mem.h"
#include "solver.h"
#include <iomanip>
using std::cout;
using std::endl;

//#define ASSYM_DEBUG
//#define DEBUG_STAMPING

#ifdef VERBOSE_DEBUG
#define VERBOSE_SUBSUME_NONEXIST
#endif

//#define VERBOSE_SUBSUME_NONEXIST

ClauseVivifier::ClauseVivifier(Solver* _solver) :
    solver(_solver)
    , numCalls(0)
{}

bool ClauseVivifier::vivify(const bool alsoStrengthen)
{
    assert(solver->ok);
    #ifdef VERBOSE_DEBUG
    cout << "c clauseVivifier started" << endl;
    #endif //VERBOSE_DEBUG
    numCalls++;

    solver->clauseCleaner->cleanClauses(solver->longIrredCls);

    if (!vivifyClausesCache(solver->longIrredCls, false, alsoStrengthen))
        goto end;

    if (!vivifyClausesCache(solver->longRedCls, true, alsoStrengthen))
        goto end;

    if (alsoStrengthen
        && !vivifyClausesLongIrred()
    ) {
        goto end;
    }

    if (alsoStrengthen
        && !vivifyClausesTriIrred()
    ) {
        goto end;
    }

end:
    //Stats
    globalStats += runStats;
    if (solver->conf.verbosity >= 1) {
        if (solver->conf.verbosity >= 3)
            runStats.print(solver->nVars());
        else
            runStats.printShort();
    }
    runStats.clear();

    return solver->ok;
}

bool ClauseVivifier::vivifyClausesTriIrred()
{
    uint64_t origShorten = runStats.numClShorten;
    uint64_t origLitRem = runStats.numLitsRem;
    double myTime = cpuTime();
    uint64_t maxNumProps = 2L*1000L*1000L;
    uint64_t oldBogoProps = solver->propStats.bogoProps;
    size_t origTrailSize = solver->trail.size();

    size_t upI;
    upI = solver->mtrand.randInt(solver->watches.size()-1);
    size_t numDone = 0;
    for (; numDone < solver->watches.size()
        ; upI = (upI +1) % solver->watches.size(), numDone++

    ) {
        if (solver->propStats.bogoProps-oldBogoProps + extraTime > maxNumProps) {
            break;
        }

        Lit lit = Lit::toLit(upI);
        const vec<Watched>& ws = solver->watches[upI];
        for (size_t i = 0; i < ws.size(); i++) {
            if (solver->propStats.bogoProps-oldBogoProps + extraTime > maxNumProps) {
                break;
            }

            //Only TRI and each TRI only once
            if (ws[i].isTri()
                && lit < ws[i].lit1()
                && ws[i].lit1() < ws[i].lit2()
            ) {
                uselessLits.clear();
                lits.resize(3);
                lits[0] = lit;
                lits[1] = ws[i].lit1();
                lits[2] = ws[i].lit2();
                testVivify(
                    std::numeric_limits<ClOffset>::max()
                    , NULL
                    , ws[i].learnt()
                    , 2
                );

                //We could have modified the watchlist, better exit now
                break;
            }
        }

        if (!solver->okay())
            break;
    }

    if (solver->conf.verbosity >= 3) {
        cout
        << "c [vivif] tri "
        << " tri-rem: " << runStats.numClShorten - origShorten
        << " lit-rem: " << runStats.numLitsRem - origLitRem
        << " 0-depth ass: " << solver->trail.size() - origTrailSize
        << " time: " << cpuTime() - myTime
        << endl;
    }

    runStats.zeroDepthAssigns = solver->trail.size() - origTrailSize;

    return solver->ok;
}

/**
@brief Performs clause vivification (by Hamadi et al.)
*/
bool ClauseVivifier::vivifyClausesLongIrred()
{
    assert(solver->ok);

    double myTime = cpuTime();
    const size_t origTrailSize = solver->trail.size();

    //Time-limiting
    uint64_t maxNumProps = 5L*1000L*1000L;
    if (solver->binTri.irredLits + solver->binTri.redLits < 500000)
        maxNumProps *=2;

    extraTime = 0;
    uint64_t oldBogoProps = solver->propStats.bogoProps;
    bool needToFinish = false;
    runStats.potentialClauses = solver->longIrredCls.size();
    runStats.numCalled = 1;

    cout << "c WARNING!! We didn't sort by clause size here" << endl;
    uint64_t origLitRem = runStats.numLitsRem;
    uint64_t origClShorten = runStats.numClShorten;

    uint32_t queueByBy = 2;
    if (numCalls > 8
        && (solver->binTri.irredLits + solver->binTri.redLits < 4000000)
        && (solver->longIrredCls.size() < 50000))
        queueByBy = 1;

    vector<ClOffset>::iterator i, j;
    i = j = solver->longIrredCls.begin();
    for (vector<ClOffset>::iterator end = solver->longIrredCls.end()
        ; i != end
        ; i++
    ) {
        //Check if we are in state where we only copy offsets around
        if (needToFinish || !solver->ok) {
            *j++ = *i;
            continue;
        }

        //if done enough, stop doing it
        if (solver->propStats.bogoProps-oldBogoProps + extraTime > maxNumProps) {
            if (solver->conf.verbosity >= 2) {
                cout
                << "c Need to finish asymm -- ran out of prop (=allocated time)"
                << endl;
            }
            needToFinish = true;
        }

        //Get pointer
        ClOffset offset = *i;
        Clause& cl = *solver->clAllocator->getPointer(offset);
        extraTime += cl.size();
        runStats.checkedClauses++;

        //Sanity check
        assert(cl.size() > 3);
        assert(!cl.learnt());

        //Copy literals
        uselessLits.clear();
        lits.resize(cl.size());
        std::copy(cl.begin(), cl.end(), lits.begin());

        //Try to vivify clause
        ClOffset offset2 = testVivify(
            offset
            , &cl
            , cl.learnt()
            , queueByBy
        );

        if (offset2 != std::numeric_limits<ClOffset>::max()) {
            *j++ = offset2;
        }
    }
    solver->longIrredCls.resize(solver->longIrredCls.size()- (i-j));

    if (solver->conf.verbosity >= 3) {
        cout << "c [vivif] longirred"
        << " tried: "
        << runStats.checkedClauses << "/" << solver->longIrredCls.size()
        << " cl-rem:"
        << runStats.numClShorten- origClShorten
        << " lits-rem:"
        << runStats.numLitsRem - origLitRem
        << endl;
    }

    //Update stats
    runStats.timeNorm = cpuTime() - myTime;
    runStats.zeroDepthAssigns = solver->trail.size() - origTrailSize;

    return solver->ok;
}

ClOffset ClauseVivifier::testVivify(
    ClOffset offset
    , Clause* cl
    , const bool learnt
    , const uint32_t queueByBy
) {
    //Try to enqueue the literals in 'queueByBy' amounts and see if we fail
    bool failed = false;
    uint32_t done = 0;
    solver->newDecisionLevel();
    for (; done < lits.size();) {
        uint32_t i2 = 0;
        for (; (i2 < queueByBy) && ((done+i2) < lits.size()); i2++) {
            lbool val = solver->value(lits[done+i2]);
            if (val == l_Undef) {
                solver->enqueue(~lits[done+i2]);
            } else if (val == l_False) {
                //Record that there is no use for this literal
                uselessLits.push_back(lits[done+i2]);
            }
        }
        done += i2;
        extraTime += 5;
        failed = (!solver->propagate().isNULL());
        if (failed) break;
    }
    solver->cancelZeroLight();
    assert(solver->ok);

    if (uselessLits.size() > 0 || (failed && done < lits.size())) {
        //Stats
        runStats.numClShorten++;
        extraTime += 20;
        const uint32_t origSize = lits.size();

        //Remove useless literals from 'lits'
        lits.resize(done);
        for (uint32_t i2 = 0; i2 < uselessLits.size(); i2++) {
            remove(lits, uselessLits[i2]);
        }


        //Detach and free clause if it's really a clause
        if (cl) {
            solver->detachClause(*cl);
            solver->clAllocator->clauseFree(offset);
        }

        //Make new clause
        Clause *cl2 = solver->addClauseInt(lits, learnt);

        //Print results
        if (solver->conf.verbosity >= 5) {
            cout
            << "c Assym branch effective." << endl;
            if (cl)
                cout
                << "c --> orig clause:" << *cl << endl;
            else
                cout
                << "c --> orig clause: TRI/BIN" << endl;
            cout
            << "c --> orig size:" << origSize << endl
            << "c --> new size:" << (cl2 == NULL ? 0 : cl2->size()) << endl
            << "c --> removing lits from end:" << origSize - done
            << "c --> useless lits in middle:" << uselessLits.size()
            << endl;
        }

        //TODO cheating here: we don't detect a NULL return that is in fact a 2/3-long clause
        runStats.numLitsRem += origSize - (cl2 == NULL ? 0 : cl2->size());

        if (cl2 != NULL) {
            return solver->clAllocator->getOffset(cl2);
        } else {
            return std::numeric_limits<ClOffset>::max();
        }
    } else {
        return offset;
    }
}
bool ClauseVivifier::vivifyClausesCache(
    vector<ClOffset>& clauses
    , bool learnt
    , bool alsoStrengthen
) {
    assert(solver->ok);

    //Stats
    uint64_t countTime = 0;
    uint64_t maxCountTime = 700000000;
    if (solver->binTri.irredLits + solver->binTri.redLits < 300000)
        maxCountTime *= 2;
    double myTime = cpuTime();

    Stats::CacheBased tmpStats;
    tmpStats.totalCls = clauses.size();
    tmpStats.numCalled = 1;
    size_t remLitTimeStampTotal = 0;
    size_t remLitTimeStampTotalInv = 0;
    size_t subsumedStamp = 0;

    //Temps
    vector<Lit> lits;
    vector<Lit> lits2;
    vector<char> seen(solver->nVars()*2); //For strengthening
    vector<char> seen_subs(solver->nVars()*2); //For subsumption
    bool needToFinish = false;

    vector<ClOffset>::iterator i = clauses.begin();
    vector<ClOffset>::iterator j = i;
    for (vector<ClOffset>::iterator
        end = clauses.end()
        ; i != end
        ; i++
    ) {
        //Check status
        if (needToFinish) {
            *j++ = *i;
            continue;
        }
        if (countTime > maxCountTime) {
            needToFinish = true;
            tmpStats.ranOutOfTime++;
        }

        //Setup
        ClOffset offset = *i;
        Clause& cl = *solver->clAllocator->getPointer(offset);
        assert(cl.size() > 3);
        countTime += cl.size()*2;
        tmpStats.tried++;
        bool isSubsumed = false;

        //Fill 'seen'
        lits2.clear();
        for (uint32_t i2 = 0; i2 < cl.size(); i2++) {
            seen[cl[i2].toInt()] = 1;
            seen_subs[cl[i2].toInt()] = 1;
            lits2.push_back(cl[i2]);
        }

        //Go through each literal and subsume/strengthen with it
        for (const Lit
            *l = cl.begin(), *end = cl.end()
            ; l != end && !isSubsumed
            ; l++
        ) {
            const Lit lit = *l;

            //Go through the watchlist
            vec<Watched>& thisW = solver->watches[lit.toInt()];
            countTime += thisW.size();
            for(vec<Watched>::iterator
                wit = thisW.begin(), wend = thisW.end()
                ; wit != wend
                ; wit++
            ) {

                if (alsoStrengthen) {
                    //Strengthening w/ bin
                    if (wit->isBinary()
                        && seen[lit.toInt()] //We haven't yet removed it
                    ) {
                        seen[(~wit->lit1()).toInt()] = 0;
                    }

                    //Strengthening w/ tri
                    if (wit->isTri()
                        && seen[lit.toInt()] //We haven't yet removed it
                    ) {
                        if (seen[(wit->lit1()).toInt()])
                            seen[(~wit->lit2()).toInt()] = 0;
                        else if (seen[wit->lit2().toInt()])
                            seen[(~wit->lit1()).toInt()] = 0;
                    }
                }

                //Subsumption w/ bin
                if (wit->isBinary() &&
                    seen_subs[wit->lit1().toInt()]
                ) {
                    //If subsuming non-learnt with learnt, make the learnt into non-learnt
                    if (wit->learnt() && !cl.learnt()) {
                        wit->setLearnt(false);
                        findWatchedOfBin(solver->watches, wit->lit1(), lit, true).setLearnt(false);
                        solver->binTri.redBins--;
                        solver->binTri.irredBins++;
                        solver->binTri.redLits -= 2;
                        solver->binTri.irredLits += 2;
                    }
                    isSubsumed = true;
                    break;
                }

                //Extension w/ bin
                if (wit->isBinary()
                    && !wit->learnt()
                    && !seen_subs[(~(wit->lit1())).toInt()]
                ) {
                    seen_subs[(~(wit->lit1())).toInt()] = 1;
                    lits2.push_back(~(wit->lit1()));
                }

                if (wit->isTri()) {
                    assert(wit->lit1() < wit->lit2());
                }

                //Subsumption w/ tri
                if (wit->isTri()
                    && lit < wit->lit1() //Check only one instance of the TRI clause
                    && seen_subs[wit->lit1().toInt()]
                    && seen_subs[wit->lit2().toInt()]
                ) {
                    //If subsuming non-learnt with learnt, make the learnt into non-learnt
                    if (!cl.learnt() && wit->learnt()) {
                        wit->setLearnt(false);
                        findWatchedOfTri(solver->watches, wit->lit1(), lit, wit->lit2(), true).setLearnt(false);
                        findWatchedOfTri(solver->watches, wit->lit2(), lit, wit->lit1(), true).setLearnt(false);
                        solver->binTri.redTris--;
                        solver->binTri.irredTris++;
                        solver->binTri.redLits -= 3;
                        solver->binTri.irredLits += 3;
                    }
                    isSubsumed = true;
                    break;
                }

                //Extension w/ tri (1)
                if (wit->isTri()
                    && lit < wit->lit1() //Check only one instance of the TRI clause
                    && !wit->learnt()
                    && seen_subs[wit->lit1().toInt()]
                    && !seen_subs[(~(wit->lit2())).toInt()]
                ) {
                    seen_subs[(~(wit->lit2())).toInt()] = 1;
                    lits2.push_back(~(wit->lit2()));
                }

                //Extension w/ tri (2)
                if (wit->isTri()
                    && lit < wit->lit1() //Check only one instance of the TRI clause
                    && !wit->learnt()
                    && !seen_subs[(~(wit->lit1())).toInt()]
                    && seen_subs[wit->lit2().toInt()]
                ) {
                    seen_subs[(~(wit->lit1())).toInt()] = 1;
                    lits2.push_back(~(wit->lit1()));
                }

            }
        }

        assert(lits2.size() > 1);
        if (!isSubsumed
            && !learnt
            && stampBasedClRem(lits2, solver->timestamp, stampNorm, stampInv)
        ) {
            isSubsumed = true;
            subsumedStamp++;
        }

        //Clear 'seen2'
        for (vector<Lit>::const_iterator
            it2 = lits2.begin(), end2 = lits2.end()
            ; it2 != end2
            ; it2++
        ) {
            seen_subs[it2->toInt()] = 0;
        }

        //Clear 'seen' and fill new clause data
        lits.clear();
        for (vector<Lit>::const_iterator
            it2 = lits2.begin(), end2 = lits2.end()
            ; it2 != end2
            ; it2++
        ) {
            //Only fill new clause data if clause hasn't been subsumed
            if (!isSubsumed
                && seen[it2->toInt()]
            ) {
                tmpStats.numLitsRem ++;
                lits.push_back(*it2);
            }

            //Clear 'seen' and 'seen_subs'
            seen[it2->toInt()] = 0;
            seen_subs[it2->toInt()] = 0;
        }

        if (alsoStrengthen
            && lits.size() > 1
            && !isSubsumed
        ) {
            std::pair<size_t, size_t> tmp = stampBasedLitRem(lits, STAMP_RED);
            remLitTimeStampTotal += tmp.first;
            remLitTimeStampTotalInv += tmp.second;
        }

        if (alsoStrengthen
            && lits.size() > 1
            && !isSubsumed
        ) {
            std::pair<size_t, size_t> tmp = stampBasedLitRem(lits, STAMP_IRRED);
            remLitTimeStampTotal += tmp.first;
            remLitTimeStampTotalInv += tmp.second;
        }

        //If nothing to do, then move along
        if (lits.size() == cl.size() && !isSubsumed) {
            *j++ = *i;
            continue;
        }

        //Else either remove or shrink clause
        countTime += cl.size()*10;
        solver->detachClause(cl);
        if (isSubsumed) {
            tmpStats.numClSubsumed++;
            solver->clAllocator->clauseFree(offset);
        } else {
            tmpStats.shrinked++;
            Clause* c2 = solver->addClauseInt(lits, cl.learnt(), cl.stats);
            solver->clAllocator->clauseFree(offset);

            if (c2 != NULL)
                *j++ = solver->clAllocator->getOffset(c2);

            if (!solver->ok)
                needToFinish = true;
        }
    }
    clauses.resize(clauses.size() - (i-j));
    solver->checkImplicitStats();

    //Set stats
    tmpStats.cpu_time = cpuTime() - myTime;
    if (learnt) {
        runStats.redCacheBased = tmpStats;
    } else {
        runStats.irredCacheBased = tmpStats;
    }

    cout
    << "c [timestamp]"
    << " lit-rem: " << remLitTimeStampTotal
    << " inv-lit-rem: " << remLitTimeStampTotalInv
    << " stamp-rem: " << subsumedStamp
    << endl;

    return solver->ok;
}

std::pair<size_t, size_t> ClauseVivifier::stampBasedLitRem(
    vector<Lit>& lits
    , const StampType stampType
) {
    size_t remLitTimeStamp = 0;
    StampSorter sorter(solver->timestamp, stampType, true);
    std::sort(lits.begin(), lits.end(), sorter);

    #ifdef DEBUG_STAMPING
    cout << "Timestamps: ";
    for(size_t i = 0; i < lits.size(); i++) {
        cout
        << " " << solver->timestamp[lits[i].toInt()].start[stampType]
        << "," << solver->timestamp[lits[i].toInt()].end[stampType];
    }
    cout << endl;
    cout << "Ori clause: " << lits << endl;
    #endif

    assert(!lits.empty());
    Lit lastLit = lits[0];
    for(size_t i = 1; i < lits.size(); i++) {
        if (solver->timestamp[lastLit.toInt()].end[stampType]
            < solver->timestamp[lits[i].toInt()].end[stampType]
        ) {
            lits[i] = lit_Undef;
            remLitTimeStamp++;
        } else {
            lastLit = lits[i];
        }
    }

    if (remLitTimeStamp) {
        //First literal cannot be removed
        assert(lits.front() != lit_Undef);

        //At least 1 literal must remain
        assert(remLitTimeStamp < lits.size());

        vector<Lit> lits2;
        lits2.reserve(lits.size()-remLitTimeStamp);
        for(size_t i = 0; i < lits.size(); i++) {
            if (lits[i] != lit_Undef)
                lits2.push_back(lits[i]);
        }

        lits.swap(lits2);

        #ifdef DEBUG_STAMPING
        cout << "New clause: " << lits << endl;
        #endif
    }

    size_t remLitTimeStampInv = 0;
    StampSorterInv sorterInv(solver->timestamp, stampType, false);
    std::sort(lits.begin(), lits.end(), sorterInv);
    assert(!lits.empty());
    lastLit = lits[0];

    for(size_t i = 1; i < lits.size(); i++) {
        if (solver->timestamp[(~lastLit).toInt()].end[stampType]
            > solver->timestamp[(~lits[i]).toInt()].end[stampType]
        ) {
            lits[i] = lit_Undef;
            remLitTimeStampInv++;
        } else {
            lastLit = lits[i];
        }
    }

    if (remLitTimeStampInv) {
        //First literal cannot be removed
        assert(lits.front() != lit_Undef);

        //At least 1 literal must remain
        assert(remLitTimeStampInv < lits.size());

        vector<Lit> lits2;
        lits2.reserve(lits.size()-remLitTimeStampInv);
        for(size_t i = 0; i < lits.size(); i++) {
            if (lits[i] != lit_Undef)
                lits2.push_back(lits[i]);
        }

        lits.swap(lits2);

        #ifdef DEBUG_STAMPING
        cout << "New clause: " << lits << endl;
        #endif
    }


    return std::make_pair(remLitTimeStamp, remLitTimeStampInv);
}

bool ClauseVivifier::subsumeAndStrengthenImplicit()
{
    assert(solver->okay());
    const double myTime = cpuTime();
    uint64_t remBins = 0;
    uint64_t remTris = 0;
    uint64_t remLitFromBin = 0;
    uint64_t remLitFromTri = 0;
    const size_t origTrailSize = solver->trail.size();

    //For stamps
    size_t stampRem = 0;

    //For delayed enqueue and binary adding
    //Used for strengthening
    vector<Lit> bin(2);
    vector<BinaryClause> binsToAdd;
    vector<Lit> toEnqueue;

    size_t wsLit = 0;
    for (vector<vec<Watched> >::iterator
        it = solver->watches.begin(), end = solver->watches.end()
        ; it != end
        ; it++, wsLit++
    ) {
        vec<Watched>& ws = *it;
        Lit lit = Lit::toLit(wsLit);

        //We can't do much when there is nothing, or only one
        if (ws.size() < 2)
            continue;

        std::sort(ws.begin(), ws.end(), WatchSorter());
        /*cout << "---> Before" << endl;
        printWatchlist(ws, lit);*/

        Watched* i = ws.begin();
        Watched* j = i;
        Watched* lastBin = NULL;

        Lit lastLit = lit_Undef;
        Lit lastLit2 = lit_Undef;
        bool lastLearnt = false;
        for (vec<Watched>::iterator end = ws.end(); i != end; i++) {

            //Don't care about long clauses
            if (i->isClause()) {
                *j++ = *i;
                continue;
            }

            if (i->isTri()) {

                //Only treat one of the TRI's instances
                if (lit > i->lit1()) {
                    *j++ = *i;
                    continue;
                }

                //Brand new TRI
                if (lastLit != i->lit1()) {
                    lastLit2 = i->lit2();
                    lastLearnt = i->learnt();
                    *j++ = *i;
                    continue;
                }

                bool remove = false;

                //Subsumed by bin
                if (lastLit2 == lit_Undef
                    && lastLit == i->lit1()
                ) {
                    if (lastLearnt && !i->learnt()) {
                        assert(lastBin->isBinary());
                        assert(lastBin->learnt());
                        assert(lastBin->lit1() == lastLit);

                        lastBin->setLearnt(false);
                        findWatchedOfBin(solver->watches, lastLit, lit, true).setLearnt(false);
                        solver->binTri.redLits -= 2;
                        solver->binTri.irredLits += 2;
                        solver->binTri.redBins--;
                        solver->binTri.irredBins++;
                        lastLearnt = false;
                    }

                    remove = true;
                }

                //Subsumed by Tri
                if (lastLit == i->lit1() && lastLit2 == i->lit2()) {
                    //The sorting algorithm prefers non-learnt to learnt, so it is
                    //impossible to have non-learnt before learnt
                    assert(!(i->learnt() == false && lastLearnt == true));

                    remove = true;
                }

                if (remove) {
                    //Remove Tri
                    removeTri(lit, i->lit1(), i->lit2(), i->learnt());
                    remTris++;
                    continue;
                }

                //Don't remove
                lastLit = i->lit1();
                lastLit2 = i->lit2();
                lastLearnt = i->learnt();
                *j++ = *i;
                continue;
            }

            //Binary from here on
            assert(i->isBinary());

            //Subsume bin with bin
            if (i->lit1() == lastLit && lastLit2 == lit_Undef) {
                //The sorting algorithm prefers non-learnt to learnt, so it is
                //impossible to have non-learnt before learnt
                assert(!(i->learnt() == false && lastLearnt == true));

                remBins++;
                assert(i->lit1().var() != lit.var());
                removeWBin(solver->watches, i->lit1(), lit, i->learnt());
                if (i->learnt()) {
                    solver->binTri.redLits -= 2;
                    solver->binTri.redBins--;
                } else {
                    solver->binTri.irredLits -= 2;
                    solver->binTri.irredBins--;
                }
                continue;
            } else {
                lastBin = j;
                lastLit = i->lit1();
                lastLit2 = lit_Undef;
                lastLearnt = i->learnt();
                *j++ = *i;
            }
        }
        ws.shrink(i-j);

        /*cout << "---> After" << endl;
        printWatchlist(ws, lit);*/


        //Now strengthen
        i = ws.begin();
        j = i;
        for (vec<Watched>::iterator end = ws.end(); i != end; i++) {
            //Can't do much with clause, will treat them during vivification
            if (i->isClause()) {
                *j++ = *i;
                continue;
            }

            //Strengthen bin with bin -- effectively setting literal
            if (i->isBinary()) {
                lits.clear();
                lits.push_back(lit);
                lits.push_back(i->lit1());
                std::pair<size_t, size_t> tmp = stampBasedLitRem(lits, STAMP_RED);
                stampRem += tmp.first;
                stampRem += tmp.second;
                assert(!lits.empty());
                if (lits.size() == 1) {
                    toEnqueue.push_back(lits[0]);
                    remLitFromBin++;
                    stampRem++;
                    *j++ = *i;
                    continue;
                }

                //If inverted, then the inverse will never be found, because
                //watches are sorted
                if (i->lit1().sign()) {
                    *j++ = *i;
                    continue;
                }

                //Try to look for a binary in this same watchlist
                //that has ~i->lit1() inside. Everything is sorted, so we are
                //lucky, this is speedy
                bool rem = false;
                vec<Watched>::const_iterator i2 = i;
                while(i2 != end
                    && (i2->isBinary() || i2->isTri())
                    && i2->lit1().var() == i2->lit1().var()
                ) {
                    //Yay, we have found what we needed!
                    if (i2->isBinary() && i2->lit1() == ~i->lit1()) {
                        rem = true;
                        break;
                    }

                    i2++;
                }

                //Enqeue literal
                if (rem) {
                    remLitFromBin++;
                    toEnqueue.push_back(lit);
                }
                *j++ = *i;
                continue;
            }

            //Strengthen tri with bin
            if (i->isTri()) {
                const Lit lit1 = i->lit1();
                const Lit lit2 = i->lit2();
                bool rem = false;

                for(vec<Watched>::const_iterator
                    it2 = solver->watches[(~lit).toInt()].begin(), end2 = solver->watches[(~lit).toInt()].end()
                    ; it2 != end2
                    ; it2++
                ) {
                    if (it2->isBinary()
                        && (it2->lit1() == lit1 || it2->lit1() == lit2)
                    ) {
                        rem = true;
                        break;
                    }
                }

                if (rem) {
                    removeTri(lit, i->lit1(), i->lit2(), i->learnt());
                    remLitFromTri++;
                    binsToAdd.push_back(BinaryClause(i->lit1(), i->lit2(), i->learnt()));
                    continue;
                } else {
                    //Strengthen TRI using stamps
                    lits.clear();
                    lits.push_back(lit);
                    lits.push_back(i->lit1());
                    lits.push_back(i->lit2());

                    //Try both stamp types to reduce size
                    std::pair<size_t, size_t> tmp = stampBasedLitRem(lits, STAMP_RED);
                    stampRem += tmp.first;
                    stampRem += tmp.second;
                    if (lits.size() > 1) {
                        std::pair<size_t, size_t> tmp = stampBasedLitRem(lits, STAMP_IRRED);
                        stampRem += tmp.first;
                        stampRem += tmp.second;
                    }

                    if (lits.size() == 2) {
                        removeTri(lit, i->lit1(), i->lit2(), i->learnt());
                        remLitFromTri++;
                        binsToAdd.push_back(BinaryClause(lits[0], lits[1], i->learnt()));
                        continue;
                    } else if (lits.size() == 1) {
                        removeTri(lit, i->lit1(), i->lit2(), i->learnt());
                        remLitFromTri+=2;
                        toEnqueue.push_back(lits[0]);
                        continue;
                    }
                }


                //Nothing to do, copy
                *j++ = *i;
                continue;
            }

            //Only bin, tri and clause in watchlist
            assert(false);
        }
        ws.shrink(i-j);
    }

    //Enqueue delayed values
    for(vector<Lit>::const_iterator
        it = toEnqueue.begin(), end = toEnqueue.end()
        ; it != end
        ; it++
    ) {
        if (solver->value(*it) == l_False) {
            solver->ok = false;
            goto end;
        }

        if (solver->value(*it) == l_Undef)
            solver->enqueue(*it);
    }
    solver->ok = solver->propagate().isNULL();
    if (!solver->okay())
        goto end;

    //Add delayed binary clauses
    for(vector<BinaryClause>::const_iterator
        it = binsToAdd.begin(), end = binsToAdd.end()
        ; it != end
        ; it++
    ) {
        bin[0] = it->getLit1();
        bin[1] = it->getLit2();
        solver->addClauseInt(bin, it->getLearnt());
        if (!solver->okay())
            goto end;
    }

end:
    if (solver->conf.verbosity >= 1) {
        cout
        << "c [implicit]"
        << " rem-bin " << remBins
        << " rem-tri " << remTris
        << " rem-litBin: " << remLitFromBin
        << " rem-litTri: " << remLitFromTri
        << " stamp:" << stampRem
        << " set-var: " << solver->trail.size() - origTrailSize

        << " time: " << std::fixed << std::setprecision(2) << std::setw(5)
        << (cpuTime() - myTime)
        << " s" << endl;
    }
    solver->checkStats();

    //Update stats
    solver->solveStats.subsBinWithBinTime += cpuTime() - myTime;
    solver->solveStats.subsBinWithBin += remBins;

    return solver->okay();
}

void ClauseVivifier::removeTri(
    const Lit lit1
    ,const Lit lit2
    ,const Lit lit3
    ,const bool learnt
) {
    //Remove tri
    Lit lits[3];
    lits[0] = lit1;
    lits[1] = lit2;
    lits[2] = lit3;
    std::sort(lits, lits+3);
    removeTriAllButOne(solver->watches, lit1, lits, learnt);

    //Update stats for tri
    if (learnt) {
        solver->binTri.redLits -= 3;
        solver->binTri.redTris--;
    } else {
        solver->binTri.irredLits -= 3;
        solver->binTri.irredTris--;
    }
}
