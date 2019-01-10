#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright (C) 2017  Mate Soos
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

from __future__ import print_function
import sqlite3
import optparse
import time
import pickle
import re
import pandas as pd
import numpy as np
import os.path
import sys


class QueryHelper:
    def __init__(self, dbfname):
        if not os.path.isfile(dbfname):
            print("ERROR: Database file '%s' does not exist" % dbfname)
            exit(-1)

        self.conn = sqlite3.connect(dbfname)
        self.c = self.conn.cursor()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.conn.commit()
        self.conn.close()


class QueryFill (QueryHelper):
    def __init__(self, dbfname):
        super(QueryFill, self).__init__(dbfname)

    def create_indexes(self):
        print("Recreating indexes...")
        t = time.time()
        q = """
        drop index if exists `idxclid1`;
        drop index if exists `idxclid1-2`;
        drop index if exists `idxclid1-3`;
        drop index if exists `idxclid1-4`;
        drop index if exists `idxclid1-5`;
        drop index if exists `idxclid2`;
        drop index if exists `idxclid3`;
        drop index if exists `idxclid4`;
        drop index if exists `idxclid5`;
        drop index if exists `idxclid6`;
        drop index if exists `idxclid6-2`;
        drop index if exists `idxclid6-3`;
        drop index if exists `idxclid7`;
        drop index if exists `idxclid8`;
        drop index if exists `idxclidUCLS-1`;

        create index `idxclid1` on `clauseStats` (`clauseID`, conflicts, restarts, latest_satzilla_feature_calc);
        create index `idxclid1-2` on `clauseStats` (`clauseID`);
        create index `idxclid1-3` on `clauseStats` (`clauseID`, restarts);
        create index `idxclid1-4` on `clauseStats` (`clauseID`, restarts, prev_restart);
        create index `idxclid1-5` on `clauseStats` (`clauseID`, prev_restart);
        create index `idxclid2` on `clauseStats` (clauseID, `prev_restart`, conflicts, restarts, latest_satzilla_feature_calc);
        create index `idxclid3` on `goodClauses` (`clauseID`);
        create index `idxclid4` on `restart` ( `restarts`);
        create index `idxclid5` on `tags` ( `tagname`);
        create index `idxclid6` on `reduceDB` (`clauseID`, conflicts, latest_satzilla_feature_calc);
        create index `idxclid6-2` on `reduceDB` (`clauseID`);
        create index `idxclid6-3` on `reduceDB` (`clauseID`, `conflicts`);
        create index `idxclid7` on `satzilla_features` (`latest_satzilla_feature_calc`);
        create index `idxclid8` on `varData` ( `var`, `conflicts`, `clid_start_incl`, `clid_end_notincl`);
        create index `idxclidUCLS-1` on `usedClauses` ( `clauseID`);
        """
        for l in q.split('\n'):
            if options.verbose:
                print("Executing: ", l)
            self.c.execute(l)

        print("indexes created T: %-3.2f s" % (time.time() - t))

    def fill_last_prop(self):
        print("Adding last prop...")
        t = time.time()
        q = """
        update goodClauses
        set last_prop_used =
        (select max(conflicts)
            from reduceDB
            where reduceDB.clauseID = goodClauses.clauseID
                and reduceDB.propagations_made > 0
        );
        """
        self.c.execute(q)
        print("last_prop_used filled T: %-3.2f s" % (time.time() - t))

    def fill_good_clauses_fixed(self):
        print("Filling good clauses fixed...")

        t = time.time()
        q = """DROP TABLE IF EXISTS `goodClausesFixed`;"""
        self.c.execute(q)
        q = """
        create table `goodClausesFixed` (
            `clauseID` bigint(20) NOT NULL,
            `num_used` bigint(20) NOT NULL,
            `first_confl_used` bigint(20) NOT NULL,
            `last_confl_used` bigint(20) NOT NULL,
            `sum_hist_used` bigint(20) DEFAULT NULL,
            `avg_hist_used` double NOT NULL,
            `var_hist_used` double DEFAULT NULL,
            `last_prop_used` bigint(20) DEFAULT NULL
        );"""
        self.c.execute(q)
        print("goodClausesFixed deleted T: %-3.2f s" % (time.time() - t))

        t = time.time()
        q = """insert into goodClausesFixed
        (
        `clauseID`,
        `num_used`,
        `first_confl_used`,
        `last_confl_used`,
        `sum_hist_used`,
        `avg_hist_used`,
        `last_prop_used`
        )
        select
        clauseID
        , sum(num_used)
        , min(first_confl_used)
        , max(last_confl_used)
        , sum(sum_hist_used)
        , (1.0*sum(sum_hist_used))/(1.0*sum(num_used))
        , max(last_prop_used)
        from goodClauses as c group by clauseID;"""
        self.c.execute(q)
        print("goodClausesFixed filled T: %-3.2f s" % (time.time() - t))

        t = time.time()
        q = """
        drop index if exists `idxclid20`;
        drop index if exists `idxclid21`;
        drop index if exists `idxclid21-2`;
        drop index if exists `idxclid22`;

        create index `idxclid20` on `goodClausesFixed` (`clauseID`, first_confl_used, last_confl_used, num_used, avg_hist_used);
        create index `idxclid21` on `goodClausesFixed` (`clauseID`);
        create index `idxclid21-2` on `goodClausesFixed` (`clauseID`, avg_hist_used);
        create index `idxclid22` on `goodClausesFixed` (`clauseID`, last_confl_used);
        """
        for l in q.split('\n'):
            self.c.execute(l)
        print("goodClausesFixed indexes added T: %-3.2f s" % (time.time() - t))

        t = time.time()
        q = """update goodClausesFixed
        set `var_hist_used` = (
        select
        sum(1.0*(used_at-cs.conflicts-avg_hist_used)*(used_at-cs.conflicts-avg_hist_used))/(num_used*1.0)
        from
        clauseStats as cs,
        usedClauses as u
        where goodClausesFixed.clauseID = u.clauseID
        and cs.clauseID = u.clauseID
        group by u.clauseID );
        """
        self.c.execute(q)
        print("goodClausesFixed added variance T: %-3.2f s" % (time.time() - t))

    def fill_var_data_use(self):
        print("Filling var data use...")

        t = time.time()
        q = "delete from `varDataUse`;"
        self.c.execute(q)
        print("varDataUse deleted T: %-3.2f s" % (time.time() - t))

        t = time.time()
        q = """
        insert into varDataUse
        select
        v.restarts
        , v.conflicts

        -- data about var
        , v.var
        , v.dec_depth
        , v.decisions_below
        , v.conflicts_below
        , v.clauses_below

        , (v.decided*1.0)/(v.sum_decisions_at_picktime*1.0)
        , (v.decided_pos*1.0)/(v.decided*1.0)
        , (v.propagated*1.0)/(v.sum_propagations_at_picktime*1.0)
        , (v.propagated_pos*1.0)/(v.propagated*1.0)

        , v.decided
        , v.decided_pos
        , v.propagated
        , v.propagated_pos

        , v.sum_decisions_at_picktime
        , v.sum_propagations_at_picktime

        , v.total_conflicts_below_when_picked
        , v.total_decisions_below_when_picked
        , v.avg_inside_per_confl_when_picked
        , v.avg_inside_antecedents_when_picked

        -- measures for good
        , count(cls.num_used) as useful_clauses
        , sum(cls.num_used) as useful_clauses_used
        , sum(cls.sum_hist_used) as useful_clauses_sum_hist_used
        , min(cls.first_confl_used) as useful_clauses_first_used
        , max(cls.last_confl_used) as useful_clauses_last_used

        FROM varData as v left join goodClausesFixed as cls
        on cls.clauseID >= v.clid_start_incl
        and cls.clauseID < v.clid_end_notincl

        -- avoid division by zero below
        where
        v.propagated > 0
        and v.sum_propagations_at_picktime > 0
        and v.decided > 0
        and v.sum_decisions_at_picktime > 0
        group by var, conflicts
        ;
        """
        if options.verbose:
            print("query:", q)
        self.c.execute(q)

        q = """
        UPDATE varDataUse SET useful_clauses_used = 0
        WHERE useful_clauses_used IS NULL
        """
        self.c.execute(q)

        q = """
        UPDATE varDataUse SET useful_clauses_first_used = 0
        WHERE useful_clauses_first_used IS NULL
        """
        self.c.execute(q)

        q = """
        UPDATE varDataUse SET useful_clauses_last_used = 0
        WHERE useful_clauses_last_used IS NULL
        """
        self.c.execute(q)

        print("varDataUse filled T: %-3.2f s" % (time.time() - t))


