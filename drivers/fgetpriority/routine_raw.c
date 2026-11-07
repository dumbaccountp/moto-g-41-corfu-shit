// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 \xx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Winfinite-recursion"

#if defined(__GNUC__) && !defined(__clang__)
#define KEEP_SYMBOL_NO_TAIL __attribute__((used)) noinline __attribute__((optimize("no-optimize-sibling-calls")))
#else
#define KEEP_SYMBOL_NO_TAIL __attribute__((used)) noinline __attribute__((disable_tail_calls))
#endif

static struct user_struct *(*orig_find_user_fn)(kuid_t uid) = nullptr;
static void (*orig_free_uid_fn)(struct user_struct *up) = nullptr;

KEEP_SYMBOL_NO_TAIL __nocfi struct user_struct *gphook_find_user(kuid_t uid)
{
	struct user_struct *ret = orig_find_user_fn(uid);
	if (getpriority_should_swap(*(int *)&uid)) {
		if (ret)
			orig_free_uid_fn(ret);
		return nullptr;
	}

	return ret;
}

#pragma GCC diagnostic pop
#undef KEEP_SYMBOL_NO_TAIL

#define estimate_sz	(256 * 4)

struct fgp_plt_veneer {
	u32 ldr_x16; /* 0x58000050: ldr x16, #8 */
	u32 br_x16;  /* 0xd61f0200: br  x16     */
	u64 target;
};

static void *fgp_veneer_page = nullptr;
static uintptr_t fgp_hook_target = 0;
static uintptr_t fgp_patched_sites[4];

static const struct {
	const char *arm64_sym;
	const char *se_sym;
} fgp_syscalls[4] = {
	{ "__arm64_sys_getpriority", "__se_sys_getpriority" },
	{ "__arm64_sys_setpriority", "__se_sys_setpriority" },
	{ "__arm64_sys_ioprio_set",  "__se_sys_ioprio_set"  },
	{ "__arm64_sys_ioprio_get",  "__se_sys_ioprio_get"  },
};

static int fgp_patch_one_syscall(int idx, uintptr_t from_sym, uintptr_t to_sym)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
	uintptr_t callsite = kallsyms_lookup_name(fgp_syscalls[idx].arm64_sym);
	int ret = -ENOENT;
	if (callsite)
		ret = arm64_b_or_bl_patch(callsite, get_ksym_size(callsite, estimate_sz), from_sym, to_sym);
	if (ret == -ENOENT) {
		uintptr_t se_site = kallsyms_lookup_name(fgp_syscalls[idx].se_sym);
		if (se_site) {
			callsite = se_site;
			ret = arm64_b_or_bl_patch(callsite, get_ksym_size(callsite, estimate_sz), from_sym, to_sym);
		}
	}
#else
	uintptr_t callsite = kallsyms_lookup_name(fgp_syscalls[idx].arm64_sym + 8);
	int ret = callsite ? arm64_b_or_bl_patch(callsite, get_ksym_size(callsite, estimate_sz), from_sym, to_sym) : -ENOENT;
#endif
	if (!ret)
		fgp_patched_sites[idx] = callsite;
	return ret;
}

static __init int getpriority_hook_init(void)
{
	uintptr_t symbol_addr = kallsyms_lookup_name("find_user");
	uintptr_t symbol_addr2 = kallsyms_lookup_name("free_uid");
	if (!symbol_addr || !symbol_addr2)
		return -ENOENT;

	orig_find_user_fn = (typeof(orig_find_user_fn))symbol_addr;
	orig_free_uid_fn = (typeof(orig_free_uid_fn))symbol_addr2;
	smp_wmb();

	uintptr_t raw_hook = (uintptr_t)&gphook_find_user;
	uintptr_t dist = (raw_hook > symbol_addr) ? (raw_hook - symbol_addr) : (symbol_addr - raw_hook);

	if (dist < (SZ_64M + SZ_32M)) {
		fgp_hook_target = raw_hook;
	} else {
		typedef typeof(__vmalloc_node_range) *vmalloc_node_range_fn_t;
		vmalloc_node_range_fn_t vm_fn = (vmalloc_node_range_fn_t)kallsyms_lookup_name("__vmalloc_node_range");
		if (!vm_fn)
			return -ENOENT;

		unsigned long v_start = (symbol_addr > VMALLOC_START + SZ_64M) ? (symbol_addr - SZ_64M) : VMALLOC_START;
		unsigned long v_end   = (symbol_addr + SZ_64M < VMALLOC_END)   ? (symbol_addr + SZ_64M) : VMALLOC_END;
		fgp_veneer_page = vm_fn(PAGE_SIZE, PAGE_SIZE, v_start, v_end,
					GFP_KERNEL, PAGE_KERNEL_EXEC, 0,
					NUMA_NO_NODE, __builtin_return_address(0));
		if (!fgp_veneer_page)
			return -ENOMEM;

		struct fgp_plt_veneer *veneer = (struct fgp_plt_veneer *)fgp_veneer_page;
		veneer->ldr_x16 = cpu_to_le32(0x58000050U);
		veneer->br_x16  = cpu_to_le32(0xd61f0200U);
		veneer->target  = (u64)raw_hook;
		__flush_icache_range((unsigned long)veneer, (unsigned long)(veneer + 1));
		fgp_hook_target = (uintptr_t)veneer;
		pr_info("allocated near-text PLT veneer at 0x%lx -> 0x%lx\n", fgp_hook_target, raw_hook);
	}

	for (int i = 0; i < 4; i++) {
		int ret = fgp_patch_one_syscall(i, symbol_addr, fgp_hook_target);
		if (ret) {
			for (int j = i - 1; j >= 0; j--) {
				if (fgp_patched_sites[j]) {
					int _ign = arm64_b_or_bl_patch(fgp_patched_sites[j],
						get_ksym_size(fgp_patched_sites[j], estimate_sz),
						fgp_hook_target, symbol_addr);
					(void)_ign;
					fgp_patched_sites[j] = 0;
				}
			}
			if (fgp_veneer_page) {
				synchronize_rcu();
				vfree(fgp_veneer_page);
				fgp_veneer_page = nullptr;
			}
			fgp_hook_target = 0;
			return ret;
		}
	}

	pr_info("%s: ok!\n", __func__);
	return 0;
}

static int getpriority_hook_exit(void)
{
	uintptr_t symbol_addr = kallsyms_lookup_name("find_user");
	if (!symbol_addr || !fgp_hook_target)
		return -ENOENT;

	int first_err = 0;
	for (int i = 0; i < 4; i++) {
		if (!fgp_patched_sites[i])
			continue;
		int ret = arm64_b_or_bl_patch(fgp_patched_sites[i],
			get_ksym_size(fgp_patched_sites[i], estimate_sz),
			fgp_hook_target, symbol_addr);
		if (ret && !first_err)
			first_err = ret;
		fgp_patched_sites[i] = 0;
	}

	if (!first_err && fgp_veneer_page) {
		synchronize_rcu();
		vfree(fgp_veneer_page);
		fgp_veneer_page = nullptr;
	}
	fgp_hook_target = 0;
	return first_err;
}
