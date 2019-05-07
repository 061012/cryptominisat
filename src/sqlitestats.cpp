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

#include "sqlitestats.h"
#include "solvertypes.h"
#include "solver.h"
#include "time_mem.h"
#include <sstream>
#include "varreplacer.h"
#include "occsimplifier.h"
#include <string>
#include <cmath>
#include <time.h>
#include "constants.h"
#include "reducedb.h"
#include "sql_tablestructure.h"
#include "varreplacer.h"

using namespace CMSat;
using std::cout;
using std::cerr;
using std::endl;
using std::string;

SQLiteStats::~SQLiteStats()
{
    if (!setup_ok)
        return;

    //Free all the prepared statements
    int ret = sqlite3_finalize(stmtRst);
    if (ret != SQLITE_OK) {
        cout << "Error closing prepared statement" << endl;
        std::exit(-1);
    }

    ret = sqlite3_finalize(stmtFeat);
    if (ret != SQLITE_OK) {
        cout << "Error closing prepared statement" << endl;
        std::exit(-1);
    }
    ret = sqlite3_finalize(stmtReduceDB);
    if (ret != SQLITE_OK) {
        cout << "Error closing prepared statement" << endl;
        std::exit(-1);
    }

    ret = sqlite3_finalize(stmtTimePassed);
    if (ret != SQLITE_OK) {
        cout << "Error closing prepared statement" << endl;
        std::exit(-1);
    }

    ret = sqlite3_finalize(stmtMemUsed);
    if (ret != SQLITE_OK) {
        cout << "Error closing prepared statement" << endl;
        std::exit(-1);
    }

    ret = sqlite3_finalize(stmt_clause_stats);
    if (ret != SQLITE_OK) {
        cout << "Error closing prepared statement" << endl;
        std::exit(-1);
    }

    ret = sqlite3_finalize(stmt_var_data);
    if (ret != SQLITE_OK) {
        cout << "Error closing prepared statement" << endl;
        std::exit(-1);
    }

    //Close clonnection
    sqlite3_close(db);
}

bool SQLiteStats::setup(const Solver* solver)
{
    setup_ok = connectServer(solver->conf.verbosity);
    if (!setup_ok) {
        return false;
    }

    //TODO check if data is in any table
    if (sqlite3_exec(db, cmsat_tablestructure_sql, NULL, NULL, NULL)) {
        cerr << "ERROR: Couln't create table structure for SQLite: "
        << sqlite3_errmsg(db)
        << endl;
        std::exit(-1);
    }

    add_solverrun(solver);
    addStartupData();
    initRestartSTMT();
    initReduceDBSTMT();
    initTimePassedSTMT();
    initMemUsedSTMT();
    init_satzilla_features();
    init_clause_stats_STMT();
    init_var_data_STMT();

    return true;
}


bool file_exists (const std::string& name) {
    std::ifstream f(name.c_str());
    return f.good();
}


bool SQLiteStats::connectServer(const int verbosity)
{
    if (file_exists(filename)) {
        cout << "ERROR -- the database already exists: " << filename << endl;
        cout << "ERROR -- We cannot store more than one run in the same database"
        << endl
        << "Exiting." << endl;
        exit(-1);
    }

    int rc = sqlite3_open(filename.c_str(), &db);
    if(rc) {
        cout << "c Cannot open sqlite database: " << sqlite3_errmsg(db) << endl;
        sqlite3_close(db);
        return false;
    }

    if (sqlite3_exec(db, "PRAGMA synchronous = OFF", NULL, NULL, NULL)) {
        cerr << "ERROR: Problem setting pragma synchronous = OFF to SQLite DB" << endl;
        cerr << "c " << sqlite3_errmsg(db) << endl;
        std::exit(-1);
    }

    if (sqlite3_exec(db, "PRAGMA journal_mode = MEMORY", NULL, NULL, NULL)) {
        cerr << "ERROR: Problem setting pragma journal_mode = MEMORY to SQLite DB" << endl;
        cerr << "c " << sqlite3_errmsg(db) << endl;
        std::exit(-1);
    }


    if (verbosity) {
        cout << "c writing to SQLite file: " << filename << endl;
    }

    return true;
}

void SQLiteStats::begin_transaction()
{
    if (sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL)) {
        cerr << "ERROR: Beginning SQLITE transaction" << endl;
        cerr << "c " << sqlite3_errmsg(db) << endl;
        std::exit(-1);
    }
}

void SQLiteStats::end_transaction()
{
    if (sqlite3_exec(db, "END TRANSACTION", NULL, NULL, NULL)) {
        cerr << "ERROR: Beginning SQLITE transaction" << endl;
        cerr << "c " << sqlite3_errmsg(db) << endl;
        std::exit(-1);
    }
}

bool SQLiteStats::add_solverrun(const Solver* solver)
{
    std::stringstream ss;
    ss
    << "INSERT INTO solverRun (`runtime`, `gitrev`) values ("
    << time(NULL)
    << ", '" << solver->get_version_sha1() << "'"
    << ");";

    //Inserting element into solverruns to get unique ID
    const int rc = sqlite3_exec(db, ss.str().c_str(), NULL, NULL, NULL);
    if (rc) {
        if (solver->getConf().verbosity >= 6) {
            cerr << "c ERROR Couldn't insert into table 'solverruns'" << endl;
            cerr << "c " << sqlite3_errmsg(db) << endl;
        }

        return false;
    }

    return true;
}

void SQLiteStats::add_tag(const std::pair<string, string>& tag)
{
    std::stringstream ss;
    ss
    << "INSERT INTO `tags` (`tagname`, `tag`) VALUES("
    << "'" << tag.first << "'"
    << ", '" << tag.second << "'"
    << ");";

    //Inserting element into solverruns to get unique ID
    if (sqlite3_exec(db, ss.str().c_str(), NULL, NULL, NULL)) {
        cerr << "SQLite: ERROR Couldn't insert into table 'tags'" << endl;
        assert(false);
        std::exit(-1);
    }
}

