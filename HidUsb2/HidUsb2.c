#include <ntifs.h>
#include <hidport.h>
#include <Usb.h>
#include <usbioctl.h>
#include <Usbdlib.h>

/*
00000000 HID_MINI_DEV_EXTENSION struc ; (sizeof=0x54, mappedto_294)
00000000 PnpState        dd ?
00000004 pDevDesc        dd ?                    ; offset
00000008 pInterfaceInfo  dd ?                    ; base 2
0000000C UsbdConfigurationHandle dd ?
00000010 PendingRequestsCount dd ?
00000014 Event           KEVENT ?
00000024 Flags        dd ?
00000028 pWorkItem       dd ?
0000002C HidDesc         _HID_DESCRIPTOR ?
00000035                 db ? ; undefined
00000036                 db ? ; undefined
00000037                 db ? ; undefined
00000038 pFdo            dd ?                    ; offset
0000003C RemoveLock      IO_REMOVE_LOCK ?
00000054 HID_MINI_DEV_EXTENSION ends

00000000 WRKITM_RESET_CONTEXT struc ; (sizeof=0x10, mappedto_303)
00000000 Tag             dd ?
00000004 pWorkItem       dd ?                    ; offset
00000008 pDeviceObject   dd ?                    ; offset
0000000C pIrp            dd ?                    ; offset
00000010 WRKITM_RESET_CONTEXT ends
*/

#define HID_USB_TAG     'UdiH'
#define HID_RESET_TAG   'tesR'
#define HID_REMLOCK_TAG 'WRUH'

const ULONG DEXT_NO_HID_DESC = 0x1;

typedef
struct _HID_MINI_DEV_EXTENSION
{
    ULONG PnpState;
    PUSB_DEVICE_DESCRIPTOR pDevDesc;
    PUSBD_INTERFACE_INFORMATION pInterfaceInfo;
    USBD_CONFIGURATION_HANDLE UsbdConfigurationHandle;
    LONG PendingRequestsCount;
    KEVENT Event;
    ULONG Flags;
    PIO_WORKITEM pWorkItem;
    HID_DESCRIPTOR HidDesc;
    //3 bytes align
    PDEVICE_OBJECT pFdo;
    IO_REMOVE_LOCK RemoveLock;
} HID_MINI_DEV_EXTENSION, *PHID_MINI_DEV_EXTENSION;

typedef
struct _WRKITM_RESET_CONTEXT
{
    ULONG Tag;
    PIO_WORKITEM pWorkItem;
    PDEVICE_OBJECT pDeviceObject;
    PIRP pIrp;
} WRKITM_RESET_CONTEXT, *PWRKITM_RESET_CONTEXT;

NTSTATUS HumGetStringDescriptor(PDEVICE_OBJECT pDevObj, PIRP pIrp);
NTSTATUS HumGetHidDescriptor(PDEVICE_OBJECT pDevObj, PIRP pIrp);
NTSTATUS HumGetReportDescriptor(PDEVICE_OBJECT pDevObj, PIRP pIrp, PBOOLEAN pNeedToCompleteIrp);
NTSTATUS HumGetDeviceAttributes(PDEVICE_OBJECT pDevObj, PIRP pIrp);
NTSTATUS HumGetPhysicalDescriptor(PDEVICE_OBJECT pDevObj, PIRP pIrp);
NTSTATUS HumGetMsGenreDescriptor(PDEVICE_OBJECT pDevObj, PIRP pIrp);
PUSBD_PIPE_INFORMATION GetInterruptInputPipeForDevice(PHID_MINI_DEV_EXTENSION pMiniDevExt);
LONG HumDecrementPendingRequestCount(PHID_MINI_DEV_EXTENSION pMiniDevExt);
NTSTATUS HumReadCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext);
NTSTATUS HumPowerCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext);
VOID HumSetIdleWorker(PDEVICE_OBJECT DeviceObject, PVOID Context);
NTSTATUS HumSetIdle(PDEVICE_OBJECT pDevObj);
NTSTATUS HumCallUSB(PDEVICE_OBJECT pDevObj, PURB pUrb);
NTSTATUS HumGetDescriptorRequest(PDEVICE_OBJECT pDevObj, USHORT Function, CHAR DescriptorType, PVOID *pDescBuffer, PULONG pDescBuffLen, int Unused, CHAR Index, SHORT LangId);
NTSTATUS HumGetDeviceDescriptor(PDEVICE_OBJECT pDevObj, PHID_MINI_DEV_EXTENSION pMiniDevExt);
NTSTATUS HumGetConfigDescriptor(PDEVICE_OBJECT pDevObj, PUSB_CONFIGURATION_DESCRIPTOR *ppConfigDesc, PULONG pConfigDescLen);
NTSTATUS HumGetHidInfo(PDEVICE_OBJECT pDevObj, UINT_PTR pConfigDescr, UINT_PTR TransferrBufferLen);
NTSTATUS HumSelectConfiguration(PDEVICE_OBJECT pDevObj, PUSB_CONFIGURATION_DESCRIPTOR pConfigDesc);
NTSTATUS HumParseHidInterface(HID_MINI_DEV_EXTENSION *pMiniDevExt, PUSB_INTERFACE_DESCRIPTOR pInterface, LONG TotalLength, PUSB_INTERFACE_DESCRIPTOR *ppHidDesc);
NTSTATUS HumCreateClose(PDEVICE_OBJECT pDevObj, PIRP pIrp);
NTSTATUS HumPnP(PDEVICE_OBJECT pDevObj, PIRP pIrp);
NTSTATUS HumSystemControl(PDEVICE_OBJECT pDevObj, PIRP pIrp);
NTSTATUS HumGetSetReport(PDEVICE_OBJECT pDevObj, PIRP pIrp, PBOOLEAN pNeedToCompleteIrp);
NTSTATUS HumAddDevice(PDRIVER_OBJECT pDrvObj, PDEVICE_OBJECT pFdo);
VOID HumUnload(PDRIVER_OBJECT pDrvObj);
NTSTATUS HumGetSetReportCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext);
VOID HumResetWorkItem(PDEVICE_OBJECT DeviceObject, PWRKITM_RESET_CONTEXT pContext);
NTSTATUS HumAbortPendingRequests(PDEVICE_OBJECT pDevObj);
NTSTATUS HumRemoveDevice(PDEVICE_OBJECT pDevObj, PIRP pIrp);
NTSTATUS HumStopDevice(PDEVICE_OBJECT pDevObj);
NTSTATUS HumWriteCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext);
NTSTATUS HumIncrementPendingRequestCount(PHID_MINI_DEV_EXTENSION pMiniDevExt);
NTSTATUS HumQueueResetWorkItem(PDEVICE_OBJECT pDevObj, PIRP pIrp);

LARGE_INTEGER UrbTimeout = { 0xFD050F80, 0xFFFFFFFF };

