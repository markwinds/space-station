#!/usr/bin/env bash

set -Eeuo pipefail

# 远端主程序使用 Linux 交叉编译产物，差分上传客户端在本机运行。
readonly TARGET_PRESET="bwg-x64-linux-release-vcpkg"
readonly CLIENT_PRESET="macos-release-vcpkg"
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly SOURCE_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly LOCAL_BINARY="${SOURCE_DIR}/out/build/${TARGET_PRESET}/space-station"
readonly LOCAL_CLIENT="${SOURCE_DIR}/out/build/${CLIENT_PRESET}/space-transfer-client"

# 可通过环境变量覆盖 SSH 目标、远端路径、端口和证书位置。
readonly SSH_TARGET="${BWG_SSH_TARGET:-bwg}"
readonly REMOTE_PATH="${BWG_REMOTE_PATH:-/root/ss/space-station}"
readonly REMOTE_DIR="$(dirname -- "${REMOTE_PATH}")"
readonly REMOTE_LOG="${BWG_REMOTE_LOG:-${REMOTE_DIR}/nohup.out}"
readonly REMOTE_PID="${BWG_REMOTE_PID:-${REMOTE_PATH}.pid}"
readonly SSH_CONNECT_TIMEOUT="${BWG_SSH_CONNECT_TIMEOUT:-10}"

readonly CHUNK_SIZE="${BWG_TRANSFER_CHUNK_SIZE:-65536}"
readonly COMPRESSION_MODE="${BWG_TRANSFER_COMPRESSION_MODE:-stream}"
readonly REMOTE_MANAGEMENT_PORT="${BWG_MANAGEMENT_PORT:-8443}"
readonly REMOTE_TRANSFER_PORT="${BWG_TRANSFER_PORT:-9443}"
readonly LOCAL_MANAGEMENT_PORT="${BWG_LOCAL_MANAGEMENT_PORT:-18443}"
readonly LOCAL_TRANSFER_PORT="${BWG_LOCAL_TRANSFER_PORT:-19443}"
readonly TRANSFER_SERVER_NAME="${BWG_TRANSFER_SERVER_NAME:-markwinds.top}"
readonly CLIENT_CERT="${BWG_TRANSFER_CLIENT_CERT:-/Volumes/samsung/data/cert/client/signed-certificate.pem}"
readonly CLIENT_KEY="${BWG_TRANSFER_CLIENT_KEY:-/Volumes/samsung/data/cert/client/private-key.pem}"
readonly SERVER_CA="${BWG_TRANSFER_SERVER_CA:-/Volumes/samsung/data/cert/root/certificate.pem}"
readonly REMOTE_SERVER_CERT="${BWG_TRANSFER_REMOTE_SERVER_CERT:-data/tls/server.crt}"
readonly REMOTE_SERVER_KEY="${BWG_TRANSFER_REMOTE_SERVER_KEY:-data/tls/server.key}"
readonly REMOTE_CLIENT_CA="${BWG_TRANSFER_REMOTE_CLIENT_CA:-data/tls/root.crt}"

readonly CONTROL_SOCKET="/tmp/space-station-bwg-deploy.$$.sock"
readonly TRANSFER_LOG="/tmp/space-station-bwg-transfer.$$.jsonl"

ssh_options=(
    -o "ConnectTimeout=${SSH_CONNECT_TIMEOUT}"
    -o ServerAliveInterval=10
    -o ServerAliveCountMax=3
)

tunnel_started=0
transfer_started=0

now_ms() {
    python3 -c 'import time; print(time.time_ns() // 1_000_000)'
}

format_duration() {
    awk -v milliseconds="$1" 'BEGIN { printf "%.3f 秒", milliseconds / 1000 }'
}

format_bytes() {
    awk -v bytes="$1" 'BEGIN {
        if (bytes >= 1073741824) printf "%.2f GiB", bytes / 1073741824;
        else if (bytes >= 1048576) printf "%.2f MiB", bytes / 1048576;
        else if (bytes >= 1024) printf "%.2f KiB", bytes / 1024;
        else printf "%d B", bytes;
    }'
}

cleanup() {
    if [[ "${transfer_started}" -eq 1 && "${tunnel_started}" -eq 1 ]]; then
        api_request /api/tools/transfer/server/stop -X POST >/dev/null 2>&1 || true
    fi
    if [[ "${tunnel_started}" -eq 1 ]]; then
        ssh "${ssh_options[@]}" -S "${CONTROL_SOCKET}" -O exit "${SSH_TARGET}" >/dev/null 2>&1 || true
    fi
    rm -f -- "${CONTROL_SOCKET}" "${TRANSFER_LOG}"
}
trap cleanup EXIT

