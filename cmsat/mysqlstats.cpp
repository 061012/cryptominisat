#include "mysqlstats.h"
#include "solvertypes.h"
#include "solver.h"
#include "time_mem.h"
#include <sstream>
#include "varreplacer.h"
#include "simplifier.h"
#include <string>
#include <time.h>

using std::cout;
using std::endl;
using std::string;

MySQLStats::MySQLStats() :
    bindAt(0)
{
}

void MySQLStats::setup(const Solver* solver)
{
    connectServer(solver);
    getID(solver);
    addFiles(solver);
    addStartupData(solver);
    initRestartSTMT(solver->getConf().verbosity);
    initReduceDBSTMT(solver->getConf().verbosity);
    initVarSTMT(solver, stmtVarBulk, solver->getConf().preparedDumpSizeVarData);
    initVarSTMT(solver, stmtVarSingle, 1);
    initClauseDistribSTMT(
        solver
        , stmtClsDistribSize
        , "clauseSizeDistrib"   //table name
        , "size"                //property name
        , solver->getConf().dumpClauseDistribMaxSize //num inserts
    );

    initClauseDistribSTMT(
        solver
        , stmtClsDistribGlue
        , "clauseGlueDistrib"   //table name
        , "glue"                //property name
        , solver->getConf().dumpClauseDistribMaxGlue //num inserts
    );
    initSizeGlueScatterSTMT(
        solver
        , solver->getConf().preparedDumpSizeScatter
    );
}

void MySQLStats::connectServer(const Solver* solver)
{
    //Init MySQL library
    serverConn = mysql_init(NULL);
    if (!serverConn) {
        cout
        << "Insufficient memory to allocate server connection"
        << endl;

        exit(-1);
    }

    //Connect to server
    if (!mysql_real_connect(
        serverConn
        , solver->getConf().sqlServer.c_str()
        , solver->getConf().sqlUser.c_str()
        , solver->getConf().sqlPass.c_str()
        , solver->getConf().sqlDatabase.c_str()
        , 0
        , NULL
        , 0)
    ) {
      cout
      << "ERROR while connecting to MySQL server:"
      << mysql_error(serverConn)
      << endl;

      cout
      << "If your MySQL server is running then you did not create the database" << endl
      << "and/or didn't add the correct user. You can fix this by executing: " << endl
      << "$ mysql -u root -p" << endl
      << "mysql> create database cmsat;" << endl
      << "mysql> grant all on cryptoms.* to user 'cmsat_solver'@localhost identified by '';" << endl
      << "mysql> quit;" << endl
      << "$ mysql -u root -p cmsat < cmsat_tablestructure.sql" << endl
      ;

      exit(1);
    }
}

bool MySQLStats::tryIDInSQL(const Solver* solver)
{
    std::stringstream ss;
    ss
    << "INSERT INTO solverRun (runID, version, time) values ("
    << runID
    << ", \"" << Solver::getVersion() << "\""
    << ", " << time(NULL)
    << ");";

    //Inserting element into solverruns to get unique ID
    if (mysql_query(serverConn, ss.str().c_str())) {

        if (solver->getConf().verbosity >= 6) {
            cout << "c Couldn't insert into table 'solverruns'" << endl;
            cout << "c " << mysql_error(serverConn) << endl;
        }

        return false;
    }

    return true;
}

void MySQLStats::getID(const Solver* solver)
{
    size_t numTries = 0;
    getRandomID();
    while(!tryIDInSQL(solver)) {
        getRandomID();
        numTries++;

        //Check if we have been in this loop for too long
        if (numTries > 10) {
            cout
            << " Something is wrong while adding runID!" << endl
            << " Exiting!"
            << endl;

            cout
            << "Maybe you didn't create the tables in the database?" << endl
            << "You can fix this by executing: " << endl
            << "$ mysql -u root -p cmsat < cmsat_tablestructure.sql" << endl
            ;

            exit(-1);
        }
    }

    if (solver->getConf().verbosity >= 1) {
        cout << "c SQL runID is " << runID << endl;
    }
}

void MySQLStats::addFiles(const Solver* solver)
{
    for(vector<string>::const_iterator
        it = solver->getFileNamesUsed().begin(), end = solver->getFileNamesUsed().end()
        ; it != end
        ; it++
    ) {

        std::stringstream ss;
        ss
        << "INSERT INTO fileNamesUsed (runID, fileName) VALUES"
        <<"(" << runID << ", \"" << *it << "\");";

        //Inserting element into solverruns to get unique ID
        if (mysql_query(serverConn, ss.str().c_str())) {
            cout << "Couldn't insert into table 'solverruns'" << endl;
            exit(1);
        }
    }
}

void MySQLStats::addStartupData(const Solver* solver)
{
    std::stringstream ss;
    ss
    << "INSERT INTO startup (runID, startTime, verbosity) VALUES ("
    << runID << ","
    << "NOW() , "
    << solver->getConf().verbosity
    << ");";

    //Inserting element into solverruns to get unique ID
    if (mysql_query(serverConn, ss.str().c_str())) {
        cout << "Couldn't insert into table 'solverruns'" << endl;
        exit(1);
    }
}

