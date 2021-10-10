#!/usr/bin/python2.6
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

import contextlib
import copy
import os
import platform
import stat
import sys
import tempfile
import solaris.zones as zones
import libbe_py
import pkg.pkgsubprocess as subprocess
from bootmgmt.pysol import (mnttab_open, getmntany, mnttab_close, platform_name,
                            zfs_name_valid, ZFS_TYPE_FILESYSTEM)

# libbe_py backstops our implementation
from libbe_py import beCreateSnapshot, beDestroySnapshot, \
    beMount, beUnmount, beRollback, bePrintErrors, \
    beGetErrDesc, beVerifyBEName

import bootmgmt.bootloader as bl
import bootmgmt.bootconfig as bc

def commit(bootconf, devlist):
        bootconf.commit_boot_config(
            boot_devices=["/dev/rdsk/" + d for d in devlist])

def beList(bename=None):

        if bename is not None and not be_name_is_valid(bename):
                print >> sys.stderr, ("The BE name provided is invalid.\n"
                       "Please check it and try again.")
                sys.exit(1)

        ret, lod = libbe_py.beList(bename)
        if ret != 0:
                return ret, lod

        # Determine which BE is activated (will be booted at next boot).  We
        # prefer what GRUB tells us, but if that doesn't work, or we're on a
        # non-GRUB system, then we'll return what we got from libbe, which is
        # what the current BE's pool's bootfs property is set to.
        if zones.getzoneid() == 0 and libbe_py.beHasGRUB():
                try:
                        bi = get_default_boot_instance()
                except Exception: # XXX overly broad; see below
                        return ret, lod
                default_bootfs = None
                if bi:
                        default_bootfs = getattr(bi, "bootfs", None)

                for d in lod:
                        root_ds = d.get("root_ds", None)
                        if root_ds and default_bootfs:
                                if root_ds == default_bootfs:
                                        d["active_boot"] = True
                                else:
                                        d["active_boot"] = False

        return ret, lod

def beRename(old_bename, new_bename):
        """Rename the boot environment 'old_bename' to 'new_bename'."""

        if not be_name_is_valid(old_bename) or not be_name_is_valid(new_bename):
                print >> sys.stderr, ("The BE name provided is invalid.\n"
                       "Please check it and try again.")
                sys.exit(1)

        # Do the core rename in libbe.
        ret = libbe_py.beRename(old_bename, new_bename)
        if ret != 0:
                return ret

        if zones.getzoneid() == 0:

                zpool = libbe_py.getzpoolbybename(new_bename)
                if not zpool:
                        print >> sys.stderr, ("Failed to find boot "
                            "environment '%s' in any ZFS pool" % new_bename)
                        sys.exit(1)

                with get_boot_config(zpool) as mybc:
                        root_ds = be_make_root_ds(new_bename, zpool)
                        ret, devlist = be_get_boot_device_list(new_bename,
                                                               zpool, root_ds)
                        if ret != 0:
                                return ret
                        old_root_ds = be_make_root_ds(old_bename, zpool)

                        bis = get_menu_entries(mybc, zpool, old_root_ds)
                        for bi in bis:
                                # if the first word in the title is the old
                                # BE name, change it to the new BE name
                                try:
                                        if bi.title.split()[0] == old_bename:
                                                bi.title = bi.title.replace(
                                                               old_bename,
                                                               new_bename, 1)
                                except StandardError:
                                        # If anything goes wrong, just use
                                        # new BE name
                                        bi.title = new_bename
                                bi.bootfs = root_ds

                        commit(mybc, devlist)

        return 0

