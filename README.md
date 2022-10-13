# [Feta: Efficient Threshold Designated-Verifier Zero-Knowledge Proofs](https://eprint.iacr.org/2022/082)

This is a C++ implementation of the protocols for thresholds t < n/3 and t < n/4. 
The files in this toplevel directory contain code that is common and shared between 
the different protocols, while the `tnX` and `log` directories contain the specific implementations 
for the protocol for t < n/X (for `log`: t < n/3).
`tn3` is an older protocol for t < n/3, with an overhead on the order of `O(sqrt(n_S))`,
while `log` performs a logarithmic number of Fiat-Shamir rounds, and only has an overhead
on the order of `O(log(n_S))` for both prover/verifier time and proof size.

Some code (dealing with circuit representations etc) has been taken from the Open Source 
SCALE-MAMBA repository. Where this has been done the copyright notice from these files 
has been included to indicate this is not our work.
The original license for SCALE-MAMBA can be found in [SCALE_MAMBA.license](./SCALE_MAMBA.license).

## Building

This is a project written for the C++17 standard, with a few dependencies:

- A C++17-capable compiler
- `meson` and `ninja` for the build system
- the `openssl` library
- a processor supporting aes-ni, pclmul and sse4.1 instructions

## Setup

To configure the project, run

```sh
meson setup build
```

If you wish to perform timing experiments, also execute

```sh
meson configure build -Dperform_timing=true
```

Finally, to build the executables, execute

```sh
ninja -C build
```

This will produce several different executable files in the `build` directory:

- `decoder`: Mostly irrelevant, used to test the Reed-Solomon robust reconstruction implementation
- `preprocessing.tn4`: Performs the preprocessing step, using the configuration of `tn4/config.h`
- `preprocessing.tn3`: Performs the preprocessing step, using the configuration of `tn3/config.h`
- `preprocessing.log`: Performs the preprocessing step, using the configuration of `log/config.h`
- `prover.tn4`: The prover for the `t < n/4` protocol
- `prover.tn3`: The prover for the **old** `t < n/3` protocol
- `prover.log`: The prover for the `t < n/3` protocol, with a logarithmic number of (Fiat-Shamir) rounds
- `verifier.tn4`: Runs a single verifier for the `t < n/4` protocol
- `verifier.tn3`: Runs a single verifier for the **old** `t < n/3` protocol
- `verifier.log`: Runs a single verifier for the `t < n/3` protocol, with a logarithmic number of (Fiat-Shamir) rounds

These binaries will generally print out a usage summary explaining which arguments they take when
invoked without any arguments.

## Protocol configuration

Each of the protocols have some options configured, such as the field size, the number of verifiers,
the corruption threshold, etc.
The variables controlling these options can be found in the `config.h` header for each of the protocol
directories.
The meaning of each variable is also documented inline in that file.

## Networking configuration

Before running any of the above executables, some configuration is necessary to establish which network
addresses all parties will use, setup a CA and certificates for the pairwise TLS connections, and
public and private keys to be established for the signatures used during preprocessing.

The network configuration file itself follows a simple format:

- The first line contains the directory where all TLS certificates and signature keys will be stored
- Each of the subsequent lines contains the IP/hostname and port for a party, starting with the prover
  (party 0) and followed by the verifiers (parties 1..n).

See the file `netconfig.txt` for an example that configures all parties to run locally on the same machine.

To quickly generate all required certificates and keys,
run `./make_certs.sh x` where `x` is either `3`, `4` or `log`, depending
on which configuration should be read to determine the number of players (`X/config.h`).

## Running a simple test

The `do_run.sh` script will perform a local test in a temporary directory,
running all parties on the current machine.

The first argument to the script (`3`, `4` or `log`) determines which protocol to run.
Pass it the name of the data directory in `test_data` as second argument to run with that circuit and private input.

## Timing

Performing a timing experiment is almost the same as the simple test described above.
This time, use the `do_timing.sh` script, and pass in a separate `netconfig.txt` and the player
index (prover = 0, verifier = 1..n) as third and forth argument.
