# AntiRansomFlt
**Windows Kernel-Mode Anti-Ransomware Minifilter Driver**

Bu proje, Windows işletim sistemlerinde fidye yazılımlarının (Ransomware) dosya şifreleme davranışlarını çekirdek düzeyinde (Ring 0) tespit etmek amacıyla C dili ile geliştirilmiş bir Minifilter dosya sistemi sürücüsüdür.

Mevcut sürüm, **Canlı Entropi Analizi (Telemetry & Entropy Calculation)** aşamasındadır. Sistemdeki dosya yazma işlemlerini yakalar, çekirdek uyumlu özel bir matematiksel algoritma ile yazılan verinin şifreli olup olmadığını hesaplar ve sonuçları anlık olarak raporlar. Henüz engelleme (blocking) yapmamaktadır.

---

## Projenin Amacı
Geleneksel anti-virüs yazılımlarının aksine, bu proje zararlı yazılımın kimliğine (imzasına) değil, **davranışına ve ürettiği verinin matematiğine** odaklanır. Fidye yazılımları orijinal bir dosyayı okuyup şifrelediğinde, diske yazılacak verinin karmaşıklığı maksimum seviyeye çıkar. Projenin amacı, bu karmaşıklığı (Shannon Entropisi) diske yazılmadan milisaniyeler önce RAM üzerinde hesaplamak ve şifrelenmiş (entropisi 7.5 ve üzeri) verileri tespit etmektir.

## Temel Özellikler (Mevcut Sürüm)
* **Gerçek Zamanlı IRP Yakalama:** `IRP_MJ_CREATE`, `IRP_MJ_WRITE` ve `IRP_MJ_SET_INFORMATION` operasyonları Filter Manager üzerinden anlık olarak izlenir.
* **Kernel Uyumlu Shannon Entropisi:** Çekirdek seviyesinde mavi ekrana (BSOD) yol açan ondalıklı sayı (`float`, `double`) ve `math.h` kütüphaneleri kullanılmadan, tamamen tamsayı (integer approximation) ve bit kaydırma mantığıyla çalışan özel bir entropi algoritması barındırır.
* **Güvenli Arabellek (Buffer) Okuma:** `PreWriteCallback` içerisinde, diske yazılacak veri paketleri MDL (Memory Descriptor List) üzerinden `MmGetSystemAddressForMdlSafe` ile güvenlice kernel hafızasına çekilir ve `__try/__except` bloklarıyla sayfa hatalarına (Page Fault) karşı korunur.
* **Yüksek Doğruluklu Analiz:** Düşük entropili (sıradan metinler, loglar: ~3.000 - 4.500) dosyalar ile yüksek entropili (şifrelenmiş veri, Ransomware, ZIP/RAR: ~7.800 - 7.990) dosyaları milisaniyeler içinde matematiksel olarak ayırt eder.
* **Canlı Telemetri:** Tespit edilen hareketler; Process ID, dosya yolu ve 1000 ile ölçeklendirilmiş Entropi değeriyle birlikte `DbgPrintEx` üzerinden Kernel Debugger ekranına aktarılır.

---

## Nasıl Çalışır?
1. Sürücü `FltRegisterFilter` ile işletim sistemine kaydolur.
2. Bir işlem dosyaya veri yazmaya çalıştığında (`IRP_MJ_WRITE`), sürücü araya girer ve yazılacak olan verinin (Buffer) boyutunu ve hafıza adresini alır.
3. Yazılacak veri, 256 elemanlı bir frekans dizisine sokulur ve her bir baytın tekrar sayısı hesaplanarak **Shannon Entropisi** bulunur.
4. Elde edilen entropi değeri (Örn: 7.954), PID ve dosya adıyla birlikte loglanır.
5. Sürücü, analizi bitirdikten sonra `FLT_PREOP_SUCCESS_NO_CALLBACK` döndürerek işlemin (şimdilik) normal seyrinde diske yazılmasına izin verir.

---

## Kurulum ve Test (Geliştirici Ortamı)

**Gereksinimler:**
* Windows Driver Kit (WDK) ve Software Development Kit (SDK)
* Test işlemleri için yapılandırılmış bir Sanal Makine (VM)

**Test Ortamında Çalıştırma:**
1. Sanal makinenizde Test Modunu aktif edin (CMD Yönetici):
   ```cmd
   bcdedit /set testsigning on
