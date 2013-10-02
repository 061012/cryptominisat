/*
 * CryptoMiniSat
 *
 * Copyright (c) 2009-2013, Mate Soos and collaborators. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.0 of the License, or (at your option) any later version.
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

#ifndef __PROPENGINE_H__
#define __PROPENGINE_H__

#include <cstdio>
#include <string.h>
#include <stack>
#include <set>

//#define ANIMATE3D

#include "constants.h"
#include "propby.h"

#include "avgcalc.h"
#include "propby.h"
#include "vec.h"
#include "heap.h"
#include "alg.h"
#include "MersenneTwister.h"
#include "clause.h"
#include "boundedqueue.h"
#include "cnf.h"

namespace CMSat {

using std::set;
class Solver;
class SQLStats;

//#define VERBOSE_DEBUG_FULLPROP
//#define DEBUG_STAMPING

#ifdef VERBOSE_DEBUG
#define VERBOSE_DEBUG_FULLPROP
#define ENQUEUE_DEBUG
#define DEBUG_ENQUEUE_LEVEL0
#endif

class Solver;
class ClauseAllocator;

enum PropResult {
    PROP_FAIL = 0
    , PROP_NOTHING = 1
    , PROP_SOMETHING = 2
    , PROP_TODO = 3
};

struct PolaritySorter
{
    PolaritySorter(const vector<VarData>& _varData) :
        varData(_varData)
    {};

    bool operator()(const Lit lit1, const Lit lit2) {
        const bool value1 = varData[lit1.var()].polarity ^ lit1.sign();
        const bool value2 = varData[lit2.var()].polarity ^ lit2.sign();

        //Strongly prefer TRUE value at the beginning
        if (value1 == true && value2 == false)
            return true;

        if (value1 == false && value2 == true)
            return false;

        //Tie 2: last level
        /*assert(pol1 == pol2);
        if (pol1 == true) return varData[lit1.var()].level < varData[lit2.var()].level;
        else return varData[lit1.var()].level > varData[lit2.var()].level;*/

        return false;
    }

    const vector<VarData>& varData;
};

/**
@brief The propagating and conflict generation class

Handles watchlists, conflict analysis, propagation, variable settings, etc.
*/
class PropEngine: public CNF
{
public:

    // Constructor/Destructor:
    //
    PropEngine(
        ClauseAllocator* clAllocator
        , const SolverConf& _conf
    );
    ~PropEngine();

    // Variable mode:
    //
    virtual Var newVar(const bool dvar = true);

    // Read state:
    //
    uint32_t nAssigns   () const;         ///<The current number of assigned literals.

    //Get state
    uint32_t    getVerbosity() const;
    uint32_t    getBinWatchSize(const bool alsoRed, const Lit lit) const;
    uint32_t    decisionLevel() const;      ///<Returns current decision level
    vector<Lit> getUnitaries() const;       ///<Return the set of unitary clauses
    uint32_t    getNumUnitaries() const;    ///<Return the set of unitary clauses
    size_t      getTrailSize() const;       ///<Return trail size (MUST be called at decision level 0)
    bool        getStoredPolarity(const Var var);
    void        resetClauseDataStats(size_t clause_num);

    #ifdef DRUP
    std::ostream* drup;
    #endif

protected:
    #ifdef DRUP
    void drupNewUnit(const Lit lit);
    void drupRemCl(const Clause* cl);
    #endif

    //Non-categorised functions
    void     cancelZeroLight(); ///<Backtrack until level 0, without updating agility, etc.
    template<class T> uint16_t calcGlue(const T& ps); ///<Calculates the glue of a clause
    bool updateGlues;
    bool doLHBR;
    friend class SQLStats;
    PropStats propStats;

    //Stats for conflicts
    ConflCausedBy lastConflictCausedBy;

    // Solver state:
    //
    vector<Lit>         trail;            ///< Assignment stack; stores all assigments made in the order they were made.
    vector<uint32_t>    trail_lim;        ///< Separator indices for different decision levels in 'trail'.
    uint32_t            qhead;            ///< Head of queue (as index into the trail)
    Lit                 failBinLit;       ///< Used to store which watches[lit] we were looking through when conflict occured

    // Temporaries (to reduce allocation overhead).
    //
    vector<uint16_t>       seen;  ///<Used in multiple places. Contains 2 * numVars() elements, all zeroed out
    vector<uint16_t>       seen2; ///<To reduce temoprary data creation overhead. Used in minimiseLeartFurther()
    vector<Lit>            toClear; ///<Temporary, used in some places

    /////////////////
    // Enqueue
    ////////////////
    void  enqueue (const Lit p, const PropBy from = PropBy()); // Enqueue a literal. Assumes value of literal is undefined.
    void  enqueueComplex(const Lit p, const Lit ancestor, const bool redStep);

