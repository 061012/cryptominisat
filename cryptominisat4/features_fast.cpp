#include <iostream>
#include <fstream>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <math.h>

#include "solver.h"
#include "features_fast.h"

using std::vector;
using namespace CMSat;

template<class Function, class Function2>
void FeatureExtract::for_one_clause(
    const Watched& cl
    , const Lit lit
    ,  Function func
    ,  Function2 func_each_lit
) const {
    unsigned neg_vars = 0;
    unsigned pos_vars = 0;
    unsigned size = 0;

    switch (cl.getType()) {
        case CMSat::watch_binary_t: {
            if (cl.red()) {
                //only irred cls
                break;
            }
            if (lit > cl.lit2()) {
                //only count once
                break;
            }

            pos_vars += !lit.sign();
            pos_vars += !cl.lit2().sign();
            size = 2;
            neg_vars = size - pos_vars;
            func(size, pos_vars, neg_vars);
            func_each_lit(lit, size, pos_vars, neg_vars);
            func_each_lit(cl.lit2(), size, pos_vars, neg_vars);
            break;
        }

        case CMSat::watch_tertiary_t: {
            if (cl.red()) {
                //only irred cls
                break;
            }
            if (lit > cl.lit2()) {
                //only count once
                break;
            }

            assert(cl.lit2() < cl.lit3());

            pos_vars += !lit.sign();
            pos_vars += !cl.lit2().sign();
            pos_vars += !cl.lit3().sign();
            size = 3;
            neg_vars = size - pos_vars;
            func(size, pos_vars, neg_vars);
            func_each_lit(lit, size, pos_vars, neg_vars);
            func_each_lit(cl.lit2(), size, pos_vars, neg_vars);
            func_each_lit(cl.lit3(), size, pos_vars, neg_vars);
            break;
        }

        case CMSat::watch_clause_t: {
            const Clause& clause = *solver->cl_alloc.ptr(cl.get_offset());
            if (clause.red()) {
                //only irred cls
                break;
            }
            if (clause[0] < clause[1]) {
                //only count once
                break;
            }

            for (const Lit cl_lit : clause) {
                pos_vars += !cl_lit.sign();
            }
            size = clause.size();
            neg_vars = size - pos_vars;
            func(size, pos_vars, neg_vars);
            for (const Lit cl_lit : clause) {
                func_each_lit(cl_lit, size, pos_vars, neg_vars);
            }
            break;
        }
    }
}

template<class Function, class Function2>
void FeatureExtract::for_all_clauses(Function func, Function2 func_each_lit) const
{
    for (size_t i = 0; i < solver->nVars() * 2; i++) {
        Lit lit = Lit::toLit(i);
        for (const Watched & w : solver->watches[lit.toInt()]) {
            for_one_clause(w, lit, func, func_each_lit);
        }
    }
}

void FeatureExtract::fill_vars_cls()
{
    feat.numVars = solver->nVars();
    feat.numClauses = solver->longIrredCls.size() + solver->binTri.irredBins + solver->binTri.irredTris;
    feat.unary = solver->get_num_nonfree_vars();
    feat.binary = solver->binTri.irredBins;
    feat.trinary = solver->binTri.irredTris;
    myVars.resize(solver->nVars());

    auto func = [&](unsigned /*size*/, unsigned pos_vars, unsigned /*neg_vars*/) -> bool {
        if (pos_vars <= 1 ) {
            feat.horn += 1;
            return true;
        }
        return false;
    };
    auto func_each_lit = [&](Lit lit, unsigned /*size*/, unsigned pos_vars, unsigned /*neg_vars*/) -> void {
        if (pos_vars <= 1 ) {
            myVars[lit.var()].horn++;
        }

        if (!lit.sign()) {
            myVars[lit.var()].numPos++;
        }
        myVars[lit.var()].size++;
    };
    for_all_clauses(func, func_each_lit);
}