def beCopy(dst_bename=None, src_bename=None, srcSnap=None, dest_rpool=None,
    bename_props=None, be_desc=None):
        """Copy a boot environment from 'src_bename' to 'dst_bename.  If 'rpool'
        is specified, create the new BE in that ZFS pool.  If 'srcSnap' is
        specified, base the new BE off that ZFS snapshot.  If 'beNameProperties'
        is specified, it should be a mapping of property names to their
        values."""

        if ((src_bename is not None and not be_name_is_valid(src_bename)) or
            (dst_bename is not None and not be_name_is_valid(dst_bename))):
                print >> sys.stderr, ("The BE name provided is invalid.\n"
                       "Please check it and try again.")
                sys.exit(1)

        # If we weren't given a source BE name, then use the current one.
        if not src_bename:
                ret, src_pool, src_ds, src_bename = \
                    libbe_py.beFindCurrentBE()
                if ret != 0:
                        return 1, None, None

        # If we weren't given a destination BE name, then we need to create one
        # for the caller.
        if not dst_bename:
                ret, lod = libbe_py.beList()
                if ret != 0:
                        return 1, None, None

                base, sep, rev = src_bename.rpartition("-")
                if sep and rev.isdigit():
                        # The source BE has already been auto-named, so we need
                        # to bump the revision.  List all BEs, cycle through the
                        # names and find the one with the same basename as
                        # src_bename, and has the highest revision.  Then add
                        # one to it.  This means that gaps in the numbering will
                        # not be filled.
                        rev = int(rev)
                        maxrev = rev
                        for d in lod:
                                oben = d.get("orig_be_name", None)
                                if not oben:
                                        continue
                                nbase, sep, nrev = oben.rpartition("-")
                                if (not sep or nbase != base or
                                    not nrev.isdigit()):
                                        continue
                                maxrev = max(int(nrev), rev)
                else:
                        # If we didn't find the separator, or if the rightmost
                        # part wasn't an integer, then we just start with the
                        # original name.
                        base = src_bename
                        maxrev = 0

                good = False
                num = maxrev + 1
                while not good:
                        dst_bename = "%s-%s" % (base, num)
                        for d in lod:
                                oben = d.get("orig_be_name", None)
                                if not oben:
                                        continue
                                if oben == dst_bename:
                                        break
                        else:
                                good = True

                        num += 1

        # Do the core copy in libbe.
        ret = libbe_py.beCopy(dst_bename, src_bename, srcSnap, dest_rpool,
            bename_props, be_desc)
        dest_snap = ret[2]
        if ret[0] != 0:
                return 1, None, None

        if zones.getzoneid() == 0:
                zpool = libbe_py.getzpoolbybename(dst_bename)
                if not zpool:
                        print >> sys.stderr, ("Failed to find boot "
                            "environment '%s' in any ZFS pool" % dst_bename)
                        sys.exit(1)

                with get_boot_config(zpool) as mybc:
                        src_root_ds = be_make_root_ds(src_bename, zpool)
                        bis = get_menu_entries(mybc, zpool, src_root_ds)
                        root_ds = be_make_root_ds(dst_bename, zpool)
                        # Clone the boot instance that the copy is based on,
                        # then just change the bootfs and tweak the title.
                        if bis:
                                newbi = copy.copy(bis[0])
                                # Do NOT inherit the default attribute!
                                newbi.default = False
                                if be_desc:
                                        newbi.title = be_desc
                                else:
                                        newbi.title = dst_bename
                                newbi.bootfs = root_ds
                                mybc.add_boot_instance(newbi)
                        else: # This really shouldn't ever happen
                                be_append_menu(mybc, root_ds, be_desc)

                        ret, devlist = be_get_boot_device_list(dst_bename,
                                                               zpool, root_ds)
                        if ret != 0:
                                return 1, dst_bename, dest_snap
                        commit(mybc, devlist)

        return 0, dst_bename, dest_snap

