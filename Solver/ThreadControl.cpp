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

#include "ThreadControl.h"
#include "VarReplacer.h"
#include "time_mem.h"
#include "CommandControl.h"
#include "SCCFinder.h"
#include "Subsumer.h"
#include "FailedLitSearcher.h"
#include "ClauseVivifier.h"
#include "ClauseCleaner.h"
#include "SolutionExtender.h"
#include "VarUpdateHelper.h"
#include "GateFinder.h"
#include <omp.h>
#include <fstream>
#include <cmath>
#include "XorFinder.h"
using std::cout;
using std::endl;

ThreadControl::ThreadControl(const SolverConf& _conf) :
    CommandControl(_conf, this)
    , backupActivityInc(_conf.var_inc_start)
    , mtrand(_conf.origSeed)
    , nbReduceDB(0)
    , conf(_conf)
    , needToInterrupt(false)

    //Stuff
    , nextCleanLimit(0)
    , numDecisionVars(0)
    , zeroLevAssignsByCNF(0)
    , zeroLevAssignsByThreads(0)
    , clausesLits(0)
    , learntsLits(0)
    , numBinsNonLearnt(0)
    , numBinsLearnt(0)
{
    failedLitSearcher = new FailedLitSearcher(this);
    subsumer = new Subsumer(this);
    sCCFinder = new SCCFinder(this);
    clauseVivifier = new ClauseVivifier(this);
    clauseCleaner = new ClauseCleaner(this);
    clAllocator = new ClauseAllocator;
    varReplacer = new VarReplacer(this);

    Solver::clAllocator = clAllocator;
}

ThreadControl::~ThreadControl()
{
    delete failedLitSearcher;
    delete subsumer;
    delete sCCFinder;
    delete clauseVivifier;
    delete clauseCleaner;
    delete clAllocator;
    delete varReplacer;
}

bool ThreadControl::addXorClauseInt(const vector< Lit >& lits, bool rhs)
{
    assert(ok);
    assert(qhead == trail.size());
    assert(decisionLevel() == 0);

    if (lits.size() > (0x01UL << 18)) {
        cout << "Too long clause!" << endl;
        exit(-1);
    }

    vector<Lit> ps(lits);
    std::sort(ps.begin(), ps.end());
    Lit p;
    uint32_t i, j;
    for (i = j = 0, p = lit_Undef; i != ps.size(); i++) {
        assert(!ps[i].sign()); //Every literal has to be unsigned

        if (ps[i].var() == p.var()) {
            //added, but easily removed
            j--;
            p = lit_Undef;
            if (!assigns[ps[i].var()].isUndef())
                rhs ^= assigns[ps[i].var()].getBool();
        } else if (assigns[ps[i].var()].isUndef()) { //just add
            ps[j++] = p = ps[i];
            assert(!subsumer->getVarElimed()[p.var()]);
        } else //modify rhs instead of adding
            rhs ^= (assigns[ps[i].var()].getBool());
    }
    ps.resize(ps.size() - (i - j));

    switch(ps.size()) {
        case 0:
            if (rhs)
                ok = false;
            return ok;

        case 1:
            enqueue(Lit(ps[0].var(), !rhs));
            propStats.propsUnit++;
            ok = propagate().isNULL();
            return ok;

        case 2:
            ps[0] ^= !rhs;
            addClauseInt(ps, false);
            if (!ok)
                return false;

            ps[0] ^= true;
            ps[1] ^= true;
            addClauseInt(ps, false);
            break;

        default:
            assert(false && "larger than 2-long XORs are not supported yet");
            break;
    }

    return ok;
}

/**
@brief Adds a clause to the problem. Should ONLY be called internally

This code is very specific in that it must NOT be called with varibles in
"ps" that have been replaced, eliminated, etc. Also, it must not be called
when the solver is in an UNSAT (!ok) state, for example. Use it carefully,
and only internally
*/
template <class T>
Clause* ThreadControl::addClauseInt(const T& lits
                            , const bool learnt
                            , const ClauseStats& stats
                            , const bool attach)
{
    assert(ok);
    assert(decisionLevel() == 0);
    assert(qhead == trail.size());
    #ifdef VERBOSE_DEBUG
    cout << "addClauseInt clause " << lits << endl;
    #endif //VERBOSE_DEBUG

    vector<Lit> ps(lits.size());
    std::copy(lits.begin(), lits.end(), ps.begin());

    std::sort(ps.begin(), ps.end());
    Lit p = lit_Undef;
    uint32_t i, j;
    for (i = j = 0; i != ps.size(); i++) {
        if (value(ps[i]).getBool() || ps[i] == ~p)
            return NULL;
        else if (value(ps[i]) != l_False && ps[i] != p) {
            ps[j++] = p = ps[i];

            if (varData[p.var()].elimed != ELIMED_NONE
                && varData[p.var()].elimed != ELIMED_QUEUED_VARREPLACER
            ) {
                cout << "ERROR: clause " << lits << " contains literal "
                << p << " whose variable has been eliminated (elim number "
                << (int) (varData[p.var()].elimed) << " )"
                << endl;
            }

            //Variables that have been eliminated cannot be added internally
            //as part of a clause. That's a bug
            assert(varData[p.var()].elimed == ELIMED_NONE
                    || varData[p.var()].elimed == ELIMED_QUEUED_VARREPLACER);
        }
    }
    ps.resize(ps.size() - (i - j));

    //Handle special cases
    switch (ps.size()) {
        case 0:
            ok = false;
            return NULL;
        case 1:
            enqueue(ps[0]);
            propStats.propsUnit++;
            ok = (propagate().isNULL());
            return NULL;
        case 2:
            attachBinClause(ps[0], ps[1], learnt);
            return NULL;
        default:
            Clause* c = clAllocator->Clause_new(ps, sumSolvingStats.numConflicts);
            if (learnt)
                c->makeLearnt(stats.glue);
            c->stats = stats;

            //In class 'Subsumer' we don't need to attach normall
            if (attach)
                attachClause(*c);

            return c;
    }
}

template Clause* ThreadControl::addClauseInt(
    const Clause& ps
    , const bool learnt
    , const ClauseStats& stats
    , const bool attach
);

template Clause* ThreadControl::addClauseInt(
    const vector<Lit>& ps
    , const bool learnt
    , const ClauseStats& stats
    , const bool attach
);

void ThreadControl::attachClause(const Clause& c)
{
    //Update stats
    if (c.learnt())
        learntsLits += c.size();
    else
        clausesLits += c.size();

    //Call Solver's function for heavy-lifting
    Solver::attachClause(c);
}

void ThreadControl::attachBinClause(
    const Lit lit1
    , const Lit lit2
    , const bool learnt
    , const bool checkUnassignedFirst
) {
    //Update stats
    if (learnt) {
        learntsLits += 2;
        numBinsLearnt++;
    } else {
        clausesLits += 2;
        numBinsNonLearnt++;
    }

    //Call Solver's function for heavy-lifting
    Solver::attachBinClause(lit1, lit2, learnt, checkUnassignedFirst);
}

void ThreadControl::detachClause(const Clause& c)
{
   if (c.size() > 3) {
       detachModifiedClause(
            c[0], c[1]
            , (c.size() == 3) ? c[2] : lit_Undef
            ,  c.size()
            , &c
        );
    } else {
        detachModifiedClause(c[0], c[1], c[2], c.size(), &c);
    }
}

void ThreadControl::detachModifiedClause(
    const Lit lit1
    , const Lit lit2
    , const Lit lit3
    , const uint32_t origSize, const Clause* address)
{
    //Update stats
    if (address->learnt())
        learntsLits -= origSize;
    else
        clausesLits -= origSize;

    //Call heavy-lifter
    Solver::detachModifiedClause(lit1, lit2, lit3, origSize, address);
}

