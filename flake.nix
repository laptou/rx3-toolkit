{
  # What the build needs that macOS does not provide.
  #
  # `app/runtime/build.py` compiles the ARM performance core with
  # `-fuse-ld=lld`. Apple's clang rejects that argument outright ("invalid
  # linker name in argument '-fuse-ld=lld'"), so the core cannot be built with
  # /usr/bin/clang no matter what is installed. An upstream LLVM clang plus its
  # ELF linker is the fix: that pair cross-compiles to arm-linux-gnueabi with
  # `-nostdlib`, needs no Linux sysroot, and emits the EABI5 shared object the
  # player loads.
  #
  # Only the cross-compiler comes from here. The host is left alone on purpose:
  #
  # - `mkShellNoCC` keeps Nix's own `cc` off PATH. With it, `tests/` build
  #   native harnesses against the shell's inputs instead of the system SDK and
  #   `-lz` stops resolving.
  # - `clang-unwrapped` is reached through `$CC`, never through PATH. Nixpkgs'
  #   wrapped `clang` splices Darwin hardening flags into every call, and the
  #   armv7 Linux target rejects `-fzero-call-used-regs=used-gpr`.
  # - No `python3`. The repository uses a virtualenv for cryptography, pycdlib
  #   and the rest, and a Nix interpreter would win the Makefile's lookup with
  #   none of them installed.

  description = "XDJ-RX3 Toolkit: ARM hook cross-compiler";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "aarch64-darwin"
        "x86_64-darwin"
        "aarch64-linux"
        "x86_64-linux"
      ];
      forEachSystem = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forEachSystem (
        pkgs:
        let
          llvm = pkgs.llvmPackages;
        in
        {
          default = pkgs.mkShellNoCC {
            packages = [ llvm.lld ];

            shellHook = ''
              # `compile_arm_hook` and the Makefile take the compiler from $CC,
              # so naming it here reaches both. The path is absolute because
              # this driver is deliberately not on PATH.
              export CC="${llvm.clang-unwrapped}/bin/clang"

              # clang resolves `-fuse-ld=lld` by searching PATH for `ld.lld`,
              # which lives in a different store path from the driver.
              export LLD="${llvm.lld}/bin/ld.lld"

              # `make` and the command line take PYTHON from the environment, so
              # point them at the virtualenv that holds the dependencies rather
              # than at whichever interpreter comes first.
              if [ -x "$PWD/.venv/bin/python" ]; then
                export PYTHON="$PWD/.venv/bin/python"
              fi

              echo "rx3: ARM core via $($CC --version | head -1)"
              "$LLD" --version
              if command -v clang >/dev/null 2>&1; then
                echo "rx3: host clang $(clang --version | head -1) (unchanged)"
              fi
            '';
          };
        }
      );
    };
}
