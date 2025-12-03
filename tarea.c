#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>   
#include <stdint.h>
#include <stdbool.h>

#define MAX_PROCESSES 200
#define MAX_PAGES_PER_PROCESS 1000
#define MAX_FRAMES 100000
#define MAX_SWAP_SLOTS 100000

typedef struct {
    bool present;       // esta en RAM?
    int frame;          // número de frame si está en RAM  
    bool in_swap;       // esta en swap?
    int swap_index;     // posicion dentro del swap
} PageEntry;

typedef struct {
    int pid;
    size_t size_bytes;  // tamaño total del proceso
    int num_pages;      // páginas que necesita
    PageEntry *pages;   // arreglo de PageEntry
    bool alive;
} Process;

typedef struct {
    int pid;    // pid dueño 
    int vpn;    // virtual page number
} Frame;

typedef struct {
    int pid;   // pid en swap 
    int vpn;   // virtual page number
} SwapSlot;

Process *processes[MAX_PROCESSES];
int process_count = 0;
int next_pid = 1;

Frame *frames = NULL;
int num_frames = 0;

SwapSlot *swap_slots = NULL;
int num_swap_slots = 0;

int *fifo_queue = NULL; // FIFO para ver que pagina expulsar
int fifo_head = 0;
int fifo_tail = 0;
int fifo_size = 0;
int fifo_capacity = 0;

int free_frames_count = 0;
int free_swap_count = 0;

long now_seconds() {
    return time(NULL);
}

// si la pagina entra en RAM, se agrega a la cola del frame
void enqueue_frame_fifo(int frame_index) {      
    if (fifo_size == fifo_capacity) {
        return;
    }
    fifo_queue[fifo_tail] = frame_index;
    fifo_tail = (fifo_tail + 1) % fifo_capacity;
    fifo_size++;
}

// lo elimino de la cola del frame si necesito expulsar pagina
int dequeue_frame_fifo() {
    if (fifo_size == 0) return -1;
    int idx = fifo_queue[fifo_head];
    fifo_head = (fifo_head + 1) % fifo_capacity;
    fifo_size--;
    return idx;
}

// busco el primer frame libre en la RAM
int find_free_frame() {
    for (int i = 0; i < num_frames; ++i) {
        if (frames[i].pid == -1) return i;
    }
    return -1;
}

// Busco un espacio vacio en el swap
int find_free_swap_slot() {
    for (int i = 0; i < num_swap_slots; ++i) {
        if (swap_slots[i].pid == -1) return i;
    }
    return -1;
}

// al terminar un proceso, reconstruyo la cola eliminando la referencia
void remove_frame_from_fifo(int frame_index) {
    if (fifo_size == 0) return;
    int newq[fifo_capacity];
    int ni = 0;
    for (int i = 0, pos = fifo_head; i < fifo_size; ++i, pos = (pos+1)%fifo_capacity) {
        int f = fifo_queue[pos];
        if (f != frame_index) {
            newq[ni++] = f;
        }
    }
    fifo_head = 0; fifo_tail = 0; fifo_size = 0;
    for (int i = 0; i < ni; ++i) {
        fifo_queue[fifo_tail++] = newq[i];
        fifo_size++;
    }
}