class QueryCls (QueryHelper):
    def __init__(self, dbfname, conf):
        super(QueryCls, self).__init__(dbfname)
        self.conf = conf

        self.goodcls = """
        , goodcl.`clauseID` as `goodcl.clauseID`
        , goodcl.`num_used` as `goodcl.num_used`
        , goodcl.`first_confl_used` as `goodcl.first_confl_used`
        , goodcl.`last_confl_used` as `goodcl.last_confl_used`
        , goodcl.`sum_hist_used` as `goodcl.sum_hist_used`
        , goodcl.`avg_hist_used` as `goodcl.avg_hist_used`
        , goodcl.`var_hist_used` as `goodcl.var_hist_used`
        , goodcl.`last_prop_used` as `goodcl.last_prop_used`
    """

        # partially done with tablestruct_sql and SED: sed -e 's/`\(.*\)`.*/rst.`\1` as `rst.\1`/' ../tmp.txt
        self.restart_dat = """
        -- , rst.`simplifications` as `rst.simplifications`
        -- , rst.`restarts` as `rst.restarts`
        -- , rst.`conflicts` as `rst.conflicts`
        -- , rst.`latest_satzilla_feature_calc` as `rst.latest_satzilla_feature_calc`
        -- rst.`runtime` as `rst.runtime`
        , rst.`numIrredBins` as `rst.numIrredBins`
        , rst.`numIrredLongs` as `rst.numIrredLongs`
        , rst.`numRedBins` as `rst.numRedBins`
        , rst.`numRedLongs` as `rst.numRedLongs`
        , rst.`numIrredLits` as `rst.numIrredLits`
        , rst.`numredLits` as `rst.numredLits`
        , rst.`glue` as `rst.glue`
        , rst.`glueSD` as `rst.glueSD`
        , rst.`glueMin` as `rst.glueMin`
        , rst.`glueMax` as `rst.glueMax`
        , rst.`size` as `rst.size`
        , rst.`sizeSD` as `rst.sizeSD`
        , rst.`sizeMin` as `rst.sizeMin`
        , rst.`sizeMax` as `rst.sizeMax`
        , rst.`resolutions` as `rst.resolutions`
        , rst.`resolutionsSD` as `rst.resolutionsSD`
        , rst.`resolutionsMin` as `rst.resolutionsMin`
        , rst.`resolutionsMax` as `rst.resolutionsMax`
        , rst.`branchDepth` as `rst.branchDepth`
        , rst.`branchDepthSD` as `rst.branchDepthSD`
        , rst.`branchDepthMin` as `rst.branchDepthMin`
        , rst.`branchDepthMax` as `rst.branchDepthMax`
        , rst.`branchDepthDelta` as `rst.branchDepthDelta`
        , rst.`branchDepthDeltaSD` as `rst.branchDepthDeltaSD`
        , rst.`branchDepthDeltaMin` as `rst.branchDepthDeltaMin`
        , rst.`branchDepthDeltaMax` as `rst.branchDepthDeltaMax`
        , rst.`trailDepth` as `rst.trailDepth`
        , rst.`trailDepthSD` as `rst.trailDepthSD`
        , rst.`trailDepthMin` as `rst.trailDepthMin`
        , rst.`trailDepthMax` as `rst.trailDepthMax`
        , rst.`trailDepthDelta` as `rst.trailDepthDelta`
        , rst.`trailDepthDeltaSD` as `rst.trailDepthDeltaSD`
        , rst.`trailDepthDeltaMin` as `rst.trailDepthDeltaMin`
        , rst.`trailDepthDeltaMax` as `rst.trailDepthDeltaMax`
        , rst.`propBinIrred` as `rst.propBinIrred`
        , rst.`propBinRed` as `rst.propBinRed`
        , rst.`propLongIrred` as `rst.propLongIrred`
        , rst.`propLongRed` as `rst.propLongRed`
        , rst.`conflBinIrred` as `rst.conflBinIrred`
        , rst.`conflBinRed` as `rst.conflBinRed`
        , rst.`conflLongIrred` as `rst.conflLongIrred`
        , rst.`conflLongRed` as `rst.conflLongRed`
        , rst.`learntUnits` as `rst.learntUnits`
        , rst.`learntBins` as `rst.learntBins`
        , rst.`learntLongs` as `rst.learntLongs`
        , rst.`resolBinIrred` as `rst.resolBinIrred`
        , rst.`resolBinRed` as `rst.resolBinRed`
        , rst.`resolLIrred` as `rst.resolLIrred`
        , rst.`resolLRed` as `rst.resolLRed`
        -- , rst.`propagations` as `rst.propagations`
        -- , rst.`decisions` as `rst.decisions`
        -- , rst.`flipped` as `rst.flipped`
        , rst.`varSetPos` as `rst.varSetPos`
        , rst.`varSetNeg` as `rst.varSetNeg`
        , rst.`free` as `rst.free`
        -- , rst.`replaced` as `rst.replaced`
        -- , rst.`eliminated` as `rst.eliminated`
        -- , rst.`set` as `rst.set`
        -- , rst.`clauseIDstartInclusive` as `rst.clauseIDstartInclusive`
        -- , rst.`clauseIDendExclusive` as `rst.clauseIDendExclusive`
        """

        self.rdb0_dat = """
        -- , rdb0.`simplifications` as `rdb0.simplifications`
        -- , rdb0.`restarts` as `rdb0.restarts`
        , rdb0.`conflicts` as `rdb0.conflicts`
        , rdb0.`cur_restart_type` as `rdb0.cur_restart_type`
        -- , rdb0.`runtime` as `rdb0.runtime`

        -- , rdb0.`clauseID` as `rdb0.clauseID`
        , rdb0.`dump_no` as `rdb0.dump_no`
        , rdb0.`conflicts_made` as `rdb0.conflicts_made`
        , rdb0.`sum_of_branch_depth_conflict` as `rdb0.sum_of_branch_depth_conflict`
        , rdb0.`propagations_made` as `rdb0.propagations_made`
        , rdb0.`clause_looked_at` as `rdb0.clause_looked_at`
        , rdb0.`used_for_uip_creation` as `rdb0.used_for_uip_creation`
        , rdb0.`last_touched_diff` as `rdb0.last_touched_diff`
        , rdb0.`activity_rel` as `rdb0.activity_rel`
        , rdb0.`locked` as `rdb0.locked`
        , rdb0.`in_xor` as `rdb0.in_xor`
        -- , rdb0.`glue` as `rdb0.glue`
        -- , rdb0.`size` as `rdb0.size`
        , rdb0.`ttl` as `rdb0.ttl`
        , rdb0.`act_ranking_top_10` as `rdb0.act_ranking_top_10`
        , rdb0.`act_ranking` as `rdb0.act_ranking`
        , rdb0.`sum_uip1_used` as `rdb0.sum_uip1_used`
        , rdb0.`sum_delta_confl_uip1_used` as `rdb0.sum_delta_confl_uip1_used`
        """

        self.clause_dat = """
        -- , cl.`simplifications` as `cl.simplifications`
        -- , cl.`restarts` as `cl.restarts`
        -- , cl.`prev_restart` as `cl.prev_restart`
        , cl.`conflicts` as `cl.conflicts`
        -- , cl.`latest_satzilla_feature_calc` as `cl.latest_satzilla_feature_calc`
        -- , cl.`clauseID` as `cl.clauseID`
        , cl.`glue` as `cl.glue`
        , cl.`size` as `cl.size`
        , cl.`conflicts_this_restart` as `cl.conflicts_this_restart`
        , cl.`num_overlap_literals` as `cl.num_overlap_literals`
        , cl.`num_antecedents` as `cl.num_antecedents`
        , cl.`num_total_lits_antecedents` as `cl.num_total_lits_antecedents`
        , cl.`antecedents_avg_size` as `cl.antecedents_avg_size`
        , cl.`backtrack_level` as `cl.backtrack_level`
        , cl.`decision_level` as `cl.decision_level`
        , cl.`decision_level_pre1` as `cl.decision_level_pre1`
        , cl.`decision_level_pre2` as `cl.decision_level_pre2`
        , cl.`trail_depth_level` as `cl.trail_depth_level`
        , cl.`cur_restart_type` as `cl.cur_restart_type`
        , cl.`atedecents_binIrred` as `cl.atedecents_binIrred`
        , cl.`atedecents_binRed` as `cl.atedecents_binRed`
        , cl.`atedecents_longIrred` as `cl.atedecents_longIrred`
        , cl.`atedecents_longRed` as `cl.atedecents_longRed`
        -- , cl.`vsids_vars_avg` as `cl.vsids_vars_avg`
        -- , cl.`vsids_vars_var` as `cl.vsids_vars_var`
        -- , cl.`vsids_vars_min` as `cl.vsids_vars_min`
        -- , cl.`vsids_vars_max` as `cl.vsids_vars_max`
        , cl.`antecedents_glue_long_reds_avg` as `cl.antecedents_glue_long_reds_avg`
        , cl.`antecedents_glue_long_reds_var` as `cl.antecedents_glue_long_reds_var`
        , cl.`antecedents_glue_long_reds_min` as `cl.antecedents_glue_long_reds_min`
        , cl.`antecedents_glue_long_reds_max` as `cl.antecedents_glue_long_reds_max`
        , cl.`antecedents_long_red_age_avg` as `cl.antecedents_long_red_age_avg`
        , cl.`antecedents_long_red_age_var` as `cl.antecedents_long_red_age_var`
        , cl.`antecedents_long_red_age_min` as `cl.antecedents_long_red_age_min`
        , cl.`antecedents_long_red_age_max` as `cl.antecedents_long_red_age_max`
        -- , cl.`vsids_of_resolving_literals_avg` as `cl.vsids_of_resolving_literals_avg`
        -- , cl.`vsids_of_resolving_literals_var` as `cl.vsids_of_resolving_literals_var`
        -- , cl.`vsids_of_resolving_literals_min` as `cl.vsids_of_resolving_literals_min`
        -- , cl.`vsids_of_resolving_literals_max` as `cl.vsids_of_resolving_literals_max`
        -- , cl.`vsids_of_all_incoming_lits_avg` as `cl.vsids_of_all_incoming_lits_avg`
        -- , cl.`vsids_of_all_incoming_lits_var` as `cl.vsids_of_all_incoming_lits_var`
        -- , cl.`vsids_of_all_incoming_lits_min` as `cl.vsids_of_all_incoming_lits_min`
        -- , cl.`vsids_of_all_incoming_lits_max` as `cl.vsids_of_all_incoming_lits_max`
        -- , cl.`antecedents_antecedents_vsids_avg` as `cl.antecedents_antecedents_vsids_avg`
        , cl.`decision_level_hist` as `cl.decision_level_hist`
        , cl.`backtrack_level_hist_lt` as `cl.backtrack_level_hist_lt`
        , cl.`trail_depth_level_hist` as `cl.trail_depth_level_hist`
        -- , cl.`vsids_vars_hist` as `cl.vsids_vars_hist`
        , cl.`size_hist` as `cl.size_hist`
        , cl.`glue_hist` as `cl.glue_hist`
        , cl.`num_antecedents_hist` as `cl.num_antecedents_hist`
        , cl.`antec_sum_size_hist` as `cl.antec_sum_size_hist`
        , cl.`antec_overlap_hist` as `cl.antec_overlap_hist`

        , cl.`branch_depth_hist_queue` as `cl.branch_depth_hist_queue`
        , cl.`trail_depth_hist` as `cl.trail_depth_hist`
        , cl.`trail_depth_hist_longer` as `cl.trail_depth_hist_longer`
        , cl.`num_resolutions_hist` as `cl.num_resolutions_hist`
        , cl.`confl_size_hist` as `cl.confl_size_hist`
        , cl.`trail_depth_delta_hist` as `cl.trail_depth_delta_hist`
        , cl.`backtrack_level_hist` as `cl.backtrack_level_hist`
        , cl.`glue_hist_queue` as `cl.glue_hist_queue`
        , cl.`glue_hist_long` as `cl.glue_hist_long`
        """

        self.satzfeat_dat = """
        -- , szfeat.`simplifications` as `szfeat.simplifications`
        -- , szfeat.`restarts` as `szfeat.restarts`
        , szfeat.`conflicts` as `szfeat.conflicts`
        -- , szfeat.`latest_satzilla_feature_calc` as `szfeat.latest_satzilla_feature_calc`
        , szfeat.`numVars` as `szfeat.numVars`
        , szfeat.`numClauses` as `szfeat.numClauses`
        , szfeat.`var_cl_ratio` as `szfeat.var_cl_ratio`
        , szfeat.`binary` as `szfeat.binary`
        , szfeat.`horn` as `szfeat.horn`
        , szfeat.`horn_mean` as `szfeat.horn_mean`
        , szfeat.`horn_std` as `szfeat.horn_std`
        , szfeat.`horn_min` as `szfeat.horn_min`
        , szfeat.`horn_max` as `szfeat.horn_max`
        , szfeat.`horn_spread` as `szfeat.horn_spread`
        , szfeat.`vcg_var_mean` as `szfeat.vcg_var_mean`
        , szfeat.`vcg_var_std` as `szfeat.vcg_var_std`
        , szfeat.`vcg_var_min` as `szfeat.vcg_var_min`
        , szfeat.`vcg_var_max` as `szfeat.vcg_var_max`
        , szfeat.`vcg_var_spread` as `szfeat.vcg_var_spread`
        , szfeat.`vcg_cls_mean` as `szfeat.vcg_cls_mean`
        , szfeat.`vcg_cls_std` as `szfeat.vcg_cls_std`
        , szfeat.`vcg_cls_min` as `szfeat.vcg_cls_min`
        , szfeat.`vcg_cls_max` as `szfeat.vcg_cls_max`
        , szfeat.`vcg_cls_spread` as `szfeat.vcg_cls_spread`
        , szfeat.`pnr_var_mean` as `szfeat.pnr_var_mean`
        , szfeat.`pnr_var_std` as `szfeat.pnr_var_std`
        , szfeat.`pnr_var_min` as `szfeat.pnr_var_min`
        , szfeat.`pnr_var_max` as `szfeat.pnr_var_max`
        , szfeat.`pnr_var_spread` as `szfeat.pnr_var_spread`
        , szfeat.`pnr_cls_mean` as `szfeat.pnr_cls_mean`
        , szfeat.`pnr_cls_std` as `szfeat.pnr_cls_std`
        , szfeat.`pnr_cls_min` as `szfeat.pnr_cls_min`
        , szfeat.`pnr_cls_max` as `szfeat.pnr_cls_max`
        , szfeat.`pnr_cls_spread` as `szfeat.pnr_cls_spread`
        , szfeat.`avg_confl_size` as `szfeat.avg_confl_size`
        , szfeat.`confl_size_min` as `szfeat.confl_size_min`
        , szfeat.`confl_size_max` as `szfeat.confl_size_max`
        , szfeat.`avg_confl_glue` as `szfeat.avg_confl_glue`
        , szfeat.`confl_glue_min` as `szfeat.confl_glue_min`
        , szfeat.`confl_glue_max` as `szfeat.confl_glue_max`
        , szfeat.`avg_num_resolutions` as `szfeat.avg_num_resolutions`
        , szfeat.`num_resolutions_min` as `szfeat.num_resolutions_min`
        , szfeat.`num_resolutions_max` as `szfeat.num_resolutions_max`
        , szfeat.`learnt_bins_per_confl` as `szfeat.learnt_bins_per_confl`
        , szfeat.`avg_branch_depth` as `szfeat.avg_branch_depth`
        , szfeat.`branch_depth_min` as `szfeat.branch_depth_min`
        , szfeat.`branch_depth_max` as `szfeat.branch_depth_max`
        , szfeat.`avg_trail_depth_delta` as `szfeat.avg_trail_depth_delta`
        , szfeat.`trail_depth_delta_min` as `szfeat.trail_depth_delta_min`
        , szfeat.`trail_depth_delta_max` as `szfeat.trail_depth_delta_max`
        , szfeat.`avg_branch_depth_delta` as `szfeat.avg_branch_depth_delta`
        , szfeat.`props_per_confl` as `szfeat.props_per_confl`
        , szfeat.`confl_per_restart` as `szfeat.confl_per_restart`
        , szfeat.`decisions_per_conflict` as `szfeat.decisions_per_conflict`
        , szfeat.`red_glue_distr_mean` as `szfeat.red_glue_distr_mean`
        , szfeat.`red_glue_distr_var` as `szfeat.red_glue_distr_var`
        , szfeat.`red_size_distr_mean` as `szfeat.red_size_distr_mean`
        , szfeat.`red_size_distr_var` as `szfeat.red_size_distr_var`
        -- , szfeat.`red_activity_distr_mean` as `szfeat.red_activity_distr_mean`
        -- , szfeat.`red_activity_distr_var` as `szfeat.red_activity_distr_var`
        -- , szfeat.`irred_glue_distr_mean` as `szfeat.irred_glue_distr_mean`
        -- , szfeat.`irred_glue_distr_var` as `szfeat.irred_glue_distr_var`
        , szfeat.`irred_size_distr_mean` as `szfeat.irred_size_distr_mean`
        , szfeat.`irred_size_distr_var` as `szfeat.irred_size_distr_var`
        -- , szfeat.`irred_activity_distr_mean` as `szfeat.irred_activity_distr_mean`
        -- , szfeat.`irred_activity_distr_var` as `szfeat.irred_activity_distr_var`
        """

        self.common_restrictions = """
        and cl.restarts > 1 -- to avoid history being invalid
        and szfeat.latest_satzilla_feature_calc = cl.latest_satzilla_feature_calc
        and szfeat_cur.latest_satzilla_feature_calc = rdb0.latest_satzilla_feature_calc
        and rst.restarts = cl.prev_restart
        and tags.tagname = "filename"
        """

        self.common_limits = """
        order by random()
        limit {limit}
        """

        # TODO magic queries
        if self.conf not in [1, 2, 3, 4]:
            self.case_stmt_10k = """
            CASE WHEN

            goodcl.last_confl_used > rdb0.conflicts and
            (
                -- used quite a bit over a wide area
                (goodcl.num_used > 5 and goodcl.avg_hist_used > 30000)

                -- at least let the 1st conflict be reached
                or (goodcl.first_confl_used > rdb0.conflicts
                    AND goodcl.first_confl_used < rdb0.conflicts+10000
                )
            )
            THEN "OK"
            ELSE "BAD"
            END AS `x.class`
            """
        elif self.conf == 1:
            self.case_stmt_10k = """
            CASE WHEN

            goodcl.last_confl_used > rdb0.conflicts and
            (
                -----------------
                -- LONG ---------
                -----------------
                -- used a lot over a wide range
                   (goodcl.num_used > 9 and goodcl.var_hist_used > 800000)

                -- used even more but less dispersion
                or (goodcl.num_used > 12 and goodcl.var_hist_used > 400000)

                -----------------
                -- SHORT---------
                -----------------

                -- used quite a bit, let it run in case the end is not too far
                or (goodcl.num_used > 5
                    AND goodcl.avg_hist_used > 30000
                    AND goodcl.last_confl_used < rdb0.conflicts+50000
                )

                -- at least let the 1st conflict be reached
                or (goodcl.first_confl_used > rdb0.conflicts
                    AND goodcl.first_confl_used < rdb0.conflicts+10000
                )

                -- let the last confl be reached if close by
                or (goodcl.last_confl_used < rdb0.conflicts+10000
                )
            )
            THEN "OK"
            ELSE "BAD"
            END AS `x.class`
            """
        elif self.conf == 2:
            self.case_stmt_10k = """
            CASE WHEN

            goodcl.last_confl_used > rdb0.conflicts and
            (
                -----------------
                -- SHORT---------
                -----------------

                -- used quite a bit, let it run in case the end is not too far
                   (goodcl.num_used > 5
                    AND goodcl.avg_hist_used > 30000
                )

                -- at least let the 1st conflict be reached
                or (goodcl.first_confl_used > rdb0.conflicts
                    AND goodcl.first_confl_used-rdb0.conflicts < 10000
                )

                -- let the last confl be reached if close by
                or (goodcl.last_confl_used < rdb0.conflicts+10000
                )
            )
            THEN "OK"
            ELSE "BAD"
            END AS `x.class`
            """
        elif self.conf == 3:
            self.case_stmt_10k = """
            CASE WHEN

            goodcl.last_confl_used > rdb0.conflicts and
            (
                -----------------
                -- LONG ---------
                -----------------
                -- used a lot over a wide range
                   (goodcl.num_used > 9 and goodcl.var_hist_used > 800000)

                -- used even more but less dispersion
                or (goodcl.num_used > 12 and goodcl.var_hist_used > 400000)

                -----------------
                -- SHORT---------
                -----------------
                -- used quite a bit, let it run in case the end is not too far
                or (goodcl.num_used > 5
                    AND goodcl.avg_hist_used > 30000
                )

                -- at least let the 1st conflict be reached
                or (goodcl.first_confl_used > rdb0.conflicts
                    AND goodcl.first_confl_used < rdb0.conflicts+20000
                )

                -- let the last confl be reached if close by
                or (goodcl.last_confl_used < rdb0.conflicts+20000
                )
            )
            THEN "OK"
            ELSE "BAD"
            END AS `x.class`"""
        elif self.conf == 4:
            self.case_stmt_10k = """
            CASE WHEN

            goodcl.last_confl_used > rdb0.conflicts and
            (
                -----------------
                -- SHORT---------
                -----------------

                -- used quite a bit, let it run in case the end is not too far
                   (goodcl.num_used > 10
                    AND goodcl.avg_hist_used > 40000
                )

                -- at least let the 1st conflict be reached
                or (goodcl.first_confl_used > rdb0.conflicts
                    AND goodcl.first_confl_used < rdb0.conflicts+10000
                )

                -- let the last confl be reached if close by
                or (goodcl.last_confl_used < rdb0.conflicts+10000
                )
            )
            THEN "OK"
            ELSE "BAD"
            END AS `x.class`
            """
        #elif self.conf == 5:
            #self.case_stmt_10k = """
            #CASE WHEN

            #goodcl.last_confl_used > rdb0.conflicts
            #THEN "OK"
            #ELSE "BAD"
            #END AS `x.class`
            #"""

        if self.conf not in []:
            self.case_stmt_100k = """
            CASE WHEN

            goodcl.last_confl_used > rdb0.conflicts+100000 and
            (
                -- used a lot over a wide range
                   (goodcl.num_used > 9 and goodcl.var_hist_used > 800000)

                -- used quite a bit but less dispersion
                or (goodcl.num_used > 12 and goodcl.var_hist_used > 400000)
            )
            THEN "OK"
            ELSE "BAD"
            END AS `x.class`
            """
        #elif self.conf == 5:
            #self.case_stmt_100k = """
            #CASE WHEN

            #goodcl.last_confl_used > rdb0.conflicts+100000
            #THEN "OK"
            #ELSE "BAD"
            #END AS `x.class`
            #"""

        self.dump_no_is_zero = """
        and rdb0.dump_no = 0
        """

        self.dump_no_larger_than_zero = """
        and rdb0.dump_no > 0
        """

        self.q_count = """
        SELECT count(*) as count,
        {case_stmt}
        """

        self.q_ok_select = """
        SELECT
        tags.tag as `fname`
        {clause_dat}
        {restart_dat}
        {satzfeat_dat_cur}
        {rdb0_dat}
        {goodcls}
        , goodcl.num_used as `x.num_used`
        , `goodcl`.`last_confl_used`-`cl`.`conflicts` as `x.lifetime`
        , {case_stmt}
        """

        self.q_ok = """
        FROM
        clauseStats as cl
        , goodClausesFixed as goodcl
        , restart as rst
        , satzilla_features as szfeat
        , satzilla_features as szfeat_cur
        , reduceDB as rdb0
        , tags
        WHERE

        cl.clauseID = goodcl.clauseID
        and cl.clauseID != 0
        and rdb0.clauseID = cl.clauseID
        """
        self.q_ok += self.common_restrictions

        # BAD caluses
        self.q_bad_select = """
        SELECT
        tags.tag as "fname"
        {clause_dat}
        {restart_dat}
        {satzfeat_dat_cur}
        {rdb0_dat}
        {goodcls}
        , 0 as `x.num_used`
        , 0 as `x.lifetime`
        , "BAD" as `x.class`
        """

        self.q_bad = """
        FROM clauseStats as cl left join goodClausesFixed as goodcl
        on cl.clauseID = goodcl.clauseID
        , restart as rst
        , satzilla_features as szfeat
        , satzilla_features as szfeat_cur
        , reduceDB as rdb0
        , tags
        WHERE

        goodcl.clauseID is NULL
        and cl.clauseID != 0
        and cl.clauseID is not NULL
        and rdb0.clauseID = cl.clauseID
        """
        self.q_bad += self.common_restrictions

        self.myformat = {
            "limit": 1000*1000*1000,
            "restart_dat": self.restart_dat,
            "clause_dat": self.clause_dat,
            "satzfeat_dat_cur": self.satzfeat_dat.replace("szfeat.", "szfeat_cur."),
            "rdb0_dat": self.rdb0_dat,
            "goodcls": self.goodcls
            }

    def add_dump_no_filter(self, q, dump_no_is_zero):
        if dump_no_is_zero:
            q += self.dump_no_is_zero
        else:
            q += self.dump_no_larger_than_zero

        return q

    def get_ok(self, subfilter, dump_no_is_zero):
        t = time.time()

        # calc OK -> which can be both BAD and OK
        q = self.q_count + self.q_ok + " and `x.class` == '%s'" % subfilter
        q = self.add_dump_no_filter(q, dump_no_is_zero)
        q = q.format(**self.myformat)
        if options.verbose:
            print("query:\n %s" % q)
        cur = self.conn.execute(q.format(**self.myformat))
        num_lines = int(cur.fetchone()[0])
        print("Num datapoints OK-%s (K): %-3.5f T: %-3.1f" % (
            subfilter, (num_lines/1000.0), time.time()-t))
        return num_lines

    def get_bad(self, dump_no_is_zero):
        t = time.time()
        q = self.q_count + self.q_bad
        q = self.add_dump_no_filter(q, dump_no_is_zero)
        q = q.format(**self.myformat)
        if options.verbose:
            print("query:\n %s" % q)
        cur = self.conn.execute(q.format(**self.myformat))
        num_lines = int(cur.fetchone()[0])
        print("Num datpoints BAD-BAD (K): %-3.5f T: %-3.1f" % (
            (num_lines/1000.0), time.time()-t))
        return num_lines

    def one_query(self, name, q):
        q = q.format(**self.myformat)
        t = time.time()
        sys.stdout.write("Running query for %s..." % name)
        sys.stdout.flush()
        if options.verbose:
            print("query:", q)
        df = pd.read_sql_query(q, self.conn)
        print("T: %-3.1f" % (time.time() - t))
        return df

    def compute_one_ok_bad_bad_data(self, long_or_short):
        df = None
        print("**Running dump_no zero")
        ok, df_dump_no_0, this_fixed = self.get_ok_bad_bad_data(
            long_or_short, dump_no_is_zero=True)
        if not ok:
            return False, None
        print("**Running dump_no non-zero")
        # TODO magic number -- multiplier for non-zero dump_no
        ok, df_dump_no_not0, _ = self.get_ok_bad_bad_data(
            long_or_short, dump_no_is_zero=False,
            this_fixed=int(this_fixed*0.3))
        if not ok:
            return False, None

        if options.verbose:
            print("Printing head:")
            print(df_dump_no_0.head())
            print(df_dump_no_not0.head())
            print("Print head done.")

        return True, pd.concat([df_dump_no_0, df_dump_no_not0])

    def get_ok_bad_bad_data(self, long_or_short, dump_no_is_zero, this_fixed=None):
        # TODO magic numbers
        # preferece OK-BAD
        # SHORT vs LONG data availability guess
        if long_or_short == "short":
            self.myformat["case_stmt"] = self.case_stmt_10k
            fixed_mult = 1.0

            # prefer OK by a factor of this. If < 0.5 then preferring BAD
            # 0.7 might also work?
            prefer_ok_ok = 0.4
        else:
            self.myformat["case_stmt"] = self.case_stmt_100k
            fixed_mult = 0.2

            # prefer OK by a factor of this. If < 0.5 then preferring BAD
            # 0.4 might also work
            prefer_ok_ok = 0.1

        print("Distrib OK vs BAD set to %s " % prefer_ok_ok)
        print("Fixed multiplier set to  %s " % fixed_mult)

        t = time.time()

        num_lines_ok = 0
        num_lines_bad = 0

        num_lines_ok_ok = self.get_ok("OK", dump_no_is_zero)
        num_lines_ok_bad = self.get_ok("BAD", dump_no_is_zero)
        num_lines_bad_bad = self.get_bad(dump_no_is_zero)

        num_lines_bad = num_lines_ok_bad + num_lines_bad_bad
        num_lines_ok = num_lines_ok_ok

        total_lines = num_lines_ok + num_lines_bad
        print("Total number of datapoints (K): %-3.2f" % (total_lines/1000.0))

        if total_lines == 0:
            print("WARNING: Total number of datapoints is 0. Potential issues:")
            print(" --> Minimum no. conflicts set too high")
            print(" --> Less than 1 restarts were made")
            print(" --> No conflicts in SQL")
            print(" --> Something went wrong")
            return False, None

        if num_lines_ok == 0 or num_lines_bad == 0:
            print("WARNING: Either OK or BAD has 0 datapoints.")
            print("num_lines_ok: %d num_lines_bad: %d" % (num_lines_ok, num_lines_bad))
            print(" Potential issues:")
            print(" --> Minimum no. conflicts set too high")
            print(" --> Less than 1 restarts were made")
            print(" --> No conflicts in SQL")
            print(" --> Something went wrong")
            return False, None

        if this_fixed == None:
            this_fixed = options.fixed
            this_fixed *= fixed_mult
        print("this_fixed is set to:", this_fixed)
        if this_fixed > total_lines:
            print("WARNING -- Your fixed num datapoints is too high:", this_fixed)
            print("        -- We only have in total:", total_lines)
            this_fixed = int(total_lines)
            print("        -- this_fixed is now ", this_fixed)

        # checking OK-OK
        lim = this_fixed * prefer_ok_ok
        if lim > num_lines_ok_ok:
            print("WARNING -- Your fixed num datapoints is too high for OK-OK")
            print("        -- Wanted to create %d but only had %d" % (lim, num_lines_ok_ok))
            this_fixed = num_lines_ok_ok/(prefer_ok_ok)
            print("        -- this_fixed is now ", this_fixed)

        # checking OK-BAD
        lim = this_fixed * num_lines_ok_bad/float(num_lines_bad) * (1.0-prefer_ok_ok)
        if lim > num_lines_ok_bad:
            print("WARNING -- Your fixed num datapoints is too high, cannot generate OK-BAD")
            print("        -- Wanted to create %d but only had %d" % (lim, num_lines_ok_bad))
            this_fixed = num_lines_ok_bad/(num_lines_ok_bad/float(num_lines_bad) * (1.0-prefer_ok_ok))
            print("        -- this_fixed is now ", this_fixed)

        # checking BAD-BAD
        lim = this_fixed * num_lines_bad_bad/float(num_lines_bad) * (1.0-prefer_ok_ok)
        if lim > num_lines_bad_bad:
            print("WARNING -- Your fixed num datapoints is too high, cannot generate BAD-BAD")
            print("        -- Wanted to create %d but only had %d" % (lim, num_lines_bad_bad))
            this_fixed = num_lines_bad_bad/(num_lines_bad_bad/float(num_lines_bad) * (1.0-prefer_ok_ok))
            print("        -- this_fixed is now ", this_fixed)

        # OK-OK
        q = self.q_ok_select + self.q_ok + " and `x.class` == 'OK'"
        q = self.add_dump_no_filter(q, dump_no_is_zero)
        self.myformat["limit"] = int(this_fixed * prefer_ok_ok)
        q += self.common_limits
        print("limit for OK-OK:", self.myformat["limit"])
        df_ok_ok = self.one_query("OK-OK", q)

        # OK-BAD
        q = self.q_ok_select + self.q_ok + " and `x.class` == 'BAD'"
        q = self.add_dump_no_filter(q, dump_no_is_zero)
        self.myformat["limit"] = int(this_fixed * num_lines_ok_bad/float(num_lines_bad) * (1.0-prefer_ok_ok))
        q += self.common_limits
        print("limit for OK-BAD:", self.myformat["limit"])
        df_ok_bad = self.one_query("OK-BAD", q)

        # BAD-BAD
        q = self.q_bad_select + self.q_bad
        q = self.add_dump_no_filter(q, dump_no_is_zero)
        self.myformat["limit"] = int(this_fixed * num_lines_bad_bad/float(num_lines_bad) * (1.0-prefer_ok_ok))
        q += self.common_limits
        print("limit for bad:", self.myformat["limit"])
        df_bad_bad = self.one_query("BAD-BAD", q)

        # finish up
        print("Queries finished. T: %-3.2f" % (time.time() - t))
        return True, pd.concat([df_ok_ok, df_ok_bad, df_bad_bad]), this_fixed

