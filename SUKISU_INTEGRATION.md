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
  fs/susfs.c                        SUSFS v2.3.0 kernel side (see below)
  include/linux/susfs.h             SUSFS userspace-shared ABI
  include/linux/susfs_def.h         SUSFS flags, CMD_* ids, helpers
  .github/workflows/build-kernel.yml CI build (see below)
  arch/arm64/configs/ext_config/moto-k69v1_64-corfu.config
                                    CONFIG_KSU / CONFIG_KPM /
                                    CONFIG_KALLSYMS_ALL / CONFIG_KSU_SUSFS*
                                    enablement

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
  4. sulog/event.c — called `strncpy_from_user_nofault()` directly, which only
     exists from 5.8. It now uses the existing
     `ksu_strncpy_from_user_nofault()` compat wrapper (which falls back to
     `strncpy_from_user()` + `pagefault_disable()`), like every other call
     site. Found by the CI build as `-Wimplicit-function-declaration`.
  5. feature/sucompat.c — `ksu_handle_devpts()` was defined *inside* the
     `CONFIG_COMPAT` branch, while `fs/devpts/inode.c#devpts_get_priv()` calls
     it unconditionally under `CONFIG_KSU`, so non-COMPAT builds failed to
     link. The definition (a stub) is now unconditional.

Everything else in the tree already had 4.19 branches (SELinux accessors,
`task_work_add`, proc_ops, path_umount fallback, etc.).

Config
------
  CONFIG_KSU=y
  # CONFIG_KSU_FEATURE_ADBROOT is not set   # adbroot deliberately disabled
  CONFIG_KPM=y                  # selects KALLSYMS + KALLSYMS_ALL
  CONFIG_KALLSYMS_ALL=y         # explicitly set as well (KPM needs it on
                                # non-GKI to resolve symbols for KPM modules)
  CONFIG_KSU_SUSFS=y            # + all sub-options, see below

  `CONFIG_KALLSYMS_ALL` upstream depends on `DEBUG_KERNEL && KALLSYMS`. In this
  tree DEBUG_KERNEL already resolves to y and KALLSYMS is default y, so the
  option is live; KPM's `select` also forces it. Verified with kconfiglib and
  with `scripts/kconfig/merge_config.sh` against
  k69v1_64_defconfig + moto-mt6768.config + moto-k69v1_64-corfu.config.

  The options live in the ext_config fragment because `defconfig.mk` merges
  `arch/arm64/configs/ext_config/moto-$(DEFCONFIG_BASENAME).config` last, so
  the MTK base defconfigs stay untouched.

SUSFS (v2.3.0)
-------------
`CONFIG_KSU_SUSFS` only holds the SukiSU *glue* (`extension/susfs_extension/`,
the `susfs_*_sid` helpers in `selinux/selinux.c`, the `SUSFS_MAGIC` command
dispatcher in `supercall/dispatch.c`); the kernel side has to be patched in.

SukiSU v4.3.0-r2 calls the **SUSFS v2.x** API — `susfs_add_sus_path_loop(void
__user **)`, `susfs_enable_log(void __user **)`, `susfs_show_variant()`,
`susfs_parse_priority_app_strings()`, `susfs_set_priority_uid_filter()`,
`susfs_set_avc_log_spoofing()`, ... The susfs4ksu `kernel-4.19` mirrors (the
original GitLab repo is gone; ShirkNeko/midori01/Trijal08/... all mirror the
same branch) ship **v1.5.5/v1.5.9**, whose signatures are incompatible with
this driver and will not link.

Applied patch: `50_add_susfs_in_kernel-4.19.patch` from a 4.19 device tree that
carries SUSFS **v2.3.0** (TALPAD-BOOM-Developers/TALIH-PD2-Kernel, branch
`susfs+resukisu`). It is self-contained: it adds `fs/susfs.c`,
`include/linux/susfs.h` and `include/linux/susfs_def.h` and touches 20 existing
files.

  Applied verbatim (20 files): block/ioprio.c, fs/Makefile, fs/dcache.c,
  fs/namei.c, fs/namespace.c, fs/notify/fdinfo.c, fs/proc/base.c,
  fs/proc/cmdline.c, fs/proc/fd.c, fs/proc/task_mmu.c, fs/proc_namespace.c,
  fs/readdir.c, fs/statfs.c, fs/super.c, kernel/kallsyms.c, kernel/sys.c,
  mm/memory.c, security/selinux/avc.c, security/selinux/ss/services.c,
  security/selinux/ss/status.c
  New files: fs/susfs.c, include/linux/susfs.h, include/linux/susfs_def.h

  Hand-merged (1 file): **fs/stat.c** — the patch's `vfs_getattr_nosec()` hunk
  collides with our KernelSU include block and with the
  `ksu_handle_stat()`/`ksu_handle_vfs_fstat()` hooks. Both sides were kept: the
  SUS_KSTAT result-mask spoofing in `vfs_getattr_nosec()` and the result-mask
  cleanup in `vfs_statx()`, next to the untouched KernelSU hooks.

  Not applied: `KernelSU/10_enable_susfs_for_ksu.patch` from the same tree. It
  targets the old flat KernelSU layout (`kernel/core_hook.c`,
  `kernel/allowlist.c`) and would not apply to `drivers/kernelsu/`; SukiSU
  already ships that glue natively.

  Symbol audit (`susfs_*` referenced from `drivers/kernelsu` vs. defined by
  `fs/susfs.c` + the two headers): every reference resolves.
  `susfs_starts_with()`/`susfs_ends_with()` — used by `feature/adb_root.c` and
  `kernel/kallsyms.c` — are `static inline` in `susfs_def.h`. The sid helpers
  (`susfs_get_sid_from_name`, `susfs_set_batch_sid`, `susfs_is_sid_equal`,
  `susfs_extra_works`, ...) stay owned by `drivers/kernelsu`; the three
  genuinely shared hooks (`susfs_add_sus_type`,
  `susfs_add_sus_avd_allowed_by_type`, `susfs_is_current_ksu_domain`) are
  declared `extern` on the driver side, so each has exactly one definition.

