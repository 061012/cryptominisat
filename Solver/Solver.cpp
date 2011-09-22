/*****************************************************************************
MiniSat -- Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
glucose -- Gilles Audemard, Laurent Simon (2008)
CryptoMiniSat -- Copyright (c) 2009 Mate Soos

Original code by MiniSat and glucose authors are under an MIT licence.
Modifications for CryptoMiniSat are under GPLv3 licence.
******************************************************************************/

#include "Solver.h"
#include <cmath>
#include <string.h>
#include <algorithm>
#include <limits.h>
#include <vector>
#include <iomanip>
#include <algorithm>

#include "ThreadControl.h"
#include "ClauseAllocator.h"
#include "Clause.h"
#include "time_mem.h"

//#define DEBUG_ENQUEUE_LEVEL0
//#define VERBOSE_DEBUG_POLARITIES
//#define DEBUG_DYNAMIC_RESTART
//#define DELAYED_NORM_WATCH_UPDATE

/**
@brief Sets a sane default config and allocates handler classes
*/
Solver::Solver(ClauseAllocator *_clAllocator, const AgilityData& agilityData) :
        // Stats
        propagations(0)
        , bogoProps(0)

        , clAllocator(_clAllocator)
        , ok(true)
        , qhead(0)
        , agility(agilityData)
{
}

/**
@brief Creates a new SAT variable in the solver

This entails making the datastructures large enough to fit the new variable
in all internal datastructures as well as all datastructures used in
classes used inside Solver

@p dvar The new variable should be used as a decision variable?
   NOTE: this has effects on the meaning of a SATISFIABLE result
*/
const Var Solver::newVar(const bool dvar)
{
    const Var v = nVars();
    if (v >= 1<<30) {
        std::cout << "ERROR! Variable requested is far too large" << std::endl;
        exit(-1);
    }

    watches.resize(watches.size() + 2);  // (list for positive&negative literals)
    assigns.push_back(l_Undef);
    varData.push_back(VarData());
    propData.resize(nVars());

    //Temporaries
    seen      .push_back(0);
    seen      .push_back(0);
    seen2     .push_back(0);
    seen2     .push_back(0);

    return v;
}

void Solver::attachBinClause(const Lit lit1, const Lit lit2, const bool learnt, const bool checkUnassignedFirst)
{
    #ifdef DEBUG_ATTACH
    assert(lit1.var() != lit2.var());
    if (checkUnassignedFirst) {
        assert(value(lit1.var()) == l_Undef);
        assert(value(lit2) == l_Undef || value(lit2) == l_False);
    }

    assert(varData[lit1.var()].elimed == ELIMED_NONE
            || varData[lit1.var()].elimed == ELIMED_QUEUED_VARREPLACER);
    assert(varData[lit2.var()].elimed == ELIMED_NONE
            || varData[lit2.var()].elimed == ELIMED_QUEUED_VARREPLACER);
    #endif //DEBUG_ATTACH

    vec<Watched>& ws1 = watches[(~lit1).toInt()];
    ws1.push(Watched(lit2, learnt));
    vec<Watched>::iterator it = ws1.begin();
    for (vec<Watched>::iterator end = ws1.end(); it != end; it++)
        if (!it->isBinary()) break;
    if (it != ws1.end()) std::swap(ws1.back(), *it);

    vec<Watched>& ws2 = watches[(~lit2).toInt()];
    ws2.push(Watched(lit1, learnt));
    it = ws2.begin();
    for (vec<Watched>::iterator end = ws2.end(); it != end; it++)
        if (!it->isBinary()) break;
    if (it != ws2.end()) std::swap(ws2.back(), *it);
}

/**
 @ *brief Attach normal a clause to the watchlists

 Handles 2, 3 and >3 clause sizes differently and specially
 */