    /////////////////
    // Propagating
    ////////////////
    void         newDecisionLevel();                       ///<Begins a new decision level.
    PropBy propagateAnyOrder();
    PropBy propagateBinFirst(
        #ifdef STATS_NEEDED
        ,  AvgCalc<size_t>* watchListSizeTraversed = NULL
        #endif
    );
    PropBy propagateIrredBin();  ///<For debug purposes, to test binary clause removal
    PropResult prop_normal_helper(
        Clause& c
        , ClOffset offset
        , vec<Watched>::iterator &j
        , const Lit p
    );
    PropResult handle_normal_prop_fail(Clause& c, ClOffset offset, PropBy& confl);
    PropResult handle_prop_tri_fail(
        const vec<Watched>::const_iterator i
        , Lit lit1
        , PropBy& confl
    );

    /////////////////
    // Operations on clauses:
    /////////////////
    virtual void attachClause(
        const Clause& c
        , const bool checkAttach = true
    );
    virtual void detachTriClause(
        const Lit lit1
        , const Lit lit2
        , const Lit lit3
        , const bool red
    );
    virtual void detachBinClause(
        const Lit lit1
        , const Lit lit2
        , const bool red
    );
    virtual void attachBinClause(
        const Lit lit1
        , const Lit lit2
        , const bool red
        , const bool checkUnassignedFirst = true
    );
    virtual void attachTriClause(
        const Lit lit1
        , const Lit lit2
        , const Lit lit3
        , const bool red
    );
    virtual void detachModifiedClause(
        const Lit lit1
        , const Lit lit2
        , const uint32_t origSize
        , const Clause* address
    );

    /////////////////////////
    //Classes that must be friends, since they accomplish things on our datastructures
    /////////////////////////
    friend class CompleteDetachReatacher;
    friend class ClauseAllocator;

    // Debug & etc:
    void     printAllClauses();
    void     checkNoWrongAttach() const;
    void     printWatchList(const Lit lit) const;
    bool     satisfied(const BinaryClause& bin);

    //Var selection, activity, etc.
    AgilityData agility;
    void sortWatched();
    void updateVars(
        const vector<uint32_t>& outerToInter
        , const vector<uint32_t>& interToOuter
        , const vector<uint32_t>& interToOuter2
    );
    void updateWatch(vec<Watched>& ws, const vector<uint32_t>& outerToInter);

private:
    bool propBinaryClause(
        const vec<Watched>::const_iterator i
        , const Lit p
        , PropBy& confl
    ); ///<Propagate 2-long clause

    ///Propagate 3-long clause
    PropResult propTriHelperSimple(
        const Lit lit1
        , const Lit lit2
        , const Lit lit3
        , const bool red
    );
    void propTriHelperAnyOrder(
        const Lit lit1
        , const Lit lit2
        , const Lit lit3
        #ifdef STATS_NEEDED
        , const bool red
        #endif
    );
    void lazy_hyper_bin_resolve(Lit lit1, Lit lit2);
    bool can_do_lazy_hyper_bin(Lit lit1, Lit lit2, Lit lit3);
    void update_glue(Clause& c);

    PropResult propTriClause (
        const vec<Watched>::const_iterator i
        , const Lit p
        , PropBy& confl
    );
    bool propTriClauseAnyOrder(
        const vec<Watched>::const_iterator i
        , const Lit lit1
        , PropBy& confl
    );

    ///Propagate >3-long clause
    PropResult propNormalClause(
        const vec<Watched>::iterator i
        , vec<Watched>::iterator &j
        , const Lit p
        , PropBy& confl
    );
    bool propNormalClauseAnyOrder(
        const vec<Watched>::iterator i
        , vec<Watched>::iterator &j
        , const Lit p
        , PropBy& confl
    );
    void lazy_hyper_bin_resolve(
        const Clause& c
        , ClOffset offset
    );
};


///////////////////////////////////////
// Implementation of inline methods:

inline void PropEngine::newDecisionLevel()
{
    trail_lim.push_back(trail.size());
    #ifdef VERBOSE_DEBUG
    cout << "New decision level: " << trail_lim.size() << endl;
    #endif
}

inline uint32_t PropEngine::decisionLevel() const
{
    return trail_lim.size();
}

inline uint32_t PropEngine::nAssigns() const
{
    return trail.size();
}

