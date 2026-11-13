#!/usr/bin/env bash
# scripts/tv.sh - install, run and watch Hotaru on an LG TV in Developer Mode, over SSH.
#
# Uses the TV's own luna-send-pub through the Developer Mode SSH server (port 9922, user
# prisoner), so neither ares-cli nor a webOS SDK is needed. The TV has no scp: files are
# streamed with cat. luna-send-pub only answers in interactive mode and while its stdin
# stays open, so calls keep it open and stop once the expected reply shows up.
#
#   scripts/tv.sh key [PASSPHRASE]     fetch the Developer Mode key (the passphrase is shown
#                                      in the TV's Developer Mode app; it changes with every session)
#   scripts/tv.sh install [IPK]        upload and install (default: the newest dist/*.ipk)
#   scripts/tv.sh launch [--probe]     start it; --probe turns diagnostics on for this run
#   scripts/tv.sh stop
#   scripts/tv.sh running              is it running
#   scripts/tv.sh log [-f]             print /tmp/hotaru.log (-f: follow it)
#   scripts/tv.sh save                 copy the log to out/tv-logs/<time>.log
#
# Env: WEBOS_HOST (default LGwebOSTV.local), WEBOS_PORT (9922), WEBOS_USER (prisoner),
#      WEBOS_KEY (~/.ssh/webos_rsa).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_ID="com.renjfk.hotaru"
WEBOS_HOST="${WEBOS_HOST:-LGwebOSTV.local}"
WEBOS_PORT="${WEBOS_PORT:-9922}"
WEBOS_USER="${WEBOS_USER:-prisoner}"
WEBOS_KEY="${WEBOS_KEY:-$HOME/.ssh/webos_rsa}"
TV_TMP="/media/developer/temp"
TV_LOG="/tmp/hotaru.log"
PROBE_FLAG="/tmp/hotaru-probe"   # src/main.c: probe mode for the run when it exists
LUNA="/usr/bin/luna-send-pub"

die() { echo "error: $*" >&2; exit 1; }

ssh_tv() {
    ssh -i "$WEBOS_KEY" \
        -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa \
        -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 \
        -p "$WEBOS_PORT" "$WEBOS_USER@$WEBOS_HOST" "$@"
}

# luna URI JSON DONE-REGEX FAIL-REGEX: one luna call; 0 once DONE matches, 1 on FAIL
luna() {
    local uri="$1" json="$2" done="$3" fail="$4" rc=0 tmp pid
    tmp="$(mktemp)"
    ssh_tv "sleep 300 | $LUNA -i '$uri' '$json' 2>&1" >"$tmp" 2>&1 &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        if grep -qE "$done" "$tmp"; then break; fi
        if grep -qE "$fail" "$tmp"; then rc=1; break; fi
        sleep 0.3
    done
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    cat "$tmp"
    rm -f "$tmp"
    return "$rc"
}

cmd_key() {
    mkdir -p "$(dirname "$WEBOS_KEY")"
    curl -fsS "http://$WEBOS_HOST:9991/webos_rsa" -o "$WEBOS_KEY" || die "no key server at $WEBOS_HOST:9991 (Developer Mode on?)"
    chmod 600 "$WEBOS_KEY"
    if [ -n "${1:-}" ] && grep -q ENCRYPTED "$WEBOS_KEY"; then
        ssh-keygen -p -P "$1" -N "" -f "$WEBOS_KEY" >/dev/null
    fi
    echo "key: $WEBOS_KEY"
}

cmd_install() {
    local ipk="${1:-}"
    if [ -z "$ipk" ]; then
        # shellcheck disable=SC2012 # the build names its packages: no odd characters to guard against
        ipk="$(ls -t "$ROOT"/dist/*.ipk 2>/dev/null | head -n1)"
        [ -n "$ipk" ] || die "no dist/*.ipk: run ./build.sh, or pass the .ipk"
    fi
    [ -f "$ipk" ] || die "no such file: $ipk"
    local remote
    remote="$TV_TMP/$(basename "$ipk")"
    echo "uploading $(basename "$ipk")"
    ssh_tv "cat > '$remote'" <"$ipk"
    echo "installing"
    if ! luna "luna://com.webos.appInstallService/dev/install" \
        "{\"id\":\"$APP_ID\",\"ipkUrl\":\"$remote\",\"subscribe\":true}" \
        '"state":"installed"' '"state":"[^"]*FAILED|"returnValue":false' >/dev/null; then
        ssh_tv "rm -f '$remote'" || true
        die "install failed"
    fi
    ssh_tv "rm -f '$remote'" || true
    echo "installed $APP_ID"
}

cmd_launch() {
    local params='{}'
    if [ "${1:-}" = "--probe" ]; then
        ssh_tv "touch $PROBE_FLAG"
        params='{"probe":true}'
    else
        ssh_tv "rm -f $PROBE_FLAG"
    fi
    luna "luna://com.webos.applicationManager/launch" "{\"id\":\"$APP_ID\",\"params\":$params}" \
        '"returnValue":true' '"returnValue":false' >/dev/null || die "launch failed"
    echo "launched $APP_ID${1:+ (probe mode)}"
}

cmd_stop() {
    luna "luna://com.webos.applicationManager/dev/closeByAppId" "{\"id\":\"$APP_ID\"}" \
        '"returnValue":true' '"returnValue":false' >/dev/null || true
    echo "stopped $APP_ID"
}

cmd_running() {
    if luna "luna://com.webos.applicationManager/dev/running" '{}' '"returnValue":true' '"returnValue":false' |
        grep -q "\"$APP_ID\""; then
        echo "running"
    else
        echo "not running"
        return 1
    fi
}

cmd_log() {
    if [ "${1:-}" = "-f" ]; then ssh_tv "tail -n 50 -f $TV_LOG"; else ssh_tv "cat $TV_LOG"; fi
}

cmd_save() {
    local dir="$ROOT/out/tv-logs" file
    mkdir -p "$dir"
    file="$dir/$(date +%Y%m%d-%H%M%S).log"
    ssh_tv "cat $TV_LOG" >"$file"
    echo "$file"
}

case "${1:-}" in
    key) shift; cmd_key "$@" ;;
    install) shift; cmd_install "$@" ;;
    launch) shift; cmd_launch "$@" ;;
    stop) cmd_stop ;;
    running) cmd_running ;;
    log) shift; cmd_log "$@" ;;
    save) cmd_save ;;
    *) sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
