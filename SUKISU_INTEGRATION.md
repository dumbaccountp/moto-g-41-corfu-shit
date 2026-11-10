SukiSU-Ultra integration — builtin branch, non-GKI (manual hooks)
=================================================================

Target
------
  Kernel tree : this repo, Linux 4.19.191 (MTK k69v1_64 / "corfu", arm64)
  Root solution: SukiSU-Ultra v4.3.0-r2 kernel side, taken from the
                 `builtin` branch of SukiSU-Ultra/SukiSU-Ultra
                 (commit 1cfa5734, the branch head at the time of writing;
                 the branch's own version strings still say v4.2.1, the
                 v4.3.0-r2 tag exists only on `main`)

Why `builtin` and not `main`
----------------------------
  * `builtin` is the source-integrated tree: no kprobes, the kernel calls its
    hook entry points directly. That is what non-GKI kernels need; `main`
    registers kprobes/tracepoints at runtime and carries 5.x-only assumptions
    (BTF parser, seccomp caching, static_call, arch syscall patching).
  * `builtin` is also the only SukiSU ref that ships the SUSFS options.

What was added
--------------
  drivers/kernelsu/                 vendored SukiSU-Ultra `kernel/` tree
  include/linux/ksu_manual_hooks.h  declarations of every manual hook entry
                                    point used by the patched core files
  drivers/Kconfig                   source "drivers/kernelsu/Kconfig"
  drivers/Makefile                  obj-$(CONFIG_KSU) += kernelsu/
  arch/arm64/configs/ext_config/moto-k69v1_64-corfu.config
                                    CONFIG_KSU / CONFIG_KPM /
                                    CONFIG_KALLSYMS_ALL enablement

  (`moto-k69v1_64_k419-corfu.config` is a symlink to that file, so both the
  4.19 and k419 corfu builds pick the same fragment.)

Manual hook sites
-----------------
All call sites are wrapped in `#ifdef CONFIG_KSU` and are exactly the ones the
upstream KernelSU non-GKI guide uses, adapted to this tree (4.19 stat/open
signatures):

  fs/exec.c               do_execveat_common()    su, ksud, init/zygote, adb root
  fs/open.c               do_faccessat()          su path rewriting
  fs/stat.c               vfs_statx()             su path rewriting
  fs/stat.c               vfs_statx_fd()          init.rc fstat size spoof
  fs/read_write.c         vfs_read()              init.rc read injection
  kernel/reboot.c         __arm64_sys_reboot()    manager supercall channel
  drivers/input/input.c   input_handle_event()    safe mode (volume keys)
  fs/devpts/inode.c       devpts_get_priv()       /dev/pts fixups for `pm`

4.19 fixes applied to the vendored tree
--------------------------------------
  1. kernel_includes.h — `<linux/static_call.h>` only exists from 5.10.
  2. extension/kpm/kpm.c — `access_ok()` took a `type` argument before 5.0.
  3. extension/cpu_spoof.c — `struct clocksource` only grew `vdso_clock_mode`
     in 5.2 and `vdso_default` in 5.7; both probes are skipped below 5.7. The
     vDSO datapage write that 4.19 actually needs is kept.

Everything else in the tree already had 4.19 branches (SELinux accessors,
`task_work_add`, proc_ops, path_umount fallback, etc.).

Config
------
  CONFIG_KSU=y
  CONFIG_KSU_FEATURE_ADBROOT=y
  CONFIG_KPM=y                  # selects KALLSYMS + KALLSYMS_ALL
  CONFIG_KALLSYMS_ALL=y         # explicitly set as well (KPM needs it on
                                # non-GKI to resolve symbols for KPM modules)
  CONFIG_KSU_SUSFS is not set

  `CONFIG_KALLSYMS_ALL` upstream depends on `DEBUG_KERNEL && KALLSYMS`. In this
  tree DEBUG_KERNEL already resolves to y and KALLSYMS is default y, so the
  option is live; KPM's `select` also forces it. Verified with kconfiglib and
  with `scripts/kconfig/merge_config.sh` against
  k69v1_64_defconfig + moto-mt6768.config + moto-k69v1_64-corfu.config.

  The options live in the ext_config fragment because `defconfig.mk` merges
  `arch/arm64/configs/ext_config/moto-$(DEFCONFIG_BASENAME).config` last, so
  the MTK base defconfigs stay untouched.

SUSFS is off on purpose
-----------------------
`CONFIG_KSU_SUSFS` expects the susfs4ksu kernel side to be present in the tree
(`fs/susfs.c`, `include/linux/susfs.h`, `include/linux/susfs_def.h`); the
SukiSU repo only ships the *glue*. This tree does not carry susfs4ksu, so
enabling it fails to build. To turn it on: apply susfs4ksu branch
`kernel-4.19` (gitlab.com/simonpunk/susfs4ksu) to this tree, then flip
`CONFIG_KSU_SUSFS` and its sub-options in
`arch/arm64/configs/ext_config/moto-k69v1_64-corfu.config`.

ksud preinit blob is disabled automatically
-------------------------------------------
SukiSU embeds a small static "preinit" runner that is compiled against the
*installed* UAPI headers (`$(objtree)/usr/include`, produced by
`make headers_install`). Android kernel builds never run headers_install, so
`drivers/kernelsu/Makefile` prints

    -- KernelSU: .../usr/include not found, preinit disabled

and compiles with `-DKSU_NO_PREINIT`; ksud is then started from the rc snippets
alone. If you want the preinit path, run
`make headers_install INSTALL_HDR_PATH=$OUT/usr` before the kernel build.

Verification performed here
---------------------------
  * kconfiglib + merge_config.sh: KSU=y, KPM=y, KALLSYMS=y, KALLSYMS_ALL=y.
  * Every patched core file compiles clean with CONFIG_KSU=y **and** with
    CONFIG_KSU undefined (so turning the feature off is safe).
  * `drivers/kernelsu/ksu_main.c` — which is a unity build that pulls in the
    entire SukiSU kernel tree — compiles clean against this 4.19 tree with the
    compat flags the Makefile auto-detects.

  Not verified here: a full vmlinux link and on-device boot. This sandbox has
  no aarch64 gcc/clang toolchain and no bison, so only per-file compilation was
  possible (zig's clang cross target was used). Build the real kernel with the
  MTK toolchain and check `CONFIG_KSU`, `CONFIG_KPM` and `CONFIG_KALLSYMS_ALL`
  in the resulting `.config`.

Using it
--------
  Nothing else is needed in the kernel: flash the image, install the SukiSU
  Ultra manager (`com.sukisu.ultra`), and it will detect the built-in kernel
  driver. `su` for apps, the manager fd via `reboot(2)` supercalls, and KPM
  modules all go through the hooks above.
