#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright (C) 2018  Mate Soos
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

import pandas as pd
import pickle
import sklearn
import sklearn.svm
import sklearn.tree
import sklearn.cluster
from sklearn.preprocessing import StandardScaler
import argparse
import sys
import numpy as np
import sklearn.metrics
import time
import itertools
import math
import matplotlib.pyplot as plt
import sklearn.ensemble
import os
ver = sklearn.__version__.split(".")
if int(ver[1]) < 20:
    from sklearn.cross_validation import train_test_split
else:
    from sklearn.model_selection import train_test_split
import re
import operator


def write_mit_header(f):
    f.write("""/******************************************
Copyright (c) 2018, Mate Soos

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
***********************************************/\n\n""")


def add_computed_features(df):
    def check_clstat_row(self, row):
        if row[self.ntoc["cl.decision_level_hist"]] == 0 or \
                row[self.ntoc["cl.backtrack_level_hist"]] == 0 or \
                row[self.ntoc["cl.trail_depth_level_hist"]] == 0 or \
                row[self.ntoc["cl.size_hist"]] == 0 or \
                row[self.ntoc["cl.glue_hist"]] == 0 or \
                row[self.ntoc["cl.num_antecedents_hist"]] == 0:
            print("ERROR: Data is in error:", row)
            assert(False)
            exit(-1)

        return row

    # relative overlaps
    print("Relative overlaps...")
    df["cl.antec_num_total_lits_rel"] = df["cl.num_total_lits_antecedents"]/df["cl.antec_sum_size_hist"]

    # ************
    # TODO decision level and branch depth are the same, right???
    # ************
    print("size/glue/trail rel...")
    # df["cl.size_rel"] = df["cl.size"] / df["cl.size_hist"]
    # df["cl.glue_rel_queue"] = df["cl.glue"] / df["cl.glue_hist_queue"]
    # df["cl.glue_rel_long"] = df["cl.glue"] / df["cl.glue_hist_long"]
    #df["cl.glue_rel"] = df["cl.glue"] / df["cl.glue_hist"]
    df["cl.trail_depth_level_rel"] = df["cl.trail_depth_level"]/df["cl.trail_depth_level_hist"]

    df["rst_cur.all_props"] = df["rst_cur.propBinRed"] + df["rst_cur.propBinIrred"] + df["rst_cur.propLongRed"] + df["rst_cur.propLongIrred"]
    df["(cl.num_total_lits_antecedents_/_cl.num_antecedents)"] = df["cl.num_total_lits_antecedents"]/df["cl.num_antecedents"]

    orig_cols = list(df)
    for col in orig_cols:
        if ("rdb0" in col) and "restart_type" not in col:
            col2 = col.replace("rdb0", "rdb1")
            cboth = col.replace("rdb0", "rdb0_plus_rdb1")
            df[cboth]=df[col]+df[col2]

    df["rdb0.act_ranking_rel"] = df["rdb0.act_ranking"]/df["rdb0.tot_cls_in_db"]
    df["rdb1.act_ranking_rel"] = df["rdb1.act_ranking"]/df["rdb1.tot_cls_in_db"]
    df["rdb0_and_rdb1.act_ranking_rel_avg"] = (df["rdb0.act_ranking_rel"]+df["rdb1.act_ranking_rel"])/2

    df["rdb0.sum_uip1_used_per_tot_confl"]=df["rdb0.sum_uip1_used"]/df["cl.time_inside_solver"]
    df["rdb1.sum_uip1_used_per_tot_confl"]=df["rdb1.sum_uip1_used"]/df["cl.time_inside_solver"]

    todiv = [
        "cl.size_hist"
        , "cl.glue_hist"
        , "cl.glue"
        , "cl.old_glue"
        , "cl.glue_hist_queue"
        , "cl.glue_hist_long"
        # , "cl.decision_level_hist"
        , "cl.num_antecedents_hist"
        # , "cl.trail_depth_level_hist"
        # , "cl.backtrack_level_hist"
        , "cl.branch_depth_hist_queue"
        , "cl.antec_overlap_hist"
        , "(cl.num_total_lits_antecedents_/_cl.num_antecedents)"
        , "cl.num_antecedents"
        , "rdb0.act_ranking_rel"
        , "szfeat_cur.var_cl_ratio"
        , "cl.time_inside_solver"
        #, "rdb1.act_ranking_rel"
        , "rdb0_and_rdb1.act_ranking_rel_avg"
        #, "sqrt(rdb0.act_ranking_rel)"
        #, "sqrt(rdb1.act_ranking_rel)"
        #, "sqrt(rdb0_and_rdb1.act_ranking_rel_avg)"
        # , "cl.num_overlap_literals"
        # , "rst_cur.resolutions"
        , "rdb0.act_ranking_top_10"
        ]

    if True:
        extra_todiv = []
        for a in todiv:
            sqrt_name = "sqrt("+a+")"
            df[sqrt_name] = df[a].apply(np.sqrt)
            extra_todiv.append(sqrt_name)
        todiv.extend(extra_todiv)

    # relative data
    cols = list(df)
    for col in cols:
        if ("rdb" in col or "cl." in col or "rst" in col) and "restart_type" not in col:
            for divper in todiv:
                df["("+col+"_/_"+divper+")"] = df[col]/df[divper]

    todiv.extend([
        "rst_cur.all_props"
        , "rdb0.last_touched_diff"
        , "rdb0.sum_delta_confl_uip1_used"
        , "rdb0.used_for_uip_creation"
        , "rdb0.sum_uip1_used_per_tot_confl"
        , "rdb1.sum_uip1_used_per_tot_confl"])

    # relative data
    for col in cols:
        if ("rdb" in col or "cl." in col or "rst" in col) and "restart_type" not in col:
            for divper in todiv:
                df["("+col+"_<_"+divper+")"] = (df[col]<df[divper]).astype(int)
                pass

    # satzilla stuff
    todiv = [
        "szfeat_cur.numVars",
        "szfeat_cur.numClauses",
        "szfeat_cur.var_cl_ratio",
        "szfeat_cur.avg_confl_size",
        "szfeat_cur.avg_branch_depth",
        "szfeat_cur.red_glue_distr_mean"
        ]
    for col in orig_cols:
        if "szfeat" in col:
            for divper in todiv:
                if "min" not in todiv and "max" not in todiv:
                    df["("+col+"_/_"+divper+")"] = df[col]/df[divper]
                    df["("+col+"_<_"+divper+")"] = (df[col]<df[divper]).astype(int)
                    pass

    # relative RDB
    print("Relative RDB...")
    for col in orig_cols:
        if "rdb0" in col and "restart_type" not in col:
            rdb0 = col
            rdb1 = col.replace("rdb0", "rdb1")
            name_per = col.replace("rdb0", "rdb0_per_rdb1")
            name_larger = col.replace("rdb0", "rdb0_larger_rdb1")
            df[name_larger]=(df[rdb0]>df[rdb1]).astype(int)

            raw_col = col.replace("rdb0.", "")
            if raw_col not in ["propagations_made", "dump_no", "conflicts_made", "used_for_uip_creation", "sum_uip1_used", "clause_looked_at", "sum_delta_confl_uip1_used", "activity_rel", "last_touched_diff", "ttl"]:
                print(rdb0)
                df[name_per]=df[rdb0]/df[rdb1]

    # smaller-or-greater comparisons
    print("smaller-or-greater comparisons...")
    df["cl.antec_sum_size_smaller_than_hist"] = (df["cl.antec_sum_size_hist"] < df["cl.num_total_lits_antecedents"]).astype(int)

    df["cl.antec_overlap_smaller_than_hist"] = (df["cl.antec_overlap_hist"] < df["cl.num_overlap_literals"]).astype(int)

    #print("flatten/list...")
    #old = set(df.columns.values.flatten().tolist())
    #df = df.dropna(how="all")
    #new = set(df.columns.values.flatten().tolist())
    #if len(old - new) > 0:
        #print("ERROR: a NaN number turned up")
        #print("columns: ", (old - new))
        #assert(False)
        #exit(-1)