void MySQLStats::writeQuestionMarks(
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

void MySQLStats::initClauseDistribSTMT(
    const Solver* solver
    , StmtClsDistrib& mystruct
    , const string& tableName
    , const string& valueName
    , const size_t numInserts
) {
    const size_t numElems = 4;
    mystruct.bind.resize(numElems*numInserts);

    if (solver->getConf().verbosity >= 6)
        cout
        << "c Trying to prepare statement with " << numInserts
        << " bulk inserts"
        << endl;

    std::stringstream ss;
    ss << "insert into `" << tableName << "`"
    << "("
    << " `runID`, `conflicts`, `" << valueName << "`, `num`"
    << ") values ";
    for(size_t i = 0; i < numInserts; i++) {
        writeQuestionMarks(numElems, ss);

        if (i != numInserts-1)
            ss << " , ";
    }
    ss << ";";

    //Get memory for statement
    mystruct.stmt = mysql_stmt_init(serverConn);
    if (!mystruct.stmt) {
        cout << "Error: mysql_stmt_init() out of memory" << endl;
        exit(1);
    }

    //Prepare the statement
    if (mysql_stmt_prepare(mystruct.stmt, ss.str().c_str(), ss.str().length())) {
        cout
        << "Error in mysql_stmt_prepare(), INSERT failed"
        << endl
        << mysql_stmt_error(mystruct.stmt)
        << endl
        << "Query was: " << ss.str()
        << endl;
        exit(0);
    }

    if (solver->getConf().verbosity >= 6) {
        cout
        << "prepare INSERT successful"
        << endl;
    }

    //Validate parameter count
    unsigned long param_count = mysql_stmt_param_count(mystruct.stmt);
    if (param_count != numElems*numInserts) {
        cout
        << "invalid parameter count returned by MySQL"
        << endl;

        exit(1);
    }

    //Clear mem of bind, get enough mem for vars
    assert(!mystruct.bind.empty());
    memset(&mystruct.bind[0], 0, mystruct.bind.size()*sizeof(MYSQL_BIND));
    mystruct.value.resize(numInserts);
    mystruct.num.resize(numInserts);

    //Bind the local variables to the statement
    bindAt = 0;
    for(size_t i = 0; i < numInserts; i++) {
        bindTo(mystruct, runID);
        bindTo(mystruct, mystruct.sumConflicts);
        bindTo(mystruct, mystruct.value[i]);
        bindTo(mystruct, mystruct.num[i]);
    }
    assert(bindAt == numElems*numInserts);

    //Bind the buffers
    if (mysql_stmt_bind_param(mystruct.stmt, &mystruct.bind[0])) {
        cout << "mysql_stmt_bind_param() failed" << endl
        << mysql_stmt_error(mystruct.stmt) << endl;
        exit(1);
    }
}

void MySQLStats::initSizeGlueScatterSTMT(
    const Solver* solver
    , const size_t numInserts
) {
    const size_t numElems = 5;

    //Output what we are trying to
    if (solver->getConf().verbosity >= 6)
        cout
        << "c Trying to prepare statement with " << numInserts
        << " bulk inserts"
        << endl;

    stmtSizeGlueScatter.bind.resize(numElems*numInserts);

    std::stringstream ss;
    ss << "insert into `sizeGlue`"
    << "("
    << " `runID`, `conflicts`, `size`, `glue`, `num`"
    << ") values ";
    for(size_t i = 0; i < numInserts; i++) {
        writeQuestionMarks(numElems, ss);

        if (i != numInserts-1)
            ss << " , ";
    }
    ss << ";";

    //Get memory for statement
    stmtSizeGlueScatter.stmt = mysql_stmt_init(serverConn);
    if (!stmtSizeGlueScatter.stmt) {
        cout << "Error: mysql_stmt_init() out of memory" << endl;
        exit(1);
    }

    //Prepare the statement
    if (mysql_stmt_prepare(stmtSizeGlueScatter.stmt, ss.str().c_str(), ss.str().length())) {
        cout
        << "Error in mysql_stmt_prepare(), INSERT failed"
        << endl
        << mysql_stmt_error(stmtSizeGlueScatter.stmt)
        << endl
        << "Query was: " << ss.str()
        << endl;
        exit(0);
    }

    if (solver->getConf().verbosity >= 6) {
        cout
        << "prepare INSERT successful"
        << endl;
    }

    //Validate parameter count
    unsigned long param_count = mysql_stmt_param_count(stmtSizeGlueScatter.stmt);
    if (param_count != numElems*numInserts) {
        cout
        << "invalid parameter count returned by MySQL"
        << endl;

        exit(1);
    }

    //Clear mem of bind, get enough mem for vars
    assert(!stmtSizeGlueScatter.bind.empty());
    memset(&stmtSizeGlueScatter.bind[0], 0, stmtSizeGlueScatter.bind.size()*sizeof(MYSQL_BIND));
    stmtSizeGlueScatter.size.resize(numInserts);
    stmtSizeGlueScatter.glue.resize(numInserts);
    stmtSizeGlueScatter.num.resize(numInserts);

    //Bind the local variables to the statement
    bindAt = 0;
    for(size_t i = 0; i < numInserts; i++) {
        bindTo(stmtSizeGlueScatter, runID);
        bindTo(stmtSizeGlueScatter, stmtSizeGlueScatter.sumConflicts);
        bindTo(stmtSizeGlueScatter, stmtSizeGlueScatter.size[i]);
        bindTo(stmtSizeGlueScatter, stmtSizeGlueScatter.glue[i]);
        bindTo(stmtSizeGlueScatter, stmtSizeGlueScatter.num[i]);
    }
    assert(bindAt == numElems*numInserts);

    //Bind the buffers
    if (mysql_stmt_bind_param(stmtSizeGlueScatter.stmt, &stmtSizeGlueScatter.bind[0])) {
        cout << "mysql_stmt_bind_param() failed" << endl
        << mysql_stmt_error(stmtSizeGlueScatter.stmt) << endl;
        exit(1);
    }
}

//Prepare statement for restart
void MySQLStats::initRestartSTMT(
    uint64_t verbosity
) {
    const size_t numElems = sizeof(stmtRst.bind)/sizeof(MYSQL_BIND);

    std::stringstream ss;
    ss << "insert into `restart`"
    << "("
    //Position
    << "  `runID`, `simplifications`, `restarts`, `conflicts`, `time`"

    //Clause stats
    << ", numIrredBins, numIrredTris, numIrredLongs"
    << ", numRedBins, numRedTris, numRedLongs"
    << ", numIrredLits, numRedLits"

    //Conflict stats
    << ", `glue`, `glueSD`, `glueMin`, `glueMax`"
    << ", `size`, `sizeSD`, `sizeMin`, `sizeMax`"
    << ", `resolutions`, `resolutionsSD`, `resolutionsMin`, `resolutionsMax`"
    << ", `conflAfterConfl`"

    //Search stats
    << ", `branchDepth`, `branchDepthSD`, `branchDepthMin`, `branchDepthMax`"
    << ", `branchDepthDelta`, `branchDepthDeltaSD`, `branchDepthDeltaMin`, `branchDepthDeltaMax`"
    << ", `trailDepth`, `trailDepthSD`, `trailDepthMin`, `trailDepthMax`"
    << ", `trailDepthDelta`, `trailDepthDeltaSD`, `trailDepthDeltaMin`,`trailDepthDeltaMax`"
    << ", `agility`"

    //Propagations
    << ", `propBinIrred` , `propBinRed` "
    << ", `propTriIrred` , `propTriRed`"
    << ", `propLongIrred` , `propLongRed`"

    //Conflicts
    << ", `conflBinIrred`, `conflBinRed`"
    << ", `conflTriIrred`, `conflTriRed`"
    << ", `conflLongIrred`, `conflLongRed`"

    //Learnts
    << ", `learntUnits`, `learntBins`, `learntTris`, `learntLongs`"

    //Misc
    << ", `watchListSizeTraversed`, `watchListSizeTraversedSD`"
    << ", `watchListSizeTraversedMin`, `watchListSizeTraversedMax`"
    << ", `litPropagatedSomething`, `litPropagatedSomethingSD`"

    //Resolutions
    << ", `resolBin`, `resolTri`, `resolLIrred`, `resolLRed`"

    //Var stats
    << ", `propagations`"
    << ", `decisions`"
    << ", `avgDecLevelVarLT`"
    << ", `avgTrailLevelVarLT`"
    << ", `avgDecLevelVar`"
    << ", `avgTrailLevelVar`"
    << ", `flipped`, `varSetPos`, `varSetNeg`"
    << ", `free`, `replaced`, `eliminated`, `set`"
    << ") values ";
    writeQuestionMarks(
        numElems
        , ss
    );
    ss << ";";

    //Get memory for statement
    stmtRst.stmt = mysql_stmt_init(serverConn);
    if (!stmtRst.stmt) {
        cout << "Error: mysql_stmt_init() out of memory" << endl;
        exit(1);
    }

    //Prepare the statement
    if (mysql_stmt_prepare(stmtRst.stmt, ss.str().c_str(), ss.str().length())) {
        cout
        << "Error in mysql_stmt_prepare(), INSERT failed"
        << endl
        << mysql_stmt_error(stmtRst.stmt)
        << endl
        << "Query was: " << ss.str()
        << endl;
        exit(0);
    }

    if (verbosity >= 6) {
        cout
        << "prepare INSERT successful"
        << endl;
    }

    //Validate parameter count
    unsigned long param_count = mysql_stmt_param_count(stmtRst.stmt);
    if (param_count != numElems) {
        cout
        << "invalid parameter count returned by MySQL"
        << endl;

        exit(1);
    }

    memset(stmtRst.bind, 0, sizeof(stmtRst.bind));


    //Bind the local variables to the statement
    bindAt =0;
    bindTo(stmtRst, runID);
    bindTo(stmtRst, stmtRst.numSimplify);
    bindTo(stmtRst, stmtRst.sumRestarts);
    bindTo(stmtRst, stmtRst.sumConflicts);
    bindTo(stmtRst, stmtRst.cpuTime);

    //Clause stats
    bindTo(stmtRst, stmtRst.numIrredBins);
    bindTo(stmtRst, stmtRst.numIrredTris);
    bindTo(stmtRst, stmtRst.numIrredLongs);
    bindTo(stmtRst, stmtRst.numRedBins);
    bindTo(stmtRst, stmtRst.numRedTris);
    bindTo(stmtRst, stmtRst.numRedLongs);

    bindTo(stmtRst, stmtRst.numIrredLits);
    bindTo(stmtRst, stmtRst.numRedLits);

    //Conflict stats
    bindTo(stmtRst, stmtRst.glueHist);
    bindTo(stmtRst, stmtRst.glueHistSD);
    bindTo(stmtRst, stmtRst.glueHistMin);
    bindTo(stmtRst, stmtRst.glueHistMax);

    bindTo(stmtRst, stmtRst.conflSizeHist);
    bindTo(stmtRst, stmtRst.conflSizeHistSD);
    bindTo(stmtRst, stmtRst.conflSizeHistMin);
    bindTo(stmtRst, stmtRst.conflSizeHistMax);

    bindTo(stmtRst, stmtRst.numResolutionsHist);
    bindTo(stmtRst, stmtRst.numResolutionsHistSD);
    bindTo(stmtRst, stmtRst.numResolutionsHistMin);
    bindTo(stmtRst, stmtRst.numResolutionsHistMax);

    bindTo(stmtRst, stmtRst.conflictAfterConflict);

    //Search stats
    bindTo(stmtRst, stmtRst.branchDepthHist);
    bindTo(stmtRst, stmtRst.branchDepthHistSD);
    bindTo(stmtRst, stmtRst.branchDepthHistMin);
    bindTo(stmtRst, stmtRst.branchDepthHistMax);

    bindTo(stmtRst, stmtRst.branchDepthDeltaHist);
    bindTo(stmtRst, stmtRst.branchDepthDeltaHistSD);
    bindTo(stmtRst, stmtRst.branchDepthDeltaHistMin);
    bindTo(stmtRst, stmtRst.branchDepthDeltaHistMax);

    bindTo(stmtRst, stmtRst.trailDepthHist);
    bindTo(stmtRst, stmtRst.trailDepthHistSD);
    bindTo(stmtRst, stmtRst.trailDepthHistMin);
    bindTo(stmtRst, stmtRst.trailDepthHistMax);

    bindTo(stmtRst, stmtRst.trailDepthDeltaHist);
    bindTo(stmtRst, stmtRst.trailDepthDeltaHistSD);
    bindTo(stmtRst, stmtRst.trailDepthDeltaHistMin);
    bindTo(stmtRst, stmtRst.trailDepthDeltaHistMax);

    bindTo(stmtRst, stmtRst.agilityHist);

    //Prop
    bindTo(stmtRst, stmtRst.propsBinIrred);
    bindTo(stmtRst, stmtRst.propsBinRed);
    bindTo(stmtRst, stmtRst.propsTriIrred);
    bindTo(stmtRst, stmtRst.propsTriRed);
    bindTo(stmtRst, stmtRst.propsLongIrred);
    bindTo(stmtRst, stmtRst.propsLongRed);

    //Confl
    bindTo(stmtRst, stmtRst.conflsBinIrred);
    bindTo(stmtRst, stmtRst.conflsBinRed);
    bindTo(stmtRst, stmtRst.conflsTriIrred);
    bindTo(stmtRst, stmtRst.conflsTriRed);
    bindTo(stmtRst, stmtRst.conflsLongIrred);
    bindTo(stmtRst, stmtRst.conflsLongRed);

    //Learnt
    bindTo(stmtRst, stmtRst.learntUnits);
    bindTo(stmtRst, stmtRst.learntBins);
    bindTo(stmtRst, stmtRst.learntTris);
    bindTo(stmtRst, stmtRst.learntLongs);

    //Misc
    bindTo(stmtRst, stmtRst.watchListSizeTraversed);
    bindTo(stmtRst, stmtRst.watchListSizeTraversedSD);
    bindTo(stmtRst, stmtRst.watchListSizeTraversedMin);
    bindTo(stmtRst, stmtRst.watchListSizeTraversedMax);

    bindTo(stmtRst, stmtRst.litPropagatedSomething);
    bindTo(stmtRst, stmtRst.litPropagatedSomethingSD);

    //Resolutions
    bindTo(stmtRst, stmtRst.resolv.binCl);
    bindTo(stmtRst, stmtRst.resolv.triCl);
    bindTo(stmtRst, stmtRst.resolv.irredLCl);
    bindTo(stmtRst, stmtRst.resolv.redLCl);

    //Var stats
    bindTo(stmtRst, stmtRst.propagations);
    bindTo(stmtRst, stmtRst.decisions);

    bindTo(stmtRst, stmtRst.varVarStats.avgDecLevelVarLT);
    bindTo(stmtRst, stmtRst.varVarStats.avgTrailLevelVarLT);
    bindTo(stmtRst, stmtRst.varVarStats.avgDecLevelVar);
    bindTo(stmtRst, stmtRst.varVarStats.avgTrailLevelVar);

    bindTo(stmtRst, stmtRst.varFlipped);
    bindTo(stmtRst, stmtRst.varSetPos);
    bindTo(stmtRst, stmtRst.varSetNeg);
    bindTo(stmtRst, stmtRst.numFreeVars);
    bindTo(stmtRst, stmtRst.numReplacedVars);
    bindTo(stmtRst, stmtRst.numVarsElimed);
    bindTo(stmtRst, stmtRst.trailSize);
    assert(bindAt == numElems);

    // Bind the buffers
    if (mysql_stmt_bind_param(stmtRst.stmt, stmtRst.bind)) {
        cout << "mysql_stmt_bind_param() failed" << endl
        << mysql_stmt_error(stmtRst.stmt) << endl;
        exit(1);
    }
}

//Prepare statement for restart
void MySQLStats::initReduceDBSTMT(
    uint64_t verbosity
) {
    const size_t numElems = sizeof(stmtReduceDB.bind)/sizeof(MYSQL_BIND);

    std::stringstream ss;
    ss << "insert into `reduceDB`"
    << "("
    //Position
    << "  `runID`, `simplifications`, `restarts`, `conflicts`, `time`"
    << ", `reduceDBs`"

    //Actual data
    << ", `irredClsVisited`, `irredLitsVisited`, `irredProps`, `irredConfls`, `irredUIP`"
    << ", `redClsVisited`, `redLitsVisited`, `redProps`, `redConfls`, `redUIP`"

    //Clean data
    << ", preRemovedNum, preRemovedLits, preRemovedGlue"
    << ", preRemovedResolBin, preRemovedResolTri, preRemovedResolLIrred, preRemovedResolLRed"
    << ", preRemovedAge, preRemovedAct"
    << ", preRemovedLitVisited, preRemovedProp, preRemovedConfl"
    << ", preRemovedLookedAt"

    << ", removedNum, removedLits, removedGlue"
    << ", removedResolBin, removedResolTri, removedResolLIrred, removedResolLRed"
    << ", removedAge, removedAct"
    << ", removedLitVisited, removedProp, removedConfl"
    << ", removedLookedAt"

    << ", remainNum, remainLits, remainGlue"
    << ", remainResolBin, remainResolTri, remainResolLIrred, remainResolLRed"
    << ", remainAge, remainAct"
    << ", remainLitVisited, remainProp, remainConfl"
    << ", remainLookedAt"
    << ") values ";
    writeQuestionMarks(
        numElems
        , ss
    );
    ss << ";";

    //Get memory for statement
    stmtReduceDB.stmt = mysql_stmt_init(serverConn);
    if (!stmtReduceDB.stmt) {
        cout << "Error: mysql_stmt_init() out of memory" << endl;
        exit(1);
    }

    //Prepare the statement
    if (mysql_stmt_prepare(stmtReduceDB.stmt, ss.str().c_str(), ss.str().length())) {
        cout
        << "Error in mysql_stmt_prepare(), INSERT failed"
        << endl
        << mysql_stmt_error(stmtReduceDB.stmt)
        << endl
        << "Query was: " << ss.str()
        << endl;
        exit(0);
    }

    if (verbosity >= 6) {
        cout
        << "prepare INSERT successful"
        << endl;
    }

    //Validate parameter count
    unsigned long param_count = mysql_stmt_param_count(stmtReduceDB.stmt);
    if (param_count != numElems) {
        cout
        << "invalid parameter count returned by MySQL"
        << endl;

        exit(1);
    }

    memset(stmtReduceDB.bind, 0, sizeof(stmtReduceDB.bind));


    //Bind the local variables to the statement
    bindAt =0;
    bindTo(stmtReduceDB, runID);
    bindTo(stmtReduceDB, stmtReduceDB.numSimplify);
    bindTo(stmtReduceDB, stmtReduceDB.sumRestarts);
    bindTo(stmtReduceDB, stmtReduceDB.sumConflicts);
    bindTo(stmtReduceDB, stmtReduceDB.cpuTime);
    bindTo(stmtReduceDB, stmtReduceDB.reduceDBs);

    //Clause stats -- irred
    bindTo(stmtReduceDB, stmtReduceDB.irredClsVisited);
    bindTo(stmtReduceDB, stmtReduceDB.irredLitsVisited);
    bindTo(stmtReduceDB, stmtReduceDB.irredProps);
    bindTo(stmtReduceDB, stmtReduceDB.irredConfls);
    bindTo(stmtReduceDB, stmtReduceDB.irredUIP);

    //Clause stats -- red
    bindTo(stmtReduceDB, stmtReduceDB.redClsVisited);
    bindTo(stmtReduceDB, stmtReduceDB.redLitsVisited);
    bindTo(stmtReduceDB, stmtReduceDB.redProps);
    bindTo(stmtReduceDB, stmtReduceDB.redConfls);
    bindTo(stmtReduceDB, stmtReduceDB.redUIP);

    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.num);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.lits);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.glue);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.resol.binCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.resol.triCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.resol.irredLCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.resol.redLCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.age);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.act);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.numLitVisited);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.numProp);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.numConfl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.preRemove.numLookedAt);

    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.num);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.lits);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.glue);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.resol.binCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.resol.triCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.resol.irredLCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.resol.redLCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.age);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.act);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.numLitVisited);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.numProp);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.numConfl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.removed.numLookedAt);

    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.num);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.lits);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.glue);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.resol.binCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.resol.triCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.resol.irredLCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.resol.redLCl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.age);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.act);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.numLitVisited);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.numProp);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.numConfl);
    bindTo(stmtReduceDB, stmtReduceDB.clean.remain.numLookedAt);

    assert(bindAt == numElems);

    // Bind the buffers
    if (mysql_stmt_bind_param(stmtReduceDB.stmt, stmtReduceDB.bind)) {
        cout << "mysql_stmt_bind_param() failed" << endl
        << mysql_stmt_error(stmtReduceDB.stmt) << endl;
        exit(1);
    }
}