NTSTATUS HumInternalIoctl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS                status;
    ULONG                   IoControlCode;
    BOOLEAN                 NeedCompleteIrp;
    PIO_STACK_LOCATION      pStack;
    PIO_STACK_LOCATION      pNextStack;
    PHID_DEVICE_EXTENSION   pDevExt;
    PVOID                   pUserBuffer;
    PURB                    pUrb;
    PUSBD_INTERFACE_INFORMATION pInterfaceInfo;
    ULONG                   Index;
    ULONG                   NumOfPipes;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    BOOLEAN                 AcquiredLock;
    PHID_XFER_PACKET        pTransferPacket;
    ULONG                   OutBuffLen;
    PUSBD_PIPE_INFORMATION  pPipeInfo;
    USHORT                  Value;
    USHORT                  UrbLen;

    UrbLen = sizeof(URB);
    AcquiredLock = FALSE;
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    NeedCompleteIrp = TRUE;
    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    5,
    3u,
    10,
    "`+d\x1Bs¨\x1298ˆIˇ+%|\x1F\\Vpò\x048N7ÎS\x1DTW_É-Ëb+ªÚb:0{\x18Ën∆d¨w\x06(oÎ\x0E•_M¨%z-+Nª,ØÊ$-+-+0‚·.Xˇ\x06˙n^jJtÈ-¶<|0Q\v+¶‡Ë");
    */
    status = IoAcquireRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, __FILE__, __LINE__, sizeof(pMiniDevExt->RemoveLock));
    if (NT_SUCCESS(status) == FALSE)
    {
        goto ExitNoLock;
    }

    pStack = IoGetCurrentIrpStackLocation(pIrp);
    AcquiredLock = TRUE;
    IoControlCode = pStack->Parameters.DeviceIoControl.IoControlCode;

    /*
    WPP_RECORDER_SF_Lqq(
    *(void **)&WPP_GLOBAL_Control->StackSize,
    v49,
    v50,
    v51,
    IoControlCode,
    (char)pDevObj_3,
    (char)pIrp);
    */

    switch (IoControlCode)
    {
    case IOCTL_HID_GET_STRING:
    {
        status = HumGetStringDescriptor(pDevObj, pIrp);

        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
            {
            pStack_6 = (PIO_STACK_LOCATION)0xB0013;
            LABEL_70:
            pDevObj_3 = (PDEVICE_OBJECT)status;
            v67 = &pDevObj_3;
            v68 = 0;
            v69 = 4;
            v70 = 0;
            v71 = (int *)&pStack_6;
            v72 = 0;
            v73 = 4;
            v74 = 0;
            _TlgWrite(v43, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
            }
            */
        }

        goto ExitCompleteIrp;
    }
    case IOCTL_HID_GET_FEATURE:
    case IOCTL_HID_SET_FEATURE:
    {
        status = HumGetSetReport(pDevObj, pIrp, &NeedCompleteIrp);
        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v56))
            {
            pDevObj_3 = (PDEVICE_OBJECT)status;
            v67 = &pDevObj_3;
            pStack_6 = (PIO_STACK_LOCATION)IoControlCode;
            v68 = 0;
            v69 = 4;
            v70 = 0;
            v71 = (int *)&pStack_6;
            v72 = 0;
            v73 = 4;
            v74 = 0;
            _TlgWrite(v40, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
            }
            */
        }
        goto ExitCheckNeedCompleteIrp;
    }
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
    {
        status = HumGetHidDescriptor(pDevObj, pIrp);
        goto ExitCompleteIrp;
    }
    case IOCTL_HID_GET_REPORT_DESCRIPTOR:
    {
        status = HumGetReportDescriptor(pDevObj, pIrp, &NeedCompleteIrp);
        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v56))
            {
            pDevObj_3 = (PDEVICE_OBJECT)status;
            v67 = &pDevObj_3;
            pStack_6 = (PIO_STACK_LOCATION)IoControlCode;
            v68 = 0;
            v69 = 4;
            v70 = 0;
            v71 = (int *)&pStack_6;
            v72 = 0;
            v73 = 4;
            v74 = 0;
            _TlgWrite(v40, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
            }
            */
        }
        goto ExitCheckNeedCompleteIrp;
    }
    case IOCTL_HID_ACTIVATE_DEVICE:
    case IOCTL_HID_DEACTIVATE_DEVICE:
    {
        status = STATUS_SUCCESS;
        goto ExitCompleteIrp;
    }
    case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
    {
        status = HumGetDeviceAttributes(pDevObj, pIrp);
        goto ExitCompleteIrp;
    }
    case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:
    {
        if (pStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(USB_IDLE_CALLBACK_INFO))
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));
            pNextStack = IoGetNextIrpStackLocation(pIrp);
            NeedCompleteIrp = FALSE;
            pNextStack->MajorFunction = pStack->MajorFunction;
            pNextStack->Parameters.DeviceIoControl.InputBufferLength = pStack->Parameters.DeviceIoControl.InputBufferLength;
            pNextStack->Parameters.DeviceIoControl.Type3InputBuffer = pStack->Parameters.DeviceIoControl.Type3InputBuffer;
            pNextStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION;
            pNextStack->DeviceObject = pDevExt->NextDeviceObject;
            status = IoCallDriver(pDevExt->NextDeviceObject, pIrp);
            if (NT_SUCCESS(status) == TRUE)
            {
                goto Exit;
            }
        }
        /*
        if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
        {
        pDevObj_3 = (PDEVICE_OBJECT)status;
        v67 = &pDevObj_3;
        pStack_6 = (PIO_STACK_LOCATION)IoControlCode;
        v68 = 0;
        v69 = 4;
        v70 = 0;
        v71 = (int *)&pStack_6;
        v72 = 0;
        v73 = 4;
        v74 = 0;
        _TlgWrite(v41, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
        }
        */
        goto ExitCheckNeedCompleteIrp;
    }
    case IOCTL_HID_SET_OUTPUT_REPORT:
    case IOCTL_HID_GET_INPUT_REPORT:
    {
        status = HumGetSetReport(pDevObj, pIrp, &NeedCompleteIrp);
        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v56))
            {
            pDevObj_3 = (PDEVICE_OBJECT)status;
            v67 = &pDevObj_3;
            pStack_6 = (PIO_STACK_LOCATION)IoControlCode;
            v68 = 0;
            v69 = 4;
            v70 = 0;
            v71 = (int *)&pStack_6;
            v72 = 0;
            v73 = 4;
            v74 = 0;
            _TlgWrite(v40, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
            }
            */
        }
        goto ExitCheckNeedCompleteIrp;
    }
    case IOCTL_GET_PHYSICAL_DESCRIPTOR:
    {
        status = HumGetPhysicalDescriptor(pDevObj, pIrp);
        goto ExitCompleteIrp;
    }
    case IOCTL_HID_GET_INDEXED_STRING:
    {
        status = HumGetStringDescriptor(pDevObj, pIrp);
        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
            {
            pStack_6 = (PIO_STACK_LOCATION)IoControlCode;
            pDevObj_3 = (PDEVICE_OBJECT)status;
            v67 = &pDevObj_3;
            v68 = 0;
            v69 = 4;
            v70 = 0;
            v71 = (int *)&pStack_6;
            v72 = 0;
            v73 = 4;
            v74 = 0;
            _TlgWrite(v43, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
            }
            */
        }
        goto ExitCompleteIrp;
    }
    case IOCTL_HID_GET_MS_GENRE_DESCRIPTOR:
    {
        status = HumGetMsGenreDescriptor(pDevObj, pIrp);
        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
            {
            pStack_6 = (PIO_STACK_LOCATION)IoControlCode;
            pDevObj_3 = (PDEVICE_OBJECT)status;
            v67 = &pDevObj_3;
            v68 = 0;
            v69 = 4;
            v70 = 0;
            v71 = (int *)&pStack_6;
            v72 = 0;
            v73 = 4;
            v74 = 0;
            _TlgWrite(v43, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
            }
            */
        }
        goto ExitCompleteIrp;
    }
    case IOCTL_HID_WRITE_REPORT:
    {
        pTransferPacket = (PHID_XFER_PACKET)pIrp->UserBuffer;
        if (pTransferPacket == NULL || pTransferPacket->reportBuffer == NULL || pTransferPacket->reportBufferLen == 0)
        {
            status = STATUS_DATA_ERROR;
            NeedCompleteIrp = TRUE;
            /*
            if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
            {
            pStack_4 = (PIO_STACK_LOCATION)status;
            v67 = (PDEVICE_OBJECT *)&pStack_4;
            OutBuffLen_1 = 0xB000F;
            v68 = 0;
            v69 = 4;
            v70 = 0;
            v71 = (int *)&OutBuffLen_1;
            v72 = 0;
            v73 = 4;
            v74 = 0;
            _TlgWrite(v47, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
            }
            */
            goto ExitCheckNeedCompleteIrp;
        }
        pUrb = (PURB)ExAllocatePoolWithTag(NonPagedPoolNx, UrbLen, HID_USB_TAG);
        if (pUrb == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            NeedCompleteIrp = TRUE;
            /*
            if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
            {
            pStack_4 = (PIO_STACK_LOCATION)status;
            v67 = (PDEVICE_OBJECT *)&pStack_4;
            OutBuffLen_1 = 0xB000F;
            v68 = 0;
            v69 = 4;
            v70 = 0;
            v71 = (int *)&OutBuffLen_1;
            v72 = 0;
            v73 = 4;
            v74 = 0;
            _TlgWrite(v47, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
            }
            */
            goto ExitCheckNeedCompleteIrp;
        }
        memset(pUrb, 0, UrbLen);
        if (BooleanFlagOn(pMiniDevExt->Flags, DEXT_NO_HID_DESC))
        {
            pPipeInfo = GetInterruptInputPipeForDevice(pMiniDevExt);
            if (pPipeInfo == NULL)
            {
                status = STATUS_DATA_ERROR;
                ExFreePool(pUrb);
                NeedCompleteIrp = TRUE;
                /*
                if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
                {
                pStack_4 = (PIO_STACK_LOCATION)status;
                v67 = (PDEVICE_OBJECT *)&pStack_4;
                OutBuffLen_1 = 0xB000F;
                v68 = 0;
                v69 = 4;
                v70 = 0;
                v71 = (int *)&OutBuffLen_1;
                v72 = 0;
                v73 = 4;
                v74 = 0;
                _TlgWrite(v47, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
                }
                */
                goto ExitCheckNeedCompleteIrp;
            }
            pUrb->UrbControlVendorClassRequest.Index = pPipeInfo->EndpointAddress & ~USB_ENDPOINT_DIRECTION_MASK;
            pUrb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
            pUrb->UrbHeader.Function = URB_FUNCTION_CLASS_ENDPOINT;
            pUrb->UrbControlVendorClassRequest.RequestTypeReservedBits = 0x22;
            pUrb->UrbControlVendorClassRequest.Request = 0x9;
            Value = pTransferPacket->reportId + 0x200;
        }
        else
        {
            NumOfPipes = pMiniDevExt->pInterfaceInfo->NumberOfPipes;
            if (NumOfPipes == 0)
            {
                pUrb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
                pUrb->UrbHeader.Function = URB_FUNCTION_CLASS_INTERFACE;
                pInterfaceInfo = pMiniDevExt->pInterfaceInfo;
                pUrb->UrbControlVendorClassRequest.Index = pInterfaceInfo->InterfaceNumber;
                pUrb->UrbControlVendorClassRequest.RequestTypeReservedBits = 0x22;
                pUrb->UrbControlVendorClassRequest.Request = 0x9;
                Value = pTransferPacket->reportId + 0x200;
            }
            else
            {
                Index = 0;
                pPipeInfo = pMiniDevExt->pInterfaceInfo->Pipes;
                for (;;)
                {
                    if ((USB_ENDPOINT_DIRECTION_OUT(pPipeInfo->EndpointAddress)) && (pPipeInfo->PipeType == UsbdPipeTypeInterrupt))
                    {
                        pUrb->UrbHeader.Length = sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER);
                        pUrb->UrbHeader.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
                        pUrb->UrbBulkOrInterruptTransfer.PipeHandle = pPipeInfo->PipeHandle;
                        pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength = pTransferPacket->reportBufferLen;
                        pUrb->UrbBulkOrInterruptTransfer.TransferBufferMDL = 0;
                        pUrb->UrbBulkOrInterruptTransfer.TransferBuffer = pTransferPacket->reportBuffer;
                        pUrb->UrbBulkOrInterruptTransfer.TransferFlags = USBD_SHORT_TRANSFER_OK;
                        pUrb->UrbBulkOrInterruptTransfer.UrbLink = 0;
                        goto CheckPnpStateAndCallDriver1;
                    }
                    ++Index;
                    pPipeInfo++;
                    if (Index >= NumOfPipes)
                    {
                        break;
                    }
                }

                pUrb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
                pUrb->UrbHeader.Function = URB_FUNCTION_CLASS_INTERFACE;
                pInterfaceInfo = pMiniDevExt->pInterfaceInfo;
                pUrb->UrbControlVendorClassRequest.Index = pInterfaceInfo->InterfaceNumber;
                pUrb->UrbControlVendorClassRequest.RequestTypeReservedBits = 0x22;
                pUrb->UrbControlVendorClassRequest.Request = 0x9;
                Value = pTransferPacket->reportId + 0x200;
            }
        }

        pUrb->UrbControlVendorClassRequest.Value = Value;
        pUrb->UrbControlVendorClassRequest.TransferFlags = 0;
        pUrb->UrbControlVendorClassRequest.TransferBuffer = pTransferPacket->reportBuffer;
        pUrb->UrbControlVendorClassRequest.TransferBufferLength = pTransferPacket->reportBufferLen;

    CheckPnpStateAndCallDriver1:
        IoSetCompletionRoutine(pIrp, HumWriteCompletion, pUrb, TRUE, TRUE, TRUE);
        pNextStack = IoGetNextIrpStackLocation(pIrp);
        pNextStack->Parameters.Others.Argument1 = pUrb;
        pNextStack->MajorFunction = pStack->MajorFunction;
        pNextStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
        pNextStack->DeviceObject = pDevExt->NextDeviceObject;
        InterlockedIncrement(&pMiniDevExt->PendingRequestsCount);
        if (pMiniDevExt->PnpState == 2 || pMiniDevExt->PnpState == 1)
        {
            status = IoCallDriver(pDevExt->NextDeviceObject, pIrp);
            NeedCompleteIrp = FALSE;

            if (NT_SUCCESS(status) == FALSE)
            {
                /*
                if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
                {
                pStack_4 = (PIO_STACK_LOCATION)status;
                v67 = (PDEVICE_OBJECT *)&pStack_4;
                OutBuffLen_1 = 0xB000F;
                v68 = 0;
                v69 = 4;
                v70 = 0;
                v71 = (int *)&OutBuffLen_1;
                v72 = 0;
                v73 = 4;
                v74 = 0;
                _TlgWrite(v47, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
                }
                */
            }
            goto ExitCheckNeedCompleteIrp;
        }
        HumDecrementPendingRequestCount(pMiniDevExt);
        ExFreePool(pUrb);
        status = STATUS_NO_SUCH_DEVICE;
        NeedCompleteIrp = TRUE;
        /*
        if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
        {
        pStack_4 = (PIO_STACK_LOCATION)status;
        v67 = (PDEVICE_OBJECT *)&pStack_4;
        OutBuffLen_1 = 0xB000F;
        v68 = 0;
        v69 = 4;
        v70 = 0;
        v71 = (int *)&OutBuffLen_1;
        v72 = 0;
        v73 = 4;
        v74 = 0;
        _TlgWrite(v47, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
        }
        */
        goto ExitCheckNeedCompleteIrp;
    }
    case IOCTL_HID_READ_REPORT:
    {
        pUserBuffer = pIrp->UserBuffer;
        OutBuffLen = pStack->Parameters.DeviceIoControl.OutputBufferLength;
        if (OutBuffLen == 0 || pUserBuffer == NULL)
        {
            status = STATUS_INVALID_PARAMETER;
        }
        else
        {
            pInterfaceInfo = pMiniDevExt->pInterfaceInfo;
            Index = 0;
            NumOfPipes = pInterfaceInfo->NumberOfPipes;
            if (NumOfPipes == 0)
            {
                status = STATUS_DEVICE_CONFIGURATION_ERROR;
                NeedCompleteIrp = TRUE;
                /*
                if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
                {
                pMiniDevExt_2 = (HID_MINI_DEV_EXTENSION *)status;
                v67 = (PDEVICE_OBJECT *)&pMiniDevExt_2;
                pStack_4 = (PIO_STACK_LOCATION)0xB000B;
                v68 = 0;
                v69 = 4;
                v70 = 0;
                v71 = (int *)&pStack_4;
                v72 = 0;
                v73 = 4;
                v74 = 0;
                _TlgWrite(v48, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
                }
                */
                goto ExitCheckNeedCompleteIrp;
            }
            pPipeInfo = pMiniDevExt->pInterfaceInfo->Pipes;
            while (USB_ENDPOINT_DIRECTION_OUT(pPipeInfo->EndpointAddress) || (pPipeInfo->PipeType != UsbdPipeTypeInterrupt))
            {
                ++Index;
                pPipeInfo++;
                if (Index >= NumOfPipes)
                {
                    status = STATUS_DEVICE_CONFIGURATION_ERROR;
                    NeedCompleteIrp = TRUE;
                    /*
                    if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
                    {
                    pMiniDevExt_2 = (HID_MINI_DEV_EXTENSION *)status;
                    v67 = (PDEVICE_OBJECT *)&pMiniDevExt_2;
                    pStack_4 = (PIO_STACK_LOCATION)0xB000B;
                    v68 = 0;
                    v69 = 4;
                    v70 = 0;
                    v71 = (int *)&pStack_4;
                    v72 = 0;
                    v73 = 4;
                    v74 = 0;
                    _TlgWrite(v48, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
                    }
                    */
                    goto ExitCheckNeedCompleteIrp;
                }
            }

            pUrb = (PURB)ExAllocatePoolWithTag(NonPagedPoolNx, UrbLen, HID_USB_TAG);
            if (pUrb == NULL)
            {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
            else
            {
                memset(pUrb, 0, UrbLen);
                pUrb->UrbHeader.Length = sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER);
                pUrb->UrbHeader.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
                pUrb->UrbBulkOrInterruptTransfer.PipeHandle = pPipeInfo->PipeHandle;
                pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength = OutBuffLen;
                pUrb->UrbBulkOrInterruptTransfer.TransferBuffer = pUserBuffer;
                pUrb->UrbBulkOrInterruptTransfer.TransferBufferMDL = 0;
                pUrb->UrbBulkOrInterruptTransfer.TransferFlags = USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN;
                pUrb->UrbBulkOrInterruptTransfer.UrbLink = 0;

                IoSetCompletionRoutine(pIrp, HumReadCompletion, pUrb, TRUE, TRUE, TRUE);

                pNextStack = IoGetNextIrpStackLocation(pIrp);
                pNextStack->Parameters.Others.Argument1 = pUrb;
                pNextStack->MajorFunction = pStack->MajorFunction;
                pNextStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
                pNextStack->DeviceObject = pDevExt->NextDeviceObject;

                InterlockedIncrement(&pMiniDevExt->PendingRequestsCount);
                if (pMiniDevExt->PnpState == 2 || pMiniDevExt->PnpState == 1)
                {
                    status = IoCallDriver(pDevExt->NextDeviceObject, pIrp);
                    NeedCompleteIrp = FALSE;

                    if (NT_SUCCESS(status) == TRUE)
                    {
                        goto Exit;
                    }
                    goto ExitCheckNeedCompleteIrp;
                }
                HumDecrementPendingRequestCount(pMiniDevExt);
                ExFreePool(pUrb);
                status = STATUS_NO_SUCH_DEVICE;
            }
        }
        NeedCompleteIrp = TRUE;
        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v54, v55))
            {
            pMiniDevExt_2 = (HID_MINI_DEV_EXTENSION *)status;
            v67 = (PDEVICE_OBJECT *)&pMiniDevExt_2;
            pStack_4 = (PIO_STACK_LOCATION)0xB000B;
            v68 = 0;
            v69 = 4;
            v70 = 0;
            v71 = (int *)&pStack_4;
            v72 = 0;
            v73 = 4;
            v74 = 0;
            _TlgWrite(v48, (unsigned __int8 *)&unk_15256, v52, v53, 4, &v66);
            }
            */
        }
        goto ExitCheckNeedCompleteIrp;
    }
    default:
    {
        status = pIrp->IoStatus.Status;
        goto ExitCompleteIrp;
    }
    }

ExitNoLock:
    /*
    LOBYTE(v4) = 2;
    WPP_RECORDER_SF_qq(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    v4,
    3u,
    11,
    "`+d\x1Bs¨\x1298ˆIˇ+%|\x1F\\Vpò\x048N7ÎS\x1DTW_É-Ëb+ªÚb:0{\x18Ën∆d¨w\x06(oÎ\x0E•_M¨%z-+Nª,ØÊ$-+-+0‚·.Xˇ\x06˙n^jJtÈ-¶<|0Q\v+¶‡Ë",
    (char)RemoveLock,
    status);
    */
    pIrp->IoStatus.Status = status;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    return status;

ExitCheckNeedCompleteIrp:
    if (NeedCompleteIrp == FALSE)
    {
        return status;
    }
ExitCompleteIrp:
    pIrp->IoStatus.Status = status;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    if (AcquiredLock)
    {
        IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));
    }
Exit:
    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    5,
    3u,
    13,
    "`+d\x1Bs¨\x1298ˆIˇ+%|\x1F\\Vpò\x048N7ÎS\x1DTW_É-Ëb+ªÚb:0{\x18Ën∆d¨w\x06(oÎ\x0E•_M¨%z-+Nª,ØÊ$-+-+0‚·.Xˇ\x06˙n^jJtÈ-¶<|0Q\v+¶‡Ë");
    */
    return status;
}

NTSTATUS HumPower(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS                status;
    PHID_DEVICE_EXTENSION   pDevExt;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    status = IoAcquireRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, __FILE__, __LINE__, sizeof(pMiniDevExt->RemoveLock));
    if (NT_SUCCESS(status) == FALSE)
    {
        PoStartNextPowerIrp(pIrp);
        pIrp->IoStatus.Status = status;
        pIrp->IoStatus.Information = 0;
        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    }
    else
    {
        IoCopyCurrentIrpStackLocationToNext(pIrp);
        IoSetCompletionRoutine(pIrp, HumPowerCompletion, NULL, TRUE, TRUE, TRUE);
        status = PoCallDriver(pDevExt->NextDeviceObject, pIrp);
    }

    return status;
}

