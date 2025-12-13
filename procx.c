#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <mqueue.h>
#include <errno.h>
#include <time.h>
#include <signal.h> // Sinyal yonetimi icin

#define MAX_PROCESS 50
#define SHM_NAME "/procx_shm"
#define SEM_NAME "/procx_sem"
#define MESSAGE_QUEUE "/procx_mq"

// Mesaj Komutlari
#define CMD_START 1
#define CMD_STOP 2

// Process Modu (Attached: 0, Detached: 1)
typedef enum {
    MODE_ATTACHED = 0,
    MODE_DETACHED = 1
} ProcessMode;

// Process Durumu (Running: 0, Terminated: 1)
typedef enum {
    STATUS_RUNNING = 0,
    STATUS_TERMINATED = 1
} ProcessStatus;

// Process bilgisi
typedef struct {
    pid_t pid; // Process ID
    pid_t owner_pid; // Başlatan instance'ın PID'si
    char command[256]; // Çalıştırılan komut
    ProcessMode mode; // Attached (0) veya Detached (1)
    ProcessStatus status; // Running (0) veya Terminated (1)
    time_t start_time; // Başlangıç zamanı
    int is_active; // Aktif mi? (1: Evet, 0: Hayır)
} ProcessInfo;

// Paylaşılan bellek yapısı
typedef struct {
    ProcessInfo processes[50]; // Maksimum 50 process
    int process_count; // Aktif process sayısı
} SharedData;

// Mesaj yapısı
typedef struct {
    long msg_type; // Mesaj tipi
    int command; // Komut (START/TERMINATE)
    pid_t sender_pid; // Gönderen PID
    pid_t target_pid; // Hedef process PID
} Message;

// Fonksiyon Prototipleri
void start_process(SharedData *shm, sem_t *sem);
void list_processes(SharedData *shm, sem_t *sem);
void stop_process(SharedData *shm, sem_t *sem);
void clean_resources(SharedData *shm_ptr, sem_t *sem_ptr);
void *monitor_thread(void *arg);
void *listener_thread(void *arg);
int get_input_from_user(char *cmd_buffer, int *mode);
void parse_command(char *cmd, char **args);
void send_message(int command, pid_t target_pid);
void print_menu();
int is_command_valid(const char *cmd);
void sigint_handler(int sig); // Sinyal yakalayici

int main() {
    // Ctrl+C (SIGINT) sinyalini yakala
    signal(SIGINT, sigint_handler);

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(SharedData));
    SharedData *shared_mem_ptr = (SharedData *)mmap(NULL, sizeof(SharedData),PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shared_mem_ptr == MAP_FAILED) {
        perror("mmap hatasi");
        exit(1);
    }
    sem_t *sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);

    sem_wait(sem);
    if (shared_mem_ptr->process_count == 0 && shared_mem_ptr->processes[0].pid == 0) {
        // Shared Memory'nin başlangıçta sıfırlandığından emin ol
        memset(shared_mem_ptr, 0, sizeof(SharedData));
    }
    sem_post(sem);
    if (sem == SEM_FAILED) {
        perror("sem_open hatasi");
        exit(1);
    }

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(Message);
    attr.mq_curmsgs = 0;

    mqd_t mq = mq_open(MESSAGE_QUEUE, O_CREAT | O_RDWR, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open hatasi");
    }

    pthread_t monitor_tid;
    if (pthread_create(&monitor_tid, NULL, monitor_thread, (void *)shared_mem_ptr) != 0) {
        perror("Monitor thread olusturulamadi");
        exit(1);
    }
    pthread_detach(monitor_tid);
    printf("Monitor thread baslatildi...\n");

    pthread_t listener_tid;
    if (pthread_create(&listener_tid, NULL, listener_thread, NULL) != 0) {
        perror("Listener thread olusturulamadi");
        exit(1);
    }
    pthread_detach(listener_tid);
    printf("IPC Listener thread baslatildi...\n");

    int choice;
    while (1) {
        print_menu();

        if (scanf("%d", &choice) != 1) {
            // Sinyal gelirse (Ctrl+C) hata flagini temizle
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            printf("Hatali giris! Lutfen sadece sayi giriniz.\n");
            while (getchar() != '\n');
            continue;
        }

        while (getchar() != '\n');

        switch (choice) {
            case 1:
                start_process(shared_mem_ptr, sem);
                break;
            case 2:
                list_processes(shared_mem_ptr, sem);
                break;
            case 3:
                stop_process(shared_mem_ptr, sem);
                break;
            case 0:
                clean_resources(shared_mem_ptr, sem);
                exit(0);
            default:
                printf("Gecersiz secim! Lutfen tekrar deneyin.\n");
        }
    }
    return 0;
}