void MySQLStats::initVarSTMT(
    const Solver* solver
    , StmtVar& stmtVar
    , uint64_t numInserts
) {
    const size_t numElems = 15;

    //Output what we are trying to
    if (solver->getConf().verbosity >= 6)
        cout
        << "c Trying to prepare statement with " << numInserts
        << " bulk inserts"
        << endl;

    stmtVar.data.resize(numInserts);
    stmtVar.bind.resize(numElems*numInserts);

    std::stringstream ss;
    ss << "insert into `vars`"
    << "("

    //Position
    << " `varInitID`, `var`"

    //Actual data
    << ", `posPolarSet`, `negPolarSet`, `flippedPolarity`"
    << ", `posDecided`, `negDecided`"

    //Dec level history stats
    << ", `decLevelAvg`, `decLevelSD`, `decLevelMin`, `decLevelMax`"

    //Trail level history stats
    << ", `trailLevelAvg`, `trailLevelSD`, `trailLevelMin`, `trailLevelMax`"
    << ") values ";
    for(size_t i = 0; i < stmtVar.data.size(); i++) {
        writeQuestionMarks(numElems, ss);

        if (i != stmtVar.data.size()-1)
            ss << " , ";
    }
    ss << ";";

    //Get memory for statement
    stmtVar.stmt = mysql_stmt_init(serverConn);
    if (!stmtVar.stmt) {
        cout << "Error: mysql_stmt_init() out of memory" << endl;
        exit(1);
    }

    //Prepare the statement
    if (mysql_stmt_prepare(stmtVar.stmt, ss.str().c_str(), ss.str().length())) {
        cout
        << "Error in mysql_stmt_prepare(), INSERT failed"
        << endl
        << mysql_stmt_error(stmtVar.stmt)
        << endl
        << "Query was: " << ss.str()
        << endl;
        exit(0);
    }

    if (solver->getConf().verbosity >= 6) {
        cout
        << "prepare INSERT successful"
        << endl;
    }

    //Validate parameter count
    unsigned long param_count = mysql_stmt_param_count(stmtVar.stmt);
    if (param_count != numElems*numInserts) {
        cout
        << "invalid parameter count returned by MySQL"
        << endl;

        exit(1);
    }

    //Clear bind
    assert(!stmtVar.bind.empty());
    memset(&stmtVar.bind[0], 0, numElems*numInserts*sizeof(MYSQL_BIND));

    //Bind the local variables to the statement
    bindAt = 0;
    for(size_t i = 0; i < stmtVar.data.size(); i++) {
        bindTo(stmtVar, stmtVar.varInitID);
        bindTo(stmtVar, stmtVar.data[i].var);
        bindTo(stmtVar, stmtVar.data[i].posPolarSet);
        bindTo(stmtVar, stmtVar.data[i].negPolarSet);
        bindTo(stmtVar, stmtVar.data[i].flippedPolarity);
        bindTo(stmtVar, stmtVar.data[i].posDecided);
        bindTo(stmtVar, stmtVar.data[i].negDecided);

        bindTo(stmtVar, stmtVar.data[i].decLevelAvg);
        bindTo(stmtVar, stmtVar.data[i].decLevelSD);
        bindTo(stmtVar, stmtVar.data[i].decLevelMin);
        bindTo(stmtVar, stmtVar.data[i].decLevelMax);

        bindTo(stmtVar, stmtVar.data[i].trailLevelAvg);
        bindTo(stmtVar, stmtVar.data[i].trailLevelSD);
        bindTo(stmtVar, stmtVar.data[i].trailLevelMin);
        bindTo(stmtVar, stmtVar.data[i].trailLevelMax);
    }
    assert(bindAt == numElems*numInserts);

    // Bind the buffers
    if (mysql_stmt_bind_param(stmtVar.stmt, &stmtVar.bind[0])) {
        cout << "mysql_stmt_bind_param() failed" << endl
        << mysql_stmt_error(stmtVar.stmt) << endl;
        exit(1);
    }
}

