# Local integration modifications

This BPNode tree contains modifications maintained by banderson777 for
https://github.com/banderson777/bpsec-bplib-integration .
Original upstream: https://github.com/nasa/bp
Upstream base commit: `c9cc1dd9adfe788fe8df4c63ed6ee6b5d6afb668`

Original copyright/license notices are retained. Modified file notices and this
manifest identify local work; commit history records the exact changes. Detailed
fix reviews, regression tests and validation evidence live in the main integration
repository's docs/ directory. This branch supports the native integration lab.

## Modified and added source/configuration

- `CMakeLists.txt`
- `config/default_bpnode_mission_cfg.h`
- `dtn_defs/targets.cmake`
- `fsw/fwp/fwp_tablep.c`
- `fsw/src/bpnode_app.c`
- `fsw/src/bpnode_cla_in.c`
- `fsw/src/bpnode_cla_in.h`
- `fsw/src/bpnode_cla_out.c`
- `fsw/tables/bpnode_mib_pn.c`
- `fsw/tables/bpnode_mib_ps.c`
- `.gitignore`
- `fsw/src/bpnode_demo.c`
- `fsw/src/bpnode_demo.h`
- `fsw/src/bpnode_video_sink.c`
- `fsw/src/bpnode_video_sink.h`

`LOCAL_CHANGES.md` is also added for attribution. Source changes were reviewed and
tested in the integration project; recorded historical validations refer to their
pinned source snapshots. Publication adds attribution notices and Git packaging
without changing the tested program logic.