/**
@brief Enqueues&sets a new fact that has been found

Call this when a fact has been found. Sets the value, enqueues it for
propagation, sets its level, sets why it was propagated, saves the polarity,
and does some logging if logging is enabled

@p p the fact to enqueue
@p from Why was it propagated (binary clause, tertiary clause, normal clause)
*/
inline void PropEngine::enqueue(const Lit p, const PropBy from)
{
    #ifdef DEBUG_ENQUEUE_LEVEL0
    #ifndef VERBOSE_DEBUG
    if (decisionLevel() == 0)
    #endif //VERBOSE_DEBUG
    cout << "enqueue var " << p.var()+1
    << " to val " << !p.sign()
    << " level: " << decisionLevel()
    << " sublevel: " << trail.size()
    << " by: " << from << endl;
    #endif //DEBUG_ENQUEUE_LEVEL0

    #ifdef ENQUEUE_DEBUG
    assert(trail.size() <= nVarsReal());
    assert(decisionLevel() == 0 || varData[p.var()].removed != Removed::elimed);
    #endif

    const Var v = p.var();
    assert(value(v) == l_Undef);
    if (!watches[(~p).toInt()].empty())
        __builtin_prefetch(watches[(~p).toInt()].begin());

    assigns[v] = boolToLBool(!p.sign());
    #ifdef STATS_NEEDED
    varData[v].stats.trailLevelHist.push(trail.size());
    varData[v].stats.decLevelHist.push(decisionLevel());
    #endif
    varData[v].reason = from;
    varData[v].level = decisionLevel();

    trail.push_back(p);
    propStats.propagations++;

    if (p.sign()) {
        #ifdef STATS_NEEDED
        varData[v].stats.negPolarSet++;
        #endif
        propStats.varSetNeg++;
    } else {
        #ifdef STATS_NEEDED
        varData[v].stats.posPolarSet++;
        #endif
        propStats.varSetPos++;
    }

    if (varData[v].polarity != !p.sign()) {
        agility.update(true);
        #ifdef STATS_NEEDED
        varData[v].stats.flippedPolarity++;
        #endif
        propStats.varFlipped++;
    } else {
        agility.update(false);
    }

    varData[v].polarity = !p.sign();

    #ifdef ANIMATE3D
    std::cerr << "s " << v << " " << p.sign() << endl;
    #endif
}

inline void PropEngine::enqueueComplex(
    const Lit p
    , const Lit ancestor
    , const bool redStep
) {
    enqueue(p, PropBy(~ancestor, redStep, false, false));

    assert(varData[ancestor.var()].level != 0);

    varData[p.var()].depth = varData[ancestor.var()].depth + 1;
    #ifdef DEBUG_DEPTH
    cout
    << "Enqueued "
    << std::setw(6) << (p)
    << " by " << std::setw(6) << (~ancestor)
    << " at depth " << std::setw(4) << varData[p.var()].depth
    << endl;
    #endif
}

inline void PropEngine::cancelZeroLight()
{
    assert((int)decisionLevel() > 0);

    for (int sublevel = trail.size()-1; sublevel >= (int)trail_lim[0]; sublevel--) {
        Var var = trail[sublevel].var();
        assigns[var] = l_Undef;
    }
    qhead = trail_lim[0];
    trail.resize(trail_lim[0]);
    trail_lim.clear();
}

inline uint32_t PropEngine::getNumUnitaries() const
{
    if (decisionLevel() > 0)
        return trail_lim[0];
    else
        return trail.size();
}

inline size_t PropEngine::getTrailSize() const
{
    assert(decisionLevel() == 0);

    return trail.size();
}

inline bool PropEngine::satisfied(const BinaryClause& bin)
{
    return ((value(bin.getLit1()) == l_True)
            || (value(bin.getLit2()) == l_True));
}

/**
@brief Calculates the glue of a clause

Used to calculate the Glue of a new clause, or to update the glue of an
existing clause. Only used if the glue-based activity heuristic is enabled,
i.e. if we are in GLUCOSE mode (not MiniSat mode)
*/
template<class T>
uint16_t PropEngine::calcGlue(const T& ps)
{
    uint32_t nbLevels = 0;
    typename T::const_iterator l, end;

    for(l = ps.begin(), end = ps.end(); l != end; l++) {
        int32_t lev = varData[l->var()].level;
        if (!seen2[lev]) {
            nbLevels++;
            seen2[lev] = 1;
        }
    }

    for(l = ps.begin(), end = ps.end(); l != end; l++) {
        int32_t lev = varData[l->var()].level;
        seen2[lev] = 0;
    }
    return nbLevels;
}


inline bool PropEngine::getStoredPolarity(const Var var)
{
    return varData[var].polarity;
}

#ifdef DRUP
inline void PropEngine::drupNewUnit(const Lit lit)
{
    if (drup) {
        *(drup)
        << lit
        << " 0\n";
    }
}

inline void PropEngine::drupRemCl(const Clause* cl)
{
    if (drup) {
        (*drup)
        << "d "
        << *cl
        << " 0\n";
    }
}
#endif

} //end namespace

#endif //__PROPENGINE_H__
