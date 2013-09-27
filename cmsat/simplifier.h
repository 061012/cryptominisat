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

#ifndef SIMPLIFIER_H
#define SIMPLIFIER_H


#include <map>
#include <vector>
#include <list>
#include <set>
#include <queue>
#include <set>
#include <iomanip>
#include <fstream>

#include "solver.h"
#include "clause.h"
#include "queue.h"
#include "bitarray.h"
#include "solvertypes.h"
#include "heap.h"
#include "touchlist.h"
#include "varupdatehelper.h"

namespace CMSat {

using std::vector;
using std::list;
using std::map;
using std::pair;
using std::priority_queue;

class ClauseCleaner;
class SolutionExtender;
class Solver;
class GateFinder;
class XorFinderAbst;
class SubsumeStrengthen;

struct BlockedClause {
    BlockedClause() :
        blockedOn(lit_Undef)
        , toRemove(false)
    {}

    BlockedClause(
        const Lit _blockedOn
        , const vector<Lit>& _lits
        , const vector<uint32_t>& interToOuterMain
    ) :
        blockedOn( getUpdatedLit(_blockedOn, interToOuterMain))
        , toRemove(false)
        , lits(_lits)
    {
        updateLitsMap(lits, interToOuterMain);
    }

    Lit blockedOn;
    bool toRemove;
    vector<Lit> lits;
};

/**
@brief Handles subsumption, self-subsuming resolution, variable elimination, and related algorithms
*/
class Simplifier
{
public:

    //Construct-destruct
    Simplifier(Solver* solver);
    ~Simplifier();

    //Called from main
    bool simplify();
    void subsumeReds();
    void newVar();
    void print_elimed_vars() const;
    void updateVars(
        const vector<uint32_t>& outerToInter
        , const vector<uint32_t>& interToOuter
    );
    bool unEliminate(const Var var);
    uint64_t memUsed() const;
    uint64_t memUsedXor() const;

    //UnElimination
    void print_blocked_clauses_reverse() const;
    void extendModel(SolutionExtender* extender);

    //Get-functions
    struct Stats
    {
        Stats() :
            numCalls(0)
            //Time
            , linkInTime(0)
            , blockTime(0)
            , asymmTime(0)
            , varElimTime(0)
            , finalCleanupTime(0)

            //Startup stats
            , origNumFreeVars(0)
            , origNumMaxElimVars(0)
            , origNumIrredLongClauses(0)
            , origNumRedLongClauses(0)

            //Each algo
            , blocked(0)
            , blockedSumLits(0)
            , asymmSubs(0)
            , subsumedByVE(0)

            //Elimination
            , numVarsElimed(0)
            , varElimTimeOut(0)
            , clauses_elimed_long(0)
            , clauses_elimed_tri(0)
            , clauses_elimed_bin(0)
            , clauses_elimed_sumsize(0)
            , longRedClRemThroughElim(0)
            , triRedClRemThroughElim(0)
            , binRedClRemThroughElim(0)
            , numRedBinVarRemAdded(0)
            , testedToElimVars(0)
            , triedToElimVars(0)
            , usedAgressiveCheckToELim(0)
            , newClauses(0)

            , zeroDepthAssings(0)
        {
        }

        double totalTime() const
        {
            return linkInTime + blockTime + asymmTime
                + varElimTime + finalCleanupTime;
        }

        void clear()
        {
            Stats stats;
            *this = stats;
        }

        Stats& operator+=(const Stats& other)
        {
            numCalls += other.numCalls;

            //Time
            linkInTime += other.linkInTime;
            blockTime += other.blockTime;
            asymmTime += other.asymmTime;
            varElimTime += other.varElimTime;
            finalCleanupTime += other.finalCleanupTime;

            //Startup stats
            origNumFreeVars += other.origNumFreeVars;
            origNumMaxElimVars += other.origNumMaxElimVars;
            origNumIrredLongClauses += other.origNumIrredLongClauses;
            origNumRedLongClauses += other.origNumRedLongClauses;

            //Each algo
            blocked += other.blocked;
            blockedSumLits += other.blockedSumLits;
            asymmSubs += other.asymmSubs;
            subsumedByVE  += other.subsumedByVE;

            //Elim
            numVarsElimed += other.numVarsElimed;
            varElimTimeOut += other.varElimTimeOut;
            clauses_elimed_long += other.clauses_elimed_long;
            clauses_elimed_tri += other.clauses_elimed_tri;
            clauses_elimed_bin += other.clauses_elimed_bin;
            clauses_elimed_sumsize += other.clauses_elimed_sumsize;
            longRedClRemThroughElim += other.longRedClRemThroughElim;
            triRedClRemThroughElim += other.triRedClRemThroughElim;
            binRedClRemThroughElim += other.binRedClRemThroughElim;
            numRedBinVarRemAdded += other.numRedBinVarRemAdded;
            testedToElimVars += other.testedToElimVars;
            triedToElimVars += other.triedToElimVars;
            usedAgressiveCheckToELim += other.usedAgressiveCheckToELim;
            newClauses += other.newClauses;

            zeroDepthAssings += other.zeroDepthAssings;

            return *this;
        }

