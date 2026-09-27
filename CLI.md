<!-- SPDX-License-Identifier: MPL-2.0 -->
# Command line

Run these commands from the repository root.

The sprite command writes `RX3_SPRITES`. It does not write `autoexec.bin`. The runtime command writes `autoexec.bin`.

## Sprite packs

```sh
python -m tools.rx3_sprites preview --source SOURCE
python -m tools.rx3_sprites write --source SOURCE --dest DEST
```

`preview` compiles one source folder in memory. It writes nothing. `write` compiles that folder and replaces `DEST/RX3_SPRITES`.

`SOURCE` is a folder with `manifest.json` and the frames. `DEST` is the parent of `RX3_SPRITES`. When the pack goes on the stick, use the USB root as `DEST`.

Both commands take the same optional flags:

| Flag | Effect |
| --- | --- |
| `--format-proof PATH` | Must match the pack encoding: format 2, palette 1, 8 bpp, transparent 0, entry 2. Without this file the command writes that format. |
| `--anchors PATH` | Explicit anchor proof. Without this file the command reads `DEST/RX3_RUNTIME/sprite-trace.log` and writes an anchor for each stable waveform. If that log has none, the command warns and the player draws nothing. |
| `-v`, `--verbose` | Summary on stderr. A second `-v` also names each PNG. |
| `-q`, `--quiet` | Errors only. A second `-q` silences the log. |
| `--log-level LEVEL` | One of `debug`, `info`, `warning`, or `error`. This flag overrides `-v` and `-q`. |

Diagnostics go to stderr. A bad source or a bad path exits with status 2.

Pack layout and the player rules are in [Animated sprites](docs/sprites.md).

## autoexec.bin

```sh
python -m tools.rx3_runtime list
python -m tools.rx3_runtime build --key KEY --output DIRECTORY
python -m tools.rx3_runtime build --key KEY --output DIRECTORY --patch sprites --patch logging
```

`list` prints each module you can select. Each line starts with the module id, then `default` or `optional`.

`build` writes `DIRECTORY/autoexec.bin`. `DIRECTORY` must already exist. `KEY` is your key file. This tree does not ship that file.

If you omit `--patch`, the build includes every default module. If you pass `--patch`, those values replace the defaults. Each `--patch` names one module. The command includes a required module, such as `core`, without a flag. The default set omits `sprites`. A `--patch sprites` entry includes the overlay. A default module stays in that build only when its id is also a `--patch` value.

`--firmware` selects the module set. The default is `1.19`. `--prebuilt-hook PATH` supplies `librx3_core.so`. If you omit it, the command compiles the core when a module needs that file.

A missing `--key` or `--output` exits with status 2. On success the command prints the file path, the byte size, and the SHA-256.

`make autoexec KEY=KEY MODULES="sprites logging"` runs this same build. The output directory is `build/`.

## Crash reports

```sh
python -m tools.rx3_crash REPORT --autoexec STICK/autoexec.bin --key KEY
python -m tools.rx3_crash REPORT --hook build/librx3_core.so
```

`REPORT` is a `RX3_RUNTIME/crash-NNNN.txt` file that the `logging` module writes. The command prints each crash in it: the signal, `pc` and `lr`, the registers, and the code addresses on the stack. It gives an address in the performance core as a function and a source line. That needs `llvm-symbolizer`, either on `PATH` or from Homebrew `llvm`. `--autoexec` reads the core from the stick image, so its symbols match the code that crashed. It prints an address in rbp without a name.

## Ghidra

```sh
python -m tools.rx3_ghidra --rbp STICK/RX3_RUNTIME/rbp-SHA1.rbp 0x1d3284 str:wave
```

With the `logging` module on, the player copies its stock `rbp` to `RX3_RUNTIME/rbp-<sha1>.rbp` once. The guarded words are put back first, and the copy is kept only if its SHA-1 is the accepted stock hash. This command runs headless Ghidra on that file and writes one C file per function to `local/ghidra/decompiled/`. Each file starts with the function's callers and callees. A target is an address (`0x...`, the function that contains it), a function name, or `str:TEXT` (each function that references a string containing `TEXT`). rbp addresses are the same numbers that the hook constants use.

The first run imports and analyses the binary. That takes some minutes. Later runs reuse `local/ghidra/project/` and only decompile. Ghidra comes from `--ghidra DIR`, from `GHIDRA_INSTALL_DIR`, or from Homebrew (`brew install ghidra`). Git ignores `local/`. Do not commit the binary or the output.
