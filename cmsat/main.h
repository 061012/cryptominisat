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

#ifndef MAIN_H
#define MAIN_H

#include <string>
#include <vector>
#include <memory>

#include "solvertypes.h"
#include "solverconf.h"

class Solver;

using std::string;
using std::vector;

class Main
{
    public:
        Main(int argc, char** argv);

        void parseCommandLine();
        int solve();

    private:

        Solver* solver;

        //File reading
        void readInAFile(const string& filename);
        void readInStandardInput();
        void parseInAllFiles();

        //Helper functions
        void printResultFunc(const lbool ret);
        void printVersionInfo();
        int correctReturnValue(const lbool ret) const;

        //Config
        SolverConf conf;
        int numThreads;
        bool debugLib;
        bool debugNewVar;
        int printResult;
        string commandLine;

        //Multi-start solving
        uint32_t max_nr_of_solutions;
        int doBanFoundSolution;

        //Files to read & write
        bool fileNamePresent;
        vector<string> filesToRead;

        //Command line arguments
        int argc;
        char** argv;
};

#endif //MAIN_H
