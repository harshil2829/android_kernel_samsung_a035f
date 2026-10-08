#!/bin/bash
set -e

export RDIR="$(pwd)"
export ARCH=arm64
export KBUILD_BUILD_USER="@developer"
export BSP_BUILD_FAMILY=qogirl6
export BSP_BUILD_ANDROID_OS=y
unset DTC_OVERLAY_TEST_EXT
unset DTC_OVERLAY_VTS_EXT

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

# Output & build directory
mkdir -p "${RDIR}/out"
mkdir -p "${RDIR}/build/modules"

# Kernel version tag
if [ -z "$BUILD_KERNEL_VERSION" ]; then
    export BUILD_KERNEL_VERSION="dev"
fi

# Fix case-collision netfilter headers on case-sensitive Linux filesystems
ln -sf xt_dscp.h "${RDIR}/include/uapi/linux/netfilter/xt_DSCP.h" 2>/dev/null || true
ln -sf xt_tcpmss.h "${RDIR}/include/uapi/linux/netfilter/xt_TCPMSS.h" 2>/dev/null || true
ln -sf ipt_ttl.h "${RDIR}/include/uapi/linux/netfilter_ipv4/ipt_TTL.h" 2>/dev/null || true
ln -sf ip6t_hl.h "${RDIR}/include/uapi/linux/netfilter_ipv6/ip6t_HL.h" 2>/dev/null || true

# Generate missing lowercase C source files for netfilter match modules on case-sensitive Linux
if [ ! -f "${RDIR}/net/netfilter/xt_hl.c" ] || [ "${RDIR}/net/netfilter/xt_hl.c" -ef "${RDIR}/net/netfilter/xt_HL.c" ]; then
    rm -f "${RDIR}/net/netfilter/xt_hl.c" 2>/dev/null || true
    cat << 'EOF' > "${RDIR}/net/netfilter/xt_hl.c"
/* IP tables module for matching the value of the TTL field */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ipt_ttl.h>
#include <linux/netfilter_ipv6/ip6t_hl.h>

MODULE_AUTHOR("Maciej Soltysiak <solt@dns.toxicfilms.tv>");
MODULE_DESCRIPTION("Xtables: Hoplimit/TTL field match");
MODULE_LICENSE("GPL");

static bool ttl_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct ipt_ttl_info *info = par->matchinfo;
	const struct iphdr *iph = ip_hdr(skb);

	switch (info->mode) {
	case IPT_TTL_EQ:
		return iph->ttl == info->ttl;
	case IPT_TTL_NE:
		return iph->ttl != info->ttl;
	case IPT_TTL_LT:
		return iph->ttl < info->ttl;
	case IPT_TTL_GT:
		return iph->ttl > info->ttl;
	default:
		return false;
	}
}

static bool hl_mt6(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct ip6t_hl_info *info = par->matchinfo;
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);

	switch (info->mode) {
	case IP6T_HL_EQ:
		return ip6h->hop_limit == info->hop_limit;
	case IP6T_HL_NE:
		return ip6h->hop_limit != info->hop_limit;
	case IP6T_HL_LT:
		return ip6h->hop_limit < info->hop_limit;
	case IP6T_HL_GT:
		return ip6h->hop_limit > info->hop_limit;
	default:
		return false;
	}
}

static struct xt_match hl_mt_reg[] __read_mostly = {
	{
		.name       = "ttl",
		.revision   = 0,
		.family     = NFPROTO_IPV4,
		.match      = ttl_mt,
		.matchsize  = sizeof(struct ipt_ttl_info),
		.me         = THIS_MODULE,
	},
	{
		.name       = "hl",
		.revision   = 0,
		.family     = NFPROTO_IPV6,
		.match      = hl_mt6,
		.matchsize  = sizeof(struct ip6t_hl_info),
		.me         = THIS_MODULE,
	},
};

static int __init hl_mt_init(void)
{
	return xt_register_matches(hl_mt_reg, ARRAY_SIZE(hl_mt_reg));
}

static void __exit hl_mt_exit(void)
{
	xt_unregister_matches(hl_mt_reg, ARRAY_SIZE(hl_mt_reg));
}

module_init(hl_mt_init);
module_exit(hl_mt_exit);
MODULE_ALIAS("ipt_ttl");
MODULE_ALIAS("ip6t_hl");
EOF
fi

if [ ! -f "${RDIR}/net/netfilter/xt_tcpmss.c" ] || [ "${RDIR}/net/netfilter/xt_tcpmss.c" -ef "${RDIR}/net/netfilter/xt_TCPMSS.c" ]; then
    rm -f "${RDIR}/net/netfilter/xt_tcpmss.c" 2>/dev/null || true
    cat << 'EOF' > "${RDIR}/net/netfilter/xt_tcpmss.c"
