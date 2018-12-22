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

#include "reducedb.h"
#include "solver.h"
#include "solverconf.h"
#include "sqlstats.h"
#ifdef FINAL_PREDICTOR
#include "all_predictors.h"
#include "clustering.h"
#endif

#include <functional>
#include <cmath>

using namespace CMSat;

struct SortRedClsGlue
{
    explicit SortRedClsGlue(ClauseAllocator& _cl_alloc) :
        cl_alloc(_cl_alloc)
    {}
    ClauseAllocator& cl_alloc;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        const Clause* x = cl_alloc.ptr(xOff);
        const Clause* y = cl_alloc.ptr(yOff);
        return x->stats.glue < y->stats.glue;
    }
};

struct SortRedClsSize
{
    explicit SortRedClsSize(ClauseAllocator& _cl_alloc) :
        cl_alloc(_cl_alloc)
    {}
    ClauseAllocator& cl_alloc;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        const Clause* x = cl_alloc.ptr(xOff);
        const Clause* y = cl_alloc.ptr(yOff);
        return x->size() < y->size();
    }
};

struct SortRedClsAct
{
    explicit SortRedClsAct(ClauseAllocator& _cl_alloc) :
        cl_alloc(_cl_alloc)
    {}
    ClauseAllocator& cl_alloc;

    bool operator () (const ClOffset xOff, const ClOffset yOff) const
    {
        const Clause* x = cl_alloc.ptr(xOff);
        const Clause* y = cl_alloc.ptr(yOff);
        return x->stats.activity > y->stats.activity;
    }
};

ReduceDB::ReduceDB(Solver* _solver) :
    solver(_solver)
{
    #ifdef FINAL_PREDICTOR
    fill_pred_funcs();
    #endif
}

void ReduceDB::sort_red_cls(ClauseClean clean_type)
{
    switch (clean_type) {
        case ClauseClean::glue : {
            std::sort(solver->longRedCls[2].begin(), solver->longRedCls[2].end(), SortRedClsGlue(solver->cl_alloc));
            break;
        }

        case ClauseClean::activity : {
            std::sort(solver->longRedCls[2].begin(), solver->longRedCls[2].end(), SortRedClsAct(solver->cl_alloc));
            break;
        }

        default: {
            assert(false && "Unknown cleaning type");
        }
    }
}

//TODO maybe we chould count binary learnt clauses as well into the
//kept no. of clauses as other solvers do
void ReduceDB::handle_lev2()
{
    #ifdef FINAL_PREDICTOR
    assert(false);
    #endif

    nbReduceDB_lev1++;
    solver->dump_memory_stats_to_sql();

    const double myTime = cpuTime();
    assert(solver->watches.get_smudged_list().empty());

    //lev2 -- clean
    int64_t num_to_reduce = solver->longRedCls[2].size();
    for(unsigned keep_type = 0
        ; keep_type < sizeof(solver->conf.ratio_keep_clauses)/sizeof(double)
        ; keep_type++
    ) {
        const uint64_t keep_num = (double)num_to_reduce*solver->conf.ratio_keep_clauses[keep_type];
        if (keep_num == 0) {
            continue;
        }
        sort_red_cls(static_cast<ClauseClean>(keep_type));
        mark_top_N_clauses(keep_num);
    }
    assert(delayed_clause_free.empty());
    cl_marked = 0;
    cl_ttl = 0;
    cl_locked_solver = 0;
    remove_cl_from_lev2();

    solver->clean_occur_from_removed_clauses_only_smudged();
    for(ClOffset offset: delayed_clause_free) {
        solver->cl_alloc.clauseFree(offset);
    }
    delayed_clause_free.clear();

    #ifdef SLOW_DEBUG
    solver->check_no_removed_or_freed_cl_in_watch();
    #endif

    if (solver->conf.verbosity >= 2) {
        cout << "c [DBclean lev2]"
        << " marked: " << cl_marked
        << " ttl:" << cl_ttl
        << " locked_solver:" << cl_locked_solver
        << solver->conf.print_times(cpuTime()-myTime)
        << endl;
    }

    if (solver->sqlStats) {
        solver->sqlStats->time_passed_min(
            solver
            , "dbclean-lev2"
            , cpuTime()-myTime
        );
    }
    total_time += cpuTime()-myTime;

    last_reducedb_num_conflicts = solver->sumConflicts;
}