void SQLiteStats::addStartupData()
{
    std::stringstream ss;
    ss
    << "INSERT INTO `startup` (`startTime`) VALUES ("
    << "datetime('now')"
    << ");";

    if (sqlite3_exec(db, ss.str().c_str(), NULL, NULL, NULL)) {
        cerr << "ERROR Couldn't insert into table 'startup' : "
        << sqlite3_errmsg(db) << endl;

        std::exit(-1);
    }
}

void SQLiteStats::finishup(const lbool status)
{
    std::stringstream ss;
    ss
    << "INSERT INTO `finishup` (`endTime`, `status`) VALUES ("
    << "datetime('now') , "
    << "'" << status << "'"
    << ");";

    if (sqlite3_exec(db, ss.str().c_str(), NULL, NULL, NULL)) {
        cerr << "ERROR Couldn't insert into table 'finishup'" << endl;
        std::exit(-1);
    }
}

void SQLiteStats::writeQuestionMarks(
    size_t num
    , std::stringstream& ss
) {
    ss << "(";
    for(size_t i = 0
        ; i < num
        ; i++
    ) {
        if (i < num-1)
            ss << "?,";
        else
            ss << "?";
    }
    ss << ")";
}


void SQLiteStats::initMemUsedSTMT()
{
    const size_t numElems = 5;

    std::stringstream ss;
    ss << "insert into `memused`"
    << "("
    //Position
    << "  `simplifications`, `conflicts`, `runtime`"

    //memory stats
    << ", `name`, `MB`"
    << ") values ";
    writeQuestionMarks(
        numElems
        , ss
    );
    ss << ";";

    //Prepare the statement
    const int rc = sqlite3_prepare(db, ss.str().c_str(), -1, &stmtMemUsed, NULL);
    if (rc) {
        cerr << "ERROR  in sqlite_stmt_prepare(), INSERT failed"
        << endl
        << sqlite3_errmsg(db)
        << " error code: " << rc
        << endl
        << "Query was: " << ss.str()
        << endl;
        std::exit(-1);
    }
}

void SQLiteStats::mem_used(
    const Solver* solver
    , const string& name
    , double given_time
    , uint64_t mem_used_mb
) {
    int bindAt = 1;
    //Position
    sqlite3_bind_int64(stmtMemUsed, bindAt++, solver->get_solve_stats().num_simplify);
    sqlite3_bind_int64(stmtMemUsed, bindAt++, solver->sumConflicts);
    sqlite3_bind_double(stmtMemUsed, bindAt++, given_time);
    //memory stats
    sqlite3_bind_text(stmtMemUsed, bindAt++, name.c_str(), -1, NULL);
    sqlite3_bind_int(stmtMemUsed, bindAt++, mem_used_mb);

    int rc = sqlite3_step(stmtMemUsed);
    if (rc != SQLITE_DONE) {
        cerr << "ERROR while executing mem_used prepared statement"
        << endl
        << "Error from sqlite: "
        << sqlite3_errmsg(db)
        << " error code: " << rc
        << endl;

        std::exit(-1);
    }

    if (sqlite3_reset(stmtMemUsed)) {
        cerr << "Error calling sqlite3_reset on stmtMemUsed" << endl;
        std::exit(-1);
    }
    /*if (sqlite3_clear_bindings(stmtMemUsed)) {
        cerr << "Error calling sqlite3_clear_bindings on stmtMemUsed" << endl;
        std::exit(-1);
    }*/
}

void SQLiteStats::initTimePassedSTMT()
{
    const size_t numElems = 7;

    std::stringstream ss;
    ss << "insert into `timepassed`"
    << "("
    //Position
    << "  `simplifications`, `conflicts`, `runtime`"

    //Clause stats
    << ", `name`, `elapsed`, `timeout`, `percenttimeremain`"
    << ") values ";
    writeQuestionMarks(
        numElems
        , ss
    );
    ss << ";";

    //Prepare the statement
    const int rc = sqlite3_prepare(db, ss.str().c_str(), -1, &stmtTimePassed, NULL);
    if (rc) {
        cerr << "ERROR  in sqlite_stmt_prepare(), INSERT failed"
        << endl
        << sqlite3_errmsg(db)
        << " error code: " << rc
        << endl
        << "Query was: " << ss.str()
        << endl;
        std::exit(-1);
    }
}

void SQLiteStats::time_passed(
    const Solver* solver
    , const string& name
    , double time_passed
    , bool time_out
    , double percent_time_remain
) {

    int bindAt = 1;
    sqlite3_bind_int64(stmtTimePassed, bindAt++, solver->get_solve_stats().num_simplify);
    sqlite3_bind_int64(stmtTimePassed, bindAt++, solver->sumConflicts);
    sqlite3_bind_double(stmtTimePassed, bindAt++, cpuTime());
    sqlite3_bind_text(stmtTimePassed, bindAt++, name.c_str(), -1, NULL);
    sqlite3_bind_double(stmtTimePassed, bindAt++, time_passed);
    sqlite3_bind_int(stmtTimePassed, bindAt++, time_out);
    sqlite3_bind_double(stmtTimePassed, bindAt++, percent_time_remain);

    int rc = sqlite3_step(stmtTimePassed);
    if (rc != SQLITE_DONE) {
        cerr << "ERROR while executing time_passed prepared statement"
        << endl
        << "Error from sqlite: "
        << sqlite3_errmsg(db)
        << " error code: " << rc
        << endl;

        std::exit(-1);
    }

    if (sqlite3_reset(stmtTimePassed)) {
        cerr << "Error calling sqlite3_reset on stmtTimePassed" << endl;
        std::exit(-1);
    }
    /*if (sqlite3_clear_bindings(stmtTimePassed)) {
        cerr << "Error calling sqlite3_clear_bindings on stmtTimePassed" << endl;
        std::exit(-1);
    }*/
}

