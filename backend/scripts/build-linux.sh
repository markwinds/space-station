#!/usr/bin/env bash

set -Eeuo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly BACKEND_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
readonly PROJECT_DIR="$(cd -- "${BACKEND_DIR}/.." && pwd)"
readonly FRONTEND_DIR="${PROJECT_DIR}/frontend"

preset="${SPACE_STATION_BUILD_PRESET:-linux-release-vcpkg}"
jobs="${SPACE_STATION_BUILD_JOBS:-}"
build_frontend=1
install_frontend=0
run_tests=0
sync_plugins=1
target=""

usage() {
    cat <<'EOF'
用法：build-linux.sh [选项]

默认构建前端和 Linux Release 后端，并将 config/plugins 同步到构建目录。

选项：
  --debug                 使用 linux-debug-vcpkg
  --release               使用 linux-release-vcpkg（默认）
  --preset NAME           使用指定的 CMake preset
  --jobs N, -j N          最大并行任务数；默认由构建工具决定
  --target NAME           只构建指定的 CMake target
  --backend-only          跳过前端构建，使用现有 frontend/dist
  --install-frontend      构建前先执行 npm ci
  --test                  构建完成后运行 CTest
  --no-sync-plugins       不同步 backend/config/plugins
  --help, -h              显示帮助

环境变量：
  VCPKG_ROOT                    vcpkg 根目录（必需）
  SPACE_STATION_BUILD_PRESET    默认 CMake preset
  SPACE_STATION_BUILD_JOBS      默认并行任务数
EOF
}

fail() {
    echo "错误：$*" >&2
    exit 1
}

require_value() {
    local option=$1
    local value=${2-}
    [[ -n "${value}" ]] || fail "${option} 缺少参数"
}

while (($# > 0)); do
    case "$1" in
        --debug)
            preset="linux-debug-vcpkg"
            ;;
        --release)
            preset="linux-release-vcpkg"
            ;;
        --preset)
            require_value "$1" "${2-}"
            preset=$2
            shift
            ;;
        --jobs|-j)
            require_value "$1" "${2-}"
            jobs=$2
            shift
            ;;
        --target)
            require_value "$1" "${2-}"
            target=$2
            shift
            ;;
        --backend-only)
            build_frontend=0
            ;;
        --install-frontend)
            install_frontend=1
            ;;
        --test)
            run_tests=1
            ;;
        --no-sync-plugins)
            sync_plugins=0
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            fail "未知参数：$1（使用 --help 查看帮助）"
            ;;
    esac
    shift
done

[[ "$(uname -s)" == "Linux" ]] || fail "该脚本只能在 Linux 下运行"

for command_name in cmake ninja npm; do
    command -v "${command_name}" >/dev/null 2>&1 || fail "找不到命令：${command_name}"
done

[[ -n "${VCPKG_ROOT:-}" ]] || fail "请先设置 VCPKG_ROOT，例如：export VCPKG_ROOT=/path/to/vcpkg"
readonly VCPKG_TOOLCHAIN="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
[[ -f "${VCPKG_TOOLCHAIN}" ]] || fail "找不到 vcpkg 工具链：${VCPKG_TOOLCHAIN}"

if [[ -n "${jobs}" && ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
    fail "并行任务数必须是正整数：${jobs}"
fi
if [[ "${run_tests}" -eq 1 && -n "${target}" ]]; then
    fail "--test 不能与 --target 同时使用，否则测试程序可能没有构建"
fi

readonly BUILD_DIR="${BACKEND_DIR}/out/build/${preset}"
readonly MAIN_BINARY="${BUILD_DIR}/space-station"

if [[ "${build_frontend}" -eq 1 ]]; then
    echo "==> 构建前端"
    (
        cd -- "${FRONTEND_DIR}"
        if [[ "${install_frontend}" -eq 1 ]]; then
            [[ -f package-lock.json ]] || fail "frontend/package-lock.json 不存在，无法执行 npm ci"
            npm ci
        elif [[ ! -d node_modules ]]; then
            echo "    frontend/node_modules 不存在，执行 npm ci"
            [[ -f package-lock.json ]] || fail "frontend/package-lock.json 不存在，请先安装前端依赖"
            npm ci
        fi
        npm run build
    )
elif [[ ! -f "${FRONTEND_DIR}/dist/index.html" ]]; then
    fail "使用 --backend-only 时必须先生成 frontend/dist"
fi

echo "==> 配置后端：${preset}"
(
    cd -- "${BACKEND_DIR}"
    cmake --preset "${preset}"
)

echo "==> 构建后端：${preset}"
build_arguments=(--build --preset "${preset}")
if [[ -n "${jobs}" ]]; then
    build_arguments+=(--parallel "${jobs}")
fi
if [[ -n "${target}" ]]; then
    build_arguments+=(--target "${target}")
fi
(
    cd -- "${BACKEND_DIR}"
    cmake "${build_arguments[@]}"
)

if [[ "${sync_plugins}" -eq 1 && -d "${BACKEND_DIR}/config/plugins" ]]; then
    echo "==> 同步终端插件"
    mkdir -p -- "${BUILD_DIR}/data/plugins"
    cp -a -- "${BACKEND_DIR}/config/plugins/." "${BUILD_DIR}/data/plugins/"
fi

if [[ "${run_tests}" -eq 1 ]]; then
    command -v ctest >/dev/null 2>&1 || fail "找不到命令：ctest"
    echo "==> 运行后端测试"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

if [[ -z "${target}" || "${target}" == "space-station" ]]; then
    [[ -x "${MAIN_BINARY}" ]] || fail "构建完成但没有找到可执行文件：${MAIN_BINARY}"
    echo "==> 构建完成"
    echo "    可执行文件：${MAIN_BINARY}"
    echo "    启动命令：${MAIN_BINARY}"
else
    echo "==> 目标 ${target} 构建完成"
    echo "    构建目录：${BUILD_DIR}"
fi
