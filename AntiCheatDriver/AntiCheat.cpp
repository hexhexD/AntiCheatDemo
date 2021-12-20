// clang-format off
#include <ntddk.h>
#include <aux_klib.h>
#include "Common.h"
// clang-format on

#define DBGSTR_PREFIX "AntiCheat: "

extern "C" NTSYSAPI NTSTATUS ObReferenceObjectByName(
    _In_ PUNICODE_STRING ObjectPath, _In_ ULONG Attributes,
    _In_opt_ PACCESS_STATE PassedAccessState,
    _In_opt_ ACCESS_MASK DesiredAccess, _In_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode, _Inout_opt_ PVOID ParseContext,
    _Out_ PVOID *Object);
extern "C" POBJECT_TYPE *IoDriverObjectType;

PVOID BASE = nullptr;
PVOID TEXTSTART;
PVOID TEXTEND = nullptr;
PVOID DEVICECONTROL;

DRIVER_DISPATCH AntiCheatCreateClose;
DRIVER_DISPATCH AntiCheatDeviceIoControl;

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status = STATUS_SUCCESS,
                     ULONG_PTR info = 0) {
  Irp->IoStatus.Status = status;
  Irp->IoStatus.Information = info;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return status;
}

void DriverUnload(PDRIVER_OBJECT DriverObject) {
  UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\AntiCheat");
  IoDeleteSymbolicLink(&symLink);
  IoDeleteDevice(DriverObject->DeviceObject);

  KdPrint(("Cheat driver unloaded\n"));
}

