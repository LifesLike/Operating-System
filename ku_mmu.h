#define ku_mmu_PAGE_SIZE 4 // size of PAGE is 4Byte
#define ku_mmu_MAX_PMEM_SIZE 64 // number of physical memory page is up to 2^6
#define ku_mmu_MAX_SMEM_SIZE 128 // number of swap space page is up to 2^7


typedef struct ku_pte {
    char entry;
} ku_pte;

typedef struct ku_mmu_PCB {
    char pid;
    char pfn_begin;
    char pfn_end;
    ku_pte* pdbr;
    struct ku_mmu_PCB* next;
} ku_mmu_PCB;

typedef struct ku_mmu_list {
    ku_mmu_PCB* head;
} ku_mmu_list;

typedef struct ku_mmu_queue_node {
    ku_pte* pte;
    struct ku_mmu_queue_node* next;
} ku_mmu_queue_node;

typedef struct ku_mmu_queue {
    ku_mmu_queue_node* front;
    ku_mmu_queue_node* rear;
} ku_mmu_queue;

void ku_mmu_listInit(ku_mmu_list* plist) {
    plist->head = (ku_mmu_PCB*) malloc(sizeof(ku_mmu_PCB));
    plist->head->next = NULL;
}

ku_mmu_PCB* ku_mmu_listInsert(ku_mmu_list* plist, char pid) {
    ku_mmu_PCB* newNode = (ku_mmu_PCB*) malloc(sizeof(ku_mmu_PCB));
    newNode->pid = pid;
    newNode->next = plist->head->next;
    plist->head->next = newNode;

    return newNode;
}

ku_mmu_PCB* ku_mmu_listSearch(ku_mmu_list* plist, char targetPid) {
    ku_mmu_PCB* curNode = plist->head->next;
    while(curNode != NULL) {
        if (curNode->pid == targetPid) {
            return curNode;
        }
        curNode = curNode->next;
    }

    return NULL;
}

void ku_mmu_queueInit(ku_mmu_queue* pq) {
    pq->front = NULL;
    pq->rear = NULL;
}

int ku_mmu_queueIsEmpty(ku_mmu_queue* pq) {
    if (pq->front == NULL) {
        return 1;
    }
    else {
        return 0;
    }
}

ku_pte* ku_mmu_enQueue(ku_mmu_queue* pq, ku_pte* pte) {
    ku_mmu_queue_node* newNode = (ku_mmu_queue_node*)malloc(sizeof(ku_mmu_queue_node));
    newNode->next = NULL;
    newNode->pte = pte;
    
    if (ku_mmu_queueIsEmpty(pq)) {
        pq->front = newNode;
        pq->rear = newNode;
    }
    else {
        pq->rear->next = newNode;
        pq->rear = newNode;
    }
}

ku_pte* ku_mmu_deQueue(ku_mmu_queue* pq) {
    if (ku_mmu_queueIsEmpty(pq)) {
        return NULL;
    }
    ku_mmu_queue_node* delNode = pq->front;
    ku_pte* delData = delNode->pte;
    pq->front = pq->front->next;

    free(delNode);
    return delData;
}


ku_mmu_PCB* ku_mmu_create_process(char pid);
int ku_mmu_findFreePhysicalPage();
int ku_mmu_swap_out();
int ku_mmu_swap_in(unsigned char pte);


char* ku_mmu_pmemBaseAddr; // physical memory base address
char* ku_mmu_smemBaseAddr; // swap space base address

char* ku_mmu_pmem_free_list; // physical memory free list
char* ku_mmu_smem_free_list; // swap space free list

unsigned int ku_mmu_pmem_free_list_size;
unsigned int ku_mmu_smem_free_list_size;

ku_mmu_list ku_mmu_running_process; // list of currently running processes
ku_mmu_queue ku_mmu_demanded_page; // queue of pages that can be swap out


