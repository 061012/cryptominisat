/***************************************************************************
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
*****************************************************************************/

#include "MTSolver.h"

#include <set>
#include <omp.h>

void MTSolver::printNumThreads() const
{
    if (conf.verbosity >= 1) {
        std::cout << "c Using " << numThreads << " thread(s)" << std::endl;
    }
}

MTSolver::MTSolver(const int _numThreads, const SolverConf& _conf, const GaussConf& _gaussConfig) :
    conf(_conf)
    , gaussConfig(_gaussConfig)
    , numThreads(_numThreads)
{
    solvers.resize(numThreads, NULL);

    MTRand myRand(conf.origSeed);
    for (int i = 0; i < numThreads; i++) {
        uint32_t rand = conf.origSeed;
        if (i > 0) rand = myRand.randInt();
        setupOneSolver(i, rand);
    }
    finishedThread = 0;
}

MTSolver::~MTSolver()
{
    for (uint32_t i = 0; i < solvers.size(); i++) {
        delete solvers[i];
    }
}

void MTSolver::setupOneSolver(const int num, const uint32_t origSeed)
{
    SolverConf myConf = conf;
    myConf.origSeed = origSeed;
    if (num > 0) {
        if (num % 6 == 0) myConf.fixRestartType = dynamic_restart;
        else if (num % 6 == 4) myConf.fixRestartType = static_restart;
        myConf.simpBurstSConf *= 1.0 + std::max(0.2*(double)num, 2.0);
        myConf.simpStartMult *= 1.0 - std::max(0.1*(double)num, 0.7);
        myConf.simpStartMMult *= 1.0 - std::max(0.1*(double)num, 0.7);
        if (num % 6 == 5) {
            myConf.doVarElim = false;
            myConf.polarity_mode = polarity_false;
        }
    }
    if (num != 0) myConf.verbosity = 0;

    Solver* solver = new Solver(myConf, gaussConfig, &sharedData);
    solvers[num] = solver;
}



const lbool MTSolver::solve(const vec<Lit>& assumps)
{
    std::set<uint32_t> finished;
    lbool retVal;

    omp_set_num_threads(numThreads);
    int numThreadsLocal;
    #pragma omp parallel
    {
        #pragma omp single
        numThreadsLocal = omp_get_num_threads();

        int threadNum = omp_get_thread_num();
        vec<Lit> assumpsLocal(assumps);
        lbool ret = solvers[threadNum]->solve(assumpsLocal, numThreadsLocal, threadNum);

        #pragma omp critical (finished)
        {
            finished.insert(threadNum);
        }

        #pragma omp single
        {
            int numNeededInterrupt = 0;
            for(int i = 0; i < numThreadsLocal; i++) {
                if (i != threadNum) {
                    solvers[i]->setNeedToInterrupt();
                    numNeededInterrupt++;
                }
            }
            assert(numNeededInterrupt == numThreadsLocal-1);

            bool mustWait = true;
            while (mustWait) {
                #pragma omp critical (finished)
                if (finished.size() == (unsigned)numThreadsLocal) mustWait = false;
            }

            finishedThread = threadNum;
            retVal = ret;
            setUpFinish(retVal, threadNum);
        }
    }

    if (numThreads != numThreadsLocal) {
        for (int i = numThreadsLocal; i < numThreads; i++) {
            delete solvers[i];
        }
        numThreads = numThreadsLocal;
        solvers.resize(numThreads);
    }

    for(int i = 0; i < numThreads; i++) {
        solvers[i]->setNeedToInterrupt(false);
    }

    return retVal;
}

void MTSolver::setUpFinish(const lbool retVal, const int threadNum)
{
    Solver& solver = *solvers[threadNum];
    model.clear();
    if (retVal == l_True) {
        model.growTo(solver.model.size());
        std::copy(solver.model.getData(), solver.model.getDataEnd(), model.getData());
    }

    conflict.clear();
    if (retVal == l_False) {
        conflict.growTo(solver.conflict.size());
        std::copy(solver.conflict.getData(), solver.conflict.getDataEnd(), conflict.getData());
    }
}