def check_long_short():
    if options.longsh is None:
        print("ERROR: You must give option '--name' as 'short' or 'long'")
        assert False
        exit(-1)

class Learner:
    def __init__(self, df, funcname, fname, df_nofilter, cluster_no):
        self.df = df
        self.funcname = funcname
        self.fname = fname
        self.df_nofilter = df_nofilter
        self.cluster_no = cluster_no


    def output_to_classical_dot(self, clf, features):
        fname = options.dot + "-" + self.funcname
        sklearn.tree.export_graphviz(clf, out_file=fname,
                                     feature_names=features,
                                     class_names=clf.classes_,
                                     filled=True, rounded=True,
                                     special_characters=True,
                                     proportion=True)
        print("Run dot:")
        print("dot -Tpng {fname} -o {fname}.png".format(fname=fname))
        print("gwenview {fname}.png".format(fname=fname))

    def filter_percentile(self, df, features, perc):
        low = df.quantile(perc , axis=0)
        high = df.quantile(1.0-perc, axis=0)
        df2 = df.copy()
        for i in features:
            df2 = df2[(df2[i] >= low[i]) & (df2[i] <= high[i])]
            print("Filtered to %f on %-30s, shape now: %s" %(perc, i, df2.shape))

        print("Original size:", df.shape)
        print("New size:", df2.shape)
        return df2

    def output_to_dot(self, clf, features, to_predict, name, df):
        import dtreeviz.trees
        df2 = self.filter_percentile(df, features, options.filter_dot)
        X_train = df2[features]
        y_train = df2[to_predict]

        values2nums = {'OK': 1, 'BAD': 0}
        y_train = y_train.map(values2nums)
        print("clf.classes_:", clf.classes_)


        #try:
        viz = dtreeviz.trees.dtreeviz(
            clf, X_train, y_train, target_name=name,
            feature_names=features, class_names=list(clf.classes_))
        viz.view()
        #except:
            #print("It doesn't have both OK or BAD -- it instead has:")
            #print("y_train head:", y_train.head())
        del df
        del df2

    def print_confusion_matrix(self, cm, classes,
                               normalize=False,
                               title='Confusion matrix',
                               cmap=plt.cm.Blues):
        """
        This function prints and plots the confusion matrix.
        Normalization can be applied by setting `normalize=True`.
        """
        if normalize:
            cm = cm.astype('float') / cm.sum(axis=1)[:, np.newaxis]
        print(title)

        np.set_printoptions(precision=2)
        print(cm)

        if options.show:
            plt.figure()
            plt.imshow(cm, interpolation='nearest', cmap=cmap)
            plt.title(title)
            plt.colorbar()
            tick_marks = np.arange(len(classes))
            plt.xticks(tick_marks, classes, rotation=45)
            plt.yticks(tick_marks, classes)

            fmt = '.2f' if normalize else 'd'
            thresh = cm.max() / 2.
            for i, j in itertools.product(range(cm.shape[0]), range(cm.shape[1])):
                plt.text(j, i, format(cm[i, j], fmt),
                         horizontalalignment="center",
                         color="white" if cm[i, j] > thresh else "black")

            plt.tight_layout()
            plt.ylabel('True label')
            plt.xlabel('Predicted label')

    # to check for too large or NaN values:
    def check_too_large_or_nan_values(self, df, features):
        # features = df.columns.values.flatten().tolist()
        index = 0
        for index, row in df[features].iterrows():
            for x, name in zip(row, features):
                try:
                    np.isfinite(x)
                except:
                    print("Name:", name)
                    print("Prolbem with value:", x)
                    print(row)

                if not np.isfinite(x) or x > np.finfo(np.float32).max:
                    print("issue with data for features: ", name, x)
                index += 1

    class CodeWriter:
        def __init__(self, clf, features, funcname, code_file):
            self.f = open(code_file, 'w')
            self.code_file = code_file
            write_mit_header(self.f)
            self.clf = clf
            self.feat = features
            self.funcname = funcname

        def define_avg_for_cls(self):
            self.f.write("""
    double rdb_rel_used_for_uip_creation = 0;
    if (cl->stats.used_for_uip_creation > cl->stats.rdb1_used_for_uip_creation) {
        rdb_rel_used_for_uip_creation = 1.0;
    } else {
        rdb_rel_used_for_uip_creation = 0.0;
    }

    double rdb0_avg_confl;
    if (cl->stats.sum_delta_confl_uip1_used == 0) {
        rdb0_avg_confl = 0;
    } else {
        rdb0_avg_confl = ((double)cl->stats.sum_uip1_used)/((double)cl->stats.sum_delta_confl_uip1_used);
    }

    double rdb0_used_per_confl;
    if (sumConflicts-cl->stats.introduced_at_conflict == 0) {
        rdb0_used_per_confl = 0;
    } else {
        rdb0_used_per_confl = ((double)cl->stats.sum_uip1_used)/((double)sumConflicts-(double)cl->stats.introduced_at_conflict);
    }
""")

        def print_full_code(self):
            self.f.write("""#include "clause.h"
#include "reducedb.h"

namespace CMSat {
""")

            num_trees = 1
            if type(self.clf) is sklearn.tree.tree.DecisionTreeClassifier:
                self.f.write("""
static double estimator_{funcname}_0(
    const CMSat::Clause* cl
    , const uint64_t sumConflicts
    , const uint32_t rdb0_last_touched_diff
    , const uint32_t rdb0_act_ranking
    , const uint32_t rdb0_act_ranking_top_10
) {{\n""".format(funcname=self.funcname))
                if options.verbose:
                    print(self.clf)
                    print(self.clf.get_params())
                self.define_avg_for_cls()
                self.get_code(self.clf, 1)
                self.f.write("}\n")
            else:
                num_trees = len(self.clf.estimators_)
                for tree, i in zip(self.clf.estimators_, range(200)):
                    self.f.write("""
static double estimator_{funcname}_{est_num}(
    const CMSat::Clause* cl
    , const uint64_t sumConflicts
    , const uint32_t rdb0_last_touched_diff
    , const uint32_t rdb0_act_ranking
    , const uint32_t rdb0_act_ranking_top_10
) {{\n""".format(est_num=i, funcname=self.funcname))
                    self.define_avg_for_cls()
                    self.get_code(tree, 1)
                    self.f.write("}\n")

            #######################
            # Final tally
            self.f.write("""
static bool {funcname}(
    const CMSat::Clause* cl
    , const uint64_t sumConflicts
    , const uint32_t rdb0_last_touched_diff
    , const uint32_t rdb0_act_ranking
    , const uint32_t rdb0_act_ranking_top_10
) {{\n""".format(funcname=self.funcname))
            self.f.write("    int votes = 0;\n")
            for i in range(num_trees):
                self.f.write("""    votes += estimator_{funcname}_{est_num}(
    cl
    , sumConflicts
    , rdb0_last_touched_diff
    , rdb0_act_ranking
    , rdb0_act_ranking_top_10
    ) < 1.0;\n""".format(est_num=i, funcname=self.funcname))
            self.f.write("    return votes >= %d;\n" % math.ceil(float(num_trees)/2.0))
            self.f.write("}\n")
            self.f.write("}\n")
            print("Wrote code to: ", self.code_file)

        def recurse(self, left, right, threshold, features, node, tabs):
            tabsize = tabs*"    "
            if (threshold[node] != -2):
                feat_name = features[node]
                if feat_name[:3] == "cl.":
                    feat_name = feat_name[3:]
                feat_name = feat_name.replace(".", "_")
                if feat_name == "dump_no":
                    feat_name = "dump_number"

                if feat_name == "size":
                    feat_name = "cl->" + feat_name + "()"
                elif feat_name == "rdb0_last_touched_diff":
                    pass
                elif feat_name == "rdb0_act_ranking_top_10":
                    pass
                elif feat_name == "rdb0_act_ranking":
                    pass
                elif feat_name == "rdb0_avg_confl":
                    pass
                elif feat_name == "rdb0_used_per_confl":
                    pass
                elif feat_name == "rdb_rel_used_for_uip_creation":
                    pass
                else:
                    feat_name = "cl->stats." + feat_name

                feat_name = re.sub(r"dump_no", r"dump_number", feat_name)
                feat_name = feat_name.replace("cl->stats.rdb0_", "cl->stats.")

                self.f.write("{tabs}if ( {feat} <= {threshold}f ) {{\n".format(
                    tabs=tabsize,
                    feat=feat_name, threshold=str(threshold[node])))

                # recruse left
                if left[node] != -1:
                    self.recurse(left, right, threshold, features, left[node], tabs+1)

                self.f.write("{tabs}}} else {{\n".format(tabs=tabsize))

                # recurse right
                if right[node] != -1:
                    self.recurse(left, right, threshold, features, right[node], tabs+1)

                self.f.write("{tabs}}}\n".format(tabs=tabsize))
            else:
                x = self.value[node][0][0]
                y = self.value[node][0][1]
                if y == 0:
                    ratio = "1"
                else:
                    ratio = "%0.1f/%0.1f" % (x, y)

                self.f.write("{tabs}return {ratio};\n".format(
                    tabs=tabsize, ratio=ratio))

        def get_code(self, clf, starttab=0):
            left = clf.tree_.children_left
            right = clf.tree_.children_right
            threshold = clf.tree_.threshold
            if options.verbose:
                print("Node count:", clf.tree_.node_count)
                print("Left: %s Right: %s Threshold: %s" % (left, right, threshold))
                print("clf.tree_.feature:", clf.tree_.feature)
            features = [self.feat[i % len(self.feat)] for i in clf.tree_.feature]
            self.value = clf.tree_.value

            self.recurse(left, right, threshold, features, 0, starttab)

    def calc_min_split_point(self, df):
        split_point = int(float(df.shape[0])*options.min_samples_split)
        if split_point < 10:
            split_point = 10
        print("Minimum split point: ", split_point)
        return split_point

    def conf_matrixes(self, dump_no, data, features, to_predict, clf, toprint="test"):
        # filter test data
        if dump_no is not None:
            print("\nCalculating confusion matrix -- dump_no == %s" % dump_no)
            data = data[data["rdb0.dump_no"] == dump_no]
        else:
            print("\nCalculating confusion matrix -- ALL dump_no")
            data = data

        # get data
        X_data = data[features]
        y_data = data[to_predict]
        print("Number of elements:", X_data.shape)
        if data.shape[0] <= 1:
            print("Cannot calculate confusion matrix, too few elements")
            return 0, 0, 0

        # Preform prediction
        y_pred = clf.predict(X_data)

        # calc acc, precision, recall
        accuracy = sklearn.metrics.accuracy_score(
            y_data, y_pred)
        precision = sklearn.metrics.precision_score(
            y_data, y_pred, pos_label="OK", average="binary")
        recall = sklearn.metrics.recall_score(
            y_data, y_pred, pos_label="OK", average="binary")
        print("%s prec : %-3.4f  recall: %-3.4f accuracy: %-3.4f" % (
            toprint, precision, recall, accuracy))

        # Plot confusion matrix
        cnf_matrix = sklearn.metrics.confusion_matrix(
            y_true=y_data, y_pred=y_pred)
        self.print_confusion_matrix(
            cnf_matrix, classes=clf.classes_,
            title='Confusion matrix, without normalization (%s)' % toprint)
        self.print_confusion_matrix(
            cnf_matrix, classes=clf.classes_, normalize=True,
            title='Normalized confusion matrix (%s)' % toprint)

        return precision, recall, accuracy

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

        if options.check_row_data:
            self.check_too_large_or_nan_values(df, features)

        # count good and bad
        files = df[["x.class", "rdb0.dump_no"]].groupby("x.class").count()
        if files["rdb0.dump_no"].index[0] == "BAD":
            bad = files["rdb0.dump_no"][0]
            good = files["rdb0.dump_no"][1]
        else:
            bad = files["rdb0.dump_no"][1]
            good = files["rdb0.dump_no"][0]

        assert bad > 0, "No need to train, data only contains BAD"
        assert good > 0, "No need to train, data only contains GOOD"

        print("Number of BAD  elements        : %-6d" % bad)
        print("Number of GOOD elements        : %-6d" % good)

        # balance it out
        prefer_ok = float(bad)/float(good)
        print("Balanced OK preference would be: %-6.3f" % prefer_ok)

        # apply inbalance from option given
        prefer_ok *= options.prefer_ok
        print("Option to prefer OK is set to  : %-6.3f" % options.prefer_ok)
        print("Final OK preference is         : %-6.3f" % prefer_ok)

        train, test = train_test_split(df, test_size=0.33)
        X_train = train[features]
        y_train = train[to_predict]
        split_point = self.calc_min_split_point(df)
        del df

        t = time.time()
        clf = None
        # clf = sklearn.linear_model.LogisticRegression()

        if final:
            clf_tree = sklearn.tree.DecisionTreeClassifier(
                    max_depth=options.tree_depth,
                    class_weight={"OK": prefer_ok, "BAD": 1},
                    min_samples_split=split_point)

            clf_svm_pre = sklearn.svm.SVC(C=500, gamma=10**-5)
            clf_svm = sklearn.ensemble.BaggingClassifier(
                clf_svm_pre,
                n_estimators=3,
                max_samples=0.5, max_features=0.5)

            clf_logreg = sklearn.linear_model.LogisticRegression(
                C=0.3,
                penalty="l1")

            clf_forest = sklearn.ensemble.RandomForestClassifier(
                    n_estimators=options.num_trees,
                    max_features="sqrt",
                    class_weight={"OK": prefer_ok, "BAD": 1},
                    min_samples_leaf=split_point)

            if options.final_is_tree:
                clf = clf_tree
            elif options.final_is_svm:
                clf = clf_svm
            elif options.final_is_logreg:
                clf = clf_logreg
            elif options.final_is_forest:
                clf = clf_forest
            elif options.final_is_voting:
                mylist = [["forest", clf_forest], ["svm", clf_svm], ["logreg", clf_logreg]]
                clf = sklearn.ensemble.VotingClassifier(mylist)
            else:
                print("ERROR: You MUST give one of: tree/forest/svm/logreg/voting classifier")
                exit(-1)
        else:
            clf = sklearn.ensemble.RandomForestClassifier(
                n_estimators=4000,
                max_features="sqrt",
                class_weight={"OK": prefer_ok, "BAD": 1},
                min_samples_leaf=split_point)

        clf.fit(X_train, y_train)

        print("Training finished. T: %-3.2f" % (time.time() - t))

        best_features = []
        if not final:
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
                best_features.append(features[indices[f]])

            # Plot the feature importances of the clf
            if options.show:
                plt.figure()
                plt.title("Feature importances")
                plt.bar(range(myrange), importances[indices],
                        color="r", align="center",
                        yerr=std[indices])
                plt.xticks(range(myrange), [features[x] for x in indices], rotation=45)
                plt.xlim([-1, myrange])

        if options.dot is not None and final:
            if not options.final_is_tree:
                print("ERROR: You cannot use the DOT function on non-trees")
                exit(-1)

            #for filt in [1, 3, 10, 10000]:
                #x = "Keep these clauses -- filtered to %d and smaller dump_no" % filt
                #print(x)
                #xdf = df[df["rdb0.dump_no"] <= filt]
                #self.output_to_dot(clf, features, to_predict, x, xdf)
                #del xdf

            self.output_to_classical_dot(clf, features)

        if options.basedir and final and write_code:
            c = self.CodeWriter(clf, features, self.funcname, self.fname)
            c.print_full_code()

        # filtered data == filtered to avg_dump_no and on cluster
        print("--------------------------")
        print("-   Filtered test data   -")
        print("-   Cluster: %04d        -" % self.cluster_no)
        print("- min avg dumpno: %1.3f  -" % options.min_avg_dumpno)
        print("--------------------------")
        for dump_no in [1, 3, 10, 20, 40, None]:
            prec, recall, acc = self.conf_matrixes(
                dump_no, test, features, to_predict, clf)

        print("--------------------------------")
        print("-- Unfiltered train+test data --")
        print("-      no cluster applied      -")
        print("-   no min avg dumpno applied  -")
        print("--------------------------------")
        for dump_no in [1, None]:
            self.conf_matrixes(
                dump_no, self.df_nofilter, features, to_predict, clf)

        # Plot "train" confusion matrix
        print("--------------------------")
        print("-  Filtered train data  - ")
        print("-   Cluster: %04d       -" % self.cluster_no)
        print("- min avg dumpno: %1.3f -" % options.min_avg_dumpno)
        print("-------------------------")
        self.conf_matrixes(
                dump_no, train, features, to_predict, clf, "train")

        # TODO do L1 regularization

        if not final:
            return best_features
        else:
            return prec+recall+acc

    def remove_old_clause_features(self, features):
        todel = []
        for name in features:
            if "cl2" in name or "cl3" in name or "cl4" in name:
                todel.append(name)

        for x in todel:
            features.remove(x)
            if options.verbose:
                print("Removing old clause feature:", x)

    def rem_features(self, feat, to_remove):
        feat_less = list(feat)
        for feature in feat:
            for rem in to_remove:
                if rem in feature:
                    feat_less.remove(feature)
                    if options.verbose:
                        print("Removing feature from feat_less:", feature)

        return feat_less

    def calc_greedy_best_features(self, top_feats):
        best_features = [top_feats[0]]
        for i in range(options.get_best_topn_feats-1):
            print("*** Round %d Best feature set until now: %s"
                  % (i, best_features))

            best_sum = 0.0
            best_feat = None
            feats_to_try = [i for i in top_feats if i not in best_features]
            print("Will try to get next best from ", feats_to_try)
            for feat in feats_to_try:
                this_feats = list(best_features)
                this_feats.append(feat)
                print("Trying feature set: ", this_feats)
                mysum = self.one_classifier(this_feats, "x.class", True)
                print("Reported mysum: ", mysum)
                if mysum > best_sum:
                    best_sum = mysum
                    best_feat = feat
                    print("-> Making this best accuracy")

            print("*** Best feature for round %d was: %s with mysum: %lf"
                  % (i, best_feat, mysum))
            best_features.append(best_feat)

            print("\n\n")
            print("Final best feature selection is: ", best_features)

        return best_features

    def learn(self):
        features = self.df.columns.values.flatten().tolist()
        features = self.rem_features(
            features, ["x.a_num_used", "x.class", "x.a_lifetime", "fname", "clust", "sum_cl_use"])
        if options.no_rdb1:
            features = self.rem_features(features, ["rdb1", "rdb.rel"])
            features = self.rem_features(features, ["rdb.rel"])

        if True:
            self.remove_old_clause_features(features)

        if options.raw_data_plots:
            pd.options.display.mpl_style = "default"
            self.df.hist()
            self.df.boxplot()

        if not options.only_final:
            top_n_feats = self.one_classifier(features, "x.class", False)
            if options.show:
                plt.show()

            if options.get_best_topn_feats is not None:
                greedy_features = self.calc_greedy_best_features(top_n_feats)

            return

        best_features = ['rdb0.used_for_uip_creation']
        best_features.append('rdb1.used_for_uip_creation')
        best_features.append('cl.size')
        best_features.append('cl.size_rel')
        best_features.append('cl.glue_rel_long')
        best_features.append('cl.glue_rel_queue')
        best_features.append('cl.glue')
        best_features.append('rdb0.act_ranking_top_10')

        # must NOT be used!!
        # this depends on how many clauses are in the database and that will DEFINIETELY
        # not be the same during a "normal" run!
        # best_features.append('rdb0.act_ranking')
        # best_features.append('rdb1.act_ranking')

        best_features.append('rdb0.last_touched_diff')
        best_features.append('rdb1.act_ranking_top_10')
        best_features.append('rdb1.last_touched_diff')
        best_features.append('rdb.rel_used_for_uip_creation')

        # expensive, not really useful?
        best_features.append('cl.num_antecedents_rel') # should we?
        best_features.append('cl.antec_num_total_lits_rel')

        # best_features.append('cl.glue_smaller_than_hist_queue')
        best_features.append('cl.num_overlap_literals')
        best_features.append('cl.num_overlap_literals_rel')

        # these don't allow for "fresh" claues to be correctly dealt with
        best_features.append('rdb0.dump_no')
        check_long_short()
        if options.longsh != "short":
            best_features.append('rdb0.sum_uip1_used')
            best_features.append('rdb0.sum_delta_confl_uip1_used')
        # best_features.append('rdb0.avg_confl')
        # best_features.append('rdb0.used_per_confl')

        best_features.append('cl.antecedents_glue_long_reds_var')
        best_features.append('cl.num_total_lits_antecedents')

        #best_features.append('cl.cur_restart_type')

        if options.no_rdb1:
            best_features = self.rem_features(best_features, ["rdb.rel", "rdb1."])

        best_features = [
        "(rdb0_plus_rdb1.propagations_made_/_rdb0_and_rdb1.act_ranking_rel_avg)"
        , "(rdb0_plus_rdb1.propagations_made_/_cl.branch_depth_hist_queue)"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_sqrt(cl.glue))"
        , "(rdb0_plus_rdb1.propagations_made_/_rdb0.act_ranking_rel)"
        , "(rdb0.sum_uip1_used_per_tot_confl_/_sqrt(cl.old_glue))"
        , "(rdb0_plus_rdb1.propagations_made_/_sqrt(rdb0_and_rdb1.act_ranking_rel_avg))"
        , "(rdb0.sum_uip1_used_per_tot_confl_/_sqrt(cl.num_antecedents))"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_sqrt(cl.old_glue))"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_rdb0_and_rdb1.act_ranking_rel_avg)"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_sqrt((cl.num_total_lits_antecedents_/_cl.num_antecedents)))"
        , "(rdb0_plus_rdb1.propagations_made_/_sqrt(rdb0.act_ranking_rel))"
        , "(rdb0_plus_rdb1.propagations_made_/_(cl.num_total_lits_antecedents_/_cl.num_antecedents))"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_sqrt(rdb0.act_ranking_top_10))"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_sqrt(cl.num_antecedents))"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_sqrt(cl.branch_depth_hist_queue))"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_cl.old_glue)"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_sqrt(cl.time_inside_solver))"
        , "(rdb0_plus_rdb1.propagations_made_/_sqrt(cl.num_antecedents))"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_sqrt(cl.glue_hist))"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_sqrt(rdb0_and_rdb1.act_ranking_rel_avg))"
        , "(rdb0_plus_rdb1.propagations_made_/_sqrt(cl.glue_hist))"
        , "(rdb0_plus_rdb1.propagations_made_/_sqrt(rdb0.act_ranking_top_10))"
        , "(rdb0.sum_uip1_used_per_tot_confl_/_sqrt(cl.glue))"
        , "(rdb0_plus_rdb1.propagations_made_/_cl.time_inside_solver)"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_cl.glue_hist)"
        , "(rdb0_plus_rdb1.used_for_uip_creation_/_cl.glue)"
        , "rdb0_plus_rdb1.used_for_uip_creation"
        , "(rdb0_plus_rdb1.propagations_made_/_rdb0.act_ranking_top_10)"
        , "(rdb0_plus_rdb1.propagations_made_/_cl.num_antecedents)"
        , "(rdb0_plus_rdb1.propagations_made_/_sqrt(cl.num_antecedents_hist))"]

        self.one_classifier(best_features, "x.class",
                            final=True,
                            write_code=True)

        if options.show:
            plt.show()