        void printShort(const bool print_var_elim = true) const
        {

            cout
            << "c [occur] " << linkInTime+finalCleanupTime << " is overhead"
            << endl;

            //About elimination
            if (print_var_elim) {
                cout
                << "c [v-elim]"
                << " elimed: " << numVarsElimed
                << " / " << origNumMaxElimVars
                << " / " << origNumFreeVars
                //<< " cl-elim: " << (clauses_elimed_long+clauses_elimed_bin)
                << " T: " << std::fixed << std::setprecision(2)
                << varElimTime << " s"
                << " T-out: " << varElimTimeOut
                << endl;

                cout
                << "c [v-elim]"
                << " cl-new: " << newClauses
                << " tried: " << triedToElimVars
                << " tested: " << testedToElimVars
                << " ("
                << (double)usedAgressiveCheckToELim/(double)testedToElimVars*100.0
                << " % agressive)"
                << endl;

                cout
                << "c [v-elim]"
                << " subs: "  << subsumedByVE
                << " red-bin rem: " << binRedClRemThroughElim
                << " red-tri rem: " << triRedClRemThroughElim
                << " red-long rem: " << longRedClRemThroughElim
                << " v-fix: " << std::setw(4) << zeroDepthAssings
                << endl;
            }

            cout
            << "c [simp] link-in T: " << linkInTime
            << " cleanup T: " << finalCleanupTime
            << endl;
        }

        void print(const size_t nVars) const
        {
            cout << "c -------- Simplifier STATS ----------" << endl;
            printStatsLine("c time"
                , totalTime()
                , varElimTime/totalTime()*100.0
                , "% var-elim"
            );

            printStatsLine("c timeouted"
                , (double)varElimTimeOut/(double)numCalls*100.0
                , "% called"
            );

            printStatsLine("c called"
                ,  numCalls
                , (double)totalTime()/(double)numCalls
                , "s per call"
            );

            printStatsLine("c v-elimed"
                , numVarsElimed
                , (double)numVarsElimed/(double)nVars*100.0
                , "% vars"
            );

            cout << "c"
            << " v-elimed: " << numVarsElimed
            << " / " << origNumMaxElimVars
            << " / " << origNumFreeVars
            << endl;

            printStatsLine("c 0-depth assigns"
                , zeroDepthAssings
                , (double)zeroDepthAssings/(double)nVars*100.0
                , "% vars"
            );

            printStatsLine("c lit-rem-str"
                , litsRemStrengthen
            );

            printStatsLine("c cl-new"
                , newClauses
            );

            printStatsLine("c tried to elim"
                , triedToElimVars
                , (double)usedAgressiveCheckToELim/(double)triedToElimVars*100.0
                , "% agressively"
            );

            printStatsLine("c blocked"
                , blocked
                , (double)blocked/(double)origNumIrredLongClauses
                , "% of irred clauses"
            );

            printStatsLine("c asymmSub"
                , asymmSubs);

            printStatsLine("c elim-bin-lt-cl"
                , binRedClRemThroughElim);

            printStatsLine("c elim-tri-lt-cl"
                , triRedClRemThroughElim);

            printStatsLine("c elim-long-lt-cl"
                , longRedClRemThroughElim);

            printStatsLine("c lt-bin added due to v-elim"
                , numRedBinVarRemAdded);

            printStatsLine("c cl-elim-bin"
                , clauses_elimed_bin);

            printStatsLine("c cl-elim-tri"
                , clauses_elimed_tri);

            printStatsLine("c cl-elim-long"
                , clauses_elimed_long);

            printStatsLine("c cl-elim-avg-s",
                ((double)clauses_elimed_sumsize
                /(double)(clauses_elimed_bin + clauses_elimed_tri + clauses_elimed_long))
            );

            printStatsLine("c v-elim-sub"
                , subsumedByVE
            );

            cout << "c -------- Simplifier STATS END ----------" << endl;
        }

