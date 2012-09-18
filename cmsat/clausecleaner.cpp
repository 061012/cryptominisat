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

#include "clausecleaner.h"

//#define DEBUG_CLEAN
//#define VERBOSE_DEBUG

ClauseCleaner::ClauseCleaner(Solver* _solver) :
    solver(_solver)
{
}

bool ClauseCleaner::satisfied(const Watched& watched, Lit lit)
{
    assert(watched.isBinary());
    if (solver->value(lit) == l_True) return true;
    if (solver->value(watched.lit1()) == l_True) return true;
    return false;
}

void ClauseCleaner::treatImplicitClauses()
{
    assert(solver->decisionLevel() == 0);

    uint64_t remNonLBin = 0;
    uint64_t remLBin = 0;
    uint64_t remNonLTri = 0;
    uint64_t remLTri = 0;

    //We can only attach these in delayed mode, otherwise we would
    //need to manipulate the watchlist we are going through
    vector<BinaryClause> toAttach;

    size_t wsLit = 0;
    for (vector<vec<Watched> >::iterator
        it = solver->watches.begin(), end = solver->watches.end()
        ; it != end
        ; it++, wsLit++
    ) {
        Lit lit = Lit::toLit(wsLit);
        vec<Watched>& ws = *it;

        vec<Watched>::iterator i = ws.begin();
        vec<Watched>::iterator j = i;
        for (vec<Watched>::iterator end2 = ws.end(); i != end2; i++) {

            //Skip clauses
            if (i->isClause()) {
                *j++ = *i;
                continue;
            }

            //Treat binaries
            if (i->isBinary()) {
                if (satisfied(*i, lit)) {
                    if (i->learnt())
                        remLBin++;
                    else
                        remNonLBin++;
                } else {
                    assert(solver->value(i->lit1()) == l_Undef);
                    assert(solver->value(lit) == l_Undef);
                    *j++ = *i;
                }
                continue;
            }

            //Treat 3-long
            assert(i->isTri());
            bool remove = false;

            //Satisfied?
            if (solver->value(lit) == l_True
                || solver->value(i->lit1()) == l_True
                || solver->value(i->lit2()) == l_True
            ) {
                remove = true;
            }

            //Shortened -- attach bin, but only *once*
            if (!remove
                && solver->value(lit) == l_False
            ) {
                if (lit < i->lit1())
                    toAttach.push_back(BinaryClause(i->lit1(), i->lit2(), i->learnt()));
                remove = true;
            }
            if (!remove
                && solver->value(i->lit1()) == l_False
            ) {
                if (lit < i->lit1())
                    toAttach.push_back(BinaryClause(lit, i->lit2(), i->learnt()));
                remove = true;
            }
            if (!remove
                && solver->value(i->lit2()) == l_False
            ) {
                if (lit < i->lit1())
                    toAttach.push_back(BinaryClause(lit, i->lit1(), i->learnt()));
                remove = true;
            }

            if (remove) {
                if (i->learnt())
                    remLTri++;
                else
                    remNonLTri++;
            } else {
                *j++ = *i;
            }
        }
        ws.shrink_(i - j);
    }

    //Attach delayed binary clauses
    for(vector<BinaryClause>::const_iterator
        it = toAttach.begin(), end = toAttach.end()
        ; it != end
        ; it++
    ) {
        assert(solver->value(it->getLit1()) == l_Undef);
        assert(solver->value(it->getLit2()) == l_Undef);
        solver->attachBinClause(it->getLit1(), it->getLit2(), it->getLearnt());
    }

    assert(remNonLBin % 2 == 0);
    assert(remLBin % 2 == 0);
    assert(remNonLTri % 3 == 0);
    assert(remLTri % 3 == 0);
    solver->irredLits -= remNonLBin + remNonLTri;
    solver->redLits -= remLBin + remLTri;
    solver->irredBins -= remNonLBin/2;
    solver->redBins -= remLBin/2;
    solver->irredTris -= remNonLTri/3;
    solver->redTris -= remLTri/3;
    solver->checkImplicitStats();
}

void ClauseCleaner::cleanClauses(vector<ClOffset>& cs)
{
    assert(solver->decisionLevel() == 0);
    assert(solver->qhead == solver->trail.size());

    #ifdef VERBOSE_DEBUG
    cout << "Cleaning " << (type==binaryClauses ? "binaryClauses" : "normal clauses" ) << endl;
    #endif //VERBOSE_DEBUG

    vector<ClOffset>::iterator s, ss, end;
    for (s = ss = cs.begin(), end = cs.end();  s != end; s++) {
        if (cleanClause(*s)) {
            solver->clAllocator->clauseFree(*s);
        } else {
            *ss++ = *s;
        }
    }
    cs.resize(cs.size() - (s-ss));

    #ifdef VERBOSE_DEBUG
    cout << "cleanClauses(Clause) useful ?? Removed: " << s-ss << endl;
    #endif
}

inline bool ClauseCleaner::cleanClause(ClOffset offset)
{
    Clause& cl = *solver->clAllocator->getPointer(offset);
    assert(cl.size() > 3);
    const uint32_t origSize = cl.size();

    Lit origLit1 = cl[0];
    Lit origLit2 = cl[1];

    Lit *i, *j, *end;
    uint32_t num = 0;
    for (i = j = cl.begin(), end = i + cl.size();  i != end; i++, num++) {
        lbool val = solver->value(*i);
        if (val == l_Undef) {
            *j++ = *i;
            continue;
        }

        if (val == l_True) {
            solver->detachModifiedClause(origLit1, origLit2, origSize, &cl);
            return true;
        }
    }
    cl.shrink(i-j);

    assert(cl.size() > 1);
    if (i != j) {
        if (cl.size() == 2) {
            solver->detachModifiedClause(origLit1, origLit2, origSize, &cl);
            solver->attachBinClause(cl[0], cl[1], cl.learnt());
            return true;
        } else if (cl.size() == 3) {
            solver->detachModifiedClause(origLit1, origLit2, origSize, &cl);
            solver->attachTriClause(cl[0], cl[1], cl[2], cl.learnt());
            return true;
        } else {
            if (cl.learnt())
                solver->redLits -= i-j;
            else
                solver->irredLits -= i-j;
        }
    }

    return false;
}

bool ClauseCleaner::satisfied(const Clause& cl) const
{
    for (uint32_t i = 0; i != cl.size(); i++)
        if (solver->value(cl[i]) == l_True)
            return true;
        return false;
}