void FeatureExtract::print_stats() const
{
    const char* sep = ", ";

    cout << "c [features] ";
    fprintf( stdout, "numVars: %d%s", feat.numVars, sep );
    fprintf( stdout, "numClauses: %d%s", feat.numClauses, sep );
    double tmp = feat.numVars;
    if (tmp > 0) {
        tmp /= (1.0 * feat.numClauses);
    }
    fprintf( stdout, "(numVars/(1.0*numClauses) %.5f%s", tmp, sep );

    fprintf( stdout, "vcg_var_mean %.5f%s", feat.vcg_var_mean, sep );
    fprintf( stdout, "vcg_var_std %.5f%s", feat.vcg_var_std, sep );
    fprintf( stdout, "vcg_var_min %.5f%s", feat.vcg_var_min, sep );
    fprintf( stdout, "vcg_var_max %.5f%s", feat.vcg_var_max, sep );
    fprintf( stdout, "vcg_var_spread %.5f%s", feat.vcg_var_spread, sep );

    fprintf( stdout, "vcg_cls_mean %.5f%s", feat.vcg_cls_mean, sep );
    fprintf( stdout, "vcg_cls_std %.5f%s", feat.vcg_cls_std, sep );
    fprintf( stdout, "vcg_cls_min %.5f%s", feat.vcg_cls_min, sep );
    fprintf( stdout, "vcg_cls_max %.5f%s", feat.vcg_cls_max, sep );
    fprintf( stdout, "vcg_cls_spread %.5f%s", feat.vcg_cls_spread, sep );

    fprintf( stdout, "pnr_var_mean %.5f%s", feat.pnr_var_mean, sep );
    fprintf( stdout, "pnr_var_std %.5f%s", feat.pnr_var_std, sep );
    fprintf( stdout, "pnr_var_min %.5f%s", feat.pnr_var_min, sep );
    fprintf( stdout, "pnr_var_max %.5f%s", feat.pnr_var_max, sep );
    fprintf( stdout, "pnr_var_spread %.5f%s", feat.pnr_var_spread, sep );

    fprintf( stdout, "pnr_cls_mean %.5f%s", feat.pnr_cls_mean, sep );
    fprintf( stdout, "pnr_cls_std %.5f%s", feat.pnr_cls_std, sep );
    fprintf( stdout, "pnr_cls_min %.5f%s", feat.pnr_cls_min, sep );
    fprintf( stdout, "pnr_cls_max %.5f%s", feat.pnr_cls_max, sep );
    fprintf( stdout, "pnr_cls_spread %.5f%s", feat.pnr_cls_spread, sep );

    fprintf( stdout, "unary %.5f%s", feat.unary, sep );
    fprintf( stdout, "binary %.5f%s", feat.binary, sep );
    fprintf( stdout, "trinary %.5f%s", feat.trinary, sep );
    fprintf( stdout, "horn_mean %.5f%s", feat.horn_mean, sep );
    fprintf( stdout, "horn_std %.5f%s", feat.horn_std, sep );
    fprintf( stdout, "horn_min %.5f%s", feat.horn_min, sep );
    fprintf( stdout, "horn_max %.5f%s", feat.horn_max, sep );
    fprintf( stdout, "horn_spread %.5f%s", feat.horn_spread, sep );
    fprintf( stdout, "horn %.5f", feat.horn );
    fprintf( stdout, "\n");
}

