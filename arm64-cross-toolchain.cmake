# arm64-cross-toolchain.cmake
# Toolchain file for cross-compiling to ARM64 (Ubuntu and Android)

# ------------------------------------------------------------------
# [SECTION] Ubuntu ARM64 Configuration
# ------------------------------------------------------------------
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux") # Assuming your host is Linux

    message(STATUS "Configuring for Ubuntu ARM64 target")

    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
    set(CMAKE_SYSTEM_VERSION "Generic") # Or specify Ubuntu version if needed

    # ----- Specify the ARM64 compilers for Ubuntu ------
    # IMPORTANT: Adjust these paths to where your ARM64 toolchain is installed!
    set(CMAKE_C_COMPILER   /usr/bin/aarch64-linux-gnu-gcc) # Example path for Debian/Ubuntu packages
    set(CMAKE_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++) # Example path for Debian/Ubuntu packages
    set(CMAKE_LINKER       /usr/bin/aarch64-linux-gnu-ld)  # Optional, CMake often infers this

    # ------ Optional: Sysroot for Ubuntu ARM64 (if needed) -----
    # If you have a sysroot for Ubuntu ARM64, uncomment and set this:
    # set(CMAKE_SYSROOT /path/to/your/ubuntu-arm64-sysroot)
    # set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
    # set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    # set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    # set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

    # ------ Compiler flags (adjust as needed for Ubuntu) -------
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a") # Example: ARMv8-A architecture
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}") # Add any specific linker flags if needed

    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so") # Common suffixes for Linux libraries
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE) # Prefer config-mode find_package
    set(CMAKE_SKIP_TOOLCHAIN_CHECK TRUE) # Skip toolchain ABI checks (sometimes needed for cross-compiling)

    # ------ Finding OpenSSL and zlib on Ubuntu ARM64 (Assumed to be in sysroot or standard paths) ----
    # CMake's find_package should ideally find them in standard paths or sysroot.
    # If not, you might need to set CMAKE_PREFIX_PATH or CMAKE_MODULE_PATH to help find them.
    # For now, let's assume they are discoverable.

endif()


## ------------------------------------------------------------------
## [SECTION] Android ARM64 Configuration
## ------------------------------------------------------------------
#if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux") # Assuming your host is Linux
#
#    message(STATUS "Configuring for Android ARM64 target")
#
#    set(CMAKE_SYSTEM_NAME Android)
#    set(CMAKE_SYSTEM_PROCESSOR aarch64)
#    set(CMAKE_SYSTEM_VERSION 30) # Example Android API level, adjust as needed (e.g., 21, 24, 30, etc.)
#    set(CMAKE_ANDROID_ARCH_ABI arm64-v8a) # ABI for ARM64 Android
#    set(CMAKE_ANDROID_NDK /path/to/your/android-ndk) # **IMPORTANT: SET THIS TO YOUR NDK PATH!**
#
#    # ------ Use Android NDK toolchain ------
#    include(${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake)
#
#    # ------ Compiler flags (adjust as needed for Android) -------
#    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a") # Example: ARMv8-A architecture
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a")
#    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}") # Android-specific linker flags if needed
#
#    set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a") # Common suffixes for Android libraries
#    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE) # Prefer config-mode find_package
#    set(CMAKE_SKIP_TOOLCHAIN_CHECK TRUE) # Skip toolchain ABI checks
#
#    # ------ Finding OpenSSL and zlib on Android NDK --------
#    # Android NDK includes zlib and OpenSSL. We'll try to let CMake find them.
#    # If it struggles, you might need to use NDK-provided CMake modules or set hints.
#    # NDK usually provides find modules for these. Let's see if find_package works first.
#
#
#endif()
#

# ------------------------------------------------------------------
# [SECTION] Common Settings (Outside of OS-specific blocks if needed)
# ------------------------------------------------------------------
# Example: Common CMake flags that might apply to both targets.
# You can add things here that are not OS-specific.
# set(CMAKE_BUILD_TYPE Release) # Example: Set build type for both Ubuntu and Android if desired.