NTSTATUS HumReadCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext)
{
    NTSTATUS                status;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    PURB                    pUrb;
    PHID_DEVICE_EXTENSION   pDevExt;

    status = STATUS_SUCCESS;
    pUrb = (PURB)pContext;
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    if (NT_SUCCESS(pIrp->IoStatus.Status) == TRUE)
    {
        pIrp->IoStatus.Information = pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
    }
    else if (pIrp->IoStatus.Status == STATUS_CANCELLED)
    {
        /*
        if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
        WPP_RECORDER_SF_qq(*(_DWORD *)&WPP_GLOBAL_Control->StackSize, 5, 2u, 23, "^jJtÈ-¶<|0Q\v+¶‡Ë", (char)pIrp, 32);
        */
    }
    else if (pIrp->IoStatus.Status == STATUS_DEVICE_NOT_CONNECTED)
    {
        /*
        WPP_RECORDER_SF_qq(*(_DWORD *)&WPP_GLOBAL_Control->StackSize, 3, 2u, 25, "^jJtÈ-¶<|0Q\v+¶‡Ë", (char)pIrp, 157);
        */
    }
    else
    {
        /*
        WPP_RECORDER_SF_qq(
        *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
        3,
        2u,
        24,
        "^jJtÈ-¶<|0Q\v+¶‡Ë",
        (char)pIrp,
        pIrp->IoStatus.Status);
        */
        status = HumQueueResetWorkItem(pDevObj, pIrp);
    }
    ExFreePool(pUrb);
    if (InterlockedDecrement(&pMiniDevExt->PendingRequestsCount) < 0)
    {
        KeSetEvent(&pMiniDevExt->Event, IO_NO_INCREMENT, FALSE);
    }
    IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));

    return status;
}

NTSTATUS HumWriteCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext)
{
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    PURB                    pUrb;
    PHID_DEVICE_EXTENSION   pDevExt;

    pUrb = (PURB)pContext;
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    if (NT_SUCCESS(pIrp->IoStatus.Status) == TRUE)
    {
        pIrp->IoStatus.Information = pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
    }
    ExFreePool(pUrb);
    if (pIrp->PendingReturned)
    {
        IoMarkIrpPending(pIrp);
    }
    if (_InterlockedDecrement(&pMiniDevExt->PendingRequestsCount) < 0)
    {
        KeSetEvent(&pMiniDevExt->Event, IO_NO_INCREMENT, FALSE);
    }
    IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));

    return STATUS_SUCCESS;
}

NTSTATUS HumPnpCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext)
{
    UNREFERENCED_PARAMETER(pDevObj);
    UNREFERENCED_PARAMETER(pIrp);

    KeSetEvent((PKEVENT)pContext, EVENT_INCREMENT /* 1 */, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}


NTSTATUS HumPowerCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext)
{
    NTSTATUS                status;
    PIO_STACK_LOCATION      pStack;
    PIO_WORKITEM            pWorkItem;
    PHID_DEVICE_EXTENSION   pDevExt;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;

    UNREFERENCED_PARAMETER(pContext);

    status = pIrp->IoStatus.Status;
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    if (pIrp->PendingReturned)
    {
        IoMarkIrpPending(pIrp);
    }

    if (NT_SUCCESS(status) == TRUE)
    {
        pStack = pIrp->Tail.Overlay.CurrentStackLocation;
        if (pStack->MinorFunction == IRP_MN_SET_POWER
            && pStack->Parameters.Power.Type == DevicePowerState
            && pStack->Parameters.Power.State.DeviceState == PowerDeviceD0
            && pMiniDevExt->PnpState == 2)
        {
            if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
            {
                /*
                LOBYTE(v6) = 4;
                WPP_RECORDER_SF_(*(_DWORD *)&WPP_GLOBAL_Control->StackSize, v6, 4u, 16, "ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
                */

                pWorkItem = IoAllocateWorkItem(pDevObj);
                if (pWorkItem)
                {
                    IoQueueWorkItem(pWorkItem, HumSetIdleWorker, 0, (PVOID)pWorkItem);
                }
                else
                {
                    /*
                    LOBYTE(v9) = 2;
                    WPP_RECORDER_SF_(
                    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
                    v9,
                    4u,
                    17,
                    "ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
                    */
                }
            }
            else
            {
                HumSetIdle(pDevObj);
            }
        }
    }
    IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));

    return STATUS_SUCCESS;
}

NTSTATUS HumSetIdle(PDEVICE_OBJECT pDevObj)
{
    NTSTATUS                status;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    PHID_DEVICE_EXTENSION   pDevExt;
    PURB                    pUrb;

    /*
    v2 = WPP_GLOBAL_Control;
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    5,
    5u,
    91,
    "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
    v2 = WPP_GLOBAL_Control;
    }
    */
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    if (pMiniDevExt == NULL)
    {
        status = STATUS_NOT_FOUND;
    }
    else
    {
        USHORT UrbLen;

        UrbLen = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
        pUrb = (PURB)ExAllocatePoolWithTag(NonPagedPoolNx, UrbLen, HID_USB_TAG);
        if (pUrb == NULL)
        {
            //v2 = WPP_GLOBAL_Control;
            status = STATUS_NO_MEMORY;
        }
        else
        {
            memset(pUrb, 0, UrbLen);
            if (BooleanFlagOn(pMiniDevExt->Flags, DEXT_NO_HID_DESC))
            {
                pUrb->UrbHeader.Length = UrbLen;
                pUrb->UrbHeader.Function = URB_FUNCTION_CLASS_ENDPOINT;
                pUrb->UrbControlVendorClassRequest.Value = 0;
                pUrb->UrbControlVendorClassRequest.Index = 0;
                pUrb->UrbControlVendorClassRequest.RequestTypeReservedBits = 0x22;
                pUrb->UrbControlVendorClassRequest.Request = 0xA;
                pUrb->UrbControlVendorClassRequest.TransferFlags = 0;
                pUrb->UrbControlVendorClassRequest.TransferBuffer = 0;
                pUrb->UrbControlVendorClassRequest.TransferBufferLength = 0;
            }
            else
            {
                pUrb->UrbHeader.Length = UrbLen;
                pUrb->UrbHeader.Function = URB_FUNCTION_CLASS_INTERFACE;
                pUrb->UrbControlVendorClassRequest.Index = pMiniDevExt->pInterfaceInfo->InterfaceNumber;
                pUrb->UrbControlVendorClassRequest.RequestTypeReservedBits = 0x22;
                pUrb->UrbControlVendorClassRequest.Request = 0xA;
                pUrb->UrbControlVendorClassRequest.TransferFlags = 0;
                pUrb->UrbControlVendorClassRequest.TransferBuffer = 0;
                pUrb->UrbControlVendorClassRequest.TransferBufferLength = 0;
            }
            status = HumCallUSB(pDevObj, pUrb);
            ExFreePool(pUrb);

            //v2 = WPP_GLOBAL_Control;
        }
    }

    /*
    if (LOWORD(v2->Queue.ListEntry.Flink))
    WPP_RECORDER_SF_(
    *(_DWORD *)&v2->StackSize,
    5,
    5u,
    92,
    "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
    */

    return status;
}

NTSTATUS HumGetDescriptorRequest(PDEVICE_OBJECT pDevObj, USHORT Function, CHAR DescriptorType, PVOID *pDescBuffer, PULONG pDescBuffLen, int Unused, CHAR Index, SHORT LangId)
{
    NTSTATUS status;
    PURB     pUrb;
    USHORT   UrbLen;
    BOOLEAN  BufferAllocated;

    UNREFERENCED_PARAMETER(Unused);
    BufferAllocated = FALSE;

    /*
    v9 = WPP_GLOBAL_Control;
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    LOBYTE(Function) = 5;
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    Function,
    5u,
    93,
    "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
    v9 = WPP_GLOBAL_Control;
    }
    */

    /*
    __annotation("TMF:",
    "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
    "#typev Unknown_cxx00 94 "%0DeviceObject:0x%10!p!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
    "{", "Arg, ItemPtr -- 10", "}",
    "PUBLIC_TMF:")
    */

    UrbLen = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);
    pUrb = (URB *)ExAllocatePoolWithTag(NonPagedPoolNx, UrbLen, HID_USB_TAG);
    if (pUrb == NULL)
    {
        status = STATUS_NO_MEMORY;

        /*
        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 99 "%0URB allocation failed: %10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemNTSTATUS -- 10", "}",
        "PUBLIC_TMF:")
        */
    }
    else
    {
        memset(pUrb, 0, UrbLen);
        if (*pDescBuffer == NULL)
        {
            *pDescBuffer = ExAllocatePoolWithTag(NonPagedPoolNx, *pDescBuffLen, HID_USB_TAG);
            BufferAllocated = TRUE;
            if (*pDescBuffer == NULL)
            {
                status = STATUS_NO_MEMORY;

                /*
                __annotation("TMF:",
                "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 98 "%0Descriptor buffer allocation failed: %10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                "{", "Arg, ItemNTSTATUS -- 10", "}",
                "PUBLIC_TMF:")
                */

                ExFreePool(pUrb);

                goto Exit;
            }
        }
        memset(*pDescBuffer, 0, *pDescBuffLen);
        pUrb->UrbHeader.Function = Function;
        pUrb->UrbHeader.Length = UrbLen;
        pUrb->UrbControlDescriptorRequest.TransferBufferLength = *pDescBuffLen;
        pUrb->UrbControlDescriptorRequest.TransferBufferMDL = 0;
        pUrb->UrbControlDescriptorRequest.TransferBuffer = *pDescBuffer;
        pUrb->UrbControlDescriptorRequest.DescriptorType = DescriptorType;
        pUrb->UrbControlDescriptorRequest.Index = Index;
        pUrb->UrbControlDescriptorRequest.LanguageId = LangId;
        pUrb->UrbControlDescriptorRequest.UrbLink = 0;

        status = HumCallUSB(pDevObj, pUrb);

        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 97 "%0HumCallUSB failed with status:%10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemNTSTATUS -- 10", "}",
            "PUBLIC_TMF:")
            */
        }
        else
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 95 "%0Descriptor:0x%10!p!, length:%11!x!, status:%12!s!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemPtr -- 10", "Arg, ItemLong -- 11", "Arg, ItemNTSTATUS -- 12", "}",
            "PUBLIC_TMF:")
            */

            if (USBD_SUCCESS(pUrb->UrbHeader.Status) == TRUE)
            {
                status = STATUS_SUCCESS;
                *pDescBuffLen = pUrb->UrbControlTransfer.TransferBufferLength;
                ExFreePool(pUrb);
                goto Exit;
            }
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 96 "%0HumCallUSB returned SUCCESS status %10!s!, but URB status 0x%11!x! fails USBD_SUCCESS test." //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemNTSTATUS -- 10", "Arg, ItemLong -- 11", "}",
            "PUBLIC_TMF:")
            */
            status = STATUS_UNSUCCESSFUL;
        }
        if (BufferAllocated == TRUE)
        {
            ExFreePool(*pDescBuffer);
            *pDescBuffLen = 0;
        }
        *pDescBuffLen = 0;
        ExFreePool(pUrb);
    }

Exit:
    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    LOBYTE(v16) = 5;
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    v16,
    5u,
    100,
    "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
    }
    */
    return status;
}

NTSTATUS HumCallUsbComplete(PDEVICE_OBJECT pDeviceObject, PIRP pIrp, PVOID Context)
{
    UNREFERENCED_PARAMETER(pDeviceObject);
    UNREFERENCED_PARAMETER(pIrp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS HumCallUSB(PDEVICE_OBJECT pDevObj, PURB pUrb)
{
    NTSTATUS              status;
    PIRP                  pIrp;
    IO_STATUS_BLOCK       IoStatusBlock;
    PHID_DEVICE_EXTENSION pDevExt;
    KEVENT                Event;

    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    __annotation("TMF:",
    "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
    "#typev Unknown_cxx00 101 " % 0HumCallUSB entry : DeviceObject: % 10!p!, DeviceExtension : % 11!p!" //   LEVEL=TRACE_LEVEL_VERBOSE FLAGS=TRACE_FLAG_USB",
    "{", "Arg, ItemPtr -- 10", "Arg, ItemPtr -- 11", "}",
    "PUBLIC_TMF:")
    }
    */

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    pIrp = IoBuildDeviceIoControlRequest(
        IOCTL_INTERNAL_USB_SUBMIT_URB,
        pDevExt->NextDeviceObject,
        NULL,
        0,
        NULL,
        0,
        TRUE,
        &Event,
        &IoStatusBlock);
    if (pIrp == NULL)
    {
        /*
        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 102 "%0Insufficient resources while allocating Irp" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
        "{", "}",
        "PUBLIC_TMF:")
        */
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        status = IoSetCompletionRoutineEx(pDevObj, pIrp, HumCallUsbComplete, &Event, TRUE, TRUE, TRUE);
        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 103 "%0IoSetCompletionRoutineEx failed with status:%10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemNTSTATUS -- 10", "}",
            "PUBLIC_TMF:")
            */
            IofCompleteRequest(pIrp, IO_NO_INCREMENT);
        }
        else
        {
            IoGetNextIrpStackLocation(pIrp)->Parameters.Others.Argument1 = pUrb;
            status = IofCallDriver(pDevExt->NextDeviceObject, pIrp);
            /*
            if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
            {
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 104 "%0IoCallDriver returned with status:%10!s!" //   LEVEL=TRACE_LEVEL_VERBOSE FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemNTSTATUS -- 10", "}",
            "PUBLIC_TMF:")
            }
            */
            if (status == STATUS_PENDING)
            {
                NTSTATUS StatusWait;
                StatusWait = KeWaitForSingleObject(&Event, Suspended, KernelMode, FALSE, &UrbTimeout);
                if (StatusWait == STATUS_TIMEOUT)
                {
                    /*
                    __annotation("TMF:",
                    "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                    "#typev Unknown_cxx00 105 "%0URB timed out after 5 seconds" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                    "{", "}",
                    "PUBLIC_TMF:")
                    */

                    IoCancelIrp(pIrp);

                    StatusWait = KeWaitForSingleObject(&Event, Suspended, KernelMode, FALSE, NULL);
                    IoStatusBlock.Status = STATUS_IO_TIMEOUT;
                }
                /*
                if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
                {
                __annotation("TMF:",
                "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 106 "%0KeWaitForSingleObject returned with status:%10!s!" //   LEVEL=TRACE_LEVEL_VERBOSE FLAGS=TRACE_FLAG_USB",
                "{", "Arg, ItemNTSTATUS -- 10", "}",
                "PUBLIC_TMF:")
                }
                */
            }
            IofCompleteRequest(pIrp, IO_NO_INCREMENT);
            if (status == STATUS_PENDING)
            {
                status = IoStatusBlock.Status;
            }
            /*
            if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
            {
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 107 "%0HumCallUSB returning status: %10!s!" //   LEVEL=TRACE_LEVEL_VERBOSE FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemNTSTATUS -- 10", "}",
            "PUBLIC_TMF:")

            if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
            {
            LOBYTE(v15) = 5;
            WPP_RECORDER_SF_(
            *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
            v15,
            5u,
            108,
            "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
            }
            }
            */
        }
    }

    return status;
}

