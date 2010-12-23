/*****************************************************************************
CryptoMiniSat -- Copyright (c) 2010 Mate Soos

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
******************************************************************************/

#include "SharedData.h"
#include "Solver.h"

class DataSync
{
    public:
        DataSync(Solver& solver, SharedData* sharedData);
        void newVar();
        const bool syncData();

        //New clause signalation
        template <class T> void signalNewBinClause(const T& ps);
        void signalNewBinClause(Lit lit1, Lit lit2);
        template <class T> void signalNewTriClause(const T& ps);
        void signalNewTriClause(const Lit lit1, const Lit lit2, const Lit lit3);

        //Get methods
        const uint32_t getSentUnitData() const;
        const uint32_t getRecvUnitData() const;
        const uint32_t getSentBinData() const;
        const uint32_t getRecvBinData() const;
        const uint32_t getSentTriData() const;
        const uint32_t getRecvTriData() const;

    private:
        //unitary shring functions
        const bool shareUnitData();
        uint32_t sentUnitData;
        uint32_t recvUnitData;

        //bin sharing functions
        const bool    shareBinData();
        vector<std::pair<Lit, Lit> > newBinClauses;
        const bool    syncBinFromOthers(const Lit lit, const vector<Lit>& bins, uint32_t& finished);
        void          syncBinToOthers();
        void          addOneBinToOthers(const Lit lit1, const Lit lit2);
        vec<uint32_t> syncFinish;
        uint32_t      sentBinData;
        uint32_t      recvBinData;

        //tri sharing functions
        const bool        shareTriData();
        vector<TriClause> newTriClauses;
        const bool        syncTriFromOthers(const Lit lit1, const vector<TriClause>& tris, uint32_t& finished);
        void              syncTriToOthers();
        void              addOneTriToOthers(const Lit lit1, const Lit lit2, const Lit lit3);
        vec<uint32_t>     syncFinishTri;
        uint32_t          sentTriData;
        uint32_t          recvTriData;

        //stats
        uint64_t lastSyncConf;
        uint32_t numCalls;

        //misc
        vec<char> seen;

        //main data
        SharedData* sharedData;
        Solver& solver;
};

inline const uint32_t DataSync::getSentUnitData() const
{
    return sentUnitData;
}

inline const uint32_t DataSync::getRecvUnitData() const
{
    return recvUnitData;
}

inline const uint32_t DataSync::getSentBinData() const
{
    return sentBinData;
}

inline const uint32_t DataSync::getRecvBinData() const
{
    return recvBinData;
}

inline const uint32_t DataSync::getSentTriData() const
{
    return sentTriData;
}

inline const uint32_t DataSync::getRecvTriData() const
{
    return recvTriData;
}

