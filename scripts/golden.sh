#!/usr/bin/env bash
# Golden-output harness for the CodeGenerator split.
#
# Compiles a corpus of ST samples with st2cpp and prints one SHA-256 per
# generated file. Refactoring the code generator must not change a single byte
# of generated C++, so this script is the gate: run it before a change, again
# after, and diff the two manifests.
#
#   scripts/golden.sh > /tmp/before.txt   # before the refactor
#   scripts/golden.sh > /tmp/after.txt    # after
#   diff /tmp/before.txt /tmp/after.txt   # must be empty
#
# A sample that fails to compile is reported as ERROR:<file> and its exit status
# recorded, so a change that breaks a case cannot hide behind a hash changing.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
st2cpp_bin="$repo/st2cpp"
outdir="${GOLDEN_OUTDIR:-/tmp/st2cpp-golden}"

if [[ ! -x "$st2cpp_bin" ]]; then
  echo "missing $st2cpp_bin - build first" >&2
  exit 1
fi

rm -rf "$outdir"
mkdir -p "$outdir"

samples=()
while IFS= read -r f; do
  samples+=("$f")
done < <(find "$repo/tests/st_samples" "$repo/examples" -name '*.st' -not -path '*/build/*' | sort)

for src in "${samples[@]}"; do
  rel="${src#"$repo"/}"
  # Flatten the sample name so two demos with the same basename do not collide.
  flat="${rel//\//_}"
  flat="${flat%.st}"
  dest="$outdir/$flat"
  mkdir -p "$dest"

  # Every mode the CLI offers, so a refactor cannot change one of them alone.
  # The modular project style only exists over a workspace, so the sample is
  # staged into its own directory and the whole directory is handed over.
  for mode in default strict caseSensitive modular; do
    out="$dest/${mode}.cpp"
    hpp="$dest/${mode}.hpp"
    case "$mode" in
      default)       args=() ;;
      strict)        args=(--strict) ;;
      caseSensitive) args=(--caseSensitive) ;;
      modular)
        mkdir -p "$dest/ws"
        cp "$src" "$dest/ws/"
        # --diag-vars opts the reflection table back in: it is opt-in (DiagVars.cpp
        # needs undoDiag.hpp and Boost), and without the flag the modular manifest
        # would silently stop covering it.
        args=(--workspace "$dest/ws" --project-style --diag-vars --output-dir "$dest/ws/out")
        out="$dest/modular.manifest"
        ;;
    esac
    # The single-file CLI derives the header path from the input name and ignores
    # --output-dir, so it lands in the current directory unless -H says otherwise.
    # Pinning it here keeps the tree clean and puts the header under test: the
    # code generator emits the declarations, so the header is half the output.
    if [[ "$mode" != modular ]]; then
      args+=(-H "$hpp")
    fi
    if "$st2cpp_bin" "$src" "${args[@]}" -o "$out" >/dev/null 2>"$dest/${mode}.stderr"; then
      # The modular style writes a tree, so the manifest is every file it
      # produced: path and content hash, in a stable order.
      if [[ "$mode" == modular ]]; then
        while IFS= read -r f; do
          echo "OK  $flat/$mode/$(basename "$f")  $(sha256sum "$f" | cut -d' ' -f1)"
        done < <(find "$dest/ws/out" -type f | sort)
      else
        echo "OK  $flat/$mode.cpp  $(sha256sum "$out" | cut -d' ' -f1)"
        echo "OK  $flat/$mode.hpp  $(sha256sum "$hpp" | cut -d' ' -f1)"
      fi
    else
      status=$?
      echo "ERR $flat/$mode  exit=$status"
      sed 's/^/       | /' "$dest/${mode}.stderr"
    fi
  done
done

# Diagnostics are part of the observable behaviour: a refactor must not move
# them, so they are hashed too.
echo "--- diagnostics ---"
while IFS= read -r d; do
  echo "DIAG $(basename "$d")  $(sha256sum "$d" | cut -d' ' -f1)"
done < <(find "$outdir" -name '*.stderr' | sort)
