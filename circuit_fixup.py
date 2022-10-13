# Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.
#
# All rights reserved
from dataclasses import dataclass

class Circuit:
    def simplify(self, inputs):
        if not hasattr(self, "_cache"):
            self._cache = self._simplify(inputs)
        self._cache._cache = self._cache
        return self._cache

    def reindex(self, collect=None):
        if hasattr(self, "_reindexed"): return
        self._reindexed = True
        is_root = collect is None
        if collect is None:
            collect = []

        for c in self.__dict__.values():
            if isinstance(c, Circuit):
                c.reindex(collect)
        collect.append(self)
        
        if is_root:
            # First do all inputs, in their original (remaining) order
            inputs = []
            gates = []
            for c in collect:
                if isinstance(c, Input):
                    inputs.append(c)
                else:
                    gates.append(c)
            assert len(gates) == len(set([g.index for g in gates]))
            for i, inp in enumerate(sorted(inputs, key=lambda inp: inp.index)):
                inp.index = i
            for i, gate in enumerate(gates, start=len(inputs)):
                gate.index = i

    def count(self):
        if hasattr(self, "_counted"):
            return (0, 0)
        self._counted = True
        if isinstance(self, Input):
            return (0, 1) # No gates, 1 wire
        else:
            ng, nw = 1, 1 # Add our own gate and output wire
            for c in self.__dict__.values():
                if isinstance(c, Circuit):
                    t = c.count()
                    ng += t[0]
                    nw += t[1]
            return ng, nw

    def dump(self, outfile):
        if hasattr(self, "_dumped"): return
        self._dumped = True
        for c in self.__dict__.values():
            if isinstance(c, Circuit):
                c.dump(outfile)
        if not isinstance(self, Input):
            outfile.write(f"{self._serialize()}\n")

@dataclass
class Concrete(Circuit):
    v: bool
    def _simplify(self, inputs):
        return self

@dataclass
class AND(Circuit):
    index: int
    a: Circuit
    b: Circuit
    def _simplify(self, inputs):
        a = self.a.simplify(inputs)
        b = self.b.simplify(inputs)
        c, d = None, None
        if isinstance(a, Concrete):
            c,d = a,b
        elif isinstance(b, Concrete):
            c,d = b,a
        if c is None:
            return AND(self.index, a, b)
        elif c.v:
            return d
        else:
            return Concrete(False)

    def _serialize(self):
        return f"2 1 {self.a.index} {self.b.index} {self.index} AND"

@dataclass
class XOR(Circuit):
    index: int
    a: Circuit
    b: Circuit
    def _simplify(self, inputs):
        a = self.a.simplify(inputs)
        b = self.b.simplify(inputs)
        c, d = None, None
        if isinstance(a, Concrete):
            c,d = a, b
        elif isinstance(b, Concrete):
            c,d = b, a
        if c is None:
            return XOR(self.index, a, b)
        elif c.v:
            return INV(self.index, d).simplify(inputs)
        else:
            return d

    def _serialize(self):
        return f"2 1 {self.a.index} {self.b.index} {self.index} XOR"

@dataclass
class INV(Circuit):
    index: int
    a: Circuit
    def _simplify(self, inputs):
        a = self.a.simplify(inputs)
        if isinstance(a, Concrete):
            return Concrete(not a.v)
        elif isinstance(a, INV):
            return a.a
        else:
            return INV(self.index, a)

    def _serialize(self):
        return f"1 1 {self.a.index} {self.index} INV"

@dataclass
class Input(Circuit):
    index: int
    def _simplify(self, inputs):
        if self.index in inputs:
            return Concrete(inputs[self.index])
        return self

GATES = {"AND": AND, "XOR": XOR, "INV": INV}

def parse_circ(buf):
    lines = buf.strip().splitlines()[::-1]
    ngates, nwires = map(int, lines.pop().strip().split())
    niv, *nis = map(int, lines.pop().strip().split())
    nov, *nos = map(int, lines.pop().strip().split())
    lines.pop() # Empty, skip
    gates = [Input(i) for i in range(sum(nis))] + [None for _ in range(nwires - sum(nis))]
    for j in range(ngates):
        i, o, *op, out, g = lines.pop().strip().split()
        i = int(i)
        o = int(o)
        assert o == 1
        g = GATES[g]
        assert gates[int(out)] is None
        assert all(gates[int(x)] is not None for x in op)
        gates[int(out)] = g(j + sum(nis), *[gates[int(x)] for x in op])
    assert not any(x is None for x in gates)
    return gates[-sum(nos):]

def parse_values(buf):
    res = {}
    for i, v in enumerate(buf.strip().splitlines()):
        v = int(v)
        if v in [0, 1]:
            res[i] = bool(v)
        elif v == -1:
            continue
        else:
            assert False, f"Invalid input choice: {v}"
    return res

def dump(circ, outfile):
    ngates, nwires = circ.count()
    outfile.write(f"{ngates} {nwires}\n")
    outfile.write(f"{1} {nwires - ngates}\n") # Treat everything as a single input value, kinda ugly
    outfile.write(f"1 1\n\n") # Single output bit
    circ.dump(outfile)

def add_output_check(output_gates, outputs):
    all_ones = []
    start = 0x100000000
    for i, g in enumerate(output_gates):
        if i not in outputs:
            assert False, "Unconstrained output gate"
        if not outputs[i]:
            all_ones.append(INV(start, g)) # Leave index up to reindexing later
            start += 1
        else:
            all_ones.append(g)

    while len(all_ones) > 1:
        combined = []
        for a, b in zip(all_ones[::2], all_ones[1::2]):
            combined.append(AND(start, a, b)) # Reindex later
            start += 1
        if len(all_ones) % 2 == 1:
            combined.append(all_ones[-1])
        all_ones = combined
    return INV(0, all_ones[0])

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <circuit_file> <public_input_file> <expected_output_file>")
        sys.exit()

    sys.setrecursionlimit(1000000)

    with open(sys.argv[1]) as f:
        circ = parse_circ(f.read())

    with open(sys.argv[2]) as f:
        inputs = parse_values(f.read())

    with open(sys.argv[3]) as f:
        expected_outputs = parse_values(f.read())

    # inputs = {x: False for x in range(10000)}
    # print("\n".join(str(int(c.simplify(inputs).v)) for c in circ))
    circ_checked_outputs = add_output_check(circ, expected_outputs)
    circ_new = circ_checked_outputs.simplify(inputs)
    circ_new.reindex()
    assert not isinstance(circ_new, Concrete), "Circuit reduces to a single value"
    dump(circ_new, sys.stdout)