NTSTATUS HumInitDevice(PDEVICE_OBJECT pDevObj)
{
    NTSTATUS                      status;
    PUSB_CONFIGURATION_DESCRIPTOR pConfigDescr;
    ULONG                         DescLen;
    PHID_MINI_DEV_EXTENSION       pMiniDevExt;
    PHID_DEVICE_EXTENSION         pDevExt;

    pConfigDescr = NULL;
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    status = HumGetDeviceDescriptor(pDevObj, pMiniDevExt);
    if (NT_SUCCESS(status) == FALSE)
    {
        /*
        if ((unsigned int)dword_16008 > 5)
        {
        if (!_TlgKeywordOn(v13, v14))
        return status;
        v9 = "PNP_DeviceDesc";
        v21 = &TransferrBufferLen;
        v28 = 0;
        v27 = 4;
        v26 = 0;
        v25 = &pMiniDevExt;
        v24 = 0;
        v23 = 4;
        v22 = 0;
        TransferrBufferLen = status;
        _TlgCreateSz((const CHAR **)&v29, v9);
        _TlgWrite(v10, (unsigned __int8 *)&unk_1521F, v11, v12, 5, &v20);
        return status;
        }
        */
    }
    else
    {
        status = HumGetConfigDescriptor(pDevObj, &pConfigDescr, &DescLen);
        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            if ((unsigned int)dword_16008 > 5)
            {
            if (!_TlgKeywordOn(v13, v15))
            return status;
            v9 = "PNP_ConfigDesc";
            LABEL_19:
            v21 = &TransferrBufferLen;
            v28 = 0;
            v27 = 4;
            v26 = 0;
            v25 = &pMiniDevExt;
            v24 = 0;
            v23 = 4;
            v22 = 0;
            TransferrBufferLen = status;
            _TlgCreateSz((const CHAR **)&v29, v9);
            _TlgWrite(v10, (unsigned __int8 *)&unk_1521F, v11, v12, 5, &v20);
            return status;
            }
            */
        }
        else
        {
            status = HumGetHidInfo(pDevObj, (UINT_PTR)pConfigDescr, DescLen);
            if (NT_SUCCESS(status) == FALSE)
            {
                /*
                v7 = "PNP_HIDInfo";
                */
            }
            else
            {
                status = HumSelectConfiguration(pDevObj, pConfigDescr);
                if (NT_SUCCESS(status))
                {
                    HumSetIdle(pDevObj);
                }
                else
                {
                    /*
                    v7 = "PNP_Configuration";
                    TransferrBufferLen = status;
                    v22 = 0;
                    v23 = 4;
                    pMiniDevExt = (HID_MINI_DEV_EXTENSION *)pMiniDevExt->pSelf;
                    v21 = &TransferrBufferLen;
                    v24 = 0;
                    v25 = &pMiniDevExt;
                    v26 = 0;
                    v27 = 4;
                    v28 = 0;
                    _TlgCreateSz((const CHAR **)&v29, v7);
                    _TlgWrite(v8, (unsigned __int8 *)&unk_1521F, v11, v12, 5, &v20);
                    ExFreePool(pConfigDescr);
                    return status;
                    */
                }
            }

            ExFreePool(pConfigDescr);
        }
    }
    return status;
}

NTSTATUS HumGetHidInfo(PDEVICE_OBJECT pDevObj, UINT_PTR pConfigDescr, UINT_PTR TransferrBufferLen)
{
    NTSTATUS                  status;
    PHID_DEVICE_EXTENSION     pDevExt;
    PHID_MINI_DEV_EXTENSION   pMiniDevExt;
    PUSB_INTERFACE_DESCRIPTOR pInterfaceDesc;
    PUSB_INTERFACE_DESCRIPTOR pHidDesc;
    BOOLEAN                   IsHIDClass;

    status = STATUS_SUCCESS;
    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    LOBYTE(pConfigDescr) = 5;
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    pConfigDescr,
    5u,
    10,
    "\\Vpò\x048N7ÎS\x1DTW_É-Ëb+ªÚb:0{\x18Ën∆d¨w\x06(oÎ\x0E•_M¨%z-+Nª,ØÊ$-+-+0‚·.Xˇ\x06˙n^jJtÈ-¶<|0Q\v+¶‡Ë");
    }
    */
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    RtlZeroMemory(&pMiniDevExt->HidDesc, sizeof(pMiniDevExt->HidDesc));
    pInterfaceDesc = (PUSB_INTERFACE_DESCRIPTOR)USBD_ParseConfigurationDescriptorEx(
        (PUSB_CONFIGURATION_DESCRIPTOR)pConfigDescr,
        (PVOID)pConfigDescr,
        -1,
        -1,
        USB_DEVICE_CLASS_HUMAN_INTERFACE,
        -1,
        -1);
    if (pInterfaceDesc == NULL)
    {
        /*
        v14 = "\\Vpò\x048N7ÎS\x1DTW_É-Ëb+ªÚb:0{\x18Ën∆d¨w\x06(oÎ\x0E•_M¨%z-+Nª,ØÊ$-+-+0‚·.Xˇ\x06˙n^jJtÈ-¶<|0Q\v+¶‡Ë";
        v13 = 13;
        */
    }
    else
    {
        IsHIDClass = pInterfaceDesc->bInterfaceClass == USB_DEVICE_CLASS_HUMAN_INTERFACE;
        pHidDesc = NULL;
        if (IsHIDClass)
        {
            HumParseHidInterface(
                pMiniDevExt,
                pInterfaceDesc,
                (LONG)(TransferrBufferLen + pConfigDescr - (UINT_PTR)pInterfaceDesc),
                &pHidDesc);
            if (pHidDesc != NULL)
            {
                /*
                LOBYTE(v8) = 4;
                WPP_RECORDER_SF_(
                *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
                v8,
                5u,
                11,
                "\\Vpò\x048N7ÎS\x1DTW_É-Ëb+ªÚb:0{\x18Ën∆d¨w\x06(oÎ\x0E•_M¨%z-+Nª,ØÊ$-+-+0‚·.Xˇ\x06˙n^jJtÈ-¶<|0Q\v+¶‡Ë");
                */

                RtlCopyMemory(&pMiniDevExt->HidDesc, pHidDesc, sizeof(pMiniDevExt->HidDesc));
                goto Exit;
            }
        }
        /*
        v14 = "\\Vpò\x048N7ÎS\x1DTW_É-Ëb+ªÚb:0{\x18Ën∆d¨w\x06(oÎ\x0E•_M¨%z-+Nª,ØÊ$-+-+0‚·.Xˇ\x06˙n^jJtÈ-¶<|0Q\v+¶‡Ë";
        v13 = 12;
        */
    }

    /*
    LOBYTE(v8) = 3;
    WPP_RECORDER_SF_(*(_DWORD *)&WPP_GLOBAL_Control->StackSize, v8, 5u, v13, v14);
    */
    status = STATUS_UNSUCCESSFUL;
Exit:
    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    LOBYTE(v11) = 5;
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    v11,
    5u,
    14,
    "\\Vpò\x048N7ÎS\x1DTW_É-Ëb+ªÚb:0{\x18Ën∆d¨w\x06(oÎ\x0E•_M¨%z-+Nª,ØÊ$-+-+0‚·.Xˇ\x06˙n^jJtÈ-¶<|0Q\v+¶‡Ë");
    }
    */
    return status;
}

NTSTATUS HumSelectConfiguration(PDEVICE_OBJECT pDevObj, PUSB_CONFIGURATION_DESCRIPTOR pConfigDesc)
{
    NTSTATUS                    status;
    PURB                        pUrb;
    PUSBD_INTERFACE_INFORMATION pInterfaceInfo;
    PUSBD_INTERFACE_INFORMATION pBuff;
    USBD_INTERFACE_LIST_ENTRY   InterfaceList[2];
    PHID_MINI_DEV_EXTENSION     pMiniDevExt;
    PHID_DEVICE_EXTENSION       pDevExt;

    pInterfaceInfo = NULL;
    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    LOBYTE(pConfigDescr) = 5;
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    (int)pConfigDesc,
    5u,
    69,
    "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
    }
    */
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    InterfaceList[0].InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(
        pConfigDesc,
        pConfigDesc,
        -1,
        -1,
        USB_DEVICE_CLASS_HUMAN_INTERFACE,
        -1,
        -1);
    InterfaceList[1].InterfaceDescriptor = NULL;
    if (InterfaceList[0].InterfaceDescriptor == NULL)
    {
        /*
        LOBYTE(v5) = 2;
        WPP_RECORDER_SF_(
        *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
        v5,
        5u,
        73,
        "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
        */
        status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        pUrb = (URB *)USBD_CreateConfigurationRequestEx(pConfigDesc, &InterfaceList[0]);
        if (pUrb == NULL)
        {
            /*
            LOBYTE(v7) = 2;
            WPP_RECORDER_SF_(
            *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
            v7,
            5u,
            72,
            "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
            */
            status = STATUS_NO_MEMORY;
        }
        else
        {
            status = HumCallUSB(pDevObj, pUrb);
            if (NT_SUCCESS(status) != TRUE)
            {
                /*
                LOBYTE(v10) = 2;
                WPP_RECORDER_SF_q(
                *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
                v10,
                5u,
                71,
                "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä",
                status);
                */
                pMiniDevExt->UsbdConfigurationHandle = 0;
            }
            else
            {
                pInterfaceInfo = &pUrb->UrbSelectConfiguration.Interface;
                pMiniDevExt->UsbdConfigurationHandle = pUrb->UrbSelectConfiguration.ConfigurationHandle;
                /*
                LOBYTE(v10) = 4;
                WPP_RECORDER_SF_q(
                *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
                v10,
                5u,
                70,
                "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä",
                (_BYTE)pUrb + 24);
                */
            }

            if (NT_SUCCESS(status))
            {
                pBuff = (USBD_INTERFACE_INFORMATION *)ExAllocatePoolWithTag(NonPagedPoolNx, pInterfaceInfo->Length, HID_USB_TAG);
                pMiniDevExt->pInterfaceInfo = pBuff;
                if (pBuff)
                {
                    memcpy(pBuff, pInterfaceInfo, pInterfaceInfo->Length);
                }
                else
                {
                    status = STATUS_NO_MEMORY;
                    /*
                    LOBYTE(v13) = 2;
                    WPP_RECORDER_SF_q(
                    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
                    v13,
                    5u,
                    89,
                    "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä",
                    0x17);
                    */
                }
            }

            ExFreePool(pUrb);
        }
    }

    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    LOBYTE(v14) = 5;
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    v14,
    5u,
    90,
    "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
    }
    */
    return status;
}

NTSTATUS HumParseHidInterface(HID_MINI_DEV_EXTENSION *pMiniDevExt, PUSB_INTERFACE_DESCRIPTOR pInterface, LONG TotalLength, PUSB_INTERFACE_DESCRIPTOR *ppHidDesc)
{
    NTSTATUS                  status;
    ULONG                     EndptIndex;
    LONG                      Remain;
    PUSB_INTERFACE_DESCRIPTOR pDesc;

    *ppHidDesc = NULL;
    pDesc = pInterface;
    Remain = TotalLength;
    EndptIndex = 0;

    if (Remain < 9)
    {
        /*
        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 43 " % 0Invalid buffer length : % 10!d!" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")
        */
        goto Exit;
    }

    if (pDesc->bLength < 9)
    {
        /*
        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 44 " % 0HW_COMPLIANCE: Interface->bLength : % 10!d!is invalid" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")
        */
        goto Exit;
    }

    Remain -= pDesc->bLength;
    if (Remain < 2)
    {
        /*
        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 45 " % 0Invalid buffer length : % 10!d!" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")
        */
    }

    pMiniDevExt->Flags &= ~DEXT_NO_HID_DESC;
    pDesc = (PUSB_INTERFACE_DESCRIPTOR)((UINT_PTR)pDesc + pDesc->bLength);
    if (pDesc->bLength < 2)
    {
        /*
        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 46 " % 0HW_COMPLIANCE: Descriptor->bLength : % 10!d!is invalid" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")
        */
        goto Exit;
    }

    if (pDesc->bDescriptorType != 0x21)
    {
        pMiniDevExt->Flags |= DEXT_NO_HID_DESC;
    }
    else
    {
        if (pDesc->bLength != 9)
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 48 " % 0HW_COMPLIANCE: HID descriptor length : % 10!d!is invalid" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            goto Exit;
        }
        *ppHidDesc = pDesc;
        Remain -= pDesc->bLength;
        if (Remain < 0)
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 47 " % 0Invalid buffer length : % 10!d!" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            goto Exit;
        }
        pDesc = (PUSB_INTERFACE_DESCRIPTOR)((UINT_PTR)pDesc + pDesc->bLength);
    }

    if (pInterface->bNumEndpoints != 0)
    {
        for (;;)
        {
            if (Remain < 2)
            {
                /*
                __annotation("TMF:",
                "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 49 " % 0Invalid buffer length : % 10!d!(not large enough)" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                "{", "Arg, ItemLong -- 10", "}",
                "PUBLIC_TMF:")
                */
                goto Exit;
            }
            if (pDesc->bDescriptorType == 5)
            {
                if (pDesc->bLength != 7)
                {
                    /*
                    __annotation("TMF:",
                    "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                    "#typev Unknown_cxx00 50 " % 0HW_COMPLIANCE: EndPoint descriptor length : % 10!d!is invalid(not equal to bLength)" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                    "{", "Arg, ItemLong -- 10", "}",
                    "PUBLIC_TMF:")
                    */
                    goto Exit;
                }
                EndptIndex++;
            }
            else
            {
                if (pDesc->bDescriptorType == 4)
                {
                    /*
                    __annotation("TMF:",
                    "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                    "#typev Unknown_cxx00 51 " % 0HW_COMPLIANCE: Next interface descriptor reached before getting all the endpoint descriptors" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                    "{", "}",
                    "PUBLIC_TMF:")
                    */
                    goto Exit;
                }
            }

            if (pDesc->bLength == 0)
            {
                /*
                __annotation("TMF:",
                "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 52 " % 0HW_COMPLIANCE: Got invalid descriptor with bLength : % 10!d!before getting all endpoint descriptors" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                "{", "Arg, ItemLong -- 10", "}",
                "PUBLIC_TMF:")
                */
                goto Exit;
            }
            Remain -= pDesc->bLength;
            if (Remain < 0)
            {
                /*
                __annotation("TMF:",
                "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 53 " % 0Invalid buffer length : % 10!d!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                "{", "Arg, ItemLong -- 10", "}",
                "PUBLIC_TMF:")
                */
                goto Exit;
            }
            pDesc = (PUSB_INTERFACE_DESCRIPTOR)((UINT_PTR)pDesc + pDesc->bLength);
            if (EndptIndex == pInterface->bNumEndpoints)
            {
                break;
            }
        }
    }

    if (BooleanFlagOn(pMiniDevExt->Flags, DEXT_NO_HID_DESC) == FALSE)
    {
        if (*ppHidDesc == NULL)
        {
            goto ExitFail;
        }
        else
        {
            //Log desc
        }
    }
    else
    {
        if (Remain < 2)
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 54 " % 0Invalid buffer length : % 10!d!(not large enough)" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            goto Exit;
        }
        if (pDesc->bDescriptorType == 0x21)
        {
            *ppHidDesc = pDesc;
        }
        else
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 55 " % 0HW_COMPLIANCE: Unknown descriptor in HID interface" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
            "{", "}",
            "PUBLIC_TMF:")
            */

            if (pDesc->bLength != 9)
            {

            }
            else
            {
                /*
                __annotation("TMF:",
                "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 56 " % 0Guessing descriptor of length : % 10!d!is actually HID" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                "{", "Arg, ItemLong -- 10", "}",
                "PUBLIC_TMF:")
                */
                *ppHidDesc = pDesc;
            }
        }

        if (Remain >= pDesc->bLength)
        {
            if (*ppHidDesc == NULL)
            {
                goto ExitFail;
            }
            else
            {
                //Log desc
            }
        }
        else
        {
            if (Remain < 9)
            {
                /*
                __annotation("TMF:",
                "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 57 " % 0Invalid buffer length : % 10!d!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                "{", "Arg, ItemLong -- 10", "}",
                "PUBLIC_TMF:")
                */
                *ppHidDesc = NULL;
            }
            else
            {
                /*
                __annotation("TMF:",
                "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 57 " % 0Invalid buffer length : % 10!d!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                "{", "Arg, ItemLong -- 10", "}",
                "PUBLIC_TMF:")
                */
            }
        }
    }

