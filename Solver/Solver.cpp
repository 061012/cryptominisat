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

#include "Clause.h"
#include "time_mem.h"

#include "VarReplacer.h"
#include "FindUndef.h"
#include "XorFinder.h"
#include "ClauseCleaner.h"
#include "RestartTypeChooser.h"
#include "FailedLitSearcher.h"
#include "Subsumer.h"
#include "PartHandler.h"
#include "XorSubsumer.h"
#include "StateSaver.h"
#include "SCCFinder.h"
#include "SharedData.h"
#include "ClauseVivifier.h"
#include "Gaussian.h"
#include "MatrixFinder.h"
#include "DataSync.h"

#ifdef VERBOSE_DEBUG
#define UNWINDING_DEBUG
#define VERBOSE_DEBUG_GATE
#endif


//#define DEBUG_UNCHECKEDENQUEUE_LEVEL0
//#define VERBOSE_DEBUG_POLARITIES
//#define DEBUG_DYNAMIC_RESTART
//#define UNWINDING_DEBUG

//=================================================================================================
// Constructor/Destructor:


/**
@brief Sets a sane default config and allocates handler classes
*/
Solver::Solver(const SolverConf& _conf, const GaussConf& _gaussconfig, SharedData* sharedData) :
        // Parameters: (formerly in 'SearchParams')
        conf(_conf)
        , needToInterrupt  (false)
        , gaussconfig(_gaussconfig)
        #ifdef USE_GAUSS
        , sum_gauss_called (0)
        , sum_gauss_confl  (0)
        , sum_gauss_prop   (0)
        , sum_gauss_unit_truths (0)
        #endif //USE_GAUSS

        // Stats
        , starts(0), dynStarts(0), staticStarts(0), fullStarts(0), decisions(0), rnd_decisions(0)
        , propagations(0)
        , bogoProps(0)
        , conflicts(0)
        , clauses_literals(0), learnts_literals(0), max_literals(0), tot_literals(0)
        , nbGlue2(0), numNewBin(0), lastNbBin(0), lastSearchForBinaryXor(0), nbReduceDB(0)
        , improvedClauseNo(0), improvedClauseSize(0)
        , numShrinkedClause(0), numShrinkedClauseLits(0)
        , moreRecurMinLDo(0)
        , updateTransCache(0)
        , nbClOverMaxGlue(0)
        , OTFGateRemLits(0)
        , OTFGateRemSucc(0)

        , ok               (true)
        , numBins          (0)
        , qhead            (0)
        , mtrand           ((unsigned long int)0)

        //variables
        , order_heap       (VarOrderLt(varData))
        , var_inc          (128)

        //learnts
        , numCleanedLearnts(1)
        , nbClBeforeRed    (NBCLAUSESBEFOREREDUCE)
        , nbCompensateSubsumer (0)

        , MYFLAG           (0)
        #ifdef STATS_NEEDED
        , logger(conf.verbosity)
        , dynamic_behaviour_analysis(false) //do not document the proof as default
        #endif
        , learnt_clause_group(0)
        , libraryCNFFile   (NULL)
        , lastDelayedEnqueueUpdate (0)
        , lastDelayedEnqueueUpdateLevel (0)
        , restartType      (static_restart)
        , subRestartType   (static_restart)
        , agility          (conf.agilityG)
        , simplifying      (false)
        , totalSimplifyTime(0.0)
        , numSimplifyRounds(0)
        , simpDB_assigns   (-1)
        , simpDB_props     (0)
{
    mtrand.seed(conf.origSeed);
    assert(conf.maxGlue < MAX_THEORETICAL_GLUE);
    varReplacer = new VarReplacer(*this);
    clauseCleaner = new ClauseCleaner(*this);
    failedLitSearcher = new FailedLitSearcher(*this);
    partHandler = new PartHandler(*this);
    subsumer = new Subsumer(*this);
    xorSubsumer = new XorSubsumer(*this);
    restartTypeChooser = new RestartTypeChooser(*this);
    sCCFinder = new SCCFinder(*this);
    clauseVivifier = new ClauseVivifier(*this);
    matrixFinder = new MatrixFinder(*this);
    dataSync = new DataSync(*this, sharedData);

    #ifdef STATS_NEEDED
    logger.setSolver(this);
    #endif
}

/**
@brief Frees clauses and frees all allocated hander classes
*/
Solver::~Solver()
{
    clearGaussMatrixes();
    delete matrixFinder;
    delete varReplacer;
    delete clauseCleaner;
    delete failedLitSearcher;
    delete partHandler;
    delete subsumer;
    delete xorSubsumer;
    delete restartTypeChooser;

    if (libraryCNFFile)
        fclose(libraryCNFFile);
}

//=================================================================================================
// Minor methods:


/**
@brief Creates a new SAT variable in the solver

This entails making the datastructures large enough to fit the new variable
in all internal datastructures as well as all datastructures used in
classes used inside Solver

@p dvar The new variable should be used as a decision variable?
   NOTE: this has effects on the meaning of a SATISFIABLE result
*/
Var Solver::newVar(bool dvar)
{
    Var v = nVars();
    watches   .push();          // (list for positive literal)
    watches   .push();          // (list for negative literal)
    reason    .push(PropBy());
    assigns   .push(l_Undef);
    varData.push_back(VarData());
    binPropData.push();
    seen      .push(0);
    seen      .push(0);
    permDiff  .push(0);
    unWindGlue.push(NULL);

    //Transitive OTF self-subsuming resolution
    seen2     .push(0);
    seen2     .push(0);
    transOTFCache.push_back(TransCache());
    transOTFCache.push_back(TransCache());
    litReachable.push_back(LitReachData());
    litReachable.push_back(LitReachData());

    //Variable heap, long-term polarity count
    decision_var.push_back(dvar);
    insertVarOrder(v);
    lTPolCount.push_back(std::make_pair(0,0));

    varReplacer->newVar();
    partHandler->newVar();
    subsumer->newVar();
    xorSubsumer->newVar();
    dataSync->newVar();

    insertVarOrder(v);

    #ifdef STATS_NEEDED
    if (dynamic_behaviour_analysis)
        logger.new_var(v);
    #endif

    if (libraryCNFFile)
        fprintf(libraryCNFFile, "c Solver::newVar() called\n");

    return v;
}

/**
@brief Adds an xor clause to the problem

Should ONLY be called from internally. This is different from the extenal
xor clause-adding function addXorClause() in that it assumes that the variables
inside are decision variables, have not been replaced, eliminated, etc.
*/
template<class T>
XorClause* Solver::addXorClauseInt(T& ps, bool xorEqualFalse, const uint32_t group, const bool learnt)
{
    assert(qhead == trail.size());
    assert(decisionLevel() == 0);

    if (ps.size() > std::numeric_limits<uint16_t>::max()) {
        std::cout << "Too long clause!" << std::endl;
        exit(-1);
    }
    std::sort(ps.getData(), ps.getDataEnd());
    Lit p;
    uint32_t i, j;
    for (i = j = 0, p = lit_Undef; i != ps.size(); i++) {
        ps[i] = ps[i].unsign();
        if (ps[i].var() == p.var()) {
            //added, but easily removed
            j--;
            p = lit_Undef;
            if (!assigns[ps[i].var()].isUndef())
                xorEqualFalse ^= assigns[ps[i].var()].getBool();
        } else if (assigns[ps[i].var()].isUndef()) { //just add
            ps[j++] = p = ps[i];
            assert(!subsumer->getVarElimed()[p.var()]);
            assert(!xorSubsumer->getVarElimed()[p.var()]);
        } else //modify xorEqualFalse instead of adding
            xorEqualFalse ^= (assigns[ps[i].var()].getBool());
    }
    ps.shrink(i - j);

    switch(ps.size()) {
        case 0: {
            if (!xorEqualFalse) ok = false;
            return NULL;
        }
        case 1: {
            uncheckedEnqueue(Lit(ps[0].var(), xorEqualFalse));
            ok = (propagate<true>().isNULL());
            return NULL;
        }
        case 2: {
            #ifdef VERBOSE_DEBUG
            cout << "--> xor is 2-long, replacing var " << ps[0].var()+1 << " with " << (!xorEqualFalse ? "-" : "") << ps[1].var()+1 << endl;
            #endif

            ps[0] = ps[0].unsign();
            ps[1] = ps[1].unsign();
            varReplacer->replace(ps, xorEqualFalse, group);
            return NULL;
        }
        default: {
            assert(!learnt);
            XorClause* c = clauseAllocator.XorClause_new(ps, xorEqualFalse, group);
            attachClause(*c);
            return c;
        }
    }
}

template XorClause* Solver::addXorClauseInt(vec<Lit>& ps, bool xorEqualFalse, const uint32_t group, const bool learnt);
template XorClause* Solver::addXorClauseInt(XorClause& ps, bool xorEqualFalse, const uint32_t group, const bool learnt);

/**
@brief Adds an xor clause to the problem

Calls addXorClauseInt() for the heavy-lifting. Basically, this does a bit
of debug-related stuff (see "libraryCNFFile"), and then checks if any of the
variables have been eliminated, replaced, etc. If so, it treats it correctly,
and then calls addXorClauseInt() to actually add the xor clause.

@p ps[inout] The VARIABLES in the xor clause. Beware, there must be NO signs
            here: ALL must be unsigned (.sign() == false). Values passed here
            WILL be changed, ordered, removed, etc!
@p xorEqualFalse The xor must be equal to TRUE or false?
*/
template<class T>
bool Solver::addXorClause(T& ps, bool xorEqualFalse, const uint32_t group, const char* group_name)
{
    assert(decisionLevel() == 0);
    if (ps.size() > (0x01UL << 18)) {
        std::cout << "Too long clause!" << std::endl;
        exit(-1);
    }

    if (libraryCNFFile) {
        fprintf(libraryCNFFile, "x");
        for (uint32_t i = 0; i < ps.size(); i++) ps[i].print(libraryCNFFile);
        fprintf(libraryCNFFile, "0\n");
    }

    #ifdef STATS_NEEDED
    if (dynamic_behaviour_analysis) {
        logger.set_group_name(group, group_name);
        learnt_clause_group = std::max(group+1, learnt_clause_group);
    }
    #endif

    if (!ok)
        return false;
    assert(qhead == trail.size());
    #ifndef NDEBUG
    for (Lit *l = ps.getData(), *end = ps.getDataEnd(); l != end; l++) {
        assert(l->var() < nVars() && "Clause inserted, but variable inside has not been declared with newVar()!");
    }
    #endif

    if (varReplacer->getNumLastReplacedVars() || subsumer->getNumElimed() || xorSubsumer->getNumElimed()) {
        for (uint32_t i = 0; i != ps.size(); i++) {
            Lit otherLit = varReplacer->getReplaceTable()[ps[i].var()];
            if (otherLit.var() != ps[i].var()) {
                ps[i] = Lit(otherLit.var(), false);
                xorEqualFalse ^= otherLit.sign();
            }
            if (subsumer->getVarElimed()[ps[i].var()] && !subsumer->unEliminate(ps[i].var()))
                return false;
            else if (xorSubsumer->getVarElimed()[ps[i].var()] && !xorSubsumer->unEliminate(ps[i].var()))
                return false;
        }
    }

    XorClause* c = addXorClauseInt(ps, xorEqualFalse, group);
    if (c != NULL) xorclauses.push(c);

    return ok;
}

template bool Solver::addXorClause(vec<Lit>& ps, bool xorEqualFalse, const uint32_t group, const char* group_name);
template bool Solver::addXorClause(XorClause& ps, bool xorEqualFalse, const uint32_t group, const char* group_name);

/**
@brief Adds a clause to the problem. Should ONLY be called internally

This code is very specific in that it must NOT be called with varibles in
"ps" that have been replaced, eliminated, etc. Also, it must not be called
when the solver is in an UNSAT (!ok) state, for example. Use it carefully,
and only internally
*/
template <class T>
Clause* Solver::addClauseInt(T& ps, uint32_t group
                            , const bool learnt, const uint32_t glue
                            , const bool inOriginalInput, const bool attach)
{
    assert(ok);

    std::sort(ps.getData(), ps.getDataEnd());
    Lit p = lit_Undef;
    uint32_t i, j;
    for (i = j = 0; i != ps.size(); i++) {
        if (value(ps[i]).getBool() || ps[i] == ~p)
            return NULL;
        else if (value(ps[i]) != l_False && ps[i] != p) {
            ps[j++] = p = ps[i];
            assert(!subsumer->getVarElimed()[p.var()]);
            assert(!xorSubsumer->getVarElimed()[p.var()]);
        }
    }
    ps.shrink(i - j);

    if (ps.size() == 0) {
        ok = false;
        return NULL;
    } else if (ps.size() == 1) {
        uncheckedEnqueue(ps[0]);
        ok = (propagate<true>().isNULL());
        return NULL;
    }

    //Randomise clause
    assert(ps.size() >= 2);
    for(uint32_t i2 = 0; i2 < (uint32_t)(ps.size()-1); i2++) {
        uint32_t r = mtrand.randInt(ps.size()-i2-1);
        std::swap(ps[i2], ps[i2+r]);
    }


    if (ps.size() > 2) {
        Clause* c = clauseAllocator.Clause_new(ps, group);
        if (learnt) c->makeLearnt(glue);
        if (attach) attachClause(*c);
        if (ps.size() == 3 && !inOriginalInput) dataSync->signalNewTriClause(ps, learnt);
        return c;
    } else {
        attachBinClause(ps[0], ps[1], learnt);
        if (!inOriginalInput) dataSync->signalNewBinClause(ps, learnt);
        numNewBin++;
        return NULL;
    }
}

template Clause* Solver::addClauseInt(Clause& ps, const uint32_t group, const bool learnt, const uint32_t glue, const bool inOriginalInput, const bool attach);
template Clause* Solver::addClauseInt(vec<Lit>& ps, const uint32_t group, const bool learnt, const uint32_t glue, const bool inOriginalInput, const bool attach);