void Solver::attachClause(const Clause& c, const uint16_t point1, const uint16_t point2, const bool checkAttach)
{
    assert(c.size() > 2);
    assert(c[point1].var() != c[point2].var());
    if (checkAttach) {
        assert(value(c[point1].var()) == l_Undef);
        assert(value(c[point2]) == l_Undef || value(c[point2]) == l_False);
    }

    #ifdef DEBUG_ATTACH
    for (uint32_t i = 0; i < c.size(); i++) {
        assert(varData[c[i].var()].elimed == ELIMED_NONE
                || varData[c[i].var()].elimed == ELIMED_QUEUED_VARREPLACER);
    }
    #endif //DEBUG_ATTACH

    //Tri-clauses are attached specially
    if (c.size() == 3) {
        watches[(~c[0]).toInt()].push(Watched(c[1], c[2]));
        watches[(~c[1]).toInt()].push(Watched(c[0], c[2]));
        watches[(~c[2]).toInt()].push(Watched(c[0], c[1]));
    } else {
        const ClauseOffset offset = clAllocator->getOffset(&c);

        //blocked literal is the lit in the middle (c.size()/2). For no reason.
        watches[(~c[point1]).toInt()].push(Watched(offset, c[c.size()/2], 0));
        watches[(~c[point2]).toInt()].push(Watched(offset, c[c.size()/2], 1));
    }

    //For compatibility reasons, even TRI-clauses have a clauseData entry
    //TODO: get rid of this uselessness. TRI-clauses don't need an  entry in clauseData!
    if (c.size() != 3) {
        const uint32_t num = c.getNum();
        if (clauseData.size() <= num)
            clauseData.resize(num + 1);

        clauseData[num] = ClauseData(point1, point2);
    }
}

/**
@brief Calls detachModifiedClause to do the heavy-lifting
*/
void Solver::detachClause(const Clause& c)
{
    if (c.size() > 3) {
        const ClauseData& data =clauseData[c.getNum()];
        detachModifiedClause(c[data[0]], c[data[1]], (c.size() == 3) ? c[2] : lit_Undef,  c.size(), &c);
    } else {
        detachModifiedClause(c[0], c[1], c[2], c.size(), &c);
    }
}

/**
@brief Detaches a (potentially) modified clause

The first two literals might have chaned through modification, so they are
passed along as arguments -- they are needed to find the correct place where
the clause is
*/
void Solver::detachModifiedClause(const Lit lit1, const Lit lit2, const Lit lit3, const uint32_t origSize, const Clause* address)
{
    assert(origSize > 2);

    ClauseOffset offset = clAllocator->getOffset(address);
    if (origSize == 3
        //The clause might have been longer, and has only recently
        //became 3-long. Check!
        && !findWCl(watches[(~lit1).toInt()], offset)
    ) {
        removeWTri(watches[(~lit1).toInt()], lit2, lit3);
        removeWTri(watches[(~lit2).toInt()], lit1, lit3);
        removeWTri(watches[(~lit3).toInt()], lit1, lit2);
    } else {
        removeWCl(watches[(~lit1).toInt()], offset);
        removeWCl(watches[(~lit2).toInt()], offset);
    }
}

/**
@brief Propagates a binary clause

Need to be somewhat tricky if the clause indicates that current assignement
is incorrect (i.e. both literals evaluate to FALSE). If conflict if found,
sets failBinLit
*/
inline const bool Solver::propBinaryClause(const vec<Watched>::const_iterator i, const Lit p, PropBy& confl)
{
    lbool val = value(i->getOtherLit());
    if (val.isUndef()) {
        enqueue(i->getOtherLit(), PropBy(~p));
    } else if (val == l_False) {
        confl = PropBy(~p);
        failBinLit = i->getOtherLit();
        qhead = trail.size();
        return false;
    }

    return true;
}


/**
@brief Propagates a normal (n-long where n > 3) clause

We have blocked literals in this case in the watchlist. That must be checked
and updated.
*/
template<bool simple> inline const bool Solver::propNormalClause(vec<Watched>::iterator &i, vec<Watched>::iterator &j, const Lit p, PropBy& confl)
{
    if (value(i->getBlockedLit()).getBool()) {
        // Clause is sat
        return true;
    }
    bogoProps += 4;
    const uint32_t offset = i->getNormOffset();
    Clause& c = *clAllocator->getPointer(offset);
    const uint32_t clauseNum = c.getNum();
    ClauseData& data = clauseData[clauseNum];
    const bool watchNum = i->getWatchNum();
    assert(c[data[watchNum]] == ~p);

    // If other watch is true, then clause is already satisfied.
    if (value(c[data[!watchNum]]) == l_True) {
        return true;
    }
    // Look for new watch:

#ifdef DELAYED_NORM_WATCH_UPDATE
    uint16_t other = std::numeric_limits<uint16_t>::max();
#endif
    for (uint16_t numLit = 0, size = c.size(); numLit < size; numLit++) {
        if (numLit == data[0] || numLit == data[1]) continue;
        if (value(c[numLit]) == l_True) {
            data[watchNum] = numLit;
            watches[(~c[numLit]).toInt()].push(Watched(offset, c[data[!watchNum]], watchNum));
            bogoProps += 2;
            j--;
            *i = *j;
            i--;
            return true;
        }
#ifdef DELAYED_NORM_WATCH_UPDATE
        if (value(c[numLit]) == l_Undef && other == std::numeric_limits<uint16_t>::max()) other = numLit;
#else
        if (value(c[numLit]) == l_Undef) {
            data[watchNum] = numLit;
            watches[(~c[numLit]).toInt()].push(Watched(offset, c[data[!watchNum]], watchNum));
            bogoProps += 2;
            j--;
            *i = *j;
            i--;
            return true;
        }
#endif
    }
#ifdef DELAYED_NORM_WATCH_UPDATE
    if (other != std::numeric_limits<uint16_t>::max()) {
        data[watchNum] = other;
        watches[(~c[other]).toInt()].push(Watched(offset, c[data[!watchNum]], watchNum));
        bogoProps += 2;
        j--;
        *i = *j;
        i--;
        return true;
    }
#endif

    // Did not find watch -- clause is unit under assignment:
    if (value(c[data[!watchNum]]) == l_False) {
        confl = PropBy(offset, !watchNum);
        qhead = trail.size();
        return false;
    } else {
        if (simple) enqueue(c[data[!watchNum]], PropBy(offset, !watchNum));
        else        addHyperBin(c, c[data[!watchNum]]);
    }

    return true;
}