void SQLiteStats::time_passed_min(
    const Solver* solver
    , const string& name
    , double time_passed
) {
    int bindAt = 1;
    sqlite3_bind_int64(stmtTimePassed, bindAt++, solver->get_solve_stats().num_simplify);
    sqlite3_bind_int64(stmtTimePassed, bindAt++, solver->sumConflicts);
    sqlite3_bind_double(stmtTimePassed, bindAt++, cpuTime());
    sqlite3_bind_text(stmtTimePassed, bindAt++, name.c_str(), -1, NULL);
    sqlite3_bind_double(stmtTimePassed, bindAt++, time_passed);
    sqlite3_bind_null(stmtTimePassed, bindAt++);
    sqlite3_bind_null(stmtTimePassed, bindAt++);

    int rc = sqlite3_step(stmtTimePassed);
    if (rc != SQLITE_DONE) {
        cerr << "ERROR while executing time_passed prepared statement (time_passed_min function)"
        << endl
        << "Error from sqlite: "
        << sqlite3_errmsg(db)
        << " error code: " << rc
        << endl;

        std::exit(-1);
    }

    if (sqlite3_reset(stmtTimePassed)) {
        cerr << "Error calling sqlite3_reset on stmtTimePassed" << endl;
        std::exit(-1);
    }
    if (sqlite3_clear_bindings(stmtTimePassed)) {
        cerr << "Error calling sqlite3_clear_bindings on stmtTimePassed" << endl;
        std::exit(-1);
    }
}

void SQLiteStats::init_satzilla_features() {
    const size_t numElems = 66;

    std::stringstream ss;
    ss << "insert into `satzilla_features`"
    << "("
    //Position
    << "  `simplifications`, `restarts`, `conflicts`, `latest_satzilla_feature_calc`"

    //Base data
    << ", `numVars`"
    << ", `numClauses`"
    << ", `var_cl_ratio`"

    //Clause distribution
    << ", `binary`"
    << ", `horn`"
    << ", `horn_mean`"
    << ", `horn_std`"
    << ", `horn_min`"
    << ", `horn_max`"
    << ", `horn_spread`"
//14
    << ", `vcg_var_mean`"
    << ", `vcg_var_std`"
    << ", `vcg_var_min`"
    << ", `vcg_var_max`"
    << ", `vcg_var_spread`"

    << ", `vcg_cls_mean`"
    << ", `vcg_cls_std`"
    << ", `vcg_cls_min`"
    << ", `vcg_cls_max`"
    << ", `vcg_cls_spread`"

    << ", `pnr_var_mean`"
    << ", `pnr_var_std`"
    << ", `pnr_var_min`"
    << ", `pnr_var_max`"
    << ", `pnr_var_spread`"

    << ", `pnr_cls_mean`"
    << ", `pnr_cls_std`"
    << ", `pnr_cls_min`"
    << ", `pnr_cls_max`"
    << ", `pnr_cls_spread`"
//34
    //Conflict clauses
    << ", `avg_confl_size`"
    << ", `confl_size_min`"
    << ", `confl_size_max`"
    << ", `avg_confl_glue`"
    << ", `confl_glue_min`"
    << ", `confl_glue_max`"
    << ", `avg_num_resolutions`"
    << ", `num_resolutions_min`"
    << ", `num_resolutions_max`"
    << ", `learnt_bins_per_confl`"
//44
    //Search
    << ", `avg_branch_depth`"
    << ", `branch_depth_min`"
    << ", `branch_depth_max`"
    << ", `avg_trail_depth_delta`"
    << ", `trail_depth_delta_min`"
    << ", `trail_depth_delta_max`"
    << ", `avg_branch_depth_delta`"
    << ", `props_per_confl`"
    << ", `confl_per_restart`"
    << ", `decisions_per_conflict`"
//54
    //clause distributions
    << ", `red_glue_distr_mean`"
    << ", `red_glue_distr_var`"
    << ", `red_size_distr_mean`"
    << ", `red_size_distr_var`"
    << ", `red_activity_distr_mean`"
    << ", `red_activity_distr_var`"
//60
    << ", `irred_glue_distr_mean`"
    << ", `irred_glue_distr_var`"
    << ", `irred_size_distr_mean`"
    << ", `irred_size_distr_var`"
    << ", `irred_activity_distr_mean`"
    << ", `irred_activity_distr_var`"
//66
    << ") values ";
    writeQuestionMarks(
        numElems
        , ss
    );
    ss << ";";

    //Prepare the statement
    if (sqlite3_prepare(db, ss.str().c_str(), -1, &stmtFeat, NULL)) {
        cerr << "ERROR in sqlite_stmt_prepare(), INSERT failed"
        << endl
        << sqlite3_errmsg(db)
        << endl
        << "Query was: " << ss.str()
        << endl;
        std::exit(-1);
    }
}