// Ctrl+C Handler
void sigint_handler(int sig) {
    printf("\n[INFO] Ctrl+C algilandi. Cikis icin '0' seciniz.\n");
    fflush(stdout);
}

void print_menu() {
    printf("\n");
    printf("╔══════════════════════════════════╗\n");
    printf("║            ProcX v1.0            ║\n");
    printf("╠══════════════════════════════════╣\n");
    printf("║ 1. Yeni Program Calistir         ║\n");
    printf("║ 2. Calisan Programlari Listele   ║\n");
    printf("║ 3. Program Sonlandir             ║\n");
    printf("║ 0. Cikis                         ║\n");
    printf("╚══════════════════════════════════╝\n");
    printf("\nSeciminiz: ");
}

int is_command_valid(const char *cmd) {
    char check_cmd[300];
    char program_name[256];

    if (sscanf(cmd, "%s", program_name) != 1) return 0;

    snprintf(check_cmd, sizeof(check_cmd), "command -v %s > /dev/null 2>&1", program_name);

    if (system(check_cmd) == 0) {
        return 1;
    }
    return 0;
}

int get_input_from_user(char *cmd_buffer, int *mode) {
    printf("Calistirilacak komutu girin: ");

    if (fgets(cmd_buffer, 256, stdin) == NULL) {
        return 0;
    }
    cmd_buffer[strcspn(cmd_buffer, "\n")] = 0; // "sleep 100\0"

    if (strlen(cmd_buffer) == 0) return 0;

    if (!is_command_valid(cmd_buffer)) {
        printf("[HATA] '%s' gecerli bir komut bulunamadi!\n", cmd_buffer);
        return 0;
    }

    printf("Mod secin (0: Attached, 1: Detached): ");
    if (scanf("%d", mode) != 1) {
        while (getchar() != '\n');
        return 0;
    }
    while (getchar() != '\n');

    return 1;
}

