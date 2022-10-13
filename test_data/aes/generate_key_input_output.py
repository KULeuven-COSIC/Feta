KEY = b"distributedverif"
INPUT = b"0123456789abcdef"

assert len(KEY) == 16
assert len(INPUT) == 16

from Crypto.Cipher import AES
c = AES.new(KEY, AES.MODE_ECB)
OUTPUT = c.encrypt(INPUT)

def to_bits(x):
    bits = []
    for c in x[::-1]:
        for _ in range(8):
            bits.append(str(c & 1))
            c >>= 1
    return bits

with open("./key", "wb") as f:
    f.write(KEY[::-1])

with open("./public_input", "w") as f:
    f.write("\n".join(["-1"] * 128))
    f.write("\n")
    f.write("\n".join(to_bits(INPUT)))
    f.write("\n")

with open("./expected_output", "w") as f:
    f.write("\n".join(to_bits(OUTPUT)))
    f.write("\n")

