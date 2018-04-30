/******************************************
Copyright (c) 2016, Mate Soos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
***********************************************/

#include "xorfinder.h"
#include "time_mem.h"
#include "solver.h"
#include "occsimplifier.h"
#include "clauseallocator.h"
#include "sqlstats.h"

#include <limits>
//#define XOR_DEBUG

using namespace CMSat;
using std::cout;
using std::endl;

XorFinder::XorFinder(OccSimplifier* _occsimplifier, Solver* _solver) :
    occsimplifier(_occsimplifier)
    , solver(_solver)
    , toClear(_solver->toClear)
{
}

void XorFinder::find_xors_based_on_long_clauses()
{
    #ifdef DEBUG_MARKED_CLAUSE
    assert(solver->no_marked_clauses());
    #endif

    vector<Lit> lits;
    for (vector<ClOffset>::iterator
        it = occsimplifier->clauses.begin()
        , end = occsimplifier->clauses.end()
        ; it != end && xor_find_time_limit > 0
        ; ++it
    ) {
        ClOffset offset = *it;
        Clause* cl = solver->cl_alloc.ptr(offset);
        xor_find_time_limit -= 1;

        //Already freed
        if (cl->freed() || cl->getRemoved()) {
            continue;
        }

        //Too large -> too expensive
        if (cl->size() > solver->conf.maxXorToFind) {
            continue;
        }

        //Let's not take learnt clauses as bases for XORs
        if (cl->red()) {
            continue;
        }

        //If not tried already, find an XOR with it
        if (!cl->stats.marked_clause ) {
            cl->stats.marked_clause = true;
            assert(!cl->getRemoved());

            size_t needed_per_ws = 1ULL << (cl->size()-2);
            //let's allow shortened clauses
            needed_per_ws >>= 1;

            for(const Lit lit: *cl) {
                if (solver->watches[lit].size() < needed_per_ws) {
                    goto next;
                }
                if (solver->watches[~lit].size() < needed_per_ws) {
                    goto next;
                }
            }

            lits.resize(cl->size());
            std::copy(cl->begin(), cl->end(), lits.begin());
            findXor(lits, offset, cl->abst);
            next:;
        }
    }
}

void XorFinder::find_xors()
{
    runStats.clear();
    runStats.numCalls = 1;
    grab_mem();

    xors.clear();
    double myTime = cpuTime();
    const int64_t orig_xor_find_time_limit =
        1000LL*1000LL*solver->conf.xor_finder_time_limitM
        *solver->conf.global_timeout_multiplier;

    xor_find_time_limit = orig_xor_find_time_limit;

    occsimplifier->sort_occur_lists_and_set_blocked_size();
    if (solver->conf.verbosity) {
        cout << "c [occ-xor] sort occur list T: " << (cpuTime()-myTime) << endl;
    }

    #ifdef DEBUG_MARKED_CLAUSE
    assert(solver->no_marked_clauses());
    #endif

    find_xors_based_on_long_clauses();
    assert(runStats.foundXors == xors.size());

    //clean them of equivalent XORs
    if (!xors.empty()) {
        for(Xor& x: xors) {
            std::sort(x.begin(), x.end());
        }
        std::sort(xors.begin(), xors.end());

        vector<Xor>::iterator i = xors.begin();
        vector<Xor>::iterator j = i;
        i++;
        uint64_t size = 1;
        for(vector<Xor>::iterator end = xors.end(); i != end; i++) {
            if (*j != *i) {
                j++;
                *j = *i;
                size++;
            }
        }
        xors.resize(size);
    }

    //Cleanup
    for(ClOffset offset: occsimplifier->clauses) {
        Clause* cl = solver->cl_alloc.ptr(offset);
        cl->stats.marked_clause = false;
    }

    //Print stats
    const bool time_out = (xor_find_time_limit < 0);
    const double time_remain = float_div(xor_find_time_limit, orig_xor_find_time_limit);
    runStats.findTime = cpuTime() - myTime;
    runStats.time_outs += time_out;
    solver->sumSearchStats.num_xors_found_last = xors.size();
    print_found_xors();

    if (solver->conf.verbosity) {
        runStats.print_short(solver, time_remain);
    }
    globalStats += runStats;

    if (solver->sqlStats) {
        solver->sqlStats->time_passed(
            solver
            , "xor-find"
            , cpuTime() - myTime
            , time_out
            , time_remain
        );
    }
}