/* Kernel module to match TCP MSS values. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_tcpmss.h>

#ifndef TCPOPT_EOL
#define TCPOPT_EOL 0
#endif
#ifndef TCPOPT_NOP
#define TCPOPT_NOP 1
#endif
#ifndef TCPOPT_MAXSEG
#define TCPOPT_MAXSEG 2
#endif
#ifndef TCPOLEN_MAXSEG
#define TCPOLEN_MAXSEG 4
#endif

MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("Xtables: TCP MSS match");
MODULE_LICENSE("GPL");

static bool tcpmss_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_tcpmss_match_info *info = par->matchinfo;
	const struct tcphdr *th;
	struct tcphdr _tcph;
	unsigned int i, optlen;
	const u_int8_t *op;
	u_int8_t _opt[15 * 4 - sizeof(struct tcphdr)];

	th = skb_header_pointer(skb, par->thoff, sizeof(_tcph), &_tcph);
	if (th == NULL)
		goto drop;

	if (th->doff * 4 < sizeof(_tcph))
		goto drop;

	optlen = th->doff * 4 - sizeof(_tcph);
	if (!optlen)
		goto nomatch;

	op = skb_header_pointer(skb, par->thoff + sizeof(_tcph), optlen, _opt);
	if (op == NULL)
		goto drop;

	for (i = 0; i < optlen; ) {
		if (op[i] == TCPOPT_EOL)
			break;
		if (op[i] == TCPOPT_NOP) {
			i++;
			continue;
		}
		if (i + 1 >= optlen)
			break;
		if (op[i + 2] < 2)
			break;
		if (i + op[i + 2] > optlen)
			break;
		if (op[i] == TCPOPT_MAXSEG) {
			u_int16_t mssval;

			if (op[i + 2] != TCPOLEN_MAXSEG)
				goto nomatch;

			mssval = (op[i + 3] << 8) | op[i + 4];

			return (mssval >= info->mss_min &&
				mssval <= info->mss_max) ^ info->invert;
		}
		i += op[i + 2];
	}

nomatch:
	return info->invert;
drop:
	par->hotdrop = true;
	return false;
}

static struct xt_match tcpmss_mt_reg[] __read_mostly = {
	{
		.name      = "tcpmss",
		.family    = NFPROTO_IPV4,
		.match     = tcpmss_mt,
		.matchsize = sizeof(struct xt_tcpmss_match_info),
		.proto     = IPPROTO_TCP,
		.me        = THIS_MODULE,
	},
	{
		.name      = "tcpmss",
		.family    = NFPROTO_IPV6,
		.match     = tcpmss_mt,
		.matchsize = sizeof(struct xt_tcpmss_match_info),
		.proto     = IPPROTO_TCP,
		.me        = THIS_MODULE,
	},
};

static int __init tcpmss_mt_init(void)
{
	return xt_register_matches(tcpmss_mt_reg, ARRAY_SIZE(tcpmss_mt_reg));
}

static void __exit tcpmss_mt_exit(void)
{
	xt_unregister_matches(tcpmss_mt_reg, ARRAY_SIZE(tcpmss_mt_reg));
}

module_init(tcpmss_mt_init);
module_exit(tcpmss_mt_exit);
EOF
fi

if [ ! -f "${RDIR}/net/netfilter/xt_dscp.c" ] || [ "${RDIR}/net/netfilter/xt_dscp.c" -ef "${RDIR}/net/netfilter/xt_DSCP.c" ]; then
    rm -f "${RDIR}/net/netfilter/xt_dscp.c" 2>/dev/null || true
    cat << 'EOF' > "${RDIR}/net/netfilter/xt_dscp.c"
/* IP tables module for matching DSCP */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_dscp.h>

MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("Xtables: DSCP Match");
MODULE_LICENSE("GPL");

static bool dscp_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_dscp_info *info = par->matchinfo;
	u_int8_t dscp = ipv4_get_dsfield(ip_hdr(skb)) >> XT_DSCP_SHIFT;

	return (dscp == info->dscp) ^ info->invert;
}

static bool dscp_mt6(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_dscp_info *info = par->matchinfo;
	u_int8_t dscp = ipv6_get_dsfield(ipv6_hdr(skb)) >> XT_DSCP_SHIFT;

	return (dscp == info->dscp) ^ info->invert;
}

static struct xt_match dscp_mt_reg[] __read_mostly = {
	{
		.name       = "dscp",
		.family     = NFPROTO_IPV4,
		.match      = dscp_mt,
		.matchsize  = sizeof(struct xt_dscp_info),
		.me         = THIS_MODULE,
	},
	{
		.name       = "dscp",
		.family     = NFPROTO_IPV6,
		.match      = dscp_mt6,
		.matchsize  = sizeof(struct xt_dscp_info),
		.me         = THIS_MODULE,
	},
};

static int __init dscp_mt_init(void)
{
	return xt_register_matches(dscp_mt_reg, ARRAY_SIZE(dscp_mt_reg));
}

static void __exit dscp_mt_exit(void)
{
	xt_unregister_matches(dscp_mt_reg, ARRAY_SIZE(dscp_mt_reg));
}

module_init(dscp_mt_init);
module_exit(dscp_mt_exit);
EOF
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

    export MALLOC_TRIM_THRESHOLD_=131072
    export MALLOC_MMAP_THRESHOLD_=131072
    ulimit -n 65536 || true
    JOBS=2

    # Compile Image and DTBs
    make -C "${RDIR}" O="${RDIR}/out" \
        BSP_BUILD_DT_OVERLAY=y \
        CC="${BUILD_CC}" \
        LD="${BUILD_LD}" \
        ARCH=arm64 \
        CLANG_TRIPLE=aarch64-linux-gnu- \
        CROSS_COMPILE="${BUILD_CROSS_COMPILE}" \
        -j"${JOBS}"
    
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