template<class T> const bool Solver::addClauseHelper(T& ps, const uint32_t group, const char* group_name)
{
    assert(decisionLevel() == 0);
    if (ps.size() > (0x01UL << 18)) {
        std::cout << "Too long clause!" << std::endl;
        exit(-1);
    }

    if (libraryCNFFile) {
        for (uint32_t i = 0; i != ps.size(); i++) ps[i].print(libraryCNFFile);
        fprintf(libraryCNFFile, "0\n");
    }

    #ifdef STATS_NEEDED
    if (dynamic_behaviour_analysis) {
        logger.set_group_name(group, group_name);
        learnt_clause_group = std::max(group+1, learnt_clause_group);
    }
    #endif

    if (!ok) return false;
    assert(qhead == trail.size());
    #ifndef NDEBUG
    for (Lit *l = ps.getData(), *end = ps.getDataEnd(); l != end; l++) {
        assert(l->var() < nVars() && "Clause inserted, but variable inside has not been declared with Solver::newVar() !");
    }
    #endif

    // Check if clause is satisfied and remove false/duplicate literals:
    if (varReplacer->getNumLastReplacedVars() || subsumer->getNumElimed() || xorSubsumer->getNumElimed()) {
        for (uint32_t i = 0; i != ps.size(); i++) {
            ps[i] = varReplacer->getReplaceTable()[ps[i].var()] ^ ps[i].sign();
            if (subsumer->getVarElimed()[ps[i].var()] && !subsumer->unEliminate(ps[i].var()))
                return false;
            if (xorSubsumer->getVarElimed()[ps[i].var()] && !xorSubsumer->unEliminate(ps[i].var()))
                return false;
        }
    }

    return true;
}


/**
@brief Adds a clause to the problem. Calls addClauseInt() for heavy-lifting

Does some debug-related stuff (see "libraryCNFFile"), and checks whether the
variables of the literals in "ps" have been eliminated/replaced etc. If so,
it acts on them such that they are correct, and calls addClauseInt() to do
the heavy-lifting
*/
template<class T>
bool Solver::addClause(T& ps, const uint32_t group, const char* group_name)
{
    #ifdef VERBOSE_DEBUG
    std::cout << "addClause() called with new clause: " << ps << std::endl;
    #endif //VERBOSE_DEBUG
    if (!addClauseHelper(ps, group, group_name)) return false;
    Clause* c = addClauseInt(ps, group, false, 0, true);
    if (c != NULL) clauses.push(c);

    return ok;
}

template bool Solver::addClause(vec<Lit>& ps, const uint32_t group, const char* group_name);
template bool Solver::addClause(Clause& ps, const uint32_t group, const char* group_name);


template<class T>
bool Solver::addLearntClause(T& ps, const uint32_t group, const char* group_name, const uint32_t glue)
{
    if (!addClauseHelper(ps, group, group_name)) return false;
    Clause* c = addClauseInt(ps, group, true, glue, true);
    if (c != NULL) learnts.push(c);

    return ok;
}

template bool Solver::addLearntClause(vec<Lit>& ps, const uint32_t group, const char* group_name, const uint32_t glue);
template bool Solver::addLearntClause(Clause& ps, const uint32_t group, const char* group_name, const uint32_t glue);


/**
@brief Attaches an xor clause to the watchlists

The xor clause must be larger than 2, since a 2-long XOR clause is a varible
replacement instruction, really.
*/
void Solver::attachClause(XorClause& c)
{
    assert(c.size() > 2);
    #ifdef DEBUG_ATTACH
    assert(assigns[c[0].var()] == l_Undef);
    assert(assigns[c[1].var()] == l_Undef);

    for (uint32_t i = 0; i < c.size(); i++) {
        assert(!subsumer->getVarElimed()[c[i].var()]);
        assert(!xorSubsumer->getVarElimed()[c[i].var()]);
    }
    #endif //DEBUG_ATTACH

    ClauseData data(0, 1);
    watches[Lit(c[0].var(), false).toInt()].push(Watched(clauseAllocator.getOffset((Clause*)&c), 0));
    watches[Lit(c[0].var(), true).toInt()].push(Watched(clauseAllocator.getOffset((Clause*)&c), 0));
    watches[Lit(c[1].var(), false).toInt()].push(Watched(clauseAllocator.getOffset((Clause*)&c), 1));
    watches[Lit(c[1].var(), true).toInt()].push(Watched(clauseAllocator.getOffset((Clause*)&c), 1));
    const uint32_t clauseNum = c.getNum();
    if (clauseData.size() <= clauseNum) clauseData.resize(clauseNum+1, ClauseData());
    clauseData[clauseNum] = data;

    clauses_literals += c.size();
}

void Solver::attachBinClause(const Lit lit1, const Lit lit2, const bool learnt)
{
    #ifdef DEBUG_ATTACH
    assert(lit1.var() != lit2.var());
    assert(assigns[lit1.var()] == l_Undef);
    assert(value(lit2) == l_Undef || value(lit2) == l_False);

    assert(!subsumer->getVarElimed()[lit1.var()]);
    assert(!subsumer->getVarElimed()[lit2.var()]);

    assert(!xorSubsumer->getVarElimed()[lit1.var()]);
    assert(!xorSubsumer->getVarElimed()[lit2.var()]);
    #endif //DEBUG_ATTACH

    watches[(~lit1).toInt()].push(Watched(lit2, learnt));
    watches[(~lit2).toInt()].push(Watched(lit1, learnt));

    numBins++;
    if (learnt) learnts_literals += 2;
    else clauses_literals += 2;
}

/**
@brief Attach normal a clause to the watchlists

Handles 2, 3 and >3 clause sizes differently and specially
*/
void Solver::attachClause(Clause& c)
{
    assert(c.size() > 2);
    #ifdef DEBUG_ATTACH
    assert(assigns[c[0].var()] == l_Undef);
    assert(value(c[1]) == l_Undef || value(c[1]) == l_False);

    for (uint32_t i = 0; i < c.size(); i++) {
        if (i > 0) assert(c[i-1].var() != c[i].var());
        assert(!subsumer->getVarElimed()[c[i].var()]);
        assert(!xorSubsumer->getVarElimed()[c[i].var()]);
    }
    #endif //DEBUG_ATTACH

    ClauseData data(0, 1);
    if (c.size() == 3) {
        watches[(~c[0]).toInt()].push(Watched(c[1], c[2]));
        watches[(~c[1]).toInt()].push(Watched(c[0], c[2]));
        watches[(~c[2]).toInt()].push(Watched(c[0], c[1]));
    } else {
        ClauseOffset offset = clauseAllocator.getOffset(&c);
        watches[(~c[0]).toInt()].push(Watched(offset, c[c.size()/2], 0));
        watches[(~c[1]).toInt()].push(Watched(offset, c[c.size()/2], 1));
    }
    const uint32_t clauseNum = c.getNum();
    if (clauseData.size() <= clauseNum) clauseData.resize(clauseNum+1, ClauseData());
    clauseData[clauseNum] = data;

    if (c.learnt())
        learnts_literals += c.size();
    else
        clauses_literals += c.size();
}

/**
@brief Calls detachModifiedClause to do the heavy-lifting
*/
void Solver::detachClause(const XorClause& c)
{
    const ClauseData& data =clauseData[c.getNum()];
    detachModifiedClause(c[data[0]].var(), c[data[1]].var(), c.size(), &c);
}

/**
@brief Calls detachModifiedClause to do the heavy-lifting
*/
void Solver::detachClause(const Clause& c)
{
    const ClauseData& data =clauseData[c.getNum()];
    detachModifiedClause(c[data[0]], c[data[1]], (c.size() == 3) ? c[2] : lit_Undef,  c.size(), &c);
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

    ClauseOffset offset = clauseAllocator.getOffset(address);
    if (origSize == 3) {
        //The clause might have been longer, and has only recently
        //became 3-long. Check, and detach accordingly
        if (findWCl(watches[(~lit1).toInt()], offset)) goto fullClause;

        removeWTri(watches[(~lit1).toInt()], lit2, lit3);
        removeWTri(watches[(~lit2).toInt()], lit1, lit3);
        removeWTri(watches[(~lit3).toInt()], lit1, lit2);

    } else {
        fullClause:
        removeWCl(watches[(~lit1).toInt()], offset);
        removeWCl(watches[(~lit2).toInt()], offset);

    }

    if (address->learnt())
        learnts_literals -= origSize;
    else
        clauses_literals -= origSize;
}

/**
@brief Detaches a (potentially) modified xor clause

The first two vars might have chaned through modification, so they are passed
along as arguments.
*/
void Solver::detachModifiedClause(const Var var1, const Var var2, const uint32_t origSize, const XorClause* address)
{
    assert(origSize > 2);

    ClauseOffset offset = clauseAllocator.getOffset(address);
    assert(findWXCl(watches[Lit(var1, false).toInt()], offset));
    assert(findWXCl(watches[Lit(var1, true).toInt()], offset));
    assert(findWXCl(watches[Lit(var2, false).toInt()], offset));
    assert(findWXCl(watches[Lit(var2, true).toInt()], offset));

    removeWXCl(watches[Lit(var1, false).toInt()], offset);
    removeWXCl(watches[Lit(var1, true).toInt()], offset);
    removeWXCl(watches[Lit(var2, false).toInt()], offset);
    removeWXCl(watches[Lit(var2, true).toInt()], offset);

    assert(!address->learnt());
    clauses_literals -= origSize;
}

void Solver::syncData()
{
    dataSync->syncData();
}

void Solver::finishAddingVars()
{
    subsumer->setFinishedAddingVars(true);
}

struct PolaritySorter
{
    PolaritySorter(const vector<VarData>& _varData) :
        varData(_varData)
    {};

    const bool operator()(const Lit lit1, const Lit lit2) {
        const bool pol1 = !varData[lit1.var()].polarity.getLastVal() ^ lit1.sign();
        const bool pol2 = !varData[lit2.var()].polarity.getLastVal() ^ lit2.sign();

        //Tie 1: polarity
        if (pol1 == true && pol2 == false) return true;
        if (pol1 == false && pol2 == true) return false;

        return (varData[lit1.var()].popularity > varData[lit2.var()].popularity);

        /*
        //Tie 2: last level
        assert(pol1 == pol2);
        if (pol1 == true) return level[lit1.var()] < level[lit2.var()];
        else return level[lit1.var()] > level[lit2.var()];*/
    }

    const vector<VarData>& varData;
};

/**
@brief Revert to the state at given level

Also reverts all stuff in Gass-elimination
*/
void Solver::cancelUntil(uint32_t level)
{
    #ifdef VERBOSE_DEBUG
    cout << "Canceling until level " << level;
    if (level > 0) cout << " sublevel: " << trail_lim[level];
    cout << endl;
    #endif

    if (decisionLevel() > level) {

        #ifdef USE_GAUSS
        for (vector<Gaussian*>::iterator gauss = gauss_matrixes.begin(), end= gauss_matrixes.end(); gauss != end; gauss++)
            (*gauss)->canceling(trail_lim[level]);
        #endif //USE_GAUSS

        for (int sublevel = trail.size()-1; sublevel >= (int)trail_lim[level]; sublevel--) {
            Var var = trail[sublevel].var();
            #ifdef VERBOSE_DEBUG
            cout << "Canceling var " << var+1 << " sublevel: " << sublevel << endl;
            #endif
            assigns[var] = l_Undef;
            insertVarOrder(var);
            if (unWindGlue[var] != NULL) {
                #ifdef UNWINDING_DEBUG
                std::cout << "unwind, var:" << var
                << " sublevel:" << sublevel
                << " coming from:" << (trail.size()-1)
                << " going until:" << (int)trail_lim[level]
                << std::endl;
                unWindGlue[var]->plainPrint();
                #endif //UNWINDING_DEBUG

                Clause*& clauseToFree = unWindGlue[var];
                detachClause(*clauseToFree);
                clauseAllocator.clauseFree(clauseToFree);
                clauseToFree = NULL;
            }
        }
        qhead = trail_lim[level];
        trail.shrink_(trail.size() - trail_lim[level]);
        trail_lim.shrink_(trail_lim.size() - level);
    }

    #ifdef VERBOSE_DEBUG
    cout << "Canceling finished. (now at level: " << decisionLevel() << " sublevel: " << trail.size()-1 << ")" << endl;
    #endif
    lastDelayedEnqueueUpdate = trail.size();
    lastDelayedEnqueueUpdateLevel = decisionLevel();
}

void Solver::cancelUntilLight()
{
    assert((int)decisionLevel() > 0);

    for (int sublevel = trail.size()-1; sublevel >= (int)trail_lim[0]; sublevel--) {
        Var var = trail[sublevel].var();
        assigns[var] = l_Undef;
    }
    qhead = trail_lim[0];
    trail.shrink_(trail.size() - trail_lim[0]);
    trail_lim.clear();
}

const bool Solver::clearGaussMatrixes()
{
    assert(decisionLevel() == 0);
    #ifdef USE_GAUSS
    bool ret = gauss_matrixes.size() > 0;
    for (uint32_t i = 0; i < gauss_matrixes.size(); i++)
        delete gauss_matrixes[i];
    gauss_matrixes.clear();

    for (uint32_t i = 0; i != freeLater.size(); i++)
        clauseAllocator.clauseFree(freeLater[i]);
    freeLater.clear();

    return ret;
    #endif //USE_GAUSS
    return false;
}

inline bool Solver::getPolarity(const Var var)
{
    switch(conf.polarity_mode) {
        case polarity_false:
            return true;
        case polarity_true:
            return false;
        case polarity_rnd:
            return mtrand.randInt(1);
        case polarity_auto:
            if (avgBranchDepth.isvalid()) {
                uint32_t multiplier;
                switch(subRestartType) {
                    case static_restart:
                        multiplier = 2;
                        break;
                    default:
                        multiplier = 1;
                }
                const bool random = mtrand.randInt(avgBranchDepth.getAvgUInt() * multiplier) == 1;

                return varData[var].polarity.getVal() ^ random;
            } else {
                return varData[var].polarity.getVal();
            }
        default:
            assert(false);
    }

    return true;
}