void ReduceDB::dump_sql_cl_data(const string& cur_rst_type)
{
    #ifdef STATS_NEEDED
    double myTime = cpuTime();
    assert(solver->sqlStats);
    solver->sqlStats->begin_transaction();
    uint64_t added_to_db = 0;


    vector<ClOffset> all_learnt;
    for(uint32_t lev = 0; lev < solver->longRedCls.size(); lev++) {
        auto& cc = solver->longRedCls[lev];
        for(const auto& offs: cc) {
            Clause* cl = solver->cl_alloc.ptr(offs);
            assert(!cl->getRemoved());
            assert(!cl->freed());
            all_learnt.push_back(offs);
        }
    }

    std::sort(all_learnt.begin(), all_learnt.end(), SortRedClsAct(solver->cl_alloc));
    for(size_t i = 0; i < all_learnt.size(); i++) {
        ClOffset offs = all_learnt[i];
        Clause* cl = solver->cl_alloc.ptr(offs);

        //Only if selected to be dumped
        if (cl->stats.dump_number != std::numeric_limits<uint32_t>::max()) {
            const bool locked = solver->clause_locked(*cl, offs);
            const uint32_t act_ranking_top_10 = std::ceil((double)i/((double)all_learnt.size()/10.0));
            //cout << "Ranking top 10: " << act_ranking_top_10 << " act: " << cl->stats.activity << endl;
            solver->sqlStats->reduceDB(
                solver
                , locked
                , cl
                , cur_rst_type
                , act_ranking_top_10
                , i
            );
            added_to_db++;
            cl->stats.dump_number++;
            cl->stats.reset_rdb_stats();
        }
    }
    solver->sqlStats->end_transaction();

    if (solver->conf.verbosity) {
        cout << "c [sql] added to DB " << added_to_db
        << " dump-ratio: " << solver->conf.dump_individual_cldata_ratio
        << solver->conf.print_times(cpuTime()-myTime)
        << endl;
    }
    #endif
}