api_request() {
    local path=$1
    shift
    curl \
        --noproxy '*' \
        --fail \
        --silent \
        --show-error \
        --connect-timeout 10 \
        --resolve "${TRANSFER_SERVER_NAME}:${LOCAL_MANAGEMENT_PORT}:127.0.0.1" \
        --cacert "${SERVER_CA}" \
        --cert "${CLIENT_CERT}" \
        --key "${CLIENT_KEY}" \
        "$@" \
        "https://${TRANSFER_SERVER_NAME}:${LOCAL_MANAGEMENT_PORT}${path}"
}

for required_file in "${CLIENT_CERT}" "${CLIENT_KEY}" "${SERVER_CA}"; do
    if [[ ! -f "${required_file}" ]]; then
        echo "Required TLS file was not found: ${required_file}" >&2
        exit 1
    fi
done

if [[ "${CHUNK_SIZE}" -lt 65536 || "${CHUNK_SIZE}" -gt 16777216 ]]; then
    echo "BWG_TRANSFER_CHUNK_SIZE must be between 65536 and 16777216 bytes" >&2
    exit 1
fi
if [[ "${COMPRESSION_MODE}" != "chunk" && "${COMPRESSION_MODE}" != "stream" ]]; then
    echo "BWG_TRANSFER_COMPRESSION_MODE must be chunk or stream" >&2
    exit 1
fi

readonly TOTAL_STARTED_MS="$(now_ms)"

echo "==> Configuring and building target ${TARGET_PRESET}"
build_started_ms="$(now_ms)"
(
    cd -- "${SOURCE_DIR}"
    cmake --preset "${TARGET_PRESET}"
    cmake --build --preset "${TARGET_PRESET}"
)

echo "==> Configuring and building local client ${CLIENT_PRESET}"
(
    cd -- "${SOURCE_DIR}"
    cmake --preset "${CLIENT_PRESET}"
    cmake --build --preset "${CLIENT_PRESET}" --target space-transfer-client
)
build_elapsed_ms=$(( $(now_ms) - build_started_ms ))

if [[ ! -x "${LOCAL_BINARY}" ]]; then
    echo "Built target executable was not found: ${LOCAL_BINARY}" >&2
    exit 1
fi
if [[ ! -x "${LOCAL_CLIENT}" ]]; then
    echo "Built local transfer client was not found: ${LOCAL_CLIENT}" >&2
    exit 1
fi

target_file_description="$(file "${LOCAL_BINARY}")"
if [[ "${target_file_description}" == *"not stripped"* ]]; then
    echo "Release target executable still contains removable symbols: ${target_file_description}" >&2
    exit 1
fi

ssh "${ssh_options[@]}" "${SSH_TARGET}" test -d "${REMOTE_DIR}"

echo "==> Opening authenticated SSH tunnels"
service_started_ms="$(now_ms)"
rm -f -- "${CONTROL_SOCKET}"
ssh "${ssh_options[@]}" \
    -M -S "${CONTROL_SOCKET}" \
    -o ExitOnForwardFailure=yes \
    -fN \
    -L "${LOCAL_MANAGEMENT_PORT}:127.0.0.1:${REMOTE_MANAGEMENT_PORT}" \
    -L "${LOCAL_TRANSFER_PORT}:127.0.0.1:${REMOTE_TRANSFER_PORT}" \
    "${SSH_TARGET}"
tunnel_started=1

# 差分服务只监听远端 loopback，通过 SSH 隧道访问；接收目录和片段目录均为程序目录。
transfer_config="$({
    python3 - \
        "${REMOTE_DIR}" \
        "${REMOTE_TRANSFER_PORT}" \
        "${REMOTE_SERVER_CERT}" \
        "${REMOTE_SERVER_KEY}" \
        "${REMOTE_CLIENT_CA}" <<'PY'
import json
import sys