def beDestroy(beName, destroy_snaps=False, force_unmount=False):
        "Destroy a BE."

        if not be_name_is_valid(beName):
                print >> sys.stderr, ("The BE name provided is invalid.\n"
                       "Please check it and try again.")
                sys.exit(1)

        zpool = libbe_py.getzpoolbybename(beName)
        if not zpool:
                print >> sys.stderr, ("Failed to find boot "
                    "environment '%s' in any ZFS pool" % beName)
                sys.exit(1)

        root_ds = be_make_root_ds(beName, zpool)

        # We need to get the device list now, since we need the BE to exist in
        # order to find it.
        ret, devlist = be_get_boot_device_list(beName, zpool, root_ds)
        if ret != 0:
                return ret

        # Do the ZFS part of the destruction
        ret = libbe_py.beDestroy(beName, destroy_snaps, force_unmount)
        if ret != 0:
                return ret

        if zones.getzoneid() == 0:
                with get_boot_config(zpool) as mybc:
                        # Delete all menu entries whose bootfs are root_ds
                        mybc.delete_boot_instance(
                            lambda x: getattr(x, "bootfs", None) == root_ds,
                            True)
                        commit(mybc, devlist)

        return 0

def beActivate(beName):
        "Activate a BE."

        if not be_name_is_valid(beName):
                print >> sys.stderr, ("The BE name provided is invalid.\n"
                       "Please check it and try again.")
                sys.exit(1)

        zpool = libbe_py.getzpoolbybename(beName)
        if not zpool:
                print >> sys.stderr, ("Failed to find boot "
                    "environment '%s' in any ZFS pool" % beName)
                sys.exit(1)

        root_ds = be_make_root_ds(beName, zpool)

        if zones.getzoneid() == 0:
                ret, devlist = be_get_boot_device_list(beName, zpool, root_ds)

                with get_boot_config(zpool) as mybc:
                        bis = get_menu_entries(mybc, zpool, root_ds)
                        if not bis:
                                bi = be_append_menu(mybc, root_ds)
                        else:  # Pick the first one
                                bi = bis[0]

                        bi.default = True
                        mp = None
                        try:
                                mp = prep_be_for_bootloader_installation(mybc,
                                         bi, beName, zpool, root_ds)

                                commit(mybc, devlist)
                                # Note: No need to clean up
                                # mybc.boot_loader.data_source_boot_instance
                                # or bi.rootpath, since they'll be unref'ed
                                # after we exit the 'with' clause.
                        except bl.BootLoaderInstallError, e:
                                print >> sys.stderr, e.msg
                                return libbe_py.errorcode["BE_ERR_BOOTFILE_INST"]
                        finally:
                                if not mp is None:
                                        ret = libbe_py.beUnmount(beName)
                                        if ret != 0:
                                                raise RuntimeError("Unable to "
                                                      "unmount BE %s" % beName)
                                        os.rmdir(mp)


        # Run the bottom half of be_activate, which does all the ZFS twiddling
        # necessary to make beName the new active BE.
        # XXX Eventually, pybootmgmt should handle this
        ret = libbe_py.beActivateBH(beName, root_ds, zpool)
        return ret

def get_active_be():
        """Return a dict describing the active boot environment."""

        rc, bes = beList()
        for be in bes:
                if be.get("active"):
                        return be

