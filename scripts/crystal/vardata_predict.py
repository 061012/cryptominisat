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
import sklearn.ensemble
import sklearn.tree
import sqlite3
import argparse
import time
import pickle
import re
import pandas as pd
import numpy as np
import os.path
import sys
import helper
import sklearn
ver = sklearn.__version__.split(".")
if int(ver[1]) < 20:
    from sklearn.cross_validation import train_test_split
else:
    from sklearn.model_selection import train_test_split


def divide(col, name, df, cols):
    # cannot divide by it, feature not present
    if name not in cols:
        return

    # divide
    if options.verbose:
        print("dividing col '%s' with '%s' " % (col, name))
    df["(" + col + "/" + name + ")"] = df[col].div(df[name]+0.000000001)


def add_computed_features(df):
    print("Relative data...")

    # remove hidden data
    del df["sum_cl_use.num_used"]

    cols = list(df)
    if not options.picktime_only:
        # create "during"
        for col in cols:
            if "_at_fintime" in col:
                during_name = col.replace("_at_fintime", "_during")
                at_picktime_name = col.replace("_at_fintime", "_at_picktime")
                at_fintime_name = col
                if options.verbose:
                    print("fintime name: ", at_fintime_name)
                    print("picktime name: ", at_picktime_name)
                df[during_name] = df[at_fintime_name]-df[at_picktime_name]

        # remove picktime & fintime, only use "during"
        cols = list(df)
        for c in cols:
            if "at_picktime" in c or "at_fintime" in c:
                del df[c]

    else:
        # remove everything to do with "during" and "fintime"
        cols = list(df)
        for c in cols:
            if "clauses_below" in c or "at_fintime" in c:
                del df[c]

    if False:
        # per-conflicts, per-decisions, per-lits
        names = [
            "var_data_picktime.sumConflicts_at_picktime"
            , "var_data_picktime.sumClLBD_at_picktime"
            , "var_data_picktime.sumClSize_at_picktime"
            , "var_data_picktime.sumConflictClauseLits_at_picktime"
            # neutral below
            , "var_data_picktime.dec_depth"
            # below during
            , "var_data_picktime.inside_conflict_clause_antecedents_at_picktime"
            , "var_data_picktime.sumDecisions_below_during"
            , "var_data_picktime.sumPropagations_below_during"
            , "var_data_picktime.sumConflicts_below_during"
            , "var_data_picktime.sumAntecedents_below_during"
            , "var_data_picktime.sumConflictClauseLits_below_during"
            , "var_data_picktime.sumAntecedentsLits_below_during"
            , "var_data_picktime.sumClSize_below_during"
            , "var_data_picktime.sumClLBD_below_during"
            ]

        cols = list(df)
        for col in cols:
            if "restart_type" not in col and "x." not in col and "var_data_use" not in col and "rst.random_var_freq" not in col:
                for name in names:
                    divide(col, name, df, cols)

    # divide var_dist by szfeat, all-by-all
    if False:
        for c in cols:
            if "szfeat" in c:
                for c2 in cols:
                    if "var_dist" in c2:
                        divide(c2, c, df, cols)

    divide("var_dist.red_num_times_in_bin_clause", "rst.numRedBins", df, cols)
    divide("var_dist.red_num_times_in_long_clause", "rst.numRedLongs", df, cols)
    df["rst.redcls"] = df["rst.numRedLongs"]+df["rst.numRedBins"]
    divide("var_dist.red_satisfies_cl", "rst.redcls", df, cols)
    divide("var_dist.red_tot_num_lit_of_bin_it_appears_in", "rst.numRedBins", df, cols)
    divide("var_dist.red_tot_num_lit_of_long_cls_it_appears_in", "rst.numRedLongs", df, cols)
    divide("var_dist.red_sum_var_act_of_cls", "rst.redcls", df, cols)


    divide("var_dist.irred_num_times_in_bin_clause", "rst.numIrredBins", df, cols)
    divide("var_dist.irred_num_times_in_long_clause", "rst.numIrredLongs", df, cols)
    df["rst.irredcls"] = df["rst.numIrredLongs"]+df["rst.numIrredBins"]
    divide("var_dist.irred_satisfies_cl", "rst.irredcls", df, cols)
    divide("var_dist.irred_tot_num_lit_of_bin_it_appears_in", "rst.numIrredBins", df, cols)
    divide("var_dist.irred_tot_num_lit_of_long_cls_it_appears_in", "rst.numIrredLongs", df, cols)
    divide("var_dist.irred_sum_var_act_of_cls", "rst.irredcls", df, cols)

    divide("var_data_picktime.inside_conflict_clause_antecedents_during_at_picktime",
           "var_data_picktime.sumAntecedentsLits_at_picktime", df, cols)

    divide("var_data_picktime.sumAntecedentsLits_below_during",
           "var_data_picktime.sumAntecedentsLits_at_picktime", df, cols)

    divide("var_data_picktime.inside_conflict_clause_antecedents_at_picktime",
        "var_data_picktime.sumAntecedentsLits_at_picktime", df, cols)

    divide("var_dist.red_satisfies_cl", "rst.redcls", df, cols)
    divide("var_dist.irred_satisfies_cl", "rst.irredcls", df, cols)
    df["rst.cls"] = df["rst.irredcls"] + df["rst.redcls"]

    divide("var_data_picktime.inside_conflict_clause_during_at_picktime", "rst.cls", df, cols)

    divide("var_data_picktime.num_decided", "var_data_picktime.sumDecisions_at_picktime", df, cols)
    divide("var_data_picktime.num_decided_pos", "var_data_picktime.sumDecisions_at_picktime", df, cols)
    divide("var_data_picktime.num_decided", "var_data_picktime.num_decided_pos", df, cols)

    divide("var_data_picktime.num_propagated", "var_data_picktime.sumPropagations_at_picktime", df, cols)
    divide("var_data_picktime.num_propagated_pos", "var_data_picktime.sumPropagations_at_picktime", df, cols)
    divide("var_data_picktime.num_propagated", "var_data_picktime.num_propagated_pos", df, cols)

    del df["rst.free"]
    del df["var_data_picktime.num_decided"]
    del df["var_data_picktime.num_decided_pos"]
    del df["var_data_picktime.num_propagated"]
    del df["var_data_picktime.num_propagated_pos"]
    del df["var_dist.red_satisfies_cl"]
    del df["var_dist.irred_satisfies_cl"]
    del df["var_data_picktime.sumAntecedentsLits_below_during"]
    del df["var_data_picktime.inside_conflict_clause_antecedents_at_picktime"]
    del df["var_data_picktime.inside_conflict_clause_antecedents_during_at_picktime"]

    del df["var_data_picktime.sumPropagations_at_picktime"]
    del df["var_data_picktime.sumDecisions_at_picktime"]

    del df["var_data_picktime.inside_conflict_clause_during_at_picktime"]

    for c in cols:
        if "rst." in c:
            if c in list(df):
                del df[c]
    pass

    # del df["rst.numredLits"]