bool ThreadControl::addClauseHelper(vector<Lit>& ps)
{
    //If already UNSAT, just return
    if (!ok)
        return false;

    //Sanity checks
    assert(decisionLevel() == 0);
    assert(qhead == trail.size());
    if (ps.size() > (0x01UL << 18)) {
        cout << "Too long clause!" << endl;
        exit(-1);
    }
    for (vector<Lit>::const_iterator it = ps.begin(), end = ps.end(); it != end; it++) {
        assert(it->var() < nVars() && "Clause inserted, but variable inside has not been declared with Solver::newVar() !");
    }

    for (uint32_t i = 0; i != ps.size(); i++) {
        //Update to correct var
        ps[i] = varReplacer->getReplaceTable()[ps[i].var()] ^ ps[i].sign();

        //Uneliminate var if need be
        if (subsumer->getVarElimed()[ps[i].var()]
            && !subsumer->unEliminate(ps[i].var(), this)
        ) return false;
    }

    //Randomise
    for (uint32_t i = 0; i < ps.size(); i++) {
        std::swap(ps[i], ps[(mtrand.randInt() % (ps.size()-i)) + i]);
    }

    return true;
}

/**
@brief Adds a clause to the problem. Calls addClauseInt() for heavy-lifting

Checks whether the
variables of the literals in "ps" have been eliminated/replaced etc. If so,
it acts on them such that they are correct, and calls addClauseInt() to do
the heavy-lifting
*/
bool ThreadControl::addClause(const vector<Lit>& lits)
{
    #ifdef VERBOSE_DEBUG
    cout << "Adding clause " << lits << endl;
    #endif //VERBOSE_DEBUG
    const size_t origTrailSize = trail.size();

    vector<Lit> ps = lits;
    if (!addClauseHelper(ps))
        return false;

    Clause* c = addClauseInt(ps);
    if (c != NULL)
        clauses.push_back(c);

    zeroLevAssignsByCNF += trail.size() - origTrailSize;
    return ok;
}

bool ThreadControl::addLearntClause(
    const vector<Lit>& lits
    , const ClauseStats& stats
) {
    vector<Lit> ps(lits.size());
    std::copy(lits.begin(), lits.end(), ps.begin());

    if (!addClauseHelper(ps))
        return false;

    Clause* c = addClauseInt(ps, true, stats);
    if (c != NULL)
        learnts.push_back(c);

    return ok;
}

void ThreadControl::reArrangeClause(Clause* clause)
{
    Clause& c = *clause;
    assert(c.size() > 2);
    if (c.size() == 3) return;

    //Change anything, but find the first two and assign them
    //accordingly at the ClauseData
    const Lit lit1 = c[0];
    const Lit lit2 = c[1];
    assert(lit1 != lit2);

    std::sort(c.begin(), c.end(), PolaritySorter(varData));

    uint8_t foundDatas = 0;
    for (uint16_t i = 0; i < c.size(); i++) {
        if (c[i] == lit1) {
            std::swap(c[i], c[0]);
            foundDatas++;
        }
    }

    for (uint16_t i = 0; i < c.size(); i++) {
        if (c[i] == lit2) {
            std::swap(c[i], c[1]);
            foundDatas++;
        }
    }
    assert(foundDatas == 2);
}

void ThreadControl::reArrangeClauses()
{
    assert(decisionLevel() == 0);
    assert(ok);
    assert(qhead == trail.size());

    double myTime = cpuTime();
    for (uint32_t i = 0; i < clauses.size(); i++) {
        reArrangeClause(clauses[i]);
    }
    for (uint32_t i = 0; i < learnts.size(); i++) {
        reArrangeClause(learnts[i]);
    }

    if (conf.verbosity >= 1) {
        cout << "c Rearrange lits in clauses "
        << std::setw(4) << std::setprecision(2) << (cpuTime() - myTime)  << " s"
        << endl;
    }
}

static void printArray(const vector<Var>& array, const std::string& str)
{
    cout << str << " : " << endl;
    for(size_t i = 0; i < array.size(); i++) {
        cout << str << "[" << i << "] : " << array[i] << endl;
    }
    cout << endl;
}

void ThreadControl::renumberVariables()
{
    double myTime = cpuTime();
    double myTotalTime = 0;
    clauseCleaner->removeAndCleanAll();

    /*vector<uint32_t> myOuterToInter;
    myOuterToInter.push_back(2);
    myOuterToInter.push_back(3);
    myOuterToInter.push_back(1);
    myOuterToInter.push_back(0);
    myOuterToInter.push_back(4);
    myOuterToInter.push_back(5);

    vector<uint32_t> myInterToOUter;
    myInterToOUter.push_back(3);
    myInterToOUter.push_back(2);
    myInterToOUter.push_back(0);
    myInterToOUter.push_back(1);
    myInterToOUter.push_back(4);
    myInterToOUter.push_back(5);

    vector<uint32_t> toreorder;
    for(size_t i = 0; i < 6; i++)
        toreorder.push_back(i);

    //updateBySwap(toreorder, seen, myOuterToInter);
    updateVarsArray(toreorder, myInterToOUter);
    for(size_t i = 0; i < 6; i++) {
        cout << toreorder[i] << " , ";
    }

    cout << endl;
    exit(-1);*/

    //outerToInter[10] = 0 ---> what was 10 is now 0.

    //Fill the first part of interToOuter with vars that are used
    vector<Var> outerToInter(nVars());
    vector<Var> interToOuter(nVars());
    size_t at = 0;
    vector<Var> useless;
    for(size_t i = 0; i < nVars(); i++) {
        if (value(i) != l_Undef
            || varData[i].elimed == ELIMED_VARELIM
            || varData[i].elimed == ELIMED_VARREPLACER
        ) {
            useless.push_back(i);
            continue;
        }

        outerToInter[i] = at;
        interToOuter[at] = i;
        at++;
    }

    //Fill the rest with variables that have been removed/eliminated/set
    for(vector<Var>::const_iterator
        it = useless.begin(), end = useless.end()
        ; it != end
        ; it++
    ) {
        outerToInter[*it] = at;
        interToOuter[at] = *it;
        at++;
    }
    assert(at == nVars());

    //Create temporary outerToInter2
    vector<uint32_t> interToOuter2(interToOuter.size()*2);
    for(size_t i = 0; i < interToOuter.size(); i++) {
        interToOuter2[i*2] = interToOuter[i]*2;
        interToOuter2[i*2+1] = interToOuter[i]*2+1;
    }
    if (conf.verbosity >= 3) {
        cout
        << "c Time to calculate rearrangement: " << (cpuTime() - myTime)
        << endl;
    }
    myTotalTime += (cpuTime() - myTime);

    //Update updater data
    updateArray(interToOuterMain, interToOuter);
    updateArray(outerToInterMain, outerToInter);

    //For debug
    /*printArray(outerToInter, "outerToInter");
    printArray(outerToInterMain, "outerToInterMain");
    printArray(interToOuter, "interToOuter");
    printArray(interToOuterMain, "interToOuterMain");*/


    //Update local data
    myTime = cpuTime();
    updateArray(backupActivity, interToOuter);
    updateArray(backupPolarity, interToOuter);

    //Update reachability
    updateArray(litReachable, interToOuter2);
    for(size_t i = 0; i < litReachable.size(); i++) {
        if (litReachable[i].lit != lit_Undef)
            litReachable[i].lit = getUpdatedLit(litReachable[i].lit, outerToInter);
    }

    updateArray(decision_var, interToOuter);
    Solver::updateVars(outerToInter, interToOuter, interToOuter2);
    if (conf.verbosity >= 3) {
        cout
        << "c Time to var-update local data: " << (cpuTime() - myTime)
        << endl;
    }
    myTotalTime += (cpuTime() - myTime);

    //Update clauses
    myTime = cpuTime();
    for(size_t i = 0; i < clauses.size(); i++) {
        updateLitsMap(*clauses[i], outerToInter);
    }

    for(size_t i = 0; i < learnts.size(); i++) {
        updateLitsMap(*learnts[i], outerToInter);
    }
    if (conf.verbosity >= 3) {
        cout
        << "c Time to var-update clauses: " << (cpuTime() - myTime)
        << endl;
    }
    myTotalTime += (cpuTime() - myTime);

    //Update sub-elements
    myTime = cpuTime();
    subsumer->updateVars(outerToInter, interToOuter);
    varReplacer->updateVars(outerToInter, interToOuter);
    implCache.updateVars(seen, outerToInter, interToOuter2);
    if (conf.verbosity >= 2) {
        cout
        << "c Time to var-update varreplacer&subsumer&cache: " << (cpuTime() - myTime)
        << endl;
    }
    myTotalTime += (cpuTime() - myTime);

    //Print results
    if (conf.verbosity >= 1) {
        cout
        << "c Reordered variables T: "
        << std::fixed << std::setw(5) << std::setprecision(2)
        << myTotalTime
        << endl;
    }

    //Check if we renumbered the varibles in the order such as to make
    //the unknown ones first and the known/eliminated ones second
    bool uninteresting = false;
    bool problem = false;
    for(size_t i = 0; i < nVars(); i++) {
        //cout << "val[" << i << "]: " << value(i);

        if (value(i)  != l_Undef)
            uninteresting = true;

        if (varData[i].elimed == ELIMED_VARELIM
            || varData[i].elimed == ELIMED_VARREPLACER
        ) {
            uninteresting = true;
            //cout << " elimed" << endl;
        } else {
            //cout << " non-elimed" << endl;
        }

        if (value(i) == l_Undef
            && varData[i].elimed != ELIMED_VARELIM
            && varData[i].elimed != ELIMED_VARREPLACER
            && uninteresting
        ) {
            problem = true;
        }
    }
    assert(!problem && "We renumbered the variables in the wrong order!");
}

