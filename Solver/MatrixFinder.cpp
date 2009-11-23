/***********************************************************************************
CryptoMiniSat -- Copyright (c) 2009 Mate Soos

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
**************************************************************************************************/

#include "MatrixFinder.h"

#include "Solver.h"
#include "Gaussian.h"
#include "GaussianConfig.h"

#include <set>
#include <map>
#include <iomanip>
using std::set;
using std::map;

//#define VERBOSE_DEBUG

#ifdef VERBOSE_DEBUG
using std::cout;
using std::endl;
#endif

MatrixFinder::MatrixFinder(Solver *_S) :
    S(_S)
{
}

const uint MatrixFinder::findMatrixes()
{
    if (S->xorclauses.size() == 0)
        return 0;
    
    const uint unAssigned = S->nVars() + 1;
    vector<uint> table(S->nVars(), unAssigned); //var -> matrix
    map<uint, vector<Var> > reverseTable; //matrix -> vars
    uint matrix_no = 0;
    
    for (XorClause** c = S->xorclauses.getData(), **end = c + S->xorclauses.size(); c != end; c++) {
        set<uint> tomerge;
        vector<Var> newSet;
        for (Lit *l = &(**c)[0], *end2 = l + (**c).size(); l != end2; l++) {
            if (table[l->var()] != unAssigned)
                tomerge.insert(table[l->var()]);
            else
                newSet.push_back(l->var());
        }
        if (tomerge.size() == 1) {
            const uint into = *tomerge.begin();
            map<uint, vector<Var> >::iterator intoReverse = reverseTable.find(into);
            for (uint i = 0; i < newSet.size(); i++) {
                intoReverse->second.push_back(newSet[i]);
                table[newSet[i]] = into;
            }
            continue;
        }
        
        for (set<uint>::iterator it = tomerge.begin(); it != tomerge.end(); it++) {
            newSet.insert(newSet.end(), reverseTable[*it].begin(), reverseTable[*it].end());
            reverseTable.erase(*it);
        }
        for (uint i = 0; i < newSet.size(); i++)
            table[newSet[i]] = matrix_no;
        reverseTable[matrix_no] = newSet;
        matrix_no++;
    }
    
    #ifdef VERBOSE_DEBUG
    for (map<uint, vector<Var> >::iterator it = reverseTable.begin(), end = reverseTable.end(); it != end; it++) {
        cout << "-- set begin --" << endl;
        for (vector<Var>::iterator it2 = it->second.begin(), end2 = it->second.end(); it2 != end2; it2++) {
            cout << *it2 << ", ";
        }
        cout << "-------" << endl;
    }
    #endif
    
    vector<uint> numXorInMatrix(matrix_no, 0);
    for (XorClause** c = S->xorclauses.getData(), **end = c + S->xorclauses.size(); c != end; c++) {
        XorClause& x = **c;
        numXorInMatrix[table[x[0].var()]]++;
    }
    
    uint realMatrixNum = 0;
    vector<uint> remapMatrixes(matrix_no, UINT_MAX);
    for (uint i = 0; i < numXorInMatrix.size(); i++) {
        if (numXorInMatrix[i] >= 20 && numXorInMatrix[i] <= 500 && realMatrixNum < (1 << 12) ) {
            cout << "|  Matrix no " << std::setw(4) << realMatrixNum << "      no. rows : " << std::setw(5) << numXorInMatrix[i] << " no cols: " << std::setw(6) << reverseTable[i].size() << std::setw(24) << "|" << endl;
            remapMatrixes[i] = realMatrixNum;
            realMatrixNum++;
        }
    }
    
    for (XorClause** c = S->xorclauses.getData(), **end = c + S->xorclauses.size(); c != end; c++) {
        XorClause& x = **c;
        const uint toSet = remapMatrixes[table[x[0].var()]];
        if (toSet != UINT_MAX)
            x.setMatrix(toSet);
        else
            x.setMatrix((1 << 12)-1);
    }
    
    for (uint i = 0; i < realMatrixNum; i++)
        S->gauss_matrixes.push_back(new Gaussian(*S, i, S->gaussconfig));
    
    return realMatrixNum;
}