void parse_command(char *cmd, char **args) {
    int i = 0;
    char * token = strtok(cmd, " "); //strtok : " " -> \0
    // command : "sleep\0100\0" token : "sleep\0"

    while (token != NULL && i < 16) {
        args[i++] = token;
        token = strtok(NULL, " "); //NULL verince ayni string ustunde kaldigi yerden devam eder.
    }
    args[i] = NULL; //execvp icin NUll terminator
}
void start_process(SharedData * shm, sem_t * sem) {
    char cmd[256];
    char * args[16];
    int mode;
    int status;
    int process_index = -1; // Process'in Shared Memory'deki index'ini tutar

    if (!get_input_from_user(cmd, &mode)) {
        return;
    }

    char cmd_copy[256];
    strcpy(cmd_copy, cmd);

    parse_command(cmd, args);

    if (args[0] == NULL) {
        printf("Hata: Komut girilmedi!\n");
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork hatasi");
        return;
    }

    if (pid == 0) {
        // Child Process
        if (mode == MODE_DETACHED) {
            setsid(); // terminalden ayrilip kendi sessionunu kur arkaplanda calis
        } else {
            // Attached process Ctrl+C ile olebilsin
            signal(SIGINT, SIG_DFL);
        }

        execvp(args[0], args);

        perror("execvp hatasi");
        exit(1);
    }

    // --- PARENT PROCESS ---

    // 1. Shared Memory'yi kilitle ve kayıt yap
    sem_wait(sem);

    for (int i = 0; i < MAX_PROCESS; i++) {
        if (shm->processes[i].is_active == 0) {
            process_index = i; // Boş index bulundu
            break;
        }
    }

    if (process_index != -1) {
        // Shared Memory'ye process bilgilerini yaz
        shm->processes[process_index].pid = pid;
        shm->processes[process_index].owner_pid = getpid();
        strncpy(shm->processes[process_index].command, cmd_copy, 255);
        shm->processes[process_index].command[255] = '\0';
        shm->processes[process_index].mode = (ProcessMode) mode;
        shm->processes[process_index].status = STATUS_RUNNING;
        shm->processes[process_index].start_time = time(NULL);
        shm->processes[process_index].is_active = 1;
        shm->process_count++;

        printf("[SUCCESS] Process baslatildi: PID %d\n", pid);

        // SHM'ye kayıt bitti. SEMAPHORE'u serbest bırak ki diğer Monitor Thread'ler okuyabilsin.
        sem_post(sem);

        // 2. BAŞLATMA BİLDİRİMİ GÖNDER (Artık SHM kilitli değil, IPC anında gidebilir)
        send_message(CMD_START, pid);

        if (mode == MODE_ATTACHED) {

            send_message(CMD_STOP, pid);
            // 3. ATTACHED MOD İÇİN BLOKLAMA
            // Ana thread (menü) burada durur. waitpid aynı zamanda child process'i sistemden temizler.
            waitpid(pid, &status, 0);

            printf("\n[INFO] Attached process sonlandi. Menuye donuluyor...\n");
            sem_wait(sem);
            for (int i = 0; i < MAX_PROCESS; i++) {
                if (shm->processes[i].pid == pid && shm->processes[i].is_active) {
                    shm->processes[i].is_active = 0;  // ← İlk iş bunu yap
                    if (shm->process_count > 0) shm->process_count--;
                    break;
                }
            }

            sem_post(sem);
            // 4. SONLANMA BİLDİRİMİ GÖNDER



            return; // Fonksiyonu burada sonlandırır, alttaki sem_post'u atlar.
        }

    } else {
        printf("[ERROR] Process listesi dolu! (Max 50)\n");
        kill(pid, SIGTERM);
        // Hata durumunda semaphore kilitli kaldı, açmalıyız.
        sem_post(sem);
    }

    // Detached modda (veya Attached değilse) buraya ulaşılır ve Semaphore zaten yukarıda (if'in hemen üstünde) serbest bırakılmıştır.
}

void list_processes(SharedData *shm, sem_t *sem) {
    sem_wait(sem);

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                          ÇALIŞAN PROGRAMLAR                               ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════════════╣\n");
    printf("║ %-8s │ %-20s │ %-10s │ %-8s │ %-16s ║\n",
           "PID", "Command", "Mode", "Owner", "Süre");
    printf("╠═══════════════════════════════════════════════════════════════════════════╣\n");

    int active = 0;
    time_t now = time(NULL);

    for (int i = 0; i < MAX_PROCESS; i++) {
        if (shm->processes[i].is_active==1) {
            double duration = difftime(now, shm->processes[i].start_time);

            printf("║ %-8d │ %-20s │ %-10s │ %-8d │ %-12.0fs   ║\n",
                   shm->processes[i].pid,
                   shm->processes[i].command,
                   (shm->processes[i].mode == MODE_DETACHED) ? "Detached" : "Attached",
                   shm->processes[i].owner_pid,
                   duration);

            active++;
        }
    }

    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n");
    printf("Toplam: %d process\n", active);

    sem_post(sem);
}

void stop_process(SharedData *shm, sem_t * sem) {
    pid_t target_pid;

    printf("Sonlandirilacak process PID: ");
    if (scanf("%d", &target_pid) != 1) {
        printf("[ERROR] Gecersiz PID girdisi.\n");
        while(getchar() != '\n');
        return;
    }
    while (getchar() != '\n');

    sem_wait(sem);

    int search = -1;
    for (int i = 0; i < MAX_PROCESS; i++) { // sonlandirilacak process indexi araniyo
        if (shm->processes[i].is_active && shm->processes[i].pid == target_pid) {
            search = i;
            break;
        }
    }

    if (search != -1) { //eger bulunduysa
        if (kill(target_pid, SIGTERM) == 0) {
            printf("[INFO] Process %d'e SIGTERM sinyali gonderildi.\n", target_pid);
            shm->processes[search].status = STATUS_TERMINATED; // Monitor için işaretle waitpid ile surec gercekten sonlanir bu bir flag gibi

            send_message(CMD_STOP, target_pid);

        } else if (errno == ESRCH) {
            printf("[INFO] Process %d zaten sonlanmis veya bulunamiyor. Listeden kaldiriliyor.\n", target_pid);
            shm->processes[search].is_active = 0;
            if (shm->process_count > 0) shm->process_count--;
        }
        else {
            perror("Sinyal gonderilemedi");
        }
    } else {
        printf("[ERROR] PID %d listede bulunamadi!\n", target_pid);
    }

    sem_post(sem);
}

