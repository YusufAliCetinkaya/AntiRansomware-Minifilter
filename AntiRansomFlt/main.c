#include <fltKernel.h>

PFLT_FILTER gFilterHandle = NULL;

// Fonksiyon prototipleri
NTSTATUS AntiRansomUnload(FLT_FILTER_UNLOAD_FLAGS Flags);
FLT_PREOP_CALLBACK_STATUS PreCreateCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext);
FLT_POSTOP_CALLBACK_STATUS PostCreateCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags);
FLT_PREOP_CALLBACK_STATUS PreWriteCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext);
FLT_POSTOP_CALLBACK_STATUS PostWriteCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags);
FLT_PREOP_CALLBACK_STATUS PreSetInfoCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext);
FLT_POSTOP_CALLBACK_STATUS PostSetInfoCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags);

// --- YARDIMCI FONKSİYON: Kernel İçi Tamsayı Logaritma (Sonucu 1000 ile çarpar) ---
ULONG IntegerLog2_Scaled(ULONG x) {
    ULONG intPart = 0;
    ULONG temp = x;
    ULONG fracPart = 0;
    ULONG mask;
    ULONG remainder;

    if (x == 0) return 0;

    // Tam sayı kısmını bul (en yüksek set edilen bit)
    while (temp >>= 1) {
        intPart++;
    }

    // Basit doğrusal interpolasyon (Linear Interpolation) ile kesirli kısmı hesapla
    mask = (1 << intPart) - 1;
    remainder = x & mask;

    if (intPart > 0) {
        fracPart = (remainder * 1000) / (1 << intPart);
    }

    return (intPart * 1000) + fracPart;
}

// --- YARDIMCI FONKSİYON: Entropi Hesaplama (Sonucu 1000 ile çarpılmış döner, örn: 7954 = 7.954) ---
ULONG CalculateEntropyScaled(PUCHAR Buffer, ULONG Length) {
    ULONG counts[256] = { 0 };
    ULONG i;
    ULONG totalLog2;
    ULONG sumCountLog2 = 0;
    ULONG entropy;

    if (Length == 0) return 0;

    // 1. Frekans sayımı (Hangi bayttan kaç tane var?)
    for (i = 0; i < Length; i++) {
        counts[Buffer[i]]++;
    }

    // 2. Toplam veri uzunluğunun logaritması
    totalLog2 = IntegerLog2_Scaled(Length);

    // 3. Sigma (count * log2(count)) işlemi
    for (i = 0; i < 256; i++) {
        if (counts[i] > 0) {
            sumCountLog2 += (counts[i] * IntegerLog2_Scaled(counts[i]));
        }
    }

    // 4. Formül: H = log2(N) - (1/N) * sum(count * log2(count))
    entropy = totalLog2 - (sumCountLog2 / Length);

    return entropy;
}

const FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE, 0, PreCreateCallback, PostCreateCallback },
    { IRP_MJ_WRITE, 0, PreWriteCallback, PostWriteCallback },
    { IRP_MJ_SET_INFORMATION, 0, PreSetInfoCallback, PostSetInfoCallback },
    { IRP_MJ_OPERATION_END }
};

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

// --- YARDIMCI FONKSİYON: İşlemleri Güvenlice Loglama ---
VOID LogFileOperation(PFLT_CALLBACK_DATA Data, PCSTR OperationName, ULONG EntropyScaled) {
    NTSTATUS status;
    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    ULONG processId;

    if (KeGetCurrentIrql() > APC_LEVEL) {
        return;
    }

    processId = (ULONG)(ULONG_PTR)FltGetRequestorProcessId(Data);
    status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo);

    if (NT_SUCCESS(status)) {
        FltParseFileNameInformation(nameInfo);

        // Eğer entropi hesaplanmışsa (WRITE işlemiyse) ekrana entropi değerini de yaz
        if (EntropyScaled > 0) {
            ULONG entInt = EntropyScaled / 1000;
            ULONG entFrac = EntropyScaled % 1000;
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[AntiRansom] %s | PID: %lu | Entropi: %lu.%03lu | Dosya: %wZ\n", OperationName, processId, entInt, entFrac, &nameInfo->Name);
        }
        else {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[AntiRansom] %s | PID: %lu | Dosya: %wZ\n", OperationName, processId, &nameInfo->Name);
        }

        FltReleaseFileNameInformation(nameInfo);
    }
}

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

NTSTATUS AntiRansomUnload(FLT_FILTER_UNLOAD_FLAGS Flags) {
    UNREFERENCED_PARAMETER(Flags);
    FltUnregisterFilter(gFilterHandle);
    return STATUS_SUCCESS;
}

// --- IRP_MJ_CREATE ---
FLT_PREOP_CALLBACK_STATUS PreCreateCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext) {
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    LogFileOperation(Data, "CREATE", 0);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS PostCreateCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags) {
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

// --- IRP_MJ_WRITE (Kritik Algoritmanın Çalıştığı Yer) ---
FLT_PREOP_CALLBACK_STATUS PreWriteCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext) {
    PVOID writeBuffer = NULL;
    ULONG writeLength = 0;
    ULONG entropy = 0;

    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    // Yazılmak istenen verinin uzunluğunu al
    writeLength = Data->Iopb->Parameters.Write.Length;

    // Eğer Paging I/O seviyesindeysek veya yazılacak veri yoksa atla
    if (writeLength > 0 && KeGetCurrentIrql() <= APC_LEVEL) {

        // MDL (Memory Descriptor List) üzerinden güvenli kernel hafıza adresini al
        if (Data->Iopb->Parameters.Write.MdlAddress != NULL) {
            writeBuffer = MmGetSystemAddressForMdlSafe(Data->Iopb->Parameters.Write.MdlAddress, NormalPagePriority | MdlMappingNoExecute);
        }
        else {
            writeBuffer = Data->Iopb->Parameters.Write.WriteBuffer;
        }

        // Buffer başarıyla alındıysa entropiyi hesapla
        if (writeBuffer != NULL) {
            __try {
                // Sürücüyü Mavi Ekrana (Page Fault) karşı koruma bloğu
                entropy = CalculateEntropyScaled((PUCHAR)writeBuffer, writeLength);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                entropy = 0; // Okuma hatası olursa entropiyi sıfır kabul et
            }
        }
    }

    LogFileOperation(Data, "WRITE", entropy);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS PostWriteCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags) {
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);
    return FLT_POSTOP_FINISHED_PROCESSING;
}

// --- IRP_MJ_SET_INFORMATION ---
FLT_PREOP_CALLBACK_STATUS PreSetInfoCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID* CompletionContext) {
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    LogFileOperation(Data, "SET_INFO", 0);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS PostSetInfoCallback(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID CompletionContext, FLT_POST_OPERATION_FLAGS Flags) {
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);
    return FLT_POSTOP_FINISHED_PROCESSING;
}