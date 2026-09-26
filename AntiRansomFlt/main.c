#include <fltKernel.h>

PFLT_FILTER gFilterHandle = NULL;

// Fonksiyonların tanımlarını derleyiciye önceden bildirir.
NTSTATUS AntiRansomUnload(FLT_FILTER_UNLOAD_FLAGS Flags);
FLT_PREOP_CALLBACK_STATUS PreCreateCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext);
FLT_PREOP_CALLBACK_STATUS PreWriteCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext);
FLT_PREOP_CALLBACK_STATUS PreSetInfoCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext);

// Kernel içinde ondalıklı sayı kullanmadan Log2 hesaplaması yapar ve sonucu 1000 ile çarparak döndürür.
ULONG IntegerLog2_Scaled(ULONG x) {
    ULONG intPart = 0;
    ULONG temp = x;
    ULONG fracPart = 0;
    ULONG mask;
    ULONG remainder;

    if (x == 0) return 0;

    while (temp >>= 1) {
        intPart++;
    }

    mask = (1 << intPart) - 1;
    remainder = x & mask;

    if (intPart > 0) {
        fracPart = (remainder * 1000) / (1 << intPart);
    }

    return (intPart * 1000) + fracPart;
}

// Diske yazılacak veri arabelleğinin Shannon Entropisini tamsayı yaklaşımıyla hesaplar.
ULONG CalculateEntropyScaled(PUCHAR Buffer, ULONG Length) {
    ULONG counts[256] = { 0 };
    ULONG i;
    ULONG totalLog2;
    ULONG64 sumCountLog2 = 0;
    ULONG entropy;

    if (Length == 0) return 0;

    for (i = 0; i < Length; i++) {
        counts[Buffer[i]]++;
    }

    totalLog2 = IntegerLog2_Scaled(Length);

    for (i = 0; i < 256; i++) {
        if (counts[i] > 0) {
            sumCountLog2 += ((ULONG64)counts[i] * IntegerLog2_Scaled(counts[i]));
        }
    }

    entropy = totalLog2 - (ULONG)(sumCountLog2 / Length);

    return entropy;
}

// Yakalanan dosya operasyonlarını, Process ID ve hesaplanan entropi değeriyle birlikte Kernel Debugger ekranına yazdırır.
VOID LogFileOperation(PFLT_CALLBACK_DATA Data, PCSTR OperationName, ULONG EntropyScaled, BOOLEAN IsPreCreate) {
    NTSTATUS status;
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    ULONG processId;
    FLT_FILE_NAME_OPTIONS nameOptions;

    if (KeGetCurrentIrql() > APC_LEVEL) {
        return;
    }

    processId = (ULONG)(ULONG_PTR)FltGetRequestorProcessId(Data);

    nameOptions = IsPreCreate ?
        (FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_DEFAULT) :
        (FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT);

    status = FltGetFileNameInformation(Data, nameOptions, &nameInfo);

    if (NT_SUCCESS(status)) {
        FltParseFileNameInformation(nameInfo);

        if (EntropyScaled > 0) {
            ULONG entInt = EntropyScaled / 1000;
            ULONG entFrac = EntropyScaled % 1000;
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "[AntiRansom] %s | PID: %lu | Entropi: %lu.%03lu | Dosya: %wZ\n",
                OperationName, processId, entInt, entFrac, &nameInfo->Name);
        }
        else {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "[AntiRansom] %s | PID: %lu | Dosya: %wZ\n",
                OperationName, processId, &nameInfo->Name);
        }

        FltReleaseFileNameInformation(nameInfo);
    }
}

// Sürücünün dinleyeceği dosya operasyonlarını (Create, Write, SetInfo) ve çalıştırılacak fonksiyonları Filter Manager'a bildirir.
const FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE, 0, PreCreateCallback, NULL },
    { IRP_MJ_WRITE, 0, PreWriteCallback, NULL },
    { IRP_MJ_SET_INFORMATION, 0, PreSetInfoCallback, NULL },
    { IRP_MJ_OPERATION_END }
};

// Sürücünün genel yapılandırma, versiyon ve geri çağırma (callback) dizisini barındıran temel kayıt yapısıdır.
const FLT_REGISTRATION FilterRegistration = {
    sizeof(FLT_REGISTRATION),
    FLT_REGISTRATION_VERSION,
    0,
    NULL,
    Callbacks,
    AntiRansomUnload,
    NULL,
    NULL,
    NULL,
    NULL
};

// Sürücü belleğe yüklendiğinde işletim sistemi tarafından ilk çağrılan ve filtreyi başlatan ana giriş noktasıdır.
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);

    status = FltRegisterFilter(DriverObject, &FilterRegistration, &gFilterHandle);
    if (NT_SUCCESS(status)) {
        status = FltStartFiltering(gFilterHandle);
        if (!NT_SUCCESS(status)) {
            FltUnregisterFilter(gFilterHandle);
        }
    }
    return status;
}

// Sürücü sistemden kaldırılırken (stop komutuyla) çağrılır ve filtreyi güvenli bir şekilde kapatır.
NTSTATUS AntiRansomUnload(FLT_FILTER_UNLOAD_FLAGS Flags) {
    UNREFERENCED_PARAMETER(Flags);
    FltUnregisterFilter(gFilterHandle);
    return STATUS_SUCCESS;
}

// Bir işlem sisteme yeni bir dosya açma veya oluşturma isteği gönderdiğinde araya girer.
FLT_PREOP_CALLBACK_STATUS PreCreateCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext) {
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    LogFileOperation(Data, "CREATE", 0, TRUE);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

// Bir işlem dosyaya veri yazmaya çalıştığında araya girerek yazılacak verinin entropisini hesaplar.
FLT_PREOP_CALLBACK_STATUS PreWriteCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext) {
    PVOID writeBuffer = NULL;
    ULONG writeLength = 0;
    ULONG entropy = 0;

    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    if (BooleanFlagOn(Data->Iopb->IrpFlags, IRP_PAGING_IO)) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    writeLength = Data->Iopb->Parameters.Write.Length;

    if (writeLength > 0 && KeGetCurrentIrql() <= APC_LEVEL) {

        if (Data->Iopb->Parameters.Write.MdlAddress != NULL) {
            writeBuffer = MmGetSystemAddressForMdlSafe(Data->Iopb->Parameters.Write.MdlAddress, NormalPagePriority | MdlMappingNoExecute);
        }
        else {
            writeBuffer = Data->Iopb->Parameters.Write.WriteBuffer;
        }

        if (writeBuffer != NULL) {
            __try {
                entropy = CalculateEntropyScaled((PUCHAR)writeBuffer, writeLength);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                entropy = 0;
            }
        }
    }

    LogFileOperation(Data, "WRITE", entropy, FALSE);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

// Bir işlem dosyanın özniteliklerini (ismini değiştirme, silme işaretleme vb.) değiştirmeye çalıştığında araya girer.
FLT_PREOP_CALLBACK_STATUS PreSetInfoCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext) {
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    LogFileOperation(Data, "SET_INFO", 0, FALSE);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