#include <errno.h> // errno kullanmak için bu kütüphaneyi eklediğinizden emin olun

void *monitor_thread(void *arg) {
    SharedData *shm = (SharedData *)arg;
    int status;
    pid_t my_pid = getpid(); // Monitor Thread'in ait olduğu ProcX instance'ının PID'si

    sem_t *sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("Monitor thread sem_open hatasi");
        return NULL;
    }

    while (1) {
        sleep(2);
        sem_wait(sem);

        for (int i = 0; i < MAX_PROCESS; i++) {
            if (shm->processes[i].is_active) {

                // Attached process'ler Ana Thread tarafından yönetilmelidir (veya Monitor Thread'in
                // Attached'ı temizlemesi gerekmez, çünkü Ana Thread onu bekler).
                if (shm->processes[i].mode == MODE_ATTACHED) {
                    continue;
                }

                // Sadece DETACHED process'leri kontrol et
                pid_t result = waitpid(shm->processes[i].pid, &status, WNOHANG);

                if (result > 0) {
                    // 1. Durum: Process başarıyla sonlandı.
                    printf("\n[MONITOR] Detached Process %d sonlandi.\n", result);
                    send_message(CMD_STOP, result);

                    // Temizle
                    shm->processes[i].is_active = 0;
                    if (shm->process_count > 0) shm->process_count--;

                } else if (result == -1 && errno == ECHILD) {
                    // 2. Durum: Process zaten sistemden temizlenmiş (ECHILD)

                    // KRİTİK KONTROL: Yalnızca sahibi BİZ isek temizleme yap.
                    if (shm->processes[i].owner_pid == my_pid) {
                         // Bu process'i ben başlattım. ECHILD almam, process'in bittiği veya
                         // Detached kuralı gereği parent'ın değiştiği anlamına gelir. Güvenle temizleyebiliriz.

                         printf("\n[MONITOR] Kendi baslattigim Detached Process %d sistemden kaybolmus (ECHILD), listeden temizleniyor.\n", shm->processes[i].pid);

                         shm->processes[i].is_active = 0;
                         if (shm->process_count > 0) shm->process_count--;

                    }
                    // Eğer process'i BAŞKASI başlattıysa, ECHILD almamız normaldir.
                    // Process hala çalışıyor olabilir. SHM'ye DOKUNMA!

                }
                // result == 0 ise: Process hala çalışıyor, listelenmeye devam etmeli.
            }
        }
        sem_post(sem);
    }
    return NULL;
}

void send_message(int command, pid_t target_pid) {
    mqd_t mq = mq_open(MESSAGE_QUEUE, O_WRONLY);
    if (mq == (mqd_t)-1) return;

    Message msg;
    msg.command = command;
    msg.sender_pid = getpid();
    msg.target_pid = target_pid;

    mq_send(mq, (const char *)&msg, sizeof(Message), 0);
    mq_close(mq);
}