void MySQLStats::varDataDump(
    const Solver* solver
    , const Searcher* search
    , const vector<Var>& varsToDump
    , const vector<VarData>& varData
) {
    double myTime = cpuTime();

    //Get ID for varDataInit
    std::stringstream ss;
    ss
    << "INSERT INTO varDataInit (`runID`, `simplifications`, `restarts`, `conflicts`, `time`)"
    << " values ("
    << runID
    << ", " << solver->getSolveStats().numSimplify
    << ", " << search->sumRestarts()
    << ", " << search->sumConflicts()
    << ", " << cpuTime()
    << ");";

    if (mysql_query(serverConn, ss.str().c_str())) {
        if (solver->getConf().verbosity >= 6) {
            cout << "c Couldn't insert into table `varDataInit`" << endl;
            cout << "c " << mysql_error(serverConn) << endl;
        }
        exit(-1);
    }
    my_ulonglong id = mysql_insert_id(serverConn);

    //Do bulk insert by defult
    StmtVar* stmtVar = &stmtVarBulk;

    //Go through top N variables

    size_t at = 0;
    size_t i = 0;
    size_t numToDump = varsToDump.size();

    for(vector<Var>::const_iterator
        it = varsToDump.begin(), end = varsToDump.end()
        ; it != end
        ; it++, i++
    ) {
        size_t var = *it;

        //If we are at beginning of bulk, but not enough is left, do one-by-one
        if ((at == 0 && numToDump-i < stmtVarBulk.data.size())) {
            stmtVar = &stmtVarSingle;
            at = 0;
        }

        stmtVar->varInitID = id;
        //Back-number variables
        stmtVar->data[at].var = solver->getInterToOuterMain()[var];

        //Overall stats
        stmtVar->data[at].posPolarSet = varData[var].stats.posPolarSet;
        stmtVar->data[at].negPolarSet = varData[var].stats.negPolarSet;
        stmtVar->data[at].flippedPolarity  = varData[var].stats.flippedPolarity;
        stmtVar->data[at].posDecided  = varData[var].stats.posDecided;
        stmtVar->data[at].negDecided  = varData[var].stats.negDecided;

        //Dec level history stats
        stmtVar->data[at].decLevelAvg  = varData[var].stats.decLevelHist.avg();
        stmtVar->data[at].decLevelSD   = sqrt(varData[var].stats.decLevelHist.var());
        stmtVar->data[at].decLevelMin  = varData[var].stats.decLevelHist.getMin();
        stmtVar->data[at].decLevelMax  = varData[var].stats.decLevelHist.getMax();

        //Trail level history stats
        stmtVar->data[at].trailLevelAvg  = varData[var].stats.trailLevelHist.avg();
        stmtVar->data[at].trailLevelSD   = sqrt(varData[var].stats.trailLevelHist.var());
        stmtVar->data[at].trailLevelMin  = varData[var].stats.trailLevelHist.getMin();
        stmtVar->data[at].trailLevelMax  = varData[var].stats.trailLevelHist.getMax();
        at++;

        if (at == stmtVar->data.size()) {
            if (mysql_stmt_execute(stmtVar->stmt)) {
                cout
                << "ERROR: while executing restart insertion MySQL prepared statement"
                << endl;

                cout << "Error from mysql: "
                << mysql_stmt_error(stmtVar->stmt)
                << endl;

                exit(-1);
            }
            at = 0;
        }
    }
    assert(at == 0 && "numInserts must be divisible");

    if (solver->getConf().verbosity >= 6) {
        cout
        << "c Time to insert variables' stats into DB: "
        << std::fixed << std::setprecision(2) << std::setw(3)
        << cpuTime() - myTime
        << " s"
        << endl;
    }
}

