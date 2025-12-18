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
