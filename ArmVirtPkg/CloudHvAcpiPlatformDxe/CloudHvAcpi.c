#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <IndustryStandard/Acpi63.h>
#include <Protocol/AcpiTable.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/DebugLib.h>

#define ACPI_ENTRY_SIZE 8
#define XSDT_LEN 36

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

EFI_STATUS
EFIAPI
InstallCloudHvAcpiTables (
 IN     EFI_ACPI_TABLE_PROTOCOL       *AcpiProtocol
 )
{
  UINTN InstalledKey, TableSize;
  UINT64 Rsdp, Xsdt, table_offset, PointerValue;
  EFI_STATUS Status = 0;
  int size;

  Rsdp = PcdGet64 (PcdAcpiRsdpBaseAddress);
  Xsdt = ((EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER *)Rsdp)->XsdtAddress;
  PointerValue = Xsdt;
  table_offset = Xsdt;
  size = ((EFI_ACPI_COMMON_HEADER *)PointerValue)->Length - XSDT_LEN;
  table_offset += XSDT_LEN;

  while(!Status && size > 0) {
    PointerValue = *(UINT64 *)table_offset;
    TableSize = ((EFI_ACPI_COMMON_HEADER *)PointerValue)->Length;
    Status = AcpiProtocol->InstallAcpiTable (AcpiProtocol,
             (VOID *)(UINT64)PointerValue, TableSize,
             &InstalledKey);
    table_offset += ACPI_ENTRY_SIZE;
    size -= ACPI_ENTRY_SIZE;
  }

  return Status;
}

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