void MySQLStats::clauseSizeDistrib(
    uint64_t sumConflicts
    , const vector<uint32_t>& sizes
) {
    assert(sizes.size() == stmtClsDistribSize.value.size());
    assert(sizes.size() == stmtClsDistribSize.num.size());

    stmtClsDistribSize.sumConflicts = sumConflicts;
    for(size_t i = 0; i < sizes.size(); i++) {
        stmtClsDistribSize.value[i] = i;
        stmtClsDistribSize.num[i]  = sizes[i];
    }

    if (mysql_stmt_execute(stmtClsDistribSize.stmt)) {
        cout
        << "ERROR: while executing restart insertion MySQL prepared statement"
        << endl;

        cout << "Error from mysql: "
        << mysql_stmt_error(stmtClsDistribSize.stmt)
        << endl;

        exit(-1);
    }
}

void MySQLStats::clauseGlueDistrib(
    uint64_t sumConflicts
    , const vector<uint32_t>& glues
) {
    assert(glues.size() == stmtClsDistribGlue.value.size());
    assert(glues.size() == stmtClsDistribGlue.num.size());

    stmtClsDistribGlue.sumConflicts = sumConflicts;
    for(size_t i = 0; i < glues.size(); i++) {
        stmtClsDistribGlue.value[i] = i;
        stmtClsDistribGlue.num[i]  = glues[i];
    }

    if (mysql_stmt_execute(stmtClsDistribGlue.stmt)) {
        cout
        << "ERROR: while executing restart insertion MySQL prepared statement"
        << endl;

        cout << "Error from mysql: "
        << mysql_stmt_error(stmtClsDistribGlue.stmt)
        << endl;

        exit(-1);
    }
}