Var ThreadControl::newVar(const bool dvar)
{
    const Var var = decision_var.size();

    outerToInterMain.push_back(var);
    interToOuterMain.push_back(var);
    decision_var.push_back(dvar);
    numDecisionVars += dvar;

    implCache.addNew();
    litReachable.push_back(LitReachData());
    litReachable.push_back(LitReachData());
    backupActivity.push_back(0);
    backupPolarity.push_back(false);

    CommandControl::newVar();

    varReplacer->newVar();
    subsumer->newVar();

    return decision_var.size()-1;
}

/// @brief Sort clauses according to glues: large glues first
bool ThreadControl::reduceDBStructGlue::operator () (
    const Clause* x
    , const Clause* y
) {
    const uint32_t xsize = x->size();
    const uint32_t ysize = y->size();

    //No clause should be less than 3-long: 2&3-long are not removed
    assert(xsize > 2 && ysize > 2);

    //First tie: glue
    if (x->stats.glue > y->stats.glue) return 1;
    if (x->stats.glue < y->stats.glue) return 0;

    //Second tie: size
    return xsize > ysize;
}

/// @brief Sort clauses according to size: large sizes first
bool ThreadControl::reduceDBStructSize::operator () (
    const Clause* x
    , const Clause* y
) {
    const uint32_t xsize = x->size();
    const uint32_t ysize = y->size();

    //No clause should be less than 3-long: 2&3-long are not removed
    assert(xsize > 2 && ysize > 2);

    //First tie: size
    if (xsize > ysize) return 1;
    if (xsize < ysize) return 0;

    //Second tie: glue
    return x->stats.glue > y->stats.glue;
}

/// @brief Sort clauses according to size: small prop+confl first
bool ThreadControl::reduceDBStructPropConfl::operator() (
    const Clause* x
    , const Clause* y
) {
    const uint32_t xsize = x->size();
    const uint32_t ysize = y->size();

    //No clause should be less than 3-long: 2&3-long are not removed
    assert(xsize > 2 && ysize > 2);

    //First tie: numPropConfl -- notice the reversal of 1/0
    //Larger is better --> should be last in the sorted list
    if (x->stats.numPropAndConfl > y->stats.numPropAndConfl) return 0;
    if (x->stats.numPropAndConfl < y->stats.numPropAndConfl) return 1;

    //Second tie: size
    return xsize > ysize;
}

/**
@brief Removes learnt clauses that have been found not to be too good

Either based on glue or MiniSat-style learnt clause activities, the clauses are
sorted and then removed
*/
void ThreadControl::reduceDB()
{
    nbReduceDB++;

    //Calculate how much to remove
    uint32_t removeNum = (double)learnts.size() * conf.ratioRemoveClauses;

    size_t alreadyRemoved = 0;
    if (conf.doPreClauseCleanPropAndConfl) {
        //Reduce based on props&confls
        size_t i, j;
        for (i = j = 0; i < learnts.size(); i++) {
            Clause* cl = learnts[i];
            if (learnts[i]->size() > 3
                && cl->stats.numPropAndConfl < conf.preClauseCleanLimit
                && cl->stats.conflictNumIntroduced + conf.preCleanMinConflTime < sumSolvingStats.numConflicts
            ) {
                detachClause(*cl);
                clAllocator->clauseFree(cl);
                alreadyRemoved++;
            } else {
                learnts[j++] = learnts[i];
            }
        }
        learnts.resize(learnts.size() -(i-j));

        //Print how many have been removed thanks to pre-clean
        if (conf.verbosity >= 2) {
            cout
            << "c cleaning learnts. Pre-clean removed: "
            << alreadyRemoved
            << endl;
        }
    }

    switch (conf.clauseCleaningType) {
        case CLEAN_CLAUSES_GLUE_BASED :
            //Sort for glue-based removal
            std::sort(learnts.begin(), learnts.end(), reduceDBStructGlue());
            break;

        case CLEAN_CLAUSES_SIZE_BASED :
            //Sort for glue-based removal
            std::sort(learnts.begin(), learnts.end(), reduceDBStructSize());
            break;

        case CLEAN_CLAUSES_PROPCONFL_BASED :
            //Sort for glue-based removal
            std::sort(learnts.begin(), learnts.end(), reduceDBStructPropConfl());
            break;
    }

    #ifdef VERBOSE_DEBUG
    cout << "Cleaning learnt clauses. Learnt clauses after sort: " << endl;
    for (uint32_t i = 0; i != learnts.size(); i++) {
        cout << "activity:" << learnts[i]->getGlue()
        << " \tsize:" << learnts[i]->size() << endl;
    }
    #endif

    if (conf.verbosity >= 2) {
        cout << "c To remove (according to remove ratio): " << removeNum;
        if (removeNum <= alreadyRemoved)
            removeNum = 0;
        else
            removeNum -= alreadyRemoved;
        cout << " -- still to be removed: " << removeNum << endl;
    }

    //Statistics about clauses removed
    uint32_t totalNumRemoved = 0;
    uint32_t totalNumNonRemoved = 0;
    uint64_t totalGlueOfRemoved = 0;
    uint64_t totalSizeOfRemoved = 0;
    uint64_t totalGlueOfNonRemoved = 0;
    uint64_t totalSizeOfNonRemoved = 0;
    uint32_t numThreeLongLearnt = 0;

    size_t i, j;
    for (i = j = 0; i < std::min(removeNum, (uint32_t)learnts.size()); i++) {
        if (i+1 < learnts.size())
            __builtin_prefetch(learnts[i+1], 0);

        Clause *cl = learnts[i];
        assert(cl->size() > 2);
        if (learnts[i]->stats.glue > 2
            && cl->size() > 3 //we cannot update activity of 3-longs because of watchlists
            && cl->stats.numPropAndConfl < conf.clauseCleanNeverCleanAtOrAboveThisPropConfl
        ) {
            totalGlueOfRemoved += cl->stats.glue;
            totalSizeOfRemoved += cl->size();
            totalNumRemoved++;
            detachClause(*cl);
            clAllocator->clauseFree(cl);
        } else {
            totalGlueOfNonRemoved += cl->stats.glue;
            totalSizeOfNonRemoved += cl->size();
            totalNumNonRemoved++;
            numThreeLongLearnt += (cl->size()==3);
            removeNum++;
            learnts[j++] = cl;
        }
    }

    //Count what is left
    for (; i < learnts.size(); i++) {
        totalGlueOfNonRemoved += learnts[i]->stats.glue;
        totalSizeOfNonRemoved += learnts[i]->size();
        totalNumNonRemoved++;
        numThreeLongLearnt += (learnts[i]->size()==3);
        learnts[j++] = learnts[i];
    }

    //Resize learnt datastruct
    learnts.resize(learnts.size() - (i - j));

    //Print results
    if (conf.verbosity >= 1) {
        cout << "c cleaning by " << getNameOfCleanType(conf.clauseCleaningType)
        << " rem " << std::setw(6) << totalNumRemoved
        << "  avgGlue "
        << std::fixed << std::setw(5) << std::setprecision(2)  << ((double)totalGlueOfRemoved/(double)totalNumRemoved)
        << "  avgSize "
        << std::fixed << std::setw(6) << std::setprecision(2) << ((double)totalSizeOfRemoved/(double)totalNumRemoved)
        << "  || remain " << std::setw(6) << totalNumNonRemoved
        << "  avgGlue "
        << std::fixed << std::setw(5) << std::setprecision(2)  << ((double)totalGlueOfNonRemoved/(double)totalNumNonRemoved)
        << "  avgSize "
        << std::fixed << std::setw(6) << std::setprecision(2) << ((double)totalSizeOfNonRemoved/(double)totalNumNonRemoved)
        //<< "  3-long: " << std::setw(6) << numThreeLongLearnt
        << "  sumConflicts:" << sumSolvingStats.numConflicts
        << endl;
    }
}

