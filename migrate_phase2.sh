#!/bin/bash
# Phase 2 Migration Helper Script
# This script helps identify functions that need ctx parameter

echo "=== Functions with E. references ==="
echo

# Find all function definitions and count E. references in each
grep -n "^void\|^static void\|^int\|^static int\|^char" src/loki_core.c | while IFS=: read -r linenum funcdef; do
    # Get next function's line number
    nextline=$(grep -n "^void\|^static void\|^int\|^static int\|^char" src/loki_core.c | grep -A1 "^$linenum:" | tail -1 | cut -d: -f1)

    if [ -z "$nextline" ]; then
        nextline=$(wc -l < src/loki_core.c)
    fi

    # Count E. references in this function
    count=$(sed -n "${linenum},${nextline}p" src/loki_core.c | grep -c "E\.")

    if [ "$count" -gt 0 ]; then
        funcname=$(echo "$funcdef" | sed 's/^.*void //' | sed 's/^.*int //' | sed 's/^.*char //' | cut -d'(' -f1 | xargs)
        echo "Line $linenum: $funcname ($count E. references)"
    fi
done

echo
echo "=== Summary ==="
echo "Total E. references in loki_core.c: $(grep -c 'E\.' src/loki_core.c)"
