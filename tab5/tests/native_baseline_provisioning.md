# Provision the frozen baseline without production LVGL dependencies

The existing ignored `.tools/native-baseline/snapshot/` plus `snapshot.json`
is self-contained. `verify`, `capture`, `build`, `benchmark` do not use production
managed_components. Keep this entire directory as the local baseline package;
do not add the generated source snapshot, object cache or captures to Git.
Only native_baseline* runner/contract sources belong in the repository.

For a fresh checkout, obtain the official LVGL 9.3.0 source **outside production**:

    git clone --depth 1 --branch v9.3.0 https://github.com/lvgl/lvgl.git .tools/native-baseline/lvgl-source-9.3.0
    git -C .tools/native-baseline/lvgl-source-9.3.0 rev-parse HEAD

Expected official release commit:
`c033a98afddd65aaafeebea625382a94020fe4a7`.
Stop if HEAD differs. Official release:
https://github.com/lvgl/lvgl/releases/tag/v9.3.0
https://github.com/lvgl/lvgl/commit/c033a98afddd65aaafeebea625382a94020fe4a7

Then:

    python tab5/tests/native_baseline.py freeze --lvgl-source .tools/native-baseline/lvgl-source-9.3.0
    python tab5/tests/native_baseline.py verify
    python tab5/tests/native_baseline.py capture

`freeze` requires lv_version.h declaring exactly 9.3.0, archives UI/tests/fonts
from full commit 369c7fcd435ffd09631e49c0118e102c66ea4d75, copies the explicit
LVGL source without .git, and pins SHA-256 for every file. It never silently
uses another LVGL revision or creates a production managed dependency.
Existing snapshot.json is verified rather than overwritten.

A manually supplied vendor-patched 9.3.0 tree is version-compatible, but not
assumed byte-identical to this baseline: compare its per-file manifest. To
reproduce the *exact* original 4179-file managed dependency snapshot, transfer
the original source.tar + snapshot/ + snapshot.json package (available locally).
A version check is not a cryptographic authenticity check; the official commit
verification above and stored SHA-256 manifest serve different purposes.
