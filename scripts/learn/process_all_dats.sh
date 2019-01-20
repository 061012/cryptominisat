#!/usr/bin/bash
set -e
set -x

numconfs=5
location="out-dats-8091519.wlm01"

function wait_threads {
    FAIL=0
    for job in $(jobs -p)
    do
        echo "waiting for job '$job' ..."
        wait "${job}" || FAIL=$((FAIL+=1))
    done
}

function check_fails {
    if [ "$FAIL" == "0" ];
    then
        echo "All went well!"
    else
        echo "FAIL of one of the threads! ($FAIL)"
        exit 255
    fi
}

function check_error {
    # exit in case of errors
    if [[ -s error ]]; then
        echo "ERROR: Issues occurred!"
        exit 255
    else
        echo "OK, no errors"
    fi
}

function populate_error {
    rm -f error
    touch error
    for (( CONF = 0; CONF < numconfs; CONF++))
    do
        grep -E --color -i -e "assert.*fail" -e "signal" -e "error" -e "kill" -e "terminate" "${1}_${CONF}" 2>&1 | tee -a error
        grep -E --color -i -e "assert.*fail" -e "signal" -e "error" -e "kill" -e "terminate" "${2}_${CONF}" 2>&1 | tee -a error
    done
}

function combine {
    rm -f out_combine_short_*
    rm -f comb-short-conf-*.dat
    rm -f out_combine_long_*
    rm -f comb-long-conf-*.dat
    for (( CONF = 0; CONF < numconfs; CONF++))
    do
        rm -f "comb-short-conf-${CONF}.dat"
        rm -f "comb-long-conf-${CONF}.dat"

        ./combine_dats.py -o "comb-short-conf-${CONF}.dat" ${location}/*-short-conf-${CONF}.dat > "out_combine_short_${CONF}" 2>&1 &
        ./combine_dats.py -o "comb-long-conf-${CONF}.dat"  ${location}/*-long-conf-${CONF}.dat  > "out_combine_long_${CONF}" 2>&1 &

        wait_threads
        check_fails
    done

    populate_error "out_combine_short" "out_combine_long"
    check_error
}

function predict {
    mkdir -p ../src/predict
    rm -f ../src/predict/*.h
    rm -f out_pred_short_*
    rm -f out_pred_long_*
    for (( CONF = 0; CONF < numconfs; CONF++))
    do
        ./predict.py "comb-long-conf-${CONF}.dat" --scale --name long  --basedir "../src/predict/" --final --forest --split 0.03 --clusters 1 --conf "${CONF}" --clustmin 0.10 > "out_pred_long_${CONF}" 2>&1 &
        ./predict.py "comb-short-conf-${CONF}.dat" --scale --name short --basedir "../src/predict/" --final --forest --split 0.03 --clusters 1 --conf "${CONF}" --clustmin 0.10 > "out_pred_short_${CONF}" 2>&1 &
        wait_threads
        check_fails
    done

    populate_error "out_pred_long" "out_pred_short"
    check_error
}

################
# running
################

combine
predict