Enabled sub-options:

  CONFIG_KSU_SUSFS=y
  CONFIG_KSU_SUSFS_SUS_PATH=y
  CONFIG_KSU_SUSFS_SUS_MOUNT=y
  CONFIG_KSU_SUSFS_SUS_KSTAT=y
  CONFIG_KSU_SUSFS_SPOOF_UNAME=y
  CONFIG_KSU_SUSFS_ENABLE_LOG=y
  CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS=y
  CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG=y
  CONFIG_KSU_SUSFS_OPEN_REDIRECT=y
  CONFIG_KSU_SUSFS_SUS_MAP=y

`CONFIG_KSU_SUSFS` has `depends on THREAD_INFO_IN_TASK`; arm64 selects that
unconditionally in `arch/arm64/Kconfig`. SUSFS v2.x keeps its state in high
`TIF_*` bits (33/34/35) of `task_struct`, which is why `THREAD_INFO_IN_TASK`
is required — do not backport this to a tree where those flags still live in
the 32-bit `thread_info.flags`.

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
  * kconfiglib: KSU=y, KPM=y, KALLSYMS=y, KALLSYMS_ALL=y, all ten
    `CONFIG_KSU_SUSFS*` = y, `KSU_FEATURE_ADBROOT` = n.
  * All 21 SUSFS-touched files compile clean with SUSFS enabled (`fs/susfs.c`
    included) and `drivers/kernelsu/ksu_main.c` still compiles clean against
    the v2.3.0 API.
  * Every patched core file compiles clean with CONFIG_KSU=y **and** with
    CONFIG_KSU undefined (so turning the feature off is safe).
  * `drivers/kernelsu/ksu_main.c` — which is a unity build that pulls in the
    entire SukiSU kernel tree — compiles clean against this 4.19 tree with the
    compat flags the Makefile auto-detects.

  * **Full build verified in GitHub Actions** — run
    [#43082356704](https://github.com/dumbaccountp/moto-g-41-corfu-shit/actions/runs/43082356704)
    on `15efbfc50` is green in 12m41s: `make defoldconfig`, the config gate,
    `Image.gz` + `dtbs`, the vmlinux symbol check and the artifact upload all
    pass. The `corfu-Image` artifact is ~149 MB and contains `Image.gz`,
    `vmlinux`, `System.map`, `kernel.config` and the `mt6768*.dtb` files.

  Not verified: on-device boot (no device here) and the AOSP boot-image
  packaging step.

Building with GitHub Actions
----------------------------
  `.github/workflows/build-kernel.yml`. The repo's `.gitignore` ignored every
  dotfile, so `.github/` is now explicitly un-ignored there.

  Triggers: push to this branch, pull requests, and `workflow_dispatch` with
  optional `defconfig` / `ext_config` / `make_targets` inputs.

  Steps, mirroring AOSP's `defconfig.mk` without the AOSP tree:

    1. concatenate, in this order,
         arch/arm64/configs/k69v1_64_k419_defconfig
         arch/arm64/configs/ext_config/moto-mt6768.config
         arch/arm64/configs/ext_config/moto-k69v1_64-corfu.config
       into `out/.config` (+ `out/mapphone_defconfig`) and run
       `make defoldconfig` (MTK's own target; AOSP's Android.mk uses it too).
    2. grep the resolved `.config` and **fail the job** unless every
       `CONFIG_KSU*` / `CONFIG_KSU_SUSFS*` / `CONFIG_KALLSYMS_ALL` above is `y`
       and `CONFIG_KSU_FEATURE_ADBROOT` is unset, so a silently dropped option
       (e.g. a `depends on` that stopped being satisfied) can never yield a
       "successful" build with the features missing.
    3. build `Image` with `gcc-aarch64-linux-gnu` from Ubuntu 22.04's archives
       (`CROSS_COMPILE=aarch64-linux-gnu-`, `KCFLAGS=-fcommon` because gcc 12
       defaults to `-fno-common`, which this pre-5.6 MTK tree does not expect),
       with `fetch-depth: 0` because `CONFIG_LOCALVERSION_AUTO=y` needs the tags
       to compute the `+g<sha>` suffix.
    4. upload `Image`, `vmlinux`, `System.map` and the resolved `kernel.config`
       as the `corfu-Image` artifact, plus `build.log`.

  The workflow builds `Image.gz dtbs`. Assembling a flashable boot image still
  needs AOSP's out-of-tree packaging
  (`device/mediatek/build/core/build_dtbimage.mk`, `PLATFORM_DTB_NAME` /
  `PROJECT_DTB_NAMES`, `mkbootimg`), which is not part of this repo.

  Toolchain: AOSP clang r383902 (the stock corfu compiler), fetched as a
  prebuilt from the crdroidandroid mirror — the same recipe this repo's
  upstream `build.yml` uses for this board. `apt-get` is *not* required: the
  dependency step is best-effort (`continue-on-error`) because apt has been
  hanging on these runners; the runner image already provides the host tools.

  Failures are reported through check-run annotations, the job summary and a
  commit comment containing `build.log`, so a red build is diagnosable without
  downloading the Actions log/artifact blobs.

Using it
--------
  Nothing else is needed in the kernel: flash the image, install the SukiSU
  Ultra manager (`com.sukisu.ultra`), and it will detect the built-in kernel
  driver. `su` for apps, the manager fd via `reboot(2)` supercalls, and KPM
  modules all go through the hooks above.