        uint64_t numCalls;

        //Time stats
        double linkInTime;
        double blockTime;
        double asymmTime;
        double varElimTime;
        double finalCleanupTime;

        //Startup stats
        uint64_t origNumFreeVars;
        uint64_t origNumMaxElimVars;
        uint64_t origNumIrredLongClauses;
        uint64_t origNumRedLongClauses;

        //Each algorithm
        uint64_t blocked;
        uint64_t blockedSumLits;
        uint64_t asymmSubs;
        uint64_t subsumedByVE;
        uint64_t litsRemStrengthen;

        //Stats for var-elim
        int64_t numVarsElimed;
        uint64_t varElimTimeOut;
        uint64_t clauses_elimed_long;
        uint64_t clauses_elimed_tri;
        uint64_t clauses_elimed_bin;
        uint64_t clauses_elimed_sumsize;
        uint64_t longRedClRemThroughElim;
        uint64_t triRedClRemThroughElim;
        uint64_t binRedClRemThroughElim;
        uint64_t numRedBinVarRemAdded;
        uint64_t testedToElimVars;
        uint64_t triedToElimVars;
        uint64_t usedAgressiveCheckToELim;
        uint64_t newClauses;

        //General stat
        uint64_t zeroDepthAssings;
    };

    bool getVarElimed(const Var var) const;
    const vector<BlockedClause>& getBlockedClauses() const;
    //const GateFinder* getGateFinder() const;
    const Stats& getStats() const;
    const SubsumeStrengthen* getSubsumeStrengthen() const;
    void checkElimedUnassignedAndStats() const;
    void checkElimedUnassigned() const;
    bool getAnythingHasBeenBlocked() const;
    void freeXorMem();

private:

    friend class SubsumeStrengthen;
    SubsumeStrengthen* subsumeStrengthen;

    //debug
    bool subsetReverse(const Clause& B) const;
    void checkAllLinkedIn();

    void finishUp(size_t origTrailSize);
    vector<ClOffset> clauses;
    bool subsumeWithBinaries();

    //Persistent data
    Solver*  solver;              ///<The solver this simplifier is connected to
    vector<bool>    var_elimed;           ///<Contains TRUE if var has been eliminated

    //Temporaries
    vector<unsigned char>    seen;        ///<Used in various places to help perform algorithms
    vector<unsigned char>    seen2;       ///<Used in various places to help perform algorithms
    vector<Lit>     dummy;       ///<Used by merge()
    vector<Lit>     toClear;      ///<Used by merge()
    vector<Lit>     finalLits;   ///<Used by addClauseInt()

    //Limits
    uint64_t addedClauseLits;
    int64_t  numMaxSubsume1;              ///<Max. number self-subsuming resolution tries to do this run
//     int64_t  numMaxTriSub;
    int64_t  numMaxSubsume0;              ///<Max. number backward-subsumption tries to do this run
    int64_t  numMaxElim;                  ///<Max. number of variable elimination tries to do this run
    int64_t  numMaxElimVars;
    int64_t  numMaxAsymm;
    int64_t  numMaxBlocked;
    int64_t  numMaxBlockedImpl;
    int64_t  numMaxVarElimAgressiveCheck;
    int64_t* toDecrease;

    //Propagation&handling of stuff
    bool propagate();

    //Start-up
    bool addFromSolver(
        vector<ClOffset>& toAdd
        , bool alsoOccur
        , bool irred
        , uint64_t& numLitsAdded
    );
    void setLimits();

    //Finish-up
    void addBackToSolver();
    bool propImplicits();
    void removeAllLongsFromWatches();
    bool completeCleanClause(Clause& ps);

    //Clause update
    lbool       cleanClause(ClOffset c);
    void        unlinkClause(ClOffset cc, bool drup = true);
    void        linkInClause(Clause& cl);
    bool        handleUpdatedClause(ClOffset c);

    struct WatchSorter {
        bool operator()(const Watched& first, const Watched& second)
        {
            //Anything but clause!
            if (first.isClause())
                return false;
            if (second.isClause())
                return true;

            //BIN is better than TRI
            if (first.isBinary() && second.isTri()) return true;

            return false;
        }
    };

    /**
    @brief Sort clauses according to size
    */
    struct MySorter
    {
        MySorter(const ClauseAllocator* _clAllocator) :
            clAllocator(_clAllocator)
        {}

