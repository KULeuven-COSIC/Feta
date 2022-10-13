#!/usr/bin/env bash
set -e

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

case $1 in
    3) RUNNER=tn3;;
    4) RUNNER=tn4;;
    log) RUNNER=log;;
    *) echo >&2 "Unknown protocol type: $RUNNER_IDX"; exit 1;;
esac

NUM_VERIFIERS=`sed -En 's/^.*\<N = ([0-9]+).*$/\1/p' $RUNNER/config.h`
NUM_PLAYERS=$((NUM_VERIFIERS + 1))
echo "[+] Generating for $NUM_VERIFIERS verifiers, for a total of $NUM_PLAYERS players"

rm -rf Cert-Store
mkdir Cert-Store
cd Cert-Store

echo "[+] Making a root CA"
openssl genrsa -out Root.key 4096
openssl req -new -x509 -days 1826 -key Root.key -out Root.crt -batch

for p in `seq 0 $NUM_VERIFIERS`; do
    echo "[+] Generating key for player $p"
    openssl genrsa -out Player${p}.key 2048
    openssl req -new -key Player${p}.key -out Player${p}.csr -subj "/CN=Player${p}" -batch
    openssl x509 -req -days 1000 -in Player${p}.csr -CA Root.crt -CAkey Root.key -set_serial 0101 -out Player${p}.crt -sha256
    echo $p
done

for p in `seq 0 $NUM_VERIFIERS`; do
    echo "[+] Generating signing key for player $p"
    openssl ecparam -genkey -noout -name prime256v1 -out Player${p}.priv
    openssl ec -in Player${p}.priv -pubout -out Player${p}.pub
done