/**
@brief Propagates a tertiary (3-long) clause

Need to be somewhat tricky if the clause indicates that current assignement
is incorrect (i.e. all 3 literals evaluate to FALSE). If conflict is found,
sets failBinLit
*/
template<bool simple> inline const bool Solver::propTriClause(const vec<Watched>::const_iterator i, const Lit p, PropBy& confl)
{
    lbool val = value(i->getOtherLit());
    if (val == l_True) return true;

    lbool val2 = value(i->getOtherLit2());
    if (val.isUndef() && val2 == l_False) {
        if (simple) enqueue(i->getOtherLit(), PropBy(~p, i->getOtherLit2()));
        else        addHyperBin(~p, i->getOtherLit2(), i->getOtherLit());
    } else if (val == l_False && val2.isUndef()) {
        if (simple) enqueue(i->getOtherLit2(), PropBy(~p, i->getOtherLit()));
        else        addHyperBin(~p, i->getOtherLit(), i->getOtherLit2());
    } else if (val == l_False && val2 == l_False) {
        confl = PropBy(~p, i->getOtherLit2());
        failBinLit = i->getOtherLit();
        qhead = trail.size();
        return false;
    }

    return true;
}

const PropBy Solver::propagate(const bool update)
{
    PropBy confl;

    #ifdef VERBOSE_DEBUG_PROP
    std::cout << "Propagation started" << std::endl;
    #endif

    while (qhead < trail.size() && confl.isNULL()) {
        Lit p = trail[qhead++];     // 'p' is enqueued fact to propagate.
        vec<Watched>& ws = watches[p.toInt()];

        #ifdef VERBOSE_DEBUG_PROP
        std::cout << "Propagating lit " << p << std::endl;
        std::cout << "ws origSize: "<< ws.size() << std::endl;
        #endif

        vec<Watched>::iterator i = ws.begin();
        vec<Watched>::iterator j = ws.end();
        bogoProps += ws.size()/2 + 1;
        for (; i != j; i++) {
            if (i->isBinary()) {
                if (!propBinaryClause(i, p, confl)) break;
                else continue;
            } //end BINARY

            if (i->isTriClause()) {
                if (!propTriClause<true>(i, p, confl)) break;
                else continue;
            } //end TRICLAUSE

            if (i->isClause()) {
                if (!propNormalClause<true>(i, j, p, confl)) break;
                else continue;
            } //end CLAUSE
        }
        ws.shrink_(ws.end()-j);
    }

    #ifdef VERBOSE_DEBUG
    std::cout << "Propagation ended." << std::endl;
    #endif

    return confl;
}

const PropBy Solver::propagateNonLearntBin()
{
    PropBy confl;
    while (qhead < trail.size()) {
        Lit p = trail[qhead++];
        vec<Watched> & ws = watches[p.toInt()];
        for(vec<Watched>::iterator k = ws.begin(), end = ws.end(); k != end; k++) {
            if (!k->isBinary() || k->getLearnt()) continue;

            if (!propBinaryClause(k, p, confl)) return confl;
        }
    }

    return PropBy();
}

