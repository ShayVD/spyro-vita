# discimage (moved to core/)

The disc image layer is portable C, so it moved to `core/` (roadmap E4) under the `enc_` names:
`core/include/encore/discimage.h`, `core/src/enc_discimage.c`, the docs in
[`core/docs/discimage.md`](../../../../core/docs/discimage.md) and the test in `core/test/`.

This directory keeps what games built before the move: the old headers, which include core's and map the old
`vpk_` names onto the `enc_` ones, and the old `.c` files, which compile core's. A game that builds this
directory, or vendored it, keeps building unchanged. [`NEEDS`](NEEDS) lists the core files it builds, and
`tools/vendor.sh` vendors them with it. New code includes the `encore/` header and calls the `enc_` names.
`../test/compat_test.c` checks the old names still build and link.
