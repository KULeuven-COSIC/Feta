import sys, random

if __name__ == "__main__":
    random.seed(b"Generating a random circuit build of all AND gates")
    N = int(sys.argv[1])
    Nin = int(sys.argv[2])
    uncovered = set()
    print(f"{N} {Nin + N}")
    print(f"1 {Nin}")
    print("1 1")
    print()
    ntaken = 0
    while ntaken + len(uncovered) < N:
        a = random.randrange(ntaken + Nin)
        b = random.randrange(ntaken + Nin)
        print(f"2 1 {a} {b} {ntaken + Nin} AND")
        uncovered -= {a, b}
        uncovered |= {ntaken + Nin}
        ntaken += 1
    uncovered = list(uncovered)
    while len(uncovered) > 1:
        a, b = uncovered[:2]
        print(f"2 1 {a} {b} {ntaken + Nin} AND")
        uncovered = uncovered[2:] + [ntaken + Nin]
        ntaken += 1
