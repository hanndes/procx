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
#include <sys/msg.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#define MAX_PROCESS 50
#define SHM_NAME "/procx_shm"
#define SEM_NAME "/procx_sem"
#define MESSAGE_QUEUE "/procx_mq"

// Mesaj Komutlari
#define CMD_START 1
#define CMD_STOP 2
#define SHM_MAGIC 0xC0FFEE42
#define MSG_KEY 07032

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
    int magic;
    ProcessInfo processes[50]; // Maksimum 50 process
    int process_count; // Aktif process sayısı
    int active_procx;
} SharedData;

// Mesaj yapısı
typedef struct {
    long msg_type; // Mesaj tipi
    int command; // Komut (START/TERMINATE)
    pid_t sender_pid; // Gönderen PID
    pid_t target_pid; // Hedef process PID
} Message;

int msgid=-1;
static volatile sig_atomic_t g_exit_requested = 0;

static SharedData *g_shm = NULL;
static sem_t *g_sem = NULL;
pthread_t monitor_tid;
pthread_t listener_tid;
int g_monitor_started=0;
int g_listener_started=0;


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
void install_sigint(void);

int main() {
    install_sigint();

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open"); exit(1);
    }

    if (ftruncate(shm_fd, sizeof(SharedData)) == -1) {
        perror("ftruncate"); exit(1);
    }

    SharedData *shared_mem_ptr = (SharedData *)mmap(NULL, sizeof(SharedData),PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shared_mem_ptr == MAP_FAILED) {
        perror("mmap hatasi");
        exit(1);
    }

    g_shm = shared_mem_ptr;
    sem_t *sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open hatasi");
        exit(1);
    }

    g_sem = sem;
    sem_wait(sem);

    // sadece ilk defa init
    if (shared_mem_ptr->magic != SHM_MAGIC) {
        memset(shared_mem_ptr, 0, sizeof(SharedData));
        shared_mem_ptr->magic = SHM_MAGIC;
    }

    // her açılan terminal sayacı artırır
    shared_mem_ptr->active_procx++;

    printf("[INFO] Aktif instance sayisi: %d\n", shared_mem_ptr->active_procx);

    sem_post(sem);

    msgid = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msgid == -1) { perror("msgget"); exit(1); }


    if (pthread_create(&monitor_tid, NULL, monitor_thread, (void *)shared_mem_ptr) != 0) {
        perror("Monitor thread olusturulamadi");
        exit(1);
    }
    g_monitor_started = 1;
    printf("Monitor thread baslatildi...\n");

    if (pthread_create(&listener_tid, NULL, listener_thread, NULL) != 0) {
        perror("Listener thread olusturulamadi");
        exit(1);
    }
    g_listener_started = 1;
    printf("IPC Listener thread baslatildi...\n");

    int choice;
    while (1) {
        if (g_exit_requested) {
            printf("\n[INFO] Ctrl+C algilandi. Temiz cikis yapiliyor...\n");
            clean_resources(g_shm, g_sem);
            _exit(0); // exit yerine _exit: daha “sert” ama güvenli
        }
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
    (void)sig;
    g_exit_requested = 1;
}

