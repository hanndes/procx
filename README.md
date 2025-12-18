🧠 ProcX – Çoklu Terminal Process Yöneticisi

Birden fazla terminal (instance) üzerinden çalışan, process yönetimi, inter-process communication (IPC) ve eşzamanlılık (concurrency) kavramlarını kullanan bir işletim sistemi dönem projesi.

📋 Proje Hakkında

ProcX, terminal tabanlı bir process yönetim aracıdır ve şunları yapmanıza olanak tanır:

    Birden fazla terminal penceresinden eş zamanlı olarak process başlatma ve yönetme
    Shared memory kullanarak tüm instance'lar arasında process durumlarını senkronize etme
    Race condition ve zombie process problemlerini önleme
    Process'leri attached (ön plan) veya detached (arka plan) modunda çalıştırma

✨ Özellikler

    Çoklu Terminal Desteği: Birden fazla terminalden aynı anda çalıştırma
    Shared Memory (POSIX): Tüm instance'ların erişebildiği ortak process tablosu
    Semaphore Koruması: POSIX semaphore ile race condition önleme
    Message Queue (System V): Instance'lar arası bildirim sistemi
    Attached & Detached Modları: Esnek process çalıştırma seçenekleri
    Zombie Önleme: Monitor thread ile otomatik temizleme
    Temiz Çıkış: Ctrl+C ile graceful shutdown
    Thread Tabanlı Mimari: Asenkron işlemler için monitor ve listener thread'leri

🛠️ Kullanılan Teknolojiler

Bileşen	Amaç
shm_open + mmap	Instance'lar arası paylaşılan process tablosu
sem_open/wait/post	Kritik bölge koruması
msgget/snd/rcv	Instance'lar arası iletişim
pthread	Monitor & Listener thread'leri
fork + execvp	Process başlatma
waitpid(WNOHANG)	Zombie process önleme
signal(SIGINT)	Ctrl+C yakalama


🏗️ Mimari

┌──────────────────────┐
│ Terminal Instance    │
├──────────────────────┤
│  Ana Thread          │
│   ├─ Menü            │
│   ├─ Process Başlat  │
│   ├─ Process Durdur  │
│   └─ Process Listele │
│                      │
│  Monitor Thread ─────┼──→ Zombie temizleme
│  Listener Thread ────┼──→ IPC mesajları
└──────────────────────┘
│
▼
┌──────────────────────────────┐
│   Shared Memory              │
│   - processes[50]            │
│   - process_count            │
│   - active_procx             │
└──────────────────────────────┘
▲
│
┌──────────────────────────────┐
│   Semaphore                  │
│   - Race condition koruması  │
└──────────────────────────────┘
│
▼
┌──────────────────────────────┐
│   Message Queue              │
│   - START/STOP bildirimleri  │
└──────────────────────────────┘

🔄 Process Türleri

🔹 Attached Process

    Terminale bağlı olarak ön planda çalışır
    Ctrl+C ile sonlandırılabilir
    waitpid() ile beklenir
    Bittiğinde anında listeden kaldırılır

🔹 Detached Process

    setsid() kullanarak arka planda çalışır
    Terminal kapansa bile çalışmaya devam eder
    Monitor thread tarafından takip edilir
    Sonlandığında otomatik olarak temizlenir

🧵 Thread Görevleri

🛠️ Monitor Thread

    Sadece DETACHED process'leri izler
    waitpid(WNOHANG) ile zombie process'leri önler
    kill(pid, 0) ile process'in sistemde var olup olmadığını kontrol eder
    Sonlanan process için:
        STATUS_TERMINATED olarak işaretler
        is_active = 0 yapar
        IPC ile diğer instance'lara bildirim gönderir

📡 Listener Thread

    Message Queue'yu sürekli dinler
    Diğer terminallerde şu olayları ekrana yazdırır:
        Process başlatıldı
        Process sonlandırıldı
    IPC_NOWAIT kullanır → asla bloklamaz
    Ana menü akışını engellemez

🔐 Senkronizasyon Stratejisi
Neden Semaphore?

