# CMake toolchain for cross-compiling to x86_64 Windows MSVC from Linux/WSL,
# using clang-cl + lld-link + the MSVC headers/libs fetched by xwin.
#
# Prerequisites:
#   - clang-cl in PATH (symlink to clang works; ~/.local/bin/clang-cl)
#   - lld-link in PATH (apt: lld)
#   - llvm-rc / llvm-lib (apt: llvm)
#   - xwin splat output at $XWIN_ROOT (default ~/.xwin/cache)
#
# Usage:
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/WindowsCrossToolchain.cmake
#   cmake --build build-win

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT DEFINED ENV{XWIN_ROOT})
    set(XWIN_ROOT "$ENV{HOME}/.xwin/cache")
else()
    set(XWIN_ROOT "$ENV{XWIN_ROOT}")
endif()
if(NOT EXISTS "${XWIN_ROOT}/crt/include")
    message(FATAL_ERROR "xwin splat not found at ${XWIN_ROOT}; run: xwin --accept-license splat --output ${XWIN_ROOT}")
endif()

# Use clang-cl as the driver (auto-targets x86_64-pc-windows-msvc when named clang-cl).
find_program(CLANG_CL_BIN NAMES clang-cl REQUIRED)
set(CMAKE_C_COMPILER   "${CLANG_CL_BIN}")
set(CMAKE_CXX_COMPILER "${CLANG_CL_BIN}")

# MSVC-compatible linker / archiver / resource compiler.
find_program(LLD_LINK_BIN NAMES lld-link REQUIRED)
find_program(LLVM_LIB_BIN NAMES llvm-lib REQUIRED)
find_program(LLVM_RC_BIN  NAMES llvm-rc  REQUIRED)
set(CMAKE_LINKER "${LLD_LINK_BIN}")
set(CMAKE_AR     "${LLVM_LIB_BIN}")
set(CMAKE_RC_COMPILER "${LLVM_RC_BIN}")

# Force linker via clang-cl driver flags.
set(CMAKE_C_LINKER_WRAPPER_FLAG "/fuse-ld=lld-link;")
set(CMAKE_CXX_LINKER_WRAPPER_FLAG "/fuse-ld=lld-link;")

# Tell CMake we're building for MSVC ABI (not MinGW).
set(CMAKE_CROSSCOMPILING TRUE)

# Headers — same directories MSVC would supply.
set(_xwin_includes
    "${XWIN_ROOT}/crt/include"
    "${XWIN_ROOT}/sdk/include/ucrt"
    "${XWIN_ROOT}/sdk/include/um"
    "${XWIN_ROOT}/sdk/include/shared"
    "${XWIN_ROOT}/sdk/include/cppwinrt"
    "${XWIN_ROOT}/sdk/include/winrt"
)
foreach(_inc ${_xwin_includes})
    if(EXISTS "${_inc}")
        string(APPEND _xwin_include_flags " /imsvc \"${_inc}\"")
    endif()
endforeach()

# Libraries — clang-cl picks these up from LIB env or /LIBPATH passed to link.
set(_xwin_libdirs
    "${XWIN_ROOT}/crt/lib/x86_64"
    "${XWIN_ROOT}/sdk/lib/um/x86_64"
    "${XWIN_ROOT}/sdk/lib/ucrt/x86_64"
)
foreach(_lib ${_xwin_libdirs})
    if(EXISTS "${_lib}")
        string(APPEND _xwin_libpath_flags " /LIBPATH:\"${_lib}\"")
    endif()
endforeach()

# Compose. /clang:--target ensures we target windows-msvc; clang-cl already does this when
# invoked under that name, but be explicit to avoid surprises.
set(_target_flags "--target=x86_64-pc-windows-msvc")

# Recent MSVC STL versions assert Clang 19+. Override the version check —
# the STL still works on older Clang for the bits we use.
set(_stl_override "/D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH")

set(CMAKE_C_FLAGS_INIT   "${_target_flags} ${_xwin_include_flags} ${_stl_override}")
set(CMAKE_CXX_FLAGS_INIT "${_target_flags} ${_xwin_include_flags} ${_stl_override}")

# Tell CMake's linker invocations where to find Windows libs.
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_xwin_libpath_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_libpath_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_libpath_flags}")

# Root path search behavior — never look at host (Linux) libraries.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