void ReduceDB::handle_lev1()
{
    #ifdef FINAL_PREDICTOR
    assert(false);
    #endif

    nbReduceDB_lev1++;
    uint32_t moved_w0 = 0;
    uint32_t used_recently = 0;
    uint32_t non_recent_use = 0;
    double myTime = cpuTime();

    size_t j = 0;
    for(size_t i = 0
        ; i < solver->longRedCls[1].size()
        ; i++
    ) {
        const ClOffset offset = solver->longRedCls[1][i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        if (cl->stats.which_red_array == 0) {
            solver->longRedCls[0].push_back(offset);
            moved_w0++;
        } else if (cl->stats.which_red_array == 2) {
            assert(false && "we should never move up through any other means");
        } else {
            if (!solver->clause_locked(*cl, offset)
                && cl->stats.last_touched + solver->conf.must_touch_lev1_within < solver->sumConflicts
            ) {
                solver->longRedCls[2].push_back(offset);
                cl->stats.which_red_array = 2;

                //when stats are needed, activities are correctly updated
                //across all clauses
                //WARNING this changes the way things behave during STATS relative to non-STATS!
                #ifndef STATS_NEEDED
                cl->stats.activity = 0;
                solver->bump_cl_act<false>(cl);
                #endif
                non_recent_use++;
            } else {
                solver->longRedCls[1][j++] = offset;
                used_recently++;
            }
        }
    }
    solver->longRedCls[1].resize(j);

    if (solver->conf.verbosity >= 2) {
        cout << "c [DBclean lev1]"
        << " used recently: " << used_recently
        << " not used recently: " << non_recent_use
        << " moved w0: " << moved_w0
        << solver->conf.print_times(cpuTime()-myTime)
        << endl;
    }

    if (solver->sqlStats) {
        solver->sqlStats->time_passed_min(
            solver
            , "dbclean-lev1"
            , cpuTime()-myTime
        );
    }
    total_time += cpuTime()-myTime;
}

#ifdef FINAL_PREDICTOR
void ReduceDB::handle_lev1_final_predictor()
{
    assert(delayed_clause_free.empty());
    nbReduceDB_lev1++;
    uint32_t deleted = 0;
    uint32_t kept = 0;
    uint32_t kept_first = 0;
    uint32_t kept_locked = 0;
    double myTime = cpuTime();
    uint32_t largest_dump_no = 0;
    uint32_t moved_w0 = 0;
    uint32_t marked_long_keep = 0;
    uint32_t kept_due_to_lock = 0;

    #ifdef FINAL_PREDICTOR_TOTAL
    assert(solver->longRedCls[0].size() == 0);
    assert(solver->longRedCls[2].size() == 0);
    #endif
    std::sort(solver->longRedCls[1].begin(), solver->longRedCls[1].end(), SortRedClsAct(solver->cl_alloc));

    const Clustering* long_clust = get_long_cluster(solver->conf.pred_conf);
    if (long_clust == NULL) {
        cout << "ERROR: You gave a config number in '--pred' that does not exist for LONG" << endl;
        exit(-1);
    }
    int long_cluster = long_clust->which_is_closest(solver->last_solve_satzilla_feature);

    const Clustering* short_clust = get_short_cluster(solver->conf.pred_conf);
    if (short_clust == NULL) {
        cout << "ERROR: You gave a config number in '--pred' that does not exist for SHORT" << endl;
        exit(-1);
    }
    int short_cluster = short_clust->which_is_closest(solver->last_solve_satzilla_feature);

    const keep_func_type short_pred = get_short_pred_funcs(solver->conf.pred_conf)[short_cluster];
    const keep_func_type long_pred = get_long_pred_funcs(solver->conf.pred_conf)[long_cluster];

    size_t j = 0;
    for(size_t i = 0
        ; i < solver->longRedCls[1].size()
        ; i++
    ) {
        const ClOffset offset = solver->longRedCls[1][i];
        Clause* cl = solver->cl_alloc.ptr(offset);

        #ifndef FINAL_PREDICTOR_TOTAL
        if (cl->stats.which_red_array == 0) {
            solver->longRedCls[0].push_back(offset);
            moved_w0++;
        } else
        #endif
        {
            const uint32_t act_ranking_top_10 = std::ceil((double)i/((double)solver->longRedCls[1].size()/10.0));

            uint32_t last_touched_diff;
            if (cl->stats.last_touched == 0) {
                last_touched_diff = solver->sumConflicts-cl->stats.introduced_at_conflict;
            } else {
                last_touched_diff = solver->sumConflicts-cl->stats.last_touched;
            }
            //cout << "Ranking top 10: " << act_ranking_top_10 << " act: " << cl->stats.activity << endl;
            if (!solver->clause_locked(*cl, offset)
                && cl->stats.dump_number > 0
                && !cl->stats.locked_long
                && !short_pred(
                    cl
                    , solver->sumConflicts
                    , last_touched_diff
                    , i
                    , act_ranking_top_10
                )
            ) {
                deleted++;
                solver->watches.smudge((*cl)[0]);
                solver->watches.smudge((*cl)[1]);
                solver->litStats.redLits -= cl->size();

                *solver->drat << del << *cl << fin;
                cl->setRemoved();
                delayed_clause_free.push_back(offset);
            } else {
                if (cl->stats.locked_long) {
                    kept_due_to_lock++;
                    cl->stats.locked_long--;
                } else {
                    if (cl->stats.dump_number > 0 && long_pred(
                        cl
                        , solver->sumConflicts
                        , last_touched_diff
                        , i
                        , act_ranking_top_10
                    )) {
                        marked_long_keep++;
                        cl->stats.locked_long = 10;
                    }


                    if (cl->stats.dump_number == 0) {
                        kept_first++;
                    } else if (solver->clause_locked(*cl, offset)){
                        kept_locked++;
                    } else {
                        kept++;
                    }
                }
                solver->longRedCls[1][j++] = offset;
                cl->stats.rdb1_used_for_uip_creation = cl->stats.used_for_uip_creation;
                if (cl->stats.dump_number > largest_dump_no)
                    largest_dump_no = cl->stats.dump_number;
                cl->stats.dump_number++;
                cl->stats.reset_rdb_stats();
            }
        }
    }
    solver->longRedCls[1].resize(j);

    //Cleanup
    solver->clean_occur_from_removed_clauses_only_smudged();
    for(ClOffset offset: delayed_clause_free) {
        solver->cl_alloc.clauseFree(offset);
    }
    delayed_clause_free.clear();

    //Stats
    if (solver->conf.verbosity >= 0) {
        cout << "c [DBCL pred]"
        << " del: " << deleted
        << " kept: " << kept
        << " kept-long: " << kept_due_to_lock
        << " kept-0: " << kept_first
        << endl;

        cout << "c [DBCL pred]"
        << " Sclust: " << short_cluster
        << " Lclust: " << long_cluster
        << " conf: " << solver->conf.pred_conf
        << " locked: " << kept_locked
        << " marked-long: " << marked_long_keep
        << " maxdump_no:" << largest_dump_no
        << solver->conf.print_times(cpuTime()-myTime)
        << endl;
    }
    assert(moved_w0 == 0);

    if (solver->sqlStats) {
        solver->sqlStats->time_passed_min(
            solver
            , "dbclean-lev1"
            , cpuTime()-myTime
        );
    }
    total_time += cpuTime()-myTime;

    delete long_clust;
    delete short_clust;
}
#endif

void ReduceDB::mark_top_N_clauses(const uint64_t keep_num)
{
    size_t marked = 0;
    for(size_t i = 0
        ; i < solver->longRedCls[2].size() && marked < keep_num
        ; i++
    ) {
        const ClOffset offset = solver->longRedCls[2][i];
        Clause* cl = solver->cl_alloc.ptr(offset);

        if (cl->used_in_xor()
            || cl->stats.ttl > 0
            || solver->clause_locked(*cl, offset)
            || cl->stats.which_red_array != 2
        ) {
            //no need to mark, skip
            continue;
        }

        if (!cl->stats.marked_clause) {
            marked++;
            cl->stats.marked_clause = true;
        }
    }
}

bool ReduceDB::cl_needs_removal(const Clause* cl, const ClOffset offset) const
{
    assert(cl->red());
    return !cl->used_in_xor()
         && !cl->stats.marked_clause
         && cl->stats.ttl == 0
         && !solver->clause_locked(*cl, offset);
}

void ReduceDB::remove_cl_from_lev2() {
    size_t i, j;
    for (i = j = 0
        ; i < solver->longRedCls[2].size()
        ; i++
    ) {
        ClOffset offset = solver->longRedCls[2][i];
        Clause* cl = solver->cl_alloc.ptr(offset);
        assert(cl->size() > 2);

        //move to another array
        if (cl->stats.which_red_array < 2) {
            cl->stats.marked_clause = 0;
            solver->longRedCls[cl->stats.which_red_array].push_back(offset);
            continue;
        }
        assert(cl->stats.which_red_array == 2);

        //Check if locked, or marked or ttl-ed
        if (cl->stats.marked_clause) {
            cl_marked++;
        } else if (cl->stats.ttl != 0) {
            cl_ttl++;
        } else if (solver->clause_locked(*cl, offset)) {
            cl_locked_solver++;
        }

        if (!cl_needs_removal(cl, offset)) {
            if (cl->stats.ttl > 0) {
                cl->stats.ttl--;
            }
            solver->longRedCls[2][j++] = offset;
            cl->stats.marked_clause = 0;
            continue;
        }

        //Stats Update
        solver->watches.smudge((*cl)[0]);
        solver->watches.smudge((*cl)[1]);
        solver->litStats.redLits -= cl->size();

        *solver->drat << del << *cl << fin;
        cl->setRemoved();
        delayed_clause_free.push_back(offset);
    }
    solver->longRedCls[2].resize(j);
}