void* ku_mmu_init(unsigned int pmem_size, unsigned int swap_size) {
    ku_mmu_pmemBaseAddr = (char*) calloc(1, pmem_size);
    ku_mmu_smemBaseAddr = (char*) calloc(1, swap_size);

    if (ku_mmu_pmemBaseAddr == NULL) {
        return 0;
    }

    ku_mmu_pmem_free_list_size = pmem_size / ku_mmu_PAGE_SIZE;
    ku_mmu_smem_free_list_size = swap_size / ku_mmu_PAGE_SIZE;

    if (ku_mmu_pmem_free_list_size > ku_mmu_MAX_PMEM_SIZE) {
        ku_mmu_pmem_free_list_size = ku_mmu_MAX_PMEM_SIZE;
    }
    if (ku_mmu_smem_free_list_size > ku_mmu_MAX_SMEM_SIZE) {
        ku_mmu_smem_free_list_size = ku_mmu_MAX_SMEM_SIZE;
    }

    ku_mmu_pmem_free_list = (char*) calloc(sizeof(char), ku_mmu_pmem_free_list_size);
    ku_mmu_pmem_free_list[0] = 1; // Occupied by OS
    ku_mmu_smem_free_list = (char*) calloc(sizeof(char), ku_mmu_smem_free_list_size);
    ku_mmu_smem_free_list[0] = 1; // don't use

    ku_mmu_listInit(&ku_mmu_running_process);
    ku_mmu_queueInit(&ku_mmu_demanded_page);

    return ku_mmu_pmemBaseAddr;
}

int ku_run_proc(char pid, struct ku_pte** ku_cr3) {

    ku_mmu_PCB* cur_node = ku_mmu_listSearch(&ku_mmu_running_process, pid);
    if (cur_node == NULL) {
        cur_node = ku_mmu_create_process(pid);
        if (cur_node == NULL) {
            return -1;
        }
    }

    *ku_cr3 = cur_node->pdbr;

    return 0;
}

int ku_page_fault(char pid, char va) {

    ku_mmu_PCB* cur_node = ku_mmu_listSearch(&ku_mmu_running_process, pid);
    if (cur_node == NULL) {
        return -1;
    }

    ku_pte* cur_pdbr = cur_node->pdbr;
    // printf("pdbr: %d\n", curPdbr); // test
    unsigned char mask = 0b11000000;
    ku_pte* cur_pde = cur_pdbr + ((va & mask) >> 6);
    // printf("pde: %d\n", curPde); // test
    if (cur_pde->entry == 0) {
        int new_PFN_idx = ku_mmu_findFreePhysicalPage();
        if (new_PFN_idx == -1) {
            new_PFN_idx = ku_mmu_swap_out();
            if (new_PFN_idx == -1) {
                return -1;
            }
        }

        ku_mmu_pmem_free_list[new_PFN_idx] = 1;
        cur_pde->entry = (new_PFN_idx << 2) | 1;
    }
    
    mask = 0b00110000;
    ku_pte* cur_pmde = (ku_pte*)(ku_mmu_pmemBaseAddr + ((cur_pde->entry >> 2) * ku_mmu_PAGE_SIZE) + ((va & mask) >> 4));
    // printf("pmde: %d\n", curPmde); // test
    if (cur_pmde->entry == 0) {
        int new_PFN_idx = ku_mmu_findFreePhysicalPage();
        if (new_PFN_idx == -1) {
            new_PFN_idx = ku_mmu_swap_out();
            if (new_PFN_idx == -1) {
                return -1;
            }
        }
        ku_mmu_pmem_free_list[new_PFN_idx] = 1;
        cur_pmde->entry = (new_PFN_idx << 2) | 1;
    }

    mask = 0b00001100;
    ku_pte* cur_pte = (ku_pte*)(ku_mmu_pmemBaseAddr + ((cur_pmde->entry >> 2) * ku_mmu_PAGE_SIZE) + ((va & mask) >> 2));
    // printf("pte: %d\n", curPte); // test
    if (cur_pte->entry == 0) {
        int new_PFN_idx = ku_mmu_findFreePhysicalPage();
        if (new_PFN_idx == -1) {
            new_PFN_idx = ku_mmu_swap_out();
            if (new_PFN_idx == -1) {
                return -1;
            }
        }
        ku_mmu_pmem_free_list[new_PFN_idx] = 1;
        cur_pte->entry = (new_PFN_idx << 2) | 1;
        ku_mmu_enQueue(&ku_mmu_demanded_page, cur_pte);
    }

    // check present
    mask = 0b00000001;
    unsigned char present_bit = cur_pte->entry & mask;
    if (present_bit == 0) {
        int new_PFN_idx = ku_mmu_swap_in(cur_pte->entry);
        if (new_PFN_idx == -1) {
            return -1;
        }
        ku_mmu_pmem_free_list[new_PFN_idx] = 1;
        cur_pte->entry = (new_PFN_idx << 2) | 1;
    }
 

    return 0;
}