void MySQLStats::clauseSizeGlueScatter(
    uint64_t sumConflicts
    , boost::multi_array<uint32_t, 2>& sizeAndGlue
) {
    //assert(glues.size() == stmtClsDistrib.value.size());
    //assert(glues.size() == stmtClsDistrib.num.size());

    const size_t numInserts = stmtSizeGlueScatter.size.size();

    stmtSizeGlueScatter.sumConflicts = sumConflicts;

    size_t at = 0;
    for(size_t i = 0; i < sizeAndGlue.shape()[0]; i++) {
        for(size_t i2 = 0; i2 < sizeAndGlue.shape()[1]; i2++) {
            stmtSizeGlueScatter.size[at] = i;
            stmtSizeGlueScatter.glue[at] = i2;
            stmtSizeGlueScatter.num[at]  = sizeAndGlue[i][i2];
            at++;

            if (at == numInserts) {
                if (mysql_stmt_execute(stmtSizeGlueScatter.stmt)) {
                    cout
                    << "ERROR: while executing restart insertion MySQL prepared statement"
                    << endl;

                    cout << "Error from mysql: "
                    << mysql_stmt_error(stmtSizeGlueScatter.stmt)
                    << endl;

                    exit(-1);
                }
                at = 0;
            }
        }
    }
    assert(at == 0 && "numInserts must be divisible");
}