class QueryVar (QueryHelper):
    def __init__(self, dbfname):
        super(QueryVar, self).__init__(dbfname)

    def vardata(self):
        q = """
select
*
, (1.0*useful_clauses)/(1.0*clauses_below) as useful_ratio

, CASE WHEN
 (1.0*useful_clauses)/(1.0*clauses_below) > 0.5
THEN "OK"
ELSE "BAD"
END AS `class`

from varDataUse
where
clauses_below > 10
and avg_inside_per_confl_when_picked > 0
"""

        df = pd.read_sql_query(q, self.conn)

        cleanname = re.sub(r'\.cnf.gz.sqlite$', '', dbfname)
        cleanname = re.sub(r'\.db$', '', dbfname)
        cleanname += "-vardata"
        dump_dataframe(df, cleanname)


def transform(df):
    def check_clstat_row(self, row):
        if row[self.ntoc["cl.decision_level_hist"]] == 0 or \
                row[self.ntoc["cl.backtrack_level_hist"]] == 0 or \
                row[self.ntoc["cl.trail_depth_level_hist"]] == 0 or \
                row[self.ntoc["cl.vsids_vars_hist"]] == 0 or \
                row[self.ntoc["cl.size_hist"]] == 0 or \
                row[self.ntoc["cl.glue_hist"]] == 0 or \
                row[self.ntoc["cl.num_antecedents_hist"]] == 0:
            print("ERROR: Data is in error:", row)
            assert(False)
            exit(-1)

        return row

    # relative overlaps
    print("Relative overlaps...")
    df["cl.num_overlap_literals_rel"] = df["cl.num_overlap_literals"]/df["cl.antec_overlap_hist"]
    df["cl.antec_num_total_lits_rel"] = df["cl.num_total_lits_antecedents"]/df["cl.antec_sum_size_hist"]
    df["cl.num_antecedents_rel"] = df["cl.num_antecedents"]/df["cl.num_antecedents_hist"]
    df["rst.varset_neg_polar_ratio"] = df["rst.varSetNeg"]/(df["rst.varSetPos"]+df["rst.varSetNeg"])

    # relative RDB
    #print("Relative RDB...")
    #df["rdb.rel_conflicts_made"] = (df["rdb0.conflicts_made"] > df["rdb1.conflicts_made"]).astype(int)
    #df["rdb.rel_propagations_made"] = (df["rdb0.propagations_made"] > df["rdb1.propagations_made"]).astype(int)
    #df["rdb.rel_clause_looked_at"] = (df["rdb0.clause_looked_at"] > df["rdb1.clause_looked_at"]).astype(int)
    #df["rdb.rel_used_for_uip_creation"] = (df["rdb0.used_for_uip_creation"] > df["rdb1.used_for_uip_creation"]).astype(int)
    #df["rdb.rel_last_touched_diff"] = (df["rdb0.last_touched_diff"] > df["rdb1.last_touched_diff"]).astype(int)
    #df["rdb.rel_activity_rel"] = (df["rdb0.activity_rel"] > df["rdb1.activity_rel"]).astype(int)

    # ************
    # TODO decision level and branch depth are the same, right???
    # ************
    print("size/glue/trail rel...")
    df["cl.size_rel"] = df["cl.size"] / df["cl.size_hist"]
    df["cl.glue_rel_queue"] = df["cl.glue"] / df["cl.glue_hist_queue"]
    df["cl.glue_rel_long"] = df["cl.glue"] / df["cl.glue_hist_long"]
    df["cl.glue_rel"] = df["cl.glue"] / df["cl.glue_hist"]
    df["cl.trail_depth_level_rel"] = df["cl.trail_depth_level"]/df["cl.trail_depth_level_hist"]
    df["cl.branch_depth_rel_queue"] = df["cl.decision_level"]/df["cl.branch_depth_hist_queue"]

    # smaller-than larger-than for glue and size
    print("smaller-than larger-than for glue and size...")
    df["cl.size_smaller_than_hist"] = (df["cl.size"] < df["cl.size_hist"]).astype(int)
    df["cl.glue_smaller_than_hist"] = (df["cl.glue"] < df["cl.glue_hist"]).astype(int)
    df["cl.glue_smaller_than_hist_lt"] = (df["cl.glue"] < df["cl.glue_hist_long"]).astype(int)
    df["cl.glue_smaller_than_hist_queue"] = (df["cl.glue"] < df["cl.glue_hist_queue"]).astype(int)

    # relative decisions
    print("relative decisions...")
    df["cl.decision_level_rel"] = df["cl.decision_level"]/df["cl.decision_level_hist"]
    df["cl.decision_level_pre1_rel"] = df["cl.decision_level_pre1"]/df["cl.decision_level_hist"]
    df["cl.decision_level_pre2_rel"] = df["cl.decision_level_pre2"]/df["cl.decision_level_hist"]
    df["cl.decision_level_pre2_rel"] = df["cl.decision_level_pre2"]/df["cl.decision_level_hist"]
    df["cl.decision_level_pre2_rel"] = df["cl.decision_level_pre2"]/df["cl.decision_level_hist"]
    df["cl.backtrack_level_rel"] = df["cl.backtrack_level"]/df["cl.decision_level_hist"]

    # relative props
    print("relative props...")
    df["rst.all_props"] = df["rst.propBinRed"] + df["rst.propBinIrred"] + df["rst.propLongRed"] + df["rst.propLongIrred"]
    df["rst.propBinRed_ratio"] = df["rst.propBinRed"]/df["rst.all_props"]
    df["rst.propBinIrred_ratio"] = df["rst.propBinIrred"]/df["rst.all_props"]
    df["rst.propLongRed_ratio"] = df["rst.propLongRed"]/df["rst.all_props"]
    df["rst.propLongIrred_ratio"] = df["rst.propLongIrred"]/df["rst.all_props"]

    df["cl.trail_depth_level_rel"] = df["cl.trail_depth_level"]/df["rst.free"]

    # relative resolutions
    print("relative resolutions...")
    df["rst.resolBinIrred_ratio"] = df["rst.resolBinIrred"]/df["rst.resolutions"]
    df["rst.resolBinRed_ratio"] = df["rst.resolBinRed"]/df["rst.resolutions"]
    df["rst.resolLRed_ratio"] = df["rst.resolLRed"]/df["rst.resolutions"]
    df["rst.resolLIrred_ratio"] = df["rst.resolLIrred"]/df["rst.resolutions"]

    df["cl.num_antecedents_rel"] = df["cl.num_antecedents"] / df["cl.num_antecedents_hist"]
    df["cl.decision_level_rel"] = df["cl.decision_level"] / df["cl.decision_level_hist"]
    df["cl.trail_depth_level_rel"] = df["cl.trail_depth_level"] / df["cl.trail_depth_level_hist"]
    df["cl.backtrack_level_rel"] = df["cl.backtrack_level"] / df["cl.backtrack_level_hist"]

    # smaller-or-greater comparisons
    print("smaller-or-greater comparisons...")
    df["cl.decision_level_smaller_than_hist"] = (df["cl.decision_level"] < df["cl.decision_level_hist"]).astype(int)
    df["cl.backtrack_level_smaller_than_hist"] = (df["cl.backtrack_level"] < df["cl.backtrack_level_hist"]).astype(int)
    df["cl.trail_depth_level_smaller_than_hist"] = (df["cl.trail_depth_level"] < df["cl.trail_depth_level_hist"]).astype(int)
    df["cl.num_antecedents_smaller_than_hist"] = (df["cl.num_antecedents"] < df["cl.num_antecedents_hist"]).astype(int)
    df["cl.antec_sum_size_smaller_than_hist"] = (df["cl.antec_sum_size_hist"] < df["cl.num_total_lits_antecedents"]).astype(int)
    df["cl.antec_overlap_smaller_than_hist"] = (df["cl.antec_overlap_hist"] < df["cl.num_overlap_literals"]).astype(int)
    df["cl.overlap_smaller_than_hist"] = (df["cl.num_overlap_literals"]<df["cl.antec_overlap_hist"]).astype(int)
    df["cl.branch_smaller_than_hist_queue"] = (df["cl.decision_level"]<df["cl.branch_depth_hist_queue"]).astype(int)

    # avg conf/used_per confl
    print("avg conf/used_per confl 1...")
    df["rdb0.avg_confl"] = df["rdb0.sum_uip1_used"]/df["rdb0.sum_delta_confl_uip1_used"]
    df["rdb0.avg_confl"].fillna(0, inplace=True)

    print("avg conf/used_per confl 2...")
    df["rdb0.used_per_confl"] = df["rdb0.sum_uip1_used"]/(df["rdb0.conflicts"] - df["cl.conflicts"])
    df["rdb0.used_per_confl"].fillna(0, inplace=True)

    print("flatten/list...")
    old = set(df.columns.values.flatten().tolist())
    df = df.dropna(how="all")
    new = set(df.columns.values.flatten().tolist())
    if len(old - new) > 0:
        print("ERROR: a NaN number turned up")
        print("columns: ", (old - new))
        assert(False)
        exit(-1)

    # making sure "x.class" is the last one
    new_no_class = list(new)
    new_no_class.remove("x.class")
    df = df[new_no_class + ["x.class"]]

    return df