lbool ThreadControl::solve()
{
    //Initialise stuff
    nextCleanLimitInc = conf.startClean;
    nextCleanLimit += nextCleanLimitInc;

    //Check if adding the clauses caused UNSAT
    lbool status = ok ? l_Undef : l_False;

    //If still unknown, simplify
    if (status == l_Undef && nVars() > 0)
        status = simplifyProblem(conf.simpBurstSConf);

    //Iterate until solved
    while (status == l_Undef) {
        if (conf.verbosity >= 2)
            printClauseSizeDistrib();

        //This is crucial, since we need to attach() clauses to threads
        clauseCleaner->removeAndCleanAll();

        //Solve using threads
        const size_t origTrailSize = trail.size();
        vector<lbool> statuses;
        uint32_t numConfls = nextCleanLimit - sumSolvingStats.numConflicts;
        for (size_t i = 0; i < conf.numCleanBetweenSimplify; i++) {
            numConfls+= (double)nextCleanLimitInc * std::pow(conf.increaseClean, i);
        }

        status = CommandControl::solve(numConfls);
        sumSolvingStats += CommandControl::getStats();

        //Back up activities, polairties and var_inc
        backupActivity.clear();
        backupPolarity.clear();
        backupActivity.resize(varData.size());
        backupPolarity.resize(varData.size());
        for (size_t i = 0; i < varData.size(); i++) {
            backupPolarity[i] = varData[i].polarity;
            backupActivity[i] = CommandControl::getSavedActivity(i);
        }
        backupActivityInc = CommandControl::getVarInc();

        if (status != l_False) {
            CommandControl::resetStats();
            fullReduce();
        }

        zeroLevAssignsByThreads += trail.size() - origTrailSize;
        if (status != l_Undef)
            break;

        //Simplify
        status = simplifyProblem(conf.simpBurstSConf);
    }

    //Handle found solution
    if (status == l_False) {
        //Not much to do, just return l_False
        return l_False;
    } else if (status == l_True) {
        //Extend solution
        SolutionExtender extender(this, solution);
        extender.extend();

        //Renumber model back to original variable numbering
        updateArrayRev(model, interToOuterMain);
    }

    return status;
}

/**
@brief The function that brings together almost all CNF-simplifications

It burst-searches for given number of conflicts, then it tries all sorts of
things like variable elimination, subsumption, failed literal probing, etc.
to try to simplifcy the problem at hand.
*/
lbool ThreadControl::simplifyProblem(const uint64_t numConfls)
{
    assert(ok);
    testAllClauseAttach();
    checkStats();

    reArrangeClauses();

    if (conf.verbosity >= 3)
        cout << "c Simplifying problem for "
        << std::setw(8) << numConfls << " confls"
        << endl;

    if (conf.doFindEqLits && !sCCFinder->find2LongXors())
        goto end;

    if (conf.doReplace && !varReplacer->performReplace())
        goto end;

    if (!implCache.tryBoth(this))
        goto end;

    if (conf.doFailedLit && !failedLitSearcher->search())
        goto end;

    if (conf.doFindEqLits && !sCCFinder->find2LongXors())
        goto end;

    if (conf.doReplace && !varReplacer->performReplace())
        goto end;

    if (needToInterrupt) return l_Undef;

    //Var-elim, gates, subsumption, strengthening
    if (conf.doSatELite && !subsumer->simplifyBySubsumption())
        goto end;

    //Vivify clauses
    if (conf.doClausVivif && !clauseVivifier->vivify())
        goto end;

    //Search & replace 2-long XORs
    if (conf.doFindEqLits && !sCCFinder->find2LongXors())
        goto end;

    if (conf.doReplace && !varReplacer->performReplace())
        goto end;

    //Cleaning, stat counting, etc.
    if (conf.doCache)
        implCache.clean(this);

    if (!implCache.tryBoth(this))
        goto end;

    if (conf.doCache && conf.doCalcReach)
        calcReachability();

    if (conf.doSortWatched)
        sortWatched();

    if (conf.doRenumberVars)
        renumberVariables();

    reArrangeClauses();

    //addSymmBreakClauses();

end:
    if (conf.verbosity >= 3)
        cout << "c Simplifying finished" << endl;

    testAllClauseAttach();
    checkNoWrongAttach();

    if (!ok) {
        return l_False;
    } else {
        checkStats();
        return l_Undef;
    }
}

void ThreadControl::calcReachability()
{
    double myTime = cpuTime();

    for (uint32_t i = 0; i < nVars()*2; i++) {
        litReachable[i] = LitReachData();
    }

    for (size_t litnum = 0; litnum < nVars()*2; litnum++) {
        const Lit lit = Lit::toLit(litnum);
        const Var var = lit.var();
        if (value(var) != l_Undef
            || varData[var].elimed != ELIMED_NONE
            || !decision_var[var]
        ) continue;

        //See where setting this lit leads to
        const vector<LitExtra>& cache = implCache[(~lit).toInt()].lits;
        const size_t cacheSize = cache.size();
        for (vector<LitExtra>::const_iterator it = cache.begin(), end = cache.end(); it != end; it++) {
            //Cannot lead to itself
            assert(it->getLit().var() != lit.var());

            //If learnt, skip
            if (!it->getOnlyNLBin())
                continue;

            //If reachability is nonexistent or low, set it
            if (litReachable[it->getLit().toInt()].lit == lit_Undef
                || litReachable[it->getLit().toInt()].numInCache < cacheSize
            ) {
                litReachable[it->getLit().toInt()].lit = lit;
                //NOTE: we actually MISREPRESENT this, as only non-learnt should be presented here
                litReachable[it->getLit().toInt()].numInCache = cacheSize;
            }
        }
    }

    if (conf.verbosity >= 1) {
        cout
        << "c calculated reachability."
        << " Time: " << (cpuTime() - myTime)
        << endl;
    }
}

Clause* ThreadControl::newClauseByThread(const vector<Lit>& lits, const uint32_t glue)
{
    Clause* cl = NULL;
    switch (lits.size()) {
        case 1:
            break;
        case 2:
            break;
        default:
            cl = clAllocator->Clause_new(lits, CommandControl::sumConflicts());
            cl->makeLearnt(glue);
            learnts.push_back(cl);
            break;
    }

    return cl;
}