def get_kargs_and_mountpt(entnum):
        """Helper function for reboot(1M).  Returns a tuple of (1) string with the following
        concatenated (with spaces separating the components): the mountpoint of
        the BE, the kernel file, and the kernel arguments for the menu item
        indexed by 'entnum'; (2) the mountpoint for the BE associated with that entry
        (it will mount the BE on a temporary directory), and (3) an error code.  If
        'entnum' is -1, return the details of the default entry.
        Caller is responsible for unmounting the BE and removing the temporary
        directory."""

        try:
                with get_boot_config() as mybc:
                        inst = None
                        if entnum == -1:
                                for bi in mybc.boot_instances:
                                        if bi.default:
                                                inst = bi
                                                break
                        else:
                                try:
                                        inst = mybc.boot_instances[entnum]
                                except IndexError:
                                        err = "BE_ERR_NO_MENU_ENTRY"
                                        return libbe_py.errorcode[err], "", ""

                        if inst is None:
                                return -1, "", ""

                        kisa = ''
                        if platform.processor() == 'i386':
                                kisa = 'amd64'
                        elif platform.processor() == 'sparc':
                                kisa = 'sparcv9'
                        kdict = {'plat': platform_name(), 'kisa': kisa}
                        kernel = getattr(inst, "kernel",
                            "/platform/%(plat)s/kernel/%(kisa)s/unix" % kdict)
                        # XXX - Workaround for now; pybootmgmt should handle these
                        if kernel.find("%(karch)s") is not -1:
                                kernel = kernel % {'karch': kisa}
                        if kernel.find("$ISADIR") is not -1:
                                kernel = kernel.replace("$ISADIR", kisa)

                        bootfs = getattr(inst, "bootfs", None)
                        if bootfs is None:
                                return -1, "", ""

                        mnttab_open()
                        mntent = getmntany(mnt_special=bootfs)
                        mnttab_close()

                        if mntent is not None:
                                mountpt = mntent['mnt_mountp']
                                # Unmount the BE, but only if the mountpt is not root
                                if mountpt != '/':
                                        rc = unmount_zfs(bootfs)
                                        if rc != 0:
                                                print >> sys.stderr, ("Could "
                                                    "not unmount %s" % bootfs)
                                                return -1, "", ""
                                        mountpt = mount_zfs_in_temp_dir(bootfs)
                        else:
                                # BE is not mounted; make a temporary directory and fire
                                # up zfs mount with a temporary mount to get it mounted
                                mountpt = mount_zfs_in_temp_dir(bootfs)

                        if mountpt is None:
                                return -1, "", ""

                        # Finally, concatenate the mountpoint, kernel, and args
                        args = mountpt + " " + kernel + " " + inst.expanded_kargs()
                        
                        return 0, args, mountpt
        except Exception, exc:
                if os.environ.get('BE_PRINT_ERR', None) is not None:
                        print >> sys.stderr, str(exc)
                return -1, "", ""


def get_menu_entries(bootconfig, pool, root_ds):
        if bootconfig.boot_fstype != "zfs":
                raise RuntimeError("Not on ZFS")

        if bootconfig.zfsrp != pool:
                return None

        bis = []

        for bi in bootconfig.boot_instances:
                if getattr(bi, "bootfs", None) == root_ds:
                        bis.append(bi)

        return bis

def be_append_menu(bootconfig, bootfs, title=None):
        bi = bc.SolarisDiskBootInstance(None, fstype="zfs", bootfs=bootfs,
            title=title)
        bootconfig.add_boot_instance(bi)
        return bi

def be_make_root_ds(beName, poolname=None):
        """Given the name of a boot environment, return the name of the dataset
        where that boot environment resides.  If the optional 'poolname'
        argument is None (the default), then the path will be relative to the
        pool root."""

        if poolname:
                return "%s/%s/%s" % (poolname, "ROOT", beName)
        else:
                return "%s/%s" % ("ROOT", beName)

def be_get_boot_device_list(beName, pool, root_ds):
        # let C code in libbe do the rest
        ret, devlist = libbe_py.beGetBootDeviceList(pool)

        return ret, devlist

