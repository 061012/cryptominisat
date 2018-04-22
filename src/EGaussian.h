/******************************************
Copyright (c) 2012  Cheng-Shen Han
Copyright (c) 2012  Jie-Hong Roland Jiang
Copyright (c) 2018  Mate Soos

For more information, see " When Boolean Satisfiability Meets Gaussian
Elimination in a Simplex Way." by Cheng-Shen Han and Jie-Hong Roland Jiang
in CAV (Computer Aided Verification), 2012: 410-426


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

#ifndef ENHANCEGAUSSIAN_H
#define ENHANCEGAUSSIAN_H

#include <vector>
#include <limits>
#include <string>
#include <utility>

#include "solvertypes.h"
#include "packedmatrix.h"
#include "bitarray.h"
#include "propby.h"
#include "xor.h"
#include "gausswatched.h"

//#define VERBOSE_DEBUG
//#define DEBUG_GAUSS
#define basic_var true
#define non_basic_var false

using std::string;
using std::pair;
using std::vector;

namespace CMSat {

class Solver;

class EGaussian {
  protected:
    // gaussian state        0          1            2             3               4
    enum gaussian_ret {conflict, unit_conflict, propagation, unit_propagation, nothing};

    Solver* solver;   // orignal sat solver
    const GaussConf& config;  // gauss some configure
    const uint32_t matrix_no;            // matrix index
    vector<Lit> tmp_clause;  // conflict&propagation handling

    PackedMatrix      clause_state;        // clasue state
    vec<bool>         GasVar_state ;      // variable state  : basic or non-basic
    vector<uint32_t>  var_to_col;             // variable to column
    class matrixset { // matrix information
      public:
        // added by hankf4
        vec<uint32_t> nb_rows; // the non_basic value in each row

        // used in orignal matrix
        PackedMatrix matrix; // The matrix, updated to reflect variable assignements
        vector<uint32_t> col_to_var; // col_to_var[COL] tells which variable is at a given column in the matrix. Gives unassigned_var if the COL has been zeroed (i.e. the variable assigned)
        uint16_t num_rows; // number of active rows in the matrix. Unactive rows are rows that contain only zeros (and if they are conflicting, then the conflict has been treated)
        uint32_t num_cols; // number of active columns in the matrix. The columns at the end that have all be zeroed are no longer active
    };
    matrixset cur_matrixset; // The current matrixset, i.e. the one we are working on, or the last one we worked on


    // fucntion
    void print_matrix(matrixset& m) const ;   // print matrix
    void eliminate(matrixset& m) ;            //gaussian elimination
    gaussian_ret adjust_matrix(matrixset& matrix); // adjust matrix, include watch, check row is zero, etc.

    inline void propagation_twoclause(const bool xorEqualFalse);
    inline void conflict_twoclause(PropBy& confl);
    inline void delete_gausswatch(const bool orig_basic, const uint16_t  row_n);

  public:
    // variable
    vector<Xor> xorclauses;   // xorclauses
    vector<pair<Clause*, uint32_t> > clauses_toclear; // use to delete propagate clause


    EGaussian(
        Solver* solver,
        const GaussConf& config,
        const uint32_t matrix_no,
        const vector<Xor>& xorclauses
    );
    ~EGaussian();

    // functiion
    void canceling(const uint32_t sublevel); //functions used throughout the Solver
    bool full_init(bool& created);  // initial arrary. return true is fine , return false means solver already false;
    void fill_matrix(matrixset& origMat); // Fills the origMat matrix
    uint32_t select_columnorder(matrixset& origMat); // Fills var_to_col and col_to_var of the origMat matrix.

    //execute gaussian
    bool  find_truths2(
        const GaussWatched* i,
        GaussWatched*& j,
        uint32_t p,
        PropBy& confl,
        const uint16_t row_n, bool& do_eliminate, uint32_t& e_var, uint16_t& e_row_n,
        int& ret_gauss,
        vector<Lit>& conflict_clause_gauss,
        uint32_t& conflict_size_gauss,
        bool& xorEqualFalse_gauss
    );

    // when basic variable is touch , eliminate one col
    void eliminate_col2(
        uint32_t e_var,
        uint16_t e_row_n,
        uint32_t p,
        PropBy& confl,
        int& ret_gauss,
        vector<Lit>& conflict_clause_gauss,
        uint32_t& conflict_size_gauss,
        bool& xorEqualFalse_gauss
    );

    void Debug_funtion(); // used to debug
};

}


#endif //ENHANCEGAUSSIAN_H