Features FeatureExtract::extract()
{
    double start_time = cpuTime();
    feat.numVars = 0;
    for ( int vv = 0; vv < (int)myVars.size(); vv++ ) {
        if ( myVars[vv].size > 0 ) {
            feat.numVars++;
        }
    }

    auto empty_func = [](const Lit, unsigned /*size*/, unsigned /*pos_vars*/, unsigned /*neg_vars*/) -> void {};
    auto func = [&](unsigned size, unsigned pos_vars, unsigned /*neg_vars*/) -> void {
        if (size == 0 ) {
            return;
        }

        double _size = (double)size / (1.0 * feat.numVars);
        if ( _size < feat.vcg_cls_min ) {
            feat.vcg_cls_min = _size;
        }
        if ( _size > feat.vcg_cls_max ) {
            feat.vcg_cls_max = _size;
        }
        feat.vcg_cls_mean += _size;

        double _pnr = 0.5 + ((2.0 * (double)pos_vars - (double)size) / (2.0 * (double)size));
        if ( _pnr < feat.pnr_cls_min ) {
            feat.pnr_cls_min = _pnr;
        }
        if ( _pnr > feat.pnr_cls_max ) {
            feat.pnr_cls_max = _pnr;
        }
        feat.pnr_cls_mean += _pnr;
    };
    for_all_clauses(func, empty_func);

    feat.vcg_cls_mean /= 1.0 * feat.numClauses;
    feat.pnr_cls_mean /= 1.0 * feat.numClauses;
    feat.horn /= 1.0 * feat.numClauses;
    feat.unary /= 1.0 * feat.numClauses;
    feat.binary /= 1.0 * feat.numClauses;
    feat.trinary /= 1.0 * feat.numClauses;

    feat.vcg_cls_spread = feat.vcg_cls_max - feat.vcg_cls_min;
    feat.pnr_cls_spread = feat.pnr_cls_max - feat.pnr_cls_min;

    for ( int vv = 0; vv < (int)myVars.size(); vv++ ) {
        if ( myVars[vv].size == 0 ) {
            continue;
        }

        double _size = myVars[vv].size / (1.0 * feat.numClauses);
        if ( vv == 0 || _size < feat.vcg_var_min ) {
            feat.vcg_var_min = _size;
        }
        if ( vv == 0 || _size > feat.vcg_var_max ) {
            feat.vcg_var_max = _size;
        }
        feat.vcg_var_mean += _size;

        double _pnr = 0.5 + ((2.0 * myVars[vv].numPos - myVars[vv].size)
                             / (2.0 * myVars[vv].size));
        if ( vv == 0 || _pnr < feat.pnr_var_min ) {
            feat.pnr_var_min = _pnr;
        }
        if ( vv == 0 || _pnr > feat.pnr_var_max ) {
            feat.pnr_var_max = _pnr;
        }
        feat.pnr_var_mean += _pnr;

        double _horn = myVars[vv].horn / (1.0 * feat.numClauses);
        if ( vv == 0 || _horn < feat.horn_min ) {
            feat.horn_min = _horn;
        }
        if ( vv == 0 || _horn > feat.horn_max ) {
            feat.horn_max = _horn;
        }
        feat.horn_mean += _horn;
    }
    if (feat.vcg_var_mean > 0) {
        feat.vcg_var_mean /= 1.0 * feat.numVars;
    }
    if (feat.pnr_var_mean > 0) {
        feat.pnr_var_mean /= 1.0 * feat.numVars;
    }
    if (feat.horn_mean > 0) {
        feat.horn_mean /= 1.0 * feat.numVars;
    }

    feat.vcg_var_spread = feat.vcg_var_max - feat.vcg_var_min;
    feat.pnr_var_spread = feat.pnr_var_max - feat.pnr_var_min;
    feat.horn_spread = feat.horn_max - feat.horn_min;

    auto func2 = [&](unsigned size, unsigned pos_vars, unsigned /*neg_vars*/) -> void {
        if ( size == 0 ) {
            return;
        }

        double _size = (double)size / (1.0 * feat.numVars);
        feat.vcg_cls_std += (feat.vcg_cls_mean - _size) * (feat.vcg_cls_mean - _size);

        double _pnr = 0.5 + ((2.0 * (double)pos_vars - (double)size) / (2.0 * (double)size));
        feat.pnr_cls_std += (feat.pnr_cls_mean - _pnr) * (feat.pnr_cls_mean - _pnr);
    };
    for_all_clauses(func2, empty_func);

    if ( feat.vcg_cls_std > feat.eps && feat.vcg_cls_mean > feat.eps ) {
        feat.vcg_cls_std = sqrt(feat.vcg_cls_std / (1.0 * feat.numClauses)) / feat.vcg_cls_mean;
    } else {
        feat.vcg_cls_std = 0;
    }
    if ( feat.pnr_cls_std > feat.eps && feat.pnr_cls_mean > feat.eps ) {
        feat.pnr_cls_std = sqrt(feat.pnr_cls_std / (1.0 * feat.numClauses)) / feat.pnr_cls_mean;
    } else {
        feat.pnr_cls_std = 0;
    }

    for ( int vv = 0; vv < (int)myVars.size(); vv++ ) {
        if ( myVars[vv].size == 0 ) {
            continue;
        }

        double _size = myVars[vv].size / (1.0 * feat.numClauses);
        feat.vcg_var_std += (feat.vcg_var_mean - _size) * (feat.vcg_var_mean - _size);

        double _pnr = 0.5 + ((2.0 * myVars[vv].numPos - myVars[vv].size) / (2.0 * myVars[vv].size));
        feat.pnr_var_std += (feat.pnr_var_mean - _pnr) * (feat.pnr_var_mean - _pnr);

        double _horn = myVars[vv].horn / (1.0 * feat.numClauses);
        feat.horn_std += (feat.horn_mean - _horn) * (feat.horn_mean - _horn);
    }
    if ( feat.vcg_var_std > eps && feat.vcg_var_mean > eps ) {
        feat.vcg_var_std = sqrt(feat.vcg_var_std / (1.0 * feat.numVars)) / feat.vcg_var_mean;
    } else {
        vcg_var_std = 0;
    }

    if ( feat.pnr_var_std > eps && feat.pnr_var_mean > feat.eps ) {
        feat.pnr_var_std = sqrt(feat.pnr_var_std / (1.0 * feat.numVars)) / feat.pnr_var_mean;
    } else {
        feat.pnr_var_std = 0;
    }

    if ( feat.horn_std / (1.0 * feat.numVars) > feat.eps && feat.horn_mean > feat.eps ) {
        feat.horn_std = sqrt(feat.horn_std / (1.0 * feat.numVars)) / feat.horn_mean;
    } else {
        horn_std = 0;
    }

    if (solver->conf.verbosity >= 2) {
        cout << "c [features] extracted"
        << solver->conf.print_times(cpuTime() - start_time)
        << endl;
    }

    return feat;
}
