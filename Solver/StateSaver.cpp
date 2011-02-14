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

#include "StateSaver.h"
#include "PartHandler.h"

StateSaver::StateSaver(Solver& _solver) :
    solver(_solver)
    , backup_order_heap(Solver::VarOrderLt(solver.varData))
{
    //Saving Solver state
    backup_var_inc = solver.var_inc;
    backup_activity.resize(solver.varData.size());
    backup_polarities.resize(solver.varData.size());
    for (uint32_t i = 0; i < solver.varData.size(); i++) {
        backup_activity[i] = solver.varData[i].activity;
        backup_polarities[i] = solver.varData[i].polarity;
    }
    backup_order_heap = solver.order_heap;
    backup_restartType = solver.restartType;
    backup_bogoProps = solver.bogoProps;
    backup_propagations = solver.propagations;
    backup_random_var_freq = solver.conf.random_var_freq;
}

void StateSaver::restore()
{
    //Restore Solver state
    solver.var_inc = backup_var_inc;
    for (uint32_t i = 0; i < solver.varData.size(); i++) {
        solver.varData[i].activity = backup_activity[i];
        solver.varData[i].polarity = backup_polarities[i];
    }
    solver.order_heap = backup_order_heap;
    solver.restartType = backup_restartType;
    solver.bogoProps = backup_bogoProps;
    solver.propagations = backup_propagations;
    solver.conf.random_var_freq = backup_random_var_freq;

    //Finally, clear the order_heap from variables set/non-decisionned
    solver.order_heap.filter(Solver::VarFilter(solver));

    for (Var var = 0; var < solver.nVars(); var++) {
        if (solver.decision_var[var]
            && solver.value(var) == l_Undef
            && solver.partHandler->getSavedState()[var] == l_Undef) solver.insertVarOrder(var);
    }
}
