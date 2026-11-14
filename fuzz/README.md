# Fuzzing mod-host's protocol

libFuzzer harnesses (clang, AddressSanitizer, UndefinedBehaviorSanitizer) for the part of mod-host
that reads bytes from a controller. They link the protocol sources only, so they need no JACK server
and no plugins. `fuzz/socket_fuzz.c` stands in for `src/socket.c`: its `socket_send()` copies each reply into
a buffer that the harness reads, so a run opens no socket. mod-host itself does not contain it.

| harness | what it feeds |
| --- | --- |
| `fuzz_protocol` | `protocol_parse()` over a table with every pattern shape (words, `%` wildcards, `...`) |
| `fuzz_statepaths` | `MakePluginStatePath()` and `GetPluginStateDir()` below a scratch directory |

```
dnf install -y clang compiler-rt   # or: apt-get install -y clang (plus libclang-rt-<N>-dev where packaged apart)
make fuzz                          # build/fuzz/fuzz_*
make fuzz-run FUZZ_TIME=60         # each harness for 60 s over a scratch copy of its corpus
```

One harness by hand: `build/fuzz/fuzz_protocol fuzz/corpus/protocol` (add `-max_total_time=60`).
A crash leaves its input in `build/fuzz/artifacts/`; run the harness on that file to reproduce it.

The seed corpora in `fuzz/corpus/` are the protocol's verbs, commands a host sends in normal use, and
malformed shapes a parser has to refuse; for the state paths, the requests a plugin can make.
