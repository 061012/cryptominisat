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
import sklearn
ver = sklearn.__version__.split(".")
if int(ver[1]) < 20:
    from sklearn.cross_validation import train_test_split
else:
    from sklearn.model_selection import train_test_split
import sklearn.ensemble


class Predict:
    def __init__(self):
        pass

    def calc_min_split_point(self, df):
        split_point = int(float(df.shape[0])*options.min_samples_split)
        if split_point < 10:
            split_point = 10
        print("Minimum split point: ", split_point)
        return split_point

    def print_confusion_matrix(self, cm, classes,
                               normalize=False,
                               title='Confusion matrix'):
        """
        This function prints and plots the confusion matrix.
        Normalization can be applied by setting `normalize=True`.
        """
        if normalize:
            cm = cm.astype('float') / cm.sum(axis=1)[:, np.newaxis]
        print(title)

        np.set_printoptions(precision=2)
        print(cm)

    def conf_matrixes(self, data, features, to_predict, clf, toprint="test"):
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
            y_data, y_pred, average="micro")
        recall = sklearn.metrics.recall_score(
            y_data, y_pred, average="micro")
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

    def get_top_features(self, df):
        split_point = self.calc_min_split_point(df)
        df["x.class"]=pd.qcut(df["x.useful_times_per_marked"],
                             q=4,
                             labels=False)
        features = list(df)
        features.remove("x.class")
        features.remove("x.useful_times_per_marked")
        to_predict="x.class"

        print("-> Number of features  :", len(features))
        print("-> Number of datapoints:", df.shape)
        print("-> Predicting          :", to_predict)

        train, test = train_test_split(df, test_size=0.33)
        X_train = train[features]
        y_train = train[to_predict]
        split_point = self.calc_min_split_point(df)
        clf = sklearn.ensemble.RandomForestClassifier(
                    n_estimators=400,
                    max_features="sqrt",
                    min_samples_leaf=split_point)

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

        self.conf_matrixes(test, features, to_predict, clf)
        self.conf_matrixes(train, features, to_predict, clf, "train")


if __name__ == "__main__":
    usage = "usage: %(prog)s [options] file.sqlite"
    parser = argparse.ArgumentParser(usage=usage)

    # dataframe
    parser.add_argument("fname", type=str, metavar='PANDASFILE')
    parser.add_argument("--split", default=0.001, type=float, metavar="RATIO",
                      dest="min_samples_split", help="Split in tree if this many samples or above. Used as a percentage of datapoints")
    parser.add_argument("--top", default=40, type=int, metavar="TOPN",
                      dest="top_num_features", help="Candidates are top N features for greedy selector")

    options = parser.parse_args()

    if options.fname is None:
        print("ERROR: You must give the pandas file!")
        exit(-1)

    df = pd.read_pickle(options.fname)
    p = Predict()
    p.get_top_features(df)
