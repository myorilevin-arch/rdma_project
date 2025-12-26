#!/bin/bash

APP_NAME="ori_levine_rdma"
HOSTS=("mlx-stud-01" "mlx-stud-02" "mlx-stud-03" "mlx-stud-04")
HOSTS_STRING="mlx-stud-01 mlx-stud-02 mlx-stud-03 mlx-stud-04"
MASTER_NODE="${HOSTS[0]}"

cleanup() {
    echo ""
    echo "!!! Ctrl+C detected. Cleaning up... !!!"
    
    local_jobs=$(jobs -p)
    if [ -n "$local_jobs" ]; then
        kill $local_jobs 2>/dev/null
    fi

    for host in "${HOSTS[@]}"; do
        echo "Killing remote process on $host..."
        ssh -o ConnectTimeout=2 ori.levine@$host "killall -9 $APP_NAME" 2>/dev/null &
    done
    wait
    
    echo "Cleanup done. Exiting."
    exit 1
}

trap cleanup SIGINT

echo "--- Step 1: Cleaning up old processes ---"
for host in "${HOSTS[@]}"; do
    ssh ori.levine@$host "killall -9 $APP_NAME 2>/dev/null" &
done
wait #

echo "--- Step 2: Copying and Compiling on $MASTER_NODE (Shared FS) ---"

scp main.c rdma_lib.c rdma_lib.h ori.levine@$MASTER_NODE:~/networks/

ssh ori.levine@$MASTER_NODE "cd ~/networks && gcc main.c rdma_lib.c -o $APP_NAME -libverbs"
if [ $? -ne 0 ]; then
    echo "Compilation failed! Aborting."
    exit 1
fi

echo "--- Step 3: Launching Ring ---"
RANDOM_PORT=$((20000 + RANDOM % 10000))
echo "Selected Port: $RANDOM_PORT"

for host in "${HOSTS[@]}"; do
    ssh ori.levine@$host "~/networks/$APP_NAME \"$HOSTS_STRING\" $RANDOM_PORT" &
done

echo "Ring is running. Press Ctrl+C to stop everything properly."
wait
