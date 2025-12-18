ProcX v1.0 - Gelişmiş Süreç Yönetim Sistemi

ProcX, Linux sistemler üzerinde süreçleri (process) yönetmek, izlemek ve süreçler arası haberleşmeyi (IPC) kullanarak birden fazla ProcX örneği (instance) arasında senkronizasyon sağlamak amacıyla C diliyle geliştirilmiş bir sistem aracıdır.

🚀 Özellikler

    Çoklu Mod Desteği: Programları terminale bağlı (Attached) veya arka planda bağımsız (Detached) şekilde çalıştırabilme.

    Paylaşılan Bellek (Shared Memory): Tüm aktif süreçlerin bilgisini POSIX Shared Memory kullanarak ortak bir tabloda tutma.

    Süreç İzleme (Monitoring): Ayrı bir thread üzerinden arka planda biten süreçleri otomatik tespit etme ve temizleme.

    Mesaj Kuyrukları (Message Queues): Farklı terminallerde açık olan ProcX örneklerinin birbirlerine "süreç başladı" veya "süreç bitti" bilgisi göndermesi.

    Semaforlar (Semaphores): Paylaşılan belleğe erişimi senkronize ederek veri bozulmasını (race condition) engelleme.

    Akıllı Temizlik: SIGINT (Ctrl+C) sinyali yakalanarak tüm kaynakların (SHM, Semaphore, MQ) sistemden güvenli bir şekilde silinmesi.

🛠 Kullanılan Teknolojiler ve Yapılar

Teknoloji	Kullanım Amacı
POSIX Shared Memory	Süreç listesinin tüm instance'lar tarafından görülmesi.
POSIX Semaphores	Paylaşılan bellek üzerindeki kritik bölgelerin korunması.
System V Message Queues	Instance'lar arası asenkron olay bildirimi.
Pthreads	Arka plan izleme (Monitor) ve mesaj dinleme (Listener) görevleri.
Signal Handling	SIGINT sinyali ile güvenli çıkış yönetimi.

📋 Kurulum ve Çalıştırma

Kodun derlenmesi için pthread ve rt (real-time) kütüphanelerinin bağlanması gerekir:

Derleme:
Bash

     gcc -o procx main.c -lpthread -lrt

Çalıştırma:
Bash

    ./procx

📖 Kullanım Klavuzu

Program açıldığında karşınıza 4 ana seçenekten oluşan bir menü gelir:

    Yeni Program Çalıştır: Çalıştırmak istediğiniz komutu (örn: sleep 100 veya ls -la) girin. Ardından modu seçin:

        0 (Attached): Program bitene kadar menüye dönemezsiniz, Ctrl+C programı kapatır.

        1 (Detached): Program arka planda çalışır, siz menüden başka işlemler yapmaya devam edebilirsiniz.

    Çalışan Programları Listele: Sistem tarafından yönetilen tüm aktif süreçlerin PID, komut, mod ve çalışma süresi bilgilerini tablo halinde gösterir.

    Program Sonlandır: Listede görülen bir PID numarasını girerek ilgili sürece SIGTERM sinyali gönderir.

    Çıkış: Programı kapatır. Eğer kapatılan son ProcX örneği ise, sistemdeki tüm paylaşılan kaynakları temizler.


Çoklu instance test için:

    # Terminal 1
    ./procx

    # Terminal 2
    ./procx

    # Terminal 3
    ./procx

🎮 Kullanım Örneği

    Terminal 1
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


    Terminal 2
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