ThreadControl::UsageStats ThreadControl::sumClauseData(
    const vector<Clause*>& toprint
    , const bool learnt
) const {
    vector<UsageStats> usageStats;
    vector<UsageStats> usageStatsGlue;

    //Reset stats
    UsageStats stats;

    for(vector<Clause*>::const_iterator
        it = toprint.begin()
        , end = toprint.end()
        ; it != end
        ; it++
    ) {
        //Clause data
        const Clause& cl = **it;
        const uint32_t clause_size = cl.size();

        //We have stats on this clause
        if (cl.size() == 3)
            continue;

        //Sum stats
        stats.num++;
        stats.sumPropConfl += cl.stats.numPropAndConfl;
        stats.sumLitVisited += cl.stats.numLitVisited;
        stats.sumLookedAt += cl.stats.numLookedAt;

        //Update size statistics
        if (usageStats.size() < cl.size() + 1U)
            usageStats.resize(cl.size()+1);

        usageStats[clause_size].num++;
        usageStats[clause_size].sumPropConfl += cl.stats.numPropAndConfl;
        usageStats[clause_size].sumLitVisited += cl.stats.numLitVisited;
        usageStats[clause_size].sumLookedAt += cl.stats.numLookedAt;

        //If learnt, sum up GLUE-based stats
        if (learnt) {
            const size_t glue = cl.stats.glue;
            assert(glue != std::numeric_limits<uint32_t>::max());
            if (usageStatsGlue.size() < glue + 1) {
                usageStatsGlue.resize(glue + 1);
            }

            usageStatsGlue[glue].num++;
            usageStatsGlue[glue].sumPropConfl += cl.stats.numPropAndConfl;
            usageStatsGlue[glue].sumLitVisited += cl.stats.numLitVisited;
            usageStatsGlue[glue].sumLookedAt += cl.stats.numLookedAt;
        }

        //If lots of verbosity, print clause's individual stat
        if (conf.verbosity >= 3) {
            //Print clause data
            cout
            << "Clause size " << std::setw(4) << cl.size();
            if (cl.learnt()) {
                cout << " glue : " << std::setw(4) << cl.stats.glue;
            }
            cout
            << " Props&confls: " << std::setw(10) << cl.stats.numPropAndConfl
            << " Lit visited: " << std::setw(10)<< cl.stats.numLitVisited
            << " Looked at: " << std::setw(10)<< cl.stats.numLookedAt
            << " Props&confls/Litsvisited*10: ";
            if (cl.stats.numLitVisited > 0) {
                cout
                << std::setw(6) << std::fixed << std::setprecision(4)
                << (10.0*(double)cl.stats.numPropAndConfl/(double)cl.stats.numLitVisited);
            }
            cout << endl;
        }
    }

    if (conf.verbosity >= 1) {
        //Print SUM stats
        if (learnt) {
            cout << "c Learnt    ";
        } else {
            cout << "c Non-learnt";
        }
        cout
        << " sum lits visited: "
        << std::setw(8) << stats.sumLitVisited/1000UL
        << " K";

        cout
        << " sum cls visited: "
        << std::setw(7) << stats.sumLookedAt/1000UL
        << " K";

        cout
        << " sum prop&conf: "
        << std::setw(6) << stats.sumPropConfl/1000UL
        << " K"
        << endl;
    }

    //Print more stats
    if (conf.verbosity >= 3) {
        printPropConflStats("clause-len", usageStats);

        if (learnt) {
            printPropConflStats("clause-glue", usageStatsGlue);
        }
    }

    return stats;
}

void ThreadControl::printPropConflStats(
    std::string name
    , const vector<UsageStats>& stats
) const {
    for(size_t i = 0; i < stats.size(); i++) {
        //Nothing to do here, no stats really
        if (stats[i].num == 0)
            continue;

        cout
        << name << " : " << std::setw(4) << i
        << " Avg. props&confls: " << std::setw(6) << std::fixed << std::setprecision(2)
        << ((double)stats[i].sumPropConfl/(double)stats[i].num);

        if (stats[i].sumLookedAt > 0) {
            cout
            << " Props&confls/looked at: " << std::setw(6) << std::fixed << std::setprecision(2)
            << ((double)stats[i].sumPropConfl/(double)stats[i].sumLookedAt);
        }

        cout
        << " Avg. lits visited: " << std::setw(6) << std::fixed << std::setprecision(2)
        << ((double)stats[i].sumLitVisited/(double)stats[i].num);

        if (stats[i].sumLookedAt > 0) {
            cout
            << " Lits visited/looked at: " << std::setw(6) << std::fixed << std::setprecision(2)
            << ((double)stats[i].sumLitVisited/(double)stats[i].sumLookedAt);
        }

        if (stats[i].sumLitVisited > 0) {
            cout
            << " Props&confls/Litsvisited*10: "
            << std::setw(6) << std::fixed << std::setprecision(4)
            << (10.0*(double)stats[i].sumPropConfl/(double)stats[i].sumLitVisited);
        }

        cout << endl;
    }
}

void ThreadControl::dumpIndividualPropConflStats(
    std::string name
    , const vector<UsageStats>& stats
    , const bool learnt
) const {
    //Generate file name
    std::stringstream ss;
    ss << "stats/propconflPERlitsvisited-stat-" << nbReduceDB << "-"
    << name << "-learnt-" << (int)learnt << ".txt";

    //Open file
    std::ofstream file(ss.str().c_str());
    if (!file) {
        cout << "Couldn't open file " << ss.str() << " for writing!" << endl;
        exit(-1);
    }

    //Dump stats per-glue/size line-by-line
    for(size_t i = 0; i < stats.size(); i++) {
        if (stats[i].sumLitVisited == 0)
            continue;

        file
        << i << "  "
        << (double)stats[i].sumPropConfl/(double)stats[i].sumLitVisited
        << endl;
    }

    //Generate file name
    std::stringstream ss2;
    ss2 << "stats/AVGpropconfl-stat-" << nbReduceDB << "-"
    << name << "-learnt-" << (int)learnt << ".txt";

    //Open file
    std::ofstream file2(ss2.str().c_str());
    if (!file2) {
        cout << "Couldn't open file " << ss2.str() << " for writing!" << endl;
        exit(-1);
    }

    //Dump stats per glue/size line-by-line
    for(size_t i = 0; i < stats.size(); i++) {
        if (stats[i].num == 0)
            continue;

        file2
        << i << "  "
        << (double)stats[i].sumPropConfl/(double)stats[i].num
        << endl;
    }
}

void ThreadControl::clearPropConfl(vector<Clause*>& clauseset)
{
    //Clear prop&confl for normal clauses
    for(vector<Clause*>::iterator
        it = clauseset.begin(), end = clauseset.end()
        ; it != end
        ; it++
    ) {
        (*it)->stats.numLitVisited = 0;
        (*it)->stats.numPropAndConfl = 0;
        (*it)->stats.numLookedAt = 0;
    }
}

void ThreadControl::fullReduce()
{
    if (conf.verbosity >= 1) {
        UsageStats stats;
        stats += sumClauseData(clauses, false);
        stats += sumClauseData(learnts, true);

        cout
        << "c sum            lits visited: "
        << std::setw(8) << stats.sumLitVisited/1000UL
        << " K";

        cout
        << " sum cls visited: "
        << std::setw(7) << stats.sumLookedAt/1000UL
        << " K";

        cout
        << " sum prop&conf: "
        << std::setw(6) << stats.sumPropConfl/1000UL
        << " K"
        << endl;
    }

    reduceDB();
    control->consolidateMem();

    if (conf.doClearPropConfEveryClauseCleaning) {
        clearPropConfl(clauses);
        clearPropConfl(learnts);
    }

    nextCleanLimit += nextCleanLimitInc;
    nextCleanLimitInc *= conf.increaseClean;
}

void ThreadControl::consolidateMem()
{
    clAllocator->consolidate(this, true);
}