//Prepare statement for restart
void SQLiteStats::initRestartSTMT()
{
    const size_t numElems = 66;

    std::stringstream ss;
    ss << "insert into `restart`"
    << "("
    //Position
    << "  `simplifications`, `restarts`, `conflicts`, `latest_satzilla_feature_calc`"
    << ", `runtime` "

    //Clause stats
    << ", numIrredBins, numIrredLongs"
    << ", numRedBins, numRedLongs"
    << ", numIrredLits, numRedLits"

    //Conflict stats
    << ", `restart_type`"
    << ", `glue`, `glueSD`, `glueMin`, `glueMax`"
    << ", `size`, `sizeSD`, `sizeMin`, `sizeMax`"
    << ", `resolutions`, `resolutionsSD`, `resolutionsMin`, `resolutionsMax`"

    //Search stats
    << ", `branchDepth`, `branchDepthSD`, `branchDepthMin`, `branchDepthMax`"
    << ", `branchDepthDelta`, `branchDepthDeltaSD`, `branchDepthDeltaMin`, `branchDepthDeltaMax`"
    << ", `trailDepth`, `trailDepthSD`, `trailDepthMin`, `trailDepthMax`"
    << ", `trailDepthDelta`, `trailDepthDeltaSD`, `trailDepthDeltaMin`,`trailDepthDeltaMax`"

    //Propagations
    << ", `propBinIrred` , `propBinRed` "
    << ", `propLongIrred` , `propLongRed`"

    //Conflicts
    << ", `conflBinIrred`, `conflBinRed`"
    << ", `conflLongIrred`, `conflLongRed`"

    //Reds
    << ", `learntUnits`, `learntBins`, `learntLongs`"

    //Resolutions
    << ", `resolBinIrred`, `resolBinRed`, `resolLIrred`, `resolLRed`"

    //Var stats
    << ", `propagations`"
    << ", `decisions`"
    << ", `flipped`, `varSetPos`, `varSetNeg`"
    << ", `free`, `replaced`, `eliminated`, `set`"
    << ", `clauseIDstartInclusive`, `clauseIDendExclusive`"
    << ") values ";
    writeQuestionMarks(
        numElems
        , ss
    );
    ss << ";";

    //Prepare the statement
    if (sqlite3_prepare(db, ss.str().c_str(), -1, &stmtRst, NULL)) {
        cerr << "ERROR in sqlite_stmt_prepare(), INSERT failed"
        << endl
        << sqlite3_errmsg(db)
        << endl
        << "Query was: " << ss.str()
        << endl;
        std::exit(-1);
    }
}

void SQLiteStats::satzilla_features(
    const Solver* solver
    , const Searcher* search
    , const SatZillaFeatures& satzilla_feat
) {
    int bindAt = 1;
    sqlite3_bind_int64(stmtFeat, bindAt++, solver->get_solve_stats().num_simplify);
    sqlite3_bind_int64(stmtFeat, bindAt++, search->sumRestarts());
    sqlite3_bind_int64(stmtFeat, bindAt++, solver->sumConflicts);
    sqlite3_bind_int(stmtFeat, bindAt++, solver->latest_satzilla_feature_calc);

    sqlite3_bind_int64(stmtFeat, bindAt++, (uint64_t)satzilla_feat.numVars);
    sqlite3_bind_int64(stmtFeat, bindAt++, (uint64_t)satzilla_feat.numClauses);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.var_cl_ratio);

    //Clause distribution
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.binary);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.horn);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.horn_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.horn_std);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.horn_min);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.horn_max);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.horn_spread);

    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.vcg_var_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.vcg_var_std);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.vcg_var_min);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.vcg_var_max);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.vcg_var_spread);

    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.vcg_cls_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.vcg_cls_std);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.vcg_cls_min);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.vcg_cls_max);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.vcg_cls_spread);

    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.pnr_var_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.pnr_var_std);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.pnr_var_min);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.pnr_var_max);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.pnr_var_spread);

    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.pnr_cls_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.pnr_cls_std);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.pnr_cls_min);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.pnr_cls_max);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.pnr_cls_spread);

    //Conflict clauses
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.avg_confl_size);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.confl_size_min);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.confl_size_max);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.avg_confl_glue);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.confl_glue_min);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.confl_glue_max);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.avg_num_resolutions);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.num_resolutions_min);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.num_resolutions_max);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.learnt_bins_per_confl);

    //Search
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.avg_branch_depth);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.branch_depth_min);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.branch_depth_max);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.avg_trail_depth_delta);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.trail_depth_delta_min);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.trail_depth_delta_max);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.avg_branch_depth_delta);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.props_per_confl);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.confl_per_restart);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.decisions_per_conflict);

    //red stats
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.red_cl_distrib.glue_distr_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.red_cl_distrib.glue_distr_var);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.red_cl_distrib.size_distr_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.red_cl_distrib.size_distr_var);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.red_cl_distrib.activity_distr_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.red_cl_distrib.activity_distr_var);

    //irred stats
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.irred_cl_distrib.glue_distr_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.irred_cl_distrib.glue_distr_var);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.irred_cl_distrib.size_distr_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.irred_cl_distrib.size_distr_var);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.irred_cl_distrib.activity_distr_mean);
    sqlite3_bind_double(stmtFeat, bindAt++, satzilla_feat.irred_cl_distrib.activity_distr_var);

    int rc = sqlite3_step(stmtFeat);
    if (rc != SQLITE_DONE) {
        cerr << "ERROR  while executing restart insertion SQLite prepared statement"
        << endl
        << "Error from sqlite: "
        << sqlite3_errmsg(db)
        << endl;

        std::exit(-1);
    }

    if (sqlite3_reset(stmtFeat)) {
        cerr << "Error calling sqlite3_reset on stmtFeat" << endl;
        std::exit(-1);
    }
    if (sqlite3_clear_bindings(stmtFeat)) {
        cerr << "Error calling sqlite3_clear_bindings on stmtFeat" << endl;
        std::exit(-1);
    }
}