class Clustering:
    def __init__(self, df):
        self.df = df

    def clear_data_from_str(self):
        values2nums = {'luby': 0, 'glue': 1, 'geom': 2}
        df.loc[:, ('cl.cur_restart_type')] = \
            df.loc[:, ('cl.cur_restart_type')].map(values2nums)

        df.loc[:, ('rdb0.cur_restart_type')] = \
            df.loc[:, ('rdb0.cur_restart_type')].map(values2nums)

        df.loc[:, ('rst_cur.restart_type')] = \
            df.loc[:, ('rst_cur.restart_type')].map(values2nums)

        if not options.no_rdb1:
            df.loc[:, ('rdb1.cur_restart_type')] = df.loc[:, ('rdb1.cur_restart_type')].map(values2nums)
        df.fillna(0, inplace=True)

    def create_code_for_cluster_centers(self, clust, scaler, sz_feats):
        sz_feats_clean = []
        for feat in sz_feats:
            assert "szfeat_cur.conflicts" not in feat

            # removing "szfeat_cur."
            c = feat[11:]
            if c[:4] == "red_":
                c = c.replace("red_", "red_cl_distrib.")
            if c[:6] == "irred_":
                c = c.replace("irred_", "irred_cl_distrib.")
            sz_feats_clean.append(c)
        assert len(sz_feats_clean) == len(sz_feats)

        check_long_short()
        f = open("{basedir}/clustering_{name}_conf{conf_num}.h".format(
            basedir=options.basedir, name=options.longsh,
            conf_num=options.conf_num), 'w')

        write_mit_header(f)
        f.write("""
#ifndef CLUSTERING_{name}_conf{conf_num}_H
#define CLUSTERING_{name}_conf{conf_num}_H

#include "satzilla_features.h"
#include "clustering.h"
#include <cmath>

namespace CMSat {{
class Clustering_{name}_conf{conf_num}: public Clustering {{

public:
    Clustering_{name}_conf{conf_num}() {{
        set_up_centers();
    }}

    virtual ~Clustering_{name}_conf{conf_num}() {{
    }}

    SatZillaFeatures center[{clusters}];
    std::vector<int> used_clusters;

""".format(clusters=options.clusters, name=options.longsh,
           conf_num=options.conf_num))

        f.write("    virtual void set_up_centers() {\n")
        for i in self.used_clusters:
            f.write("\n        // Doing cluster center %d\n" % i)
            f.write("\n        used_clusters.push_back(%d);\n" % i)
            for i2 in range(len(sz_feats_clean)):
                feat = sz_feats_clean[i2]
                center = clust.cluster_centers_[i][i2]
                f.write("        center[{num}].{feat} = {center}L;\n".format(
                    num=i, feat=feat, center=center))

        f.write("    }\n")

        f.write("""
    double sq(double x) const {
        return x*x;
    }

    virtual double norm_dist(const SatZillaFeatures& a, const SatZillaFeatures& b) const {
        double dist = 0;
        double tmp;
""")
        for feat, i in zip(sz_feats_clean, range(100)):
            f.write("        tmp = (a.%s-%-3.9fL)/%-3.8fL;\n" %
                    (feat, scaler.mean_[i], scaler.scale_[i]))
            f.write("        dist+=sq(tmp-b.{feat});\n\n".format(feat=feat))

        f.write("""
        return dist;
    }\n""")

        f.write("""
    virtual int which_is_closest(const SatZillaFeatures& p) const {
        double closest_dist = std::numeric_limits<double>::max();
        int closest = -1;
        for (int i: used_clusters) {
            double dist = norm_dist(p, center[i]);
            if (dist < closest_dist) {
                closest_dist = dist;
                closest = i;
            }
        }
        return closest;
    }
""")

        f.write("""
};


} //end namespace

#endif //header guard
""")

    def write_all_predictors_file(self, fnames, functs):
        check_long_short()
        f = open("{basedir}/all_predictors_{name}_conf{conf_num}.h".format(
                basedir=options.basedir, name=options.longsh,
                conf_num=options.conf_num), "w")

        write_mit_header(f)
        f.write("""///auto-generated code. Under MIT license.
#ifndef ALL_PREDICTORS_{name}_conf{conf_num}_H
#define ALL_PREDICTORS_{name}_conf{conf_num}_H\n\n""".format(name=options.longsh, conf_num=options.conf_num))
        f.write('#include "clause.h"\n')
        f.write('#include "predict_func_type.h"\n\n')
        for _, fname in fnames.items():
            f.write('#include "predict/%s"\n' % fname)

        f.write('#include <vector>\n')
        f.write('using std::vector;\n\n')

        f.write("namespace CMSat {\n")

        f.write("\nvector<keep_func_type> should_keep_{name}_conf{conf_num}_funcs = {{\n".format(
            conf_num=options.conf_num, name=options.longsh, clusters=options.clusters))

        for i in range(options.clusters):
            dummy = ""
            if i not in self.used_clusters:
                # just use a dummy one. will never be called
                func = next(iter(functs.values()))
                dummy = " /*dummy function, cluster too small*/"
            else:
                # use the correct one
                func = functs[i]

            f.write("    CMSat::{func}{dummy}".format(func=func, dummy=dummy))
            if i < options.clusters-1:
                f.write(",\n")
            else:
                f.write("\n")
        f.write("};\n\n")

        f.write("} //end namespace\n\n")
        f.write("#endif //ALL_PREDICTORS\n")

    def check_clust_distr(self, clust):
        print("Checking cluster distribution....")

        # print distribution
        dist = {}
        for x in clust.labels_:
            if x not in dist:
                dist[x] = 1
            else:
                dist[x] += 1
        print(dist)

        self.used_clusters = []
        minimum = int(self.df.shape[0]*options.minimum_cluster_rel)
        for clust_num, clauses in dist.items():
            if clauses < minimum:
                print("== !! Will skip cluster %d !! ==" % clust_num)
            else:
                self.used_clusters.append(clust_num)

        if options.show_class_dist:
            print("Exact class contents follow")
            for clno in range(options.clusters):
                x = self.df[(self.df.clust == clno)]
                fname_dist = {}
                for _, d in x.iterrows():
                    fname = d['fname']
                    if fname not in fname_dist:
                        fname_dist[fname] = 1
                    else:
                        fname_dist[fname] += 1

                skipped = "SKIPPED"
                if clno in self.used_clusters:
                    skipped = ""
                print("\n\nFile name distribution in {skipped} cluster {clno} **".format(
                    clno=clno, skipped=skipped))

                sorted_x = sorted(fname_dist.items(), key=operator.itemgetter(0))
                for a, b in sorted_x:
                    print("--> %-10s : %s" % (b, a))
            print("\n\nClass contents finished.\n")

        self.used_clusters = sorted(self.used_clusters)

    def filter_min_avg_dump_no(self):
        print("Filtering to minimum average dump_no of {min_avg_dumpno}...".format(
            min_avg_dumpno=options.min_avg_dumpno))
        print("Pre-filter number of datapoints:", self.df.shape)
        self.df_nofilter = self.df.copy()

        self.df['rdb0.dump_no'].replace(['None'], 0, inplace=True)
        self.df.fillna(0, inplace=True)
        # print(df[["fname", "sum_cl_use.num_used"]])
        files = df[["fname", "rdb0.dump_no"]].groupby("fname").mean()
        fs = files[files["rdb0.dump_no"] >= options.min_avg_dumpno].index.values
        filenames = list(fs)
        print("Left with {num} files".format(num=len(filenames)))
        self.df = self.df[self.df["fname"].isin(fs)].copy()

        print("Post-filter number of datapoints:", self.df.shape)

    def cluster(self):
        features = self.df.columns.values.flatten().tolist()

        # features from dataframe
        sz_all = []
        for x in features:
            if "szfeat_cur" in x:
                sz_all.append(x)
        sz_all.remove("szfeat_cur.conflicts")
        if options.verbose:
            print("All features would be: ", sz_all)

        sz_all = []
        sz_all.append("szfeat_cur.var_cl_ratio")
        sz_all.append("szfeat_cur.numClauses")
        # sz_all.append("szfeat_cur.avg_confl_glue")
        sz_all.append("szfeat_cur.avg_num_resolutions")
        sz_all.append("szfeat_cur.irred_size_distr_mean")
        # sz_all.append("szfeat_cur.irred_size_distr_var")
        if options.verbose:
            print("Using features for clustering: ", sz_all)

        # fit to slice that only includes CNF features
        df_clust = self.df[sz_all].astype(float).copy()
        if options.scale:
            scaler = StandardScaler()
            scaler.fit(df_clust)
            if options.verbose:
                print("Scaler:")
                print(" -- ", scaler.mean_)
                print(" -- ", scaler.scale_)

            if options.verbose:
                df_clust_back = df_clust.copy()
            df_clust[sz_all] = scaler.transform(df_clust)
        else:
            class ScalerNone:
                def __init__(self):
                    self.mean_ = [0.0 for n in range(df_clust.shape[1])]
                    self.scale_ = [1.0 for n in range(df_clust.shape[1])]
            scaler = ScalerNone()

        # test scaler's code generation
        if options.scale and options.verbose:
            # we rely on this later in code generation
            # for scaler.mean_
            # for scaler.scale_
            # for cluster.cluster_centers_
            for i in range(df_clust_back.shape[1]):
                assert df_clust_back.columns[i] == sz_all[i]

            # checking that scaler works as expected
            for feat in range(df_clust_back.shape[1]):
                df_clust_back[df_clust_back.columns[feat]] -= scaler.mean_[feat]
                df_clust_back[df_clust_back.columns[feat]] /= scaler.scale_[feat]

            print(df_clust_back.head()-df_clust.head())
            print(df_clust_back.head())
            print(df_clust.head())

        clust = sklearn.cluster.KMeans(n_clusters=options.clusters)
        clust.fit(df_clust)
        self.df["clust"] = clust.labels_

        # print information about the clusters
        if options.verbose:
            print(sz_all)
            print(clust.labels_)
            print(clust.cluster_centers_)
            print(clust.get_params())
        self.check_clust_distr(clust)

        fnames = {}
        functs = {}
        for clno in self.used_clusters:
            check_long_short()
            funcname = "should_keep_{name}_conf{conf_num}_cluster{clno}".format(
                clno=clno, name=options.longsh, conf_num=options.conf_num)
            functs[clno] = funcname

            fname = "final_predictor_{name}_conf{conf_num}_cluster{clno}.h".format(
                clno=clno, name=options.longsh, conf_num=options.conf_num)
            fnames[clno] = fname

            if options.basedir is not None:
                f = options.basedir+"/"+fname
            else:
                f = None
            learner = Learner(
                self.df[(self.df.clust == clno)],
                funcname=funcname,
                fname=f,
                df_nofilter=self.df_nofilter,
                cluster_no = clno)

            print("================ Cluster %3d ================" % clno)
            learner.learn()

        if options.basedir is not None:
            self.create_code_for_cluster_centers(clust, scaler, sz_all)
            self.write_all_predictors_file(fnames, functs)


