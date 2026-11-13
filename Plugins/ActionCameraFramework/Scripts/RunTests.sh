#!/usr/bin/env bash
# Action Camera Framework: runs the automation tests and checks the result (Phase 6B / B-9, README 22 章).
#
#   Scripts/RunTests.sh [options] [filter]
#
#   filter              Test name filter (default ActionCameraFramework). "+" separates several; "^Name$" is an exact name.
#   --context CONTEXT   game     (default) -game -NullRHI in the ACF test environment: the engine's empty map
#                                /Engine/Maps/Entry with the engine GameModeBase / PlayerController / DefaultPawn and
#                                GameInstance (nothing of the host project's game logic runs).
#                       host     -game -NullRHI on the host project's default map and GameMode (the pre-B-9 way).
#                       editor   editor context (no -game), -NullRHI.
#                       samples  rendered, on /ActionCameraFramework/Samples/L_ACFCameraTest (host project GameMode:
#                                the sample checks need a humanoid player pawn). Filter default .Samples.
#                       profile  like samples, with -ACFProfile. Filter default .Profile.Scenarios.
#   --repeat N          N separate runs (fresh process each).
#   --isolate           every matching test in its own process (order / state independence). Slow: ~20 s per test.
#   --list              print the matching test names and exit.
#   --project FILE      .uproject (default: the first one found above this plugin).
#   --engine DIR        engine root (default $UE_ROOT, else "C:/Program Files/Epic Games/UE_5.8").
#   --log-dir DIR       logs and summary.tsv (default <project>/Saved/Logs/ACFTests/<context>).
#
# A run passes only when: the editor process exits with 0, the log says "N tests performed" with N > 0, N tests report
# Result={Success}, none failed, and the log has no crash / ensure. The script exits with 1 if any run failed and
# writes one line per test and run to summary.tsv (run, result, test). Tests that skip themselves (wrong map, plugin not
# loaded) still report Success; the verdict line shows how many did.

set -u
export MSYS_NO_PATHCONV=1 # Git Bash: keep /Engine/... and /Game/... as they are

CONTEXT=game
FILTER=""
REPEAT=1
ISOLATE=0
LIST=0
PROJECT=""
ENGINE="${UE_ROOT:-C:/Program Files/Epic Games/UE_5.8}"
LOGDIR=""

while [ $# -gt 0 ]; do
	case "$1" in
		--context) CONTEXT="$2"; shift 2 ;;
		--repeat) REPEAT="$2"; shift 2 ;;
		--isolate) ISOLATE=1; shift ;;
		--list) LIST=1; shift ;;
		--project) PROJECT="$2"; shift 2 ;;
		--engine) ENGINE="$2"; shift 2 ;;
		--log-dir) LOGDIR="$2"; shift 2 ;;
		-h|--help) sed -n '2,30p' "$0"; exit 0 ;;
		*) FILTER="$1"; shift ;;
	esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -z "$PROJECT" ]; then
	DIR="$SCRIPT_DIR"
	while [ "$DIR" != "/" ] && [ -z "$PROJECT" ]; do
		DIR="$(dirname "$DIR")"
		PROJECT="$(ls "$DIR"/*.uproject 2>/dev/null | head -1)"
	done
fi
if [ -z "$PROJECT" ] || [ ! -f "$PROJECT" ]; then
	echo "No .uproject found above $SCRIPT_DIR (use --project)." >&2
	exit 2
fi
if command -v cygpath > /dev/null 2>&1; then
	PROJECT="$(cygpath -m "$PROJECT")" # Git Bash: the editor needs C:/... paths
fi
EDITOR_CMD="$ENGINE/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
if [ ! -f "$EDITOR_CMD" ]; then
	echo "UnrealEditor-Cmd.exe not found under $ENGINE (use --engine or UE_ROOT)." >&2
	exit 2
fi
PROJECT_DIR="$(dirname "$PROJECT")"
[ -z "$LOGDIR" ] && LOGDIR="$PROJECT_DIR/Saved/Logs/ACFTests/$CONTEXT"
mkdir -p "$LOGDIR"
LOGDIR="$(cd "$LOGDIR" && pwd)"
command -v cygpath > /dev/null 2>&1 && LOGDIR="$(cygpath -m "$LOGDIR")"

ARGS=(-unattended -nosplash -nosound)
case "$CONTEXT" in
	game)
		[ -z "$FILTER" ] && FILTER=ActionCameraFramework
		ARGS=("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase" -game -NullRHI
			"-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/Engine.GameInstance" "${ARGS[@]}") ;;
	host)
		[ -z "$FILTER" ] && FILTER=ActionCameraFramework
		ARGS=(-game -NullRHI "${ARGS[@]}") ;;
	editor)
		[ -z "$FILTER" ] && FILTER=ActionCameraFramework
		ARGS=(-NullRHI "${ARGS[@]}") ;;
	samples)
		[ -z "$FILTER" ] && FILTER=ActionCameraFramework.Samples
		ARGS=(/ActionCameraFramework/Samples/L_ACFCameraTest -game -windowed -resx=1280 -resy=720 "${ARGS[@]}") ;;
	profile)
		[ -z "$FILTER" ] && FILTER=ActionCameraFramework.Profile.Scenarios
		ARGS=(/ActionCameraFramework/Samples/L_ACFCameraTest -game -windowed -resx=1280 -resy=720 -ACFProfile "${ARGS[@]}") ;;
	*) echo "Unknown context $CONTEXT" >&2; exit 2 ;;