void XorFinder::print_found_xors()
{
    if (solver->conf.verbosity >= 5) {
        cout << "c Found XORs: " << endl;
        for(vector<Xor>::const_iterator
            it = xors.begin(), end = xors.end()
            ; it != end
            ; ++it
        ) {
            cout << "c " << *it << endl;
        }
    }
}

void XorFinder::add_xors_to_gauss()
{
    solver->xorclauses = xors;
    #if defined(SLOW_DEBUG) || defined(XOR_DEBUG)
    for(const Xor& x: xors) {
        for(uint32_t v: x) {
            assert(solver->varData[v].removed == Removed::none);
        }
    }
    #endif
}

void XorFinder::findXor(vector<Lit>& lits, const ClOffset offset, cl_abst_type abst)
{
    //Set this clause as the base for the XOR, fill 'seen'
    xor_find_time_limit -= lits.size()/4+1;
    poss_xor.setup(lits, offset, abst, occcnt);

    //Run findXorMatch for the 2 smallest watchlists
    Lit slit = lit_Undef;
    Lit slit2 = lit_Undef;
    uint32_t smallest = std::numeric_limits<uint32_t>::max();
    uint32_t smallest2 = std::numeric_limits<uint32_t>::max();
    for (size_t i = 0, end = lits.size(); i < end; i++) {
        const Lit lit = lits[i];
        uint32_t num = solver->watches[lit].size();
        num += solver->watches[~lit].size();
        if (num < smallest) {
            slit2 = slit;
            smallest2 = smallest;

            slit = lit;
            smallest = num;
        } else if (num < smallest2) {
            slit2 = lit;
            smallest2 = num;
        }

    }
    findXorMatch(solver->watches[slit], slit);
    findXorMatch(solver->watches[~slit], ~slit);
    findXorMatch(solver->watches[slit2], slit2);
    findXorMatch(solver->watches[~slit2], ~slit2);

    if (poss_xor.foundAll()) {
        std::sort(lits.begin(), lits.end());
        Xor found_xor(lits, poss_xor.getRHS());
        #if defined(SLOW_DEBUG) || defined(XOR_DEBUG)
        for(Lit lit: lits) {
            assert(solver->varData[lit.var()].removed == Removed::none);
        }
        #endif

        add_found_xor(found_xor);
        for(ClOffset offs: poss_xor.get_offsets()) {
            Clause* cl = solver->cl_alloc.ptr(offs);
            assert(!cl->getRemoved());
            cl->set_used_in_xor(true);
        }
    }

    //Clear 'seen'
    for (const Lit tmp_lit: lits) {
        occcnt[tmp_lit.var()] = 0;
    }
}

void XorFinder::add_found_xor(const Xor& found_xor)
{
    xors.push_back(found_xor);
    runStats.foundXors++;
    runStats.sumSizeXors += found_xor.size();
}

