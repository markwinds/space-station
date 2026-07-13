#!/usr/bin/env bash

set -Eeuo pipefail

# 交叉编译使用的 CMake preset，以及对应的本地产物路径。
readonly PRESET="bwg-x64-linux-release-vcpkg"
readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly SOURCE_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly LOCAL_BINARY="${SOURCE_DIR}/out/build/${PRESET}/space-station"

# 可通过环境变量覆盖 SSH 目标、远端程序路径和连接超时时间。
readonly SSH_TARGET="${BWG_SSH_TARGET:-bwg}"
readonly REMOTE_PATH="${BWG_REMOTE_PATH:-/root/ss/space-station}"
readonly REMOTE_DIR="$(dirname -- "${REMOTE_PATH}")"
readonly REMOTE_NAME="$(basename -- "${REMOTE_PATH}")"
readonly REMOTE_TMP="${REMOTE_DIR}/.${REMOTE_NAME}.upload.$$"
readonly REMOTE_LOG="${BWG_REMOTE_LOG:-${REMOTE_DIR}/nohup.out}"
readonly REMOTE_PID="${BWG_REMOTE_PID:-${REMOTE_PATH}.pid}"
readonly SSH_CONNECT_TIMEOUT="${BWG_SSH_CONNECT_TIMEOUT:-10}"

ssh_options=(
    -o "ConnectTimeout=${SSH_CONNECT_TIMEOUT}"
    -o ServerAliveInterval=10
    -o ServerAliveCountMax=3
)

# 本地流程中途失败时，尽量清理已经上传的临时文件。
cleanup_remote_upload() {
    ssh "${ssh_options[@]}" "${SSH_TARGET}" rm -f -- "${REMOTE_TMP}" >/dev/null 2>&1 || true
}
trap cleanup_remote_upload ERR INT TERM

# 每次部署前重新配置并构建，确保上传的是当前源码生成的程序。
echo "==> Configuring ${PRESET}"
(
    cd -- "${SOURCE_DIR}"
    cmake --preset "${PRESET}"
)

echo "==> Building ${LOCAL_BINARY}"
(
    cd -- "${SOURCE_DIR}"
    cmake --build --preset "${PRESET}"
)

if [[ ! -x "${LOCAL_BINARY}" ]]; then
    echo "Built executable was not found: ${LOCAL_BINARY}" >&2
    exit 1
fi

# 确认远端部署目录存在，防止 scp 把路径解释成意外的文件名。
ssh "${ssh_options[@]}" "${SSH_TARGET}" test -d "${REMOTE_DIR}"

# 先上传到程序目录内的临时路径，避免上传过程覆盖正在运行的程序。
echo "==> Uploading to ${SSH_TARGET}:${REMOTE_TMP}"
scp "${ssh_options[@]}" "${LOCAL_BINARY}" "${SSH_TARGET}:${REMOTE_TMP}"

# 上传成功后，在同一个远端会话中停止旧程序、替换文件并启动新程序。
echo "==> Stopping the old process and starting ${REMOTE_PATH}"
ssh "${ssh_options[@]}" "${SSH_TARGET}" sh -s -- \
    "${REMOTE_PATH}" "${REMOTE_TMP}" "${REMOTE_LOG}" "${REMOTE_PID}" <<'REMOTE_SCRIPT'
set -eu

remote_path=$1
remote_tmp=$2
remote_log=$3
remote_pid=$4

# 通过 /proc/<pid>/exe 精确匹配程序路径，避免误杀名称相近的进程。
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

    # 先发送 SIGTERM，让程序正常停止并释放端口等资源。
    kill -TERM $old_pids

    # 最多等待约 10 秒；超时后使用 SIGKILL，防止部署一直阻塞。
    attempts=0
    while [ "$attempts" -lt 50 ]; do
        remaining_pids=$(find_running_pids)
        [ -n "$remaining_pids" ] || break
        sleep 0.2
        attempts=$((attempts + 1))
    done

    remaining_pids=$(find_running_pids)
    if [ -n "$remaining_pids" ]; then
        echo "Old process did not stop in time; sending SIGKILL: $(printf '%s' "$remaining_pids" | tr '\n' ' ')"
        kill -KILL $remaining_pids
    fi
else
    echo "No running ${remote_path} process found"
fi

# 旧程序停止后再原子替换可执行文件。
rm -f -- "$remote_pid"
chmod 0755 "$remote_tmp"
mv -f -- "$remote_tmp" "$remote_path"

# 后台启动新程序，并保存日志与 PID，便于后续排查和管理。
cd -- "$(dirname -- "$remote_path")"
nohup "$remote_path" >>"$remote_log" 2>&1 </dev/null &
new_pid=$!
printf '%s\n' "$new_pid" >"$remote_pid"

# 等待一秒检查启动结果；若进程已经退出，则输出最近的日志。
sleep 1
if ! kill -0 "$new_pid" 2>/dev/null; then
    echo "New process exited during startup. Last log lines:" >&2
    tail -n 50 "$remote_log" >&2 || true
    rm -f -- "$remote_pid"
    exit 1
fi

echo "Started ${remote_path} with PID ${new_pid}; log: ${remote_log}"
REMOTE_SCRIPT

trap - ERR INT TERM
echo "==> Deployment completed"
