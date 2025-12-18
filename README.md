🧠 ProcX – Multi-Instance Process Manager (IPC Based)

Bu proje, birden fazla terminal (instance) üzerinden çalışan, process yönetimi, inter-process communication (IPC) ve eşzamanlılık (concurrency) kavramlarını kullanan bir işletim sistemi dönem projesidir.

Amaç; Linux ortamında çalışan process’leri başlatmak, listelemek, sonlandırmak, bu işlemleri farklı terminaller arasında senkronize etmek ve zombie / race condition gibi problemlerin önüne geçmektir.

🚀 Özellikler

Çoklu terminal (multi-instance) desteği

Shared Memory (POSIX SHM) ile ortak process tablosu

POSIX Semaphore ile race condition önleme

System V Message Queue ile instance’lar arası bildirim

Attached & Detached process yönetimi

Zombie process önleme (monitor thread)

Ctrl+C ile temiz çıkış (graceful shutdown)

Thread tabanlı mimari (monitor + listener)

🧩 Kullanılan Teknolojiler
Bileşen	Amaç
shm_open + mmap	Paylaşılan process tablosu
sem_open / sem_wait / sem_post	Kritik bölge koruması
msgget / msgsnd / msgrcv	Instance’lar arası IPC
pthread	Monitor & Listener thread’leri
fork / execvp	Process başlatma
waitpid(WNOHANG)	Zombie önleme
signal(SIGINT)	Ctrl+C yakalama
🗂 Mimari Genel Bakış
+----------------------+
| Terminal / Instance  |
|----------------------|
|  Main Thread         |
|   ├─ Menu            |
|   ├─ Start Process   |
|   ├─ Stop Process    |
|   └─ List Processes  |
|                      |
|  Monitor Thread      |--> Detached process cleanup
|  Listener Thread     |--> IPC message listener
+----------------------+

        │
        ▼
+----------------------------------+
| Shared Memory (Process Table)    |
|  - processes[]                   |
|  - process_count                 |
|  - active_procx                  |
+----------------------------------+
▲
│
+----------------------------------+
| Semaphore (SEM_NAME)             |
|  - Race condition önleme         |
+----------------------------------+

        │
        ▼
+----------------------------------+
| Message Queue (System V)         |
|  - START / STOP bildirimleri     |
+----------------------------------+

🧪 Process Türleri
🔹 Attached Process

Terminale bağlı çalışır

Ctrl+C ile sonlanabilir

waitpid() ile beklenir

Bittiğinde anında listeden kaldırılır

🔹 Detached Process

Arka planda çalışır (setsid())

Terminal kapansa bile devam eder

Monitor thread tarafından takip edilir

Bittiğinde otomatik temizlenir

🧵 Thread’ler Ne İş Yapar?
🛠 Monitor Thread

Sadece DETACHED process’leri izler

waitpid(WNOHANG) ile zombie önler

kill(pid, 0) ile sistemde var mı kontrol eder

Sonlanan process’i:

STATUS_TERMINATED

is_active = 0

IPC ile diğer instance’lara bildirir

📡 Listener Thread

Message Queue’yu dinler

Diğer terminalde:

process başladı

process sonlandı
bildirimlerini ekrana basar

IPC_NOWAIT kullanır → asla bloklamaz

🔐 Neden Semaphore Kullanıldı?

Shared Memory tüm instance’lar tarafından aynı anda erişilebilir.

Aşağıdaki işlemler kritik bölgedir:

process ekleme

process silme

sayaç güncelleme (process_count, active_procx)

Bu yüzden:

sem_wait(sem);
// shared memory erişimi
sem_post(sem);


kullanılarak race condition engellenmiştir.

📩 Neden IPC_NOWAIT?
msgsnd(msgid, &msg, msg_size, IPC_NOWAIT);


Çünkü:

Menü akarken bloklanmamalı

Monitor thread semafor tutarken kilitlenmemeli

Ctrl+C sırasında program takılmamalı

Mesajlar bildirim amaçlıdır, kritik veri shared memory’dedir.
Bu yüzden kuyruk doluysa mesajın düşmesi kabul edilebilir.

☠️ Zombie Process Nasıl Önleniyor?

Attached process → waitpid(pid, 0)

Detached process → monitor_thread:

waitpid(pid, WNOHANG)

ECHILD ve ESRCH kontrolü

Otomatik cleanup

🧹 Temiz Çıkış (Cleanup)

Ctrl+C veya 0 - Exit seçildiğinde:

Thread’lere çık sinyali verilir

Monitor & Listener thread’ler iptal edilir (pthread_cancel + join)

Bu instance’ın başlattığı attached process’ler sonlandırılır

active_procx azaltılır

Son instance ise:

shared memory silinir

semaphore unlink edilir

message queue kaldırılır

▶️ Derleme & Çalıştırma
gcc procx.c -o procx -pthread
./procx


Birden fazla terminalde çalıştırarak IPC davranışı gözlemlenebilir.

🎯 Projenin Kazandırdıkları

IPC mekanizmalarının gerçek kullanımını anlama

Race condition & deadlock farkındalığı

Zombie process kavramının pratik çözümü

Thread + process + IPC birlikte kullanımı

Gerçek OS seviyesinde senkronizasyon deneyimi