/**
@brief Tally votes for a default TRUE or FALSE value for the variable using the Jeroslow-Wang method

@p votes[inout] Votes are tallied at this place for each variable
@p cs The clause to tally votes for
*/
void Solver::tallyVotes(const vec<Clause*>& cs, vec<double>& votes) const
{
    for (const Clause * const*it = cs.getData(), * const*end = it + cs.size(); it != end; it++) {
        const Clause& c = **it;
        if (c.learnt()) continue;

        double divider;
        if (c.size() > 63) divider = 0.0;
        else divider = 1.0/(double)((uint64_t)1<<(c.size()-1));

        for (const Lit *it2 = c.getData(), *end2 = c.getDataEnd(); it2 != end2; it2++) {
            if (it2->sign()) votes[it2->var()] += divider;
            else votes[it2->var()] -= divider;
        }
    }
}

void Solver::tallyVotesBin(vec<double>& votes) const
{
    uint32_t wsLit = 0;
    for (const vec<Watched> *it = watches.getData(), *end = watches.getDataEnd(); it != end; it++, wsLit++) {
        Lit lit = ~Lit::toLit(wsLit);
        const vec<Watched>& ws = *it;
        for (const Watched *it2 = ws.getData(), *end2 = ws.getDataEnd(); it2 != end2; it2++) {
            if (it2->isBinary() && lit.toInt() < it2->getOtherLit().toInt()) {
                if (!it2->getLearnt()) {
                    if (lit.sign()) votes[lit.var()] += 0.5;
                    else votes[lit.var()] -= 0.5;

                    Lit lit2 = it2->getOtherLit();
                    if (lit2.sign()) votes[lit2.var()] += 0.5;
                    else votes[lit2.var()] -= 0.5;
                }
            }
        }
    }
}

/**
@brief Tally votes a default TRUE or FALSE value for the variable using the Jeroslow-Wang method

For XOR clause, we simply add some weight for a FALSE default, i.e. being in
xor clauses makes the variabe more likely to be FALSE by default
*/
void Solver::tallyVotes(const vec<XorClause*>& cs, vec<double>& votes) const
{
    for (const XorClause * const*it = cs.getData(), * const*end = it + cs.size(); it != end; it++) {
        const XorClause& c = **it;
        double divider;
        if (c.size() > 63) divider = 0.0;
        else divider = 1.0/(double)((uint64_t)1<<(c.size()-1));

        for (const Lit *it2 = c.getData(), *end2 = c.getDataEnd(); it2 != end2; it2++) {
            votes[it2->var()] += divider;
        }
    }
}

/**
@brief Tallies votes for a TRUE/FALSE default polarity using Jeroslow-Wang

Voting is only used if polarity_mode is "polarity_auto". This is the default.
Uses the tallyVotes() functions to tally the votes
*/
void Solver::calculateDefaultPolarities()
{
    #ifdef VERBOSE_DEBUG_POLARITIES
    std::cout << "Default polarities: " << std::endl;
    #endif

    assert(decisionLevel() == 0);
    if (conf.polarity_mode == polarity_auto) {
        double myTime = cpuTime();

        vec<double> votes(nVars(), 0.0);

        tallyVotes(clauses, votes);
        tallyVotesBin(votes);
        tallyVotes(xorclauses, votes);

        Var i = 0;
        uint32_t posPolars = 0;
        uint32_t undecidedPolars = 0;
        for (const double *it = votes.getData(), *end = votes.getDataEnd(); it != end; it++, i++) {
            varData[i].polarity.setLastVal(*it >= 0.0);
            posPolars += (*it < 0.0);
            undecidedPolars += (*it == 0.0);
            #ifdef VERBOSE_DEBUG_POLARITIES
            std::cout << !defaultPolarities[i] << ", ";
            #endif //VERBOSE_DEBUG_POLARITIES
        }

        if (conf.verbosity >= 2) {
            std::cout << "c Calc default polars - "
            << " time: " << std::fixed << std::setw(6) << std::setprecision(2) << (cpuTime() - myTime) << " s"
            << " pos: " << std::setw(7) << posPolars
            << " undec: " << std::setw(7) << undecidedPolars
            << " neg: " << std::setw(7) << nVars()-  undecidedPolars - posPolars
            << std:: endl;
        }
    } else {
        for (uint32_t i = 0; i < varData.size(); i++) {
            varData[i].polarity.setLastVal(getPolarity(i));
        }
    }

    #ifdef VERBOSE_DEBUG_POLARITIES
    std::cout << std::endl;
    #endif //VERBOSE_DEBUG_POLARITIES
}

void Solver::calcReachability()
{
    double myTime = cpuTime();

    for (uint32_t i = 0; i < nVars()*2; i++) {
        litReachable[i] = LitReachData();
    }

    for (uint32_t i = 0; i < order_heap.size(); i++) for (uint32_t sig1 = 0; sig1 < 2; sig1++)  {
        Lit lit = Lit(order_heap[i], sig1);
        if (value(lit.var()) != l_Undef
            || subsumer->getVarElimed()[lit.var()]
            || xorSubsumer->getVarElimed()[lit.var()]
            || varReplacer->getReplaceTable()[lit.var()].var() != lit.var()
            || partHandler->getSavedState()[lit.var()] != l_Undef
            || !decision_var[lit.var()])
            continue;

        vector<LitExtra>& cache = transOTFCache[(~lit).toInt()].lits;
        uint32_t cacheSize = cache.size();
        for (vector<LitExtra>::const_iterator it = cache.begin(), end = cache.end(); it != end; it++) {
            /*if (solver.value(it->var()) != l_Undef
            || solver.subsumer->getVarElimed()[it->var()]
            || solver.xorSubsumer->getVarElimed()[it->var()]
            || partHandler->getSavedState()[lit.var()] != l_Undef)
            continue;*/
            if (it->getLit() == lit || it->getLit() == ~lit) continue;

            if (litReachable[it->getLit().toInt()].lit == lit_Undef
                || litReachable[it->getLit().toInt()].numInCache </*=*/ cacheSize
            ) {
                //if (litReachable[it->toInt()].numInCache == cacheSize && mtrand.randInt(1) == 0) continue;
                litReachable[it->getLit().toInt()].lit = lit;
                litReachable[it->getLit().toInt()].numInCache = cacheSize;
            }
        }
    }

    /*for (uint32_t i = 0; i < nVars()*2; i++) {
        std::sort(litReachable[i].begin(), litReachable[i].end(), MySorterX(transOTFCache));
    }*/

    /*for (uint32_t i = 0; i < nVars()*2; i++) {
        vector<Lit>& myset = litReachable[i];
        for (uint32_t i2 = 0; i2 < myset.size(); i2++) {
            std::cout << transOTFCache[myset[i2].toInt()].lits.size() << " , ";
        }
        std::cout << std::endl;
    }*/

    if (conf.verbosity >= 1) {
        std::cout << "c calculated reachability. Time: " << (cpuTime() - myTime) << std::endl;
    }
}

void Solver::saveOTFData()
{
    assert(decisionLevel() == 1);

    Lit lev0Lit = trail[trail_lim[0]];
    TransCache& oTFCache = transOTFCache[(~lev0Lit).toInt()];
    oTFCache.conflictLastUpdated = conflicts;

    vector<Lit> lits;
    for (int sublevel = trail.size()-1; sublevel > (int)trail_lim[0]; sublevel--) {
        Lit lit = trail[sublevel];
        lits.push_back(lit);
    }
    oTFCache.merge(lits, false, seen);
}

//=================================================================================================
// Major methods:


/**
@brief Picks a branching variable and its value (True/False)

We do three things here:
-# Try to do random decision (rare, less than 2%)
-# Try acitivity-based decision

Then, we pick a sign (True/False):
\li If we are in search-burst mode ("simplifying" is set), we pick a sign
totally randomly
\li Otherwise, we simply take the saved polarity
*/
Lit Solver::pickBranchLit()
{
    #ifdef VERBOSE_DEBUG
    cout << "decision level: " << decisionLevel() << " ";
    #endif

    Var next = var_Undef;

    bool random = mtrand.randDblExc() < conf.random_var_freq;

    // Random decision:
    if (random && !order_heap.empty()) {
        if (conf.restrictPickBranch == 0)
            next = order_heap[mtrand.randInt(order_heap.size()-1)];
        else
            next = order_heap[mtrand.randInt(std::min((uint32_t)order_heap.size()-1, conf.restrictPickBranch))];

        if (assigns[next] == l_Undef && decision_var[next])
            rnd_decisions++;
    }

    bool signSet = false;
    bool signSetTo = false;
    // Activity based decision:
    while (next == var_Undef
      || assigns[next] != l_Undef
      || !decision_var[next]) {
        if (order_heap.empty()) {
            next = var_Undef;
            break;
        }

        next = order_heap.removeMin();
        if (!simplifying && value(next) == l_Undef && decision_var[next]) {
            signSet = true;
            signSetTo = getPolarity(next);

            Lit nextLit = Lit(next, signSetTo);
            Lit lit2 = litReachable[nextLit.toInt()].lit;
            if (lit2 != lit_Undef && value(lit2.var()) == l_Undef && decision_var[lit2.var()] && mtrand.randInt(1) == 1) {
                insertVarOrder(next);
                next = litReachable[nextLit.toInt()].lit.var();
                signSetTo = litReachable[nextLit.toInt()].lit.sign();
            }
        }
    }

    //if "simplifying" is set, i.e. if we are in a burst-search mode, then
    //randomly pick a sign. Otherwise, we check the default polarity,
    // and we may change it a bit
    //randomly based on the average branch depth. Otherwise, we just go for the
    //polarity that has been saved
    bool sign;
    if (next != var_Undef)  {
        if (signSet) {
            sign = signSetTo;
        } else {
            if (simplifying && random)
                sign = mtrand.randInt(1);
            else
                sign = getPolarity(next);
        }
    }

    assert(next == var_Undef || value(next) == l_Undef);

    if (next == var_Undef) {
        #ifdef VERBOSE_DEBUG
        cout << "SAT!" << endl;
        #endif
        return lit_Undef;
    } else {
        Lit lit(next,sign);
        #ifdef VERBOSE_DEBUG
        assert(decision_var[lit.var()]);
        cout << "decided on: " << lit.var()+1 << " to set:" << !lit.sign() << endl;
        #endif
        return lit;
    }
}

/**
@brief Checks subsumption. Used in on-the-fly subsumption code

Assumes 'seen' is cleared (will leave it cleared)
*/
template<class T1, class T2>
bool subset(const T1& A, const T2& B, vec<char>& seen)
{
    for (uint32_t i = 0; i != B.size(); i++)
        seen[B[i].toInt()] = 1;
    for (uint32_t i = 0; i != A.size(); i++) {
        if (!seen[A[i].toInt()]) {
            for (uint32_t i = 0; i != B.size(); i++)
                seen[B[i].toInt()] = 0;
            return false;
        }
    }
    for (uint32_t i = 0; i != B.size(); i++)
        seen[B[i].toInt()] = 0;
    return true;
}


