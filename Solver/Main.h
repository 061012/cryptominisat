/*****************************************************************************
MiniSat -- Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
CryptoMiniSat -- Copyright (c) 2009 Mate Soos

Original code by MiniSat authors are under an MIT licence.
Modifications for CryptoMiniSat are under GPLv3 licence.
******************************************************************************/

#ifndef MAIN_H
#define MAIN_H

#include <string>
using std::string;
#include <vector>
#ifndef DISABLE_ZLIB
#include <zlib.h>
#endif // DISABLE_ZLIB

#include "Solver.h"

class Main
{
    public:
        Main(int argc, char** argv);

        int singleThreadSolve();
        void* oneThreadSolve( void *ptr );
        int multiThreadSolve();

        static void printStats(Solver& solver);

    private:

        void printUsage(char** argv, Solver& S);
        const char* hasPrefix(const char* str, const char* prefix);
        void printResultFunc(const Solver& S, const lbool ret, FILE* res);
        void parseCommandLine(Solver& S);

        //File reading
        template<class B>
        void readInAFile(B stuff, Solver& solver);
        void parseInAllFiles(Solver& solver);
        FILE* openOutputFile();
        #ifndef DISABLE_ZLIB
        gzFile openGzFile(int inNum);
        gzFile openGzFile(const char* name);
        #endif //DISABLE_ZLIB

        void setDoublePrecision(const uint32_t verbosity);
        void printVersionInfo(const uint32_t verbosity);
        int correctReturnValue(const lbool ret) const;

        //Stats
        template<class T, class T2>
        static void printStatsLine(string left, T value, T2 value2, string extra);
        template<class T>
        static void printStatsLine(string left, T value, string extra = "");

        bool grouping;
        bool debugLib;
        bool debugNewVar;
        bool printResult;
        uint32_t max_nr_of_solutions;
        bool fileNamePresent;
        bool twoFileNamesPresent;
        std::vector<std::string> filesToRead;

        int argc;
        char** argv;
};

#endif //MAIN_H