void install_sigint(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;              // <<< SA_RESTART YOK

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
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

    if (pid == 0) { // Child Process
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
//bos yer bulup processi ekle --critical section--
    sem_wait(sem);

    for (int i = 0; i < MAX_PROCESS; i++) {
        if (shm->processes[i].is_active == 0) {
            process_index = i; // Boş index bulundu
            break;
        }
    }

    if (process_index != -1) {
        // shared memorye processi yaz
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

        // semaphor bırak diger threadler erissin
        sem_post(sem);

        send_message(CMD_START, pid);

        if (mode == MODE_ATTACHED) {
            waitpid(pid, &status, 0);
            printf("\n[INFO] Attached process sonlandi. Menuye donuluyor...\n");
            sem_wait(sem);
            shm->processes[process_index].is_active = 0;
            shm->process_count--;
            shm->processes[process_index].status= STATUS_TERMINATED;
            sem_post(sem);
            send_message(CMD_STOP,pid);
            sleep(5);
            return; // Fonksiyonu burada sonlandırır, alttaki sem_post'u atlar.
        }

    } else {
        printf("[ERROR] Process listesi dolu! (Max 50)\n");
        kill(pid, SIGTERM);
        // hata durumunda semaphore kilitli kaldi acmaliyiz
        sem_post(sem);
    }
}

void list_processes(SharedData *shm, sem_t *sem) {
    sem_wait(sem);

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                          ÇALIŞAN PROGRAMLAR                              ║\n");
    printf("╠══════════════════════════════════════════════════════════════════════════╣\n");
    printf("║ %-8s │ %-19s │ %-10s │ %-8s │ %-16s ║\n",           "PID", "Command", "Mode", "Owner", "Süre");
    printf("╠══════════════════════════════════════════════════════════════════════════╣\n");

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

    printf("╚══════════════════════════════════════════════════════════════════════════╝\n");
    printf("Toplam: %d process\n", active);

    sem_post(sem);
}

void stop_process(SharedData *shm, sem_t *sem) {
    pid_t target_pid;

    printf("Sonlandirilacak process PID: ");
    fflush(stdout);

    errno = 0; // eski errno kalmasın

    if (scanf("%d", &target_pid) != 1) {

        // Ctrl+C burada yakalanır (scanf EINTR ile bölünürse)
        if (errno == EINTR || g_exit_requested) {
            clearerr(stdin);
            printf("\n[INFO] Ctrl+C algilandi. Temiz cikis...\n");
            clean_resources(g_shm, g_sem);
            exit(0);
        }

        // Ctrl+C değilse: gerçekten hatalı giriş
        printf("[ERROR] Gecersiz PID girdisi.\n");
        clearerr(stdin);
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {}
        return;
    }

    // input satırı sonunu temizle
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}

    // (Opsiyonel) Ctrl+C flag'i burada da kontrol edebilirsin
    if (g_exit_requested) {
        printf("\n[INFO] Ctrl+C algilandi. Temiz cikis...\n");
        clean_resources(g_shm, g_sem);
        exit(0);
    }

    sem_wait(sem);

    int search = -1;
    for (int i = 0; i < MAX_PROCESS; i++) {
        if (shm->processes[i].is_active && shm->processes[i].pid == target_pid) {
            search = i;
            break;
        }
    }

    if (search != -1) {
        if (kill(target_pid, SIGTERM) == 0) {
            printf("[INFO] Process %d'e SIGTERM sinyali gonderildi.\n", target_pid);
            shm->processes[search].status = STATUS_TERMINATED;
            // İstersen burada send_message(CMD_STOP, target_pid) yaparsın (ama duplicate olmasın diye tek yerde)
        } else if (errno == ESRCH) {
            printf("[INFO] Process %d zaten sonlanmis veya bulunamiyor. Listeden kaldiriliyor.\n", target_pid);
            shm->processes[search].is_active = 0;
            if (shm->process_count > 0) shm->process_count--;
        } else {
            perror("Sinyal gonderilemedi");
        }
    } else {
        printf("[ERROR] PID %d listede bulunamadi!\n", target_pid);
    }

    sem_post(sem);
}


void *monitor_thread(void *arg) {
    SharedData *shm = (SharedData *)arg;
    int status;
    pid_t my_pid = getpid();

    sem_t *sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("Monitor thread sem_open hatasi");
        return NULL;
    }

    while (!g_exit_requested) {
        sleep(2);
        sem_wait(sem);
        if (g_exit_requested) break;

        for (int i = 0; i < MAX_PROCESS; i++) {
            if (shm->processes[i].is_active && shm->processes[i].mode == MODE_DETACHED) {

                pid_t process_pid = shm->processes[i].pid;

                // 1. waitpid ile kendi çocuklarımızı kontrol et ve reaped et
                pid_t result = waitpid(process_pid, &status, WNOHANG);

                int perform_cleanup = 0;

                if (result > 0) {
                    // Kendi çocuğumuz sonlandı. Temizle ve bildirim gönder.
                    printf("\n[MONITOR] Detached Process %d sonlandi.\n", result);
                    send_message(CMD_STOP, result);
                    perform_cleanup = 1;

                } else if (result == -1 && errno == ECHILD) {
                    // ECHILD alındı. Eğer sahibi BİZ isek, reaped edildiğini kabul edip temizle.
                    if (shm->processes[i].owner_pid == my_pid) {
                         printf("\n[MONITOR] Kendi baslattigim Detached Process %d sistemden kaybolmus (ECHILD), listeden temizleniyor.\n", process_pid);
                         perform_cleanup = 1;
                    }
                }

                // 2. EVRENSEL KONTROL: Eğer yukarıdaki durumlarda temizlenmediyse (özellikle başkasınınkiler için)
                if (!perform_cleanup) {
                    if (kill(process_pid, 0) == -1 && errno == ESRCH) {
                        // Süreç sistemde yok (ESRCH). Kalıntıdır, temizlenmeli.
                        printf("\n[MONITOR] Kalinti Process %d (Owner: %d) sistemde bulunamadi, temizleniyor.\n",
                               process_pid, shm->processes[i].owner_pid);
                        send_message(CMD_STOP, process_pid); // Temizlik öncesi sonlandırma bildirimi
                        perform_cleanup = 1;
                    }
                }

                // 3. TEMİZLEME İŞLEMİ
                if (perform_cleanup) {
                    shm->processes[i].is_active = 0;
                    if (shm->process_count > 0) shm->process_count--;
                }
            }
        }
        sem_post(sem);
    }
    return NULL;
}