// DUZELTILMIS VE GEREKSINIMLERE UYGUN TEMIZLIK FONKSIYONU
void clean_resources(SharedData *shm_ptr, sem_t *sem_ptr) {
    printf("\n[CLEANUP] Cikis baslatiliyor. Kaynaklar temizleniyor...\n");

    // BU INSTANCE TARAFINDAN BASLATILAN 'ATTACHED' PROCESSLERI OLDUR
    pid_t my_pid = getpid();

    // Temizlik yaparken karisiklik olmasin diye semaphore ile bekle
    sem_wait(sem_ptr);

    for(int i = 0; i < MAX_PROCESS; i++) {
        if(shm_ptr->processes[i].is_active && shm_ptr->processes[i].owner_pid == my_pid) {

            // SADECE ATTACHED OLANLARI OLDUR (Gereksinim 6.6)
            if(shm_ptr->processes[i].mode == MODE_ATTACHED) {
                printf("[CLEANUP] Attached process %d sonlandiriliyor...\n", shm_ptr->processes[i].pid);
                kill(shm_ptr->processes[i].pid, SIGTERM);
            }
            // DETACHED OLANLARA DOKUNMA (Gereksinim 6.6: "calismaya devam etmeli")
            else {
                printf("[CLEANUP] Detached process %d arka planda calismaya devam ediyor.\n", shm_ptr->processes[i].pid);
            }
        }
    }

    sem_post(sem_ptr);

    if (munmap(shm_ptr, sizeof(SharedData)) != 0) {
        perror("[CLEANUP] munmap hatasi");
    }

    if (shm_unlink(SHM_NAME) != 0) { perror("[CLEANUP] shm_unlink hatasi"); }
    if (sem_unlink(SEM_NAME) != 0) { perror("[CLEANUP] sem_unlink hatasi"); }

    // mq_unlink() fonksiyonu hata verirse de program devam etmeli (kuyruk zaten silinmis olabilir)
    if (mq_unlink(MESSAGE_QUEUE) != 0) {
        if (errno != ENOENT) { // ENOENT: Dosya yok hatasi (Normal)
            perror("[CLEANUP] mq_unlink hatasi");
        }
    }

    printf("[CLEANUP] ProcX basariyla sonlandi.\n");
}


// Argüman olarak NULL beklediği için SharedData'ya global isimlerle erişir.
void *listener_thread(void *arg) {
    // SharedData ve Semaphore'a global isimleriyle erişim sağlanmalı
    sem_t *sem = sem_open(SEM_NAME, 0);
    mqd_t mq;
    pid_t my_pid = getpid(); // Kendi PID'mizi alalım

    if (sem == SEM_FAILED) {
        perror("Listener thread sem_open hatasi");
        return NULL;
    }

    // Mesaj kuyruğunu sadece okuma modunda aç
    mq = mq_open(MESSAGE_QUEUE, O_RDONLY);
    if (mq == (mqd_t)-1) {
        perror("Listener thread mq_open hatasi");
        sem_close(sem);
        return NULL;
    }

    printf("IPC Listener thread dinlemeye basladi. PID: %d\n", my_pid);

    Message msg;
    unsigned int priority;

    // Mesaj alımı için döngü
    while (1) {
        // Mesajı bekle ve al
        if (mq_receive(mq, (char *)&msg, sizeof(Message), &priority) == -1) {
            // Eğer thread sinyal (örn: Ctrl+C) nedeniyle kesintiye uğrarsa
            if (errno != EINTR) {
                perror("mq_receive hatasi");
            }
            continue;
        }

        // --- Mesaj İşleme ---

        // GEREKSİNİM 6.5: Kendi gönderdiği mesajları atla
        if (msg.sender_pid == my_pid) {
            // Kendi gönderdiğimiz mesajı görmezden gel, dinlemeye devam et.
            continue;
        }

        // --- Başka Bir Instance'tan Gelen Mesaj ---

        if (msg.command == CMD_START) {
            // Başka bir instance process başlattı
            printf("\n[IPC BİLDİRİMİ] Process BAŞLATILDI: PID %d (Gönderen: %d)\n",
                   msg.target_pid, msg.sender_pid);
        } else if (msg.command == CMD_STOP) {
            // Başka bir instance process sonlandırdı veya Monitor Thread temizledi
            printf("\n[IPC BİLDİRİMİ] Process SONLANDIRILDI: PID %d (Gönderen: %d)\n",
                   msg.target_pid, msg.sender_pid);
        } else {
             // Bilinmeyen komut bildirimi
             printf("\n[IPC BİLDİRİMİ] Bilinmeyen komut alindi: %d (Hedef PID %d)\n",
                   msg.command, msg.target_pid);
        }

        // Ana döngü kesintiye uğramamalı, menüyü tekrar çiz
        print_menu();
        fflush(stdout); // Çıktıyı hemen terminale yazdır
    }

    // Temizlik (Thread sonlandığında)
    mq_close(mq);
    sem_close(sem);
    return NULL;
}