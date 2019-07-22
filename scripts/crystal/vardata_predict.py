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
import sklearn.ensemble


def add_computed_features(df):
    print("Relative data...")
    cols = list(df)
    for col in cols:
        if "_at_fintime" in col:
            during_name = col.replace("_at_fintime", "_during")
            at_picktime_name = col.replace("_at_fintime", "_at_picktime")
            if options.verbose:
                print("fintime name: ", col)
                print("picktime name: ", at_picktime_name)
            df[during_name] = df[col]-df[at_picktime_name]

    # remove stuff
    del df["var_data_use.useful_clauses_used"]
    del df["var_data_use.cls_marked"]

    # more complicated
    df["var_data.propagated_per_sumconfl"]=df["var_data.propagated"]/df["var_data.sumConflicts_at_fintime"]
    df["var_data.propagated_per_sumprop"]=df["var_data.propagated"]/df["var_data.sumPropagations_at_fintime"]

    # remove picktime & fintime
    cols = list(df)
    for c in cols:
        if "at_picktime" in c or "at_fintime" in c:
            del df[c]

    # per-conflicts, per-decisions, per-lits
    names = [
        "var_data.sumDecisions_during"
        , "var_data.sumPropagations_during"
        , "var_data.sumConflicts_during"
        , "var_data.sumAntecedents_during"
        , "var_data.sumConflictClauseLits_during"
        , "var_data.sumAntecedentsLits_during"
        , "var_data.sumClSize_during"
        , "var_data.sumClLBD_during"
        , "var_data.dec_depth"
        ]

    cols = list(df)
    for col in cols:
        if "restart_type" not in col and "x." not in col and "useful_clauses" not in col:
            for name in names:
                if options.verbose:
                    print("dividing col '%s' with '%s' " % (col, name))

                df["(" + col + "/" + name + ")"]=df[col]/df[name]
                pass

    # remove sum
    #cols = list(df)
    #for c in cols:
        #if c[0:3] == "sum":
            #del df[c]


def rem_useless_features(df):
    if True:
        # remove these
        torem = [
            "var_data.propagated"
            , "var_data.decided"
            , "var_data.clauses_below"
            , "var_data.dec_depth"
            , "var_data.sumDecisions_during"
            , "var_data.sumPropagations_during"
            , "var_data.sumConflicts_during"
            , "var_data.sumAntecedents_during"
            , "var_data.sumAntecedentsLits_during"
            , "var_data.sumConflictClauseLits_during"
            , "var_data.sumDecisionBasedCl_during"
            , "var_data.sumClLBD_during"
            , "var_data.sumClSize_during"
            ]
        cols = list(df)

        # also remove rst
        for col in cols:
            if "rst." in col:
                #torem.append(col)
                pass

        torem.append("rst.restart_type")

        # actually remove
        for x in torem:
            if x in df:
                del df[x]
    else:
        del df["rst.restart_type"]


class Predict:
    def __init__(self):
        pass

    def get_top_features(self, df):
        df["x.class"]=pd.qcut(df["x.useful_times_per_marked"],
                             q=options.quantiles,
                             labels=False)
        df["x.class"]=pd.cut(df["x.useful_times_per_marked"],
                             bins=[-1000, 1, 5, 10, 20, 40, 100, 200, 10**20],
                             labels=False)

        features = list(df)
        features.remove("x.class")
        features.remove("x.useful_times_per_marked")
        to_predict="x.class"

        if options.check_row_data:
            helper.check_too_large_or_nan_values(df, features+["x.class"])
            print("Checked, all good!")

        print("-> Number of features  :", len(features))
        print("-> Number of datapoints:", df.shape)
        print("-> Predicting          :", to_predict)

        train, test = train_test_split(df, test_size=0.33)
        X_train = train[features]
        y_train = train[to_predict]
        split_point = helper.calc_min_split_point(df, options.min_samples_split)
        clf = sklearn.ensemble.RandomForestClassifier(
                    n_estimators=1000,
                    max_features="sqrt")

        t = time.time()
        clf.fit(X_train, y_train)
        print("Training finished. T: %-3.2f" % (time.time() - t))

        best_features = []
        importances = clf.feature_importances_
        std = np.std([tree.feature_importances_ for tree in clf.estimators_], axis=0)
        indices = np.argsort(importances)[::-1]
        indices = indices[:options.top_num_features]
        myrange = min(X_train.shape[1], options.top_num_features)

        # Print the feature ranking
        print("Feature ranking:")
        for f in range(myrange):
            print("%-3d  %-55s -- %8.4f" %
                  (f + 1, features[indices[f]], importances[indices[f]]))

        helper.conf_matrixes(test, features, to_predict, clf)
        helper.conf_matrixes(train, features, to_predict, clf, "train")
        if options.get_best_topn_feats is not None:
                greedy_features = helper.calc_greedy_best_features(top_n_feats)


if __name__ == "__main__":
    usage = "usage: %(prog)s [options] file.sqlite"
    parser = argparse.ArgumentParser(usage=usage)

    # dataframe
    parser.add_argument("fname", type=str, metavar='PANDASFILE')
    parser.add_argument("--split", default=0.001, type=float, metavar="RATIO",
                      dest="min_samples_split", help="Split in tree if this many samples or above. Used as a percentage of datapoints")
    parser.add_argument("--verbose", "-v", action="store_true", default=False,
                        dest="verbose", help="Print more output")
    parser.add_argument("--top", default=40, type=int, metavar="TOPN",
                      dest="top_num_features", help="Candidates are top N features for greedy selector")
    parser.add_argument("-q", default=4, type=int, metavar="QUANTS",
                      dest="quantiles", help="Number of quantiles we want")
    parser.add_argument("--nocomputed", default=False, action="store_true",
                      dest="no_computed", help="Don't add computed features")
    parser.add_argument("--check", action="store_true", default=False,
                      dest="check_row_data", help="Check row data for NaN or float overflow")
    parser.add_argument("--greedy", default=None, type=int, metavar="TOPN",
                      dest="get_best_topn_feats", help="Greedy Best K top features from the top N features given by '--top N'")

    options = parser.parse_args()

    if options.fname is None:
        print("ERROR: You must give the pandas file!")
        exit(-1)

    df = pd.read_pickle(options.fname)

    if not options.no_computed:
        add_computed_features(df)

    rem_useless_features(df)

    p = Predict()
    p.get_top_features(df)