void SQLiteStats::restart(
    const std::string& restart_type
    , const PropStats& thisPropStats
    , const SearchStats& thisStats
    , const Solver* solver
    , const Searcher* search
) {
    const SearchHist& searchHist = search->getHistory();
    const BinTriStats& binTri = solver->getBinTriStats();

    int bindAt = 1;
    sqlite3_bind_int64(stmtRst, bindAt++, solver->get_solve_stats().num_simplify);
    sqlite3_bind_int64(stmtRst, bindAt++, search->sumRestarts());
    sqlite3_bind_int64(stmtRst, bindAt++, solver->sumConflicts);
    sqlite3_bind_int(stmtRst, bindAt++, solver->latest_satzilla_feature_calc);
    sqlite3_bind_double(stmtRst, bindAt++, cpuTime());


    sqlite3_bind_int64(stmtRst, bindAt++, binTri.irredBins);
    sqlite3_bind_int64(stmtRst, bindAt++, solver->get_num_long_irred_cls());

    sqlite3_bind_int64(stmtRst, bindAt++, binTri.redBins);
    sqlite3_bind_int64(stmtRst, bindAt++, solver->get_num_long_red_cls());

    sqlite3_bind_int64(stmtRst, bindAt++, solver->litStats.irredLits);
    sqlite3_bind_int64(stmtRst, bindAt++, solver->litStats.redLits);

    //Conflict stats
    sqlite3_bind_text(stmtRst, bindAt++, restart_type.c_str(), -1, NULL);
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.glueHist.getLongtTerm().avg());
    sqlite3_bind_double(stmtRst, bindAt++, std:: sqrt(searchHist.glueHist.getLongtTerm().var()));
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.glueHist.getLongtTerm().getMin());
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.glueHist.getLongtTerm().getMax());

    sqlite3_bind_double(stmtRst, bindAt++, searchHist.conflSizeHist.avg());
    sqlite3_bind_double(stmtRst, bindAt++, std:: sqrt(searchHist.conflSizeHist.var()));
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.conflSizeHist.getMin());
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.conflSizeHist.getMax());

    sqlite3_bind_double(stmtRst, bindAt++, searchHist.numResolutionsHist.avg());
    sqlite3_bind_double(stmtRst, bindAt++, std:: sqrt(searchHist.numResolutionsHist.var()));
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.numResolutionsHist.getMin());
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.numResolutionsHist.getMax());

    //Search stats
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.branchDepthHist.avg());
    sqlite3_bind_double(stmtRst, bindAt++, std:: sqrt(searchHist.branchDepthHist.var()));
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.branchDepthHist.getMin());
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.branchDepthHist.getMax());

    sqlite3_bind_double(stmtRst, bindAt++, searchHist.branchDepthDeltaHist.avg());
    sqlite3_bind_double(stmtRst, bindAt++, std:: sqrt(searchHist.branchDepthDeltaHist.var()));
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.branchDepthDeltaHist.getMin());
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.branchDepthDeltaHist.getMax());

    sqlite3_bind_double(stmtRst, bindAt++, searchHist.trailDepthHist.getLongtTerm().avg());
    sqlite3_bind_double(stmtRst, bindAt++, std:: sqrt(searchHist.trailDepthHist.getLongtTerm().var()));
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.trailDepthHist.getLongtTerm().getMin());
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.trailDepthHist.getLongtTerm().getMax());

    sqlite3_bind_double(stmtRst, bindAt++, searchHist.trailDepthDeltaHist.avg());
    sqlite3_bind_double(stmtRst, bindAt++, std:: sqrt(searchHist.trailDepthDeltaHist.var()));
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.trailDepthDeltaHist.getMin());
    sqlite3_bind_double(stmtRst, bindAt++, searchHist.trailDepthDeltaHist.getMax());

    //Prop
    sqlite3_bind_int64(stmtRst, bindAt++, thisPropStats.propsBinIrred);
    sqlite3_bind_int64(stmtRst, bindAt++, thisPropStats.propsBinRed);
    sqlite3_bind_int64(stmtRst, bindAt++, thisPropStats.propsLongIrred);
    sqlite3_bind_int64(stmtRst, bindAt++, thisPropStats.propsLongRed);

    //Confl
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.conflStats.conflsBinIrred);
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.conflStats.conflsBinRed);
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.conflStats.conflsLongIrred);
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.conflStats.conflsLongRed);

    //Red
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.learntUnits);
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.learntBins);
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.learntLongs);

    //Resolv stats
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.resolvs.binIrred);
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.resolvs.binRed);
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.resolvs.longIrred);
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.resolvs.longRed);


    //Var stats
    sqlite3_bind_int64(stmtRst, bindAt++, thisPropStats.propagations);
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.decisions);

    sqlite3_bind_int64(stmtRst, bindAt++, thisPropStats.varFlipped);
    sqlite3_bind_int64(stmtRst, bindAt++, thisPropStats.varSetPos);
    sqlite3_bind_int64(stmtRst, bindAt++, thisPropStats.varSetNeg);
    sqlite3_bind_int64(stmtRst, bindAt++, solver->get_num_free_vars());
    sqlite3_bind_int64(stmtRst, bindAt++, solver->varReplacer->get_num_replaced_vars());
    sqlite3_bind_int64(stmtRst, bindAt++, solver->get_num_vars_elimed());
    sqlite3_bind_int64(stmtRst, bindAt++, search->getTrailSize());

    //ClauseID
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.clauseID_at_start_inclusive);
    sqlite3_bind_int64(stmtRst, bindAt++, thisStats.clauseID_at_end_exclusive);

    int rc = sqlite3_step(stmtRst);
    if (rc != SQLITE_DONE) {
        cerr << "ERROR  while executing restart insertion SQLite prepared statement"
        << endl
        << "Error from sqlite: "
        << sqlite3_errmsg(db)
        << endl;

        std::exit(-1);
    }

    if (sqlite3_reset(stmtRst)) {
        cerr << "Error calling sqlite3_reset on stmtRst" << endl;
        std::exit(-1);
    }
    if (sqlite3_clear_bindings(stmtRst)) {
        cerr << "Error calling sqlite3_clear_bindings on stmtRst" << endl;
        std::exit(-1);
    }
}


//Prepare statement for restart
void SQLiteStats::initReduceDBSTMT()
{
    const size_t numElems = 24;

    std::stringstream ss;
    ss << "insert into `reduceDB`"
    << "("
    //Position
    << "  `simplifications`, `restarts`"
    << ", `conflicts`"
    << ", `latest_satzilla_feature_calc`"
    << ", `cur_restart_type`"
    << ", `runtime`"

    //data
    << ", `clauseID`"
    << ", `dump_no`"
    << ", `conflicts_made`"
    << ", `propagations_made`"
    << ", `clause_looked_at`"
    << ", `used_for_uip_creation`"
    << ", `last_touched_diff`"
    << ", `activity_rel`"
    << ", `locked`"
    << ", `in_xor`"
    << ", `glue`"
    << ", `size`"
    << ", `ttl`"
    << ", `act_ranking_top_10`"
    << ", `act_ranking`"
    << ", `sum_uip1_used`"
    << ", `sum_delta_confl_uip1_used`"
    << ") values ";
    writeQuestionMarks(
        numElems
        , ss
    );
    ss << ";";

    //Prepare the statement
    int rc = sqlite3_prepare(db, ss.str().c_str(), -1, &stmtReduceDB, NULL);
    if (rc) {
        cout
        << "Error in sqlite_prepare(), INSERT failed"
        << endl
        << sqlite3_errmsg(db)
        << endl
        << "Query was: " << ss.str()
        << endl;
        std::exit(-1);
    }
}

