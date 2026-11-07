#include <linux/compiler.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/thread_info.h>
#include <linux/cred.h>
#include <linux/seccomp.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#ifdef CONFIG_KSU_SUSFS
#include <linux/susfs_def.h>
#include "selinux/selinux.h"
#endif

#include "policy/app_profile.h"
#include "policy/allowlist.h"
#include "hook/setuid_hook.h"
#include "klog.h" // IWYU pragma: keep
#include "manager/manager_identity.h"
#include "infra/seccomp_cache.h"
#include "supercall/supercall.h"
#include "hook/hook_manager.h"
#include "feature/kernel_umount.h"
#include "compat/kernel_compat.h"

extern void disable_seccomp(void);

#ifdef CONFIG_KSU_SUSFS
extern u32 susfs_zygote_sid;
extern u32 susfs_zygote_next_sid;
extern struct work_struct susfs_extra_works;

static inline void ksu_handle_extra_susfs_work(void)
{
    if (work_pending(&susfs_extra_works))
        return;

    schedule_work(&susfs_extra_works);
}

static int handle_zygote_setresuid(uid_t old_uid, uid_t ruid)
{
    if (is_isolated_process(ruid)) {
        susfs_set_current_proc_no_su();
        susfs_set_current_proc_umounted();
        goto do_umount;
    }

    if (likely(ksu_is_manager_appid_valid()) && unlikely(is_uid_manager(ruid))) {
        disable_seccomp();
        pr_info("install fd for manager: %d\n", ruid);
        ksu_install_fd();
        return 0;
    }

    if (unlikely(ruid == WEBVIEW_ZYGOTE_UID)) {
        if (ksu_uid_should_umount(ruid)) {
            susfs_set_current_proc_no_su();
            susfs_set_current_proc_umounted();
            goto do_umount;
        }
        susfs_set_current_proc_no_su();
        return 0;
    }

    if (likely(is_appuid(ruid) && ksu_uid_should_umount(ruid))) {
        susfs_set_current_proc_no_su();
        susfs_set_current_proc_umounted();
        goto do_umount;
    }

    if (ksu_is_allow_uid_for_current(ruid)) {
        disable_seccomp();
        return 0;
    }

    susfs_set_current_proc_no_su();
    return 0;

do_umount:
    ksu_handle_umount(old_uid, ruid);
    ksu_handle_extra_susfs_work();
    return 0;
}

static int handle_zygote_next_setresuid(uid_t ruid)
{
    if (is_isolated_process(ruid)) {
        susfs_set_current_proc_no_su();
        susfs_set_current_proc_umounted();
        susfs_set_current_proc_umounted_for_zygote_next();
        goto do_susfs_work;
    }

    if (likely(ksu_is_manager_appid_valid()) && unlikely(is_uid_manager(ruid))) {
        disable_seccomp();
        pr_info("install fd for manager: %d\n", ruid);
        ksu_install_fd();
        return 0;
    }

    if (unlikely(ruid == WEBVIEW_ZYGOTE_UID)) {
        if (ksu_uid_should_umount(ruid)) {
            susfs_set_current_proc_no_su();
            susfs_set_current_proc_umounted();
            susfs_set_current_proc_umounted_for_zygote_next();
            goto do_susfs_work;
        }
        susfs_set_current_proc_no_su();
        return 0;
    }

    if (likely(is_appuid(ruid) && ksu_uid_should_umount(ruid))) {
        susfs_set_current_proc_no_su();
        susfs_set_current_proc_umounted();
        susfs_set_current_proc_umounted_for_zygote_next();
        goto do_susfs_work;
    }

    if (ksu_is_allow_uid_for_current(ruid)) {
        disable_seccomp();
        return 0;
    }

    susfs_set_current_proc_no_su();
    return 0;

do_susfs_work:
    ksu_handle_extra_susfs_work();
    return 0;
}

int ksu_handle_setresuid(uid_t old_uid, uid_t new_uid)
{
    if (old_uid != 0)
        return 0;

    if (susfs_is_sid_equal(current_cred(), susfs_zygote_sid))
        return handle_zygote_setresuid(old_uid, new_uid);

    if (susfs_is_sid_equal(current_cred(), susfs_zygote_next_sid))
        return handle_zygote_next_setresuid(new_uid);

    return 0;
}
#else
int ksu_handle_setresuid(uid_t old_uid, uid_t new_uid)
{
    pr_info("handle_setresuid from %d to %d\n", old_uid, new_uid);

    if (unlikely(is_uid_manager(new_uid))) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
        if (current->seccomp.mode == SECCOMP_MODE_FILTER && current->seccomp.filter) {
            ksu_seccomp_allow_cache(current->seccomp.filter, __NR_reboot);
        }
#else
        disable_seccomp();
#endif

#ifdef KSU_KPROBES_HOOK
        ksu_set_task_tracepoint_flag(current);
#endif

        pr_info("install fd for manager: %d\n", new_uid);
        ksu_install_fd();
        return 0;
    }

    if (ksu_is_allow_uid_for_current(new_uid)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
        if (current->seccomp.mode == SECCOMP_MODE_FILTER && current->seccomp.filter) {
            ksu_seccomp_allow_cache(current->seccomp.filter, __NR_reboot);
        }
#else
        disable_seccomp();
#endif

#ifdef KSU_KPROBES_HOOK
        ksu_set_task_tracepoint_flag(current);
#endif
    } else {
#ifdef KSU_KPROBES_HOOK
        ksu_clear_task_tracepoint_flag_if_needed(current);
#endif
    }

    ksu_handle_umount(old_uid, new_uid);

    return 0;
}
#endif

void __init ksu_setuid_hook_init(void)
{
    ksu_kernel_umount_init();
}

void __exit ksu_setuid_hook_exit(void)
{
    pr_info("ksu_core_exit\n");
    ksu_kernel_umount_exit();
}
