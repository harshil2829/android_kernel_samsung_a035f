#!/bin/bash
set -e

export RDIR="$(pwd)"
export ARCH=arm64
export KBUILD_BUILD_USER="@developer"
export BSP_BUILD_FAMILY=qogirl6
export BSP_BUILD_ANDROID_OS=y
export LD_LIBRARY_PATH="${RDIR}/tools/lib64:${LD_LIBRARY_PATH}"
export DTC_OVERLAY_TEST_EXT="${RDIR}/tools/mkdtimg/ufdt_apply_overlay"
export DTC_OVERLAY_VTS_EXT="${RDIR}/tools/mkdtimg/ufdt_verify_overlay_host"

# Install requirements on Ubuntu/Debian
if [ ! -f ".requirements" ] && [ -f "/etc/debian_version" ]; then
    sudo apt update && sudo apt install -y git device-tree-compiler lz4 xz-utils zlib1g-dev gcc g++ python3 \
        gnupg flex bison gperf build-essential zip curl libc6-dev libncurses-dev libssl-dev cpio kmod libelf-dev bc --fix-missing || true
    touch .requirements
fi

# Toolchains
if [ -d "${RDIR}/toolchain/clang/host/linux-x86/clang-r383902/bin" ]; then
    export CLANG_TOOL_PATH="${RDIR}/toolchain/clang/host/linux-x86/clang-r383902/bin"
    export PATH="${CLANG_TOOL_PATH}:${PATH}"
    export BUILD_CC="${CLANG_TOOL_PATH}/clang"
    export BUILD_LD="${CLANG_TOOL_PATH}/ld.lld"
elif [ -d "${RDIR}/toolchain/clang/host/linux-x86/clang-r353983c/bin" ]; then
    export CLANG_TOOL_PATH="${RDIR}/toolchain/clang/host/linux-x86/clang-r353983c/bin"
    export PATH="${CLANG_TOOL_PATH}:${PATH}"
    export BUILD_CC="${CLANG_TOOL_PATH}/clang"
    export BUILD_LD="${CLANG_TOOL_PATH}/ld.lld"
else
    export BUILD_CC="clang"
    export BUILD_LD="ld.lld"
fi

if [ -d "${RDIR}/toolchain/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin" ]; then
    export GCC_TOOL_PATH="${RDIR}/toolchain/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin"
    export PATH="${GCC_TOOL_PATH}:${PATH}"
    export BUILD_CROSS_COMPILE="aarch64-linux-android-"
else
    export BUILD_CROSS_COMPILE="aarch64-linux-gnu-"
fi

# Output & build directory
mkdir -p "${RDIR}/out"
mkdir -p "${RDIR}/build/modules"

# Kernel version tag
if [ -z "$BUILD_KERNEL_VERSION" ]; then
    export BUILD_KERNEL_VERSION="dev"
fi

# Compile Kernel
build_kernel() {
    echo "======================================================"
    echo "  Building SM-A035F Kernel (Unisoc T606 / qogirl6)   "
    echo "======================================================"
    
    # Generate defconfig
    make -C "${RDIR}" O="${RDIR}/out" \
        BSP_BUILD_DT_OVERLAY=y \
        CC="${BUILD_CC}" \
        LD="${BUILD_LD}" \
        ARCH=arm64 \
        CLANG_TRIPLE=aarch64-linux-gnu- \
        CROSS_COMPILE="${BUILD_CROSS_COMPILE}" \
        a03_cis_open_defconfig

    "${RDIR}/scripts/config" --file "${RDIR}/out/.config" -d IKHEADERS || true

    # Compile Image and DTBs
    make -C "${RDIR}" O="${RDIR}/out" \
        BSP_BUILD_DT_OVERLAY=y \
        CC="${BUILD_CC}" \
        LD="${BUILD_LD}" \
        ARCH=arm64 \
        CLANG_TRIPLE=aarch64-linux-gnu- \
        CROSS_COMPILE="${BUILD_CROSS_COMPILE}" \
        -j"$(nproc)"
    
    mkdir -p "${RDIR}/arch/arm64/boot"
    cp "${RDIR}/out/arch/arm64/boot/Image" "${RDIR}/arch/arm64/boot/Image" 2>/dev/null || true
    echo "[+] Kernel Image built successfully: ${RDIR}/out/arch/arm64/boot/Image"
}

# Repack boot.img
build_boot() {
    if [ -d "${RDIR}/AIK-Linux" ] && [ -f "${RDIR}/AIK-Linux/repackimg.sh" ]; then
        echo "======================================================"
        echo "  Repacking boot.img via AIK                          "
        echo "======================================================"
        rm -f "${RDIR}/AIK-Linux/split_img/boot.img-kernel" "${RDIR}/AIK-Linux/boot.img"
        cp "${RDIR}/out/arch/arm64/boot/Image" "${RDIR}/AIK-Linux/split_img/boot.img-kernel"
        mkdir -p "${RDIR}/AIK-Linux/ramdisk"/{debug_ramdisk,dev,metadata,mnt,proc,second_stage_resources,sys}
        cd "${RDIR}/AIK-Linux" && ./repackimg.sh --nosudo && mv image-new.img "${RDIR}/build/boot.img" || true
        cd "${RDIR}"
    fi
}

# Build flashable Odin tar
build_tar() {
    if [ -f "${RDIR}/build/boot.img" ]; then
        cd "${RDIR}/build"
        tar -cvf "Kernel-SM-A035F-${BUILD_KERNEL_VERSION}.tar" boot.img
        echo -e "\n[+] Odin Flashable TAR created: build/Kernel-SM-A035F-${BUILD_KERNEL_VERSION}.tar\n"
        cd "${RDIR}"
    fi
}

# Run
build_kernel
build_boot
build_tar

# Build external memkernel modules
if [ -f "${RDIR}/build_external_module.sh" ]; then
    echo "======================================================"
    echo "  Building MemKernel / RTDev Modules                  "
    echo "======================================================"
    bash "${RDIR}/build_external_module.sh"
fi

echo -e "\n[✔] All Builds Completed Successfully!\n"
