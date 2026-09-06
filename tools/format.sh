#!/bin/sh
# Format the C sources with the project's .clang-format.
#
#   tools/format.sh          rewrite in place
#   tools/format.sh --check  exit non-zero if anything would change (CI)
#
# clang-format's output changes between releases, so one release is pinned
# here and installed the same way locally and on CI:
#
#   python3 -m pip install clang-format==18.1.8
#
# Set CLANG_FORMAT to use another binary.  Generated data under src/gen is
# excluded by its own .clang-format; the shaders are GLSL and not touched.
set -eu
cd "$(dirname "$0")/.."
CF=${CLANG_FORMAT:-clang-format}
files=$(find src contract -name '*.c' -o -name '*.h' | grep -v '^src/gen/' | sort)
if [ "${1:-}" = "--check" ]; then
    # shellcheck disable=SC2086
    $CF --dry-run --Werror $files && echo "formatting: clean"
else
    # shellcheck disable=SC2086
    $CF -i $files
    echo "formatted $(echo "$files" | wc -l | tr -d ' ') files"
fi