void SQLiteStats::reduceDB(
    const Solver* solver
    , const bool locked
    , const Clause* cl
    , const string& cur_restart_type
    , const uint32_t act_ranking_top_10
    , const uint32_t act_ranking
) {
    assert(cl->stats.dump_number != std::numeric_limits<uint32_t>::max());

    int bindAt = 1;
    sqlite3_bind_int64(stmtReduceDB, bindAt++, solver->get_solve_stats().num_simplify);
    sqlite3_bind_int64(stmtReduceDB, bindAt++, solver->sumRestarts());
    sqlite3_bind_int64(stmtReduceDB, bindAt++, solver->sumConflicts);
    sqlite3_bind_int64(stmtReduceDB, bindAt++, solver->latest_satzilla_feature_calc);
    sqlite3_bind_text(stmtReduceDB, bindAt++, cur_restart_type.c_str(), -1, NULL);
    sqlite3_bind_double(stmtReduceDB, bindAt++, cpuTime());

    //data
    sqlite3_bind_int64(stmtReduceDB, bindAt++, cl->stats.ID);
    sqlite3_bind_int64(stmtReduceDB, bindAt++, cl->stats.dump_number);
    sqlite3_bind_int64(stmtReduceDB, bindAt++, cl->stats.conflicts_made);
    sqlite3_bind_int64(stmtReduceDB, bindAt++, cl->stats.propagations_made);
    sqlite3_bind_int64(stmtReduceDB, bindAt++, cl->stats.clause_looked_at);
    sqlite3_bind_int64(stmtReduceDB, bindAt++, cl->stats.used_for_uip_creation);

    uint64_t last_touched_diff;
    if (cl->stats.last_touched == 0) {
        last_touched_diff = solver->sumConflicts-cl->stats.introduced_at_conflict;
    } else {
        last_touched_diff = solver->sumConflicts-cl->stats.last_touched;
    }
    sqlite3_bind_int64(stmtReduceDB, bindAt++, last_touched_diff);

    sqlite3_bind_double(stmtReduceDB, bindAt++, (double)cl->stats.activity/(double)solver->get_cla_inc());
    sqlite3_bind_int(stmtReduceDB, bindAt++, locked);
    sqlite3_bind_int(stmtReduceDB, bindAt++, cl->used_in_xor());
    sqlite3_bind_int(stmtReduceDB, bindAt++, cl->stats.glue);
    sqlite3_bind_int(stmtReduceDB, bindAt++, cl->size());
    sqlite3_bind_int(stmtReduceDB, bindAt++, cl->stats.ttl);
    sqlite3_bind_int(stmtReduceDB, bindAt++, act_ranking_top_10);
    sqlite3_bind_int(stmtReduceDB, bindAt++, act_ranking);
    sqlite3_bind_int(stmtReduceDB, bindAt++, cl->stats.sum_uip1_used);
    sqlite3_bind_int(stmtReduceDB, bindAt++, cl->stats.sum_delta_confl_uip1_used);

    int rc = sqlite3_step(stmtReduceDB);
    if (rc != SQLITE_DONE) {
        cout
        << "ERROR: while executing clause DB cleaning SQLite prepared statement"
        << endl;

        cout << "Error from sqlite: "
        << sqlite3_errmsg(db)
        << endl;
        std::exit(-1);
    }

    if (sqlite3_reset(stmtReduceDB)) {
        cerr << "Error calling sqlite3_reset on stmtReduceDB" << endl;
        std::exit(-1);
    }

    if (sqlite3_clear_bindings(stmtReduceDB)) {
        cerr << "Error calling sqlite3_clear_bindings on stmtReduceDB" << endl;
        std::exit(-1);
    }
}