esac

# One editor process: run_editor <log> <automation command>. Returns the process exit code.
run_editor() {
	rm -f "$1"
	"$EDITOR_CMD" "$PROJECT" "${ARGS[@]}" "-ExecCmds=Automation $2" "-TestExit=Automation Test Queue Empty" "-abslog=$1" > /dev/null 2>&1
}

strip_time() { sed 's/^\[[^]]*\]\[[^]]*\]//'; }

# check_log <log> <exit code> <run label>: prints a one-line verdict, appends to summary.tsv, returns 0 when the run passed.
check_log() {
	local LOG="$1" CODE="$2" RUN="$3"
	local PERFORMED SUCCESS FAILED PROBLEMS
	PERFORMED=$(grep -oE "Automation Test Queue Empty [0-9]+ tests performed" "$LOG" 2>/dev/null | grep -oE "[0-9]+" | tail -1)
	PERFORMED=${PERFORMED:-0}
	SUCCESS=$(grep -c "Test Completed. Result={Success}" "$LOG" 2>/dev/null)
	FAILED=$(grep -cE "Test Completed. Result=\{(Fail|Failed)" "$LOG" 2>/dev/null)
	PROBLEMS=$(grep -cE "Fatal error|Unhandled Exception|Ensure condition failed|=== Critical error ===" "$LOG" 2>/dev/null)
	local SKIPPED
	SKIPPED=$(grep -cE "LogAutomationController: (Warning: )?Skipped:" "$LOG" 2>/dev/null) # passed without checking (wrong map / plugin missing)
	grep -E "Test Completed. Result=" "$LOG" 2>/dev/null | strip_time \
		| sed -E 's/.*Result=\{([A-Za-z]+)\}.*Path=\{([^}]*)\}.*/\1\t\2/' | sed "s/^/$RUN\t/" >> "$LOGDIR/summary.tsv"
	local VERDICT=PASS
	if [ "$CODE" -ne 0 ] || [ "$PERFORMED" -eq 0 ] || [ "$SUCCESS" -ne "$PERFORMED" ] || [ "$FAILED" -ne 0 ] || [ "$PROBLEMS" -ne 0 ]; then
		VERDICT=FAIL
	fi
	echo "$VERDICT  $RUN: $SUCCESS/$PERFORMED succeeded ($SKIPPED of them skipped), $FAILED failed, exit $CODE, crash/ensure $PROBLEMS  ($LOG)"
	grep -E "Test Completed. Result=\{(Fail|Failed)" "$LOG" 2>/dev/null | strip_time | sed 's/^/      /'
	[ "$VERDICT" = PASS ]
}

if [ "$LIST" -eq 1 ] || [ "$ISOLATE" -eq 1 ]; then
	LISTLOG="$LOGDIR/list.log"
	run_editor "$LISTLOG" "List" # lists the tests of this context without running them
	ALL=$(grep -E "LogAutomationCommandLine: Display: *	'" "$LISTLOG" | sed -E "s/.*	'(.*)'.*/\1/" | tr -d '\r' | sort -u)
	TESTS=""
	IFS='+' read -ra TERMS <<< "$FILTER"
	for TERM in "${TERMS[@]}"; do
		TESTS+=$(echo "$ALL" | grep -F -- "$TERM")$'\n' # substring match, like RunTests
	done
	TESTS=$(echo "$TESTS" | sed '/^$/d' | sort -u)
	if [ "$LIST" -eq 1 ]; then
		echo "$TESTS"
		exit 0
	fi
fi

: > "$LOGDIR/summary.tsv"
FAILURES=0
RUNS=0
for ((R = 1; R <= REPEAT; R++)); do
	if [ "$ISOLATE" -eq 1 ]; then
		while IFS= read -r TEST; do
			[ -z "$TEST" ] && continue
			SAFE=$(echo "$TEST" | tr -c 'A-Za-z0-9._-' '_')
			LOG="$LOGDIR/run${R}_$SAFE.log"
			run_editor "$LOG" "RunTests ^$TEST\$"
			CODE=$?
			RUNS=$((RUNS + 1))
			check_log "$LOG" "$CODE" "run$R:$TEST" || FAILURES=$((FAILURES + 1))
		done <<< "$TESTS"
	else
		LOG="$LOGDIR/run$R.log"
		run_editor "$LOG" "RunTests $FILTER"
		CODE=$?
		RUNS=$((RUNS + 1))
		check_log "$LOG" "$CODE" "run$R" || FAILURES=$((FAILURES + 1))
	fi
done

echo "== $CONTEXT, filter '$FILTER': $((RUNS - FAILURES))/$RUNS runs passed. Summary: $LOGDIR/summary.tsv"
[ "$FAILURES" -eq 0 ]