Exit:
    if (*ppHidDesc == NULL)
    {
    ExitFail:
        /*
        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 66 " % 0HW_COMPLIANCE: Failed to find a valid HID descriptor in interface" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
        "{", "}",
        "PUBLIC_TMF:")
        */
        status = STATUS_UNSUCCESSFUL;
    }
    else
    {
        if (pInterface->bNumEndpoints == EndptIndex)
        {
            status = STATUS_SUCCESS;
        }
        else
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 67 " % 0HW_COMPLIANCE: Failed to find all of the endpoints in interface" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
            "{", "}",
            "PUBLIC_TMF:")
            */
            status = STATUS_UNSUCCESSFUL;
        }
    }

    return status;
}

NTSTATUS HumGetConfigDescriptor(PDEVICE_OBJECT pDevObj, PUSB_CONFIGURATION_DESCRIPTOR *ppConfigDesc, PULONG pConfigDescLen)
{
    NTSTATUS                      status;
    ULONG                         TotalLength;
    ULONG                         ConfigDescLen;
    PUSB_CONFIGURATION_DESCRIPTOR pConfigDesc;

    pConfigDesc = NULL;
    ConfigDescLen = sizeof(*pConfigDesc);
    status = HumGetDescriptorRequest(
        pDevObj,
        URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE,
        USB_CONFIGURATION_DESCRIPTOR_TYPE,
        &pConfigDesc,
        &ConfigDescLen,
        0,
        0,
        0);
    if (NT_SUCCESS(status) == FALSE)
    {
        /*
        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 41 " % 0HumGetDescriptorRequest(base) failed: % 10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemNTSTATUS -- 10", "}",
        "PUBLIC_TMF:")
        */
    }
    else
    {
        if (ConfigDescLen < 9)
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 27 " % 0Config descriptor buffer(1) length % 10!d!is not large enough" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            return STATUS_DEVICE_DATA_ERROR;
        }
        TotalLength = pConfigDesc->wTotalLength;
        ExFreePool(pConfigDesc);
        if (TotalLength == 0)
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 28 " % 0HW_COMPLIANCE: ConfigDesc->wTotalLength is zero" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
            "{", "}",
            "PUBLIC_TMF:")
            */
            return STATUS_DEVICE_DATA_ERROR;
        }
        pConfigDesc = NULL;
        ConfigDescLen = TotalLength;
        status = HumGetDescriptorRequest(
            pDevObj,
            URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE,
            USB_CONFIGURATION_DESCRIPTOR_TYPE,
            &pConfigDesc,
            &ConfigDescLen,
            0,
            0,
            0);

        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 40 " % 0HumGetDescriptorRequest failed : % 10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemNTSTATUS -- 10", "}",
            "PUBLIC_TMF:")
            */
        }
        else
        {
            if (!ConfigDescLen || ConfigDescLen < 9)
            {
                if (pConfigDesc)
                {
                    ExFreePool(pConfigDesc);
                }

                /*
                __annotation("TMF:",
                "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 29 " % 0Config descriptor buffer(2) length % 10!d!is not large enough" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                "{", "Arg, ItemLong -- 10", "}",
                "PUBLIC_TMF:")
                */
                return STATUS_DEVICE_DATA_ERROR;
            }
            if (pConfigDesc->wTotalLength > TotalLength)
            {
                pConfigDesc->wTotalLength = (USHORT)TotalLength;
            }
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 30 " % 0Config = 0x % 10!p!:" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemPtr -- 10", "}",
            "PUBLIC_TMF:")
            */

            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 31 " % 0  Config->bLength = 0x % 10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */

            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 32 " % 0  Config->bDescriptorType = 0x % 10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 33 " % 0  Config->wTotalLength = 0x % 10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 34 " % 0  Config->bNumInterfaces = 0x % 10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 35 " % 0  Config->bConfigurationValue = 0x % 10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 36 " % 0  Config->iConfiguration = 0x % 10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 37 " % 0  Config->bmAttributes = 0x % 10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            /*
            __annotation("TMF:",
            "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 38 " % 0  Config->MaxPower = 0x % 10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */

            if (pConfigDesc->bLength < 9)
            {
                pConfigDesc->bLength = 9;
                /*
                __annotation("TMF:",
                "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 39 " % 0HW_COMPLIANCE: Invalid Config->bLength" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
                "{", "}",
                "PUBLIC_TMF:")
                */
            }
        }
    }

    *ppConfigDesc = pConfigDesc;
    *pConfigDescLen = ConfigDescLen;

    return status;
}

NTSTATUS HumGetDeviceDescriptor(PDEVICE_OBJECT pDevObj, PHID_MINI_DEV_EXTENSION pMiniDevExt)
{
    NTSTATUS status;
    ULONG    DeviceDescLen;

    DeviceDescLen = sizeof(USB_DEVICE_DESCRIPTOR);

    /*
    PDEVICE_OBJECT v4; // ecx@1
    v4 = WPP_GLOBAL_Control;
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    LOBYTE(pMiniDevExt) = 5;
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    (int)pMiniDevExt,
    5u,
    15,
    "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
    }
    */
    status = HumGetDescriptorRequest(
        pDevObj,
        URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE,
        USB_DEVICE_DESCRIPTOR_TYPE,
        &pMiniDevExt->pDevDesc,
        &DeviceDescLen,
        0,
        0,
        0);
    if (NT_SUCCESS(status) == FALSE)
    {
        /*
        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 25 "%0HumGetDescriptorRequest failed with status: %10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemNTSTATUS -- 10", "}",
        "PUBLIC_TMF:")
        */
    }
    else
    {
        /*
        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 16 "%0Device = 0x%10!p!:" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemPtr -- 10", "}",
        "PUBLIC_TMF:") //pMiniDevExt

        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 17 "%0  Device->bLength              = 0x%10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")

        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 18 "%0  Device->bDescriptorType      = 0x%10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")

        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 19 "%0  Device->bDeviceClass         = 0x%10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")

        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 20 "%0  Device->bDeviceSubClass      = 0x%10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")

        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 21 "%0  Device->bDeviceProtocol      = 0x%10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")

        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 22 "%0  Device->idVendor             = 0x%10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")

        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 23 "%0  Device->idProduct            = 0x%10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")

        __annotation("TMF:",
        "f7e3565c-ec04-374e-89e4-1de957dc9fd1 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 24 "%0  Device->bcdDevice            = 0x%10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_USB",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")
        */
    }

    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    LOBYTE(v17) = 5;
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    v17,
    5u,
    26,
    "\\V„˜\x04ÏN7â‰\x1DÈW‹ü—äbºØïb:0{\x18ä¸íÎ©w\x06(oâ\x0Eù_M™%zÕøNØ,ﬂë$“ΩÕ√0É†.Xò\x06£n^jJtÇ—≤<|0Q\v…›Öä");
    }
    */

    return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pRegPath)
{
    NTSTATUS                    status;
    HID_MINIDRIVER_REGISTRATION HidReg;
    PDRIVER_EXTENSION           pDrvExt;

    //TraceLoggingRegisterEx((int)pDrvObj);
    //WppLoadTracingSupport();
    //__annotation("TMC:", "896f2806-9d0e-4d5f-aa25-7acdbf4eaf2c", "HidUsbTraceGuid", "TRACE_FLAG_INIT", "TRACE_FLAG_HID", "TRACE_FLAG_IOCTL", "TRACE_FLAG_PNP", "TRACE_FLAG_USB", "PUBLIC_TMF:")
    //WppInitKm((int)pDrvObj_1, (int)pRegPath_1);

    /*
    __annotation("TMF:",
    "afbc628a-6295-303a-7b18-8afc92eba977 hidusb // SRC=Unknown_cxx00 MJ= MN=",
    "#typev Unknown_cxx00 11 "%0%!FUNC! invoked for DriverObject:0x%10!p!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_INIT",
    "{", "Arg, ItemPtr -- 10", "}",
    "PUBLIC_TMF:")
    */
    pDrvExt = pDrvObj->DriverExtension;
    pDrvObj->MajorFunction[IRP_MJ_CREATE] = (PDRIVER_DISPATCH)HumCreateClose;
    pDrvObj->MajorFunction[IRP_MJ_CREATE] = (PDRIVER_DISPATCH)HumCreateClose;
    pDrvObj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = (PDRIVER_DISPATCH)HumInternalIoctl;
    pDrvObj->MajorFunction[IRP_MJ_PNP] = HumPnP;
    pDrvObj->MajorFunction[IRP_MJ_POWER] = HumPower;
    pDrvObj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = (PDRIVER_DISPATCH)HumSystemControl;
    pDrvExt->AddDevice = (PDRIVER_ADD_DEVICE)HumAddDevice;
    pDrvObj->DriverUnload = (PDRIVER_UNLOAD)HumUnload;

    HidReg.Revision = HID_REVISION;
    HidReg.DriverObject = pDrvObj;
    HidReg.RegistryPath = pRegPath;
    //HidReg.DeviceExtensionSize = 84;//0x54, sizeof()
    HidReg.DeviceExtensionSize = sizeof(HID_MINI_DEV_EXTENSION);
    HidReg.DevicesArePolled = FALSE;
    status = HidRegisterMinidriver(&HidReg);

    /*
    __annotation("TMF:",
    "afbc628a-6295-303a-7b18-8afc92eba977 hidusb // SRC=Unknown_cxx00 MJ= MN=",
    "#typev Unknown_cxx00 12 " % 0HidRegisterMinidriver returned with status : % 10!s!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_INIT",
    "{", "Arg, ItemNTSTATUS -- 10", "}",
    "PUBLIC_TMF:")
    */

    //KeInitializeSpinLock(&resetWorkItemsListSpinLock);
    if (NT_SUCCESS(status) == FALSE)
    {
        /*
        if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v14, v15))
        {
        v16 = status;
        v23 = (int *)&v16;
        v24 = 0;
        v25 = 4;
        v26 = 0;
        _TlgCreateSz((const CHAR **)&v27, "HIDUSB_MiniportReg");
        _TlgWrite(v10, (unsigned __int8 *)&unk_151E9, v12, v13, 4, &v22);
        }
        */
        //EtwUnregister(dword_16020, dword_16024);
        //dword_16020 = 0;
        //dword_16024 = 0;
        //dword_16008 = 0;
        //WppCleanupKm(pDrvObj_1);
    }
    return status;
}

NTSTATUS HumAddDevice(PDRIVER_OBJECT pDrvObj, PDEVICE_OBJECT pFdo)
{
    PHID_DEVICE_EXTENSION   pDevExt;
    HID_MINI_DEV_EXTENSION *pMiniDevExt;

    UNREFERENCED_PARAMETER(pDrvObj);

    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    5,
    1u,
    19,
    (char *)&WPP_afbc628a6295303a7b188afc92eba977_Traceguids);
    */
    pDevExt = (PHID_DEVICE_EXTENSION)pFdo->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    pMiniDevExt->Flags = 0;
    pMiniDevExt->PendingRequestsCount = 0;
    KeInitializeEvent(&pMiniDevExt->Event, NotificationEvent, FALSE);
    pMiniDevExt->pWorkItem = NULL;
    pMiniDevExt->PnpState = 0;
    pMiniDevExt->pFdo = pFdo;
    IoInitializeRemoveLockEx(&pMiniDevExt->RemoveLock, HID_USB_TAG, 2, 0, sizeof(pMiniDevExt->RemoveLock));

    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    5,
    1u,
    20,
    (char *)&WPP_afbc628a6295303a7b188afc92eba977_Traceguids);
    */

    return STATUS_SUCCESS;
}