// expulsa una pagina usando la política FIFO
int evict_page_fifo() {
    int frame_index = dequeue_frame_fifo(); // tomo el primer frame en entrar (FIFO)
    if (frame_index < 0) return -1;
    Frame *f = &frames[frame_index]; // puntero en el frame real dentro del array
    if (f->pid == -1) return frame_index; 

    // obtengo el PID y el VPN del frame
    int pid = f->pid;
    int vpn = f->vpn;

    // buscamos el proceso dueño del frame
    for (int i = 0; i < process_count; ++i) {
        Process *p = processes[i];
        if (p && p->pid == pid) {
            if (vpn >= 0 && vpn < p->num_pages) {
                // muevo la pagina al swap
                int swap_idx = find_free_swap_slot();
                if (swap_idx == -1) {
                    return -1;
                }

                //guarod en el swap quien el dueño y que pagina es
                swap_slots[swap_idx].pid = pid;
                swap_slots[swap_idx].vpn = vpn;

                // se actualiza la page table
                p->pages[vpn].present = false;
                p->pages[vpn].in_swap = true;
                p->pages[vpn].swap_index = swap_idx;
                
                // se marca el frame fisico como libre
                frames[frame_index].pid = -1;
                frames[frame_index].vpn = -1;
                free_frames_count++;
                free_swap_count--;
                
                printf("[EVICT FIFO] Se expulsó el frame %d (P%d:página %d) hacia el slot de swap %d\n", frame_index, pid, vpn, swap_idx);
                return frame_index;
            }
        }
    }
    frames[frame_index].pid = -1;
    frames[frame_index].vpn = -1;
    free_frames_count++;
    printf("[EVICT FIFO] Frame %d liberado (el proceso dueño ya no existe)\n", frame_index);
    return frame_index;
}

// traigo una pagina del swap a la RAM
int bring_page_into_ram(int pid, int vpn) {
    
    //busco el proceso
    Process *p = NULL;
    for (int i = 0; i < process_count; ++i) {
        if (processes[i] && processes[i]->pid == pid) { p = processes[i]; break; }
    }
    if (!p) return -1;

    // si no hay frames libres, expulsko uno por FIFO
    int frame_index = find_free_frame();
    if (frame_index == -1) {
        frame_index = evict_page_fifo();
        if (frame_index == -1) return -1;
    } else {
        free_frames_count--;
    }
    // si la pagina estaba en el swap, libero el espacio en el
    if (p->pages[vpn].in_swap) {
        int sidx = p->pages[vpn].swap_index;
        if (sidx >= 0 && sidx < num_swap_slots && swap_slots[sidx].pid == pid && swap_slots[sidx].vpn == vpn) {
            swap_slots[sidx].pid = -1;
            swap_slots[sidx].vpn = -1;
            free_swap_count++;
        }
        p->pages[vpn].in_swap = false;
        p->pages[vpn].swap_index = -1;
    }

    // coloco la pagina en el frame libre
    frames[frame_index].pid = pid;
    frames[frame_index].vpn = vpn;
    p->pages[vpn].present = true;
    p->pages[vpn].frame = frame_index;
    enqueue_frame_fifo(frame_index);
    return frame_index;
}

// asigno las paginas de un nuevo proceso a la memoria RAM si hay espacio, y al swap si no hay suficiente RAM
bool allocate_process_pages(Process *p) {
    for (int v = 0; v < p->num_pages; ++v) {
        int f = find_free_frame();
        if (f != -1) {
            // se colola la pagina en RAM
            frames[f].pid = p->pid;
            frames[f].vpn = v;
            p->pages[v].present = true;
            p->pages[v].frame = f;
            p->pages[v].in_swap = false;
            p->pages[v].swap_index = -1;
            enqueue_frame_fifo(f);
            free_frames_count--;
        } else {
            // si no hay RAM libre, se busca un espacio libre en el swap
            int s = find_free_swap_slot();
            if (s == -1) {
                // si tampoco hat swap disponible, se libera cualquier frame o espacio del swap que ya habiamos asignado a este proceso en esta funcion
                for (int u = 0; u <= v; ++u) {
                    if (p->pages[u].present) {
                        int fr = p->pages[u].frame;
                        if (fr >= 0) {
                            frames[fr].pid = -1;
                            frames[fr].vpn = -1;
                            remove_frame_from_fifo(fr);
                            free_frames_count++;
                        }
                        p->pages[u].present = false;
                        p->pages[u].frame = -1;
                    }
                    if (p->pages[u].in_swap) {
                        int si = p->pages[u].swap_index;
                        if (si >= 0) {
                            swap_slots[si].pid = -1;
                            swap_slots[si].vpn = -1;
                            free_swap_count++;
                        }
                        p->pages[u].in_swap = false;
                        p->pages[u].swap_index = -1;
                    }
                }
                return false;
            // si hay espacio libre en el swap, guardo el PID del proceso y el VPN en el
            } else {
                swap_slots[s].pid = p->pid;
                swap_slots[s].vpn = v;
                p->pages[v].present = false;
                p->pages[v].frame = -1;
                p->pages[v].in_swap = true;
                p->pages[v].swap_index = s;
                free_swap_count--;
            }
        }
    }
    return true;
}

