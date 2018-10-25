#!/usr/bin/bash
set -x
set -e

# orig: 26s
# prop inside learnt: 22s
# FNAME="barman-pfile07-027.sas.cr.37.cnf"
# FNAMEOUT="mystuff2"
# RATIO="1"
# FIXED="6000"
#
FNAME="UTI-20-10p0.cnf-unz"
FNAMEOUT="mystuff2"
RATIO="0.20"
FIXED="150000"

# orig: 101s
# prop inside learned: 120s
# no prop inside: 77s
# locking in: 131s
FNAME="countbitswegner064.cnf"
FNAMEOUT="mystuff2"
RATIO="0.30" #ratio before 0.6
FIXED="70000" #used before 70k

# orig: 27s
# prop inside learnt: 29
# FNAME="goldb-heqc-i10mul.cnf"
# FNAMEOUT="mystuff2"
# RATIO="0.60"
# FIXED="20000"

# orig time: 70.84
# no prop inside 33.24
# FNAME="goldb-heqc-alu4mul.cnf"
# FNAMEOUT="mystuff2"
# RATIO="0.60"
# FIXED="40000"


# # orig: 98s
# prop inside learnt: 82s
# no prop inside: 95s
# locked: 69s -- 737d7a5dcf32c2c2e07e2f5edf09819ee5fb0dfb
FNAME="AProVE07-16.cnf"
FNAMEOUT="mystuff2"
RATIO="0.60"
FIXED="30000"


# orig: 197.10
# prop inside learnt: 181.35
# no prop inside: 131.36
# locking in: 158.23
# FNAME="UCG-20-5p0.cnf"
# FNAMEOUT="mystuff2"
# RATIO="0.10" #used 0.6 before lock-in
# FIXED="20000" #must use 4000 for lock-in

# cleanup
rm -f "$FNAMEOUT.db"
rm -f "$FNAMEOUT.lemmas-*"
rm -f "$FNAMEOUT.db*-pandas*"
rm -f ../src/final_predictor*

# get data
./build_stats.sh
./cryptominisat5 --cldatadumpratio "$RATIO" --clid --sql 2 --sqlitedb "$FNAMEOUT.db" --drat "$FNAMEOUT.drat" --zero-exit-status "$FNAME"
# --bva 0 --updateglueonanalysis 0 --otfsubsume 0

./tests/drat-trim/drat-trim "$FNAME" "$FNAMEOUT.drat" -x $FNAMEOUT.lemmas -i
./add_lemma_ind.py "$FNAMEOUT.db" "$FNAMEOUT.lemmas"

./gen_pandas.py "$FNAMEOUT.db" --fixed "$FIXED"
./predict.py "$FNAMEOUT.db-short-pandasdata.dat" --final --conf --tree --code ../src/final_predictor_short.cpp --funcname should_keep_short --split 100
./predict.py "$FNAMEOUT.db-long-pandasdata.dat" --final --conf --tree --code ../src/final_predictor_long.cpp --funcname should_keep_long --split 100
./build_final_predictor.sh
./cryptominisat5 "$FNAME"


#--bva 0 --updateglueonanalysis 0 --otfsubsume 0