NTSTATUS HumCreateClose(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(pDevObj);
    status = STATUS_SUCCESS;
    /*
    v3 = WPP_GLOBAL_Control;
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    5,
    1u,
    14,
    (char *)&WPP_afbc628a6295303a7b188afc92eba977_Traceguids);
    v3 = WPP_GLOBAL_Control;
    }
    */
    if (pIrp->Tail.Overlay.CurrentStackLocation->MajorFunction == IRP_MJ_CREATE)
    {
        /*
        __annotation("TMF:",
        "afbc628a-6295-303a-7b18-8afc92eba977 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 15 " % 0Received IRP_MJ_CREATE IRP : 0x % 10!p!for DeviceObject:0x % 11!p!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_INIT",
        "{", "Arg, ItemPtr -- 10", "Arg, ItemPtr -- 11", "}",
        "PUBLIC_TMF:")
        */
    }
    else
    {
        if (pIrp->Tail.Overlay.CurrentStackLocation->MajorFunction != IRP_MJ_CLOSE)
        {
            /*
            __annotation("TMF:",
            "afbc628a-6295-303a-7b18-8afc92eba977 hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 17 " % 0Received invalid IRP : 0x % 10!p!for DeviceObject:0x % 11!p!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_INIT",
            "{", "Arg, ItemPtr -- 10", "Arg, ItemPtr -- 11", "}",
            "PUBLIC_TMF:")
            */
            status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        /*
        __annotation("TMF:",
        "afbc628a-6295-303a-7b18-8afc92eba977 hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 16 " % 0Received IRP_MJ_CLOSE IRP : 0x % 10!p!for DeviceObject:0x % 11!p!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_INIT",
        "{", "Arg, ItemPtr -- 10", "Arg, ItemPtr -- 11", "}",
        "PUBLIC_TMF:")
        */
    }


    pIrp->IoStatus.Information = 0;
Exit:
    pIrp->IoStatus.Status = status;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    WPP_RECORDER_SF_(
    *(_DWORD *)&WPP_GLOBAL_Control->StackSize,
    5,
    1u,
    18,
    (char *)&WPP_afbc628a6295303a7b188afc92eba977_Traceguids);
    */
    return status;
}

VOID HumUnload(PDRIVER_OBJECT pDrvObj)
{
    UNREFERENCED_PARAMETER(pDrvObj);
    /*
    __annotation("TMF:",
    "afbc628a-6295-303a-7b18-8afc92eba977 hidusb // SRC=Unknown_cxx00 MJ= MN=",
    "#typev Unknown_cxx00 21 "%0Unloading DriverObject:0x%10!p!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_INIT",
    "{", "Arg, ItemPtr -- 10", "}",
    "PUBLIC_TMF:")
    */

    /*
    EtwUnregister(dword_16020, dword_16024);
    dword_16020 = 0;
    dword_16024 = 0;
    dword_16008 = 0;
    return WppCleanupKm(a1);
    */
}

VOID HumSetIdleWorker(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    HumSetIdle(DeviceObject);
    IoFreeWorkItem((PIO_WORKITEM)Context);
}

PUSBD_PIPE_INFORMATION GetInterruptInputPipeForDevice(PHID_MINI_DEV_EXTENSION pMiniDevExt)
{
    ULONG                       Index;
    ULONG                       NumOfPipes;
    PUSBD_PIPE_INFORMATION      pPipeInfo;
    PUSBD_INTERFACE_INFORMATION pInterfaceInfo;

    pInterfaceInfo = pMiniDevExt->pInterfaceInfo;
    Index = 0;
    NumOfPipes = pInterfaceInfo->NumberOfPipes;
    if (NumOfPipes == 0)
    {
        return NULL;
    }
    pPipeInfo = &pInterfaceInfo->Pipes[0];
    while ((USBD_PIPE_DIRECTION_IN(pPipeInfo) == FALSE) || (pPipeInfo->PipeType != UsbdPipeTypeInterrupt))
    {
        ++Index;
        ++pPipeInfo;
        if (Index >= NumOfPipes)
        {
            return NULL;
        }
    }
    return &pInterfaceInfo->Pipes[Index];
}

LONG HumDecrementPendingRequestCount(PHID_MINI_DEV_EXTENSION pMiniDevExt)
{
    LONG result;

    result = _InterlockedDecrement(&pMiniDevExt->PendingRequestsCount);
    if (result < 0)
    {
        result = KeSetEvent(&pMiniDevExt->Event, IO_NO_INCREMENT, FALSE);
    }
    return result;
}

NTSTATUS HumGetPortStatus(PDEVICE_OBJECT pDevObj, PULONG pOut)
{
    NTSTATUS              status;
    PIRP                  pIrp;
    KEVENT                Event;
    IO_STATUS_BLOCK       IoStatus;
    PHID_DEVICE_EXTENSION pDevExt;
    PIO_STACK_LOCATION    pStack;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    *pOut = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    pIrp = IoBuildDeviceIoControlRequest(
        IOCTL_INTERNAL_USB_GET_PORT_STATUS,
        pDevExt->NextDeviceObject,
        NULL,
        0,
        NULL,
        0,
        TRUE,
        &Event,
        &IoStatus);
    if (pIrp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    pStack = IoGetNextIrpStackLocation(pIrp);
    pStack->Parameters.Others.Argument1 = pOut;
    status = IoCallDriver(pDevExt->NextDeviceObject, pIrp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Suspended, KernelMode, FALSE, NULL);
        status = IoStatus.Status;
    }
    return status;
}

NTSTATUS HumGetSetReport(PDEVICE_OBJECT pDevObj, PIRP pIrp, PBOOLEAN pNeedToCompleteIrp)
{
    NTSTATUS                status;
    USHORT                  UrbLen;
    LONG                    TransferFlags;
    PVOID                   pUserBuffer;
    PURB                    pUrb;
    USHORT                  Value;
    SHORT                   Index;
    PIO_STACK_LOCATION      pStack;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    SHORT                   Offset;
    CHAR                    Request;
    PHID_DEVICE_EXTENSION   pDevExt;
    PHID_XFER_PACKET        pTransferPacket;
    PIO_STACK_LOCATION      pNextStack;

    status = STATUS_SUCCESS;
    pStack = IoGetCurrentIrpStackLocation(pIrp);
    switch (pStack->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_HID_SET_FEATURE:
        TransferFlags = 0;
        Request = 9;
        Offset = 0x300;
        break;
    case IOCTL_HID_GET_FEATURE:
        Offset = 0x300;
        TransferFlags = 1;
        Request = 1;
        break;
    case IOCTL_HID_SET_OUTPUT_REPORT:
        TransferFlags = 0;
        Request = 9;
        Offset = 0x200;
        break;
    case IOCTL_HID_GET_INPUT_REPORT:
        Offset = 0x100;
        TransferFlags = 1;
        Request = 1;
        break;
    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    if (NT_SUCCESS(status) == FALSE)
    {
        goto Exit;
    }

    TransferFlags = Offset = Request = 0; //Make compiler happy

    pUserBuffer = pIrp->UserBuffer;
    pTransferPacket = (PHID_XFER_PACKET)pUserBuffer;
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    if (pTransferPacket == NULL || pTransferPacket->reportBuffer == NULL || pTransferPacket->reportBufferLen == 0)
    {
        status = STATUS_DATA_ERROR;
    }
    else
    {
        UrbLen = sizeof(URB);
        pUrb = (PURB)ExAllocatePoolWithTag(NonPagedPoolNx, UrbLen, HID_USB_TAG);
        if (pUrb == NULL)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            memset(pUrb, 0, UrbLen);
            Value = pTransferPacket->reportId + Offset;
            if (BooleanFlagOn(pMiniDevExt->Flags, DEXT_NO_HID_DESC))
            {
                pUrb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
                pUrb->UrbHeader.Function = URB_FUNCTION_CLASS_ENDPOINT;
                Index = 1;
            }
            else
            {
                pUrb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
                pUrb->UrbHeader.Function = URB_FUNCTION_CLASS_INTERFACE;
                Index = pMiniDevExt->pInterfaceInfo->InterfaceNumber;
            }
            pUrb->UrbControlVendorClassRequest.Index = Index;
            pUrb->UrbControlVendorClassRequest.TransferFlags = TransferFlags;
            pUrb->UrbControlVendorClassRequest.RequestTypeReservedBits = 0x22;
            pUrb->UrbControlVendorClassRequest.Request = Request;
            pUrb->UrbControlVendorClassRequest.Value = Value;
            pUrb->UrbControlVendorClassRequest.TransferBuffer = pTransferPacket->reportBuffer;
            pUrb->UrbControlVendorClassRequest.TransferBufferLength = pTransferPacket->reportBufferLen;

            IoSetCompletionRoutine(pIrp, HumGetSetReportCompletion, pUrb, TRUE, TRUE, TRUE);

            pNextStack = IoGetNextIrpStackLocation(pIrp);
            pNextStack->Parameters.Others.Argument1 = pUrb;
            pNextStack->MajorFunction = pStack->MajorFunction;
            pNextStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
            pNextStack->DeviceObject = pDevExt->NextDeviceObject;

            if (NT_SUCCESS(HumIncrementPendingRequestCount(pMiniDevExt)) == FALSE)
            {
                ExFreePool(pUrb);
                status = STATUS_NO_SUCH_DEVICE;
            }
            else
            {
                status = IoCallDriver(pDevExt->NextDeviceObject, pIrp);
                *pNeedToCompleteIrp = FALSE;
            }
        }
    }

Exit:
    return status;
}

NTSTATUS HumGetSetReportCompletion(PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext)
{
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    PHID_DEVICE_EXTENSION   pDevExt;
    PURB                    pUrb;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    pUrb = (PURB)pContext;

    if (NT_SUCCESS(pIrp->IoStatus.Status) == TRUE)
    {
        pIrp->IoStatus.Information = pUrb->UrbControlTransfer.TransferBufferLength;
    }

    ExFreePool(pUrb);
    if (pIrp->PendingReturned)
    {
        IoMarkIrpPending(pIrp);
    }
    HumDecrementPendingRequestCount(pMiniDevExt);
    IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));

    return STATUS_SUCCESS;
}

NTSTATUS HumIncrementPendingRequestCount(PHID_MINI_DEV_EXTENSION pMiniDevExt)
{
    InterlockedIncrement(&pMiniDevExt->PendingRequestsCount);
    if (pMiniDevExt->PnpState == 2 || pMiniDevExt->PnpState == 1)
    {
        return 0;
    }
    HumDecrementPendingRequestCount(pMiniDevExt);
    return STATUS_NO_SUCH_DEVICE;
}

NTSTATUS HumQueueResetWorkItem(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS                status;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    PWRKITM_RESET_CONTEXT   pContext;
    PIO_WORKITEM            pWorkItem;
    PHID_DEVICE_EXTENSION   pDevExt;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    if (NT_SUCCESS(HumIncrementPendingRequestCount(pMiniDevExt)) == FALSE)
    {
        return 0;
    }
    pContext = (WRKITM_RESET_CONTEXT *)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(WRKITM_RESET_CONTEXT), HID_USB_TAG);
    if (pContext)
    {
        pWorkItem = IoAllocateWorkItem(pMiniDevExt->pFdo);
        pContext->pWorkItem = pWorkItem;
        if (pWorkItem)
        {
            if (InterlockedCompareExchangePointer(
                &pMiniDevExt->pWorkItem,
                &pContext->pWorkItem,
                0) == 0)
            {
                pContext->pDeviceObject = pDevObj;
                pContext->Tag = HID_RESET_TAG;
                pContext->pIrp = pIrp;
                pIrp->Tail.Overlay.CurrentStackLocation->Control |= 1u;
                IoAcquireRemoveLockEx(&pMiniDevExt->RemoveLock, (PVOID)HID_REMLOCK_TAG, __FILE__, __LINE__, sizeof(pMiniDevExt->RemoveLock));
                IoQueueWorkItem(pContext->pWorkItem, HumResetWorkItem, DelayedWorkQueue, pContext);
                return STATUS_MORE_PROCESSING_REQUIRED;
            }
            IoFreeWorkItem(pContext->pWorkItem);
        }
        ExFreePool(pContext);
        HumDecrementPendingRequestCount(pMiniDevExt);
        status = 0;
    }
    else
    {
        HumDecrementPendingRequestCount(pMiniDevExt);
        status = 0;
    }
    return status;
}

NTSTATUS HumResetInterruptPipe(PDEVICE_OBJECT pDevObj)
{
    NTSTATUS                status;
    USHORT                  UrbLen;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    PURB                    pUrb;
    PUSBD_PIPE_INFORMATION  pPipeInfo;
    PHID_DEVICE_EXTENSION   pDevExt;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    UrbLen = sizeof(struct _URB_PIPE_REQUEST);
    pUrb = (PURB)ExAllocatePoolWithTag(NonPagedPoolNx, UrbLen, HID_USB_TAG);
    if (pUrb == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pUrb->UrbHeader.Length = UrbLen;
    pUrb->UrbHeader.Function = URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL;
    pPipeInfo = GetInterruptInputPipeForDevice(pMiniDevExt);
    if (pPipeInfo != NULL)
    {
        pUrb->UrbPipeRequest.PipeHandle = pPipeInfo->PipeHandle;
        status = HumCallUSB(pDevObj, pUrb);
        ExFreePool(pUrb);
    }
    else
    {
        /*
        __annotation("TMF:",
        "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 12 " % 0Returning STATUS_INVALID_DEVICE_REQUEST because pipeInfo is 0. (device likely has no interrupt IN pipe)" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_HID",
        "{", "}",
        "PUBLIC_TMF:")
        */

        ExFreePool(pUrb);
        status = STATUS_INVALID_DEVICE_REQUEST;
    }
    return status;
}

NTSTATUS HumResetParentPort(PDEVICE_OBJECT pDevObj)
{
    NTSTATUS              status;
    PIRP                  pIrp;
    KEVENT                Event;
    IO_STATUS_BLOCK       IoStatus;
    PHID_DEVICE_EXTENSION pDevExt;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    pIrp = IoBuildDeviceIoControlRequest(
        IOCTL_INTERNAL_USB_RESET_PORT,
        pDevExt->NextDeviceObject,
        NULL,
        0,
        NULL,
        0,
        TRUE,
        &Event,
        &IoStatus);
    if (pIrp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = IoCallDriver(pDevExt->NextDeviceObject, pIrp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Suspended, KernelMode, FALSE, NULL);
        status = IoStatus.Status;
    }

    return status;
}

VOID HumResetWorkItem(PDEVICE_OBJECT pDevObj, PWRKITM_RESET_CONTEXT pContext)
{
    NTSTATUS                status;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    ULONG                   PortStatus;
    PHID_DEVICE_EXTENSION   pDevExt;

    UNREFERENCED_PARAMETER(pDevObj);

    PortStatus = 0;
    pDevExt = (PHID_DEVICE_EXTENSION)pContext->pDeviceObject->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    if (NT_SUCCESS(HumIncrementPendingRequestCount(pMiniDevExt) == TRUE))
    {
        status = HumGetPortStatus(pContext->pDeviceObject, &PortStatus);
        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            __annotation("TMF:",
            "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 21 " % 0HumGetPortStatus failed with status : % 10!s!" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_HID",
            "{", "Arg, ItemNTSTATUS -- 10", "}",
            "PUBLIC_TMF:")
            */

            HumDecrementPendingRequestCount(pMiniDevExt);
            goto Exit;
        }
        if ((PortStatus & USBD_PORT_CONNECTED) == 0)
        {
            /*
            __annotation("TMF:",
            "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 20 " % 0Device is no longer connected.PortStatus = 0x % 10!x!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_HID",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */
            HumDecrementPendingRequestCount(pMiniDevExt);
        }
        else
        {
            /*
            __annotation("TMF:",
            "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 13 " % 0Device is still present(PortStatus = 0x % 10!x!)." //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_HID",
            "{", "Arg, ItemLong -- 10", "}",
            "PUBLIC_TMF:")
            */

            status = HumAbortPendingRequests(pContext->pDeviceObject);
            if (NT_SUCCESS(status) == FALSE)
            {
                /*
                __annotation("TMF:",
                "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 18 " % 0HumAbortPendingRequests failed % 10!s!" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_HID",
                "{", "Arg, ItemNTSTATUS -- 10", "}",
                "PUBLIC_TMF:")
                */
            }
            else
            {
                /*
                __annotation("TMF:",
                "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 14 " % 0AbortPendingRequests succeeded.Now resetting port." //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_HID",
                "{", "}",
                "PUBLIC_TMF:")
                */

                status = HumResetParentPort(pContext->pDeviceObject);
                if (status == STATUS_DEVICE_DATA_ERROR)
                {
                    /*
                    __annotation("TMF:",
                    "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
                    "#typev Unknown_cxx00 15 " % 0HumResetParentPort failed with status : % 10!s!Device Object : 0x % 11!p!marked failed" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_HID",
                    "{", "Arg, ItemNTSTATUS -- 10", "Arg, ItemPtr -- 11", "}",
                    "PUBLIC_TMF:")
                    */

                    pMiniDevExt->PnpState = 6;
                    IoInvalidateDeviceState(pDevExt->PhysicalDeviceObject);

                    HumDecrementPendingRequestCount(pMiniDevExt);
                    goto Exit;
                }
                if (NT_SUCCESS(status))
                {
                    /*
                    __annotation("TMF:",
                    "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
                    "#typev Unknown_cxx00 17 " % 0ResetParentPort succeeded(% 10!s!)" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_HID",
                    "{", "Arg, ItemNTSTATUS -- 10", "}",
                    "PUBLIC_TMF:")
                    */
                }
                else
                {
                    /*
                    __annotation("TMF:",
                    "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
                    "#typev Unknown_cxx00 16 " % 0ERROR_ASSERT: ResetParentPort failed with unhandled status % 10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_HID",
                    "{", "Arg, ItemNTSTATUS -- 10", "}",
                    "PUBLIC_TMF:")
                    */
                }
            }
            if (NT_SUCCESS(status))
            {
                status = HumResetInterruptPipe(pContext->pDeviceObject);

                /*
                __annotation("TMF:",
                "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 19 " % 0ResetInterruptPipe returned % 10!s!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_HID",
                "{", "Arg, ItemNTSTATUS -- 10", "}",
                "PUBLIC_TMF:")
                */
            }
            HumDecrementPendingRequestCount(pMiniDevExt);
        }
    }

Exit:
    InterlockedExchangePointer(&pMiniDevExt->pWorkItem, NULL);

    /*
    __annotation("TMF:",
    "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
    "#typev Unknown_cxx00 22 " % 0Completing IRP : 0x % 10!p!following portResetAttempted = % 11!s!, portResetSuccessful = % 12!s!" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_HID",
    "{", "Arg, ItemPtr -- 10", "Arg, ItemListByte(FALSE,TRUE) -- 11", "Arg, ItemListByte(FALSE,TRUE) -- 12", "}",
    "PUBLIC_TMF:")
    */

    IoCompleteRequest(pContext->pIrp, IO_NO_INCREMENT);
    IoFreeWorkItem(pContext->pWorkItem);
    ExFreePool(pContext);
    HumDecrementPendingRequestCount(pMiniDevExt);
    IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, (PVOID)HID_REMLOCK_TAG, sizeof(pMiniDevExt->RemoveLock));
}

