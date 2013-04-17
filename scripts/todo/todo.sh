#!/bin/bash
#filespos="~/media/sat/examples/satcop09"
#files=`ls $filespos/*.cnf.gz`
#FILES="~/media/sat/examples/satcop09/*.cnf.gz"

ulimit -t unlimited
ulimit -a
shopt -s nullglob
rm todo
touch todo
#fileloc="/home/soos/sat/examples/satcomp09/*.cnf.gz"
fileloc="/home/soos/sat/examples/satcomp11/application/*cnf.gz"
solver="/home/soos/cryptominisat/build/cryptominisat"
#solver="/home/soos/lingeling-ala-b02aa1a-121013/lingeling"
#solver="/home/soos/glucose2.2/simp/glucose"
#opts="--restart glue --clean glue --flippolarfreq 0"
#opts="--restart agility"
opts=""
output="/home/soos/sat/newout/44-satcomp11"
tlimit="1000"
#5GB mem limit
memlimit="5000000"

mkdir -p $output

# remove everything that has been done
for file in `ls $output`
do
    echo "deleting file $output/$file"
    rm -i $output/$file
    status=$?
    if [ $status -ne 0 ]; then
        echo "error, can't delete file $ouput/$file"
        exit 112
    fi

done

# create todo
echo -ne "Creating todo..."
for file in  $fileloc
do
    filename=$(basename "$file")
    todo="/usr/bin/time --verbose -o $output/$filename.timeout $solver $opts $file > $output/$filename.out 2>&1"
    #todo="$solver $file > $output/$filename.out"
    echo $todo >> todo
    # $todo
done
numlines=`wc -l todo |  awk '{print $1}'`
echo "Done creating todo with $numlines of problems"

# create random order
echo -ne "Randomizing order of execution of $val files"
sort --random-sort todo > todo_rnd
echo "Done."

# create per-core todos
echo "numlines:" $numlines
let numper=numlines/4
remain=$((numlines-numper*4))
mystart=0
echo -ne "Creating per-core TODOs"
for myi in 0 1 2 3
do
    rm todo_rnd_$myi.sh
    echo todo_rnd$myi.sh
    echo "ulimit -t $tlimit" > todo_rnd$myi.sh
    echo "ulimit -v $memlimit" >> todo_rnd$myi.sh
    echo "ulimit -a" >> todo_rnd$myi.sh
    typeset -i myi
    typeset -i numper
    typeset -i mystart
    echo "myi: $myi, numper: $numper,"
    mystart=$((mystart + numper))
    echo "mystart: $mystart"
    head -n $mystart todo_rnd | tail -n $numper>> todo_rnd$myi.sh
    chmod +x todo_rnd$myi.sh
done
echo "Done."
tail -n $remain todo_rnd >> todo_rnd$myi.sh

#check that todos match original
# rm valami
# cat todo_rnd_*.sh >> valami
# diff todo_rnd valami
# status=$?
# if [ $status -ne 0 ]; then
#     echo "error, files don't match"
#     exit 113
# fi

# Execute todos
rm out_*
echo -ne "executing todos..."
for myi in 0 1 2 3
do
#    nohup ./todo_rnd$myi.sh > out_$I &
    echo "OK"
done
echo  "done."

