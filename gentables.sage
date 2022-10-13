# Copyright (c) 2022, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.
#
# All rights reserved
GENERATE_EMBEDDINGS = [3, 4, 5, 7]

tables = []
for k in range(2, 9):
    F = GF(2^k, 'α', modulus="minimal_weight")
    inj = F.fetch_int
    proj = lambda x: x.integer_representation()
    tables.append((k, [[proj(inj(a) * inj(b)) for b in range(2^k)] for a in range(2^k)] + [[0] + [proj(inj(a)^-1) for a in range(1, 2^k)]]))

textual_tables = []
for k, tb in tables:
    textual_tables.append("constexpr inline std::uint8_t mul%d[(1<<%d) + 1][1<<%d] = %s;" % (k, k, k, str(tb).translate("".maketrans("[]", "{}"))))

with open("gftables.h", "w") as f:
    f.write("""
// This file was automatically generated, changes may be overwritten
#pragma once
#include <cstdint>
namespace gftables {
    %s
} // namespace gftables
""" % "\n    ".join(textual_tables))



embeddings = {}
for base_k in GENERATE_EMBEDDINGS:
    F1 = GF(2^base_k, 'α', modulus="minimal_weight")
    extension_factor = 1
    while extension_factor * base_k <= 128:
        ext_k = extension_factor * base_k
        F2 = GF(2^ext_k, 'β', modulus="minimal_weight")
        el = F2['x'](F1.modulus()).roots()[0][0]
        embeddings[(base_k, ext_k)] = [(el^i).integer_representation() for i in range(base_k)]
        extension_factor += 1

textual_embeddings = []
impl_embeddings_decl = []
impl_embeddings_def = []
for k, v in embeddings.items():
    if k[1] <= 64:
        textual_embeddings.append(f"template <> inline const GF2k<{k[1]}> lift_v<{k[0]}, {k[1]}>[{k[0]}] = "
                + "{%s};" % ",".join("GF2k<%d>{%du}" % (k[1], x) for x in v))
    else:
        # Annoyingly can't do this inline properly, so offload it to an initializer function in its own cpp file
        impl_embeddings_decl.append(f"template <> GF2k<{k[1]}> lift_v<{k[0]}, {k[1]}>[{k[0]}];")
        for i, x in enumerate(v):
            impl_embeddings_def.append(f"lift_v<{k[0]}, {k[1]}>[{i}] = " + "GF2k<%d>{detail::int128(%du, %du)};" % (k[1], x & ((1 << 64) - 1), x >> 64))

with open("gflifttables.h", "w") as f:
    f.write("""
// This file was automatically generated, changes may be overwritten
#pragma once
#include <cstdint>
// Only to keep everything looking nice if you somehow would include the file directly; it's circular otherwise
#include "arith.h"

namespace gflifttables {
    template <int k, int k2> extern const GF2k<k2> lift_v[k];
    %s
} // namespace gflifttables
""" % "\n    ".join(textual_embeddings))

with open("gflifttables.cpp", "w") as f:
    f.write("""
// This file was automatically generated, changes may be overwritten
#include "arith.h"
#include "gflifttables.h"

namespace gflifttables {
    %s
    void __attribute__((constructor)) init_gflifttables() {
        %s
    }
} // namespace gflifttables
""" % ("\n    ".join(impl_embeddings_decl), "\n        ".join(impl_embeddings_def)))
