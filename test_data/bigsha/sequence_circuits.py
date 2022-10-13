import dataclasses

@dataclasses.dataclass
class Circuit:
    ninputs: int
    noutputs: int
    # gates: list[tuple[int, str, ...]] # (result index, type, ...arguments)
    gates: list


def parse_circ(buf : str) -> Circuit:
    lines = buf.strip().splitlines()[::-1]
    ngates, nwires = map(int, lines.pop().strip().split())
    niv, *nis = map(int, lines.pop().strip().split())
    nov, *nos = map(int, lines.pop().strip().split())
    lines.pop() # Empty, skip
    ninputs = sum(nis)
    noutputs = sum(nos)
    gates = []
    for j in range(ngates):
        i, o, *op, out, g = lines.pop().strip().split()
        i = int(i)
        o = int(o)
        assert o == 1
        gates.append((int(out), g, list(map(int, op))))
    assert len(gates) == ngates
    return Circuit(ninputs, noutputs, gates)

def dump(circ: Circuit, out):
    out.write(f"{len(circ.gates)} {len(circ.gates) + circ.ninputs}\n")
    out.write(f"1 {circ.ninputs}\n")
    out.write(f"1 {circ.noutputs}\n")
    out.write("\n")
    for g in circ.gates:
        out.write(f"{len(g[2])} 1 {' '.join(map(str, g[2]))} {g[0]} {g[1]}\n")

def paste(first: Circuit, second: Circuit) -> Circuit:
    assert first.noutputs <= second.ninputs # Not as general as it could be, but good enough
    ninputs = first.ninputs + (second.ninputs - first.noutputs)
    noutputs = second.noutputs # All C1 inputs must have been consumed, cf assert above
    new_inputs = ninputs - first.ninputs

    # Rebase the first circuit
    new_gates = []
    for (o, g, ins) in first.gates:
        assert all(i < first.ninputs + len(first.gates) for i in ins) # Simplify assumptions: we aren't using output gates in the circuit it originates from
        rebased_ins = tuple(i + (0 if i < first.ninputs else new_inputs) for i in ins)
        # We don't care about the outputs here, we'll take the choice between new inputs and old outputs when dealing with the next circuit
        new_gates.append((o + new_inputs, g, rebased_ins))

    # Rebase the second circuit
    for (o, g, ins) in second.gates:
        assert all(i < second.ninputs + len(second.gates) for i in ins) # Simplify assumptions: we aren't using output gates in the circuit it originates from
        rebased_ins = []
        for i in ins:
            if i < second.ninputs: # Originally an input to C2
                if i < first.noutputs: # An output from C1
                    rebased_ins.append(i + first.ninputs + len(first.gates)) # Offset from C1's output gates
                else: # A "fresh" input
                    rebased_ins.append(i - first.noutputs + first.ninputs) # Get rid of the output-to-inputs and allow for C1's input gates first
            else:
                rebased_ins.append(i - second.ninputs + ninputs + len(first.gates))
        o = o - second.ninputs + ninputs + len(first.gates)
        new_gates.append((o, g, tuple(rebased_ins)))
    return Circuit(ninputs, noutputs, new_gates)

def ntimes(circ: Circuit, n: int) -> Circuit:
    assert n > 0
    res = circ
    for _ in range(n - 1):
        res = paste(res, circ)
    return res

if __name__ == "__main__":
    sha = parse_circ(open("../sha/sha256.txt", "r").read())
    with open("sha256_10blocks.txt", "w") as f:
        dump(ntimes(sha, 10), f)