void SQLiteStats::init_clause_stats_STMT()
{
    const size_t numElems = 66;

    std::stringstream ss;
    ss << "insert into `clauseStats`"
    << "("
    << " `simplifications`,"
    << " `restarts`,"
    << " `prev_restart`,"
    << " `conflicts`,"
    << " `latest_satzilla_feature_calc`,"
    << " `clauseID`,"
    << ""
    << " `glue`,"
    << " `size`,"
    << " `conflicts_this_restart`,"
    << " `num_overlap_literals`,"
    << " `num_antecedents`,"
    << " `num_total_lits_antecedents`,"
    << " `antecedents_avg_size`,"

    << " `last_dec_var_act_vsids_0`,"
    << " `last_dec_var_act_vsids_1`,"
    << " `first_dec_var_act_vsids_0`,"
    << " `first_dec_var_act_vsids_1`,"

    << " `backtrack_level`,"
    << " `decision_level`,"
    << " `decision_level_pre1`,"
    << " `decision_level_pre2`,"
    << " `trail_depth_level`,"
    << " `cur_restart_type` ,"

    << " `atedecents_binIrred`,"
    << " `atedecents_binRed`,"
    << " `atedecents_longIrred`,"
    << " `atedecents_longRed`,"

    << " `vsids_vars_avg`,"
    << " `vsids_vars_var`,"
    << " `vsids_vars_min`,"
    << " `vsids_vars_max`,"

    << " `antecedents_glue_long_reds_avg`,"
    << " `antecedents_glue_long_reds_var`,"
    << " `antecedents_glue_long_reds_min`,"
    << " `antecedents_glue_long_reds_max`,"

    << " `antecedents_long_red_age_avg`,"
    << " `antecedents_long_red_age_var`,"
    << " `antecedents_long_red_age_min`,"
    << " `antecedents_long_red_age_max`,"

    << " `vsids_of_resolving_literals_avg`,"
    << " `vsids_of_resolving_literals_var`,"
    << " `vsids_of_resolving_literals_min`,"
    << " `vsids_of_resolving_literals_max`,"

    << " `vsids_of_all_incoming_lits_avg`,"
    << " `vsids_of_all_incoming_lits_var`,"
    << " `vsids_of_all_incoming_lits_min`,"
    << " `vsids_of_all_incoming_lits_max`,"

    << " `antecedents_antecedents_vsids_avg`,"

    << " `decision_level_hist`,"
    << " `backtrack_level_hist_lt`,"
    << " `trail_depth_level_hist`,"
    << " `vsids_vars_hist`,"
    << " `size_hist`,"
    << " `glue_hist`,"
    << " `num_antecedents_hist`,"
    << " `antec_sum_size_hist`,"
    << " `antec_overlap_hist`,"
    << " `branch_depth_hist_queue`,"
    << " `trail_depth_hist`,"
    << " `trail_depth_hist_longer`,"
    << " `num_resolutions_hist`,"
    << " `confl_size_hist`,"
    << " `trail_depth_delta_hist`,"
    << " `backtrack_level_hist`,"
    << " `glue_hist_queue`,"
    << " `glue_hist_long`"
    << ") values ";
    writeQuestionMarks(
        numElems
        , ss
    );
    ss << ";";

    //Prepare the statement
    int rc = sqlite3_prepare(db, ss.str().c_str(), -1, &stmt_clause_stats, NULL);
    if (rc) {
        cout
        << "Error in sqlite_prepare(), INSERT failed"
        << endl
        << sqlite3_errmsg(db)
        << endl
        << "Query was: " << ss.str()
        << endl;
        std::exit(-1);
    }
}

void SQLiteStats::dump_clause_stats(
    const Solver* solver
    , uint64_t clid
    , uint32_t glue
    , uint32_t backtrack_level
    , uint32_t size
    , AtecedentData<uint16_t> antec_data
    , size_t decision_level
    , size_t trail_depth
    , uint64_t conflicts_this_restart
    , const std::string& restart_type
    , const SearchHist& hist
    , const double last_dec_var_act_vsids_0
    , const double last_dec_var_act_vsids_1
    , const double first_dec_var_act_vsids_0
    , const double first_dec_var_act_vsids_1
) {
    uint32_t num_overlap_literals = antec_data.sum_size()-(antec_data.num()-1)-size;

    int bindAt = 1;
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, solver->get_solve_stats().num_simplify);
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, solver->sumRestarts());
    if (solver->sumRestarts() == 0) {
        sqlite3_bind_int64(stmt_clause_stats, bindAt++, 0);
    } else {
        sqlite3_bind_int64(stmt_clause_stats, bindAt++, solver->sumRestarts()-1);
    }
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, solver->sumConflicts);
    sqlite3_bind_int(stmt_clause_stats, bindAt++, solver->latest_satzilla_feature_calc);
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, clid);

    sqlite3_bind_int(stmt_clause_stats, bindAt++, glue);
    sqlite3_bind_int(stmt_clause_stats, bindAt++, size);
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, conflicts_this_restart);
    sqlite3_bind_int(stmt_clause_stats, bindAt++, num_overlap_literals);
    sqlite3_bind_int(stmt_clause_stats, bindAt++, antec_data.num());
    sqlite3_bind_int(stmt_clause_stats, bindAt++, antec_data.sum_size());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, (double)antec_data.sum_size()/(double)antec_data.num() );
    sqlite3_bind_double(stmt_clause_stats, bindAt++, last_dec_var_act_vsids_0);
    sqlite3_bind_double(stmt_clause_stats, bindAt++, last_dec_var_act_vsids_1);
    sqlite3_bind_double(stmt_clause_stats, bindAt++, first_dec_var_act_vsids_0);
    sqlite3_bind_double(stmt_clause_stats, bindAt++, first_dec_var_act_vsids_1);

    sqlite3_bind_int(stmt_clause_stats, bindAt++, backtrack_level);
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, decision_level);
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, hist.branchDepthHistQueue.prev(1));
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, hist.branchDepthHistQueue.prev(2));
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, trail_depth);
    sqlite3_bind_text(stmt_clause_stats, bindAt++,  restart_type.c_str(), -1, NULL);

    sqlite3_bind_int(stmt_clause_stats, bindAt++, antec_data.binIrred);
    sqlite3_bind_int(stmt_clause_stats, bindAt++, antec_data.binRed);
    sqlite3_bind_int(stmt_clause_stats, bindAt++, antec_data.longIrred);
    sqlite3_bind_int(stmt_clause_stats, bindAt++, antec_data.longRed);

    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_vars.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_vars.var());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_vars.getMin());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_vars.getMax());

    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.glue_long_reds.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.glue_long_reds.var());
    sqlite3_bind_int(stmt_clause_stats, bindAt++, antec_data.glue_long_reds.getMin());
    sqlite3_bind_int(stmt_clause_stats, bindAt++, antec_data.glue_long_reds.getMax());

    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.age_long_reds.avg() );
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.age_long_reds.var() );
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, antec_data.age_long_reds.getMin() );
    sqlite3_bind_int64(stmt_clause_stats, bindAt++, antec_data.age_long_reds.getMax() );

    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_of_resolving_literals.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_of_resolving_literals.var());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_of_resolving_literals.getMin());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_of_resolving_literals.getMax());

    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_all_incoming_vars.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_all_incoming_vars.var());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_all_incoming_vars.getMin());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_all_incoming_vars.getMax());

    sqlite3_bind_double(stmt_clause_stats, bindAt++, antec_data.vsids_of_ants.avg());

    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.decisionLevelHistLT.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.backtrackLevelHistLT.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.trailDepthHistLT.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.vsidsVarsAvgLT.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.conflSizeHistLT.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.glueHistLT.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.numResolutionsHistLT.avg());

    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.antec_data_sum_sizeHistLT.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.overlapHistLT.avg());

    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.branchDepthHistQueue.avg_nocheck());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.trailDepthHist.avg_nocheck());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.trailDepthHistLonger.avg_nocheck());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.numResolutionsHist.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.conflSizeHist.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.trailDepthDeltaHist.avg());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.backtrackLevelHist.avg_nocheck());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.glueHist.avg_nocheck());
    sqlite3_bind_double(stmt_clause_stats, bindAt++, hist.glueHist.getLongtTerm().avg());

    int rc = sqlite3_step(stmt_clause_stats);
    if (rc != SQLITE_DONE) {
        cout
        << "ERROR: while executing clause DB cleaning SQLite prepared statement"
        << endl;

        cout << "Error from sqlite: "
        << sqlite3_errmsg(db)
        << endl;
        std::exit(-1);
    }

    if (sqlite3_reset(stmt_clause_stats)) {
        cerr << "Error calling sqlite3_reset on stmt_clause_stats" << endl;
        std::exit(-1);
    }

    if (sqlite3_clear_bindings(stmt_clause_stats)) {
        cerr << "Error calling sqlite3_clear_bindings on stmt_clause_stats" << endl;
        std::exit(-1);
    }
}

