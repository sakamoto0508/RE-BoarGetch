#!/bin/sh
# Log checks shared by run_ue_py.sh and run_ue_gui.sh (sourced, not run directly).

KNOWN_GFD_1='LogGameFeatures: Error: Asset manager settings do not include a rule for assets of type GameFeatureData'
KNOWN_GFD_2='LoadErrors: Error: Asset Manager settings do not include an entry for assets of type GameFeatureData'

# Succeeds when the log shows a Python exception, a crash or an assertion.
log_has_fatal() {
    grep -q -E 'Traceback \(most recent call last\)|LogPython: Error|Critical error|Assertion failed|Fatal error|Unhandled Exception' "$1"
}

# Succeeds only when exit code 1 is fully explained by the known GameFeatureData configuration errors:
# - the log was closed and has the shutdown summary ("Warning/Error Summary" ... "Failure - N error(s)")
# - the known errors are present, and every ": Error:" line in the log is one of them
# - the number of error lines before the summary equals N
# Logs without that summary (editor/PIE runs) cannot prove this, so they never qualify.
only_known_gamefeature_errors() {
    log=$1
    grep -q 'Log file closed' "$log" || return 1
    header=$(grep -n 'Warning/Error Summary (Unique only)' "$log" | tail -1 | cut -d: -f1)
    footer=$(grep -n 'LogInit: Display: Failure - [0-9]* error(s)' "$log" | tail -1)
    [ -n "$header" ] && [ -n "$footer" ] || return 1
    footer_line=${footer%%:*}
    [ "$footer_line" -gt "$header" ] || return 1
    count=$(printf '%s' "$footer" | sed -n 's/.*Failure - \([0-9]*\) error(s).*/\1/p')
    body=$(head -n $((header - 1)) "$log" | grep ': Error:')
    [ -n "$body" ] || return 1
    # Any error line, anywhere in the log, that is not a known GameFeatureData line disqualifies.
    grep ': Error:' "$log" | grep -v -F -e "$KNOWN_GFD_1" -e "$KNOWN_GFD_2" | grep -q . && return 1
    [ "$(printf '%s\n' "$body" | wc -l)" -eq "$count" ]
}