/**
@brief    Analyze conflict and produce a reason clause.

Pre-conditions:
\li  'out_learnt' is assumed to be cleared.
\li Current decision level must be greater than root level.

Post-conditions:
\li 'out_learnt[0]' is the asserting literal at level 'out_btlevel'.

Effect: Will undo part of the trail, upto but not beyond the assumption of the
current decision level.

@return NULL if the conflict doesn't on-the-fly subsume the last clause, and
the pointer of the clause if it does
*/
Clause* Solver::analyze(PropBy conflHalf, vec<Lit>& out_learnt, uint32_t& out_btlevel, uint32_t &glue, const bool update)
{
    int pathC = 0;
    Lit p     = lit_Undef;

    // Generate conflict clause:
    //
    out_learnt.push();      // (leave room for the asserting literal)
    int index   = trail.size() - 1;
    out_btlevel = 0;

    PropByFull confl(conflHalf, failBinLit, clauseAllocator, clauseData, assigns);
    PropByFull oldConfl;

    #ifdef UPDATE_VAR_ACTIVITY_BASED_ON_GLUE
    vec<Var> lastDecisionLevel;
    #endif

    do {
        assert(!confl.isNULL());          // (otherwise should be UIP)

        for (uint32_t j = (p == lit_Undef) ? 0 : 1, size = confl.size(); j != size; j++) {
            Lit q = confl[j];
            const Var my_var = q.var();

            if (!seen[my_var] && varData[my_var].level > 0) {
                varBumpActivity(my_var);
                seen[my_var] = 1;
                assert(varData[my_var].level <= decisionLevel());
                if (varData[my_var].level >= decisionLevel()) {
                    pathC++;
                    #ifdef UPDATE_VAR_ACTIVITY_BASED_ON_GLUE
                    if (subRestartType == dynamic_restart
                        && reason[q.var()].isClause()
                        && !reason[q.var()].isNULL()
                        && clauseAllocator.getPointer(reason[q.var()].getClause())->learnt())
                        lastDecisionLevel.push(q.var());
                    #endif //#define UPDATEVARACTIVITY
                } else {
                    out_learnt.push(q);
                    if (varData[my_var].level > out_btlevel)
                        out_btlevel = varData[my_var].level;
                }
            }
        }

        // Select next clause to look at:
        while (!seen[trail[index--].var()]);
        p     = trail[index+1];
        oldConfl = confl;
        confl = PropByFull(reason[p.var()], failBinLit, clauseAllocator, clauseData, assigns);
        //if (confl.isClause()) __builtin_prefetch(confl.getClause(), 1, 0);
        seen[p.var()] = 0;
        pathC--;

    } while (pathC > 0);
    assert(pathC == 0);
    out_learnt[0] = ~p;

    // Simplify conflict clause:
    //
    uint32_t i, j;
    if (conf.expensive_ccmin) {
        uint32_t abstract_level = 0;
        for (i = 1; i < out_learnt.size(); i++)
            abstract_level |= abstractLevel(out_learnt[i].var()); // (maintain an abstraction of levels involved in conflict)

        out_learnt.copyTo(analyze_toclear);
        for (i = j = 1; i < out_learnt.size(); i++)
            if (reason[out_learnt[i].var()].isNULL() || !litRedundant(out_learnt[i], abstract_level))
                out_learnt[j++] = out_learnt[i];
    } else {
        out_learnt.copyTo(analyze_toclear);
        for (i = j = 1; i < out_learnt.size(); i++) {
            PropByFull c(reason[out_learnt[i].var()], failBinLit, clauseAllocator, clauseData, assigns);

            for (uint32_t k = 1, size = c.size(); k < size; k++) {
                if (!seen[c[k].var()] && varData[c[k].var()].level > 0) {
                    out_learnt[j++] = out_learnt[i];
                    break;
                }
            }
        }
    }
    max_literals += out_learnt.size();
    out_learnt.shrink(i - j);
    for (uint32_t j = 0; j != analyze_toclear.size(); j++)
        seen[analyze_toclear[j].var()] = 0;    // ('seen[]' is now cleared)


    if (conf.doMinimLearntMore && out_learnt.size() > 1) minimiseLeartFurther(out_learnt, calcNBLevels(out_learnt));
    glue = calcNBLevels(out_learnt);
    tot_literals += out_learnt.size();

    #ifdef VERBOSE_DEBUG_GATE
    std::cout << "Final clause: " << out_learnt << std::endl;
    for (uint32_t i = 0; i < out_learnt.size(); i++) {
        std::cout << "val out_learnt[" << i << "]:" << value(out_learnt[i]) << std::endl;
        std::cout << "lev out_learnt[" << i << "]:" << level[out_learnt[i].var()] << std::endl;
    }
    #endif

    // Find correct backtrack level:
    //
    if (out_learnt.size() == 1)
        out_btlevel = 0;
    else {
        uint32_t max_i = 1;
        for (uint32_t i = 2; i < out_learnt.size(); i++)
            if (varData[out_learnt[i].var()].level > varData[out_learnt[max_i].var()].level)
                max_i = i;
        std::swap(out_learnt[max_i], out_learnt[1]);
        out_btlevel = varData[out_learnt[1].var()].level;
    }
    #ifdef VERBOSE_DEBUG_GATE
    std::cout << "out_btlevel: " << out_btlevel << std::endl;
    #endif

    #ifdef UPDATE_VAR_ACTIVITY_BASED_ON_GLUE
    if (subRestartType == dynamic_restart) {
        for(uint32_t i = 0; i != lastDecisionLevel.size(); i++) {
            PropBy cl = reason[lastDecisionLevel[i]];
            if (cl.isClause() && clauseAllocator.getPointer(cl.getClause())->getGlue() < glue)
                varBumpActivity(lastDecisionLevel[i]);
        }
        lastDecisionLevel.clear();
    }
    #endif

    //We can only on-the-fly subsume clauses that are not 2- or 3-long
    //furthermore, we cannot subsume a clause that is marked for deletion
    //due to its high glue value
    if (out_learnt.size() == 1
        || !oldConfl.isClause()
        || oldConfl.getClause()->isXor()
        || (conf.doMaxGlueDel && oldConfl.getClause()->getGlue() > conf.maxGlue)
        || out_learnt.size() >= oldConfl.getClause()->size()) return NULL;

    if (!subset(out_learnt, *oldConfl.getClause(), seen)) return NULL;

    improvedClauseNo++;
    improvedClauseSize += oldConfl.getClause()->size() - out_learnt.size();
    return oldConfl.getClause();
}

/**
@brief Performs on-the-fly self-subsuming resolution

Only uses binary and tertiary clauses already in the watchlists in native
form to carry out the forward-self-subsuming resolution
*/
void Solver::minimiseLeartFurther(vec<Lit>& cl, const uint32_t glue)
{
    if (!conf.doCacheOTFSSR || !conf.doMinimLMoreRecur) return;
    //80 million is kind of a hack. It seems that the longer the solving
    //the slower this operation gets. So, limiting the "time" with total
    //number of conflict literals is maybe a good way of doing this
    bool clDoMinLRec = false;
    if (conf.doAlwaysFMinim)
        clDoMinLRec = true;
    else {
        switch(subRestartType) {
            case dynamic_restart :
                clDoMinLRec |= glue < 0.6*glueHistory.getAvgAllDouble();
                //NOTE: No "break;" here on purpose
            case static_restart :
                clDoMinLRec |= cl.size() < 0.6*conflSizeHist.getAvgDouble();
                break;
            default :
                assert(false);
        }
    }

    if (clDoMinLRec) moreRecurMinLDo++;
    uint64_t thisUpdateTransOTFSSCache = UPDATE_TRANSOTFSSR_CACHE;
    if (tot_literals > 80000000) thisUpdateTransOTFSSCache *= 2;

    //To count the "amount of time" invested in doing transitive on-the-fly
    //self-subsuming resolution
    //uint32_t moreRecurProp = 0;

    for (uint32_t i = 0; i < cl.size(); i++) seen[cl[i].toInt()] = 1;

    if (conf.doGateFind && clDoMinLRec) {
        bool gateRemSuccess = false;
        vec<Lit> oldCl = cl;
        while (true) {
            for (Lit *l = cl.getData(), *end = cl.getDataEnd(); l != end; l++) {
                const vector<OrGate*>& gates = subsumer->getGateOcc(*l);
                if (gates.empty()) continue;

                for (vector<OrGate*>::const_iterator it2 = gates.begin(), end2 = gates.end(); it2 != end2; it2++) {
                    OrGate& gate = **it2;

                    gate.eqLit = varReplacer->getReplaceTable()[gate.eqLit.var()] ^ gate.eqLit.sign();
                    if (subsumer->getVarElimed()[gate.eqLit.var()]
                        || xorSubsumer->getVarElimed()[gate.eqLit.var()]
                        || partHandler->getSavedState()[gate.eqLit.var()] != l_Undef
                        || !decision_var[gate.eqLit.var()]) continue;

                    bool OK = true;
                    for (uint32_t i = 0; i < gate.lits.size(); i++) {
                        if (!seen[gate.lits[i].toInt()]) {
                            OK = false;
                            break;
                        }
                    }
                    if (!OK) continue;

                    //Treat gate
                    #ifdef VERBOSE_DEBUG_GATE
                    std::cout << "Gate: eqLit "  << gate.eqLit << " lits ";
                    for (uint32_t i = 0; i < gate.lits.size(); i++) {
                        std::cout << gate.lits[i] << ", ";
                    }
                    /*std::cout << " origclause: ";
                    for (uint32_t i = 0; i < gate.origCl.size(); i++) {
                        std::cout << gate.origCl[i] << ", ";
                    }
                    std::cout << std::endl;*/
                    #endif

                    bool firstInside = false;
                    Lit *lit1 = cl.getData();
                    Lit *lit2 = cl.getData();
                    bool first = true;
                    for (Lit *end3 = cl.getDataEnd(); lit1 != end3; lit1++, first = false) {
                        bool in = false;
                        for (uint32_t i = 0; i < gate.lits.size(); i++) {
                            if (*lit1 == gate.lits[i]) {
                                in = true;
                                if (first) firstInside = true;
                            }
                        }
                        if (in) {
                            seen[lit1->toInt()] = false;
                        } else {
                            *lit2++ = *lit1;
                        }
                    }
                    if (!seen[gate.eqLit.toInt()]) {
                        *lit2++ = gate.eqLit;
                        seen[gate.eqLit.toInt()] = true;
                    }
                    assert(!seen[(~gate.eqLit).toInt()]);
                    cl.shrink_(lit1-lit2);
                    OTFGateRemLits += lit1-lit2;
                    #ifdef VERBOSE_DEBUG_GATE
                    std::cout << "Old clause: " << oldCl << std::endl;
                    for (uint32_t i = 0; i < oldCl.size(); i++) {
                        std::cout << "-> Lit " << oldCl[i] << " lev: " << level[oldCl[i].var()] << " val: " << value(oldCl[i]) <<std::endl;
                    }
                    std::cout << "New clause: " << cl << std::endl;
                    for (uint32_t i = 0; i < cl.size(); i++) {
                        std::cout << "-> Lit " << cl[i] << " lev: " << level[cl[i].var()] << " val: " << value(cl[i]) << std::endl;
                    }
                    #endif

                    if (firstInside) {
                        uint32_t swapWith = std::numeric_limits<uint32_t>::max();
                        for (uint32_t i = 0; i < cl.size(); i++) {
                            if (cl[i] == gate.eqLit) swapWith = i;
                        }
                        std::swap(cl[swapWith], cl[0]);
                    }
                    #ifdef VERBOSE_DEBUG_GATE
                    std::cout << "New clause2: " << cl << std::endl;
                    for (uint32_t i = 0; i < cl.size(); i++) {
                        std::cout << "-> Lit " << cl[i] << " lev: " << level[cl[i].var()] << std::endl;
                    }
                    #endif

                    gateRemSuccess = true;

                    //Do this recurively, again
                    goto next;
                }
            }
            break;
            next:;
        }
        OTFGateRemSucc += gateRemSuccess;
        #ifdef VERBOSE_DEBUG_GATE
        if (gateRemSuccess) std::cout << "--------" << std::endl;
        #endif
    }

    uint32_t moreRecurProp = 0;

    for (Lit *l = cl.getData(), *end = cl.getDataEnd(); l != end; l++) {
        if (seen[l->toInt()] == 0) continue;
        Lit lit = *l;

        if (clDoMinLRec && conf.doCacheOTFSSR) {
            if (moreRecurProp >= 450
                || (transOTFCache[l->toInt()].conflictLastUpdated != std::numeric_limits<uint64_t>::max()
                    && (transOTFCache[l->toInt()].conflictLastUpdated + thisUpdateTransOTFSSCache >= conflicts))
               ) {
                const TransCache& cache1 = transOTFCache[l->toInt()];
                for (vector<LitExtra>::const_iterator it = cache1.lits.begin(), end2 = cache1.lits.end(); it != end2; it++) {
                    seen[(~(it->getLit())).toInt()] = 0;
                }
            } else {
                updateTransCache++;
                transMinimAndUpdateCache(lit, moreRecurProp);
            }
        }

        //watched is messed: lit is in watched[~lit]
        vec<Watched>& ws = watches[(~lit).toInt()];
        for (Watched* i = ws.getData(), *end = ws.getDataEnd(); i != end; i++) {
            if (i->isBinary()) {
                seen[(~i->getOtherLit()).toInt()] = 0;
                continue;
            }

            if (i->isTriClause()) {
                if (seen[(~i->getOtherLit()).toInt()] && seen[i->getOtherLit2().toInt()]) {
                    seen[(~i->getOtherLit()).toInt()] = 0;
                }
                if (seen[(~i->getOtherLit2()).toInt()] && seen[i->getOtherLit().toInt()]) {
                    seen[(~i->getOtherLit2()).toInt()] = 0;
                }
                continue;
            }

            //watches are mostly sorted, so it's more-or-less OK to break
            //  if non-bi or non-tri is encountered
            //break;
        }
    }

    uint32_t removedLits = 0;
    Lit *i = cl.getData();
    Lit *j= i;
    //never remove the 0th literal
    seen[cl[0].toInt()] = 1;
    for (Lit* end = cl.getDataEnd(); i != end; i++) {
        if (seen[i->toInt()]) *j++ = *i;
        else removedLits++;
        seen[i->toInt()] = 0;
    }
    numShrinkedClause += (removedLits > 0);
    numShrinkedClauseLits += removedLits;
    cl.shrink_(i-j);

    #ifdef VERBOSE_DEBUG
    std::cout << "c Removed further " << removedLits << " lits" << std::endl;
    #endif
}

void Solver::transMinimAndUpdateCache(const Lit lit, uint32_t& moreRecurProp)
{
    assert(conf.doCacheOTFSSR);
    vector<Lit> lits;
    std::stack<Lit> toRecursiveProp;

    toRecursiveProp.push(~lit);
    while(!toRecursiveProp.empty()) {
        Lit thisLit = toRecursiveProp.top();
        toRecursiveProp.pop();
        vec<Watched>& ws = watches[thisLit.toInt()];
        moreRecurProp += ws.size() +10;
        for (Watched* i = ws.getData(), *end = ws.getDataEnd(); i != end; i++) {
            if (i->isBinary()) {
                moreRecurProp += 5;
                Lit otherLit = i->getOtherLit();
                //don't do indefinite recursion, and don't remove "a" when doing self-subsuming-resolution with 'a OR b'
                if (seen2[otherLit.toInt()] != 0 || otherLit == ~lit) continue;
                seen2[otherLit.toInt()] = 1;
                lits.push_back(otherLit);
                toRecursiveProp.push(otherLit);
            }
        }
    }
    assert(toRecursiveProp.empty());

    for (vector<Lit>::const_iterator  it = lits.begin(), end = lits.end(); it != end; it++) {
        seen2[it->toInt()] = 0;
    }
    transOTFCache[lit.toInt()].merge(lits, false, seen2);

    for (vector<LitExtra>::const_iterator  it = transOTFCache[lit.toInt()].lits.begin(), end = transOTFCache[lit.toInt()].lits.end(); it != end; it++) {
        seen[it->getLit().toInt()] = 0;
    }

    transOTFCache[lit.toInt()].conflictLastUpdated = conflicts;
}

