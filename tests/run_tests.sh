#!/bin/sh
#
# Runs vclpp over each test case and compares what it does with what is expected.
#
# A case is a directory under tests/<group>/ holding:
#   input.vcl        The file to preprocess, plus whatever files it includes.
#   args             Optional: extra command-line options, on one line.
#   expected.vsm     The expected output. If present, vclpp must succeed and write
#                    exactly this; if not, it must fail and write nothing.
#   expected.stderr  Optional: the expected diagnostics. Without it, there must be none.
#
# vclpp runs from inside the case directory, so file names in diagnostics are short.
#
# Usage: tests/run_tests.sh <path to vclpp> [--update]
#   --update  Rewrite each case's expected files from this run instead of comparing.
#

set -u

if [ $# -lt 1 ]; then
    echo "usage: $0 <path to vclpp> [--update]"
    exit 1
fi

vclpp=$1
update=0
if [ "${2:-}" = "--update" ]; then
    update=1
fi

tests_dir=$(cd "$(dirname "$0")" && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

passed=0
failed=0

for case_dir in "$tests_dir"/*/*/; do
    case_dir=${case_dir%/}
    [ -f "$case_dir/input.vcl" ] || continue
    name=${case_dir#"$tests_dir"/}

    args=""
    if [ -f "$case_dir/args" ]; then
        args=$(cat "$case_dir/args")
    fi

    rm -f "$work_dir/out.vsm"
    : > "$work_dir/diff"
    # $args is unquoted on purpose: it is a list of options.
    # shellcheck disable=SC2086
    (cd "$case_dir" && "$vclpp" input.vcl "$work_dir/out.vsm" $args 2> "$work_dir/stderr")
    status=$?

    if [ $update -eq 1 ]; then
        if [ $status -eq 0 ]; then
            cp "$work_dir/out.vsm" "$case_dir/expected.vsm"
        else
            rm -f "$case_dir/expected.vsm"
        fi
        if [ -s "$work_dir/stderr" ]; then
            cp "$work_dir/stderr" "$case_dir/expected.stderr"
        else
            rm -f "$case_dir/expected.stderr"
        fi
        echo "updated $name"
        continue
    fi

    problem=""
    if [ -f "$case_dir/expected.vsm" ]; then
        if [ $status -ne 0 ]; then
            problem="failed with status $status"
            cp "$work_dir/stderr" "$work_dir/diff"
        elif ! diff -u "$case_dir/expected.vsm" "$work_dir/out.vsm" > "$work_dir/diff"; then
            problem="output differs from expected.vsm"
        fi
    else
        if [ $status -eq 0 ]; then
            problem="succeeded, but was expected to fail"
        elif [ -f "$work_dir/out.vsm" ]; then
            problem="failed, but wrote an output file"
        fi
    fi

    if [ -z "$problem" ]; then
        expected_stderr="$case_dir/expected.stderr"
        [ -f "$expected_stderr" ] || expected_stderr=/dev/null
        if ! diff -u "$expected_stderr" "$work_dir/stderr" > "$work_dir/diff"; then
            problem="diagnostics differ from expected.stderr"
        fi
    fi

    if [ -z "$problem" ]; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        echo "FAIL $name: $problem"
        cat "$work_dir/diff"
    fi
done

if [ $update -eq 1 ]; then
    exit 0
fi

echo "$passed passed, $failed failed."
[ $failed -eq 0 ]