void XorFinder::findXorMatch(watch_subarray_const occ, const Lit wlit)
{
    xor_find_time_limit -= (int64_t)occ.size()/8+1;
    for (const Watched& w: occ) {

        if (w.isIdx()) {
            continue;
        }
        assert(poss_xor.getSize() > 2);

        if (w.isBin()) {
            #ifdef SLOW_DEBUG
            assert(occcnt[wlit.var()]);
            #endif
            if (!occcnt[w.lit2().var()]) {
                goto end;
            }

            binvec.clear();
            binvec.resize(2);
            binvec[0] = w.lit2();
            binvec[1] = wlit;
            if (binvec[0] > binvec[1]) {
                std::swap(binvec[0], binvec[1]);
            }

            xor_find_time_limit -= 1;
            poss_xor.add(binvec, std::numeric_limits<ClOffset>::max(), varsMissing);
            if (poss_xor.foundAll())
                break;
        } else {
            if ((w.getBlockedLit().var() | poss_xor.getAbst()) != poss_xor.getAbst())
                continue;

            const ClOffset offset = w.get_offset();
            Clause& cl = *solver->cl_alloc.ptr(offset);
            if (cl.freed() || cl.getRemoved()) {
                //Clauses are ordered!!
                break;
            }

            //Allow the clause to be smaller or equal in size
            if (cl.size() > poss_xor.getSize()) {
                //clauses are ordered!!
                break;
            }

            //Doesn't contain variables not in the original clause
            #if defined(SLOW_DEBUG) || defined(XOR_DEBUG)
            assert(cl.abst == calcAbstraction(cl));
            #endif
            if ((cl.abst | poss_xor.getAbst()) != poss_xor.getAbst())
                continue;

            //Check RHS, vars inside
            bool rhs = true;
            for (const Lit cl_lit :cl) {
                //early-abort, contains literals not in original clause
                if (!occcnt[cl_lit.var()])
                    goto end;

                rhs ^= cl_lit.sign();
            }
            //either the invertedness has to match, or the size must be smaller
            if (rhs != poss_xor.getRHS() && cl.size() == poss_xor.getSize())
                continue;

            //If the size of this clause is the same of the base clause, then
            //there is no point in using this clause as a base for another XOR
            //because exactly the same things will be found.
            if (cl.size() == poss_xor.getSize()) {
                cl.stats.marked_clause = true;;
            }

            xor_find_time_limit -= cl.size()/4+1;
            poss_xor.add(cl, offset, varsMissing);
            if (poss_xor.foundAll())
                break;
        }
        end:;
    }
}

void XorFinder::remove_xors_without_connecting_vars()
{
    assert(toClear.empty());

    //Fill seen with vars used
    for(const Xor& x: xors) {
        for(uint32_t v: x) {
            if (occcnt[v] == 0) {
                toClear.push_back(Lit(v, false));
            }

            if (occcnt[v] < 2) {
                occcnt[v]++;
            }
        }
    }

    vector<Xor>::iterator i = xors.begin();
    vector<Xor>::iterator j = i;
    for(vector<Xor>::iterator end = xors.end()
        ; i != end
        ; i++
    ) {
        if (xor_has_interesting_var(*i)) {
            *j++ = *i;
        }
    }
    xors.resize(xors.size() - (i-j));

    for(Lit l: toClear) {
        occcnt[l.var()] = 0;
    }
    toClear.clear();
}