def prep_be_for_bootloader_installation(bc, bi, beName, pool, root_ds):
        # make sure root_ds is mounted, somewhere
        ret, belist = libbe_py.beList(beName)

        for be in belist:
                if ("snap_name" not in be and
                    be.get("orig_be_pool", None) == pool and
                    be["root_ds"] == root_ds):
                        mounted, mountpoint = be["mounted"], be["mountpoint"]
                        if mounted:
                                needsmount = False
                                break
        else:
                mounted, mountpoint = None, None
                needsmount = True

        # We could assert that mounted and mountpoint are set here, but there's
        # a window between the getzpoolbybename() in beActivate() and here.

        if not mounted:
                mountpoint = tempfile.mkdtemp(prefix="be-%s." % beName)
                ret = libbe_py.beMount(beName, mountpoint)
                if ret != 0:
                        # Sometimes, if there are zones on the system and there are
                        # subordinate filesystems on those zones whose names conflict
                        # with existing non-empty directories, beMount will return
                        # failure, but will NOT actually unmount the BE's filesystem
                        # (and that's all we care about anyway), so check for this
                        # case by seeing if the directory is empty -- if it is NOT,
                        # then we can proceed below.  Otherwise, we'll have to
                        # raise an exception.
                        if len(os.listdir(mountpoint)) == 0:
                                os.rmdir(mountpoint)
                                raise RuntimeError("Unable to mount BE %s" % beName)

        # No need to invoke BootInstance.init_from_rootpath() here, since we
        # don't care about anything other than having a valid root fs
        # associated with this BootInstance
        bi.rootpath = mountpoint

        # get the version of the bootloader package
        cmd = "/usr/bin/pkg -R %s list -Hv " % mountpoint
        ver = None
        for pkgname in bc.boot_loader.pkg_names:
                proc = subprocess.Popen((cmd + pkgname).split(),
                    stdout=subprocess.PIPE)
                output = proc.stdout.read()
                ret = proc.wait()
                if ret != 0:
                        # "be_get_pkg_version: Error invoking pkg(1M) to get "
                        # "version of %s package.\n"
                        continue

                # Did we get useful output?
                try:
                        ver = output[output.index("@") + 1:output.index(" ")]
                except ValueError:
                        # "be_get_pkg_version: error parsing pkg(1M) output to "
                        # "get version of %s package.\n"
                        continue

                break
        else:
                raise bl.BootLoaderInstallError("Can't find a bootloader version "
                                    "for bootloader files stored in BE " + beName)

        # Set the version in the boot_loader so that BootLoader.install() has
        # the information it needs.
        bc.boot_loader.version = ver
        # Set the boot instance to use for boot data (i.e. boot block files)
        bc.boot_loader.data_source_boot_instance = bi

        # If we had to mount the BE, be sure to unmount it here.
        if needsmount:
                return mountpoint

        return None


@contextlib.contextmanager
def get_boot_config(zpool=None):
        if zpool is None:
                rc, zpool = libbe_py.beFindCurrentBE()[0:2]
                if rc != 0:
                        # beFindCurrentBE() can fail if we're on a miniroot.
                        # in that case, perform a best-effort search for the
                        # boot configuration stored on a root pool.  The first
                        # boot configuration that successfully loads is used.
                        rpool_list = libbe_py.beGetRootPoolList()
                else:
                        rpool_list = [zpool]
        else:
                rpool_list = [zpool]

        if not rpool_list:
                raise RuntimeError("Could not determine the root pool.")

        rpool = None
        for rpool in rpool_list:
                try:
                        (boot_cfg,
                         pool_mounted,
                         pool_path) = _try_load_boot_config(rpool)
                except Exception:
                        boot_cfg = None

                if boot_cfg:
                        break

        if not boot_cfg:
                raise RuntimeError ("Could not load the boot configuration "
                    "(tried loading from %s root pool%s: %s)." %
                    ("this" if len(rpool_list) == 1 else "these",
                     "" if len(rpool_list) == 1 else "s",
                     ', '.join(rpool_list)))

        yield boot_cfg

        # If we'd mounted the pool's root dataset, be sure to unmount it.
        if pool_mounted == "no" and pool_path:
                cmd = "/usr/sbin/zfs unmount %s" % rpool
                ret = spawn_command(cmd)[0]
                if ret == 0:
                        try:
                                os.rmdir(pool_path)
                        except OSError:
                                pass


