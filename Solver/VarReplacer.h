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

#ifndef VARREPLACER_H
#define VARREPLACER_H

#include <map>
#include <vector>

#include "constants.h"
#include "SolverTypes.h"
#include "Clause.h"
#include "Vec.h"

//#define VERBOSE_DEBUG

using std::map;
using std::vector;
class SolutionExtender;
class ThreadControl;

class LaterAddBinXor
{
    public:
        LaterAddBinXor(const Lit _lit1, const Lit _lit2) :
            lit1(_lit1)
            , lit2(_lit2)
        {}

        Lit lit1;
        Lit lit2;
};

/**
@brief Replaces variables with their anti/equivalents
*/
class VarReplacer
{
    public:
        VarReplacer(ThreadControl* control);
        ~VarReplacer();
        bool performReplace();
        bool replace(
            Lit lit1
            , Lit lit2
            , const bool xorEqualFalse
            , bool addLaterAsTwoBins
        );

        void extendModel(SolutionExtender* extender) const;

        vector<Var> getReplacingVars() const;
        const vector<Lit>& getReplaceTable() const;
        const Lit getLitReplacedWith(Lit lit) const;
        const map<Var, vector<Var> >&getReverseTable() const;
        bool replacingVar(const Var var) const;
        void newVar();
        bool addLaterAddBinXor();
        void updateVars(
            const vector<uint32_t>& outerToInter
            , const vector<uint32_t>& interToOuter
        );

        //Stats
        size_t getNumReplacedVars() const;
        size_t getNumLastReplacedVars() const;
        size_t getNewToReplaceVars() const;
        size_t getNumTrees() const;
        struct Stats
        {
            Stats() :
                numCalls(0)
                , cpu_time(0)
                , replacedLits(0)
                , zeroDepthAssigns(0)
                , actuallyReplacedVars(0)
                , removedBinClauses(0)
                , removedLongClauses(0)
                , removedLongLits(0)
            {}

            void clear()
            {
                Stats tmp;
                *this = tmp;
            }

            Stats& operator+=(const Stats& other)
            {
                cpu_time += other.cpu_time;
                replacedLits += other.replacedLits;
                zeroDepthAssigns += other.zeroDepthAssigns;
                actuallyReplacedVars += other.actuallyReplacedVars;
                removedBinClauses += other.removedBinClauses;
                removedLongClauses += other.removedLongClauses;
                removedLongLits += other.removedLongLits;

                return *this;
            }

            void print(const size_t nVars) const
            {
                cout << "c --------- VAR REPLACE STATS ----------" << endl;
                printStatsLine("c time"
                    , cpu_time
                    , cpu_time/(double)numCalls
                    , "per call"
                );

                printStatsLine("c trees' crown"
                    , actuallyReplacedVars
                    , 100.0*(double)actuallyReplacedVars/(double)nVars
                    , "% of vars"
                );

                printStatsLine("c 0-depth assigns"
                    , zeroDepthAssigns
                    , (double)zeroDepthAssigns/(double)nVars*100.0
                    , "% vars"
                );

                printStatsLine("c lits replaced"
                    , replacedLits
                );

                printStatsLine("c bin cls removed"
                    , removedBinClauses
                );

                printStatsLine("c long cls removed"
                    , removedLongClauses
                );

                printStatsLine("c long lits removed"
                    , removedLongLits
                );
                cout << "c --------- VAR REPLACE STATS END ----------" << endl;
            }

            void printShort() const
            {
                cout
                << "c vrep"
                << " vars " << actuallyReplacedVars
                << " lits " << replacedLits
                << " rem-bin-cls " << removedBinClauses
                << " rem-long-cls " << removedLongClauses
                << " T: " << std::fixed << std::setprecision(2)
                << cpu_time << " s "
                << endl;
            }

            uint64_t numCalls;
            double cpu_time;
            uint64_t replacedLits; ///<Num literals replaced during var-replacement
            uint64_t zeroDepthAssigns;
            uint64_t actuallyReplacedVars;
            uint64_t removedBinClauses;
            uint64_t removedLongClauses;
            uint64_t removedLongLits;
        };
        const Stats& getStats() const;

    private:
        ThreadControl* control; ///<The solver we are working with

        bool replace_set(vector<Clause*>& cs);
        bool replaceBins();
        bool handleUpdatedClause(Clause& c, const Lit origLit1, const Lit origLit2, const Lit origLit3);

        void setAllThatPointsHereTo(const Var var, const Lit lit);
        bool alreadyIn(const Var var, const Lit lit);
        vector<LaterAddBinXor> laterAddBinXor;

        //Mapping tables
        vector<Lit> table; ///<Stores which variables have been replaced by which literals. Index by: table[VAR]
        map<Var, vector<Var> > reverseTable; ///<mapping of variable to set of variables it replaces

        //Stats
        uint64_t replacedVars; ///<Num vars replaced during var-replacement
        uint64_t lastReplacedVars;
        Stats runStats;
        Stats globalStats;
};

inline size_t VarReplacer::getNumReplacedVars() const
{
    return replacedVars;
}

inline size_t VarReplacer::getNumLastReplacedVars() const
{
    return lastReplacedVars;
}

inline size_t VarReplacer::getNewToReplaceVars() const
{
    return replacedVars-lastReplacedVars;
}

inline const vector<Lit>& VarReplacer::getReplaceTable() const
{
    return table;
}

inline const Lit VarReplacer::getLitReplacedWith(const Lit lit) const
{
    return table[lit.var()] ^ lit.sign();
}

inline bool VarReplacer::replacingVar(const Var var) const
{
    return (reverseTable.find(var) != reverseTable.end());
}

inline size_t VarReplacer::getNumTrees() const
{
    return reverseTable.size();
}

inline const map<Var, vector<Var> >& VarReplacer::getReverseTable() const
{
    return reverseTable;
}

inline const VarReplacer::Stats& VarReplacer::getStats() const
{
    return globalStats;
}

#endif //VARREPLACER_H
