#!/usr/bin/env bash
set -e

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
#### Processing command line arguments
if [ $# -ne 4 ]; then
    echo >&2 "Usage: $0 {3,4,log} <circuit_dir> <netconfig.txt> <player>"
    exit 1
fi
RUNNER_IDX=$1
TEST_NAME=$2
NETCONF=$3
PLAYER=$4

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

if [[ -f "$NETCONF" ]]; then
    NETCONF=$(realpath "$NETCONF")
else
    echo >&2 "netconfig.txt ($NETCONF) is not a file"
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
    N_PREPROCESSING="$(grep AND ${SCRIPT_DIR}/test_data/${TEST_NAME}/${CIRCUIT} | wc -l)"
else
    N_PREPROCESSING_LOG=
fi

# Do this before changing directory
CERT_STORE_LOC=$(realpath $(head -n 1 "$NETCONF"))

TMP=$(mktemp -d)
echo Using temp directory $TMP
cd $TMP

echo "[+] Preparing"
N_VERIFIERS=`sed -En 's/^.*N = ([0-9]+).*$/\1/p' "$SCRIPT_DIR/$RUNNER/config.h"`
echo "$CERT_STORE_LOC" > netconfig.txt
tail -n +2 "$NETCONF" >> netconfig.txt

echo "[+] Preprocessing"
"$SCRIPT_DIR/build/preprocessing.$RUNNER" netconfig.txt "$PLAYER" "$N_PREPROCESSING" $N_PREPROCESSING_LOG | tee "${TEST_NAME}_preprocessing_$PLAYER.txt"
wait

if [[ $PLAYER -eq 0 ]]; then
    echo "[+] Starting prover"
    "$SCRIPT_DIR/build/prover.$RUNNER" netconfig.txt "$SCRIPT_DIR/test_data/$TEST_NAME/$CIRCUIT" "$SCRIPT_DIR/test_data/$TEST_NAME/$PRIV_INPUT" $EXTRA_PROVER_ARGS | tee "${TEST_NAME}_prover.txt"
else
    echo "[+] Starting verifier $PLAYER"
    "$SCRIPT_DIR/build/verifier.$RUNNER" netconfig.txt "$PLAYER" "$SCRIPT_DIR/test_data/$TEST_NAME/$CIRCUIT" $EXTRA_VERIFIER_ARGS | tee "${TEST_NAME}_verifier_${PLAYER}.txt"
fi
wait
