/**************************************************************************
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
*****************************************************************************/

#include "ClauseVivifier.h"
#include "ClauseCleaner.h"
#include "time_mem.h"
#include <iomanip>

//#define ASSYM_DEBUG

#define MAX_BINARY_PROP 60000000


ClauseVivifier::ClauseVivifier(Solver& _solver) :
    lastTimeWentUntil(0)
    , numCalls(0)
    , solver(_solver)
{}

const bool ClauseVivifier::vivify()
{
    assert(solver.ok);
    #ifdef VERBOSE_DEBUG
    std::cout << "c clauseVivifier started" << std::endl;
    //solver.printAllClauses();
    #endif //VERBOSE_DEBUG

    solver.clauseCleaner->cleanClauses(solver.clauses, ClauseCleaner::clauses);
    numCalls++;

    if (numCalls >= 2) {
        if (solver.conf.doCacheOTFSSR) {
            if (!vivifyClausesCache(solver.clauses)) return false;
            if (!vivifyClausesCache(solver.learnts)) return false;
        }
    }

    if (!vivifyClausesNormal()) return false;

    return true;
}

const bool ClauseVivifier::calcAndSubsume()
{
    assert(solver.ok);

    if (!subsWNonExistBinsFill()) return false;
    subsumeNonExist();

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

const bool ClauseVivifier::subsNonExistHelper(Clause& cl)
{
    for (const Lit *l = cl.getData(), *end = cl.getDataEnd(); l != end; l++) {
        solver.seen[l->toInt()] = true;
    }

    bool toRemove = false;
    for (const Lit *l = cl.getData(), *end = cl.getDataEnd(); l != end; l++) {
        const vector<LitExtra>& cache = solver.transOTFCache[l->toInt()].lits;
        for (vector<LitExtra>::const_iterator cacheLit = cache.begin(), endCache = cache.end(); cacheLit != endCache; cacheLit++) {
            if (cacheLit->getOnlyNLBin() && solver.seen[cacheLit->getLit().toInt()]) {
                toRemove = true;
                break;
            }
        }
        if (toRemove) break;
    }

    for (const Lit *l = cl.getData(), *end = cl.getDataEnd(); l != end; l++) {
        solver.seen[l->toInt()] = false;
    }

    return toRemove;
}

void ClauseVivifier::subsumeNonExist()
{
    double myTime = cpuTime();
    uint32_t clRem = 0;
    Clause **i, **j;
    i = j = solver.clauses.getData();
    for (Clause **end = solver.clauses.getDataEnd(); i != end; i++) {
        if (subsNonExistHelper(**i)) {

            solver.removeClause(**i);
            clRem ++;
        } else {
            *j++ = *i;
        }
    }
    solver.clauses.shrink(i-j);

    i = j = solver.learnts.getData();
    for (Clause **end = solver.learnts.getDataEnd(); i != end; i++) {
        if (subsNonExistHelper(**i)) {

            solver.removeClause(**i);
            clRem ++;
        } else {
            *j++ = *i;
        }
    }
    solver.learnts.shrink(i-j);

    if (solver.conf.verbosity  >= 1) {
        std::cout << "c Subs w/ non-existent bins: " << std::setw(6) << clRem
        << " time: " << std::fixed << std::setprecision(2) << std::setw(5) << (cpuTime() - myTime) << " s"
        << std::endl;
    }
}

/**
@brief Call subsWNonExistBins with randomly picked starting literals

This is the function that overviews the deletion of all clauses that could be
inferred from non-existing binary clauses, and the strenghtening (through self-
subsuming resolution) of clauses that could be strenghtened using non-existent
binary clauses.
*/
const bool ClauseVivifier::subsWNonExistBinsFill()
{
    double myTime = cpuTime();
    for (vec<Watched> *it = solver.watches.getData(), *end = solver.watches.getDataEnd(); it != end; it++) {
        if (it->size() < 2) continue;
        std::sort(it->getData(), it->getDataEnd(), BinSorter2());
    }

    uint32_t oldTrailSize = solver.trail.size();
    uint64_t oldProps = solver.propagations;
    uint64_t maxProp = MAX_BINARY_PROP*3;

    uint32_t extraTimeNonExist = 0;
    uint32_t doneNumNonExist = 0;
    uint32_t startFrom = solver.mtrand.randInt(solver.order_heap.size());
    for (uint32_t i = 0; i < solver.order_heap.size(); i++) {
        Var var = solver.order_heap[(startFrom + i) % solver.order_heap.size()];
        if (solver.propagations + extraTimeNonExist*150 > oldProps + maxProp) break;
        if (solver.assigns[var] != l_Undef || !solver.decision_var[var]) continue;
        doneNumNonExist++;
        extraTimeNonExist += 5;

        Lit lit(var, true);
        if (!subsWNonExistBinsFillHelper(lit)) {
            if (!solver.ok) return false;
            solver.cancelUntilLight();
            solver.uncheckedEnqueue(~lit);
            solver.ok = solver.propagate<true>().isNULL();
            if (!solver.ok) return false;
            continue;
        }
        extraTimeNonExist += 10;

        //in the meantime it could have got assigned
        if (solver.assigns[var] != l_Undef) continue;
        lit = ~lit;
        if (!subsWNonExistBinsFillHelper(lit)) {
            if (!solver.ok) return false;
            solver.cancelUntilLight();
            solver.uncheckedEnqueue(~lit);
            solver.ok = solver.propagate<true>().isNULL();
            if (!solver.ok) return false;
            continue;
        }
        extraTimeNonExist += 10;
    }

    if (solver.conf.verbosity  >= 1) {
        std::cout << "c Calc non-exist non-lernt bins"
        << " v-fix: " << std::setw(5) << solver.trail.size() - oldTrailSize
        << " done: " << std::setw(6) << doneNumNonExist
        << " time: " << std::fixed << std::setprecision(2) << std::setw(5) << (cpuTime() - myTime) << " s"
        << std::endl;
    }

    return true;
}

/**
@brief Subsumes&strenghtens clauses with non-existent binary clauses

Generates binary clauses that could exist, then calls \function subsume0BIN()
with them, thus performing self-subsuming resolution and subsumption on the
clauses.

@param[in] lit This literal is the starting point of this set of non-existent
binary clauses (this literal is the starting point in the binary graph)
*/
const bool ClauseVivifier::subsWNonExistBinsFillHelper(const Lit lit)
{
    #ifdef VERBOSE_DEBUG
    std::cout << "subsWNonExistBins called with lit " << lit << std::endl;
    #endif //VERBOSE_DEBUG
    solver.newDecisionLevel();
    solver.uncheckedEnqueueLight(lit);
    bool failed = (!solver.propagateNonLearntBin().isNULL());
    if (failed) return false;

    vector<Lit> lits;
    assert(solver.decisionLevel() > 0);
    for (int sublevel = solver.trail.size()-1; sublevel > (int)solver.trail_lim[0]; sublevel--) {
        Lit x = solver.trail[sublevel];
        lits.push_back(x);
        solver.assigns[x.var()] = l_Undef;
    }
    solver.assigns[solver.trail[solver.trail_lim[0]].var()] = l_Undef;
    solver.qhead = solver.trail_lim[0];
    solver.trail.shrink_(solver.trail.size() - solver.trail_lim[0]);
    solver.trail_lim.shrink_(solver.trail_lim.size());
    //solver.cancelUntilLight();

    solver.transOTFCache[(~lit).toInt()].merge(lits, true, solver.seen);
    solver.transOTFCache[(~lit).toInt()].conflictLastUpdated = solver.conflicts;
    return solver.ok;
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
    assert(solver.ok);

    bool failed;
    uint32_t effective = 0;
    uint32_t effectiveLit = 0;
    double myTime = cpuTime();
    uint64_t maxNumProps = 20*1000*1000;
    if (solver.clauses_literals + solver.learnts_literals < 500000)
        maxNumProps *=2;
    uint64_t extraDiff = 0;
    uint64_t oldProps = solver.propagations;
    bool needToFinish = false;
    uint32_t checkedClauses = 0;
    uint32_t potentialClauses = solver.clauses.size();
    if (lastTimeWentUntil + 500 > solver.clauses.size())
        lastTimeWentUntil = 0;
    uint32_t thisTimeWentUntil = 0;
    vec<Lit> lits;
    vec<Lit> unused;

    if (solver.clauses.size() < 1000000) {
        //if too many clauses, random order will do perfectly well
        std::sort(solver.clauses.getData(), solver.clauses.getDataEnd(), sortBySize());
    }

    uint32_t queueByBy = 2;
    if (numCalls > 8
        && (solver.clauses_literals + solver.learnts_literals < 4000000)
        && (solver.clauses.size() < 50000))
        queueByBy = 1;

    Clause **i, **j;
    i = j = solver.clauses.getData();
    for (Clause **end = solver.clauses.getDataEnd(); i != end; i++) {
        if (needToFinish || lastTimeWentUntil > 0) {
            if (!needToFinish) {
                lastTimeWentUntil--;
                thisTimeWentUntil++;
            }
            *j++ = *i;
            continue;
        }

        //if done enough, stop doing it
        if (solver.propagations-oldProps + extraDiff > maxNumProps) {
            //std::cout << "Need to finish -- ran out of prop" << std::endl;
            needToFinish = true;
        }

        //if bad performance, stop doing it
        /*if ((i-solver.clauses.getData() > 5000 && effectiveLit < 300)) {
            std::cout << "Need to finish -- not effective" << std::endl;
            needToFinish = true;
        }*/

        Clause& c = **i;
        extraDiff += c.size();
        checkedClauses++;
        thisTimeWentUntil++;

        assert(c.size() > 2);
        assert(!c.learnt());

        unused.clear();
        lits.clear();
        lits.growTo(c.size());
        memcpy(lits.getData(), c.getData(), c.size() * sizeof(Lit));

        failed = false;
        uint32_t done = 0;
        solver.newDecisionLevel();
        for (; done < lits.size();) {
            uint32_t i2 = 0;
            for (; (i2 < queueByBy) && ((done+i2) < lits.size()); i2++) {
                lbool val = solver.value(lits[done+i2]);
                if (val == l_Undef) {
                    solver.uncheckedEnqueueLight(~lits[done+i2]);
                } else if (val == l_False) {
                    unused.push(lits[done+i2]);
                }
            }
            done += i2;
            failed = (!solver.propagate<false>(false).isNULL());
            if (failed) break;
        }
        solver.cancelUntilLight();
        assert(solver.ok);

        if (unused.size() > 0 || (failed && done < lits.size())) {
            effective++;
            uint32_t origSize = lits.size();
            #ifdef ASSYM_DEBUG
            std::cout << "Assym branch effective." << std::endl;
            std::cout << "-- Orig clause:"; c.plainPrint();
            #endif
            solver.detachClause(c);

            lits.shrink(lits.size() - done);
            for (uint32_t i2 = 0; i2 < unused.size(); i2++) {
                remove(lits, unused[i2]);
            }

            Clause *c2 = solver.addClauseInt(lits, c.getGroup());
            #ifdef ASSYM_DEBUG
            std::cout << "-- Origsize:" << origSize << " newSize:" << (c2 == NULL ? 0 : c2->size()) << " toRemove:" << c.size() - done << " unused.size():" << unused.size() << std::endl;
            #endif
            extraDiff += 20;
            //TODO cheating here: we don't detect a NULL return that is in fact a 2-long clause
            effectiveLit += origSize - (c2 == NULL ? 0 : c2->size());
            solver.clauseAllocator.clauseFree(&c);

            if (c2 != NULL) {
                #ifdef ASSYM_DEBUG
                std::cout << "-- New clause:"; c2->plainPrint();
                #endif
                *j++ = c2;
            }

            if (!solver.ok) needToFinish = true;
        } else {
            *j++ = *i;
        }
    }
    solver.clauses.shrink(i-j);

    lastTimeWentUntil = thisTimeWentUntil;

    if (solver.conf.verbosity  >= 1) {
        std::cout << "c asymm "
        << " cl-useful: " << effective << "/" << checkedClauses << "/" << potentialClauses
        << " lits-rem:" << effectiveLit
        << " time: " << cpuTime() - myTime
        << std::endl;
    }

    return solver.ok;
}


const bool ClauseVivifier::vivifyClausesCache(vec<Clause*>& clauses)
{
    assert(solver.ok);

    vec<char> seen;
    seen.growTo(solver.nVars()*2, 0);
    uint32_t litsRem = 0;
    uint32_t clShrinked = 0;
    uint64_t countTime = 0;
    uint64_t maxCountTime = 500000000;
    if (solver.clauses_literals + solver.learnts_literals < 500000)
        maxCountTime *= 2;
    if (numCalls >= 5) maxCountTime*= 3;
    uint32_t clTried = 0;
    vec<Lit> lits;
    bool needToFinish = false;
    double myTime = cpuTime();
    const vector<TransCache>& cache = solver.transOTFCache;

    Clause** i = clauses.getData();
    Clause** j = i;
    for (Clause** end = clauses.getDataEnd(); i != end; i++) {
        if (needToFinish) {
            *j++ = *i;
            continue;
        }
        if (countTime > maxCountTime) needToFinish = true;

        Clause& cl = **i;
        countTime += cl.size()*2;
        clTried++;

        for (uint32_t i2 = 0; i2 < cl.size(); i2++) seen[cl[i2].toInt()] = 1;
        for (const Lit *l = cl.getData(), *end = cl.getDataEnd(); l != end; l++) {
            if (seen[l->toInt()] == 0) continue;
            Lit lit = *l;

            countTime += cache[l->toInt()].lits.size();
            for (vector<LitExtra>::const_iterator it2 = cache[l->toInt()].lits.begin()
                , end2 = cache[l->toInt()].lits.end(); it2 != end2; it2++) {
                seen[(~(it2->getLit())).toInt()] = 0;
            }
        }

        lits.clear();
        for (const Lit *it2 = cl.getData(), *end2 = cl.getDataEnd(); it2 != end2; it2++) {
            if (seen[it2->toInt()]) lits.push(*it2);
            else litsRem++;
            seen[it2->toInt()] = 0;
        }
        if (lits.size() < cl.size()) {
            countTime += cl.size()*10;
            solver.detachClause(cl);
            clShrinked++;
            Clause* c2 = solver.addClauseInt(lits, cl.getGroup(), cl.learnt(), cl.getGlue());
            solver.clauseAllocator.clauseFree(&cl);

            if (c2 != NULL) *j++ = c2;
            if (!solver.ok) needToFinish = true;
        } else {
            *j++ = *i;
        }
    }

    clauses.shrink(i-j);

    if (solver.conf.verbosity >= 1) {
        std::cout << "c vivif2 -- "
        << " cl tried " << std::setw(8) << clTried
        << " cl shrink " << std::setw(8) << clShrinked
        << " lits rem " << std::setw(10) << litsRem
        << " time: " << cpuTime() - myTime
        << std::endl;
    }

    return solver.ok;
}
