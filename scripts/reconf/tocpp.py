#!/usr/bin/env python
# -*- coding: utf-8 -*-

import sys
import gzip
import re
import ntpath

print """
#ifndef _FEATURES_TO_RECONF_H_
#define _FEATURES_TO_RECONF_H_

namespace CMSat {

int features_to_reconf(const Feature& feat)
{
    double best_score = 0.0;
    int best_val = 0;
    double score;
"""

for i in range(12):
    print """
    get_score%d(feat);
    if (best_score < score) {
        best_score = score;
        best_val = %d;
    }
    """ % (i, i)

print """
    return best_val;
}

"""

def read_one_rule(rule_num) :
    f = open("outs/out%d.rules" % rule_num)
    num_conds = 0
    cond_no = 0
    num_rules = 0
    rule_no = 0
    string = ""

    print """
int get_score%d(const Features& feat)
{""" % rule_num
    for line in f:
        if "id=" in line:
            continue

        line = line.strip()
        line = line.split(" ")
        dat = {}
        for elem in line:
            elems = elem.split("=")
            elems = [e.strip("\"") for e in elems]
            # print "elems:", elems
            dat[elems[0]] = elems[1]

        if "conds" in dat:
            assert num_conds == cond_no
            num_conds = int(dat["conds"])
            rule_class = dat["class"]
            cond_no = 0
            continue

        if "entries" in dat:
            continue

        if "rules" in dat:
            num_rules = int(dat["rules"])

        if "default" in dat:
            default = dat["default"]
            if default == "+":
                print "\tdouble output = %.2f;\n" %(1.0)
            else:
                print "\tdouble output = %.2f;\n" %(0.0)
            continue

        #process rules
        if cond_no == 0:
            string = "\tif "
        else:
            string +=" &&\n\t\t"

        # print "dat:", dat
        string +="(%s %s %s)" % (dat["att"], dat["result"], dat["cut"])
        cond_no+= 1

        #end rules
        if cond_no == num_conds:
            string +="\n\t{"
            print string

            string = ""
            string += "\t\toutput = "
            if rule_class == "+":
                string += "1.0;"
            else:
                string += "0.0;"

            print string
            print "\t}"
            rule_no += 1

        # print dat

    print "\t// num_rules:", num_rules
    print "\t// rule_no:", rule_no
    assert num_rules == rule_no
    print "\t// default is:", default
    print """
    \treturn output;
    }
    """



for i in range(12):
    read_one_rule(i)

print """
} //end namespace

#endif //_FEATURES_TO_RECONF_H_

"""

