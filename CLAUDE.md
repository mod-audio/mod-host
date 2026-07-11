# mod-host — pi-Stomp fork

LV2 plugin host for JACK. Takes text commands on a socket (default 5555), pushes async events out a feedback socket (5556). `mod-ui` is the only client that matters here; pi-Stomp reaches us through it or via MIDI CC.

Fork of `mod-audio/mod-host` at `TreeFallSound/mod-host`. `origin` = the fork, `upstream` = MOD. **Keep the delta small and rebaseable** — we track upstream, we don't diverge from it. Today it is two commits (see [Fork deltas](#fork-deltas)).

## Authority

`README.md` **is** the protocol spec, and it is compiled into the binary: `make` extracts the text between "The commands supported" and "bye!" into `src/info.h`, which becomes the interactive-mode help. Adding a command means editing README.md in the same change. `src/info.h` is a no-dependency rule — it is only regenerated after `make clean`, so a stale help message after editing README is expected, not a bug.

Do not restate the command list here or in code comments. Read `README.md`.

## Build & test

```bash
make                 # mod-host, mod-host.so (jack internal client), fake-input.so, mod-monitor.so
make DEBUG=1         # -O0 -g -DDEBUG
make TESTBUILD=1     # -Werror plus the full warning set; run this before proposing a diff
make test            # pytest tests/test_host.py — needs a live JACK and the eg-amp LV2
```

Needs jack2, lilv, serd, readline; optional fftw3, hylia, cc_client. The dev box is macOS and the target is arm64 Linux — a local `make` proves it compiles, nothing more. Behavioural verification happens on a device (`pistomp.local`), which is where JACK, the real plugin set, and mod-ui actually exist.

## Threads, and the one invariant

| Thread | Owns |
|--------|------|
| JACK process callback | `ProcessPlugin` in `effects.c` — audio, MIDI in, param application |
| `PostPonedEventsThread` | drains the event list, formats feedback text, writes the feedback socket |
| socket thread (`socket.c` → `protocol.c` → `effects_*`) | command dispatch; mutates the graph |
| JACK callback threads (port registration, etc.) | enqueue events |
| LV2 worker (`worker.c`), HMI client | plugin-scheduled and hardware work |

**The process callback never allocates, locks a contended mutex, blocks, or writes a socket.** Anything it wants to tell the world becomes a `POSTPONED_*` event allocated from `g_rtsafe_mem_pool` (preallocated) and spliced onto `g_rtsafe_list`; `PostPonedEventsThread` picks it up. Adding a new outbound message means adding a `POSTPONED_*` kind, not calling `socket_send_feedback` from the RT path.

Corollary, and the source of a real heap-corruption bug (`9732d45`): the list is a lifetime hazard. Teardown must drain it *after* the last `jack_client_close`/`lilv_instance_free`, because non-RT JACK callbacks keep enqueueing events referencing instances you are freeing. `effects_remove` / `effects_remove_multi` each drain twice for this reason. Any new teardown path needs the same second drain.

`effects.c` is 8.5k lines and holds nearly all of this. That is upstream's design; do not "clean it up".

## Fork deltas

**Lazy LV2 loading** (`src/lv2_index.c`, `+HAVE_SERD`) — upstream calls `lilv_world_load_all()` at startup, parsing ~600 manifests into a sord model that mod-host barely uses: it never enumerates plugins, it only resolves a URI when instantiating one. We skim the manifests with a streaming serd reader and `lilv_world_load_bundle` on demand. Three things must stay true, and the header comments say why:

- Specification/ontology bundles and dyn-manifest bundles load **eagerly** — a patch property's `rdfs:range` lives in the spec, and dyn-manifest plugin URIs aren't knowable without running it.
- A plugin's bundle set includes bundles holding anything `lv2:appliesTo` it — **mod-ui writes user presets into their own bundles**, and `lilv_plugin_get_related` only sees them if those manifests are loaded.
- A miss is a fall-through to the old path, never an error. If `lv2_index_build()` returns NULL, `effects.c` must still `lilv_world_load_all`.

**PortRegistration race** (`9732d45`) — described above.

## Deploying

Shipped as the `mod-host-pistomp` Debian package, built by `pi-gen-pistomp` from **`TreeFallSound/mod-host#master`** (`config.sh: MOD_HOST_BRANCH`). To ship: land on `master`, then in `../pi-gen-pistomp` run `./scripts/bump-version.sh mod-host-pistomp "..."` and push — the version bump is what triggers a rebuild and the apt-repo publish.

On device: `/usr/bin/mod-host -p 5555 -f 5556`, forking, as user `pistomp`, `After=jack.service`, unit shipped by the deb. `journalctl -u mod-host -f`.

Poke it directly:

```bash
ssh pistomp@pistomp.local
python3 - <<'EOF'
import socket; s=socket.create_connection(("localhost",5555))
s.send(b"param_set 2 gain 3.0\0"); print(s.recv(1024))
EOF
```

## Conventions

- C99 (`-std=gnu99`), 4 spaces, existing brace style. Match the file you are in.
- Prefer the smallest diff that upstream could plausibly accept; new subsystems go in new files (`lv2_index.c`), not into `effects.c`.
- Comment the *why* (invariant, ordering hazard, upstream quirk). The code says the what.
- Don't `git commit`/`push`/`rebase` unless asked.

## Siblings (all in `..`)

* `mod-ui` — the webserver that drives this socket
* `pi-stomp` — LCD/footswitch controller; talks to mod-ui, not to us. * `pi-gen-pistomp` — image + deb + apt repo.
* `jack2`, `lilv` — checked out for reference.