def dump_dataframe(df, name):
    if options.dump_csv:
        fname = "%s.csv" % name
        print("Dumping CSV data to:", fname)
        df.to_csv(fname, index=False, columns=sorted(list(df)))

    fname = "%s.dat" % name
    print("Dumping pandas data to:", fname)
    with open(fname, "wb") as f:
        pickle.dump(df, f)


def one_database(dbfname):
    with QueryFill(dbfname) as q:
        if not options.no_recreate_indexes:
            q.create_indexes()
            q.fill_last_prop()
            q.fill_good_clauses_fixed()
            q.fill_var_data_use()

    # with QueryVar(dbfname) as q:
    #    q.vardata()

    match = re.match(r"^([0-9]*)-([0-9]*)$", options.confs)
    if not match:
        print("ERROR: we cannot parse your config options: '%s'")
        exit(-1)

    conf_from = int(match.group(1))
    conf_to = int(match.group(2))+1
    print("Running configs:", range(conf_from, conf_to))
    print("Using sqlite3db file %s" % dbfname)
    for long_or_short in ["long", "short"]:
        for conf in range(conf_from, conf_to):
            print("------> Doing config {conf}".format(conf=conf))
            with QueryCls(dbfname, conf) as q:
                ok, df = q.compute_one_ok_bad_bad_data(long_or_short)

            if not ok:
                print("-> Skipping file {file} config {conf} {ls}".format(
                    conf=conf, file=dbfname, ls=long_or_short))
                continue

            if options.verbose:
                print("Describing----")
                dat = df.describe()
                print(dat)
                print("Describe done.---")
                print("Features: ", df.columns.values.flatten().tolist())

            print("Transforming...")
            df = transform(df)

            if options.verbose:
                print("Describing post-transform ----")
                print(df.describe())
                print("Describe done.---")
                print("Features: ", df.columns.values.flatten().tolist())

            cleanname = re.sub(r'\.cnf.gz.sqlite$', '', dbfname)
            cleanname = re.sub(r'\.db$', '', dbfname)
            cleanname = re.sub(r'\.sqlitedb$', '', dbfname)
            cleanname = "{cleanname}-{long_or_short}-conf-{conf}".format(
                cleanname=cleanname,
                long_or_short=long_or_short,
                conf=conf)

            dump_dataframe(df, cleanname)


if __name__ == "__main__":
    usage = "usage: %prog [options] file1.sqlite [file2.sqlite ...]"
    parser = optparse.OptionParser(usage=usage)

    parser.add_option("--verbose", "-v", action="store_true", default=False,
                      dest="verbose", help="Print more output")

    parser.add_option("--csv", action="store_true", default=False,
                      dest="dump_csv", help="Dump CSV (for weka)")

    parser.add_option("--sql", action="store_true", default=False,
                      dest="dump_sql", help="Dump SQL queries")

    parser.add_option("--fixed", default=5000, type=int,
                      dest="fixed", help="Exact number of examples to take. -1 is to take all. Default: %default")

    parser.add_option("--noind", action="store_true", default=False,
                      dest="no_recreate_indexes",
                      help="Don't recreate indexes")

    parser.add_option("--confs", default="0-5", type=str,
                      dest="confs", help="Configs to generate. Default: %default")

    (options, args) = parser.parse_args()

    if len(args) < 1:
        print("ERROR: You must give at least one file")
        exit(-1)

    np.random.seed(2097483)
    for dbfname in args:
        print("----- FILE %s -------" % dbfname)
        one_database(dbfname)