void ThreadControl::printFullStats()
{
    std::cout << "----------------" << endl;
    sumSolvingStats.printSolvingStats(cpuTime(), propStats);
    std::cout << "----------------" << endl;

    double cpu_time = cpuTime();

    printStatsLine("c 0-depth assigns", trail.size()
        , (double)trail.size()/(double)nVars()*100.0
        , "% vars"
    );
    printStatsLine("c 0-depth assigns by thrds"
        , zeroLevAssignsByThreads
        , (double)zeroLevAssignsByThreads/(double)nVars()*100.0
        , "% vars"
    );
    printStatsLine("c 0-depth assigns by CNF"
        , zeroLevAssignsByCNF
        , (double)zeroLevAssignsByCNF/(double)nVars()*100.0
        , "% vars"
    );

    //Failed lit stats
    printStatsLine("c probing time"
                    , getTotalTimeFailedLitSearcher()
                    , getTotalTimeFailedLitSearcher()/cpu_time*100.0
                    , "% time");

    printStatsLine("c probing 0-depth-assigns"
                    , failedLitSearcher->getTotalZeroDepthAssigns()
                    , (double)failedLitSearcher->getTotalZeroDepthAssigns()/(double)nVars()*100.0
                    , "% vars");

    printStatsLine("c probing success rate"
                    , 100.0*(double)failedLitSearcher->getTotalNumFailed()/(double)failedLitSearcher->getTotalNumTried()
                    , "% of probes");

    printStatsLine("c probing visited"
                    , (double)failedLitSearcher->getTotalNumVisited()/(1000.0*1000.0)
                    , " M lits");

    printStatsLine("c probing failed"
                    , failedLitSearcher->getTotalNumFailed()
                    , (double)failedLitSearcher->getTotalNumFailed()/(double)nVars()*100.0
                    , "% vars");

    printStatsLine("c probing bin added", failedLitSearcher->getTotalAddedBin());
    printStatsLine("c probing bin rem", failedLitSearcher->getTotalRemovedBin());


    //Subsumer stats
    printStatsLine("c SatELite time"
                    , subsumer->getStats().totalTime()
                    , subsumer->getStats().totalTime()/cpu_time*100.0
                    , "% time");
    printStatsLine("c SatELite v-elimed"
                    , subsumer->getStats().numVarsElimed
                    , (double)subsumer->getStats().numVarsElimed/(double)nVars()*100.0
                    , "% vars");
    printStatsLine("c SatELite 0-detph assigns"
                    , subsumer->getStats().zeroDepthAssings
                    , (double)subsumer->getStats().zeroDepthAssings/(double)nVars()*100.0
                    , "% vars");
    printStatsLine("c SatELite lit-rem", subsumer->getStats().litsRemStrengthen);
    printStatsLine("c SatELite cl-subs", subsumer->getStats().clauses_subsumed);
    printStatsLine("c SatELite blocked", subsumer->getStats().blocked);
    printStatsLine("c SatELite asymmSub", subsumer->getStats().asymmSubs);
    printStatsLine("c SatELite elim-bin-lt-cl", subsumer->getStats().binLearntClRemThroughElim);
    printStatsLine("c SatELite elim-long-lt-cl", subsumer->getStats().longLearntClRemThroughElim);

    //GateFinder stats
    printStatsLine("c gatefinder time"
                    , subsumer->getGateFinder()->getStats().totalTime()
                    , subsumer->getGateFinder()->getStats().totalTime()/cpu_time*100.0
                    , "% time");
    printStatsLine("c gatefinder cl-short"
        , subsumer->getGateFinder()->getStats().orGateUseful);
    printStatsLine("c gatefinder litsrem"
        , subsumer->getGateFinder()->getStats().litsRem);
    printStatsLine("c gatefinder cl-rem"
        , subsumer->getGateFinder()->getStats().andGateUseful);

    //XOR stats
    printStatsLine("c XOR time"
        , subsumer->getXorFinder()->getStats().totalTime());
    printStatsLine("c XOR avg num"
        , (double)subsumer->getXorFinder()->getStats().foundXors/(double)subsumer->getXorFinder()->getNumCalls()
        , (double)subsumer->getXorFinder()->getStats().sumSizeXors/(double)subsumer->getXorFinder()->getStats().foundXors
        , "avg size");
    printStatsLine("c XOR unit found"
        , subsumer->getXorFinder()->getStats().newUnits);
    printStatsLine("c XOR 0-depth assings"
        , subsumer->getXorFinder()->getStats().zeroDepthAssigns);
    printStatsLine("c XOR bin found"
        , subsumer->getXorFinder()->getStats().newBins);

    //VarReplacer stats
    printStatsLine("c EqLit find time"
        , getTotalTimeSCC()
        , getTotalTimeSCC()/cpu_time*100.0
        , "% time"
    );
    printStatsLine("c EqLit replace time"
        , varReplacer->getTotalTime()
        , varReplacer->getTotalTime()/cpu_time*100.0
        , "% time"
    );

    printStatsLine("c EqLit tree roots", getNumXorTrees());
    printStatsLine("c EqLit trees' crown"
                    , getNumXorTreesCrownSize()
                    , (double)getNumXorTreesCrownSize()/(double)getNumXorTrees()
                    , "leafs/tree");
    printStatsLine("c EqLit trees' crown"
                    , getNumXorTreesCrownSize()
                    , 100.0*(double)getNumXorTreesCrownSize()/(double)nVars()
                    , "% of vars");
    printStatsLine("c EqLit 0-depth assigns"
        , varReplacer->getTotalZeroDepthAssigns()
        , (double)varReplacer->getTotalZeroDepthAssigns()/(double)nVars()*100.0
        , "% vars"
    );
    printStatsLine("c EqLit lits replaced", varReplacer->getTotalReplacedLits());

    //Vivifier-ASYMM stats
    printStatsLine("c Asymm time"
                    , clauseVivifier->getTotalTimeAsymm()
                    , clauseVivifier->getTotalTimeAsymm()/cpu_time*100.0
                    , "% time");
    printStatsLine("c Asymm 0-depth assings", clauseVivifier->getTotalZeroDepthAssignsAsymm());
    printStatsLine("c Asymm lits rem", clauseVivifier->getTotalNumLitsRemAsymm());

    //Vivifier-cache-based non-learnt stats
    printStatsLine("c cache-vivify-NL time"
                    , clauseVivifier->getTotalTimeCacheNonLearnt()
                    , clauseVivifier->getTotalTimeCacheNonLearnt()/cpu_time*100.0
                    , "% time");
    printStatsLine("c cache-vivify-NL lits rem", clauseVivifier->getTotalNumLitsRemCacheNonLearnt());
    printStatsLine("c cache-vivify-NL cl subs", clauseVivifier->getTotalNumClSubsumedCacheNonLearnt());

    //Vivifier-cache-based learnt stats
    printStatsLine("c cache-vivify-L time"
                    , clauseVivifier->getTotalTimeCacheLearnt()
                    , clauseVivifier->getTotalTimeCacheLearnt()/cpu_time*100.0
                    , "% time");
    printStatsLine("c cache-vivify-L lits rem", clauseVivifier->getTotalNumLitsRemCacheLearnt());
    printStatsLine("c cache-vivify-L cl subs", clauseVivifier->getTotalNumClSubsumedCacheLearnt());


    //Other stats
    printStatsLine("c Conflicts"
        , sumSolvingStats.numConflicts, (double)sumSolvingStats.numConflicts/cpu_time
        , "confl/sec"
    );
    printStatsLine("c Total time", cpu_time);
}

template<class T, class T2>
void ThreadControl::printStatsLine(std::string left, T value, T2 value2, std::string extra)
{
    cout
    << std::fixed << std::left << std::setw(27) << left
    << ": " << std::setw(11) << std::setprecision(2) << value
    << " (" << std::left << std::setw(9) << std::setprecision(2) << value2 << " "
    << extra << ")"
    << std::right
    << endl;
}

template<class T>
void ThreadControl::printStatsLine(std::string left, T value, std::string extra)
{
    cout
    << std::fixed << std::left << std::setw(27) << left
    << ": " << std::setw(11) << std::setprecision(2) << value
    << extra
    << std::right
    << endl;
}

void ThreadControl::dumpBinClauses(const bool alsoLearnt, const bool alsoNonLearnt, std::ostream& outfile) const
{
    uint32_t wsLit = 0;
    for (vector<vec<Watched> >::const_iterator it = watches.begin(), end = watches.end(); it != end; it++, wsLit++) {
        Lit lit = ~Lit::toLit(wsLit);
        const vec<Watched>& ws = *it;
        for (vec<Watched>::const_iterator it2 = ws.begin(), end2 = ws.end(); it2 != end2; it2++) {
            if (it2->isBinary() && lit < it2->getOtherLit()) {
                bool toDump = false;
                if (it2->getLearnt() && alsoLearnt) toDump = true;
                if (!it2->getLearnt() && alsoNonLearnt) toDump = true;

                if (toDump)
                    outfile << it2->getOtherLit() << " " << lit << " 0" << endl;
            }
        }
    }
}