if __name__ == "__main__":
    usage = "usage: %(prog)s [options] file.pandas"
    parser = argparse.ArgumentParser(usage=usage)

    parser.add_argument("fname", type=str, metavar='PANDASFILE')
    parser.add_argument("--verbose", "-v", action="store_true", default=False,
                      dest="verbose", help="Print more output")
    parser.add_argument("--printfeat", action="store_true", default=False,
                      dest="print_features", help="Print features")

    # tree options
    parser.add_argument("--depth", default=None, type=int,
                      dest="tree_depth", help="Depth of the tree to create")
    parser.add_argument("--split", default=0.01, type=float, metavar="RATIO",
                      dest="min_samples_split", help="Split in tree if this many samples or above. Used as a percentage of datapoints")

    # generation of predictor
    parser.add_argument("--dot", type=str, default=None,
                      dest="dot", help="Create DOT file")
    parser.add_argument("--filterdot", default=0.05, type=float,
                      dest="filter_dot", help="Filter the DOT output from outliers so the graph looks nicer")
    parser.add_argument("--show", action="store_true", default=False,
                      dest="show", help="Show visual graphs")
    parser.add_argument("--check", action="store_true", default=False,
                      dest="check_row_data", help="Check row data for NaN or float overflow")
    parser.add_argument("--rawplots", action="store_true", default=False,
                      dest="raw_data_plots", help="Display raw data plots")

    parser.add_argument("--name", default=None, type=str,
                      dest="longsh", help="Raw C-like code will be written to this function and file name")
    parser.add_argument("--basedir", type=str,
                      dest="basedir", help="The base directory of where the CryptoMiniSat source code is")
    parser.add_argument("--conf", default=0, type=int,
                      dest="conf_num", help="Which predict configuration this is")

    # data filtering
    parser.add_argument("--only", default=0.99, type=float,
                      dest="only_pecr", help="Only use this percentage of data")
    parser.add_argument("--nordb1", default=False, action="store_true",
                      dest="no_rdb1", help="Delete RDB1 data")
    parser.add_argument("--mindump", default=0, type=float,
                      dest="min_avg_dumpno", help="Minimum average dump_no. To filter out simple problems.")

    # final generator or greedy
    parser.add_argument("--final", default=False, action="store_true",
                      dest="only_final", help="Only generate final predictor")
    parser.add_argument("--greedy", default=None, type=int, metavar="TOPN",
                      dest="get_best_topn_feats", help="Greedy Best K top features from the top N features given by '--top N'")
    parser.add_argument("--top", default=None, type=int, metavar="TOPN",
                      dest="top_num_features", help="Candidates are top N features for greedy selector")

    # clustering
    parser.add_argument("--clusters", default=1, type=int,
                      dest="clusters", help="How many clusters to use")
    parser.add_argument("--clustmin", default=0.05, type=float, metavar="RATIO",
                      dest="minimum_cluster_rel", help="What's the minimum size of the cluster relative to the original set of data.")
    parser.add_argument("--scale", default=False, action="store_true",
                      dest="scale", help="Scale clustering")
    parser.add_argument("--distr", default=False, action="store_true",
                      dest="show_class_dist", help="Show class distribution")

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

    # classifier
    parser.add_argument("--numtrees", default=5, type=int,
                      dest="num_trees", help="How many trees to generate for the forest")

    # classifier weights
    parser.add_argument("--prefok", default=2.0, type=float,
                      dest="prefer_ok", help="Prefer OK if >1.0, equal weight if = 1.0, prefer BAD if < 1.0")

    options = parser.parse_args()

    if options.fname is None:
        print("ERROR: You must give the pandas file!")
        exit(-1)

    if options.clusters <= 0:
        print("ERROR: You must give a '--clusters' option that is greater than 0")
        exit(-1)

    if options.get_best_topn_feats and options.only_final:
        print("Can't do both greedy best and only final")
        exit(-1)

    if options.top_num_features and options.only_final:
        print("Can't do both top N features and only final")
        exit(-1)

    assert options.min_samples_split <= 1.0, "You must give min_samples_split that's smaller than 1.0"
    if not os.path.isfile(options.fname):
        print("ERROR: '%s' is not a file" % options.fname)
        exit(-1)

    df = pd.read_pickle(options.fname)
    if options.print_features:
        feats = sorted(list(df))
        for f in feats:
            print(f)

    add_computed_features(df)

    c = Clustering(df)
    c.clear_data_from_str();
    c.filter_min_avg_dump_no()
    c.cluster()