void MySQLStats::reduceDB(
    const ClauseUsageStats& irredStats
    , const ClauseUsageStats& redStats
    , const CleaningStats& clean

    , const Solver* solver
) {
    //Position of solving
    stmtReduceDB.numSimplify     = solver->getSolveStats().numSimplify;
    stmtReduceDB.sumRestarts     = solver->sumRestarts();
    stmtReduceDB.sumConflicts    = solver->sumConflicts();
    stmtReduceDB.cpuTime         = cpuTime();
    stmtReduceDB.reduceDBs       = solver->getSolveStats().nbReduceDB;

    //Clause data for IRRED
    stmtReduceDB.irredLitsVisited   = irredStats.sumLitVisited;
    stmtReduceDB.irredClsVisited    = irredStats.sumLookedAt;
    stmtReduceDB.irredProps         = irredStats.sumProp;
    stmtReduceDB.irredConfls        = irredStats.sumConfl;
    stmtReduceDB.irredUIP           = irredStats.sumUsedUIP;

    //Clause data for RED
    stmtReduceDB.redLitsVisited     = redStats.sumLitVisited;
    stmtReduceDB.redClsVisited      = redStats.sumLookedAt;
    stmtReduceDB.redProps           = redStats.sumProp;
    stmtReduceDB.redConfls          = redStats.sumConfl;
    stmtReduceDB.redUIP             = redStats.sumUsedUIP;

    //Clean data
    stmtReduceDB.clean              = clean;

    if (mysql_stmt_execute(stmtReduceDB.stmt)) {
        cout
        << "ERROR: while executing clause DB cleaning MySQL prepared statement"
        << endl;

        cout << "Error from mysql: "
        << mysql_stmt_error(stmtReduceDB.stmt)
        << endl;

        exit(-1);
    }
}


