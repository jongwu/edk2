/** @file
  Install Acpi tables for Cloud Hypervisor

  Copyright (c) 2021, Arm Limited. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <IndustryStandard/Acpi63.h>
#include <Protocol/AcpiTable.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/DebugLib.h>

/**
   Find Acpi table Protocol and return it
**/
STATIC
EFI_ACPI_TABLE_PROTOCOL *
FindAcpiTableProtocol (
  VOID
  )
{
  EFI_STATUS              Status;
  EFI_ACPI_TABLE_PROTOCOL *AcpiTable;

  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID**)&AcpiTable
                  );
  ASSERT_EFI_ERROR (Status);
  return AcpiTable;
}

/** Install Acpi tables for Cloud Hypervisor
 
  @param [in]  AcpiProtocol  Acpi Protocol which is used to install Acpi talbles

  @return EFI_SUCCESS            The table was successfully inserted.
  @return EFI_INVALID_PARAMETER  Either AcpiTableBuffer is NULL, TableKey is NULL, or AcpiTableBufferSize
                                 and the size field embedded in the ACPI table pointed to by AcpiTableBuffer
                                 are not in sync.
  @return EFI_OUT_OF_RESOURCES   Insufficient resources exist to complete the request.
  @retval EFI_ACCESS_DENIED      The table signature matches a table already
                                 present in the system and platform policy
                                 does not allow duplicate tables of this type. 
**/
EFI_STATUS
EFIAPI
InstallCloudHvAcpiTables (
 IN     EFI_ACPI_TABLE_PROTOCOL       *AcpiProtocol
 )
{
  UINTN InstalledKey, TableSize, AcpiTableLength;
  UINT64 RsdpPtr, XsdtPtr, TableOffset, AcpiTablePtr, DsdtPtr = 0;
  EFI_STATUS Status = EFI_SUCCESS;
  BOOLEAN GotFacp = FALSE;

  RsdpPtr = PcdGet64 (PcdCloudHvAcpiRsdpBaseAddress);
  XsdtPtr = ((EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER *) RsdpPtr)->XsdtAddress;
  AcpiTableLength = ((EFI_ACPI_COMMON_HEADER *) XsdtPtr)->Length;
  TableOffset = sizeof (EFI_ACPI_DESCRIPTION_HEADER);

  while (!EFI_ERROR(Status)
    && (TableOffset < AcpiTableLength))
  {
    AcpiTablePtr = *(UINT64 *) (XsdtPtr + TableOffset);
    TableSize = ((EFI_ACPI_COMMON_HEADER *) AcpiTablePtr)->Length;

    //
    // Install ACPI tables from XSDT
    //
    Status = AcpiProtocol->InstallAcpiTable (
		             AcpiProtocol,
                             (VOID *)(UINT64)AcpiTablePtr,
			     TableSize,
                             &InstalledKey
	                     );
    //
    // Get DSDT from FADT
    //
    if (!GotFacp
      && !AsciiStrnCmp ((CHAR8 *) &((EFI_ACPI_COMMON_HEADER *) AcpiTablePtr)->Signature, "FACP", 4))
    {
      DsdtPtr = ((EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE *) AcpiTablePtr)->XDsdt;
      GotFacp = TRUE;
    }

    TableOffset += sizeof (UINT64);
  }

  if (DsdtPtr == 0) {
    DEBUG ((DEBUG_ERROR, "%a: no DSDT found\n", __FUNCTION__));
    ASSERT (FALSE);
    CpuDeadLoop ();
  }

  //
  // Install DSDT table
  //
  TableSize = ((EFI_ACPI_COMMON_HEADER *) DsdtPtr)->Length;
  Status = AcpiProtocol->InstallAcpiTable (
             AcpiProtocol,
             (VOID *)(UINT64) DsdtPtr,
             TableSize,
             &InstalledKey
             );

  return Status;
}

/** Entry point for Cloud Hypervisor Platform Dxe

  @param [in]  ImageHandle  Handle for this image.
  @param [in]  SystemTable  Pointer to the EFI system table.

  @return EFI_SUCCESS            The table was successfully inserted.
  @return EFI_INVALID_PARAMETER  Either AcpiTableBuffer is NULL, TableKey is NULL, or AcpiTableBufferSize
                                 and the size field embedded in the ACPI table pointed to by AcpiTableBuffer
                                 are not in sync.
  @return EFI_OUT_OF_RESOURCES   Insufficient resources exist to complete the request.
  @retval EFI_ACCESS_DENIED      The table signature matches a table already
                                 present in the system and platform policy
                                 does not allow duplicate tables of this type.
**/
EFI_STATUS
EFIAPI
CloudHvAcpiPlatformEntryPoint (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS                         Status;

  Status = InstallCloudHvAcpiTables (FindAcpiTableProtocol ());
  return Status;
}