NTSTATUS HumGetStringDescriptor(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS                status;
    PIO_STACK_LOCATION      pStack;
    ULONG                   OutBuffLen;
    ULONG_PTR               Type3InputBuffer;
    PUSB_DEVICE_DESCRIPTOR  pDevDesc;
    ULONG                   DescBuffLen;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    PVOID                   pUserBuffer;
    CHAR                    Index;
    ULONG                   IoControlCode;
    USHORT                  LangId;
    USHORT                  GetStrCtlCode;
    BOOLEAN                 Mapped;
    PUSB_STRING_DESCRIPTOR  pStrDesc;
    PHID_DEVICE_EXTENSION   pDevExt;

    pStack = IoGetCurrentIrpStackLocation(pIrp);
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    Mapped = FALSE;
    IoControlCode = pStack->Parameters.DeviceIoControl.IoControlCode;
    if (IoControlCode == IOCTL_HID_GET_STRING)
    {
        pUserBuffer = pIrp->UserBuffer;
    }
    else
    {
        if (IoControlCode != 0xB01E2) //XXX Undoc?
        {
            pUserBuffer = NULL;
        }
        else
        {
            if (pIrp->MdlAddress == NULL)
            {
                pUserBuffer = NULL;
            }
            else
            {
                pUserBuffer = MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, MdlMappingNoExecute | NormalPagePriority);
                Mapped = TRUE;
            }
        }
    }

    OutBuffLen = pStack->Parameters.DeviceIoControl.OutputBufferLength;
    if (pUserBuffer == NULL || OutBuffLen < 2)
    {
        return STATUS_INVALID_USER_BUFFER;
    }

    Type3InputBuffer = (ULONG_PTR)pStack->Parameters.DeviceIoControl.Type3InputBuffer;
    LangId = (USHORT)(Type3InputBuffer >> 0x10);
    GetStrCtlCode = Type3InputBuffer & 0xFFFF;
    Index = (CHAR)GetStrCtlCode;
    if (Mapped == FALSE)
    {
        switch (GetStrCtlCode)
        {
        case HID_STRING_ID_IPRODUCT:
            pDevDesc = pMiniDevExt->pDevDesc;
            Index = pDevDesc->iProduct;
            if (pDevDesc->iProduct == 0 || pDevDesc->iProduct == -1)
            {
                return STATUS_INVALID_PARAMETER;
            }
            break;
        case HID_STRING_ID_IMANUFACTURER:
            pDevDesc = pMiniDevExt->pDevDesc;
            Index = pDevDesc->iManufacturer;
            if (pDevDesc->iManufacturer == 0 || pDevDesc->iManufacturer == -1)
            {
                return STATUS_INVALID_PARAMETER;
            }
            break;
        case HID_STRING_ID_ISERIALNUMBER:
            pDevDesc = pMiniDevExt->pDevDesc;
            Index = pDevDesc->iSerialNumber;
            if (pDevDesc->iSerialNumber == 0 || pDevDesc->iSerialNumber == -1)
            {
                return STATUS_INVALID_PARAMETER;
            }
            break;
        default:
            return STATUS_INVALID_PARAMETER;
        }
    }

    DescBuffLen = OutBuffLen + 2;
    pStrDesc = (PUSB_STRING_DESCRIPTOR)ExAllocatePoolWithTag(NonPagedPoolNx, DescBuffLen, HID_USB_TAG);
    if (pStrDesc == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = HumGetDescriptorRequest(pDevObj, URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE, USB_STRING_DESCRIPTOR_TYPE, (PVOID *)&pStrDesc, &DescBuffLen, 0, Index, LangId);
    if (NT_SUCCESS(status) == TRUE)
    {
        ULONG Length;

        Length = pStrDesc->bLength;
        Length -= 2;
        if (Length > DescBuffLen)
        {
            Length = DescBuffLen;
        }
        Length &= 0xFFFFFFFE;
        if (Length >= OutBuffLen - 2)
        {
            status = STATUS_INVALID_BUFFER_SIZE;
        }
        else
        {
            PWCHAR p;
            RtlCopyMemory(pUserBuffer, &pStrDesc->bString, Length);

            p = (PWCHAR)((PCHAR)pUserBuffer + Length);
            *p = UNICODE_NULL;
            Length += 2;
            pIrp->IoStatus.Information = Length;
        }
    }

    ExFreePool(pStrDesc);
    return status;
}

NTSTATUS HumPnP(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS                    status;
    PHID_MINI_DEV_EXTENSION     pMiniDevExt;
    ULONG                       PrevPnpState;
    UCHAR                       MinorFunction;
    PUSBD_INTERFACE_INFORMATION pInterfaceInfo;
    PIO_STACK_LOCATION          pStack;
    KEVENT                      Event;
    PHID_DEVICE_EXTENSION       pDevExt;

    /*
    if (LOWORD(WPP_GLOBAL_Control->Queue.ListEntry.Flink))
    {
    WPP_RECORDER_SF_(*(_DWORD *)&WPP_GLOBAL_Control->StackSize, 5, 4u, 10, "ØÊ$-+-+0‚·.Xˇ\x06˙n^jJtÈ-¶<|0Q\v+¶‡Ë");
    }
    */
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    pStack = IoGetCurrentIrpStackLocation(pIrp);
    status = IoAcquireRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, __FILE__, __LINE__, sizeof(pMiniDevExt->RemoveLock));
    if (NT_SUCCESS(status) == FALSE)
    {
        /*
        __annotation("TMF:",
        "d22491df-cdbd-30c3-83a0-2e589806a36e hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 11 " % 0IoAcquireRemoveLock failed with status : % 10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_PNP",
        "{", "Arg, ItemNTSTATUS -- 10", "}",
        "PUBLIC_TMF:")
        */

        pIrp->IoStatus.Status = status;
        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
        return status;
    }
    else
    {
        switch (pStack->MinorFunction)
        {
        case IRP_MN_START_DEVICE:
            PrevPnpState = pMiniDevExt->PnpState;
            pMiniDevExt->PnpState = 1;
            KeResetEvent(&pMiniDevExt->Event);
            if (PrevPnpState == 3 || PrevPnpState == 4 || PrevPnpState == 5)
            {
                HumIncrementPendingRequestCount(pMiniDevExt);

                /*
                __annotation("TMF:",
                "d22491df-cdbd-30c3-83a0-2e589806a36e hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 18 " % 0Received start after stop(oldDeviceState: % 10!u!), re - incremented pendingRequestCount" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_PNP",
                "{", "Arg, ItemLong -- 10", "}",
                "PUBLIC_TMF:")
                */
            }
            pMiniDevExt->pInterfaceInfo = 0;
            break;
        case IRP_MN_REMOVE_DEVICE:
            return HumRemoveDevice(pDevObj, pIrp);
        case IRP_MN_STOP_DEVICE:
            if (pMiniDevExt->PnpState != 2)
            {
                break;
            }
            status = HumStopDevice(pDevObj);
            if (NT_SUCCESS(status) == FALSE)
            {
                /*
                __annotation("TMF:",
                "d22491df-cdbd-30c3-83a0-2e589806a36e hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 15 " % 0Failed PnP IRP for DeviceObject:0x % 10!p!status: % 11!s!" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_PNP",
                "{", "Arg, ItemPtr -- 10", "Arg, ItemNTSTATUS -- 11", "}",
                "PUBLIC_TMF:")
                */
                pIrp->IoStatus.Status = status;
                IoCompleteRequest(pIrp, IO_NO_INCREMENT);
                IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));
                return status;
            }
            break;
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            if (pMiniDevExt->PnpState == 6)
            {
                pIrp->IoStatus.Information |= PNP_DEVICE_FAILED;
            }
            break;
        default:
            break;
        }

        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        IoCopyCurrentIrpStackLocationToNext(pIrp);
        status = IoSetCompletionRoutineEx(pDevObj, pIrp, HumPnpCompletion, &Event, TRUE, TRUE, TRUE);
        if (NT_SUCCESS(status) == FALSE)
        {
            /*
            __annotation("TMF:",
            "d22491df-cdbd-30c3-83a0-2e589806a36e hidusb // SRC=Unknown_cxx00 MJ= MN=",
            "#typev Unknown_cxx00 12 " % 0IoSetCompletionRoutineEx failed with status : % 10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_PNP",
            "{", "Arg, ItemNTSTATUS -- 10", "}",
            "PUBLIC_TMF:")
            */
            goto ExitUnlock;
        }
        if (IoCallDriver(pDevExt->NextDeviceObject, pIrp) == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        }
        status = pIrp->IoStatus.Status;
        MinorFunction = pStack->MinorFunction;
        switch (MinorFunction)
        {
        case IRP_MN_STOP_DEVICE:
        {
            pInterfaceInfo = pMiniDevExt->pInterfaceInfo;
            pMiniDevExt->PnpState = 4;
            if (pInterfaceInfo)
            {
                ExFreePool(pInterfaceInfo);
                pMiniDevExt->pInterfaceInfo = NULL;
            }
            if (pMiniDevExt->pDevDesc)
            {
                ExFreePool(pMiniDevExt->pDevDesc);
                pMiniDevExt->pDevDesc = 0;
            }
            break;
        }
        case IRP_MN_QUERY_CAPABILITIES:
        {
            if (NT_SUCCESS(status) == TRUE)
            {
                pStack->Parameters.DeviceCapabilities.Capabilities->SurpriseRemovalOK = 1;
            }
            break;
        }
        case IRP_MN_START_DEVICE:
        {
            if (NT_SUCCESS(status) == FALSE)
            {
                /*
                __annotation("TMF:",
                "d22491df-cdbd-30c3-83a0-2e589806a36e hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 14 " % 0Failed start IRP for DeviceObject:0x % 10!p!status: % 11!s!" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_PNP",
                "{", "Arg, ItemPtr -- 10", "Arg, ItemNTSTATUS -- 11", "}",
                "PUBLIC_TMF:")
                */

                pMiniDevExt->PnpState = 6;
                /*
                if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v28, v30))
                {
                v20 = "PNP_DeviceStart";
                v43 = 0;
                v36 = &pStack;
                v42 = 4;
                v41 = 0;
                v40 = &pMiniDevExt_1;
                v39 = 0;
                v38 = 4;
                v37 = 0;
                pMiniDevExt_1 = (HID_MINI_DEV_EXTENSION *)6;
                pStack = (_IO_STACK_LOCATION *)status;
                _TlgCreateSz((const CHAR **)&v44, v20);
                _TlgWrite(v21, (unsigned __int8 *)&unk_1521F, v23, v25, 5, &v35);
                goto ExitUnlock;
                }
                */
            }
            else
            {
                pMiniDevExt->PnpState = 2;
                status = HumInitDevice(pDevObj);
                if (NT_SUCCESS(status) == FALSE)
                {
                    /*
                    __annotation("TMF:",
                    "d22491df-cdbd-30c3-83a0-2e589806a36e hidusb // SRC=Unknown_cxx00 MJ= MN=",
                    "#typev Unknown_cxx00 13 " % 0HumInitDevice failed during start IRP DeviceObject : 0x % 10!p!" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_PNP",
                    "{", "Arg, ItemPtr -- 10", "}",
                    "PUBLIC_TMF:")
                    */

                    pMiniDevExt->PnpState = 6;

                    /*
                    if ((unsigned int)dword_16008 > 5 && _TlgKeywordOn(v27, v29))
                    {
                    v20 = "PNP_HumInitDevice";
                    LABEL_39:
                    v43 = 0;
                    v36 = &pStack;
                    v42 = 4;
                    v41 = 0;
                    v40 = &pMiniDevExt_1;
                    v39 = 0;
                    v38 = 4;
                    v37 = 0;
                    pMiniDevExt_1 = (HID_MINI_DEV_EXTENSION *)6;
                    pStack = (_IO_STACK_LOCATION *)status;
                    _TlgCreateSz((const CHAR **)&v44, v20);
                    _TlgWrite(v21, (unsigned __int8 *)&unk_1521F, v23, v25, 5, &v35);
                    goto ExitUnlock;
                    }
                    */
                }
            }
            break;
        }
        }
    ExitUnlock:
        pIrp->IoStatus.Status = status;
        IoCompleteRequest(pIrp, IO_NO_INCREMENT);
        IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));
        return status;
    }
}