def _try_load_boot_config(rpool):

        # pybootmgmt needs the root dataset to be mounted so it can load the
        # boot loader portion of the boot configuration.  If the root dataset
        # cannot be mounted, that's a fatal problem.

        # Find out whether the pool's root dataset and the BE's root dataset are
        # mounted, and if so, where.
        cmd = "/usr/sbin/zfs list -H -o name,mountpoint,mounted %s" % rpool
        cmd_stdout = spawn_command(cmd)[1]

        # We might have gotten partial results, so gather what information we
        # can, and set the rest to None.
        pool_mounted, pool_path = (None, None)
        for line in cmd_stdout.rstrip().splitlines():
                ds, mtpt, mtd = line.rstrip().split()
                if ds == rpool:
                        pool_mounted = mtd
                        pool_path = mtpt

        # If the pool's root dataset isn't mounted, mount it temporarily.
        if pool_mounted == "no":
                pool_path = tempfile.mkdtemp(prefix="pool-%s." % rpool)
                cmd = "/usr/sbin/zfs mount -o mountpoint=%s %s" % (pool_path,
                                                                   rpool)
                ret = spawn_command(cmd)[0]
                if ret != 0 or not pool_path:
                        try:
                                os.rmdir(pool_path)
                        except Exception:
                                pass
                        return (None, None, None)

        # If pool_path is None, then creation of the DiskBootConfig will
        # fail because the boot loader will not be able to load its config
        # file.
        try:
                boot_cfg = bc.DiskBootConfig([], rpname=rpool,
                                             tldpath=pool_path)
                return (boot_cfg, pool_mounted, pool_path)
        except Exception, exc: # XXX Overly broad, but we must clean up
                               # XXX mounted filesystems at all costs
                if os.environ.get('BE_PRINT_ERR', None) is not None:
                        print >> sys.stderr, ("Error getting boot "
                            "configuration from pool %s: %s" %
                            (rpool, exc))
                if pool_mounted == "no" and pool_path:
                        cmd = "/usr/sbin/zfs unmount %s" % rpool
                        ret = spawn_command(cmd)[0]
                        if ret == 0:
                                try:
                                        os.rmdir(pool_path)
                                except OSError:
                                        pass
                raise



def get_default_boot_instance():
        # The contextmanager construct is a bit weird here.
        with get_boot_config() as mybc:
                for bi in mybc.boot_instances:
                        if bi.default:
                                return bi

def be_name_is_valid(beName):
        """Returns True if the BE name passed in is valid"""

        BE_NAME_MAX_LEN = 64 # from libbe_priv.h

        if beName is None or len(beName) > BE_NAME_MAX_LEN or len(beName) is 0:
                return False

        # A BE name must not be a multi-level dataset name.  We also check
        # that it does not contain the ' ' and '%' characters.  The ' ' is
        # a valid character for datasets, however we don't allow that in a
        # BE name.  The '%' is invalid, but zfs_name_valid() allows it for
        # internal reasons, so we explicitly check for it here.
        for c in beName:
                if c == '/' or c == ' ' or c == '%':
                        return False

        # The BE name must comply with a zfs dataset filesystem.
        if not zfs_name_valid(beName, ZFS_TYPE_FILESYSTEM):
                return False

        return True

def mount_zfs_in_temp_dir(zfsname):
        temp_dir = tempfile.mkdtemp(prefix="libbe-")
        cmd = "/usr/sbin/zfs mount -o mountpoint=%s %s" % (temp_dir, zfsname)
        ret = spawn_command(cmd)[0]
        if ret != 0:
                print >> sys.stderr, ("Failed to mount zfs "
                    "filesystem %s" % zfsname)
                try:
                        os.rmdir(temp_dir)
                except Exception:
                        pass
                return None
        return temp_dir

def unmount_zfs(bootfs):
        cmd = "/usr/sbin/zfs unmount %s" % bootfs
        return spawn_command(cmd)[0]

def spawn_command(cmd):
        args = cmd.split()
        try:
                proc = subprocess.Popen(args, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)
                (cmd_stdout, cmd_stderr) = proc.communicate()
                ret = proc.returncode
                if ret != 0:
                        print >> sys.stderr, ("%s returned %d. "
                                              "STDERR was:\n%s" %
                                              (args[0], ret, cmd_stderr))
                return ret, cmd_stdout
        except OSError, e:
                print >> sys.stderr, "Could not execute '%s': %s" % (cmd, e)
                return -1, None
        except ValueError, valErr:
                print >> sys.stderr, ("Error while creating subprocess.Popen "
                    "object: %s" % valErr)
                return -1, None