bool XorFinder::xor_together_xors()
{
    assert(solver->okay());
    assert(solver->decisionLevel() == 0);
    assert(solver->watches.get_smudged_list().empty());
    uint32_t xored = 0;
    const double myTime = cpuTime();
    assert(toClear.empty());
    uint32_t unit_added = 0;
    uint32_t bin_added = 0;

    //Link in xors into watchlist
    for(size_t i = 0; i < xors.size(); i++) {
        const Xor& x = xors[i];
        for(uint32_t v: x) {
            if (occcnt[v] == 0) {
                toClear.push_back(Lit(v, false));
            }
            occcnt[v]++;

            Lit l(v, false);
            assert(solver->watches.size() > l.toInt());
            solver->watches[l].push(Watched(i)); //Idx watch
            solver->watches.smudge(l);
        }
    }

    //until fixedpoint
    bool changed = true;
    while(changed) {
        changed = false;
        interesting.clear();
        for(const Lit l: toClear) {
            if (occcnt[l.var()] == 2) {
                interesting.push_back(l.var());
            }
        }

        while(!interesting.empty()) {
            const uint32_t v = interesting.back();
            interesting.resize(interesting.size()-1);

            Xor x[2];
            size_t idxes[2];
            unsigned at = 0;
            size_t i2 = 0;
            assert(solver->watches.size() > Lit(v, false).toInt());
            watch_subarray ws = solver->watches[Lit(v, false)];

            for(size_t i = 0; i < ws.size(); i++) {
                const Watched& w = ws[i];
                if (!w.isIdx()) {
                    ws[i2++] = ws[i];
                } else if (xors[w.get_idx()] != Xor()) {
                    //seen may be not 2
                    if (at >= 2) {
                        //signal that this is wrong by making "at" too large
                        at++;
                        break;
                    }

                    idxes[at] = w.get_idx();
                    at++;
                }
            }
            if (at != 2) {
                continue;
            }

            ws.resize(i2);
            Xor x_new(xor_two(xors[idxes[0]], xors[idxes[1]]),
                      xors[idxes[0]].rhs ^ xors[idxes[1]].rhs);

            //check x_new
            bool add = true;
            if (x_new.size() == 1) {
                unit_added++;
                Lit l(x_new[0], !x_new.rhs);
                if (solver->value(l) == l_False) {
                    solver->ok = false;
                    goto end;
                } else if (solver->value(l) == l_Undef) {
                    solver->enqueue(l);
                    solver->ok = solver->propagate_occur();
                    if (!solver->ok) {
                        goto end;
                    }
                }
                add = false;
            }

            changed = true;
            if (add) {
                xors.push_back(x_new);
                for(uint32_t v2: x_new) {
                    Lit l(v2, false);
                    solver->watches[l].push(Watched(xors.size()-1));
                    if (occcnt[l.var()] == 2) {
                        interesting.push_back(l.var());
                    }
                    solver->watches.smudge(l);
                }
            }
            xors[idxes[0]] = Xor();
            xors[idxes[1]] = Xor();
            xored++;
        }
    }
    end:

    for(const Lit l: toClear) {
        occcnt[l.var()] = 0;
    }
    toClear.clear();

    solver->clean_occur_from_idx_types_only_smudged();
    clean_xors_from_empty();
    double recur_time = cpuTime() - myTime;
        if (solver->conf.verbosity) {
        cout
        << "c [occ-xor] xored together " << xored
        << " cls "
        << " unit: " << unit_added << " bin: " << bin_added << " "
        << solver->conf.print_times(recur_time)
        << endl;
    }


    if (solver->sqlStats) {
        solver->sqlStats->time_passed_min(
            solver
            , "xor-xor-together"
            , recur_time
        );
    }

    #if defined(SLOW_DEBUG) || defined(XOR_DEBUG)
    //Make sure none is 2.
    assert(toClear.empty());
    for(const Xor& x: xors) {
        for(uint32_t v: x) {
            if (solver->seen[v] == 0) {
                toClear.push_back(Lit(v, false));
            }

            //Don't roll around
            if (solver->seen[v] != std::numeric_limits<uint16_t>::max()) {
                solver->seen[v]++;
            }
        }
    }

    for(const Lit c: toClear) {
        assert(solver->seen[c.var()] != 2);
        solver->seen[c.var()] = 0;
    }
    toClear.clear();
    #endif

    return solver->ok;
}

void XorFinder::clean_xors_from_empty()
{
    size_t i2 = 0;
    for(size_t i = 0;i < xors.size(); i++) {
        Xor& x = xors[i];
        if (x.size() == 0
            && x.rhs == false
        ) {
            //nothing, skip
        } else {
            xors[i2] = xors[i];
            i2++;
        }
    }
    xors.resize(i2);
}