        bool operator () (const ClOffset x, const ClOffset y)
        {
            Clause* cl1 = clAllocator->getPointer(x);
            Clause* cl2 = clAllocator->getPointer(y);
            return (cl1->size() < cl2->size());
        }

        const ClauseAllocator* clAllocator;
    };

    /////////////////////
    //Variable elimination

    vector<pair<int, int> > varElimComplexity;
    ///Order variables according to their complexity of elimination
    struct VarOrderLt {
        const vector<pair<int, int> >&  varElimComplexity;
        bool operator () (const size_t x, const size_t y) const
        {
            //Smallest cost first
            if (varElimComplexity[x].first != varElimComplexity[y].first)
                return varElimComplexity[x].first < varElimComplexity[y].first;

            //Smallest cost first
            return varElimComplexity[x].second < varElimComplexity[y].second;
        }

        VarOrderLt(
            const vector<pair<int,int> >& _varElimComplexity
        ) :
            varElimComplexity(_varElimComplexity)
        {}
    };
    void        orderVarsForElimInit();
    Heap<VarOrderLt> varElimOrder;
    uint32_t    numIrredBins(const Lit lit) const;
    //void        addRedBinaries(const Var var);
    void        removeClausesHelper(const vec<Watched>& todo, const Lit lit);


    TouchList   touched;
    bool        maybeEliminate(const Var x);
    int         testVarElim(Var var);
    vector<pair<vector<Lit>, ClauseStats> > resolvents;

    struct HeuristicData
    {
        HeuristicData() :
            bin(0)
            , tri(0)
            , longer(0)
            , lit(0)
            , count(std::numeric_limits<uint32_t>::max()) //resolution count (if can be counted, otherwise MAX)
        {};

        uint32_t totalCls() const
        {
            return bin + tri + longer;
        }

        uint32_t bin;
        uint32_t tri;
        uint32_t longer;
        uint32_t lit;
        uint32_t count;
    };
    HeuristicData calcDataForHeuristic(const Lit lit);
    std::pair<int, int> strategyCalcVarElimScore(const Var var);

    //For empty resolvents
    enum class ResolventCountAction{count, set, unset};
    bool checkEmptyResolvent(const Lit lit);
    bool checkEmptyResolventHelper(
        const Lit lit
        , ResolventCountAction action
        , size_t otherSize
    );

    pair<int, int>  heuristicCalcVarElimScore(const Var var);
    bool merge(
        const Watched& ps
        , const Watched& qs
        , const Lit noPosLit
        , const bool useCache
    );
    bool agressiveCheck(
        const Lit lit
        , const Lit noPosLit
        , bool& retval
    );
    bool        eliminateVars();
    bool        loopSubsumeVarelim();

    /////////////////////
    //Helpers
    friend class XorFinder;
    friend class GateFinder;
    XorFinderAbst *xorFinder;
    GateFinder *gateFinder;

    /////////////////////
    //Blocked clause elimination
    void asymmTE();
    bool anythingHasBeenBlocked;
    void blockClauses();
    void blockImplicit(bool bins = false, bool tris = true);
    bool checkBlocked(const Lit lit);
    vector<BlockedClause> blockedClauses;
    map<Var, vector<size_t> > blk_var_to_cl;
    bool blockedMapBuilt;
    void buildBlockedMap();
    void cleanBlockedClauses();

    //validity checking
    void sanityCheckElimedVars();
    void printOccur(const Lit lit) const;

    ///Stats from this run
    Stats runStats;

    ///Stats globally
    Stats globalStats;
};

inline const vector<BlockedClause>& Simplifier::getBlockedClauses() const
{
    return blockedClauses;
}

inline bool Simplifier::getVarElimed(const Var var) const
{
    return var_elimed[var];
}

inline const Simplifier::Stats& Simplifier::getStats() const
{
    return globalStats;
}

inline bool Simplifier::getAnythingHasBeenBlocked() const
{
    return anythingHasBeenBlocked;
}

inline std::ostream& operator<<(std::ostream& os, const BlockedClause& bl)
{
    os << bl.lits << " blocked on: " << bl.blockedOn;

    return os;
}

inline bool Simplifier::subsetReverse(const Clause& B) const
{
    for (uint32_t i = 0; i != B.size(); i++) {
        if (!seen[B[i].toInt()])
            return false;
    }
    return true;
}

inline const SubsumeStrengthen* Simplifier::getSubsumeStrengthen() const
{
    return subsumeStrengthen;
}

/*inline const XorFinder* Simplifier::getXorFinder() const
{
    return xorFinder;
}*/

} //end namespace

#endif //SIMPLIFIER_H
