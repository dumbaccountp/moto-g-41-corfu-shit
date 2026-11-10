/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SukiSU-Ultra (KernelSU) builtin branch -- non-GKI manual hook entry points.
 *
 * The `builtin` branch does not register kprobes: the kernel source calls these
 * entry points directly (manual hooks). This header is the single declaration
 * site for them, so the patched kernel files only need to include it.
 *
 * Hook sites (all guarded by #ifdef CONFIG_KSU in the kernel source):
 *
 *   fs/exec.c               do_execveat_common()    su, ksud, init/zygote, adb root
 *   fs/open.c               do_faccessat()          su path rewriting
 *   fs/stat.c               vfs_statx()             su path rewriting
 *   fs/stat.c               vfs_statx_fd()          init.rc fstat size spoof
 *   fs/read_write.c         vfs_read()              init.rc read injection
 *   kernel/reboot.c         __arm64_sys_reboot()    manager supercall channel
 *   drivers/input/input.c   input_handle_event()    safe mode (volume keys)
 *   fs/devpts/inode.c       devpts_get_priv()       /dev/pts fixups
 */
#ifndef __KSU_H_MANUAL_HOOKS
#define __KSU_H_MANUAL_HOOKS

#include <linux/fs.h>
#include <linux/types.h>

#ifdef CONFIG_KSU

/* fs/exec.c */
extern bool ksu_execveat_hook;
extern int ksu_handle_execveat(int *fd, struct filename **filename_ptr, void *argv, void *envp,
			       int *flags);
extern int ksu_handle_execveat_sucompat(int *fd, struct filename **filename_ptr, void *argv,
					void *envp, int *flags);

/* fs/open.c + fs/stat.c -- 4.19 signature (const char __user **) */
extern int ksu_handle_faccessat(int *dfd, const char __user **filename_user, int *mode,
				int *flags);
extern int ksu_handle_stat(int *dfd, const char __user **filename_user, int *flags);

/* fs/stat.c + fs/read_write.c */
extern bool ksu_vfs_read_hook;
extern int ksu_handle_vfs_read(struct file **file_ptr, char __user **buf_ptr, size_t *count_ptr,
			       loff_t **pos);
extern void ksu_handle_vfs_fstat(int fd, loff_t *kstat_size_ptr);

/* kernel/reboot.c */
extern int ksu_handle_sys_reboot(int magic1, int magic2, unsigned int cmd, void __user **arg);

/* drivers/input/input.c */
extern bool ksu_input_hook;
extern int ksu_handle_input_handle_event(unsigned int *type, unsigned int *code, int *value);

/* fs/devpts/inode.c */
extern int ksu_handle_devpts(struct inode *inode);

#endif /* CONFIG_KSU */

#endif /* __KSU_H_MANUAL_HOOKS */
