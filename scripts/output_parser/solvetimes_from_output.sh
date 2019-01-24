#!/bin/sh

echo "checking for assert/signal/error/terminate fail"
xzgrep --color -i -e "assert.*fail" -e "signal" -e "error" -e "terminate" `ls *.out.xz` | tee issues.csv
echo "checking for signal 4"
xzgrep "signal 4"  issues.csv

# 1500 cutoff
echo "Getting solveTimes"
xzgrep "Total" *.out.xz | awk '{print $7 " "$1}' | sed 's/:c.*$//' > solveTimes_xz.csv
echo "Getting problems solved under 1500"
awk '{if ($1 < 1500) {print $2}}' solveTimes_xz.csv | sort > solved_under_1500_full_list_xz.csv
awk '{print $2}' solveTimes_xz.csv | sort > solved_xz.csv
sed 's/.gz.*/.gz/' solved_xz.csv > solved.csv
sed 's/.gz.*/.gz/' solveTimes_xz.csv | sort -n > solveTimes.csv
ls -- *.out.xz > allFiles_xz.csv
ls -- *.out.xz | sed "s/.gz.*/.gz/" > allFiles.csv


# for normal
echo "Getting SAT & UNSAT"
xzgrep "^s UNSATISFIABLE" $(cat solved_xz.csv) | sed 's/:s.*$//' | sed 's/.gz.*/.gz/' | sort > solvedUNSAT.csv
xzgrep "^s SATISFIABLE" $(cat solved_xz.csv)   | sed 's/:s.*$//' | sed 's/.gz.*/.gz/' | sort > solvedSAT.csv

# adjusting solved.csv, solveTimes.csv, solveTimes_rev.csv
echo "Getting solveTimes_rev.csv"
grep -v -f solved.csv allFiles.csv | sed "s/.gz.*/.gz/" > unsolved.csv
grep -v -f solved_xz.csv allFiles_xz.csv > unsolved_xz.csv
cat unsolved.csv | awk '{print "5000.00 " $1}' >> solveTimes.csv
awk '{print $2 " " $1}' solveTimes.csv | sort > solveTimes_rev.csv

# memory out
echo "Getting memout..."
xzgrep "what.*bad.*alloc" $(cat unsolved_xz.csv) | sed "s/.gz.*/.gz/" | sort > memout.csv
grep -v -f memout.csv allFiles.csv | sed "s/.gz/.gz OK/" > memout2.csv
cat memout.csv | sed "s/.gz/.gz BAD/" >> memout2.csv
sort memout2.csv > memout3.csv
rm memout.csv memout2.csv
mv memout3.csv memout.csv

# 1500 cutoff
echo "Getting 1500 UNSAT"
xzgrep "^s UNSATISFIABLE" $(cat solved_under_1500_full_list_xz.csv) | sed 's/:s.*$//' | sed 's/.gz.*/.gz/' | sort > solvedUNSAT1500.csv
echo "Getting 1500 SAT"
xzgrep "^s SATISFIABLE" $(cat solved_under_1500_full_list_xz.csv)   | sed 's/:s.*$//' | sed 's/.gz.*/.gz/' | sort > solvedSAT1500.csv
rm -f solved1500.csv
cat solvedUNSAT1500.csv > solved1500.csv
cat solvedSAT1500.csv >> solved1500.csv

cat solvedSAT.csv | awk '{print $1 " SAT"}' > solved_sol.csv
cat solvedUNSAT.csv | awk '{print $1 " UNSAT"}' >> solved_sol.csv
cat unsolved.csv | awk '{print $1 " UNKN"}' >> solved_sol.csv
sort solved_sol.csv > solved_sol2.csv
rm solved_sol.csv
mv solved_sol2.csv solved_sol.csv


xzgrep signal  *.timeout.xz | sed -E "s/.timeout.*signal (.*)/ \1/" > signals.csv
xzgrep signal  *.timeout.xz | sed -E "s/.timeout.*//" > signals_files.csv
xzgrep -v -f signals_files.csv allFiles.csv | awk '{print $1 " -1"}' >> signals.csv
sort signals.csv > signals_sorted.csv
rm signals.csv signals_files.csv
mv signals_sorted.csv signals.csv
