#include "pch.h"

#define DBGSTR_PREFIX "Cheat: "

extern "C" NTSYSAPI NTSTATUS ObReferenceObjectByName(
    _In_ PUNICODE_STRING ObjectPath, _In_ ULONG Attributes,
    _In_opt_ PACCESS_STATE PassedAccessState,
    _In_opt_ ACCESS_MASK DesiredAccess, _In_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode, _Inout_opt_ PVOID ParseContext,
    _Out_ PVOID *Object);

extern "C" POBJECT_TYPE *IoDriverObjectType;

DRIVER_DISPATCH CheatCreateClose;
DRIVER_DISPATCH CheatRead;

PDRIVER_DISPATCH originalDeviceControl;

long long totalCallNum;

NTSTATUS CheatDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  KdPrint((DBGSTR_PREFIX "kbdclass DeviceIoControl called\n"));
  InterlockedAdd64(&totalCallNum, 1);
  return originalDeviceControl(DeviceObject, Irp);
}

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status = STATUS_SUCCESS,
                     ULONG_PTR info = 0) {
  Irp->IoStatus.Status = status;
  Irp->IoStatus.Information = info;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return status;
}

void DriverUnload(PDRIVER_OBJECT DriverObject) { 
  // Spent 20 minutes at 1am wonrdering why my driver is not unloading because
  // I didn't close the device object lol
  UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Cheat");
  IoDeleteSymbolicLink(&symLink);
  IoDeleteDevice(DriverObject->DeviceObject);

  KdPrint(("Cheat driver unloaded\n")); 
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject) {
  KdPrint((DBGSTR_PREFIX "Loading cheat driver\n"));
  totalCallNum = 0;
  DriverObject->DriverUnload = DriverUnload;
  DriverObject->MajorFunction[IRP_MJ_CREATE] = CheatCreateClose;
  DriverObject->MajorFunction[IRP_MJ_CLOSE] = CheatCreateClose;
  DriverObject->MajorFunction[IRP_MJ_READ] = CheatRead;

  // setup device object
  UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\Cheat");
  UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Cheat");
  PDEVICE_OBJECT DeviceObject = nullptr;
  auto status = STATUS_SUCCESS;
  do {
    status = IoCreateDevice(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN,
      0, FALSE, &DeviceObject);
    if (!NT_SUCCESS(status)) {
      KdPrint((DBGSTR_PREFIX "failed to create device (0x%08X)\n",
        status));
      break;
    }
    // we direct io bois
    DeviceObject->Flags |= DO_DIRECT_IO;
    status = IoCreateSymbolicLink(&symLink, &deviceName);
    if (!NT_SUCCESS(status)) {
      KdPrint((DBGSTR_PREFIX "failed to create symbolic link (0x%08X)\n",
        status));
      break;
    }
  } while (false);

  if (!NT_SUCCESS(status)) {
    if (DeviceObject)
      IoDeleteDevice(DeviceObject);
    return status;
  }

  // Hook dispatch routine
  UNICODE_STRING name;
  RtlInitUnicodeString(&name, L"\\driver\\kbdclass");
  PDRIVER_OBJECT driver;
  status = ObReferenceObjectByName(
      &name, OBJ_CASE_INSENSITIVE, nullptr, 0, *IoDriverObjectType, KernelMode,
      nullptr, (PVOID *)&driver);
  if (NT_SUCCESS(status)) {

    originalDeviceControl = (PDRIVER_DISPATCH)InterlockedExchangePointer(
        (PVOID *)&driver->MajorFunction[IRP_MJ_DEVICE_CONTROL],
        CheatDeviceControl);
    KdPrint((DBGSTR_PREFIX "kbdclass DeviceIoControl hooked\n"));

    ObDereferenceObject(driver);
  }

  return STATUS_SUCCESS;
}

NTSTATUS CheatCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  UNREFERENCED_PARAMETER(DeviceObject);
  KdPrint((DBGSTR_PREFIX "CreateClose called \n"));
  return CompleteIrp(Irp);
}

NTSTATUS CheatRead(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
  UNREFERENCED_PARAMETER(DeviceObject);
  KdPrint((DBGSTR_PREFIX "Read called \n"));

  auto stack = IoGetCurrentIrpStackLocation(Irp);
  auto len = stack->Parameters.Read.Length;
  if (len == 0)
    return CompleteIrp(Irp, STATUS_INVALID_BUFFER_SIZE);

  auto buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
  if (!buffer)
    return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES);

  RtlCopyMemory(buffer, &totalCallNum, sizeof(totalCallNum));

  return CompleteIrp(Irp, STATUS_SUCCESS, sizeof(totalCallNum));
}