void ThreadControl::printClauseSizeDistrib()
{
    size_t size3 = 0;
    size_t size4 = 0;
    size_t size5 = 0;
    size_t sizeLarge = 0;
    for(vector<Clause*>::const_iterator it = clauses.begin(), end = clauses.end(); it != end; it++) {
        switch((*it)->size()) {
            case 0:
            case 1:
            case 2:
                assert(false);
                break;
            case 3:
                size3++;
                break;
            case 4:
                size4++;
                break;
            case 5:
                size5++;
                break;
            default:
                sizeLarge++;
                break;
        }
    }

    for(vector<Clause*>::const_iterator it = learnts.begin(), end = learnts.end(); it != end; it++) {
        switch((*it)->size()) {
            case 0:
            case 1:
            case 2:
                assert(false);
                break;
            case 3:
                size3++;
                break;
            case 4:
                size4++;
                break;
            case 5:
                size5++;
                break;
            default:
                sizeLarge++;
                break;
        }
    }

    cout << "c size3: " << size3
    << " size4: " << size4
    << " size5: " << size5
    << " larger: " << sizeLarge << endl;
}

void ThreadControl::dumpLearnts(std::ostream& os, const uint32_t maxSize)
{
    os
    << "c " << endl
    << "c ---------" << endl
    << "c unitaries" << endl
    << "c ---------" << endl;
    for (uint32_t i = 0, end = (trail_lim.size() > 0) ? trail_lim[0] : trail.size() ; i < end; i++) {
        os << trail[i] << " 0" << endl;    }


    os
    << "c " << endl
    << "c ---------------------------------" << endl
    << "c learnt binary clauses (extracted from watchlists)" << endl
    << "c ---------------------------------" << endl;
    if (maxSize >= 2)
        dumpBinClauses(true, false, os);

    os
    << "c " << endl
    << "c ---------------------------------------" << endl
    << "c clauses representing 2-long XOR clauses" << endl
    << "c ---------------------------------------" << endl;
    if (maxSize >= 2) {
        const vector<Lit>& table = varReplacer->getReplaceTable();
        for (Var var = 0; var != table.size(); var++) {
            Lit lit = table[var];
            if (lit.var() == var)
                continue;

            os << (~lit) << " " << Lit(var, false) << " 0" << endl;
            os << lit << " " << Lit(var, true) << " 0" << endl;
        }
    }

    os
    << "c " << endl
    << "c --------------------" << endl
    << "c clauses from learnts" << endl
    << "c --------------------" << endl;
    for (int i = learnts.size()-1; i >= 0 ; i--) {
        Clause& cl = *learnts[i];
        if (cl.size() <= maxSize) {
            os << cl << " 0" << endl;
            os
            << "c clause learnt "
            << (cl.learnt() ? "yes" : "no")
            << " stats "  << cl.stats << endl;
        }
    }
}

void ThreadControl::dumpOrigClauses(std::ostream& os) const
{
    uint32_t numClauses = 0;
    //unitary clauses
    for (uint32_t i = 0, end = (trail_lim.size() > 0) ? trail_lim[0] : trail.size() ; i < end; i++)
        numClauses++;

    //binary XOR clauses
    const vector<Lit>& table = varReplacer->getReplaceTable();
    for (Var var = 0; var != table.size(); var++) {
        Lit lit = table[var];
        if (lit.var() == var)
            continue;
        numClauses += 2;
    }

    //binary normal clauses
    numClauses += countNumBinClauses(false, true);

    //normal clauses
    numClauses += clauses.size();

    //previously eliminated clauses
    const vector<BlockedClause>& blockedClauses = subsumer->getBlockedClauses();
    numClauses += blockedClauses.size();

    os << "p cnf " << nVars() << " " << numClauses << endl;

    ////////////////////////////////////////////////////////////////////

    os
    << "c " << endl
    << "c ---------" << endl
    << "c unitaries" << endl
    << "c ---------" << endl;
    for (uint32_t i = 0, end = (trail_lim.size() > 0) ? trail_lim[0] : trail.size() ; i < end; i++) {
        os << trail[i] << " 0" << endl;
    }

    os
    << "c " << endl
    << "c ---------------------------------------" << endl
    << "c clauses representing 2-long XOR clauses" << endl
    << "c ---------------------------------------" << endl;
    for (Var var = 0; var != table.size(); var++) {
        Lit lit = table[var];
        if (lit.var() == var)
            continue;

        Lit litP1 = ~lit;
        Lit litP2 = Lit(var, false);
        os << litP1 << " " << litP2 << endl;
        os << ~litP1 << " " << ~litP2 << endl;
    }

    os
    << "c " << endl
    << "c ---------------" << endl
    << "c binary clauses" << endl
    << "c ---------------" << endl;
    dumpBinClauses(false, true, os);

    os
    << "c " << endl
    << "c ---------------" << endl
    << "c normal clauses" << endl
    << "c ---------------" << endl;
    for (vector<Clause*>::const_iterator i = clauses.begin(); i != clauses.end(); i++) {
        assert(!(*i)->learnt());
        os << (**i) << " 0" << endl;
    }

    os
    << "c " << endl
    << "c -------------------------------" << endl
    << "c previously eliminated variables" << endl
    << "c -------------------------------" << endl;
    for (vector<BlockedClause>::const_iterator it = blockedClauses.begin(); it != blockedClauses.end(); it++) {
        os << "c next clause is eliminated/blocked on lit " << it->blockedOn << endl;
        os << it->lits << " 0" << endl;
    }
}

void ThreadControl::printAllClauses() const
{
    for (uint32_t i = 0; i < clauses.size(); i++) {
        cout
                << "Normal clause num " << clAllocator->getOffset(clauses[i])
                << " cl: " << *clauses[i] << endl;
    }

    uint32_t wsLit = 0;
    for (vector<vec<Watched> >::const_iterator it = watches.begin(), end = watches.end(); it != end; it++, wsLit++) {
        Lit lit = ~Lit::toLit(wsLit);
        const vec<Watched>& ws = *it;
        cout << "watches[" << lit << "]" << endl;
        for (vec<Watched>::const_iterator it2 = ws.begin(), end2 = ws.end(); it2 != end2; it2++) {
            if (it2->isBinary()) {
                cout << "Binary clause part: " << lit << " , " << it2->getOtherLit() << endl;
            } else if (it2->isClause()) {
                cout << "Normal clause num " << it2->getNormOffset() << endl;
            } else if (it2->isTriClause()) {
                cout << "Tri clause:"
                << lit << " , "
                << it2->getOtherLit() << " , "
                << it2->getOtherLit2() << endl;
            }
        }
    }
}

bool ThreadControl::verifyBinClauses() const
{
    uint32_t wsLit = 0;
    for (vector<vec<Watched> >::const_iterator it = watches.begin(), end = watches.end(); it != end; it++, wsLit++) {
        Lit lit = ~Lit::toLit(wsLit);
        const vec<Watched>& ws = *it;

        for (vec<Watched>::const_iterator i = ws.begin(), end = ws.end() ; i != end; i++) {
            if (i->isBinary()
                && modelValue(lit) != l_True
                && modelValue(i->getOtherLit()) != l_True
            ) {
                cout << "bin clause: " << lit << " , " << i->getOtherLit() << " not satisfied!" << endl;
                cout << "value of unsat bin clause: " << value(lit) << " , " << value(i->getOtherLit()) << endl;
                return false;
            }
        }
    }

    return true;
}

bool ThreadControl::verifyClauses(const vector<Clause*>& cs) const
{
    #ifdef VERBOSE_DEBUG
    cout << "Checking clauses whether they have been properly satisfied." << endl;;
    #endif

    bool verificationOK = true;

    for (uint32_t i = 0; i != cs.size(); i++) {
        Clause& c = *cs[i];
        for (uint32_t j = 0; j < c.size(); j++)
            if (modelValue(c[j]) == l_True)
                goto next;

            cout << "unsatisfied clause: " << *cs[i] << endl;
        verificationOK = false;
        next:
                ;
    }

    return verificationOK;
}

