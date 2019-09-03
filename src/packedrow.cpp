/******************************************
Copyright (c) 2018  Mate Soos
Copyright (c) 2012  Cheng-Shen Han
Copyright (c) 2012  Jie-Hong Roland Jiang

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

#include "packedrow.h"

using namespace CMSat;

///returns popcnt
uint32_t PackedRow::find_watchVar(
    vector<Lit>& tmp_clause,
    const vector<uint32_t>& col_to_var,
    vector<char> &var_has_resp_row,
    uint32_t& non_resp_var
) {
    uint32_t popcnt = 0;
    non_resp_var = std::numeric_limits<uint32_t>::max();
    tmp_clause.clear();

    for(int i = 0; i < size*32 && popcnt < 3; i++) {
        if (this->operator[](i)){
            popcnt++;
            uint32_t var = col_to_var[i];
            tmp_clause.push_back(Lit(var, false));

            if (!var_has_resp_row[var]) {
                non_resp_var = var;
            } else {
                //What??? WARNING
                //This var already has a responsible for it...
                //How can it be 1???
                std::swap(tmp_clause[0], tmp_clause.back());
            }
        }
    }
    assert(tmp_clause.size() == popcnt);
    assert( popcnt == 0 || var_has_resp_row[ tmp_clause[0].var() ]) ;
    return popcnt;
}

void PackedRow::get_reason(
    vector<Lit>& tmp_clause,
    const vector<lbool>& assigns,
    const vector<uint32_t>& col_to_var,
    Lit prop
) {
    for (int i = 0; i < size; i++) if (mp[i]) {
        int tmp = mp[i];
        int at = __builtin_ffs(tmp);
        int extra = 0;
        while (at != 0) {
            uint32_t col = extra + at-1 + i*32;
            #ifdef SLOW_DEBUG
            assert(this->operator[](col) == 1);
            #endif
            const uint32_t var = col_to_var[col];
            if (var == prop.var()) {
                tmp_clause.push_back(prop);
                std::swap(tmp_clause[0], tmp_clause.back());
            } else {
                const lbool val = assigns[var];
                const bool val_bool = (val == l_True);
                tmp_clause.push_back(Lit(var, val_bool));
            }

            extra += at;
            if (extra == 32)
                break;

            tmp >>= at;
            at = __builtin_ffs(tmp);
        }
    }

    #ifdef SLOW_DEBUG
    for(uint32_t i = 1; i < tmp_clause.size(); i++) {
        assert(assigns[tmp_clause[i].var()] != l_Undef);
    }
    #endif
}

gret PackedRow::propGause(
    vector<Lit>& tmp_clause,
    const vector<lbool>& assigns,
    const vector<uint32_t>& col_to_var,
    vector<char> &var_has_resp_row,
    uint32_t& new_resp_var,
    PackedRow& tmp_col,
    PackedRow& tmp_col2,
    PackedRow& cols_vals,
    PackedRow& cols_set,
    Lit& ret_lit_prop
) {
    //cout << "start" << endl;
    //cout << "line: " << *this << endl;
    new_resp_var = std::numeric_limits<uint32_t>::max();
    tmp_col.and_inv(*this, cols_set);
    uint32_t pop = tmp_col.popcnt();

    //Find new watch
    if (pop >=2) {
        for (int i = 0; i < size; i++) if (tmp_col.mp[i]) {
            int tmp = tmp_col.mp[i];
            int at = __builtin_ffs(tmp);
            int extra = 0;
            while (at != 0) {
                uint32_t col = extra + at-1 + i*32;
                //cout << "col: " << col << " extra: " << extra << " at: " << at << endl;
                assert(tmp_col[col] == 1);
                const uint32_t var = col_to_var[col];
                const lbool val = assigns[var];

                // found new non-basic variable, let's watch it
                assert(val == l_Undef);
                if (!var_has_resp_row[var]) {
                    new_resp_var = var;
                    return gret::nothing_fnewwatch;
                }

                extra += at;
                if (extra == 32)
                    break;

                tmp >>= at;
                at = __builtin_ffs(tmp);
            }
        }
        assert(false && "Should have found a new watch!");
    }

    //Calc value of row
    tmp_col2 = *this;
    tmp_col2 &= cols_vals;
    const uint32_t pop_t = tmp_col2.popcnt() + tmp_col2.rhs();

    //Lazy prop
    if (pop == 1) {
        for (int i = 0; i < size; i++) if (tmp_col.mp[i]) {
            int tmp = tmp_col.mp[i];
            int at = __builtin_ffs(tmp);

            // found prop
            uint32_t col = at-1 + i*32;
            assert(tmp_col[col] == 1);
            const uint32_t var = col_to_var[col];
            assert(assigns[var] == l_Undef);
            ret_lit_prop = Lit(var, !(pop_t % 2));
            return gret::prop;
        }
        assert(false && "Should have found the propagating literal!");
    }

    //Only SAT & UNSAT left.
    assert(pop == 0);

    //Satisfied
    if (pop_t % 2 == 0) {
        return gret::nothing_satisfied;
    }

    //Conflict
    tmp_clause.clear();
    #ifdef SLOW_DEBUG
    bool final_val = !rhs_internal;
    #endif
    for (int i = 0; i < size; i++) if (mp[i]) {
        int tmp = mp[i];
        int at = __builtin_ffs(tmp);
        int extra = 0;
        while (at != 0) {
            uint32_t col = extra + at-1 + i*32;
            #ifdef SLOW_DEBUG
            assert(this->operator[](col) == 1);
            #endif
            const uint32_t var = col_to_var[col];
            const lbool val = assigns[var];
            const bool val_bool = (val == l_True);
            #ifdef SLOW_DEBUG
            final_val ^= val_bool;
            #endif
            tmp_clause.push_back(Lit(var, val_bool));

            //TODO check do we need this????
            //if this is the basic variable, put it to the 0th position
            if (var_has_resp_row[var]) {
                std::swap(tmp_clause[0], tmp_clause.back());
            }

            extra += at;
            tmp >>= at;
            if (extra == 32)
                break;

            at = __builtin_ffs(tmp);
        }
    }

    #ifdef SLOW_DEBUG
    assert(!final_val);
    #endif
    return gret::confl;
}




