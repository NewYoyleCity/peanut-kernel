#!/bin/bash
# cloc.sh - Peanut Kernel Line of Code Counter
# Counts lines of code in the kernel source with various options.
#
# Usage:
#   ./tools/cloc.sh              Count total LoC (default)
#   ./tools/cloc.sh --total      Count total LoC (same as default)
#   ./tools/cloc.sh --files      Count LoC of each file
#   ./tools/cloc.sh --languages  Count LoC per language
#   ./tools/cloc.sh --logical    Count logical LoC (semicolons + blocks)
#   ./tools/cloc.sh --comments   Count comment lines only
#   ./tools/cloc.sh --blanks     Count blank lines only
#   ./tools/cloc.sh --all        Full breakdown
#   ./tools/cloc.sh --help       Show this help

KERNEL_SRC="src include"
LOGICAL=0
FILES=0
LANGUAGES=0
COMMENTS=0
BLANKS=0
ALL=0
TOTAL=1

usage() {
    head -20 "$0" | grep '^#' | sed 's/^#//'
    exit 0
}

[[ $# -eq 0 ]] && TOTAL=1

for arg in "$@"; do
    case "$arg" in
        --total)    TOTAL=1 ;;
        --files)    FILES=1; TOTAL=0 ;;
        --languages) LANGUAGES=1; TOTAL=0 ;;
        --logical)  LOGICAL=1; TOTAL=0 ;;
        --comments) COMMENTS=1; TOTAL=0 ;;
        --blanks)   BLANKS=1; TOTAL=0 ;;
        --all)      ALL=1; TOTAL=0 ;;
        --help|-h)  usage ;;
        *)          echo "Unknown option: $arg"; usage ;;
    esac
done

if [[ $ALL -eq 1 ]]; then
    TOTAL=1; FILES=1; LANGUAGES=1; LOGICAL=1; COMMENTS=1; BLANKS=1;
fi

cd "$(dirname "$0")/.." || exit 1

find_sources() {
    find $KERNEL_SRC -type f \( \
        -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \
        -o -name '*.asm' -o -name '*.S' -o -name '*.ld' -o -name '*.py' \
        -o -name '*.sh' -o -name '*.psf' \
    \) ! -path 'include/generated/*' ! -path 'include/config/auto*' | sort
}

total_loc() {
    find_sources | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}'
}

files_loc() {
    find_sources | while read -r f; do
        lines=$(wc -l < "$f")
        printf "%6d  %s\n" "$lines" "$f"
    done
}

languages_loc() {
    echo "Language        Lines"
    echo "--------------- ------"
    for ext in c cpp h hpp asm S ld py sh; do
        pattern="*.${ext}"
        total=0
        while IFS= read -r -d '' f; do
            lines=$(wc -l < "$f" 2>/dev/null)
            total=$((total + lines))
        done < <(find $KERNEL_SRC -name "$pattern" ! -path 'include/generated/*' ! -path 'include/config/auto*' -print0 2>/dev/null)
        case "$ext" in
            c)   name="C" ;;
            cpp) name="C++" ;;
            h)   name="C Header" ;;
            hpp) name="C++ Header" ;;
            asm) name="Assembly" ;;
            S)   name="Assembly (preproc)" ;;
            ld)  name="Linker Script" ;;
            py)  name="Python" ;;
            sh)  name="Shell" ;;
            *)   name="$ext" ;;
        esac
        [[ $total -gt 0 ]] && printf "%-15s %6d\n" "$name" "$total"
    done
}

logical_loc() {
    total=0
    while IFS= read -r -d '' f; do
        case "$f" in
            *.c|*.cpp|*.h|*.hpp)
                # Count statements (semicolons at end of line) + block starts
                count=$(grep -c '[;{}]' "$f" 2>/dev/null)
                total=$((total + count))
                ;;
            *.asm|*.S)
                # Count instructions (non-label, non-comment, non-blank)
                count=$(grep -cE '^\s+[a-zA-Z]' "$f" 2>/dev/null)
                total=$((total + count))
                ;;
        esac
    done < <(find $KERNEL_SRC -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.asm' -o -name '*.S' \) ! -path 'include/generated/*' -print0 2>/dev/null)
    echo "$total"
}

comments_loc() {
    total=0
    while IFS= read -r -d '' f; do
        case "$f" in
            *.c|*.cpp|*.h|*.hpp|*.asm|*.S)
            count=$(grep -cE '^\s*(//|#|;|/\*|\*|#\s)' "$f" 2>/dev/null)
            total=$((total + count))
            ;;
        esac
    done < <(find $KERNEL_SRC -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.asm' -o -name '*.S' \) ! -path 'include/generated/*' -print0 2>/dev/null)
    echo "$total"
}

blanks_loc() {
    total=0
    while IFS= read -r -d '' f; do
        count=$(grep -c '^[[:space:]]*$' "$f" 2>/dev/null)
        total=$((total + count))
    done < <(find $KERNEL_SRC -type f ! -path 'include/generated/*' ! -path 'include/config/auto*' -print0 2>/dev/null)
    echo "$total"
}

[[ $TOTAL -eq 1 ]] && echo "Total LoC: $(total_loc)"
[[ $FILES -eq 1 ]] && { echo ""; echo "=== Lines per file ==="; files_loc; }
[[ $LANGUAGES -eq 1 ]] && { echo ""; echo "=== Lines per language ==="; languages_loc; }
[[ $LOGICAL -eq 1 ]] && echo "Logical LoC: $(logical_loc)"
[[ $COMMENTS -eq 1 ]] && echo "Comment lines: $(comments_loc)"
[[ $BLANKS -eq 1 ]] && echo "Blank lines: $(blanks_loc)"