bool ThreadControl::verifyModel() const
{
    bool verificationOK = true;
    verificationOK &= verifyClauses(clauses);
    verificationOK &= verifyClauses(learnts);
    verificationOK &= verifyBinClauses();

    if (conf.verbosity >= 1 && verificationOK) {
        cout
        << "c Verified " <<  clauses.size() << " clauses."
        << endl;
    }

    return verificationOK;
}


void ThreadControl::checkLiteralCount() const
{
    // Check that sizes are calculated correctly:
    uint64_t cnt = 0;
    for (uint32_t i = 0; i != clauses.size(); i++)
        cnt += clauses[i]->size();

    if (clausesLits != cnt) {
        cout << "c ERROR! literal count: " << clausesLits << " , real value = " <<  cnt << endl;
        assert(clausesLits == cnt);
    }
}

uint32_t ThreadControl::getNumXorTrees() const
{
    return varReplacer->getNumTrees();
}

uint32_t ThreadControl::getNumXorTreesCrownSize() const
{
    return varReplacer->getNumReplacedVars();
}

double ThreadControl::getTotalTimeFailedLitSearcher() const
{
    return failedLitSearcher->getTotalTime();
}

double ThreadControl::getTotalTimeSCC() const
{
    return  sCCFinder->getTotalTime();
}

uint32_t ThreadControl::getNumUnsetVars() const
{
    assert(decisionLevel() == 0);
    return (decision_var.size() - trail.size());
}

uint32_t ThreadControl::getNumDecisionVars() const
{
    return numDecisionVars;
}

void ThreadControl::setNeedToInterrupt()
{
    CommandControl::setNeedToInterrupt();

    needToInterrupt = true;
}

lbool ThreadControl::modelValue (const Lit p) const
{
    return model[p.var()] ^ p.sign();
}

void ThreadControl::testAllClauseAttach() const
{
#ifndef DEBUG_ATTACH_MORE
    return;
#endif

    for (vector<Clause*>::const_iterator it = clauses.begin(), end = clauses.end(); it != end; it++) {
        assert(normClauseIsAttached(**it));
    }
}

bool ThreadControl::normClauseIsAttached(const Clause& c) const
{
    bool attached = true;
    assert(c.size() > 2);

    ClauseOffset offset = clAllocator->getOffset(&c);
    if (c.size() == 3) {
        //The clause might have been longer, and has only recently
        //became 3-long. Check, and detach accordingly
        if (findWCl(watches[(~c[0]).toInt()], offset)) goto fullClause;

        Lit lit1 = c[0];
        Lit lit2 = c[1];
        Lit lit3 = c[2];
        attached &= findWTri(watches[(~lit1).toInt()], lit2, lit3);
        attached &= findWTri(watches[(~lit2).toInt()], lit1, lit3);
        attached &= findWTri(watches[(~lit3).toInt()], lit1, lit2);
    } else {
        fullClause:
        attached &= findWCl(watches[(~c[0]).toInt()], offset);
        attached &= findWCl(watches[(~c[1]).toInt()], offset);
    }

    return attached;
}

void ThreadControl::findAllAttach() const
{
    for (uint32_t i = 0; i < watches.size(); i++) {
        const Lit lit = ~Lit::toLit(i);
        for (uint32_t i2 = 0; i2 < watches[i].size(); i2++) {
            const Watched& w = watches[i][i2];
            if (!w.isClause())
                continue;

            //Get clause
            Clause* cl = clAllocator->getPointer(w.getNormOffset());
            assert(!cl->getFreed());
            cout << (*cl) << endl;

            //Assert watch correctness
            if ((*cl)[0] != lit
                && (*cl)[1] != lit
            ) {
                cout
                << "ERROR! Clause " << (*cl)
                << " not attached?"
                << endl;
            }

            //Clause in one of the lists
            if (!findClause(cl)) {
                cout << "ERROR! did not find clause!" << endl;
            }
        }
    }
}


bool ThreadControl::findClause(const Clause* c) const
{
    for (uint32_t i = 0; i < clauses.size(); i++) {
        if (clauses[i] == c) return true;
    }
    for (uint32_t i = 0; i < learnts.size(); i++) {
        if (learnts[i] == c) return true;
    }

    return false;
}

void ThreadControl::checkNoWrongAttach() const
{
    #ifndef VERBOSE_DEBUG
    return;
    #endif //VERBOSE_DEBUG

    for (vector<Clause*>::const_iterator i = learnts.begin(), end = learnts.end(); i != end; i++) {

        const Clause& cl = **i;
        for (uint32_t i = 0; i < cl.size(); i++) {
            if (i > 0) assert(cl[i-1].var() != cl[i].var());
        }
    }
}

uint32_t ThreadControl::getNumFreeVars() const
{
    assert(decisionLevel() == 0);
    uint32_t freeVars = nVars();
    freeVars -= trail.size();
    freeVars -= subsumer->getStats().numVarsElimed;
    freeVars -= varReplacer->getNumReplacedVars();

    return freeVars;
}

uint32_t ThreadControl::getNumFreeVarsAdv(size_t trail_size_of_thread) const
{
    assert(decisionLevel() == 0);
    uint32_t freeVars = nVars();
    freeVars -= trail_size_of_thread;
    freeVars -= subsumer->getStats().numVarsElimed;
    freeVars -= varReplacer->getNumReplacedVars();

    return freeVars;
}

void ThreadControl::printClauseStats()
{
    cout
    << " " << std::setw(7) << clauses.size()
    << " " << std::setw(4) << std::fixed << std::setprecision(1)
    << (double)(clausesLits - numBinsNonLearnt*2)/(double)(clauses.size())

    << " " << std::setw(7) << learnts.size()
    << " " << std::setw(4) << std::fixed << std::setprecision(1)
    << (double)(learntsLits - numBinsLearnt*2)/(double)(learnts.size())

    << " " << std::setw(6) << numBinsNonLearnt
    << " " << std::setw(6) << numBinsLearnt
    ;
}

void ThreadControl::checkStats() const
{
    //If in crazy mode, don't check
    #ifdef NDEBUG
    return;
    #endif

    //Check number of learnt & non-learnt binary clauses
    uint64_t thisNumLearntBins = 0;
    uint64_t thisNumNonLearntBins = 0;
    for(vector<vec<Watched> >::const_iterator
        it = watches.begin(), end = watches.end()
        ; it != end
        ; it++
    ) {
        const vec<Watched>& ws = *it;
        for(vec<Watched>::const_iterator
            it2 = ws.begin(), end2 = ws.end()
            ; it2 != end2
            ; it2++
        ) {
            if (it2->isBinary()) {
                if (it2->getLearnt())
                    thisNumLearntBins++;
                else
                    thisNumNonLearntBins++;
            }
        }
    }

    assert(thisNumLearntBins/2 == numBinsLearnt);
    assert(thisNumNonLearntBins/2 == numBinsNonLearnt);

    //Check number of non-learnt literals
    uint64_t numLitsNonLearnt = numBinsNonLearnt*2;
    for(vector<Clause*>::const_iterator
        it = clauses.begin(), end = clauses.end()
        ; it != end
        ; it++
    ) {
        numLitsNonLearnt += (*it)->size();
    }
    assert(numLitsNonLearnt == clausesLits);

    //Check number of learnt literals
    uint64_t numLitsLearnt = numBinsLearnt*2;
    for(vector<Clause*>::const_iterator
        it = learnts.begin(), end = learnts.end()
        ; it != end
        ; it++
    ) {
        numLitsLearnt += (*it)->size();
    }
    assert(numLitsLearnt == learntsLits);

}

uint32_t ThreadControl::getNewToReplaceVars() const
{
    return varReplacer->getNewToReplaceVars();
}

const char* ThreadControl::getVersion()
{
    return get_git_version();
}
