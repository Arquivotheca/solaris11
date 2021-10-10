#!/usr/sbin/dtrace -s
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#pragma D option quiet

dtrace:::BEGIN
{
        printf(
            "%39s/%-17s %-31s %8s %-10s %5s %9s %5s %6s %4s\n",
            "CLIENT",
            "SESSION",
            "REQUEST",
            "TIME(us)",
            "STATUS",
            "MID",
            "PID",
            "TID",
            "FLAGS2",
            "FLAGS");
}

dtrace:::END
{
        printf(
            "%39s/%-17s %-31s %8s %-10s %5s %9s %5s %6s %4s\n",
            "CLIENT",
            "SESSION",
            "REQUEST",
            "TIME(us)",
            "STATUS",
            "MID",
            "PID",
            "TID",
            "FLAGS2",
            "FLAGS");
}

smb:::op-Read-start,
smb:::op-ReadRaw-start,
smb:::op-ReadX-start,
smb:::op-LockAndRead-start,
smb:::op-Write-start,
smb:::op-WriteAndClose-start,
smb:::op-WriteAndUnlock-start,
smb:::op-WriteRaw-start,
smb:::op-WriteX-start,
smb:::op-CheckDirectory-start,
smb:::op-Close-start,
smb:::op-CloseAndTreeDisconnect-start,
smb:::op-ClosePrintFile-start,
smb:::op-Create-start,
smb:::op-CreateDirectory-start,
smb:::op-CreateNew-start,
smb:::op-CreateTemporary-start,
smb:::op-Delete-start,
smb:::op-DeleteDirectory-start,
smb:::op-Echo-start,
smb:::op-Find-start,
smb:::op-FindClose-start,
smb:::op-FindClose2-start,
smb:::op-FindUnique-start,
smb:::op-Flush-start,
smb:::op-GetPrintQueue-start,
smb:::op-Ioctl-start,
smb:::op-LockByteRange-start,
smb:::op-LockingX-start,
smb:::op-LogoffX-start,
smb:::op-Negotiate-start,
smb:::op-NtCancel-start,
smb:::op-NtCreateX-start,
smb:::op-NtTransact-start,
smb:::op-NtTransactSecondary-start,
smb:::op-NtRename-start,
smb:::op-Open-start,
smb:::op-OpenPrintFile-start,
smb:::op-WritePrintFile-start,
smb:::op-OpenX-start,
smb:::op-ProcessExit-start,
smb:::op-QueryInformation-start,
smb:::op-QueryInformation2-start,
smb:::op-QueryInformationDisk-start,
smb:::op-Rename-start,
smb:::op-Search-start,
smb:::op-Seek-start,
smb:::op-SessionSetupX-start,
smb:::op-SetInformation-start,
smb:::op-SetInformation2-start,
smb:::op-Transaction-start,
smb:::op-Transaction2-start,
smb:::op-Transaction2Secondary-start,
smb:::op-TransactionSecondary-start,
smb:::op-TreeConnect-start,
smb:::op-TreeConnectX-start,
smb:::op-TreeDisconnect-start,
smb:::op-UnlockByteRange-start
{
        self->thread = curthread;
        self->start = timestamp;
}

smb:::op-Read-done,
smb:::op-ReadRaw-done,
smb:::op-ReadX-done,
smb:::op-LockAndRead-done,
smb:::op-Write-done,
smb:::op-WriteAndClose-done,
smb:::op-WriteAndUnlock-done,
smb:::op-WriteRaw-done,
smb:::op-WriteX-done,
smb:::op-CheckDirectory-done,
smb:::op-Close-done,
smb:::op-CloseAndTreeDisconnect-done,
smb:::op-ClosePrintFile-done,
smb:::op-Create-done,
smb:::op-CreateDirectory-done,
smb:::op-CreateNew-done,
smb:::op-CreateTemporary-done,
smb:::op-Delete-done,
smb:::op-DeleteDirectory-done,
smb:::op-Echo-done,
smb:::op-Find-done,
smb:::op-FindClose-done,
smb:::op-FindClose2-done,
smb:::op-FindUnique-done,
smb:::op-Flush-done,
smb:::op-GetPrintQueue-done,
smb:::op-Ioctl-done,
smb:::op-LockByteRange-done,
smb:::op-LockingX-done,
smb:::op-LogoffX-done,
smb:::op-Negotiate-done,
smb:::op-NtCancel-done,
smb:::op-NtCreateX-done,
smb:::op-NtTransact-done,
smb:::op-NtTransactSecondary-done,
smb:::op-NtRename-done,
smb:::op-Open-done,
smb:::op-OpenPrintFile-done,
smb:::op-WritePrintFile-done,
smb:::op-OpenX-done,
smb:::op-ProcessExit-done,
smb:::op-QueryInformation-done,
smb:::op-QueryInformation2-done,
smb:::op-QueryInformationDisk-done,
smb:::op-Rename-done,
smb:::op-Search-done,
smb:::op-Seek-done,
smb:::op-SessionSetupX-done,
smb:::op-SetInformation-done,
smb:::op-Transaction-done,
smb:::op-SetInformation2-done,
smb:::op-Transaction2-done,
smb:::op-Transaction2Secondary-done,
smb:::op-TransactionSecondary-done,
smb:::op-TreeConnect-done,
smb:::op-TreeConnectX-done,
smb:::op-TreeDisconnect-done,
smb:::op-UnlockByteRange-done
/self->thread == curthread/
{
        printf("%39s/%-17d %-31s %8d 0x%08x %5d %9d %5d 0x%04x 0x%02x\n",
               args[0]->ci_remote,
               args[1]->soi_sid,
               probename,
               (timestamp - self->start) / 1000,
               args[1]->soi_status,
               args[1]->soi_mid,
               args[1]->soi_pid,
               args[1]->soi_tid,
               args[1]->soi_flags2,
               args[1]->soi_flags);
}
