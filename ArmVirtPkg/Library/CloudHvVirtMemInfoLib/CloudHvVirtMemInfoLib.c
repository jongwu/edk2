/** @file

  Copyright (c) 2022, Arm Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>

#include <Base.h>
#include <libfdt.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>

#include<Library/PrePiLib.h>

// The first memnory node is the one whose base address is the lowest.
UINT64 FirMemNodeBase = 0, FirMemNodeSize = 0;
//
// Cloud Hypervisor may have more than one memory nodes. Even there is no limit for that,
// I think 10 is enough in general.
//
#define CLOUDHV_MAX_MEM_NODE_NUM 10

// Record memory node info (base address and size)
struct CloudHvMemNodeInfo {
  UINT64 Base;
  UINT64 Size;
};

struct CloudHvMemNodeInfo CloudHvMemNode[CLOUDHV_MAX_MEM_NODE_NUM];

RETURN_STATUS
EFIAPI
CloudHvVirtMemInfoPeiLibConstructor (
  VOID
  )
{
  VOID           *DeviceTreeBase;
  EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttributes;
  INT32          Node, Prev;
  UINT64         CurBase, MemBase;
  UINT64         CurSize;
  CONST CHAR8    *Type;
  INT32          Len;
  CONST UINT64   *RegProp;
  RETURN_STATUS  PcdStatus;
  UINT8          Index;

  ZeroMem (CloudHvMemNode, sizeof(CloudHvMemNode[0]) * CLOUDHV_MAX_MEM_NODE_NUM);

  Index = 0;
  MemBase = FixedPcdGet64 (PcdSystemMemoryBase);
  ResourceAttributes = (
                        EFI_RESOURCE_ATTRIBUTE_PRESENT |
                        EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
                        EFI_RESOURCE_ATTRIBUTE_TESTED
                        );
  DeviceTreeBase = (VOID *)(UINTN)PcdGet64 (PcdDeviceTreeInitialBaseAddress);
  ASSERT (DeviceTreeBase != NULL);

  //
  // Make sure we have a valid device tree blob
  //
  ASSERT (fdt_check_header (DeviceTreeBase) == 0);

  //
  // Look for the lowest memory node
  //
  for (Prev = 0; ; Prev = Node) {
    Node = fdt_next_node (DeviceTreeBase, Prev, NULL);
    if (Node < 0) {
      break;
    }

    //
    // Check for memory node
    //
    Type = fdt_getprop (DeviceTreeBase, Node, "device_type", &Len);
    if (Type && (AsciiStrnCmp (Type, "memory", Len) == 0)) {
      //
      // Get the 'reg' property of this node. For now, we will assume
      // two 8 byte quantities for base and size, respectively.
      //
      RegProp = fdt_getprop (DeviceTreeBase, Node, "reg", &Len);
      if ((RegProp != 0) && (Len == (2 * sizeof (UINT64)))) {
        CurBase = fdt64_to_cpu (ReadUnaligned64 (RegProp));
        CurSize = fdt64_to_cpu (ReadUnaligned64 (RegProp + 1));

        DEBUG ((
          DEBUG_INFO,
          "%a: System RAM @ 0x%lx - 0x%lx\n",
          __FUNCTION__,
          CurBase,
          CurBase + CurSize - 1
          ));

        if (CurBase == MemBase) {
            FirMemNodeBase = CurBase;
            FirMemNodeSize = CurSize;
        }

        CloudHvMemNode[Index].Base = CurBase;
        CloudHvMemNode[Index].Size = CurSize;
        Index++;
        // We should build Hob seperately for the memory node except the first one
/*        if (CurBase != MemBase) {
          BuildResourceDescriptorHob (
            EFI_RESOURCE_SYSTEM_MEMORY,
            ResourceAttributes,
            CurBase,
            CurSize
            );
        }*/
        if (Index >= CLOUDHV_MAX_MEM_NODE_NUM) {
          DEBUG ((
            DEBUG_WARN,
            "%a: memory node larger than %d will not be included into Memory System\n",
            __FUNCTION__,
            CLOUDHV_MAX_MEM_NODE_NUM
            ));
          break;
        }
      } else {
        DEBUG ((
          DEBUG_ERROR,
          "%a: Failed to parse FDT memory node\n",
          __FUNCTION__
          ));
      }
    }
  }

  //
  // Make sure the start of DRAM matches our expectation
  //
  ASSERT (FixedPcdGet64 (PcdSystemMemoryBase) == FirMemNodeBase);
  PcdStatus = PcdSet64S (PcdSystemMemorySize, FirMemNodeSize);
  ASSERT_RETURN_ERROR (PcdStatus);
  ASSERT (
    (((UINT64)PcdGet64 (PcdFdBaseAddress) +
      (UINT64)PcdGet32 (PcdFdSize)) <= FirMemNodeBase) ||
    ((UINT64)PcdGet64 (PcdFdBaseAddress) >= (FirMemNodeBase + FirMemNodeSize))
    );

  return RETURN_SUCCESS;
}

