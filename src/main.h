/*
 * CryptoMiniSat
 *
 * Copyright (c) 2009-2015, Mate Soos. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.0 of the License.
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

#ifndef MAIN_H
#define MAIN_H

#include <string>
#include <vector>
#include <memory>
#include <fstream>

#include "solverconf.h"
#include "cryptominisat4/cryptominisat.h"

using std::string;
using std::vector;

#include <boost/program_options.hpp>
namespace po = boost::program_options;
using namespace CMSat;

struct SATCount {
    uint32_t hashCount;
    uint32_t cellSolCount;
};

class Main
{
    public:
        Main(int argc, char** argv);

        void parseCommandLine();
        int solve();

    private:
        string typeclean;
        string var_elim_strategy;
        string drupfilname;
        void add_supported_options();
        void check_options_correctness();
        void manually_parse_some_options();
        void parse_var_elim_strategy();
        void handle_drup_option();
        void parse_restart_type();
        void parse_polarity_type();
        void dumpIfNeeded() const;
        void check_num_threads_sanity(const unsigned thread_num) const;

        po::positional_options_description p;
        po::variables_map vm;
        po::options_description cmdline_options;
        po::options_description help_options_simple;
        po::options_description help_options_complicated;

        SATSolver* solver = NULL;

        //File reading
        void readInAFile(const string& filename);
        void readInStandardInput();
        void parseInAllFiles();

        //Helper functions
        void printResultFunc(
            std::ostream* os
            , const bool toFile
            , const lbool ret
            , const bool firstSolut
        );
        void printVersionInfo();
        int correctReturnValue(const lbool ret) const;

        //Config
        SolverConf conf;
        bool needResultFile = false;
        bool zero_exit_status = false;
        std::string resultFilename;

        std::string debugLib;
        int printResult = true;
        string commandLine;
        unsigned num_threads = 1;

        //Multi-start solving
        uint32_t max_nr_of_solutions = 1;

        //Files to read & write
        bool fileNamePresent;
        vector<string> filesToRead;

        //Command line arguments
        int argc;
        char** argv;

        //Drup checker
        std::ostream* drupf = NULL;
        bool drupDebug = false;

        //Unistuff
        SATCount ApproxMC(Solver& solver, vector<FILE*>* resLog, std::mt19937& randomEngine);
        uint32_t UniGen(uint32_t samples, Solver& solver
            , FILE* res, vector<FILE*>* resLog, uint32_t sampleCounter
            , std::mt19937& randomEngine, std::map<std::string, uint32_t>& solutionMap
            , uint32_t* lastSuccessfulHashOffset, double timeReference);
        bool AddHash(uint32_t clausNum, Solver& s, vec<Lit>& assumptions, std::mt19937& randomEngine);
        int32_t BoundedSATCount(uint32_t maxSolutions, Solver& solver, vec<Lit>& assumptions);
        lbool BoundedSAT(uint32_t maxSolutions, uint32_t minSolutions, Solver& solver, vec<Lit>& assumptions, std::mt19937& randomEngine, std::map<std::string, uint32_t>& solutionMap, uint32_t* solutionCount);
        bool GenerateRandomBits(string& randomBits, uint32_t size, std::mt19937& randomEngine);
        uint32_t SolutionsToReturn(uint32_t maxSolutions, uint32_t minSolutions, unsigned long currentSolutions);
        int GenerateRandomNum(int maxRange, std::mt19937& randomEngine);
        void printResultFunc(Solver& S, vec<lbool> solutionModel, const lbool ret, FILE* res);
        bool printSolutions(FILE* res);
        void SeedEngine(std::mt19937& randomEngine);

        time_t  startTime;
        std::map< std::string, std::vector<uint32_t>> globalSolutionMap;
};

#endif //MAIN_H