NTSTATUS HumGetReportDescriptor(PDEVICE_OBJECT pDevObj, PIRP pIrp, PBOOLEAN pNeedToCompleteIrp)
{
    NTSTATUS                status;
    PIO_STACK_LOCATION      pStack;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    SIZE_T                  OutBuffLen;
    PVOID                   pReportDesc;
    ULONG                   ReportDescLen;
    PHID_DEVICE_EXTENSION   pDevExt;

    pReportDesc = NULL;
    pStack = IoGetCurrentIrpStackLocation(pIrp);
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    ReportDescLen = pMiniDevExt->HidDesc.DescriptorList[0].wReportLength + 0x40;
    if (BooleanFlagOn(pMiniDevExt->Flags, DEXT_NO_HID_DESC) == FALSE)
    {
        status = HumGetDescriptorRequest(
            pDevObj,
            URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE,
            pMiniDevExt->HidDesc.DescriptorList[0].bReportType,
            &pReportDesc,
            &ReportDescLen,
            0,
            0,
            pMiniDevExt->pInterfaceInfo->InterfaceNumber);
    }
    else
    {
        PUSBD_PIPE_INFORMATION pPipeInfo;

        pPipeInfo = GetInterruptInputPipeForDevice(pMiniDevExt);
        if (pPipeInfo == NULL)
        {
            /*
            mov     esi, 0C0000182h
            mov     ecx, _WPP_GLOBAL_Control
            mov     dl, 3
            push    esi
            push    ebx
            push    offset _WPP_744a6a5ed1823cb27c30510bc9dd858a_Traceguids ; "^jJtÈ-¶<|0Q\v+¶‡Ë"
            mov     ecx, [ecx+30h]
            push    0Ah
            push    2
            call    _WPP_RECORDER_SF_qq@28 ; WPP_RECORDER_SF_qq(x,x,x,x,x,x,x)
            */
            pIrp->IoStatus.Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            status = HumQueueResetWorkItem(pDevObj, pIrp);
            if (status == STATUS_MORE_PROCESSING_REQUIRED)
            {
                *pNeedToCompleteIrp = FALSE;
                IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));
                return STATUS_PENDING;
            }

            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        else
        {
            UCHAR EndptAddr;

            EndptAddr = pPipeInfo->EndpointAddress;
            EndptAddr &= USB_ENDPOINT_DIRECTION_MASK;

            status = HumGetDescriptorRequest(
                pDevObj,
                URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT,
                pMiniDevExt->HidDesc.DescriptorList[0].bReportType,
                &pReportDesc,
                &ReportDescLen,
                0,
                0,
                EndptAddr);
        }
    }

    if (NT_SUCCESS(status) == FALSE)
    {
        if (status == STATUS_DEVICE_NOT_CONNECTED)
        {
            /*
            mov     ecx, _WPP_GLOBAL_Control
            mov     dl, 3
            push    esi
            push    ebx
            push    offset _WPP_744a6a5ed1823cb27c30510bc9dd858a_Traceguids; "^jJtÈ-¶<|0Q\v+¶‡Ë"
            mov     ecx, [ecx + 30h]
            push    0Bh
            push    2
            call    _WPP_RECORDER_SF_qq@28
            */
            return status;
        }
        else
        {
            /*
            mov     ecx, _WPP_GLOBAL_Control
            mov     dl, 3
            push    esi
            push    ebx
            push    offset _WPP_744a6a5ed1823cb27c30510bc9dd858a_Traceguids; "^jJtÈ-¶<|0Q\v+¶‡Ë"
            mov     ecx, [ecx + 30h]
            push    0Ah
            push    2
            call    _WPP_RECORDER_SF_qq@28; WPP_RECORDER_SF_qq(x, x, x, x, x, x, x)
            */
            NTSTATUS ResetStatus;

            pIrp->IoStatus.Status = status;
            ResetStatus = HumQueueResetWorkItem(pDevObj, pIrp);
            if (ResetStatus == STATUS_MORE_PROCESSING_REQUIRED)
            {
                *pNeedToCompleteIrp = FALSE;
                IoReleaseRemoveLockEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));
                return STATUS_PENDING;
            }

            return status;
        }
    }

    OutBuffLen = pStack->Parameters.DeviceIoControl.OutputBufferLength;
    if (OutBuffLen > pMiniDevExt->HidDesc.DescriptorList[0].wReportLength)
    {
        OutBuffLen = pMiniDevExt->HidDesc.DescriptorList[0].wReportLength;
    }
    if (OutBuffLen > ReportDescLen)
    {
        OutBuffLen = ReportDescLen;
    }

    RtlCopyMemory(pIrp->UserBuffer, pReportDesc, OutBuffLen);
    pIrp->IoStatus.Information = OutBuffLen;
    ExFreePool(pReportDesc);

    return status;
}

NTSTATUS HumGetDeviceAttributes(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PHID_DEVICE_EXTENSION   pDevExt;
    PHID_DEVICE_ATTRIBUTES  pDevAttrs;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    PIO_STACK_LOCATION      pStack;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    pDevAttrs = (PHID_DEVICE_ATTRIBUTES)pIrp->UserBuffer;
    pStack = IoGetCurrentIrpStackLocation(pIrp);
    if (pStack->Parameters.DeviceIoControl.OutputBufferLength < 0x20)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    pIrp->IoStatus.Information = 0x20;
    pDevAttrs->Size = 0x20;
    pDevAttrs->VendorID = pMiniDevExt->pDevDesc->idVendor;
    pDevAttrs->ProductID = pMiniDevExt->pDevDesc->idProduct;
    pDevAttrs->VersionNumber = pMiniDevExt->pDevDesc->bcdDevice;

    return STATUS_SUCCESS;
}

NTSTATUS HumGetHidDescriptor(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PHID_DEVICE_EXTENSION   pDevExt;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    PHID_DESCRIPTOR         pHidDesc;
    UCHAR                   DescLen;
    ULONG                   OutBuffLen;
    PIO_STACK_LOCATION      pStack;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    pHidDesc = &pMiniDevExt->HidDesc;
    DescLen = pMiniDevExt->HidDesc.bLength;
    if (DescLen == 0)
    {
        pIrp->IoStatus.Information = 0;
        return STATUS_INVALID_PARAMETER;
    }
    pStack = IoGetCurrentIrpStackLocation(pIrp);
    OutBuffLen = pStack->Parameters.DeviceIoControl.OutputBufferLength;
    if (OutBuffLen > DescLen)
    {
        OutBuffLen = DescLen;
    }
    memcpy(pIrp->UserBuffer, pHidDesc, OutBuffLen);
    pIrp->IoStatus.Information = OutBuffLen;

    return STATUS_SUCCESS;
}

NTSTATUS HumAbortPendingRequests(PDEVICE_OBJECT pDevObj)
{
    NTSTATUS                    status;
    USHORT                      UrbLen;
    PHID_MINI_DEV_EXTENSION     pMiniDevExt;
    PURB                        pUrb;
    PUSBD_INTERFACE_INFORMATION pInterfaceInfo;
    USBD_PIPE_HANDLE            PipeHandle;
    PHID_DEVICE_EXTENSION       pDevExt;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    UrbLen = sizeof(struct _URB_PIPE_REQUEST);
    pUrb = (PURB)ExAllocatePoolWithTag(NonPagedPoolNx, UrbLen, HID_USB_TAG);
    if (pUrb == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    pInterfaceInfo = pMiniDevExt->pInterfaceInfo;
    if (pInterfaceInfo == NULL || pInterfaceInfo->NumberOfPipes == 0)
    {
        status = STATUS_NO_SUCH_DEVICE;

        /*
        __annotation("TMF:",
        "d22491df-cdbd-30c3-83a0-2e589806a36e hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 20 " % 0No such device status : % 10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_PNP",
        "{", "Arg, ItemNTSTATUS -- 10", "}",
        "PUBLIC_TMF:")
        */
    }
    else
    {
        PipeHandle = pInterfaceInfo->Pipes[0].PipeHandle;
        if (PipeHandle)
        {
            status = STATUS_NO_SUCH_DEVICE;
        }
        else
        {
            pUrb->UrbHeader.Length = UrbLen;
            pUrb->UrbHeader.Function = URB_FUNCTION_ABORT_PIPE;
            pUrb->UrbPipeRequest.PipeHandle = PipeHandle;
            status = HumCallUSB(pDevObj, pUrb);
            if (NT_SUCCESS(status) == FALSE)
            {
                /*
                __annotation("TMF:",
                "d22491df-cdbd-30c3-83a0-2e589806a36e hidusb // SRC=Unknown_cxx00 MJ= MN=",
                "#typev Unknown_cxx00 19 " % 0URB_FUNCTION_ABORT_PIPE failed with status : % 10!s!" //   LEVEL=TRACE_LEVEL_WARNING FLAGS=TRACE_FLAG_PNP",
                "{", "Arg, ItemNTSTATUS -- 10", "}",
                "PUBLIC_TMF:")
                */
            }
        }
    }

    ExFreePool(pUrb);

    return status;
}

NTSTATUS HumRemoveDevice(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS                status;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    ULONG                   PrevPnpState;
    PHID_DEVICE_EXTENSION   pDevExt;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    PrevPnpState = pMiniDevExt->PnpState;
    pMiniDevExt->PnpState = 5;
    if (PrevPnpState != 3 && PrevPnpState != 4)
    {
        HumDecrementPendingRequestCount(pMiniDevExt);
    }
    if (PrevPnpState == 2)
    {
        HumAbortPendingRequests(pDevObj);
    }
    KeWaitForSingleObject(&pMiniDevExt->Event, Executive, KernelMode, FALSE, NULL);

    IoCopyCurrentIrpStackLocationToNext(pIrp);
    pIrp->IoStatus.Status = 0;
    status = IofCallDriver(pDevExt->NextDeviceObject, pIrp);
    IoReleaseRemoveLockAndWaitEx(&pMiniDevExt->RemoveLock, pIrp, sizeof(pMiniDevExt->RemoveLock));

    if (pMiniDevExt->pInterfaceInfo)
    {
        ExFreePool(pMiniDevExt->pInterfaceInfo);
        pMiniDevExt->pInterfaceInfo = NULL;
    }
    if (pMiniDevExt->pDevDesc)
    {
        ExFreePool(pMiniDevExt->pDevDesc);
        pMiniDevExt->pDevDesc = NULL;
    }

    return status;
}

NTSTATUS HumStopDevice(PDEVICE_OBJECT pDevObj)
{
    NTSTATUS                status;
    USHORT                  UrbLen;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    PURB                    pUrb;
    PHID_DEVICE_EXTENSION   pDevExt;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;

    pMiniDevExt->PnpState = 3;
    HumAbortPendingRequests(pDevObj);
    HumDecrementPendingRequestCount(pMiniDevExt);
    KeWaitForSingleObject(&pMiniDevExt->Event, Executive, KernelMode, FALSE, NULL);

    UrbLen = sizeof(struct _URB_SELECT_CONFIGURATION);
    pUrb = (PURB)ExAllocatePoolWithTag(NonPagedPoolNx, UrbLen, HID_USB_TAG);
    if (pUrb == NULL)
    {
        status = STATUS_UNSUCCESSFUL;
    }
    else
    {
        pUrb->UrbHeader.Length = UrbLen;
        pUrb->UrbHeader.Function = URB_FUNCTION_SELECT_CONFIGURATION;
        pUrb->UrbSelectConfiguration.ConfigurationDescriptor = NULL;
        status = HumCallUSB(pDevObj, pUrb);
        ExFreePool(pUrb);
        if (NT_SUCCESS(status))
        {
            return status;
        }
    }

    pMiniDevExt->PnpState = 4;

    return status;
}

NTSTATUS HumSystemControl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PHID_DEVICE_EXTENSION pDevExt;

    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    IoCopyCurrentIrpStackLocationToNext(pIrp);
    return IoCallDriver(pDevExt->NextDeviceObject, pIrp);
}

NTSTATUS HumGetMsGenreDescriptor(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS                status;
    USHORT                  UrbLen;
    PIO_STACK_LOCATION      pStack;
    PVOID                   pMappedBuff;
    PURB                    pUrb;
    PHID_MINI_DEV_EXTENSION pMiniDevExt;
    ULONG                   OutBuffLen;
    PHID_DEVICE_EXTENSION   pDevExt;

    pStack = IoGetCurrentIrpStackLocation(pIrp);
    pDevExt = (PHID_DEVICE_EXTENSION)pDevObj->DeviceExtension;
    pMiniDevExt = (PHID_MINI_DEV_EXTENSION)pDevExt->MiniDeviceExtension;
    /*
    __annotation("TMF:",
    "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
    "#typev Unknown_cxx00 26 "%0Received request for genre descriptor" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_HID",
    "{", "}",
    "PUBLIC_TMF:")
    */
    OutBuffLen = pStack->Parameters.DeviceIoControl.OutputBufferLength;
    if (OutBuffLen == 0)
    {
        return STATUS_INVALID_USER_BUFFER;
    }

    pMappedBuff = MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, MdlMappingNoExecute | NormalPagePriority);
    if (pMappedBuff == NULL)
    {
        return STATUS_INVALID_USER_BUFFER;
    }

    UrbLen = sizeof(struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST);
    pUrb = (PURB)ExAllocatePoolWithTag(NonPagedPoolNx, UrbLen, HID_USB_TAG);
    if (pUrb == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    memset(pUrb, 0, UrbLen);
    memset(pMappedBuff, 0, OutBuffLen);
    pUrb->UrbHeader.Length = UrbLen;
    pUrb->UrbHeader.Function = URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR;
    pUrb->UrbOSFeatureDescriptorRequest.TransferBufferMDL = NULL;
    pUrb->UrbOSFeatureDescriptorRequest.Recipient = 1;
    pUrb->UrbOSFeatureDescriptorRequest.TransferBufferLength = OutBuffLen;
    pUrb->UrbOSFeatureDescriptorRequest.TransferBuffer = pMappedBuff;
    pUrb->UrbOSFeatureDescriptorRequest.InterfaceNumber = pMiniDevExt->pInterfaceInfo->InterfaceNumber;
    pUrb->UrbOSFeatureDescriptorRequest.MS_FeatureDescriptorIndex = 1;
    pUrb->UrbOSFeatureDescriptorRequest.UrbLink = 0;

    /*
    __annotation("TMF:",
    "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
    "#typev Unknown_cxx00 27 " % 0Sending OS feature descriptor request" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_HID",
    "{", "}",
    "PUBLIC_TMF:")
    */

    status = HumCallUSB(pDevObj, pUrb);
    if (NT_SUCCESS(status) == FALSE)
    {
        /*
        __annotation("TMF:",
        "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 30 "%0Call to HumCallUSB returned failing status %10!s!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_HID",
        "{", "Arg, ItemNTSTATUS -- 10", "}",
        "PUBLIC_TMF:")
        */

        ExFreePool(pUrb);
    }
    else if (USBD_SUCCESS(pUrb->UrbHeader.Status) == FALSE)
    {
        /*
        __annotation("TMF:",
        "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 29 " % 0Genre descriptor request unsuccessful, UrbHeader.Status value 0x % 10!x!" //   LEVEL=TRACE_LEVEL_ERROR FLAGS=TRACE_FLAG_HID",
        "{", "Arg, ItemLong -- 10", "}",
        "PUBLIC_TMF:")
        */

        ExFreePool(pUrb);
        status = STATUS_UNSUCCESSFUL;
    }
    else
    {
        /*
        __annotation("TMF:",
        "744a6a5e-d182-3cb2-7c30-510bc9dd858a hidusb // SRC=Unknown_cxx00 MJ= MN=",
        "#typev Unknown_cxx00 28 " % 0Genre descriptor request successful" //   LEVEL=TRACE_LEVEL_INFORMATION FLAGS=TRACE_FLAG_HID",
        "{", "}",
        "PUBLIC_TMF:")
        */

        pIrp->IoStatus.Information = pUrb->UrbOSFeatureDescriptorRequest.TransferBufferLength;
        ExFreePool(pUrb);
        status = STATUS_SUCCESS;
    }

    return status;
}

NTSTATUS HumGetPhysicalDescriptor(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS status;
    PVOID    pPhysDesc;
    ULONG    OutBuffLen;

    OutBuffLen = pIrp->Tail.Overlay.CurrentStackLocation->Parameters.DeviceIoControl.OutputBufferLength;
    pPhysDesc = MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, MdlMappingNoExecute | NormalPagePriority);
    if (OutBuffLen != 0 && pPhysDesc != NULL)
    {
        status = HumGetDescriptorRequest(pDevObj, URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE, 35, &pPhysDesc, &OutBuffLen, 0, 0, 0);
    }
    else
    {
        status = STATUS_INVALID_USER_BUFFER;
    }
    return status;
}
