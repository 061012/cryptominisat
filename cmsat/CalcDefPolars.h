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

#ifndef CALCDEFAULTPOLARITIES__H
#define CALCDEFAULTPOLARITIES__H

#include "Vec.h"
#include "Clause.h"

class Solver;

class CalcDefPolars
{
    public:
        CalcDefPolars(Solver* solver);
        const vector<char> calculate();

    private:
        void tallyVotes(const vector<Clause*>& cs, vector<double>& votes) const;
        void tallyVotesBin(vector<double>& votes, const vector<vec<Watched> >& watched) const;

        Solver* solver;
};

#endif //CALCDEFAULTPOLARITIES__H
