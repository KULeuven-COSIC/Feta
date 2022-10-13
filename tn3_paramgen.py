# Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.
#
# All rights reserved
import sys, math, argparse

def ilog2(x):
    return len(bin(abs(x))) - 2

if __name__ == "__main__":
    parser = argparse.ArgumentParser(sys.argv[0], description="Compute the number of repetitions required (both full and SZ) for a given field size and batch size")
    parser.add_argument("--n2", action="store", type=int, required=True, help="The batch size")
    parser.add_argument("-F", "--F", action="store", type=int, required=True, help="The extension degree of the binary field")
    parser.add_argument("--logq", action="store", type=int, required=False, default=40, help="log2(q), with q the number of RO queries")
    parser.add_argument("--sec", action="store", type=int, required=False, default=40, help="Security parameter, want soundness error < 2^-seq")
    args = parser.parse_args(sys.argv[1:])
    n2 = args.n2
    sec = args.sec
    logq = args.logq
    F = args.F


    found = False
    rhomin = math.ceil((sec + logq) / F) - 1
    print(f"Minimal preprocessing ρ: {rhomin + 1}")
    for rho in range(rhomin, rhomin + 5):
        for sigma in range(1, 100):
            Fs = 2**F
            if Fs <= 2*(n2 + sigma - 1): break
            num = Fs * (Fs - n2)**sigma
            denom = (Fs - n2)**sigma + Fs * (2*(n2 + sigma - 1))**sigma
            x = rho * ilog2(num // denom)
            if x >= sec + logq:
                print(f"F_{{2^{F}}}; ρ = {rho}; σ = {sigma}; security = {x}")
                found = True
                break
    if not found:
        print("Nothing found")