void send_message(int command, pid_t target_pid) {
    if (msgid == -1) return; // Kuyruk id yoksa çık

    Message msg;
    // msg_type 1 olmalıdır genel iletisim tipi
    msg.msg_type = 1;
    msg.command = command;
    msg.sender_pid = getpid();
    msg.target_pid = target_pid;

    // mesajın sadece veri kısmının boyutunu hesapla (msg_type hariç)
    size_t msg_size = sizeof(Message) - sizeof(long);

    // msgsnd kullan (kuyruk ID, mesaj yapısı, veri boyutu, bayraklar)
    if (msgsnd(msgid, &msg, msg_size, IPC_NOWAIT) == -1) {
        if (errno != EAGAIN) {
            perror("msgsnd hatasi");
        } else {
            // IPC_NOWAIT (Non-blocking) olduğu için kuyruk doluysa EAGAIN döner.
            printf("[INFO] Message queue dolu, mesaj gonderilemedi.\n");
        }
    }
}

void clean_resources(SharedData *shm_ptr, sem_t *sem_ptr) {
    printf("\n[CLEANUP] Cikis baslatiliyor...\n");

    // 0) Thread'lere "çık" sinyali
    g_exit_requested = 1;

    // 0.1) Bloklanan threadleri uyandır / kestir
    if (g_listener_started) {
        pthread_cancel(listener_tid);   // msgrcv cancellation point
    }
    if (g_monitor_started) {
        pthread_cancel(monitor_tid);    // sleep cancellation point
    }

    // 0.2) Join ile gerçekten kapandıklarını bekle (detach yok!)
    if (g_listener_started) pthread_join(listener_tid, NULL);
    if (g_monitor_started)  pthread_join(monitor_tid, NULL);

    pid_t my_pid = getpid();
    int last_instance = 0;

    // 1) Kritik bölüm: SHM + sayaç güncellemesi semaphore ile korunmalı
    sem_wait(sem_ptr);

    // Bu instance'ın başlattığı ATTACHED processleri öldür
    for (int i = 0; i < MAX_PROCESS; i++) {
        if (shm_ptr->processes[i].is_active &&
            shm_ptr->processes[i].owner_pid == my_pid) {

            if (shm_ptr->processes[i].mode == MODE_ATTACHED) {
                printf("[CLEANUP] Attached process %d sonlandiriliyor...\n",
                       shm_ptr->processes[i].pid);
                kill(shm_ptr->processes[i].pid, SIGTERM);
            } else {
                printf("[CLEANUP] Detached process %d calismaya devam ediyor.\n",
                       shm_ptr->processes[i].pid);
            }
        }
    }

    // 2) Instance sayacini azalt
    if (shm_ptr->active_procx > 0) shm_ptr->active_procx--;
    last_instance = (shm_ptr->active_procx == 0);

    printf("[CLEANUP] Kalan instance sayisi: %d\n", shm_ptr->active_procx);

    sem_post(sem_ptr);

    // 3) Bu instance kendi map’ini kapatsın
    if (munmap(shm_ptr, sizeof(SharedData)) != 0) {
        perror("[CLEANUP] munmap hatasi");
    }

    // Bu instance semaphore handle'ini kapatsın
    sem_close(sem_ptr);

    // 4) Sadece son instance global kaynaklari silsin
    if (last_instance) {
        printf("[CLEANUP] Son instance. Global kaynaklar siliniyor...\n");

        if (shm_unlink(SHM_NAME) != 0 && errno != ENOENT)
            perror("[CLEANUP] shm_unlink");

        if (sem_unlink(SEM_NAME) != 0 && errno != ENOENT)
            perror("[CLEANUP] sem_unlink");

        if (msgid != -1) {
            if (msgctl(msgid, IPC_RMID, NULL) == -1)
                perror("[CLEANUP] msgctl IPC_RMID");
        }
    } else {
        printf("[CLEANUP] Diger instance'lar calisiyor. Kaynaklar korunuyor.\n");
    }

    printf("[CLEANUP] ProcX kapandi.\n");
}

void *listener_thread(void *arg) {
    (void)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    Message msg;
    pid_t my_pid = getpid();
    size_t msg_size = sizeof(Message) - sizeof(long);

    while (!g_exit_requested) {
        ssize_t r = msgrcv(msgid, &msg, msg_size, 0, IPC_NOWAIT);

        if (r == -1) {
            if (errno == ENOMSG) {
                usleep(50 * 1000); // 50ms
                continue;
            }
            if (errno == EINTR) continue;
            perror("msgrcv");
            usleep(200 * 1000);
            msgsnd(msgid, &msg, msg_size, 0);

            continue;
        }

        if (msg.sender_pid == my_pid) continue;

        printf("\r\033[K");
        if (msg.command == CMD_START)
            printf("[IPC] Process BASLATILDI: PID %d (Gonderen: %d)\n",
                   msg.target_pid, msg.sender_pid);
        else if (msg.command == CMD_STOP)
            printf("[IPC] Process SONLANDIRILDI: PID %d (Gonderen: %d)\n",
                   msg.target_pid, msg.sender_pid);

        printf("Seciminiz: ");
        fflush(stdout);
    }

    return NULL;
}