/**
@brief Check if 'p' can be removed from a learnt clause

'abstract_levels' is used to abort early if the algorithm is
visiting literals at levels that cannot be removed later.
*/
bool Solver::litRedundant(Lit p, uint32_t abstract_levels)
{
    analyze_stack.clear();
    analyze_stack.push(p);
    int top = analyze_toclear.size();
    while (analyze_stack.size() > 0) {
        assert(!reason[analyze_stack.last().var()].isNULL());
        PropByFull c(reason[analyze_stack.last().var()], failBinLit, clauseAllocator, clauseData, assigns);

        analyze_stack.pop();

        for (uint32_t i = 1, size = c.size(); i < size; i++) {
            Lit p  = c[i];
            if (!seen[p.var()] && varData[p.var()].level > 0) {
                if (!reason[p.var()].isNULL() && (abstractLevel(p.var()) & abstract_levels) != 0) {
                    seen[p.var()] = 1;
                    analyze_stack.push(p);
                    analyze_toclear.push(p);
                } else {
                    for (uint32_t j = top; j != analyze_toclear.size(); j++)
                        seen[analyze_toclear[j].var()] = 0;
                    analyze_toclear.shrink(analyze_toclear.size() - top);
                    return false;
                }
            }
        }
    }

    return true;
}


/*_________________________________________________________________________________________________
|
|  analyzeFinal : (p : Lit)  ->  [void]
|
|  Description:
|    Specialized analysis procedure to express the final conflict in terms of assumptions.
|    Calculates the (possibly empty) set of assumptions that led to the assignment of 'p', and
|    stores the result in 'out_conflict'.
|________________________________________________________________________________________________@*/
void Solver::analyzeFinal(Lit p, vec<Lit>& out_conflict)
{
    out_conflict.clear();
    out_conflict.push(p);

    if (decisionLevel() == 0)
        return;

    seen[p.var()] = 1;

    for (int32_t i = (int32_t)trail.size()-1; i >= (int32_t)trail_lim[0]; i--) {
        Var x = trail[i].var();
        if (seen[x]) {
            if (reason[x].isNULL()) {
                assert(varData[x].level > 0);
                out_conflict.push(~trail[i]);
            } else {
                PropByFull c(reason[x], failBinLit, clauseAllocator, clauseData, assigns);
                for (uint32_t j = 1, size = c.size(); j < size; j++)
                    if (varData[c[j].var()].level > 0)
                        seen[c[j].var()] = 1;
            }
            seen[x] = 0;
        }
    }

    seen[p.var()] = 0;
}

void Solver::delayedEnqueueUpdate()
{
    uint32_t pseudoLevel = lastDelayedEnqueueUpdateLevel;

    for (uint32_t i = lastDelayedEnqueueUpdate; i < trail.size(); i++) {
        if (pseudoLevel < trail_lim.size()
            && trail_lim[pseudoLevel] == i) pseudoLevel++;

        const Lit p = trail[i];
        const Var v = p.var();
        varData[v].level = pseudoLevel;
        agility.update(varData[v].polarity.getLastVal() != p.sign());
        varData[v].polarity.setLastVal(p.sign());
        varData[v].popularity++;
    }

    lastDelayedEnqueueUpdate = trail.size();
    lastDelayedEnqueueUpdateLevel = decisionLevel();
}

void Solver::uncheckedEnqueueExtend(const Lit p, const PropBy& from)
{
    assert(assigns[p.var()].isUndef());
    const Var v = p.var();
    assigns [v] = boolToLBool(!p.sign());//lbool(!sign(p));  // <<== abstract but not uttermost effecient
    varData [v].level = decisionLevel();
    reason  [v] = from;
    trail.push(p);
}

/**
@brief Propagates a binary clause

Need to be somewhat tricky if the clause indicates that current assignement
is incorrect (i.e. both literals evaluate to FALSE). If conflict if found,
sets failBinLit
*/
template<bool full>
inline const bool Solver::propBinaryClause(Watched* i, const Lit p, PropBy& confl)
{
    lbool val = value(i->getOtherLit());
    if (val.isUndef()) {
        if (full) uncheckedEnqueue(i->getOtherLit(), PropBy(p));
        else      uncheckedEnqueueLight(i->getOtherLit());
    } else if (val == l_False) {
        confl = PropBy(p);
        failBinLit = i->getOtherLit();
        qhead = trail.size();
        return false;
    }

    return true;
}

