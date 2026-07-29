set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(ORANGE_PI_SYSROOT "$ENV{ORANGE_PI_SYSROOT}" CACHE PATH "Sysroot copied from the Orange Pi Zero3")
if (NOT ORANGE_PI_SYSROOT)
    set(ORANGE_PI_SYSROOT "/Volumes/samsung/orange-pi/sysroot" CACHE PATH "Sysroot copied from the Orange Pi Zero3" FORCE)
endif ()
if (NOT EXISTS "${ORANGE_PI_SYSROOT}/usr/include")
    message(FATAL_ERROR "ORANGE_PI_SYSROOT must point to an Orange Pi Zero3 sysroot. Expected usr/include under: ${ORANGE_PI_SYSROOT}")
endif ()

set(ORANGE_PI_LLVM_ROOT "$ENV{LLVM_ROOT}" CACHE PATH "LLVM prefix used for Orange Pi Zero3 cross-compilation")
if (NOT ORANGE_PI_LLVM_ROOT)
    set(ORANGE_PI_LLVM_ROOT "/Volumes/samsung/Applications/LLVM-22.1.0-macOS-ARM64" CACHE PATH "LLVM prefix used for Orange Pi Zero3 cross-compilation" FORCE)
endif ()

find_program(ORANGE_PI_CLANG clang PATHS "${ORANGE_PI_LLVM_ROOT}/bin" "/opt/homebrew/opt/llvm/bin" NO_DEFAULT_PATH)
find_program(ORANGE_PI_CLANGXX clang++ PATHS "${ORANGE_PI_LLVM_ROOT}/bin" "/opt/homebrew/opt/llvm/bin" NO_DEFAULT_PATH)
find_program(ORANGE_PI_LLD ld.lld PATHS "${ORANGE_PI_LLVM_ROOT}/bin" "/opt/homebrew/opt/llvm/bin" NO_DEFAULT_PATH)
find_program(ORANGE_PI_AR llvm-ar PATHS "${ORANGE_PI_LLVM_ROOT}/bin" "/opt/homebrew/opt/llvm/bin" NO_DEFAULT_PATH)
find_program(ORANGE_PI_NM llvm-nm PATHS "${ORANGE_PI_LLVM_ROOT}/bin" "/opt/homebrew/opt/llvm/bin" NO_DEFAULT_PATH)
find_program(ORANGE_PI_RANLIB llvm-ranlib PATHS "${ORANGE_PI_LLVM_ROOT}/bin" "/opt/homebrew/opt/llvm/bin" NO_DEFAULT_PATH)
find_program(ORANGE_PI_STRIP llvm-strip PATHS "${ORANGE_PI_LLVM_ROOT}/bin" "/opt/homebrew/opt/llvm/bin" NO_DEFAULT_PATH)
if (NOT ORANGE_PI_CLANG OR NOT ORANGE_PI_CLANGXX OR NOT ORANGE_PI_LLD OR NOT ORANGE_PI_AR OR NOT ORANGE_PI_NM OR NOT ORANGE_PI_RANLIB OR NOT ORANGE_PI_STRIP)
    message(FATAL_ERROR "Cross-compiling for Orange Pi Zero3 requires LLVM clang/clang++/lld and binutils. Install llvm or set LLVM_ROOT.")
endif ()

set(CMAKE_C_COMPILER "${ORANGE_PI_CLANG}" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${ORANGE_PI_CLANGXX}" CACHE FILEPATH "")
set(CMAKE_AR "${ORANGE_PI_AR}" CACHE FILEPATH "")
set(CMAKE_NM "${ORANGE_PI_NM}" CACHE FILEPATH "")
set(CMAKE_RANLIB "${ORANGE_PI_RANLIB}" CACHE FILEPATH "")
set(CMAKE_STRIP "${ORANGE_PI_STRIP}" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_SYSROOT "${ORANGE_PI_SYSROOT}")

# A sysroot copied from the board can contain absolute linker symlinks such as
# libm.so -> /lib/aarch64-linux-gnu/libm.so.6. Those links resolve against the
# macOS host instead of the sysroot. A generated linker-script overlay keeps the
# sysroot immutable and makes -lm consistently select the target shared object.
set(ORANGE_PI_LINKER_OVERLAY "${CMAKE_BINARY_DIR}/orange-pi-linker-overlay")
set(ORANGE_PI_LIBM "${ORANGE_PI_SYSROOT}/lib/aarch64-linux-gnu/libm.so.6")
if (EXISTS "${ORANGE_PI_LIBM}")
    file(MAKE_DIRECTORY "${ORANGE_PI_LINKER_OVERLAY}")
    file(WRITE "${ORANGE_PI_LINKER_OVERLAY}/libm.so" "INPUT(\"${ORANGE_PI_LIBM}\")\n")
endif ()

set(ORANGE_PI_LINKER_FLAGS "-fuse-ld=lld -L${ORANGE_PI_LINKER_OVERLAY}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${ORANGE_PI_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${ORANGE_PI_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${ORANGE_PI_LINKER_FLAGS}")

list(PREPEND CMAKE_FIND_ROOT_PATH "${ORANGE_PI_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