// creo un nuevo proceso con tamaño aleatoreo entre min_size y max_size (bytes)
Process* create_process(size_t min_size_bytes, size_t max_size_bytes, size_t page_size) {
    if (process_count >= MAX_PROCESSES) return NULL;
    Process *p = malloc(sizeof(Process));
    if (!p) return NULL;
    p->pid = next_pid++;
    p->size_bytes = min_size_bytes + (rand() % (max_size_bytes - min_size_bytes + 1));
    p->num_pages = (p->size_bytes + page_size - 1) / page_size;
    if (p->num_pages <= 0) p->num_pages = 1;
    if (p->num_pages > MAX_PAGES_PER_PROCESS) p->num_pages = MAX_PAGES_PER_PROCESS;
    p->pages = calloc(p->num_pages, sizeof(PageEntry));
    for (int i = 0; i < p->num_pages; ++i) {
        p->pages[i].present = false;
        p->pages[i].frame = -1;
        p->pages[i].in_swap = false;
        p->pages[i].swap_index = -1;
    }
    p->alive = true;

    // se intentan asignar las paginas en RAM o swap
    bool ok = allocate_process_pages(p);
    if (!ok) {
        free(p->pages);
        free(p);
        return NULL;
    }
    // agregamos el proceso al arreglo
    processes[process_count++] = p;
    printf("[CREATED] Proceso P%d creado: tamaño %zu bytes, %d páginas asignadas\n", p->pid, p->size_bytes, p->num_pages);
    return p;
}

// finalizo el proceso
void kill_process(Process *p) {
    if (!p) return;
    printf("[TERMINATE] Proceso P%d finaliza. Liberando recursos...\n", p->pid);
    for (int v = 0; v < p->num_pages; ++v) {
        if (p->pages[v].present) {
            int fr = p->pages[v].frame;
            if (fr >= 0) {
                frames[fr].pid = -1;
                frames[fr].vpn = -1;
                remove_frame_from_fifo(fr);
                free_frames_count++;
            }
            p->pages[v].present = false;
            p->pages[v].frame = -1;
        }
        if (p->pages[v].in_swap) {
            int si = p->pages[v].swap_index;
            if (si >= 0) {
                swap_slots[si].pid = -1;
                swap_slots[si].vpn = -1;
                free_swap_count++;
            }
            p->pages[v].in_swap = false;
            p->pages[v].swap_index = -1;
        }
    }
    p->alive = false;

    // buscamos la posicion del proceso en el arreglo global processes
    int found = -1;
    for (int i = 0; i < process_count; ++i) {
        if (processes[i] && processes[i]->pid == p->pid) { found = i; break; }
    }
    if (found >= 0) {
        free(p->pages);
        free(p);
        for (int j = found; j < process_count - 1; ++j) processes[j] = processes[j+1];
        processes[--process_count] = NULL;
    }
}