remote_dir, port, certificate, private_key, client_ca = sys.argv[1:]
print(json.dumps({
    "configVersion": 1,
    "listenAddress": "127.0.0.1",
    "tlsEnabled": True,
    "tlsPort": int(port),
    "plainEnabled": False,
    "plainPort": 9080,
    "certificatePath": certificate,
    "privateKeyPath": private_key,
    "clientCaPath": client_ca,
    "destinationRoot": remote_dir,
    "basisRoots": [remote_dir],
    "sameNameMatchThreshold": 0.5,
    "overwrite": True,
}, separators=(",", ":")))
PY
})"

echo "==> Configuring and starting remote differential-transfer server"
api_request /api/tools/transfer/server/config \
    -X PUT -H 'Content-Type: application/json' -d "${transfer_config}" >/dev/null
api_request /api/tools/transfer/server/start -X POST >/dev/null
transfer_started=1
service_elapsed_ms=$(( $(now_ms) - service_started_ms ))

echo "==> Differential upload: chunk size ${CHUNK_SIZE} bytes, compression ${COMPRESSION_MODE}"
transfer_started_ms="$(now_ms)"
(
    cd -- "${SOURCE_DIR}"
    "${LOCAL_CLIENT}" \
        --host 127.0.0.1 \
        --port "${LOCAL_TRANSFER_PORT}" \
        --chunk-size "${CHUNK_SIZE}" \
        --compression "${COMPRESSION_MODE}" \
        --cert "${CLIENT_CERT}" \
        --key "${CLIENT_KEY}" \
        --server-ca "${SERVER_CA}" \
        --server-name "${TRANSFER_SERVER_NAME}" \
        "${LOCAL_BINARY}"
) | tee "${TRANSFER_LOG}" | python3 -c '
import json
import sys

last_stage = None
last_bucket = -1
for line in sys.stdin:
    item = json.loads(line)
    stage = item.get("stage", "")
    size = item.get("fileSize", 0)
    done = item.get("matchedBytes", 0) + item.get("uploadedBytes", 0)
    bucket = int(done * 10 / size) if size else 0
    if stage != last_stage or (stage == "uploading" and bucket > last_bucket):
        percent = min(100, done * 100 / size) if size else 0
        reused = item.get("matchedBytes", 0)
        uploaded = item.get("uploadedBytes", 0)
        wire = item.get("wireBytes", uploaded)
        compressed = item.get("compressedChunks", 0)
        mode = item.get("compressionMode", "none")
        print(f"    [{stage}] {percent:.1f}%  reused={reused}  missing={uploaded}  wire={wire}  compression={mode}:{compressed}", flush=True)
        last_stage = stage
        last_bucket = bucket
'
transfer_elapsed_ms=$(( $(now_ms) - transfer_started_ms ))

IFS=$'\t' read -r file_size matched_bytes uploaded_bytes wire_bytes compressed_chunks compression_mode < <(
    python3 - "${TRANSFER_LOG}" <<'PY'
import json
import sys

completed = None
with open(sys.argv[1], encoding="utf-8") as stream:
    for line in stream:
        item = json.loads(line)
        if item.get("stage") == "completed":
            completed = item
if completed is None:
    raise SystemExit("transfer did not produce a completed result")
uploaded = completed["uploadedBytes"]
print(completed["fileSize"], completed["matchedBytes"], uploaded,
      completed.get("wireBytes", uploaded), completed.get("compressedChunks", 0),
      completed.get("compressionMode", "none"), sep="\t")
PY
)

local_sha="$(shasum -a 256 "${LOCAL_BINARY}" | awk '{print $1}')"
remote_sha="$(ssh "${ssh_options[@]}" -S "${CONTROL_SOCKET}" "${SSH_TARGET}" sha256sum "${REMOTE_PATH}" | awk '{print $1}')"
if [[ "${local_sha}" != "${remote_sha}" ]]; then
    echo "Remote SHA-256 does not match local build" >&2
    exit 1
fi

api_request /api/tools/transfer/server/stop -X POST >/dev/null
transfer_started=0

echo "==> Restarting ${REMOTE_PATH}"
restart_started_ms="$(now_ms)"
ssh "${ssh_options[@]}" -S "${CONTROL_SOCKET}" "${SSH_TARGET}" sh -s -- \
    "${REMOTE_PATH}" "${REMOTE_LOG}" "${REMOTE_PID}" <<'REMOTE_SCRIPT'
set -eu

remote_path=$1
remote_log=$2
remote_pid=$3