const PropBy Solver::propagateFull(std::set<BinaryClause>& uselessBin)
{
    #ifdef VERBOSE_DEBUG_FULLPROP
    std::cout << "Prop full started" << std::endl;
    #endif

    PropBy ret;

    //Assert startup: only 1 enqueued, uselessBin is empty
    assert(uselessBin.empty());
    assert(decisionLevel() == 1);
    assert(trail.size() - trail_lim.back() == 1);

    //Set up root node
    Lit root = trail[qhead];
    propData[root.var()].ancestor = lit_Undef;
    propData[root.var()].learntStep = false;
    propData[root.var()].hyperBin = false;
    propData[root.var()].hyperBinNotAdded = false;

    uint32_t nlBinQHead = qhead;
    uint32_t lBinQHead = qhead;

    needToAddBinClause.clear();;
    start:

    //Propagate binary non-learnt
    while (nlBinQHead < trail.size()) {
        Lit p = trail[nlBinQHead++];
        vec<Watched> & ws = watches[p.toInt()];
        bogoProps += ws.size()/2;
        for(vec<Watched>::iterator k = ws.begin(), end = ws.end(); k != end; k++) {
            if (!k->isBinary() || k->getLearnt()) continue;

            ret = propBin(p, k, uselessBin);
            if (ret != PropBy()) return ret;
        }
    }

    //Propagate binary learnt
    while (lBinQHead < trail.size()) {
        Lit p = trail[lBinQHead];
        vec<Watched> & ws = watches[p.toInt()];
        bogoProps += ws.size()/2;
        enqeuedSomething = false;

        for(vec<Watched>::iterator k = ws.begin(), end = ws.end(); k != end; k++) {
            if (!k->isBinary() || !k->getLearnt()) continue;

            ret = propBin(p, k, uselessBin);
            if (ret != PropBy()) return ret;
            if (enqeuedSomething) goto start;
        }
        lBinQHead++;
    }

    while (qhead < trail.size()) {
        PropBy confl;
        Lit p = trail[qhead];
        vec<Watched> & ws = watches[p.toInt()];
        bogoProps += ws.size()/2;
        enqeuedSomething = false;

        vec<Watched>::iterator i = ws.begin();
        vec<Watched>::iterator j = ws.end();
        for(; i != j; i++) {
            if (i->isBinary()) continue;

            if (i->isTriClause()) {
                if (!propTriClause<false>(i, p, confl)) break;
                if (enqeuedSomething) break;
                else continue;
            }

            if (i->isClause()) {
                if (!propNormalClause<false>(i, j, p, confl)) break;
                if (enqeuedSomething) break;
                else continue;
            }
        }
        ws.shrink_(ws.end()-j);
        if (confl != PropBy()) return confl;
        if (enqeuedSomething) goto start;
        qhead++;
    }

    return PropBy();
}

const PropBy Solver::propBin(const Lit p, vec<Watched>::iterator k, std::set<BinaryClause>& uselessBin)
{
    const Lit lit = k->getOtherLit();
    const lbool val = value(lit);
    if (val.isUndef()) {
        enqueueComplex(lit, p, k->getLearnt());
        return PropBy();
    } else if (val == l_False) {
        return PropBy(p);
    } else if (varData[lit.var()].level != 0) {
        assert(val == l_True);

        #ifdef VERBOSE_DEBUG_FULLPROP
        std::cout << "Lit " << p << " also wants to propagate " << lit << std::endl;
        #endif
        Lit remove = removeWhich(lit, p, k->getLearnt());
        if (remove == p) {
            Lit origAnc = propData[lit.var()].ancestor;
            assert(origAnc != lit_Undef);

            const BinaryClause clauseToRemove(~propData[lit.var()].ancestor,lit, propData[lit.var()].learntStep);
            //We now remove the clause
            //If it's hyper-bin, then we remove the to-be-added hyper-binary clause
            //However, if the hyper-bin was never added because only 1 literal was unbound at level 0 (i.e. through
            //clause cleaning, the clause would have been 2-long), then we don't do anything.
            if (!propData[lit.var()].hyperBin) {
                #ifdef VERBOSE_DEBUG_FULLPROP
                std::cout << "Normal removing clause " << clauseToRemove << std::endl;
                #endif
                uselessBin.insert(clauseToRemove);
            } else if (!propData[lit.var()].hyperBinNotAdded) {
                #ifdef VERBOSE_DEBUG_FULLPROP
                std::cout << "Hyper-bin removing clause " << clauseToRemove << std::endl;
                #endif
                std::set<BinaryClause>::iterator it = needToAddBinClause.find(clauseToRemove);
                assert(it != needToAddBinClause.end());
                needToAddBinClause.erase(it);
            }

            //Update data indicating what lead to lit
            propData[lit.var()].ancestor = p;
            propData[lit.var()].learntStep = k->getLearnt();
            propData[lit.var()].hyperBin = false;
            propData[lit.var()].hyperBinNotAdded = false;

            //for correctness, we would need this, but that would need re-writing of history :S
            //if (!onlyNonLearnt) return PropBy();
        } else if (remove != lit_Undef) {
            #ifdef VERBOSE_DEBUG_FULLPROP
            std::cout << "Removing reverse real bin clause" << std::endl;
            #endif
            uselessBin.insert(BinaryClause(~p, lit, k->getLearnt()));
        }
    }

    return PropBy();
}