Shared memory tüm instance'lar tarafından aynı anda erişilebilir. Şu işlemler kritik bölgedir:

    Process ekleme
    Process silme
    Sayaç güncelleme (process_count, active_procx)

Çözüm:
c

sem_wait(sem);
// ... shared memory erişimi ...
sem_post(sem);

Neden IPC_NOWAIT?
c

msgsnd(msgid, &msg, msg_size, IPC_NOWAIT);

Sebepler:

    Menü yanıt vermeye devam etmeli
    Monitor thread semaphore tutarken kilitlenmemeli
    Ctrl+C sırasında program donmamalı
    Mesajlar sadece bildirim amaçlıdır (kritik veri shared memory'dedir)
    Kuyruk doluysa mesajın düşmesi kabul edilebilir

☠️ Zombie Process Önleme

    Attached process: waitpid(pid, 0) ile temizlenir
    Detached process: Monitor thread tarafından yönetilir:
        Periyodik waitpid(pid, WNOHANG) kontrolü
        ECHILD ve ESRCH hata kontrolü
        Otomatik olarak process tablosundan kaldırma

🧹 Temiz Çıkış (Cleanup)

Ctrl+C basıldığında veya Exit seçildiğinde:

    Monitor ve listener thread'lerine çıkış sinyali gönderilir
    Thread'ler pthread_cancel + pthread_join ile sonlandırılır
    Bu instance'ın başlattığı attached process'ler sonlandırılır
    Instance sayacı (active_procx) azaltılır
    Eğer son instance ise:
        Shared memory silinir (shm_unlink)
        Semaphore kaldırılır (sem_unlink)
        Message queue yok edilir (msgctl IPC_RMID)

📦 Derleme ve Çalıştırma
bash

gcc procx.c -o procx -pthread
./procx

Çoklu instance test için:
bash

# Terminal 1
./procx

# Terminal 2
./procx

# Terminal 3
./procx

🎮 Kullanım Örneği

╔══════════════════════════════════╗
║            ProcX v1.0            ║
╠══════════════════════════════════╣
║ 1. Yeni Program Calistir         ║
║ 2. Calisan Programlari Listele   ║
║ 3. Program Sonlandir             ║
║ 0. Cikis                         ║
╚══════════════════════════════════╝

Seciminiz: 1
Calistirilacak komutu girin: sleep 100
Mod secin (0: Attached, 1: Detached): 1
[SUCCESS] Process baslatildi: PID 12345

[IPC] Process BASLATILDI: PID 12345 (Gonderen: 12340)

🎯 Kazanımlar

Bu proje sayesinde şunları öğrenebilirsiniz:

    IPC Mekanizmaları: Shared memory, semaphore ve message queue'lerin gerçek kullanımı
    Eşzamanlılık: Race condition ve deadlock'tan korunma yöntemleri
    Process Yönetimi: Fork, exec, wait ve sinyal yönetimi
    Zombie Process: Zombie process probleminin pratik çözümü
    Thread + Process Koordinasyonu: Threading ve IPC'nin birlikte kullanımı
    İşletim Sistemi Seviyesinde Senkronizasyon: Production-grade senkronizasyon kalıpları

🐛 Önemli Uygulama Detayları

    Maksimum 50 eş zamanlı process (MAX_PROCESS)
    Komut doğrulama çalıştırmadan önce
    Owner tracking her process için cleanup amaçlı
    Graceful degradation message queue dolu olduğunda
    Baştan sona thread-safe operasyonlar

📝 Notlar

    Tüm instance'lar aynı process tablosunu paylaşır
    Detached process'ler terminal kapansa bile çalışmaya devam eder
    Message queue sadece bildirim amaçlıdır
    Semaphore veri tutarlılığını garanti eder
    Beklenmeyen sonlanmalarda bile düzgün cleanup yapılır

🔧 Gereksinimler

    İşletim Sistemi: Linux
    Derleyici: pthread desteği olan GCC
    Kütüphaneler: POSIX threads, POSIX shared memory, System V IPC

Ders: İşletim Sistemleri
Tür: Dönem Projesi
Konu: Process Yönetimi, IPC, Eşzamanlılık
