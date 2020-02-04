/** @file
  VMGEXIT Support Library.

  Copyright (c) 2019, Advanced Micro Devices, Inc. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Base.h>
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Register/Amd/Ghcb.h>
#include <Register/Amd/Msr.h>

STATIC
UINT64
VmgExitErrorCheck (
  GHCB                *Ghcb
  )
{
  GHCB_EVENT_INJECTION  Event;
  GHCB_EXIT_INFO        ExitInfo;
  UINT64                Status;

  ExitInfo.Uint64 = Ghcb->SaveArea.SwExitInfo1;
  ASSERT ((ExitInfo.Elements.Lower32Bits == 0) ||
          (ExitInfo.Elements.Lower32Bits == 1));

  Status = 0;
  if (ExitInfo.Elements.Lower32Bits == 0) {
    return Status;
  }

  if (ExitInfo.Elements.Lower32Bits == 1) {
    ASSERT (Ghcb->SaveArea.SwExitInfo2 != 0);

    // Check that the return event is valid
    Event.Uint64 = Ghcb->SaveArea.SwExitInfo2;
    if (Event.Elements.Valid &&
        Event.Elements.Type == GHCB_EVENT_INJECTION_TYPE_EXCEPTION) {
      switch (Event.Elements.Vector) {
      case GP_EXCEPTION:
      case UD_EXCEPTION:
        // Use returned event as return code
        Status = Event.Uint64;
      }
    }
  }

  if (Status == 0) {
    GHCB_EVENT_INJECTION  Event;

    Event.Uint64 = 0;
    Event.Elements.Vector = GP_EXCEPTION;
    Event.Elements.Type   = GHCB_EVENT_INJECTION_TYPE_EXCEPTION;
    Event.Elements.Valid  = 1;

    Status = Event.Uint64;
  }

  return Status;
}

UINT64
EFIAPI
VmgExit (
  GHCB                *Ghcb,
  UINT64              ExitCode,
  UINT64              ExitInfo1,
  UINT64              ExitInfo2
  )
{
  Ghcb->SaveArea.SwExitCode = ExitCode;
  Ghcb->SaveArea.SwExitInfo1 = ExitInfo1;
  Ghcb->SaveArea.SwExitInfo2 = ExitInfo2;

  //
  // Guest memory is used for the guest-hypervisor communication, so fence
  // the invocation of the VMGEXIT instruction to ensure GHCB accesses are
  // synchronized properly.
  //
  MemoryFence ();
  AsmVmgExit ();
  MemoryFence ();

  return VmgExitErrorCheck (Ghcb);
}

VOID
EFIAPI
VmgInit (
  GHCB                *Ghcb
  )
{
  SetMem (&Ghcb->SaveArea, sizeof (Ghcb->SaveArea), 0);
}

VOID
EFIAPI
VmgDone (
  GHCB                *Ghcb
  )
{
}

UINTN
EFIAPI
VmgMmio (
  UINT8               *MmioAddress,
  UINT8               *Buffer,
  UINTN               Bytes,
  BOOLEAN             Write
  )
{
  UINT64                    MmioOp, ExitInfo1, ExitInfo2, Status;
  GHCB                      *Ghcb;
  MSR_SEV_ES_GHCB_REGISTER  Msr;

  Msr.GhcbPhysicalAddress = AsmReadMsr64 (MSR_SEV_ES_GHCB);
  Ghcb = Msr.Ghcb;

  VmgInit (Ghcb);

  if (Write) {
    MmioOp = SvmExitMmioWrite;
  } else {
    MmioOp = SvmExitMmioRead;
  }

  ExitInfo1 = (UINT64) (UINTN) MmioAddress;
  ExitInfo2 = Bytes;

  if (Write) {
    CopyMem (Ghcb->SharedBuffer, Buffer, Bytes);
  }

  Ghcb->SaveArea.SwScratch = (UINT64) (UINTN) Ghcb->SharedBuffer;
  Status = VmgExit (Ghcb, MmioOp, ExitInfo1, ExitInfo2);
  if (Status != 0) {
    return Status;
  }

  if (!Write) {
    CopyMem (Buffer, Ghcb->SharedBuffer, Bytes);
  }

  VmgDone (Ghcb);

  return 0;
}

VOID
EFIAPI
VmgMmioWrite (
  UINT8               *Dest,
  UINT8               *Src,
  UINTN                Bytes
  )
{
  VmgMmio (Dest, Src, Bytes, TRUE);
}

UINTN
EFIAPI
VmgExitSetAPJumpTable (
  EFI_PHYSICAL_ADDRESS  Address
  )
{
  UINT64                    ExitInfo1, ExitInfo2, Status;
  GHCB                      *Ghcb;
  MSR_SEV_ES_GHCB_REGISTER  Msr;

  Msr.GhcbPhysicalAddress = AsmReadMsr64 (MSR_SEV_ES_GHCB);
  Ghcb = Msr.Ghcb;

  VmgInit (Ghcb);

  ExitInfo1 = 0;
  ExitInfo2 = (UINT64) (UINTN) Address;

  Status = VmgExit (Ghcb, SvmExitApJumpTable, ExitInfo1, ExitInfo2);

  VmgDone (Ghcb);

  return Status;
}