void SQLiteStats::init_var_data_STMT()
{
    const size_t numElems = 19;

    std::stringstream ss;
    ss << "insert into `varData`"
    << "("
    //Position
    << "  `restarts`, `conflicts`"

    //data
    ", `var`"
    ", `dec_depth`"
    ", `decisions_below`"
    ", `conflicts_below`"
    ", `clauses_below`"

    ", `decided`"
    ", `decided_pos`"
    ", `propagated`"
    ", `propagated_pos`"
    ", `sum_decisions_at_picktime`"
    ", `sum_propagations_at_picktime`"

    ", `total_conflicts_below_when_picked`"
    ", `total_decisions_below_when_picked`"
    ", `avg_inside_per_confl_when_picked`"
    ", `avg_inside_antecedents_when_picked`"

    ", `clid_start_incl`"
    ", `clid_end_notincl`"
    << ") values ";
    writeQuestionMarks(
        numElems
        , ss
    );
    ss << ";";

    //Prepare the statement
    int rc = sqlite3_prepare(db, ss.str().c_str(), -1, &stmt_var_data, NULL);
    if (rc) {
        cout
        << "Error in sqlite_prepare(), INSERT failed"
        << endl
        << sqlite3_errmsg(db)
        << endl
        << "Query was: " << ss.str()
        << endl;
        std::exit(-1);
    }
}

void SQLiteStats::var_data(
    const Solver* solver
    , const uint32_t var
    , const VarData& vardata
    , const uint32_t decisions_below
    , const uint32_t conflicts_below
    , const uint32_t cls_below
    , const uint64_t end_clid_notincl
) {
    int bindAt = 1;
    sqlite3_bind_int64(stmt_var_data, bindAt++, solver->sumRestarts());
    sqlite3_bind_int64(stmt_var_data, bindAt++, solver->sumConflicts);

    sqlite3_bind_int   (stmt_var_data, bindAt++, var);
    sqlite3_bind_int64 (stmt_var_data, bindAt++, vardata.level);
    sqlite3_bind_int64 (stmt_var_data, bindAt++, decisions_below);
    sqlite3_bind_int64 (stmt_var_data, bindAt++, conflicts_below);
    sqlite3_bind_int64 (stmt_var_data, bindAt++, cls_below);

    /////
    /////data at picked time data
    /////

    //these are not updated while working below.
    sqlite3_bind_int64 (stmt_var_data, bindAt++, vardata.num_decided);
    sqlite3_bind_int64 (stmt_var_data, bindAt++, vardata.num_decided_pos);
    sqlite3_bind_int64 (stmt_var_data, bindAt++, vardata.num_propagated);
    sqlite3_bind_int64 (stmt_var_data, bindAt++, vardata.num_propagated_pos);

    //to average above data
    sqlite3_bind_int64 (stmt_var_data, bindAt++, vardata.sum_decisions_at_picktime);
    sqlite3_bind_int64 (stmt_var_data, bindAt++, vardata.sum_propagations_at_picktime);

    //data about stuff that's below
    sqlite3_bind_int64 (stmt_var_data, bindAt++, vardata.total_conflicts_below_when_picked);
    sqlite3_bind_int64 (stmt_var_data, bindAt++, vardata.total_decisions_below_when_picked);

    //data about variable in general
    sqlite3_bind_double(stmt_var_data, bindAt++, vardata.avg_inside_per_confl_when_picked);
    sqlite3_bind_double(stmt_var_data, bindAt++, vardata.avg_inside_antecedents_when_picked);

    //to get usage data good cl/bad cl, etc.
    sqlite3_bind_int64 (stmt_var_data, bindAt++, vardata.clid_at_picking);
    sqlite3_bind_int64 (stmt_var_data, bindAt++, end_clid_notincl);

    int rc = sqlite3_step(stmt_var_data);
    if (rc != SQLITE_DONE) {
        cout
        << "ERROR: while executing clause DB cleaning SQLite prepared statement"
        << endl;

        cout << "Error from sqlite: "
        << sqlite3_errmsg(db)
        << endl;
        std::exit(-1);
    }

    if (sqlite3_reset(stmt_var_data)) {
        cerr << "Error calling sqlite3_reset on stmt_var_data" << endl;
        std::exit(-1);
    }

    if (sqlite3_clear_bindings(stmt_var_data)) {
        cerr << "Error calling sqlite3_clear_bindings on stmt_var_data" << endl;
        std::exit(-1);
    }
}