// simulo el acceso a una direccion aleatoria de un proceso aleatorio y verifico si la pagina esta en RAM o si se produjo un page fault
void simulate_random_access(size_t page_size) {
    if (process_count == 0) {
        printf("[ACCESS] No hay procesos para acceder.\n");
        return;
    }
    int idx = rand() % process_count;
    Process *p = processes[idx];
    if (!p) return;
    // se selecciona aleatoriamente un proceso del arreglo processes

    size_t offset = rand() % p->size_bytes;
    int vpn = offset / page_size;
    printf("[ACCESS] P%d accede a dirección virtual (offset %zu) -> página %d\n", p->pid, offset, vpn);
    if (vpn < 0 || vpn >= p->num_pages) {
        printf("  -> dirección fuera de rango\n");
        return;
    }
    if (p->pages[vpn].present) {
        printf("  -> HIT: página en RAM, frame %d\n", p->pages[vpn].frame);
    } else {
        printf("  -> PAGE FAULT: página no está en RAM\n");
        // intentamos traer la pagina a RAM
        if (free_frames_count > 0) {
            int fr = bring_page_into_ram(p->pid, vpn);
            if (fr >= 0) {
                printf("  -> Traída a frame %d desde swap/creación\n", fr);
            } else {
                printf("  -> Error al traer página: no hay espacio y no se pudo evictar\n");
            }
        } else {
            // se elije expulsar un frame con FIFO
            int fr = evict_page_fifo();
            if (fr == -1) {
                printf("  -> No hay swap disponible para realizar swap out; terminando simulación.\n");
                exit(0);
            }
            // cargo la página en el frame liberado
            frames[fr].pid = p->pid;
            frames[fr].vpn = vpn;
            p->pages[vpn].present = true;
            p->pages[vpn].frame = fr;
            // libero espacio del swap si estaba ahi
            if (p->pages[vpn].in_swap) {
                int sidx = p->pages[vpn].swap_index;
                if (sidx >= 0 && sidx < num_swap_slots) {
                    swap_slots[sidx].pid = -1;
                    swap_slots[sidx].vpn = -1;
                    free_swap_count++;
                }
                p->pages[vpn].in_swap = false;
                p->pages[vpn].swap_index = -1;
            }
            enqueue_frame_fifo(fr);
            free_frames_count--;
            printf("  -> Página cargada en frame %d después de eviction\n", fr);
        }
    }
}

/* Print summary of memory usage */
void print_memory_status(size_t page_size) {
    int used_frames = num_frames - free_frames_count;
    int used_swap = num_swap_slots - free_swap_count;
    printf("=== Estado Memoria ===\n");
    printf("Frames RAM: %d total, %d usados, %d libres\n", num_frames, used_frames, free_frames_count);
    printf("Swap slots: %d total, %d usados, %d libres\n", num_swap_slots, used_swap, free_swap_count);
    printf("Procesos: %d\n", process_count);
    for (int i = 0; i < process_count; ++i) {
        Process *p = processes[i];
        printf("  P%d: size=%zu bytes, pages=%d\n", p->pid, p->size_bytes, p->num_pages);
    }
    printf("=======================================================\n");
}