def rem_useless_features(df):
    col = list(df)
    for c in col:
        if "restart_type" in c or "szfeat" in c:
            del df[c]
    pass


class Learner:
    def __init__(self, df, funcname, fname, cluster_no):
        self.df = df
        self.func_name = funcname
        self.fname = fname
        self.cluster_no = cluster_no

    def cut_into_chunks(self):
        df["x.class"] = pd.qcut(
            df["x.num_used"],
            q=options.quantiles,
            labels=["%d" % x for x in range(options.quantiles)])

        #df["x.class"] = pd.cut(
            #df["x.num_used"],
            #bins=[-1000, 1, 5, 10, 20, 40, 100, 200, 10**20],
            ##bins = [-1000, 20, 10**20],
            #labels=["%d" % x for x in range(8)])

    @staticmethod
    def fix_feat_name(x):
        if "during" in x or "clauses_below" in x:
            x = x.replace("var_data_", "")

        return x

    def one_classifier(self, features, to_predict, final, write_code=False):
        print("-> Number of features  :", len(features))
        print("-> Number of datapoints:", self.df.shape)
        print("-> Predicting          :", to_predict)

        # get smaller part to work on
        # also, copy it so we don't get warning about setting a slice of a DF
        if options.only_pecr >= 0.98:
            df = self.df.copy()
        else:
            _, df_tmp = train_test_split(self.df, test_size=options.only_pecr)
            df = df_tmp.copy()
            print("-> Number of datapoints after applying '--only':", df.shape)

        if options.dump_csv:
            fname = "mydump.csv"
            print("Dumping CSV data to:", fname)
            df.to_csv(fname, index=False, columns=sorted(list(df)))

        if options.check_row_data:
            helper.check_too_large_or_nan_values(df, features+["x.class"])
            print("Checked, all good!")

        train, test = train_test_split(df, test_size=0.33)
        X_train = train[features]
        y_train = train[to_predict]

        t = time.time()
        clf = None
        if final:
            split_point = helper.calc_min_split_point(
                df, options.min_samples_split)
            clf = sklearn.tree.DecisionTreeClassifier(
                max_depth=options.tree_depth,
                min_samples_split=split_point)
        else:
            clf = sklearn.ensemble.RandomForestClassifier(
                n_estimators=1000,
                max_features="sqrt")

        del df
        clf.fit(X_train, y_train)
        print("Training finished. T: %-3.2f" % (time.time() - t))

        if not final:
            best_features = helper.print_feature_ranking(
                clf, X_train,
                top_num_features=options.top_num_features,
                features=features,
                plot=options.show)
        else:
            if options.dot is not None:
                if not options.final_is_tree:
                    print("ERROR: You cannot use the DOT function on non-trees")
                    exit(-1)

                helper.output_to_classical_dot(
                    clf, features,
                    fname=options.dot + "-" + self.func_name)

            if options.basedir and write_code:
                c = helper.CodeWriter(clf, features, self.func_name, self.fname, options.verbose)
                c.func_signature = """
                const Solver*    solver
                , const VarData& varData
                , uint64_t       sumConflicts_during
                , uint64_t       sumDecisions_during
                , uint64_t       sumPropagations_during
                , uint64_t       sumAntecedents_during
                , uint64_t       sumAntecedentsLits_during
                , uint64_t       sumConflictClauseLits_during
                , uint64_t       sumDecisionBasedCl_during
                , uint64_t       sumClLBD_during
                , uint64_t       sumClSize_during
                , uint64_t       clauses_below
                """
                c.func_call = """
                solver
                , varData
                , sumConflicts_during
                , sumDecisions_during
                , sumPropagations_during
                , sumAntecedents_during
                , sumAntecedentsLits_during
                , sumConflictClauseLits_during
                , sumDecisionBasedCl_during
                , sumClLBD_during
                , sumClSize_during
                , clauses_below
                """
                c.per_func_defines = """"""
                c.file_header = """
                #include "clause.h"
                #include "vardata.h"
                #include "solver.h"
                #include <cmath>

                namespace CMSat {
                """
                c.fix_feat_name = self.fix_feat_name
                c.clean_up()
                c.print_full_code()

        prec, recall, acc = helper.conf_matrixes(test, features, to_predict, clf, average="micro")
        helper.conf_matrixes(train, features, to_predict, clf, "train", average="micro")

        # TODO do L1 regularization
        # TODO do principal component analysis

        if not final:
            return best_features
        else:
            return prec+recall+acc

    def learn(self):
        self.cut_into_chunks()
        features = list(self.df)
        features.remove("clust")
        features.remove("x.class")
        features.remove("x.num_used")

        if options.raw_data_plots:
            pd.options.display.mpl_style = "default"
            self.df.hist()
            self.df.boxplot()

        if not options.only_final:
            top_n_feats = self.one_classifier(features, "x.class", final=False)
            if options.show:
                plt.show()

            if options.get_best_topn_feats is not None:
                greedy_features = helper.calc_greedy_best_features(
                    top_n_feats, options.get_best_topn_feats,
                    self)

            return

        best_features = [
            '(var_data.sumAntecedentsLits_during/var_data.sumAntecedents_during)',
            '(var_data.sumClSize_during/var_data.sumPropagations_during)',
            '(var_data.sumConflictClauseLits_during/var_data.sumAntecedents_during)',
            '(var_data.sumDecisions_during/var_data.sumAntecedentsLits_during)',
            '(var_data.sumPropagations_during/var_data.sumClLBD_during)',
            '(var_data.clauses_below/var_data.sumAntecedentsLits_during)',
            '(var_data.sumClLBD_during/var_data.sumPropagations_during)']

        best_features = ["var_data.rel_activity_at_picktime"]

        self.one_classifier(best_features, "x.class",
                            final=True,
                            write_code=True)

        if options.show:
            plt.show()