Var MTSolver::newVar(bool dvar)
{
    uint32_t ret = 0;
    for (uint32_t i = 0; i < solvers.size(); i++) {
        ret = solvers[i]->newVar(dvar);
    }

    return ret;
}

template<class T> bool MTSolver::addClause (T& ps, const uint32_t group, const char* group_name)
{
    bool globalRet = true;
    for (uint32_t i = 0; i < solvers.size(); i++) {
        vec<Lit> copyPS(ps.size());
        std::copy(ps.getData(), ps.getDataEnd(), copyPS.getData());
        bool ret = solvers[i]->addClause(copyPS, group, group_name);
        if (ret == false) {
            #pragma omp critical
            globalRet = false;
        }
    }

    return globalRet;
}
template bool MTSolver::addClause(vec<Lit>& ps, const uint32_t group, const char* group_name);
template bool MTSolver::addClause(Clause& ps, const uint32_t group, const char* group_name);

template<class T> bool MTSolver::addLearntClause(T& ps, const uint32_t group, const char* group_name, const uint32_t glue, const float miniSatActivity)
{
    bool globalRet = true;
    for (uint32_t i = 0; i < solvers.size(); i++) {
        vec<Lit> copyPS(ps.size());
        std::copy(ps.getData(), ps.getDataEnd(), copyPS.getData());
        bool ret = solvers[i]->addLearntClause(copyPS, group, group_name, glue, miniSatActivity);
        if (ret == false) {
            #pragma omp critical
            globalRet = false;
        }
    }

    return globalRet;
}
template bool MTSolver::addLearntClause(vec<Lit>& ps, const uint32_t group, const char* group_name, const uint32_t glue, const float miniSatActivity);
template bool MTSolver::addLearntClause(Clause& ps, const uint32_t group, const char* group_name, const uint32_t glue, const float miniSatActivity);

template<class T> bool MTSolver::addXorClause (T& ps, bool xorEqualFalse, const uint32_t group, const char* group_name)
{
    bool globalRet = true;
    for (uint32_t i = 0; i < solvers.size(); i++) {
        vec<Lit> copyPS(ps.size());
        std::copy(ps.getData(), ps.getDataEnd(), copyPS.getData());
        bool ret = solvers[i]->addXorClause(copyPS, xorEqualFalse, group, group_name);
        if (ret == false) {
            globalRet = false;
        }
    }

    return globalRet;
}
template bool MTSolver::addXorClause(vec<Lit>& ps, bool xorEqualFalse, const uint32_t group, const char* group_name);
template bool MTSolver::addXorClause(XorClause& ps, bool xorEqualFalse, const uint32_t group, const char* group_name);


void MTSolver::printStats()
{
    Solver& solver = *solvers[finishedThread];
    solver.printStats(numThreads);
}

void MTSolver::setNeedToInterrupt()
{
    for (uint32_t i = 0; i < solvers.size(); i++) {
        solvers[i]->setNeedToInterrupt();
    }
}

void MTSolver::dumpSortedLearnts(const std::string& fileName, const uint32_t maxSize)
{
    solvers[finishedThread]->dumpSortedLearnts(fileName, maxSize);
}

void MTSolver::dumpOrigClauses(const std::string& fileName, const bool alsoLearntBin) const
{
    solvers[finishedThread]->dumpSortedLearnts(fileName, alsoLearntBin);
}

void MTSolver::setVariableName(const Var var, const std::string& name)
{
    for (uint32_t i = 0; i < solvers.size(); i++) {
        solvers[i]->setVariableName(var, name);
    }
}

//void needLibraryCNFFile(const std::string& fileName); //creates file in current directory with the filename indicated, and puts all calls from the library into the file.