bool XorFinder::add_new_truths_from_xors(vector<Xor>& this_xors)
{
    size_t origTrailSize  = solver->trail_size();
    size_t origBins = solver->binTri.redBins;
    double myTime = cpuTime();

    assert(solver->ok);
    size_t i2 = 0;
    for(size_t i = 0;i < this_xors.size(); i++) {
        Xor& x = this_xors[i];
        if (x.size() > 2) {
            this_xors[i2] = this_xors[i];
            i2++;
            continue;
        }

        switch(x.size() ) {
            case 0: {
                if (x.rhs == true) {
                    solver->ok = false;
                    return false;
                }
                break;
            }

            case 1: {
                vector<Lit> lits = {Lit(x[0], !x.rhs)};
                solver->add_clause_int(lits, true, ClauseStats(), false);
                if (!solver->ok) {
                    return false;
                }
                break;
            }

            case 2: {
                //RHS == 1 means both same is not allowed
                vector<Lit> lits{Lit(x[0], false), Lit(x[1], true^x.rhs)};
                solver->add_clause_int(lits, true, ClauseStats(), false);
                if (!solver->ok) {
                    return false;
                }
                lits = {Lit(x[0], true), Lit(x[1], false^x.rhs)};
                solver->add_clause_int(lits, true, ClauseStats(), false);
                if (!solver->ok) {
                    return false;
                }
                break;
            }

            default: {
                assert(false && "Not possible");
            }
        }
    }
    this_xors.resize(i2);

    double add_time = cpuTime() - myTime;
    uint32_t num_bins_added = solver->binTri.redBins - origBins;
    uint32_t num_units_added = solver->trail_size() - origTrailSize;

    if (solver->conf.verbosity) {
        cout
        << "c [occ-xor] added unit " << num_units_added
        << " bin " << num_bins_added
        << solver->conf.print_times(add_time)
        << endl;
    }


    if (solver->sqlStats) {
        solver->sqlStats->time_passed_min(
            solver
            , "xor-add-new-bin-unit"
            , add_time
        );
    }

    return true;
}

vector<uint32_t> XorFinder::xor_two(
    Xor& x1, Xor& x2
) {
    x1.sort();
    x2.sort();
    vector<uint32_t> ret;
    size_t x1_at = 0;
    size_t x2_at = 0;
    while(x1_at < x1.size() || x2_at < x2.size()) {
        if (x1_at == x1.size()) {
            ret.push_back(x2[x2_at]);
            x2_at++;
            continue;
        }

        if (x2_at == x2.size()) {
            ret.push_back(x1[x1_at]);
            x1_at++;
            continue;
        }

        const uint32_t a = x1[x1_at];
        const uint32_t b = x2[x2_at];
        if (a == b) {
            x1_at++;
            x2_at++;

            assert(occcnt[a] >= 2);
            occcnt[a] -= 2;
            if (occcnt[a] == 2) {
                interesting.push_back(a);
            }
            continue;
        }

        if (a < b) {
            ret.push_back(a);
            x1_at++;
            continue;
        } else {
            ret.push_back(b);
            x2_at++;
            continue;
        }
    }

    return ret;
}

bool XorFinder::xor_has_interesting_var(const Xor& x)
{
    for(uint32_t v: x) {
        if (occcnt[v] > 1) {
            return true;
        }
    }
    return false;
}

size_t XorFinder::mem_used() const
{
    size_t mem = 0;
    mem += xors.capacity()*sizeof(Xor);

    //Temporary
    mem += tmpClause.capacity()*sizeof(Lit);
    mem += varsMissing.capacity()*sizeof(uint32_t);

    return mem;
}

void XorFinder::grab_mem()
{
    occcnt.clear();
    occcnt.resize(solver->nVars(), 0);
}

void XorFinder::free_mem()
{
    occcnt.clear();
    occcnt.shrink_to_fit();
}

void XorFinder::Stats::print_short(const Solver* solver, double time_remain) const
{
    cout
    << "c [occ-xor] found " << std::setw(6) << foundXors
    << " avg sz " << std::setw(4) << std::fixed << std::setprecision(1)
    << float_div(sumSizeXors, foundXors)
    << solver->conf.print_times(findTime, time_outs, time_remain)
    << endl;
}

void XorFinder::Stats::print() const
{
    cout << "c --------- XOR STATS ----------" << endl;
    print_stats_line("c num XOR found on avg"
        , float_div(foundXors, numCalls)
        , "avg size"
    );

    print_stats_line("c XOR avg size"
        , float_div(sumSizeXors, foundXors)
    );

    print_stats_line("c XOR finding time"
        , findTime
        , float_div(time_outs, numCalls)*100.0
        , "time-out"
    );
    cout << "c --------- XOR STATS END ----------" << endl;
}

XorFinder::Stats& XorFinder::Stats::operator+=(const XorFinder::Stats& other)
{
    //Time
    findTime += other.findTime;

    //XOR
    foundXors += other.foundXors;
    sumSizeXors += other.sumSizeXors;

    //Usefulness
    time_outs += other.time_outs;

    return *this;
}
