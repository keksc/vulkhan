perf script > flamegraph.perf
stackcollapse-perf.pl flamegraph.perf > flamegraph.folded
flamegraph.pl flamegraph.folded > flamegraph.svg
