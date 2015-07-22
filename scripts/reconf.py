#!/usr/bin/env python
# -*- coding: utf-8 -*-

import sys
import gzip
import re
import ntpath


debug_print = True


def parse_features_line(line):
    line = re.sub("c.*features. ", "", line)
    line = line.strip().split(" ")
    # print line
    dat = {}

    name = ""
    for elem, i in zip(line, xrange(1000)):
        elem = elem.strip(":").strip(",")
        if i % 2 == 0:
            name = elem
            continue

        if name == "(numVars/(1.0*numClauses)":
            name = "var_cl_ratio"
        dat[name] = elem
        # print name, " -- ", elem
        name = ""

    # print dat
    return dat

def nobody_could_solve_it(reconf_score):
    for r_s_elem in reconf_score:
        if r_s_elem[1] != 0:
            return False

    return True

def all_above_fixed_score(reconf_score, score_limit):
    for x in reconf_score:
        if x[1] < score_limit:
            return False

    return True

def print_features_and_scores(fname, features, reconfs_scores):
    r_s = sorted(reconfs_scores, key=lambda x: x[1])[::-1]
    best_reconf = r_s[0][0]
    best_reconf_score = r_s[0][1]
    if debug_print: print r_s

    if nobody_could_solve_it(r_s):
        print "Nobody could solve it"
        return -1

    if all_above_fixed_score(r_s, 4500):
        print "All above score:", r_s
        return -2

    #special case for 7, it's to bad to be used generally
    if best_reconf == 7:
        diff = best_reconf_score-r_s[1][1]
        if debug_print: print "DIFF: %s" % diff
        if diff < 1200:
            tmp = r_s[1]
            r_s[1] = r_s[0]
            r_s[0] = tmp

    #calculate final array
    final_array = [0.0]*9
    val = 1.0
    for conf_score,i in zip(r_s, xrange(100)):
        if conf_score[1] > 0:
            final_array[conf_score[0]] = val
        val -= 0.3
        if val < 0.0 or conf_score[1] == 0:
            val = 0.0


    #print final string
    string = "%s " % (fname)
    for name, val in features.items():
        string += "%s " % val

    string += " || "
    for elem in final_array:
        string += "%.3f " % elem

    print string
    return best_reconf

def parse_file(fname):
    f = gzip.open(fname, 'rb')
    #print "fname orig:", fname
    fname_clean = re.sub("cnf.gz-.*", "cnf.gz", fname)
    fname_clean =  ntpath.split(fname_clean)[1]
    reconf = 0

    satisfiable = None
    features = None
    score = 0
    for line in f:
        line = line.strip()
        #print "parsing line:", line
        if features is None and "features" in line and "numClauses" in line:
            features = parse_features_line(line)

        if "Total time" in line:
            time_used = line.strip().split(":")[1].strip()
            score = int(round(float(time_used)))
            #score -= score % 1000
            score = 5000-score

        if "reconfigured" in line:
            reconf = line.split("to config")[1].strip()
            reconf = int(reconf)

        if "s SATIS" in line:
            satisfiable = True

        if "s UNSATIS" in line:
            satisfiable = False


    #if satisfiable == True:
    #    score = 0

    return fname_clean, reconf, features, score

all_files = set()
all_files_scores = {}
all_files_features = {}
for x in sys.argv[1:]:
    if debug_print: print "# parsing infile:", x
    fname, reconf, features, score = parse_file(x)
    if fname in all_files:
        if all_files_features[fname] != features:
            print "ERROR different features extracted for fname", fname
            print "orig:", all_files_features[fname]
            print "new: ", features
        assert all_files_features[fname] == features
    else:
        all_files.add(fname)
        all_files_features[fname] = features
        all_files_scores[fname] = []

    #print "fname:", fname
    all_files_scores[fname].append([reconf, score])
    sys.stdout.write(".")
    sys.stdout.flush()

if debug_print: print "END--------"
if debug_print: print "all files:", all_files
print ""

best_reconf = {}
for fname in all_files:
    #print "fname:", fname
    if all_files_features[fname] is not None:
        best = print_features_and_scores(fname, all_files_features[fname], all_files_scores[fname])
        if best not in best_reconf:
            best_reconf[best] = 1
        else:
            best_reconf[best] = best_reconf[best] + 1

        if debug_print: print ""

print best_reconf