void Solver::sortWatched()
{
    #ifdef VERBOSE_DEBUG
    std::cout << "Sorting watchlists:" << std::endl;
    #endif

    //double myTime = cpuTime();
    for (vector<vec<Watched> >::iterator i = watches.begin(), end = watches.end(); i != end; i++) {
        if (i->size() == 0) continue;
        #ifdef VERBOSE_DEBUG
        vec<Watched>& ws = *i;
        std::cout << "Before sorting:" << std::endl;
        for (uint32_t i2 = 0; i2 < ws.size(); i2++) {
            if (ws[i2].isBinary()) std::cout << "Binary,";
            if (ws[i2].isTriClause()) std::cout << "Tri,";
            if (ws[i2].isClause()) std::cout << "Normal,";
        }
        std::cout << std::endl;
        #endif //VERBOSE_DEBUG

        std::sort(i->begin(), i->end(), WatchedSorter());

        #ifdef VERBOSE_DEBUG
        std::cout << "After sorting:" << std::endl;
        for (uint32_t i2 = 0; i2 < ws.size(); i2++) {
            if (ws[i2].isBinary()) std::cout << "Binary,";
            if (ws[i2].isTriClause()) std::cout << "Tri,";
            if (ws[i2].isClause()) std::cout << "Normal,";
        }
        std::cout << std::endl;
        #endif //VERBOSE_DEBUG
    }

    /*if (conf.verbosity >= 3) {
        std::cout << "c watched "
        << "sorting time: " << cpuTime() - myTime
        << std::endl;
    }*/
}

void Solver::printWatchList(const Lit lit) const
{
    const vec<Watched>& ws = watches[(~lit).toInt()];
    for (vec<Watched>::const_iterator it2 = ws.begin(), end2 = ws.end(); it2 != end2; it2++) {
        if (it2->isBinary()) {
            std::cout << "bin: " << lit << " , " << it2->getOtherLit() << " learnt : " <<  (it2->getLearnt()) << std::endl;
        } else if (it2->isTriClause()) {
            std::cout << "tri: " << lit << " , " << it2->getOtherLit() << " , " <<  (it2->getOtherLit2()) << std::endl;
        } else if (it2->isClause()) {
            std::cout << "cla:" << it2->getNormOffset() << std::endl;
        } else {
            assert(false);
        }
    }
}

const uint32_t Solver::getBinWatchSize(const bool alsoLearnt, const Lit lit) const
{
    uint32_t num = 0;
    const vec<Watched>& ws = watches[lit.toInt()];
    for (vec<Watched>::const_iterator it2 = ws.begin(), end2 = ws.end(); it2 != end2; it2++) {
        if (it2->isBinary() && (alsoLearnt || !it2->getLearnt())) {
            num++;
        }
    }

    return num;
}

const vector<Lit> Solver::getUnitaries() const
{
    vector<Lit> unitaries;
    if (decisionLevel() > 0) {
        for (uint32_t i = 0; i != trail_lim[0]; i++) {
            unitaries.push_back(trail[i]);
        }
    }

    return unitaries;
}

const uint32_t Solver::countNumBinClauses(const bool alsoLearnt, const bool alsoNonLearnt) const
{
    uint32_t num = 0;

    uint32_t wsLit = 0;
    for (vector<vec<Watched> >::const_iterator it = watches.begin(), end = watches.end(); it != end; it++, wsLit++) {
        const vec<Watched>& ws = *it;
        for (vec<Watched>::const_iterator it2 = ws.begin(), end2 = ws.end(); it2 != end2; it2++) {
            if (it2->isBinary()) {
                if (it2->getLearnt()) num += alsoLearnt;
                else num+= alsoNonLearnt;
            }
        }
    }

    assert(num % 2 == 0);
    return num/2;
}