NTSTATUS detectHook() {

  // Get kbdclass base address
  AuxKlibInitialize();
  ULONG modulesSize = 0;
  AUX_MODULE_EXTENDED_INFO *modules;
  ULONG numberOfModules;
  auto status = AuxKlibQueryModuleInformation(
      &modulesSize, sizeof(AUX_MODULE_EXTENDED_INFO), nullptr);
  if (!NT_SUCCESS(status) || modulesSize == 0) {
    KdPrint((DBGSTR_PREFIX "Failed to get Aux buffer size\n"));
  }

  numberOfModules = modulesSize / sizeof(AUX_MODULE_EXTENDED_INFO);
  modules = (AUX_MODULE_EXTENDED_INFO *)ExAllocatePoolWithTag(
      PagedPool, modulesSize, 'OLOY');
  if (modules == nullptr) {
    KdPrint((DBGSTR_PREFIX "Failed to allocate buffer for Aux module\n"));
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  status = AuxKlibQueryModuleInformation(
      &modulesSize, sizeof(AUX_MODULE_EXTENDED_INFO), modules);
  if (!NT_SUCCESS(status)) {
    KdPrint((DBGSTR_PREFIX "Failed to query module info\n"));
  }

  for (ULONG i = 0; i < numberOfModules; ++i) {
    auto const &e = modules[i];
    char *name = (char *)&e.FullPathName[e.FileNameOffset];
    // KdPrint((DBGSTR_PREFIX "Module name: %s\n", name));
    // early return to reduce indentation
    if (strcmp(name, "kbdclass.sys")) {
      continue;
    }

    BASE = e.BasicInfo.ImageBase;
    KdPrint((DBGSTR_PREFIX "kbdclass.sys base address: %p\n", BASE));
    break;
  }

  // Get .text section start and end address
  PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)BASE;
  if (dosHeader->e_magic != 'ZM') {
    KdPrint((DBGSTR_PREFIX "Invalid PE magic\n"));
  }
  PIMAGE_NT_HEADERS ntHeader =
      (PIMAGE_NT_HEADERS)((char *)BASE + dosHeader->e_lfanew);
  PIMAGE_OPTIONAL_HEADER optionalHeader = &ntHeader->OptionalHeader;
  PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeader);

  for (unsigned i = 0; i < ntHeader->FileHeader.NumberOfSections; ++i) {
    if (!strcmp((char *)sectionHeader->Name, ".text")) {
      TEXTSTART = (char *)BASE + sectionHeader->VirtualAddress;
      TEXTEND = (char *)BASE + sectionHeader->VirtualAddress +
                sectionHeader->Misc.VirtualSize;
      KdPrint((DBGSTR_PREFIX "kbdclass.sys .text starts at offset: %p\n",
               TEXTSTART));
      KdPrint((DBGSTR_PREFIX "kbdclass.sys .text ends at: %p\n", TEXTEND));
    }
    sectionHeader++;
  }

  TEXTSTART = (char *)BASE + optionalHeader->BaseOfCode;
  // Sanity check
  KdPrint((DBGSTR_PREFIX "kbdclass.sys .text starts at: %p\n", TEXTSTART));

  // Get kbdclass.sys device control address
  UNICODE_STRING name;
  RtlInitUnicodeString(&name, L"\\driver\\kbdclass");
  PDRIVER_OBJECT driver;
  status = ObReferenceObjectByName(&name, OBJ_CASE_INSENSITIVE, nullptr, 0,
                                   *IoDriverObjectType, KernelMode, nullptr,
                                   (PVOID *)&driver);
  if (!NT_SUCCESS(status)) {
    KdPrint((DBGSTR_PREFIX "Failed to get kbdclass device object\n"));
  }
  DEVICECONTROL = driver->MajorFunction[IRP_MJ_DEVICE_CONTROL];
  KdPrint((DBGSTR_PREFIX "device control at : %p\n", DEVICECONTROL));
  UINT64 mask = 0xFFFFFFFFFFFFULL;
  KdPrint((DBGSTR_PREFIX "Before if\n"));
  // Got'em
  // I counldn't get this to work even the dbg messages look correct lol
  if (((UINT64)DEVICECONTROL & mask) < ((UINT64)TEXTSTART & mask) ||
    ((UINT64)DEVICECONTROL & mask) > ((UINT64)TEXTEND & mask)) {
    KdPrint((DBGSTR_PREFIX "kbdclass device control is hooked"));
  }
  KdPrint((DBGSTR_PREFIX "After if\n"));

  ObDereferenceObject(driver);
  return STATUS_SUCCESS;
}
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject) {
  KdPrint((DBGSTR_PREFIX "Loading anti cheat driver\n"));
  DriverObject->DriverUnload = DriverUnload;
  DriverObject->MajorFunction[IRP_MJ_CREATE] = AntiCheatCreateClose;
  DriverObject->MajorFunction[IRP_MJ_CLOSE] = AntiCheatCreateClose;
  DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AntiCheatDeviceIoControl;

  // setup device object
  UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\AntiCheat");
  UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\AntiCheat");
  PDEVICE_OBJECT DeviceObject = nullptr;
  auto status = STATUS_SUCCESS;
  do {
    status = IoCreateDevice(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN,
                            0, FALSE, &DeviceObject);
    if (!NT_SUCCESS(status)) {
      KdPrint((DBGSTR_PREFIX "failed to create device (0x%08X)\n", status));
      break;
    }
    status = IoCreateSymbolicLink(&symLink, &deviceName);
    if (!NT_SUCCESS(status)) {
      KdPrint(
          (DBGSTR_PREFIX "failed to create symbolic link (0x%08X)\n", status));
      break;
    }
  } while (false);

  if (!NT_SUCCESS(status)) {
    if (DeviceObject)
      IoDeleteDevice(DeviceObject);
    return status;
  }

  detectHook();

  return STATUS_SUCCESS;
}

NTSTATUS AntiCheatCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  UNREFERENCED_PARAMETER(DeviceObject);
  KdPrint((DBGSTR_PREFIX "CreateClose called \n"));
  return CompleteIrp(Irp);
}

NTSTATUS AntiCheatDeviceIoControl(PDEVICE_OBJECT, PIRP Irp) {
  auto stack = IoGetCurrentIrpStackLocation(Irp);
  auto status = STATUS_SUCCESS;

  switch (stack->Parameters.DeviceIoControl.IoControlCode) {
  case IOCTL_ANTI_CHEAT_SCAN_HOOK: {
    if (stack->Parameters.DeviceIoControl.InputBufferLength <
        sizeof(MyInfo)) {
      status = STATUS_BUFFER_TOO_SMALL;
      KdPrint((DBGSTR_PREFIX "Buffer too small\n"));
      break;
    }
    auto payload =
        (MyInfo *)stack->Parameters.DeviceIoControl.Type3InputBuffer;
    if (!payload) {
      status = STATUS_INVALID_PARAMETER;
      KdPrint((DBGSTR_PREFIX "Invalid parameter\n"));
      break;
    }
    payload->deviceControl = DEVICECONTROL;
    payload->start = TEXTSTART;
    payload->end = TEXTEND;

    break;
  }
  default:
    status = STATUS_INVALID_DEVICE_REQUEST;
    break;
  }

  return CompleteIrp(Irp, status, 0);
}
