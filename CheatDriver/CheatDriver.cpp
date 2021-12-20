#include <ntddk.h>
 
void DriverUnload(PDRIVER_OBJECT DriverObject) {
  UNREFERENCED_PARAMETER(DriverObject);
  KdPrint(("Cheat driver unloaded\n"));
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject) {
  DriverObject->DriverUnload = DriverUnload;

  KdPrint(("Cheat driver loaded\n"));
  return STATUS_SUCCESS;
}