find_running_pids() {
    for proc_dir in /proc/[0-9]*; do
        [ -d "$proc_dir" ] || continue
        executable=$(readlink "$proc_dir/exe" 2>/dev/null || true)
        if [ "$executable" = "$remote_path" ] || [ "$executable" = "$remote_path (deleted)" ]; then
            printf '%s\n' "${proc_dir##*/}"
        fi
    done
}

old_pids=$(find_running_pids)
if [ -n "$old_pids" ]; then
    echo "Stopping old process: $(printf '%s' "$old_pids" | tr '\n' ' ')"
    kill -TERM $old_pids
    attempts=0
    while [ "$attempts" -lt 50 ]; do
        remaining_pids=$(find_running_pids)
        [ -n "$remaining_pids" ] || break
        sleep 0.2
        attempts=$((attempts + 1))
    done
    remaining_pids=$(find_running_pids)
    if [ -n "$remaining_pids" ]; then
        kill -KILL $remaining_pids
    fi
fi

# 正常情况下差分服务会继承旧文件权限；这里再设一次，避免旧版本服务留下 0644 文件。
chmod 0755 "$remote_path"
rm -f -- "$remote_pid"
cd -- "$(dirname -- "$remote_path")"
nohup "$remote_path" >>"$remote_log" 2>&1 </dev/null &
new_pid=$!
printf '%s\n' "$new_pid" >"$remote_pid"
sleep 1
if ! kill -0 "$new_pid" 2>/dev/null; then
    echo "New process exited during startup. Last log lines:" >&2
    tail -n 50 "$remote_log" >&2 || true
    rm -f -- "$remote_pid"
    exit 1
fi
echo "Started ${remote_path} with PID ${new_pid}"
REMOTE_SCRIPT
restart_elapsed_ms=$(( $(now_ms) - restart_started_ms ))

echo "==> Waiting for mTLS health check"
health_started_ms="$(now_ms)"
health_response=""
for ((attempt = 0; attempt < 30; attempt++)); do
    if health_response="$(api_request /api/health 2>/dev/null)"; then
        break
    fi
    sleep 0.5
done
if [[ -z "${health_response}" ]]; then
    echo "Remote health check did not recover" >&2
    exit 1
fi
health_elapsed_ms=$(( $(now_ms) - health_started_ms ))
total_elapsed_ms=$(( $(now_ms) - TOTAL_STARTED_MS ))

reuse_percent="$(awk -v matched="${matched_bytes}" -v size="${file_size}" 'BEGIN { printf "%.2f", size ? matched * 100 / size : 100 }')"
upload_percent="$(awk -v uploaded="${uploaded_bytes}" -v size="${file_size}" 'BEGIN { printf "%.2f", size ? uploaded * 100 / size : 0 }')"
wire_percent="$(awk -v wire="${wire_bytes}" -v size="${file_size}" 'BEGIN { printf "%.2f", size ? wire * 100 / size : 0 }')"
compression_saved=$(( uploaded_bytes - wire_bytes ))
if (( compression_saved >= 0 )); then
    compression_effect="saved $(format_bytes "${compression_saved}") (${compression_saved} bytes)"
else
    compression_added=$(( -compression_saved ))
    compression_effect="added $(format_bytes "${compression_added}") (${compression_added} bytes)"
fi

echo
echo "==> Differential deployment completed"
echo "    Health: ${health_response}"
echo "    SHA-256: ${local_sha}"
echo "    File size: $(format_bytes "${file_size}") (${file_size} bytes)"
echo "    Reused payload: $(format_bytes "${matched_bytes}") (${matched_bytes} bytes, ${reuse_percent}%)"
echo "    Missing payload: $(format_bytes "${uploaded_bytes}") (${uploaded_bytes} bytes, ${upload_percent}%)"
echo "    Wire payload: $(format_bytes "${wire_bytes}") (${wire_bytes} bytes, ${wire_percent}%)"
echo "    Compression: ${compression_mode}, ${compressed_chunks} logical chunks, ${compression_effect}"
echo "    Chunk size: $(format_bytes "${CHUNK_SIZE}")"
echo "    Build: $(format_duration "${build_elapsed_ms}")"
echo "    Transfer service setup: $(format_duration "${service_elapsed_ms}")"
echo "    Hash/scan/upload/verify: $(format_duration "${transfer_elapsed_ms}")"
echo "    Restart: $(format_duration "${restart_elapsed_ms}")"
echo "    Health check: $(format_duration "${health_elapsed_ms}")"
echo "    Total: $(format_duration "${total_elapsed_ms}")"