int main() {
    srand(time(NULL));
    printf("Simulación de paginación - política FIFO (interactiva)\n");
    printf("Ingrese tamaño de memoria física (MB): ");
    double phys_mb;
    if (scanf("%lf", &phys_mb) != 1) { printf("Entrada inválida\n"); return 1; }
    printf("Ingrese tamaño de página (KB): ");
    double page_kb;
    if (scanf("%lf", &page_kb) != 1) { printf("Entrada inválida\n"); return 1; }
    // rango de tamaño de procesos (MB) 
    printf("Ingrese tamaño mínimo de proceso (KB): ");
    double proc_min_kb;
    if (scanf("%lf", &proc_min_kb) != 1) { printf("Entrada inválida\n"); return 1; }
    printf("Ingrese tamaño máximo de proceso (KB): ");
    double proc_max_kb;
    if (scanf("%lf", &proc_max_kb) != 1) { printf("Entrada inválida\n"); return 1; }
    if (proc_max_kb < proc_min_kb) { printf("Máximo < mínimo\n"); return 1; }

    size_t phys_bytes = (size_t)(phys_mb * 1024.0 * 1024.0 + 0.5);
    size_t page_bytes = (size_t)(page_kb * 1024.0 + 0.5);
    if (page_bytes == 0) { printf("Tamaño de página inválido\n"); return 1; }

    // memoria virtual random entre 1.5 - 4.5
    double factor = 1.5 + ((double)rand() / RAND_MAX) * 3.0;
    size_t virt_bytes = (size_t)(phys_bytes * factor + 0.5);

    int phys_pages = (phys_bytes + page_bytes - 1) / page_bytes;
    int virt_pages = (virt_bytes + page_bytes - 1) / page_bytes;
    int swap_pages = virt_pages - phys_pages;
    if (swap_pages < 0) swap_pages = 0;

    num_frames = phys_pages;
    num_swap_slots = swap_pages;

    // reservo memoria para los frames de RAM y los espacios del swap
    frames = calloc(num_frames, sizeof(Frame));
    for (int i = 0; i < num_frames; ++i) { frames[i].pid = -1; frames[i].vpn = -1; }
    swap_slots = calloc(num_swap_slots, sizeof(SwapSlot));
    for (int i = 0; i < num_swap_slots; ++i) { swap_slots[i].pid = -1; swap_slots[i].vpn = -1; }

    free_frames_count = num_frames;
    free_swap_count = num_swap_slots;

    fifo_capacity = num_frames > 0 ? num_frames : 1;
    fifo_queue = malloc(sizeof(int) * fifo_capacity);
    fifo_head = fifo_tail = fifo_size = 0;

    printf("Memoria física: %zu bytes (%d páginas)\n", phys_bytes, num_frames);
    printf("Memoria virtual: %zu bytes (factor %.2f) (%d páginas)\n", virt_bytes, factor, virt_pages);
    printf("Swap disponible (páginas): %d\n", num_swap_slots);
    printf("Política de reemplazo: FIFO (cola de marcos)\n");
    printf("Creación de procesos cada 2s. A partir de 30s: accesos y terminaciones cada 5s.\n");
    printf("Interactivo: mostrando eventos en tiempo real.\n");

    // (ticks = segundos)
    int tick = 0;
    int last_creation = -2; 
    int last_periodic = -5;
    while (1) {
        tick++;
        printf("\n--- Tiempo: %d s ---\n", tick);

        // creo proceso cada 2 segundos
        if ((tick - last_creation) >= 2) {
            last_creation = tick;
            // creo proceso con tamaño aleatoreo entre proc_min_kb y proc_max_kb
            size_t min_b = (size_t)(proc_min_kb * 1024.0 + 0.5);
            size_t max_b = (size_t)(proc_max_kb * 1024.0 + 0.5);
            Process *p = create_process(min_b, max_b, page_bytes);
            if (!p) {
                printf("[ERROR] No se pudo crear proceso: no hay espacio en RAM ni en Swap. Terminando simulación.\n");
                break;
            }
            print_memory_status(page_bytes);
        }

        // after first 30 seconds, every 5 seconds: random termination and random access
        // despues de los primeros 30 segundos, cada 5 segundos se ejecutan eventos periodicos: terminación aleatoria de un proceso / acceso aleatorio a memoria
        if (tick >= 30 && (tick - last_periodic) >= 5) {
            last_periodic = tick;
            // terminacion
            if (process_count > 0) {
                int idx = rand() % process_count;
                Process *victim = processes[idx];
                printf("[EVENTO] Cada 5s: terminación aleatoria -> P%d\n", victim->pid);
                kill_process(victim);
            } else {
                printf("[EVENTO] No hay procesos para terminar.\n");
            }
            // acceso aleatoreo
            simulate_random_access(page_bytes);
            print_memory_status(page_bytes);
        }

        // si no hay frames libres ni espacio en el swap y no se pueden expulsar páginas (FIFO vacio), la simulación termina
        if (free_frames_count == 0 && free_swap_count == 0) {
            if (fifo_size == 0) {
                printf("[TERMINACION] No hay memoria disponible en RAM ni swap. Finalizando.\n");
                break;
            }
        }

        // 1 segundo
        sleep(1);
    }

    for (int i = 0; i < process_count; ++i) {
        if (processes[i]) {
            free(processes[i]->pages);
            free(processes[i]);
            processes[i] = NULL;
        }
    }
    free(frames);
    free(swap_slots);
    free(fifo_queue);

    printf("Simulación finalizada.\n");
    return 0;
}