// ku_mmu_PCB* ku_mmu_create_process(char pid) {
//     // 피지컬 메모리에 pcb 추가하는거 구현해야됨
//     int new_PFN_idx = ku_mmu_findFreePhysicalPage_forPCB();
//     if (new_PFN_idx == -1) {
//         new_PFN_idx = ku_mmu_swap_out();
//         if (new_PFN_idx == -1) {
//             return NULL;
//         }
//         return ku_mmu_create_process(pid);
//     }

//     for (int i = 0; i < 2; i++) {
//         ku_mmu_pmem_free_list[new_PFN_idx + i] = 1;
//     }
    
//     ku_mmu_PCB* new_process = ku_mmu_listInsert(&ku_mmu_running_process, pid);
//     new_process->pdbr = (ku_pte*)(ku_mmu_pmemBaseAddr + new_PFN_idx*ku_mmu_PAGE_SIZE); // 여기서부터 다시

//     printf("size: %d\n", sizeof(new_process));
//     return new_process;
// }

ku_mmu_PCB* ku_mmu_create_process(char pid) {
    int new_PFN_begin = ku_mmu_findFreePhysicalPage();
    if (new_PFN_begin == -1) {
        new_PFN_begin = ku_mmu_swap_out();
        if (new_PFN_begin == -1) {
            return NULL;
        }
    }

    ku_mmu_pmem_free_list[new_PFN_begin] = 1;

    int new_PFN_end = ku_mmu_findFreePhysicalPage();
    if (new_PFN_end == -1) {
        new_PFN_end = ku_mmu_swap_out();
        if (new_PFN_end == -1) {
            return NULL;
        }
    }

    ku_mmu_pmem_free_list[new_PFN_end] = 1;

    int new_PFN_pdbr = ku_mmu_findFreePhysicalPage();
    if (new_PFN_pdbr == -1) {
        new_PFN_pdbr = ku_mmu_swap_out();
        if (new_PFN_pdbr == -1) {
            return NULL;
        }
    }

    ku_mmu_pmem_free_list[new_PFN_pdbr] = 1;


    ku_mmu_PCB* new_process = ku_mmu_listInsert(&ku_mmu_running_process, pid);
    new_process->pdbr = (ku_pte*)(ku_mmu_pmemBaseAddr + new_PFN_pdbr*ku_mmu_PAGE_SIZE);
    new_process->pfn_begin = new_PFN_begin;
    new_process->pfn_end = new_PFN_end;

    long long addr = (long long) new_process;
    unsigned int first_half = (addr >> 32);
    unsigned int second_half = (addr << 32)>>32;
    // printf("new process: %ld\n", addr);
    // printf("first_half: %d\n", first_half);
    // printf("second_half: %d\n", second_half);

   *(ku_mmu_pmemBaseAddr + new_PFN_begin*ku_mmu_PAGE_SIZE) = first_half;
    *(ku_mmu_pmemBaseAddr + new_PFN_end*ku_mmu_PAGE_SIZE) = second_half;

    return new_process;
}

int ku_mmu_findFreePhysicalPage() {
    for (int i = 1; i < ku_mmu_pmem_free_list_size; i++) {
        if (ku_mmu_pmem_free_list[i] == 0) {
            return i;
        }
    }
    return -1; // no free page found
}

int ku_mmu_findFreeSwappingPage() {
    for (int i = 1; i < ku_mmu_smem_free_list_size; i++) {
        if (ku_mmu_smem_free_list[i] == 0) {
            return i;
        }
    }
    return -1; // no free page found;
}

int ku_mmu_swap_out() {
    ku_pte* target_pte = ku_mmu_deQueue(&ku_mmu_demanded_page);
    if (target_pte == NULL) { // in case of too small physical memory
        return -1;
    }
    int new_SFN_idx = ku_mmu_findFreeSwappingPage();
    if (new_SFN_idx == -1) {
        return -1;
    }

    ku_mmu_smem_free_list[new_SFN_idx] = 1;
    char swap_entry = new_SFN_idx << 1 | 0;
    int cur_pfn = (target_pte->entry >> 2);
    target_pte->entry = swap_entry;

    return cur_pfn;
}

int ku_mmu_swap_in(unsigned char pte) {
    unsigned char swap_space_offset = (pte & 0b11111110) >> 1;
    // printf("swapspace offset: %d\n", swap_space_offset);

    int new_PFN_idx = ku_mmu_findFreePhysicalPage();
    if (new_PFN_idx == -1) {
        new_PFN_idx = ku_mmu_swap_out();
    }
    ku_mmu_smem_free_list[swap_space_offset] = 0;

    return new_PFN_idx;
}