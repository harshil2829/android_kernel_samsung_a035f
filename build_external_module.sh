#!/bin/bash
set -e

export RDIR="$(pwd)"
export ARCH=arm64
export BSP_BUILD_FAMILY=qogirl6
export BSP_BUILD_ANDROID_OS=y

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
    export BUILD_CROSS_COMPILE="${GCC_TOOL_PATH}/aarch64-linux-android-"
    for f in "${GCC_TOOL_PATH}"/aarch64-linux-android-*; do
        [ -f "$f" ] || continue
        base="$(basename "$f")"
        target_link="${GCC_TOOL_PATH}/${base/aarch64-linux-android-/aarch64-linux-gnu-}"
        target_plain="${GCC_TOOL_PATH}/${base/aarch64-linux-android-/}"
        [ ! -e "$target_link" ] && ln -sf "$base" "$target_link" || true
        [ ! -e "$target_plain" ] && ln -sf "$base" "$target_plain" || true
    done
else
    export BUILD_CROSS_COMPILE="aarch64-linux-gnu-"
fi

export MODULE_DIR="${RDIR}/memkernel"
export MODULE_DIR_ENH="${RDIR}/memkernel_enhanced"

# Function to package a .ko into a self-extracting .sh driver script
package_to_sh() {
    local ko_file="$1"
    local out_sh="$2"
    
    if [ ! -f "${ko_file}" ]; then
        return
    fi
    
cat << 'EOF' > "${out_sh}"
#!/system/bin/sh
# RT-Dev Driver Self-Extracting Loader for SM-A035F
# Made by: @rajput_harshil

GREEN='\033[0;32m'
CYAN='\033[0;36m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

clear
echo -e "${CYAN}====================================================${NC}"
echo -e "${GREEN}       RT-Dev Kernel Driver for SM-A035F            ${NC}"
echo -e "${YELLOW}           Made by: @rajput_harshil                 ${NC}"
echo -e "${CYAN}====================================================${NC}"
echo ""

if [ "$(id -u)" != "0" ]; then
  echo -e "${RED}[!] Error: Please run this script as root (su)${NC}" 1>&2
  exit 1
fi

TMP_MOD="/data/local/tmp/rt_driver.ko"
rm -f "${TMP_MOD}" 2>/dev/null

# Extract the embedded .ko payload
sed "1,/^#__PAYLOAD_START__$/d" "$0" > "${TMP_MOD}"
chmod 644 "${TMP_MOD}"

# Load the kernel module
echo -e "[+] Loading driver into kernel..."
if insmod "${TMP_MOD}"; then
  echo -e "${GREEN}[✔] Driver loaded successfully by @rajput_harshil!${NC}"
  echo -e "${GREEN}[✔] Device node is active and ready.${NC}"
  rm -f "${TMP_MOD}" 2>/dev/null
else
  rm -f "${TMP_MOD}" 2>/dev/null
  echo -e "${RED}[!] Driver load failed. Kernel log (dmesg):${NC}"
  dmesg | tail -n 20
  exit 1
fi
exit 0
#__PAYLOAD_START__
EOF
    cat "${ko_file}" >> "${out_sh}"
    chmod +x "${out_sh}"
    echo "[+] Generated self-extracting script: ${out_sh}"
}

# Build memkernel
if [ -d "${MODULE_DIR}" ]; then
  echo "[+] Building memkernel module..."
  make -w \
    -C "${RDIR}" \
    O="${RDIR}/out" \
    M="${MODULE_DIR}" \
    -j"$(nproc)" \
    ARCH=arm64 \
    BSP_BUILD_FAMILY=qogirl6 \
    BSP_BUILD_ANDROID_OS=y \
    CROSS_COMPILE="${BUILD_CROSS_COMPILE}" \
    CLANG_TRIPLE=aarch64-linux-gnu- \
    CC="${BUILD_CC}" \
    LD="${BUILD_LD}" \
    modules
fi

# Build memkernel_enhanced
if [ -d "${MODULE_DIR_ENH}" ]; then
  echo "[+] Building memkernel_enhanced module..."
  make -w \
    -C "${RDIR}" \
    O="${RDIR}/out" \
    M="${MODULE_DIR_ENH}" \
    -j"$(nproc)" \
    ARCH=arm64 \
    BSP_BUILD_FAMILY=qogirl6 \
    BSP_BUILD_ANDROID_OS=y \
    CROSS_COMPILE="${BUILD_CROSS_COMPILE}" \
    CLANG_TRIPLE=aarch64-linux-gnu- \
    CC="${BUILD_CC}" \
    LD="${BUILD_LD}" \
    modules
fi

mkdir -p "${RDIR}/build/modules"
cp "${MODULE_DIR}"/*.ko "${RDIR}/build/modules/" 2>/dev/null || true
cp "${MODULE_DIR_ENH}"/*.ko "${RDIR}/build/modules/" 2>/dev/null || true

# Generate RT-Dev style .sh executable scripts
if [ -f "${RDIR}/build/modules/memkernel.ko" ]; then
    package_to_sh "${RDIR}/build/modules/memkernel.ko" "${RDIR}/build/modules/SM-A035F_memkernel.sh"
fi

if [ -f "${RDIR}/build/modules/memkernel_enhanced.ko" ]; then
    package_to_sh "${RDIR}/build/modules/memkernel_enhanced.ko" "${RDIR}/build/modules/SM-A035F_memkernel_enhanced.sh"
fi

echo "[+] External modules and .sh driver scripts built in build/modules/:"
ls -lh "${RDIR}/build/modules/"