/**
@brief Propagates a tertiary (3-long) clause

Need to be somewhat tricky if the clause indicates that current assignement
is incorrect (i.e. all 3 literals evaluate to FALSE). If conflict is found,
sets failBinLit
*/
template<bool full>
inline const bool Solver::propTriClause(Watched* i, const Lit p, PropBy& confl)
{
    lbool val = value(i->getOtherLit());
    lbool val2 = value(i->getOtherLit2());
    if (val.isUndef() && val2 == l_False) {
        if (full) uncheckedEnqueue(i->getOtherLit(), PropBy(p, i->getOtherLit2()));
        else      uncheckedEnqueueLight(i->getOtherLit());
    } else if (val == l_False && val2.isUndef()) {
        if (full) uncheckedEnqueue(i->getOtherLit2(), PropBy(p, i->getOtherLit()));
        else      uncheckedEnqueueLight(i->getOtherLit2());
    } else if (val == l_False && val2 == l_False) {
        confl = PropBy(p, i->getOtherLit2());
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
template<bool full>
inline const bool Solver::propNormalClause(Watched* &i, Watched* &j, const Lit p, PropBy& confl, const bool update)
{
    if (value(i->getBlockedLit()).getBool()) {
        // Clause is sat
        *j++ = *i;
        return true;
    }
    bogoProps += 6;
    const uint32_t offset = i->getNormOffset();
    Clause& c = *clauseAllocator.getPointer(offset);
    const uint32_t clauseNum = c.getNum();
    ClauseData& data = clauseData[clauseNum];
    const bool watchNum = i->getWatchNum();
    #ifdef VERBOSE_DEBUG_PROP
    printf("PropNorm. Watches: %d, %d -- this is watchNum %d\n", data[0], data[1], watchNum);
    #endif
    assert(c[data[watchNum]] == ~p);

    // If other watch is true, then clause is already satisfied.
    if (value(c[data[!watchNum]]).getBool()) {
        #ifdef VERBOSE_DEBUG_PROP
        printf("Other watch is true\n");
        #endif
        *j = Watched(offset, i->getBlockedLit(), watchNum);
        j++;
        return true;
    }
    // Look for new watch:
    uint32_t other = std::numeric_limits<uint32_t>::max();
    for (uint16_t numLit = 0, size = c.size(); numLit < size; numLit++) {
        if (numLit == data[0] || numLit == data[1]) continue;
        if (value(c[numLit]) == l_True) {
            #ifdef VERBOSE_DEBUG_PROP
            printf("new watch: %d watchNum: %d\n", numLit, watchNum);
            #endif
            data[watchNum] = numLit;
            watches[(~c[numLit]).toInt()].push(Watched(offset, c[data[!watchNum]], watchNum));
            bogoProps += 6;
            return true;
        }
        if (value(c[numLit]) == l_Undef) other = numLit;
    }
    if (other != std::numeric_limits<uint32_t>::max()) {
        #ifdef VERBOSE_DEBUG_PROP
        printf("new watch: %d watchNum: %d\n", other, watchNum);
        #endif
        data[watchNum] = other;
        watches[(~c[other]).toInt()].push(Watched(offset, c[data[!watchNum]], watchNum));;
        bogoProps += 6;
        return true;
    }

    #ifdef VERBOSE_DEBUG_PROP
    printf("Did not find watch\n");
    #endif
    // Did not find watch -- clause is unit under assignment:
    *j++ = *i;
    if (value(c[data[!watchNum]]) == l_False) {
        #ifdef VERBOSE_DEBUG_PROP
        printf("PropNorm causing conflict\n");
        #endif
        confl = PropBy(offset, !watchNum);
        qhead = trail.size();
        return false;
    } else {
        #ifdef VERBOSE_DEBUG_PROP
        printf("PropNorm causing propagation\n");
        #endif
        if (full) uncheckedEnqueue(c[data[!watchNum]], PropBy(offset, !watchNum));
        else      uncheckedEnqueueLight(c[data[!watchNum]]);
        #ifdef DYNAMICALLY_UPDATE_GLUE
        if (update && c.learnt() && c.getGlue() > 2) { // GA
            uint32_t glue = calcNBLevels(c);
            if (glue+1 < c.getGlue()) {
                //c.setGlue(std::min(nbLevels, MAX_THEORETICAL_GLUE);
                c.setGlue(glue);
            }
        }
        #endif
    }

    return true;
}

/**
@brief Propagates an XOR clause

Strangely enough, we need to have 4 literals in the wathclists:
for the first two varialbles, BOTH negations (v and ~v). This means quite some
pain, since we need to remove v when moving ~v and vica-versa. However, it means
better memory-accesses since the watchlist is already in the memory...

\todo maybe not worth it, and a variable-based watchlist should be used
*/
template<bool full>
inline const bool Solver::propXorClause(Watched* &i, Watched* &j, const Lit p, PropBy& confl)
{
    bogoProps += 20;
    ClauseOffset offset = i->getXorOffset();
    XorClause& c = *(XorClause*)clauseAllocator.getPointer(offset);
    const uint32_t clauseNum = c.getNum();
    ClauseData& data = clauseData[clauseNum];
    const bool watchNum = i->getWatchNum();

    assert(c[data[watchNum]].var() == p.var());

    bool final = c.xorEqualFalse();
    for (uint32_t k = 0, size = c.size(); k != size; k++) {
        const lbool val = assigns[c[k].var()];
        if (val.isUndef() && k != data[0] && k != data[1]) {
            data[watchNum] = k;
            removeWXCl(watches[(~p).toInt()], offset);
            watches[Lit(c[k].var(), false).toInt()].push(Watched(offset, watchNum));
            watches[Lit(c[k].var(), true).toInt()].push(Watched(offset, watchNum));
            return true;
        }

        final ^= val.getBool();
    }

    // Did not find watch -- clause is unit under assignment:
    *j++ = *i;
    if (value(c[data[!watchNum]].var()) == l_Undef) {
        Lit tmp = c[data[!watchNum]].unsign()^final;
        if (full) uncheckedEnqueue(tmp, PropBy(offset, !watchNum));
        else      uncheckedEnqueueLight(tmp);
    } else if (!final) {
        confl = PropBy(offset, !watchNum);
        qhead = trail.size();
        return false;
    } else {
        //Satisfied, we are happy
    }

    return true;
}

/**
@brief Does the propagation

Basically, it goes through the watchlists recursively, and calls the appropirate
propagaton function
*/
template<bool full>
PropBy Solver::propagate(const bool update)
{
    PropBy confl;
    uint64_t oldPropagations = propagations;
    Watched *i, *j, *i2, *end;
    uint32_t qhead2 = qhead;

    #ifdef VERBOSE_DEBUG_PROP
    cout << "Propagation started" << endl;
    #endif

    while (qhead < trail.size()) {
        while (qhead2 < trail.size()) {
            Lit p = trail[qhead2++];     // 'p' is enqueued fact to propagate.
            vec<Watched>& ws = watches[p.toInt()];
            bogoProps += 2;
            i = ws.getData();
            end = ws.getDataEnd();
            for (; i != end; i++) {
                if (i->isBinary()) {
                    if (!propBinaryClause<full>(i, p, confl)) goto end;
                    else continue;
                } //end BINARY
            }
        }

        Lit p = trail[qhead++];     // 'p' is enqueued fact to propagate.
        vec<Watched>& ws = watches[p.toInt()];

        #ifdef VERBOSE_DEBUG_PROP
        cout << "Propagating lit " << p << endl;
        cout << "ws origSize: "<< ws.size() << endl;
        #endif

        i = i2 = j = ws.getData();
        i2++;
        Watched *end = ws.getDataEnd();
        for (; i != end; i++, i2++) {
            #ifdef VERBOSE_DEBUG_PROP
            cout << "end-i: " << end-i << endl;
            cout << "end-j: " << end-j << endl;
            cout << "i-j: " << i-j << endl;
            if (i->isClause())
                std::cout << "clause num " << i->getNormOffset()
                << " as i of prop: " << *clauseAllocator.getPointer(i->getNormOffset())
                << std::endl;
            #endif

            if (i2 != end && i2->isClause() && !value(i2->getBlockedLit()).getBool()) {
                __builtin_prefetch(clauseAllocator.getPointer(i2->getNormOffset()), 0);
            }

            if (i->isBinary()) {
                *j++ = *i;
                continue;
            } //end BINARY

            if (i->isTriClause()) {
                *j++ = *i;
                if (!propTriClause<full>(i, p, confl)) break;
                else continue;
            } //end TRICLAUSE

            if (i->isClause()) {
                if (!propNormalClause<full>(i, j, p, confl, update)) break;
                else {
                    #ifdef VERBOSE_DEBUG_PROP
                    std::cout << "clause num " << i->getNormOffset() << " after propNorm: " << *clauseAllocator.getPointer(i->getNormOffset()) << std::endl;
                    #endif
                    continue;
                }
            } //end CLAUSE

            if (i->isXorClause()) {
                if (!propXorClause<full>(i, j, p, confl)) break;
                else continue;
            } //end XORCLAUSE
        }
        if (i != end) {
            i++;
            //copy remaining watches
            Watched *j2;
            for(i2 = i, j2 = j; i2 != end; i2++) {
                *j2++ = *i2;
            }
            //memmove(j, i, sizeof(Watched)*(end-i));
        }
        assert(i >= j);
        ws.shrink_(i-j);
    }
end:
    simpDB_props -= (propagations-oldPropagations);

    #ifdef VERBOSE_DEBUG
    cout << "Propagation ended." << endl;
    #endif

    return confl;
}
template PropBy Solver::propagate <true>(const bool update);
template PropBy Solver::propagate <false>(const bool update);

/**
@brief Only propagates binary clauses

This is used in special algorithms outside the main Solver class
*/
PropBy Solver::propagateBin(vec<Lit>& uselessBin)
{
    #ifdef DEBUG_USELESS_LEARNT_BIN_REMOVAL
    assert(uselessBin.empty());
    #endif

    while (qhead < trail.size()) {
        Lit p = trail[qhead++];

        //setting up binPropData
        uint32_t lev = binPropData[p.var()].lev;
        Lit lev1Ancestor;
        switch (lev) {
            case 0 :
                lev1Ancestor = lit_Undef;
                break;
            case 1:
                lev1Ancestor = p;
                break;
            default:
                lev1Ancestor = binPropData[p.var()].lev1Ancestor;
        }
        lev++;
        const bool learntLeadHere = binPropData[p.var()].learntLeadHere;
        bool& hasChildren = binPropData[p.var()].hasChildren;
        hasChildren = false;

        //std::cout << "lev: " << lev << " ~p: "  << ~p << std::endl;
        const vec<Watched> & ws = watches[p.toInt()];
        bogoProps += 2;
        for(const Watched *k = ws.getData(), *end = ws.getDataEnd(); k != end; k++) {
            hasChildren = true;
            if (!k->isBinary()) continue;

            //std::cout << (~p) << ", " << k->getOtherLit() << " learnt: " << k->getLearnt() << std::endl;
            lbool val = value(k->getOtherLit());
            if (val.isUndef()) {
                uncheckedEnqueueLight2(k->getOtherLit(), lev, lev1Ancestor, learntLeadHere || k->getLearnt());
            } else if (val == l_False) {
                return PropBy(p);
            } else {
                assert(val == l_True);
                Lit lit2 = k->getOtherLit();
                if (lev > 1
                    && varData[lit2.var()].level != 0
                    && binPropData[lit2.var()].lev == 1
                    && lev1Ancestor != lit2) {
                    //Was propagated at level 1, and again here, original level 1 binary clause is useless
                    binPropData[lit2.var()].lev = lev;
                    binPropData[lit2.var()].lev1Ancestor = lev1Ancestor;
                    binPropData[lit2.var()].learntLeadHere = learntLeadHere || k->getLearnt();
                    uselessBin.push(lit2);
                }
            }
        }
    }
    //std::cout << " -----------" << std::endl;

    return PropBy();
}

/**
@brief Only propagates non-learnt binary clauses

This is used in special algorithms outside the main Solver class
Beware, it MUST have the watchlist sorted to work properly: will "break;" on
learnt binary or any non-binary or clause in watchlist (example sort:
Subsumer::BinSorter2 )
*/
PropBy Solver::propagateNonLearntBin()
{
    multiLevelProp = false;
    uint32_t origQhead = qhead + 1;

    while (qhead < trail.size()) {
        Lit p = trail[qhead++];
        const vec<Watched> & ws = watches[p.toInt()];
        bogoProps += 2;
        for(const Watched *k = ws.getData(), *end = ws.getDataEnd(); k != end; k++) {
            if (!k->isNonLearntBinary()) break;

            lbool val = value(k->getOtherLit());
            if (val.isUndef()) {
                if (qhead != origQhead) multiLevelProp = true;
                uncheckedEnqueueLight(k->getOtherLit());
            } else if (val == l_False) {
                return PropBy(p);
            }
        }
    }

    return PropBy();
}

/**
@brief Propagate recursively on non-learnt binaries, but do not propagate exceptLit if we reach it
*/
const bool Solver::propagateBinExcept(const Lit exceptLit)
{
    while (qhead < trail.size()) {
        Lit p   = trail[qhead++];
        const vec<Watched> & ws = watches[p.toInt()];
        bogoProps += 2;
        for(const Watched *i = ws.getData(), *end = ws.getDataEnd(); i != end; i++) {
            if (!i->isNonLearntBinary()) break;

            lbool val = value(i->getOtherLit());
            if (val.isUndef() && i->getOtherLit() != exceptLit) {
                uncheckedEnqueueLight(i->getOtherLit());
            } else if (val == l_False) {
                return false;
            }
        }
    }

    return true;
}

/**
@brief Propagate only for one hop(=non-recursively) on non-learnt bins
*/
const bool Solver::propagateBinOneLevel()
{
    Lit p   = trail[qhead];
    const vec<Watched> & ws = watches[p.toInt()];
    bogoProps += 2;
    for(const Watched *i = ws.getData(), *end = ws.getDataEnd(); i != end; i++) {
        if (!i->isNonLearntBinary()) break;

        lbool val = value(i->getOtherLit());
        if (val.isUndef()) {
            uncheckedEnqueueLight(i->getOtherLit());
        } else if (val == l_False) {
            return false;
        }
    }

    return true;
}

/**
@brief Calculates the glue of a clause

Used to calculate the Glue of a new clause, or to update the glue of an
existing clause. Only used if the glue-based activity heuristic is enabled,
i.e. if we are in GLUCOSE mode (not MiniSat mode)
*/
template<class T>
inline const uint32_t Solver::calcNBLevels(const T& ps)
{
    MYFLAG++;
    uint32_t nbLevels = 0;
    for(const Lit *l = ps.getData(), *end = ps.getDataEnd(); l != end; l++) {
        uint32_t lev = varData[l->var()].level;
        if (permDiff[lev] != MYFLAG) {
            permDiff[lev] = MYFLAG;
            nbLevels++;
        }
    }
    return nbLevels;
}

/*_________________________________________________________________________________________________
|
|  reduceDB : ()  ->  [void]
|
|  Description:
|    Remove half of the learnt clauses, minus the clauses locked by the current assignment. Locked
|    clauses are clauses that are reason to some assignment. Binary clauses are never removed.
|________________________________________________________________________________________________@*/
bool  reduceDB_ltGlucose::operator () (const Clause* x, const Clause* y) {
    const uint32_t xsize = x->size();
    const uint32_t ysize = y->size();

    assert(xsize > 2 && ysize > 2);
    if (x->getGlue() > y->getGlue()) return 1;
    if (x->getGlue() < y->getGlue()) return 0;
    return xsize > ysize;
}

/**
@brief Removes learnt clauses that have been found not to be too good

Either based on glue or MiniSat-style learnt clause activities, the clauses are
sorted and then removed
*/
void Solver::reduceDB()
{
    uint32_t     i, j;

    nbReduceDB++;
    std::sort(learnts.getData(), learnts.getDataEnd(), reduceDB_ltGlucose());

    #ifdef VERBOSE_DEBUG
    std::cout << "Cleaning learnt clauses. Learnt clauses after sort: " << std::endl;
    for (uint32_t i = 0; i != learnts.size(); i++) {
        std::cout << "activity:" << learnts[i]->getGlue()
        << " \tsize:" << learnts[i]->size() << std::endl;
    }
    #endif


    delayedEnqueueUpdate();
    uint32_t removeNum = (double)learnts.size() * (double)RATIOREMOVECLAUSES;
    uint32_t totalNumRemoved = 0;
    uint32_t totalNumNonRemoved = 0;
    uint64_t totalGlueOfRemoved = 0;
    uint64_t totalSizeOfRemoved = 0;
    uint64_t totalGlueOfNonRemoved = 0;
    uint64_t totalSizeOfNonRemoved = 0;
    uint32_t numThreeLongLearnt = 0;
    for (i = j = 0; i < std::min(removeNum, learnts.size()); i++){
        if (i+1 < learnts.size()) __builtin_prefetch(learnts[i+1], 0);
        assert(learnts[i]->size() > 2);
        if (!locked(*learnts[i])
            && learnts[i]->getGlue() > 2
            && learnts[i]->size() > 3) { //we cannot update activity of 3-longs because of wathclists

            totalGlueOfRemoved += learnts[i]->getGlue();
            totalSizeOfRemoved += learnts[i]->size();
            totalNumRemoved++;
            removeClause(*learnts[i]);
        } else {
            totalGlueOfNonRemoved += learnts[i]->getGlue();
            totalSizeOfNonRemoved += learnts[i]->size();
            totalNumNonRemoved++;
            numThreeLongLearnt += (learnts[i]->size()==3);
            removeNum++;
            learnts[j++] = learnts[i];
        }
    }
    for (; i < learnts.size(); i++) {
        totalGlueOfNonRemoved += learnts[i]->getGlue();
        totalSizeOfNonRemoved += learnts[i]->size();
        totalNumNonRemoved++;
        numThreeLongLearnt += (learnts[i]->size()==3);
        learnts[j++] = learnts[i];
    }
    learnts.shrink_(i - j);

    if (conf.verbosity >= 3) {
        std::cout << "c rem-learnts " << std::setw(6) << totalNumRemoved
        << "  avgGlue "
        << std::fixed << std::setw(5) << std::setprecision(2)  << ((double)totalGlueOfRemoved/(double)totalNumRemoved)
        << "  avgSize "
        << std::fixed << std::setw(6) << std::setprecision(2) << ((double)totalSizeOfRemoved/(double)totalNumRemoved)
        << "  || remain " << std::setw(6) << totalNumNonRemoved
        << "  avgGlue "
        << std::fixed << std::setw(5) << std::setprecision(2)  << ((double)totalGlueOfNonRemoved/(double)totalNumNonRemoved)
        << "  avgSize "
        << std::fixed << std::setw(6) << std::setprecision(2) << ((double)totalSizeOfNonRemoved/(double)totalNumNonRemoved)
        << "  3-long: "
        << std::setw(6) << numThreeLongLearnt
        << std::endl;
    }

    clauseAllocator.consolidate(this);
}

inline int64_t abs64(int64_t a)
{
    if (a < 0) return -a;
    return a;
}

/**
@brief Simplify the clause database according to the current top-level assigment.

We remove satisfied clauses, clean clauses from assigned literals, find
binary xor-clauses and replace variables with one another. Heuristics are
used to check if we need to find binary xor clauses or not.
*/
const bool Solver::simplify()
{
    testAllClauseAttach();
    assert(decisionLevel() == 0);

    if (!ok || !propagate<true>().isNULL()) {
        ok = false;
        return false;
    }

    if (simpDB_props > 0) {
        return true;
    }
    double myTime = cpuTime();

    double slowdown = (100000.0/((double)numBins * 30000.0/((double)order_heap.size())));
    slowdown = std::min(1.5, slowdown);
    slowdown = std::max(0.01, slowdown);

    double speedup = 200000000.0/(double)(propagations-lastSearchForBinaryXor);
    speedup = std::min(3.5, speedup);
    speedup = std::max(0.2, speedup);

    /*std::cout << "new:" << nbBin - lastNbBin + becameBinary << std::endl;
    std::cout << "left:" << ((double)(nbBin - lastNbBin + becameBinary)/BINARY_TO_XOR_APPROX) * slowdown  << std::endl;
    std::cout << "right:" << (double)order_heap.size() * PERCENTAGEPERFORMREPLACE * speedup << std::endl;*/

    if (conf.doFindEqLits && conf.doRegFindEqLits &&
        (((double)abs64((int64_t)numNewBin - (int64_t)lastNbBin)/BINARY_TO_XOR_APPROX) * slowdown) >
        ((double)order_heap.size() * PERCENTAGEPERFORMREPLACE * speedup)) {
        lastSearchForBinaryXor = propagations;

        clauseCleaner->cleanClauses(clauses, ClauseCleaner::clauses);
        clauseCleaner->cleanClauses(learnts, ClauseCleaner::learnts);
        clauseCleaner->removeSatisfiedBins();
        if (!ok) return false;

        if (!sCCFinder->find2LongXors()) return false;

        lastNbBin = numNewBin;
    }

    // Remove satisfied clauses:
    clauseCleaner->removeAndCleanAll();
    if (!ok) return false;

    if (conf.doReplace && !varReplacer->performReplace())
        return false;

    // Remove fixed variables from the variable heap:
    order_heap.filter(VarFilter(*this));

    #ifdef USE_GAUSS
    for (vector<Gaussian*>::iterator gauss = gauss_matrixes.begin(), end = gauss_matrixes.end(); gauss != end; gauss++) {
        if (!(*gauss)->full_init()) return false;
    }
    #endif //USE_GAUSS

    simpDB_assigns = nAssigns();
    simpDB_props = std::min((uint64_t)80000000, 4*clauses_literals + 4*learnts_literals); //at most 6 sec wait
    simpDB_props = std::max((int64_t)30000000, simpDB_props); //at least 2 sec wait
    totalSimplifyTime += cpuTime() - myTime;

    testAllClauseAttach();
    checkNoWrongAttach();
    return true;
}

/**
@brief Search for a model

Limits: must be below the specified number of conflicts and must keep the
number of learnt clauses below the provided limit

Use negative value for 'nof_conflicts' or 'nof_learnts' to indicate infinity.

Output: 'l_True' if a partial assigment that is consistent with respect to the
clauseset is found. If all variables are decision variables, this means
that the clause set is satisfiable. 'l_False' if the clause set is
unsatisfiable. 'l_Undef' if the bound on number of conflicts is reached.
*/
lbool Solver::search(const uint64_t nof_conflicts, const uint64_t maxNumConfl, const bool update)
{
    assert(ok);
    uint64_t    conflictC = 0;
    vec<Lit>    learnt_clause;
    llbool      ret;

    if (!simplifying && update) {
        starts++;
        if (restartType == static_restart) staticStarts++;
        else dynStarts++;
    }
    glueHistory.fastclear();
    agility.reset();

    #ifdef USE_GAUSS
    for (vector<Gaussian*>::iterator gauss = gauss_matrixes.begin(), end = gauss_matrixes.end(); gauss != end; gauss++) {
        if (!(*gauss)->full_init())
            return l_False;
    }
    #endif //USE_GAUSS

    testAllClauseAttach();
    findAllAttach();
    #ifdef VERBOSE_DEBUG
    std::cout << "c started Solver::search()" << std::endl;
    //printAllClauses();
    #endif //VERBOSE_DEBUG
    for (;;) {
        assert(ok);
        PropBy confl = propagate<true>(update);
        #ifdef VERBOSE_DEBUG
        std::cout << "c Solver::search() has finished propagation" << std::endl;
        //printAllClauses();
        #endif //VERBOSE_DEBUG

        if (!confl.isNULL()) {
            /*if (conflicts % 100 == 99) {
                std::cout << "dyn: " << (restartType == dynamic_restart)
                << ", confl: " << std::setw(6) << conflictC
                << ", rest: " << std::setw(6) << starts
                << ", agility : " << std::setw(6) << std::fixed << std::setprecision(2) << agility.getAgility()
                << ", agilityTooLow: " << std::setw(4) << agility.getNumTooLow()
                << ", agilityLimit : " << std::setw(6) << std::fixed << std::setprecision(2) << conf.agilityLimit << std::endl;
            }*/

            ret = handle_conflict(learnt_clause, confl, conflictC, update);
            if (ret != l_Nothing) return ret;
        } else {
            #ifdef USE_GAUSS
            bool at_least_one_continue = false;
            for (vector<Gaussian*>::iterator gauss = gauss_matrixes.begin(), end= gauss_matrixes.end(); gauss != end; gauss++) {
                ret = (*gauss)->find_truths(learnt_clause, conflictC);
                if (ret == l_Continue) at_least_one_continue = true;
                else if (ret != l_Nothing) return ret;
            }
            if (at_least_one_continue) continue;
            #endif //USE_GAUSS

            assert(ok);
            if (conf.doCacheOTFSSR  && decisionLevel() == 1) saveOTFData();
            ret = new_decision(nof_conflicts, maxNumConfl, conflictC);
            if (ret != l_Nothing) return ret;
        }
    }
}

/**
@brief Picks a new decision variable to branch on

@returns l_Undef if it should restart instead. l_False if it reached UNSAT
         (through simplification)
*/
llbool Solver::new_decision(const uint64_t nof_conflicts, const uint64_t maxNumConfl, const uint64_t conflictC)
{

    if (conflicts >= maxNumConfl || needToInterrupt)  {
        #ifdef STATS_NEEDED
        if (dynamic_behaviour_analysis)
            progress_estimate = progressEstimate();
        #endif
        cancelUntil(0);
        return l_Undef;
    }

    // Reached bound on number of conflicts?
    if (agility.getAgility() < conf.agilityLimit) agility.tooLow(conflictC);

    switch (restartType) {
    case dynamic_restart:
        if ((agility.getNumTooLow() > MIN_GLUE_RESTART/2)
            /*|| (glueHistory.isvalid() && 0.95*glueHistory.getAvgDouble() > glueHistory.getAvgAllDouble())*/) {

            #ifdef DEBUG_DYNAMIC_RESTART
            if (glueHistory.isvalid()) {
                std::cout << "glueHistory.getavg():" << glueHistory.getavg() <<std::endl;
                std::cout << "totalSumOfGlue:" << totalSumOfGlue << std::endl;
                std::cout << "conflicts:" << conflicts<< std::endl;
                std::cout << "compTotSumGlue:" << compTotSumGlue << std::endl;
                std::cout << "conflicts-compTotSumGlue:" << conflicts-compTotSumGlue<< std::endl;
            }
            #endif

            #ifdef STATS_NEEDED
            if (dynamic_behaviour_analysis)
                progress_estimate = progressEstimate();
            #endif

            cancelUntil(0);
            return l_Undef;
        }
        break;
    case static_restart:
        if (conflictC >= nof_conflicts) {
            #ifdef STATS_NEEDED
            if (dynamic_behaviour_analysis)
                progress_estimate = progressEstimate();
            #endif
            cancelUntil(0);
            return l_Undef;
        }
        break;
    case auto_restart:
        assert(false);
        break;
    }

    // Simplify the set of problem clauses:
    if (decisionLevel() == 0) {
        if (!dataSync->syncData()) return l_False;
        if (!simplify()) return l_False;
    }

    // Reduce the set of learnt clauses:
    if (conflicts >= numCleanedLearnts * nbClBeforeRed + nbCompensateSubsumer) {
        numCleanedLearnts ++;
        reduceDB();
        nbClBeforeRed += 500;
    }

    Lit next = lit_Undef;
    while (decisionLevel() < assumptions.size()) {
        // Perform user provided assumption:
        Lit p = assumptions[decisionLevel()];
        if (value(p) == l_True) {
            // Dummy decision level:
            newDecisionLevel();
        } else if (value(p) == l_False) {
            analyzeFinal(~p, conflict);
            return l_False;
        } else {
            next = p;
            break;
        }
    }

    if (next == lit_Undef) {
        // New variable decision:
        decisions++;
        next = pickBranchLit();

        if (next == lit_Undef)
            return l_True;
    }

    // Increase decision level and enqueue 'next'
    assert(value(next) == l_Undef);
    newDecisionLevel();
    uncheckedEnqueue(next);

    return l_Nothing;
}

/**
@brief Handles a conflict that we reached through propagation

Handles on-the-fly subsumption: the OTF subsumption check is done in
conflict analysis, but this is the code that actually replaces the original
clause with that of the shorter one
@returns l_False if UNSAT
*/
llbool Solver::handle_conflict(vec<Lit>& learnt_clause, PropBy confl, uint64_t& conflictC, const bool update)
{
    #ifdef VERBOSE_DEBUG
    cout << "Handling conflict: ";
    for (uint32_t i = 0; i < learnt_clause.size(); i++)
        cout << learnt_clause[i].var()+1 << ",";
    cout << endl;
    #endif

    uint32_t backtrack_level;
    uint32_t glue;

    delayedEnqueueUpdate();
    conflicts++;
    conflictC++;
    if (decisionLevel() == 0)
        return l_False;
    learnt_clause.clear();
    Clause* c = analyze(confl, learnt_clause, backtrack_level, glue, update);
    if (update) {
        avgBranchDepth.push(decisionLevel());
        glueHistory.push(glue);
        conflSizeHist.push(learnt_clause.size());
    }

    #ifdef STATS_NEEDED
    if (dynamic_behaviour_analysis)
        logger.conflict(Logger::simple_confl_type, backtrack_level, confl->getGroup(), learnt_clause);
    #endif
    cancelUntil(backtrack_level);

    #ifdef VERBOSE_DEBUG
    cout << "Learning:";
    for (uint32_t i = 0; i < learnt_clause.size(); i++) printLit(learnt_clause[i]), cout << " ";
    cout << endl;
    cout << "reverting var " << learnt_clause[0].var()+1 << " to " << !learnt_clause[0].sign() << endl;
    #endif

    assert(value(learnt_clause[0]) == l_Undef);
    //Unitary learnt
    if (learnt_clause.size() == 1) {
        uncheckedEnqueue(learnt_clause[0]);
        assert(backtrack_level == 0 && "Unit clause learnt, so must cancel until level 0, right?");

        #ifdef VERBOSE_DEBUG
        cout << "Unit clause learnt." << endl;
        #endif
    //Normal learnt
    } else {
        bumpUIPPolCount(learnt_clause);
        if (learnt_clause.size() == 2) {
            attachBinClause(learnt_clause[0], learnt_clause[1], true);
            numNewBin++;
            dataSync->signalNewBinClause(learnt_clause);
            uncheckedEnqueue(learnt_clause[0], PropBy(learnt_clause[1]));
            goto end;
        }

        std::sort(learnt_clause.getData()+1, learnt_clause.getDataEnd(), PolaritySorter(varData));
        if (c) { //On-the-fly subsumption
            uint32_t origSize = c->size();
            detachClause(*c);
            for (uint32_t i = 0; i != learnt_clause.size(); i++)
                (*c)[i] = learnt_clause[i];
            c->shrink(origSize - learnt_clause.size());
            if (c->learnt() && c->getGlue() > glue)
                c->setGlue(glue); // LS
            attachClause(*c);
            uncheckedEnqueue(learnt_clause[0], PropBy(clauseAllocator.getOffset(c), 0));
        } else {  //no on-the-fly subsumption
            #ifdef STATS_NEEDED
            if (dynamic_behaviour_analysis)
                logger.set_group_name(c->getGroup(), "learnt clause");
            #endif
            c = clauseAllocator.Clause_new(learnt_clause, learnt_clause_group++, true);
            if (conf.doMaxGlueDel && glue > conf.maxGlue) {
                nbClOverMaxGlue++;
                nbCompensateSubsumer++;
                unWindGlue[learnt_clause[0].var()] = c;
                #ifdef UNWINDING_DEBUG
                std::cout << "unwind, var:" << learnt_clause[0].var() << std::endl;
                c->plainPrint();
                #endif //VERBOSE_DEBUG
            } else {
                learnts.push(c);
            }
            c->setGlue(std::min(glue, MAX_THEORETICAL_GLUE));
            attachClause(*c);
            uncheckedEnqueue(learnt_clause[0], PropBy(clauseAllocator.getOffset(c), 0));
        }
        end:;
    }

    varDecayActivity();

    return l_Nothing;
}

void Solver::bumpUIPPolCount(const vec<Lit>& lits)
{
    for (const Lit *l = lits.getData(), *end = lits.getDataEnd(); l != end; l++) {
        uint32_t factor = 0;
        if (l == lits.getData()) factor = 1;
        if (l->sign()) lTPolCount[l->var()].second += factor;
        else lTPolCount[l->var()].first += factor;
    }
}

/**
@brief After a full restart, determines which restar type to use

Uses class RestartTypeChooser to do the heavy-lifting
*/
const bool Solver::chooseRestartType(const uint32_t& lastFullRestart)
{
    uint32_t relativeStart = starts - lastFullRestart;

    if (relativeStart > RESTART_TYPE_DECIDER_FROM  && relativeStart < RESTART_TYPE_DECIDER_UNTIL) {
        if (conf.fixRestartType == auto_restart)
            restartTypeChooser->addInfo();

        if (relativeStart == (RESTART_TYPE_DECIDER_UNTIL-1)) {
            RestartType tmp;
            if (conf.fixRestartType == auto_restart) {
                tmp = restartTypeChooser->choose();
            } else
                tmp = conf.fixRestartType;

            if (tmp == dynamic_restart) {
                if (conf.verbosity >= 1)
                    std::cout << "c Decided on dynamic restart strategy"
                    << std::endl;
                conf.agilityLimit = 0.2;
            } else  {
                if (conf.verbosity >= 1)
                    std::cout << "c Decided on static restart strategy"
                    << std::endl;
                conf.agilityLimit = 0.1;
            }
            if (!matrixFinder->findMatrixes()) return false;

            subRestartType = tmp;
            restartType = dynamic_restart;
            restartTypeChooser->reset();
        }
    }

    return true;
}

inline void Solver::setDefaultRestartType()
{
    if (conf.fixRestartType != auto_restart) restartType = conf.fixRestartType;
    else restartType = static_restart;

    glueHistory.clear();
    glueHistory.initSize(MIN_GLUE_RESTART);
    conflSizeHist.clear();
    conflSizeHist.initSize(1000);

    subRestartType = restartType;
}

/**
@brief The function that brings together almost all CNF-simplifications

It burst-searches for given number of conflicts, then it tries all sorts of
things like variable elimination, subsumption, failed literal probing, etc.
to try to simplifcy the problem at hand.
*/
const lbool Solver::simplifyProblem(const uint32_t numConfls)
{
    testAllClauseAttach();
    bool gaussWasCleared = clearGaussMatrixes();

    reArrangeClauses();
    StateSaver savedState(*this);;

    #ifdef BURST_SEARCH
    if (conf.verbosity >= 3)
        std::cout << "c " << std::setw(24) << " "
        << "Simplifying problem for " << std::setw(8) << numConfls << " confls"
        << std::endl;
    conf.random_var_freq = 1;
    simplifying = true;
    uint64_t origConflicts = conflicts;
    #endif //BURST_SEARCH

    lbool status = l_Undef;

    #ifdef BURST_SEARCH
    restartType = static_restart;

    printRestartStat("S");
    while(status == l_Undef && conflicts-origConflicts < numConfls && needToInterrupt == false) {
        status = search(100, std::numeric_limits<uint64_t>::max(), false);
    }
    if (needToInterrupt) return l_Undef;
    printRestartStat("S");
    if (status != l_Undef) goto end;
    #endif //BURST_SEARCH

    if (conf.doXorSubsumption && !xorSubsumer->simplifyBySubsumption()) goto end;

    if (conf.doFailedLit && !failedLitSearcher->search()) goto end;

    if (needToInterrupt) return l_Undef;

    if (conf.doClausVivif && !clauseVivifier->calcAndSubsume()) goto end;
    if (conf.doSatELite && !subsumer->simplifyBySubsumption(false)) goto end;
    if (conf.doClausVivif && !clauseVivifier->vivify()) goto end;
    if (conf.doSatELite && !subsumer->simplifyBySubsumption(true)) goto end;

    /*if (findNormalXors && xorclauses.size() > 200 && clauses.size() < MAX_CLAUSENUM_XORFIND/8) {
        XorFinder xorFinder(*this, clauses, ClauseCleaner::clauses);
        if (!xorFinder.doNoPart(3, 7)) {
            status = l_False;
            goto end;
        }
    } else*/ if (xorclauses.size() <= 200 && xorclauses.size() > 0 && nClauses() > 10000) {
        XorFinder x(*this, clauses);
        x.addAllXorAsNorm();
    }

    //addSymmBreakClauses();

    if (conf.doSortWatched) sortWatched();
    if (conf.doCacheOTFSSR &&  conf.doCalcReach) calcReachability();

    //Free memory if possible
    for (Var var = 0; var < nVars(); var++) {
        if (value(var) != l_Undef) {
            vector<LitExtra> tmp1;
            transOTFCache[Lit(var, false).toInt()].lits.swap(tmp1);
            vector<LitExtra> tmp2;
            transOTFCache[Lit(var, true).toInt()].lits.swap(tmp2);
        }
    }

end:
    #ifdef BURST_SEARCH
    if (conf.verbosity >= 3)
        std::cout << "c Simplifying finished" << std::endl;
    #endif //#ifdef BURST_SEARCH

    savedState.restore();
    simplifying = false;

    if (status == l_Undef && gaussWasCleared && !matrixFinder->findMatrixes())
        status = l_False;

    testAllClauseAttach();
    checkNoWrongAttach();
    numSimplifyRounds++;

    if (!ok) return l_False;
    return status;
}

void Solver::reArrangeClause(Clause* clause)
{
    Clause& c = *clause;
    if (c.size() == 3) return;

    ClauseData& data = clauseData[c.getNum()];
    Lit lit1 = c[data[0]];
    Lit lit2 = c[data[1]];
    assert(lit1 != lit2);

    std::sort(c.getData(), c.getDataEnd(), PolaritySorter(varData));

    uint32_t foundDatas = 0;
    for (uint32_t i = 0; i < c.size(); i++) {
        if (c[i] == lit1) {
            data[0] = i;
            foundDatas++;
        }
        if (c[i] == lit2) {
            data[1] = i;
            foundDatas++;
        }
    }
    assert(foundDatas == 2);
}

void Solver::reArrangeClauses()
{
    assert(decisionLevel() == 0);
    assert(ok);

    double myTime = cpuTime();
    for (uint32_t i = 0; i < clauses.size(); i++) {
        reArrangeClause(clauses[i]);
    }
    for (uint32_t i = 0; i < learnts.size(); i++) {
        reArrangeClause(learnts[i]);
    }

    for (Var v = 0; v < nVars(); v++) {
        varData[v].popularity = 0;
    }

    if (conf.verbosity >= 3) {
        std::cout << "c Time to rearrange lits in clauses " << (cpuTime() - myTime) << std::endl;
    }
}

/**
@brief Should we perform a full restart?

If so, we also do the things to be done if the full restart is effected.
Currently, this means we try to find disconnected components and solve
them with sub-solvers using class PartHandler
*/
const bool Solver::fullRestart(uint32_t& lastFullRestart)
{
    #ifdef USE_GAUSS
    clearGaussMatrixes();
    #endif //USE_GAUSS

    restartType = static_restart;
    subRestartType = static_restart;
    lastFullRestart = starts;
    fullStarts++;

    if (conf.verbosity >= 3)
        std::cout << "c Fully restarting" << std::endl;
    printRestartStat("F");

    /*if (findNormalXors && clauses.size() < MAX_CLAUSENUM_XORFIND) {
        XorFinder xorFinder(this, clauses, ClauseCleaner::clauses);
        if (!xorFinder.doNoPart(3, 10))
            return false;
    }*/

    if (conf.doPartHandler && !partHandler->handle())
        return false;

    return true;
}

/**
@brief Performs a set of pre-optimisations before the beggining of solving

This is somewhat different than the set of optimisations carried out during
solving in simplifyProblem(). For instance, binary xors are searched fully
here, while there, no search for them is carried out. Also, the ordering
is different.

\todo experiment to use simplifyProblem() instead of this, with the only
addition of binary clause search. Maybe it will do just as good (or better).
*/
void Solver::performStepsBeforeSolve()
{
    assert(qhead == trail.size());
    testAllClauseAttach();

    printRestartStat();
    if (conf.doReplace && !varReplacer->performReplace()) return;

    if (conf.doClausVivif && !conf.libraryUsage
        && !clauseVivifier->vivify()) return;

    bool saveDoHyperBin = conf.doHyperBinRes;
    conf.doHyperBinRes = false;
    clauseAllocator.consolidate(this, true);
    if (conf.doFailedLit && !failedLitSearcher->search()) return;
    conf.doHyperBinRes = saveDoHyperBin;

    #ifdef VERBOSE_DEBUG
    printAllClauses();
    #endif //VERBOSE_DEBUG

    if (conf.doClausVivif && !conf.libraryUsage
        && !clauseVivifier->calcAndSubsume())
        return;

    if (conf.doSatELite
        && !conf.libraryUsage
        && clauses.size() < 4800000
        && !subsumer->simplifyBySubsumption())
        return;

    if (conf.doFindEqLits) {
        if (!sCCFinder->find2LongXors()) return;
        lastNbBin = numNewBin;
        if (conf.doReplace && !varReplacer->performReplace(true)) return;
    }

    if (conf.doFindXors && clauses.size() < MAX_CLAUSENUM_XORFIND) {
        XorFinder xorFinder(*this, clauses);
        if (!xorFinder.fullFindXors(3, 7)) return;
    }

    if (xorclauses.size() > 1) {
        if (conf.doXorSubsumption && !xorSubsumer->simplifyBySubsumption())
            return;

        if (conf.doReplace && !varReplacer->performReplace())
            return;
    }

    if (conf.doSortWatched) sortWatched();
    if (conf.doCacheOTFSSR && conf.doCalcReach) calcReachability();

    testAllClauseAttach();
}

/**
@brief Initialises model, restarts, learnt cluause cleaning, burst-search, etc.
*/
void Solver::initialiseSolver()
{
    //Clear up previous stuff like model, final conflict, matrixes
    model.clear();
    conflict.clear();
    clearGaussMatrixes();

    //Initialise restarts & dynamic restart datastructures
    setDefaultRestartType();

    //Initialise avg. branch depth
    avgBranchDepth.clear();
    avgBranchDepth.initSize(500);

    //Initialise number of restarts&full restarts
    starts = 0;
    fullStarts = 0;

    if (nClauses() * conf.learntsize_factor < nbClBeforeRed) {
        if (nClauses() * conf.learntsize_factor < nbClBeforeRed/2)
            nbClBeforeRed /= 4;
        else
            nbClBeforeRed = (nClauses() * conf.learntsize_factor)/2;
    }

    testAllClauseAttach();
    findAllAttach();
}

/**
@brief The main solve loop that glues everything together

We clear everything needed, pre-simplify the problem, calculate default
polarities, and start the loop. Finally, we either report UNSAT or extend the
found solution with all the intermediary simplifications (e.g. variable
elimination, etc.) and output the solution.
*/
const lbool Solver::solve(const vec<Lit>& assumps, const int _numThreads , const int _threadNum)
{
    numThreads = _numThreads;
    threadNum = _threadNum;
    #ifdef VERBOSE_DEBUG
    std::cout << "Solver::solve() called" << std::endl;
    #endif
    if (!ok) return l_False;
    assert(qhead == trail.size());
    assert(subsumer->checkElimedUnassigned());
    assert(xorSubsumer->checkElimedUnassigned());

    if (libraryCNFFile)
        fprintf(libraryCNFFile, "c Solver::solve() called\n");

    assumps.copyTo(assumptions);
    initialiseSolver();
    uint64_t  nof_conflicts = conf.restart_first; //Geometric restart policy, start with this many
    uint32_t  lastFullRestart = starts; //last time a full restart was made was at this number of restarts
    lbool     status = l_Undef; //Current status
    uint64_t  nextSimplify = conf.restart_first * conf.simpStartMult + conflicts; //Do simplifyProblem() at this number of conflicts
    if (!conf.doSchedSimp) nextSimplify = std::numeric_limits<uint64_t>::max();

    uint64_t oldConflicts = conflicts;
    if (conflicts == 0) {
        if (conf.doPerformPreSimp) performStepsBeforeSolve();
        if (!ok) return l_False;
    }
    calculateDefaultPolarities();

    bool firstRearrangeDone = false;
    printStatHeader();
    printRestartStat("B");
    uint64_t lastConflPrint = conflicts;
    // Search:
    while (status == l_Undef && starts < conf.maxRestarts) {
        #ifdef DEBUG_VARELIM
        assert(subsumer->checkElimedUnassigned());
        assert(xorSubsumer->checkElimedUnassigned());
        #endif //DEBUG_VARELIM

        if ((conflicts - lastConflPrint) > std::min(std::max(conflicts/100*6, (uint64_t)4000), (uint64_t)20000)) {
            printRestartStat("N");
            lastConflPrint = conflicts;
        }
        if (conflicts-oldConflicts > 5000 && numSimplifyRounds == 0 && !firstRearrangeDone) {
            reArrangeClauses();
            firstRearrangeDone = true;
        }

        if (conf.doSchedSimp && conflicts >= nextSimplify) {
            status = simplifyProblem(conf.simpBurstSConf);
            printRestartStat();
            lastConflPrint = conflicts;
            nextSimplify = std::min((uint64_t)((double)conflicts * conf.simpStartMMult), conflicts + MAX_CONFL_BETWEEN_SIMPLIFY);
            if (status != l_Undef) break;

            if ((numSimplifyRounds == 1 ||  numSimplifyRounds % 5 == 3)) {
                if (!fullRestart(lastFullRestart)) return l_False;
                nof_conflicts = conf.restart_first;
            }
            if (numSimplifyRounds % 3 == 0) nof_conflicts = conf.restart_first;
        }
        if (!chooseRestartType(lastFullRestart)) return l_False;

        #ifdef STATS_NEEDED
        if (dynamic_behaviour_analysis) {
            logger.end(Logger::restarting);
            logger.begin();
        }
        #endif

        status = search(nof_conflicts, nextSimplify);
        if (needToInterrupt) {
            cancelUntil(0);
            return l_Undef;
        }
        if (status != l_Undef) break;

        nof_conflicts = (double)nof_conflicts * conf.restart_inc;
    }
    printEndSearchStat();

    #ifdef VERBOSE_DEBUG
    if (status == l_True)
        std::cout << "Solution  is SAT" << std::endl;
    else if (status == l_False)
        std::cout << "Solution is UNSAT" << std::endl;
    else
        std::cout << "Solutions is UNKNOWN" << std::endl;
    #endif //VERBOSE_DEBUG

    if (status == l_True) handleSATSolution();
    else if (status == l_False) handleUNSATSolution();

    #ifdef STATS_NEEDED
    if (dynamic_behaviour_analysis) {
        if (status == l_True)
            logger.end(Logger::model_found);
        else if (status == l_False)
            logger.end(Logger::unsat_model_found);
        else if (status == l_Undef)
            logger.end(Logger::restarting);
    }
    #endif

    cancelUntil(0);
    if (conf.doPartHandler && status != l_False) partHandler->readdRemovedClauses();
    restartTypeChooser->reset();

    #ifdef VERBOSE_DEBUG
    std::cout << "Solver::solve() finished" << std::endl;
    #endif
    return status;
}

/**
@brief Extends a SAT solution to the full solution

variable elimination, variable replacement, sub-part solving, etc. all need to
be handled correctly to arrive at a solution that is a solution to ALL of the
original problem, not just of what remained of it at the end inside this class
(i.e. we need to combine things from the helper classes)
*/
void Solver::handleSATSolution()
{

    /*if (greedyUnbound) {
        double time = cpuTime();
        FindUndef finder(*this);
        const uint32_t unbounded = finder.unRoll();
        if (conf.verbosity >= 1)
            printf("c Greedy unbounding     :%5.2lf s, unbounded: %7d vars\n", cpuTime()-time, unbounded);
    }*/
    assert(subsumer->checkElimedUnassigned());
    assert(xorSubsumer->checkElimedUnassigned());

    partHandler->addSavedState();
    varReplacer->extendModelPossible();
    checkSolution();

    if (subsumer->getNumElimed() || xorSubsumer->getNumElimed()) {
        if (conf.verbosity >= 1) {
            std::cout << "c Solution needs extension. Extending." << std::endl;
        }
        Solver s;
        s.conf = conf;
        s.conf.doSatELite = false;
        s.conf.doReplace = false;
        s.conf.doFindEqLits = false;
        s.conf.doRegFindEqLits = false;
        s.conf.doFailedLit= false;
        s.conf.doConglXors = false;
        s.conf.doSubsWNonExistBins = false;
        s.conf.doRemUselessBins = false;
        s.conf.doClausVivif = false;
        s.conf.doPartHandler = false;
        s.conf.doSortWatched = false;
        s.conf.verbosity = 0;

        vec<Lit> tmp;
        for (Var var = 0; var < nVars(); var++) {
            s.newVar(decision_var[var] || subsumer->getVarElimed()[var] || varReplacer->varHasBeenReplaced(var) || xorSubsumer->getVarElimed()[var]);

            //assert(!(xorSubsumer->getVarElimed()[var] && (decision_var[var] || subsumer->getVarElimed()[var] || varReplacer->varHasBeenReplaced(var))));

            if (value(var) != l_Undef) {
                #ifdef VERBOSE_DEBUG
                std::cout << "Setting var " << var + 1
                << " in extend-solver to " << value(var) << std::endl;
                #endif
                tmp.clear();
                tmp.push(Lit(var, value(var) == l_False));
                s.addClause(tmp);
            }
        }
        varReplacer->extendModelImpossible(s);
        subsumer->extendModel(s);
        xorSubsumer->extendModel(s);

        lbool status = s.solve();
        release_assert(status == l_True && "c ERROR! Extension of model failed!");
#ifdef VERBOSE_DEBUG
        std::cout << "Solution extending finished. Enqueuing results" << std::endl;
#endif
        for (Var var = 0; var < nVars(); var++) {
            if (assigns[var] == l_Undef && s.model[var] != l_Undef) uncheckedEnqueueExtend(Lit(var, s.model[var] == l_False));
        }
        ok = (propagate<true>().isNULL());
        release_assert(ok && "c ERROR! Extension of model failed!");
    }
    checkSolution();
    //Copy model:
    model.growTo(nVars());
    for (Var var = 0; var != nVars(); var++) model[var] = value(var);
}

/**
@brief When problem is decided to be UNSAT, this is called

There is basically nothing to be handled for the moment, but this could be
made extensible
*/
void Solver::handleUNSATSolution()
{
    if (conflict.size() == 0)
        ok = false;
}
