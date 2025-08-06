#!/bin/bash

# array of programs to run
PROGRAMS=("syscall-check" "file-check" "tty-drivers" "netfilter-hijack" "tcp-op" "process-list" "priv-escalation" "module-list" "keyboard-sniffer" "vfs-hook")

# command-line arguments
# -r [IP address of adapter host, i.e. target of introspection]
# -b [bus:devfn of FPGA on adapter host]
# -s [path to System.map on device host]
ARGS="-r 192.168.20.1 -b 01:00 -s $HOME/System.map"  

# number of trials for each program
TRIALS=5  

for prog in "${PROGRAMS[@]}"; do
    prog_cmd="$prog $ARGS"
    echo "./$prog_cmd"

    times=()

    for trial in $(seq 1 $TRIALS); do
        output=$(./"$prog" $ARGS)
        time_value=$(echo "$output" | tail -1 | grep -oE '[0-9]+\.?[0-9]*' | tail -1)
        echo "    Trial $trial: $time_value seconds"

        if [[ -n "$time_value" ]]; then
            times+=("$time_value")
        fi
    done

    # Compute statistics (min, max, med, avg)
    if [[ ${#times[@]} -gt 0 ]]; then 
        echo "Statistics:"

        IFS=$'\n' sorted=($(sort -g <<<"${times[*]}"))
        unset IFS

        min=${sorted[0]}
        max=${sorted[-1]}

        sum=$(printf '%s\n' "${times[@]}" | awk '{sum+=$1} END {print sum}')
        count=${#times[@]}
        avg=$(echo "scale=6; $sum / $count" | bc)

        mid=$((count / 2))
        if ((count % 2 == 1)); then 
            median=${sorted[$mid]}
        else
            median=$(echo "scale=6; (${sorted[$mid-1]} + ${sorted[$mid]}) / 2" | bc)
        fi

        echo "    min: $min seconds"
        echo "    max: $max seconds"
        echo "    avg: 0$avg seconds"
        echo "    med: $median seconds"
    fi
    echo ""
done