void MySQLStats::restart(
    const PropStats& thisPropStats
    , const Searcher::Stats& thisStats
    , const VariableVariance& varVarStats
    , const Solver* solver
    , const Searcher* search
) {
    const Searcher::Hist& searchHist = search->getHistory();
    const Solver::BinTriStats& binTri = solver->getBinTriStats();

    //Position of solving
    stmtRst.numSimplify     = solver->getSolveStats().numSimplify;
    stmtRst.sumRestarts     = search->sumRestarts();
    stmtRst.sumConflicts    = search->sumConflicts();
    stmtRst.cpuTime         = cpuTime();

    //Clause stats
    stmtRst.numIrredBins  = binTri.irredBins;
    stmtRst.numIrredTris  = binTri.irredTris;
    stmtRst.numIrredLongs = solver->getNumLongIrredCls();
    stmtRst.numIrredLits  = binTri.irredLits;
    stmtRst.numRedBins    = binTri.redBins;
    stmtRst.numRedTris    = binTri.redTris;
    stmtRst.numRedLongs   = solver->getNumLongRedCls();
    stmtRst.numRedLits    = binTri.redLits;

    //Conflict stats
    stmtRst.glueHist        = searchHist.glueHist.getLongtTerm().avg();
    stmtRst.glueHistSD      = sqrt(searchHist.glueHist.getLongtTerm().var());
    stmtRst.glueHistMin      = searchHist.glueHist.getLongtTerm().getMin();
    stmtRst.glueHistMax      = searchHist.glueHist.getLongtTerm().getMax();

    stmtRst.conflSizeHist   = searchHist.conflSizeHist.getLongtTerm().avg();
    stmtRst.conflSizeHistSD = sqrt(searchHist.conflSizeHist.getLongtTerm().var());
    stmtRst.conflSizeHistMin = searchHist.conflSizeHist.getLongtTerm().getMin();
    stmtRst.conflSizeHistMax = searchHist.conflSizeHist.getLongtTerm().getMax();

    stmtRst.numResolutionsHist =
        searchHist.numResolutionsHist.avg();
    stmtRst.numResolutionsHistSD =
        sqrt(searchHist.numResolutionsHist.var());
    stmtRst.numResolutionsHistMin =
        searchHist.numResolutionsHist.getMin();
    stmtRst.numResolutionsHistMax =
        searchHist.numResolutionsHist.getMax();

    stmtRst.conflictAfterConflict =
        searchHist.conflictAfterConflict.avg()*100.0;

    //Search stats
    stmtRst.branchDepthHist         = searchHist.branchDepthHist.avg();
    stmtRst.branchDepthHistSD       = sqrt(searchHist.branchDepthHist.var());
    stmtRst.branchDepthHistMin      = searchHist.branchDepthHist.getMin();
    stmtRst.branchDepthHistMax      = searchHist.branchDepthHist.getMax();


    stmtRst.branchDepthDeltaHist    = searchHist.branchDepthDeltaHist.getLongtTerm().avg();
    stmtRst.branchDepthDeltaHistSD  = sqrt(searchHist.branchDepthDeltaHist.getLongtTerm().var());
    stmtRst.branchDepthDeltaHistMin  = searchHist.branchDepthDeltaHist.getLongtTerm().getMin();
    stmtRst.branchDepthDeltaHistMax  = searchHist.branchDepthDeltaHist.getLongtTerm().getMax();

    stmtRst.trailDepthHist          = searchHist.trailDepthHist.avg();
    stmtRst.trailDepthHistSD        = sqrt(searchHist.trailDepthHist.var());
    stmtRst.trailDepthHistMin       = searchHist.trailDepthHist.getMin();
    stmtRst.trailDepthHistMax       = searchHist.trailDepthHist.getMax();

    stmtRst.trailDepthDeltaHist     = searchHist.trailDepthDeltaHist.avg();
    stmtRst.trailDepthDeltaHistSD   = sqrt(searchHist.trailDepthDeltaHist.var());
    stmtRst.trailDepthDeltaHistMin  = searchHist.trailDepthDeltaHist.getMin();
    stmtRst.trailDepthDeltaHistMax  = searchHist.trailDepthDeltaHist.getMax();

    stmtRst.agilityHist             = searchHist.agilityHist.avg();

    //Prop
    stmtRst.propsBinIrred    = thisPropStats.propsBinIrred;
    stmtRst.propsBinRed      = thisPropStats.propsBinRed;
    stmtRst.propsTriIrred    = thisPropStats.propsTriIrred;
    stmtRst.propsTriRed      = thisPropStats.propsTriRed;
    stmtRst.propsLongIrred   = thisPropStats.propsLongIrred;
    stmtRst.propsLongRed     = thisPropStats.propsLongRed;

    //Confl
    stmtRst.conflsBinIrred  =  thisStats.conflStats.conflsBinIrred;
    stmtRst.conflsBinRed    = thisStats.conflStats.conflsBinRed;
    stmtRst.conflsTriIrred  = thisStats.conflStats.conflsTriIrred;
    stmtRst.conflsTriRed    = thisStats.conflStats.conflsTriRed;
    stmtRst.conflsLongIrred = thisStats.conflStats.conflsLongIrred;
    stmtRst.conflsLongRed   = thisStats.conflStats.conflsLongRed;

    //Learnt
    stmtRst.learntUnits = thisStats.learntUnits;
    stmtRst.learntBins  = thisStats.learntBins;
    stmtRst.learntTris  = thisStats.learntTris;
    stmtRst.learntLongs = thisStats.learntLongs;

    //Misc
    stmtRst.watchListSizeTraversed   = searchHist.watchListSizeTraversed.avg();
    stmtRst.watchListSizeTraversedSD = sqrt(searchHist.watchListSizeTraversed.var());
    stmtRst.watchListSizeTraversedMin= searchHist.watchListSizeTraversed.getMin();
    stmtRst.watchListSizeTraversedMax= searchHist.watchListSizeTraversed.getMax();


    stmtRst.litPropagatedSomething   = searchHist.litPropagatedSomething.avg()*100.0;
    stmtRst.litPropagatedSomethingSD = sqrt(searchHist.litPropagatedSomething.var())*100.0;

    //Var stats
    stmtRst.decisions       = thisStats.decisions;
    stmtRst.propagations    = thisPropStats.propagations;
    stmtRst.varVarStats     = varVarStats;

    stmtRst.varFlipped      = thisPropStats.varFlipped;
    stmtRst.varSetPos       = thisPropStats.varSetPos;
    stmtRst.varSetNeg       = thisPropStats.varSetNeg;
    stmtRst.numFreeVars     = solver->getNumFreeVars();
    stmtRst.numReplacedVars = solver->getNumVarsReplaced();
    stmtRst.numVarsElimed   = solver->getNumVarsElimed();
    stmtRst.trailSize       = search->getTrailSize();

    if (mysql_stmt_execute(stmtRst.stmt)) {
        cout
        << "ERROR: while executing restart insertion MySQL prepared statement"
        << endl;

        cout << "Error from mysql: "
        << mysql_stmt_error(stmtRst.stmt)
        << endl;

        exit(-1);
    }
}