// Number of Virtual Memory Map Descriptors
#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS  (4 + CLOUDHV_MAX_MEM_NODE_NUM)

//
// Core peripherals such as the UART, the GIC and the RTC are
// all mapped in the 'miscellaneous device I/O' region, which we just map
// in its entirety rather than device by device. Note that it does not
// cover any of the NOR flash banks or PCI resource windows.
//
#define MACH_VIRT_PERIPH_BASE  0x00400000
#define MACH_VIRT_PERIPH_SIZE  0x0FC00000

//
// The top of the 64M memory region under 4GB reserved for device
//
#define TOP_32BIT_DEVICE_BASE  0xFC000000
#define TOP_32BIT_DEVICE_SIZE  0x04000000

/**
  Return the Virtual Memory Map of your platform

  This Virtual Memory Map is used by MemoryInitPei Module to initialize the MMU
  on your platform.

  @param[out]   VirtualMemoryMap    Array of ARM_MEMORY_REGION_DESCRIPTOR
                                    describing a Physical-to-Virtual Memory
                                    mapping. This array must be ended by a
                                    zero-filled entry. The allocated memory
                                    will not be freed.

**/
VOID
ArmVirtGetMemoryMap (
  OUT ARM_MEMORY_REGION_DESCRIPTOR  **VirtualMemoryMap
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR  *VirtualMemoryTable;
  UINT8 Index = 0, i = 0;

  ASSERT (VirtualMemoryMap != NULL);

  VirtualMemoryTable = AllocatePool (
                         sizeof (ARM_MEMORY_REGION_DESCRIPTOR) *
                         MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS
                         );

  if (VirtualMemoryTable == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Error: Failed AllocatePool()\n", __FUNCTION__));
    return;
  }
    // System DRAM
  while (CloudHvMemNode[i].Size != 0) {
    VirtualMemoryTable[Index].PhysicalBase = CloudHvMemNode[i].Base;
    VirtualMemoryTable[Index].VirtualBase  = CloudHvMemNode[i].Base;
    VirtualMemoryTable[Index].Length       = CloudHvMemNode[i].Size;
    VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;

    DEBUG ((
      DEBUG_INFO,
      "%a: Dumping System DRAM Memory Node%d Map:\n"
      "\tPhysicalBase: 0x%lX\n"
      "\tVirtualBase: 0x%lX\n"
      "\tLength: 0x%lX\n",
      __FUNCTION__,
      i,
      VirtualMemoryTable[Index].PhysicalBase,
      VirtualMemoryTable[Index].VirtualBase,
      VirtualMemoryTable[Index].Length
      ));
    Index++;
    i++;
  }
  // Memory mapped peripherals (UART, RTC, GIC, virtio-mmio, etc)
  VirtualMemoryTable[Index].PhysicalBase = MACH_VIRT_PERIPH_BASE;
  VirtualMemoryTable[Index].VirtualBase  = MACH_VIRT_PERIPH_BASE;
  VirtualMemoryTable[Index].Length       = MACH_VIRT_PERIPH_SIZE;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;
  Index++;

  // Map the FV region as normal executable memory
  VirtualMemoryTable[Index].PhysicalBase = PcdGet64 (PcdFvBaseAddress);
  VirtualMemoryTable[Index].VirtualBase  = VirtualMemoryTable[Index].PhysicalBase;
  VirtualMemoryTable[Index].Length       = FixedPcdGet32 (PcdFvSize);
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK;
  Index++;

  // Memory mapped for 32bit device (like TPM)
  VirtualMemoryTable[Index].PhysicalBase = TOP_32BIT_DEVICE_BASE;
  VirtualMemoryTable[Index].VirtualBase  = TOP_32BIT_DEVICE_BASE;
  VirtualMemoryTable[Index].Length       = TOP_32BIT_DEVICE_SIZE;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;
  Index++;

  // End of Table
  ZeroMem (&VirtualMemoryTable[Index], sizeof (ARM_MEMORY_REGION_DESCRIPTOR));

  *VirtualMemoryMap = VirtualMemoryTable;
}