if __name__ == "__main__":
    usage = "usage: %(prog)s [options] file.sqlite"
    parser = argparse.ArgumentParser(usage=usage)

    # dataframe
    parser.add_argument("fname", type=str, metavar='PANDASFILE')
    parser.add_argument("--verbose", "-v", action="store_true", default=False,
                        dest="verbose", help="Print more output")
    parser.add_argument("--top", default=40, type=int, metavar="TOPN",
                        dest="top_num_features", help="Candidates are top N features for greedy selector")
    parser.add_argument("--printfeat", action="store_true", default=False,
                        dest="print_features", help="Print features")
    parser.add_argument("--check", action="store_true", default=False,
                        dest="check_row_data", help="Check row data for NaN or float overflow")
    parser.add_argument("--greedy", default=None, type=int, metavar="TOPN",
                        dest="get_best_topn_feats", help="Greedy Best K top features from the top N features given by '--top N'")
    parser.add_argument("--show", default=False, action="store_true",
                        dest="show", help="Show graphs")
    parser.add_argument("--final", default=False, action="store_true",
                        dest="only_final", help="Only generate final predictor")
    parser.add_argument("--rawplots", action="store_true", default=False,
                        dest="raw_data_plots", help="Display raw data plots")
    parser.add_argument("--dot", type=str, default=None,
                        dest="dot", help="Create DOT file")
    parser.add_argument("--basedir", type=str,
                        dest="basedir", help="The base directory of where the CryptoMiniSat source code is")
    parser.add_argument("--clust", default=False, action="store_true",
                        dest="use_clusters", help="Use clusters")
    parser.add_argument("--conf", default=0, type=int,
                        dest="conf_num", help="Which predict configuration this is")
    parser.add_argument("--picktimeonly", default=False, action="store_true",
                        dest="picktime_only", help="Only use and generate pictime data")

    # tree/forest options
    parser.add_argument("--depth", default=None, type=int,
                        dest="tree_depth", help="Depth of the tree to create")
    parser.add_argument("--split", default=0.01, type=float, metavar="RATIO",
                        dest="min_samples_split", help="Split in tree if this many samples or above. Used as a percentage of datapoints")
    parser.add_argument("--numtrees", default=5, type=int,
                        dest="num_trees", help="How many trees to generate for the forest")

    # data filtering
    parser.add_argument("--only", default=0.99, type=float,
                        dest="only_pecr", help="Only use this percentage of data")
    parser.add_argument("-q", default=2, type=int, metavar="QUANTS",
                        dest="quantiles", help="Number of quantiles we want")
    parser.add_argument("--nocomputed", default=False, action="store_true",
                        dest="no_computed", help="Don't add computed features")
    parser.add_argument("--csv", action="store_true", default=False,
                        dest="dump_csv", help="Dump CSV (for weka)")

    # type of classifier
    parser.add_argument("--tree", default=False, action="store_true",
                        dest="final_is_tree", help="Final classifier should be a tree")
    parser.add_argument("--svm", default=False, action="store_true",
                        dest="final_is_svm", help="Final classifier should be an svm")
    parser.add_argument("--lreg", default=False, action="store_true",
                        dest="final_is_logreg", help="Final classifier should be a logistic regression")
    parser.add_argument("--forest", default=False, action="store_true",
                        dest="final_is_forest", help="Final classifier should be a forest")
    parser.add_argument("--voting", default=False, action="store_true",
                        dest="final_is_voting", help="Final classifier should be a voting of all of: forest, svm, logreg")

    options = parser.parse_args()

    if options.fname is None:
        print("ERROR: You must give the pandas file!")
        exit(-1)

    df = pd.read_pickle(options.fname)

    rem_useless_features(df)
    if not options.no_computed:
        add_computed_features(df)

    print(list(df))

    # cluster setup
    if options.use_clusters:
        used_clusters = df.groupby("clust").nunique()
        clusters = []
        for clust, _ in used_clusters["clust"].iteritems():
            clusters.append(clust)
    else:
        clusters = [0]
        df["clust"] = 0

    # generation
    for clno in clusters:
        funcname = "maple_reward_conf{conf_num}_cluster{clno}".format(
                clno=clno, conf_num=options.conf_num)

        fname = "maple_predictor_conf{conf_num}_cluster{clno}.h".format(
            clno=clno, conf_num=options.conf_num)

        if options.basedir is not None:
            f = options.basedir+"/"+fname
        else:
            f = None

        p = Learner(
            df,
            funcname=funcname,
            fname=f,
            cluster_no=clno)
        p.learn()
