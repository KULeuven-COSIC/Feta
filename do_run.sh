#!/usr/bin/env bash
set -e

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
#### Processing command line arguments
if [ $# -ne 2 ]; then
    echo >&2 "Usage: $0 {3,4,log} <circuit_dir>"
    exit 1
fi
RUNNER_IDX=$1
TEST_NAME=$2

case $RUNNER_IDX in
    3) RUNNER=tn3;;
    4) RUNNER=tn4;;
    log) RUNNER=log;;
    *) echo >&2 "Unknown protocol type: $RUNNER_IDX"; exit 1;;
esac

if [[ ! -f "${SCRIPT_DIR}/test_data/$TEST_NAME/run_config" ]]; then
    echo >&2 "Unknown circuit: $TEST_NAME"
    exit 1
fi

###############

# Load the config for the test case
source "${SCRIPT_DIR}/test_data/$TEST_NAME/run_config"

# We don't always need the batch size
if [ "$RUNNER" = "tn3" ]; then
    EXTRA_PROVER_ARGS=$BATCH_SIZE
    EXTRA_VERIFIER_ARGS=$BATCH_SIZE
fi

if [ "$RUNNER" = "log" ]; then
    N_PREPROCESSING=$(("$(grep AND ${SCRIPT_DIR}/test_data/${TEST_NAME}/${CIRCUIT} | wc -l)" + "$(head ${SCRIPT_DIR}/test_data/${TEST_NAME}/${CIRCUIT} -n 2 | tail -n 1 | awk '{print $2}')"))
else
    N_PREPROCESSING_LOG=
fi

TMP=$(mktemp -d)
echo Using temp directory $TMP
cd $TMP

echo "[+] Preparing"
N_VERIFIERS=`sed -En 's/^.* N = ([0-9]+).*$/\1/p' "$SCRIPT_DIR/$RUNNER/config.h"`
echo "$SCRIPT_DIR/$(head -n 1 "$SCRIPT_DIR/netconfig.txt")" > netconfig.txt
tail -n +2 "$SCRIPT_DIR/netconfig.txt" >> netconfig.txt

echo "[+] Preprocessing"
for v in `seq 1 ${N_VERIFIERS}`; do
    "$SCRIPT_DIR/build/preprocessing.$RUNNER" netconfig.txt $v $N_PREPROCESSING $N_PREPROCESSING_LOG &
done
"$SCRIPT_DIR/build/preprocessing.$RUNNER" netconfig.txt 0 $N_PREPROCESSING $N_PREPROCESSING_LOG
wait

echo "[+] Starting Verifiers"
for v in `seq 1 $N_VERIFIERS`; do
    "$SCRIPT_DIR/build/verifier.$RUNNER" netconfig.txt $v "$SCRIPT_DIR/test_data/$TEST_NAME/$CIRCUIT" $EXTRA_VERIFIER_ARGS &
done

echo "[+] Starting Prover"
# valgrind --tool=callgrind --dump-instr=yes --collect-jumps=yes "$SCRIPT_DIR/build/prover.$RUNNER" netconfig.txt "$SCRIPT_DIR/test_data/$TEST_NAME/$CIRCUIT" "$SCRIPT_DIR/test_data/$TEST_NAME/$PRIV_INPUT" $EXTRA_PROVER_ARGS
"$SCRIPT_DIR/build/prover.$RUNNER" netconfig.txt "$SCRIPT_DIR/test_data/$TEST_NAME/$CIRCUIT" "$SCRIPT_DIR/test_data/$TEST_NAME/$PRIV_INPUT" $EXTRA_PROVER_ARGS
wait
