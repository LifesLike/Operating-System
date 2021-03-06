#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define PARTITION_SIZE 256 * 1024
#define BLOCK_SIZE 4 * 1024

typedef struct _Inode {
    unsigned int fsize;
    unsigned int blocks;
    unsigned int pointer[12];
    char __dummy__[200];
} Inode;

int ku_fs_init();
void set_bitmap(char* bitmap, int inode_num);
void clear_bitmap(char* bitmap, int inode_num);
int is_mapped_inum(char* bitmap, int inode_num);
int find_free_bitmap_idx(char* bitmap);
int write_file(char* file_name, unsigned int byte);
int read_file(char* file_name, unsigned int byte);
int delete_file(char* file_name);

void* partition;    // 초기화된 파일 시스템 위치
char* inode_bitmap; // inode 비트맵 위치
char* data_bitmap;  // data 비트맵 위치
Inode* root_inode;  // 루트 inode
char* root_data_block;  // 루트 inode 데이터가 저장된 위치


int main(int argc, char** argv) {
    
    if (argc != 2) {
        printf("ku_fs: Wrong number of arguments\n");
		return 1;
    }

    FILE* fd=NULL;
    char buffer[100];
    fd = fopen(argv[1], "r");

	if(!fd){
		printf("ku_fs: Fail to open the input file\n");
		return 1;
	}

    int init = ku_fs_init();
    if (init) {
        printf("ku_fs: Fail to allocate new file system\n");
        return 1;
    }

    while (fgets(buffer, sizeof(buffer), fd) != NULL) {
        char* tok = strtok(buffer, " ");
        char title[3];
        unsigned int byte = 0;
        strcpy(title, tok);
        tok = strtok(NULL, " ");
        char mode = *tok;
        if (mode != 'd') {
            tok = strtok(NULL, " ");
            byte = atoi(tok);
        }
        if (mode == 'w') {
            write_file(title, byte);
        }
        else if (mode == 'r') {
            read_file(title, byte);
        }
        else if (mode == 'd') {
            delete_file(title);
        }

    }
    // eof
    for (int i = 0; i < 4096*64; i++) {
        printf("%.2x ", *((unsigned char*)partition + i));        
    }

    return 0;
}

int ku_fs_init() {
    // 256KB 파티션 생성
    partition = calloc(1, PARTITION_SIZE);
    inode_bitmap = &partition[BLOCK_SIZE];
    data_bitmap = &partition[2*BLOCK_SIZE];

    set_bitmap(inode_bitmap, 0);
    set_bitmap(inode_bitmap, 1); // not used

    //루트 디렉토리 초기화
    int inode_idx = find_free_bitmap_idx(inode_bitmap);
    if (inode_idx == -1) {
        return -1;
    }
    set_bitmap(inode_bitmap, inode_idx);

    // inode offset
    unsigned int root_inode_offset = inode_idx * sizeof(Inode) + 3 * BLOCK_SIZE;

    root_inode = (partition + root_inode_offset);
    root_inode->fsize = 4 * 80;
    root_inode->blocks = 1;

    int data_block_idx = find_free_bitmap_idx(data_bitmap);
    if (data_block_idx == -1) {
        return -1;
    }
    set_bitmap(data_bitmap, data_block_idx);
    root_inode->pointer[0] = data_block_idx;

    int data_block_offset = data_block_idx * BLOCK_SIZE + 8*BLOCK_SIZE;
    root_data_block = (partition + data_block_offset); // 루트 디렉토리 데이터 블럭 실제 위치

    return 0;
}

void set_bitmap(char* bitmap, int inode_num) {
    int order = inode_num / 8;
    int offset = 8 - inode_num%8 - 1;
    unsigned char flag = 1;
    flag = flag << offset;
    bitmap[order] = bitmap[order] | flag;
}

void clear_bitmap(char* bitmap, int inode_num) {
    int order = inode_num / 8;
    int offset = 8 - inode_num%8 - 1;
    unsigned char flag =1;
    flag = flag << offset;
    flag = ~flag;
    bitmap[order] = bitmap[order] & flag;
}

int is_mapped_inum(char* bitmap, int inode_num) {
    int order = inode_num / 8;
    int offset = 8 - inode_num%8 - 1;
    unsigned char flag =1;
    flag = flag << offset;
    if (bitmap[order] & flag) {
        return 1;
    }
    else {
        return 0;
    }
}

int find_free_bitmap_idx(char* bitmap) {
    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (is_mapped_inum(bitmap, i) == 0) {
            return i;
        }
    }
    return -1;
}

int write_file(char* file_name, unsigned int byte) {
    unsigned int new_inode_offset; // 새로 할당할 inode 위치
    int error_flag = 0;

    // 동일한 이름 파일 여부
    for (int i = 0; i < BLOCK_SIZE; i+= 4) {
        if (*(root_data_block+i) != 0) {
            if (strcmp((root_data_block+i + 1), file_name) == 0) {
                error_flag = 1;
                break;
            }
        }
    }
    
    if (error_flag == 0) {
        // 4바이트씩 순회하면서 비어있는 inum 찾기
        for (int i = 0; i < BLOCK_SIZE; i+=4) {
            if (*(root_data_block+i) == 0) {
                int new_inode_num = find_free_bitmap_idx(inode_bitmap);
                if (new_inode_num == -1 || new_inode_num > 55) {
                    error_flag = 3;
                    break;
                }
                new_inode_offset = new_inode_num * sizeof(Inode) + 3 * BLOCK_SIZE;
                Inode* new_inode = (partition + new_inode_offset);
                new_inode->fsize = byte;
                int pointer_cnt = 0;
                unsigned int remain_byte = byte;
                while (remain_byte) {
                    int new_data_block_idx = find_free_bitmap_idx(data_bitmap);
                    if (new_data_block_idx == -1 || new_data_block_idx > 55) {
                        error_flag = 2;
                        break;
                    }
                    if (pointer_cnt == 12) {
                        error_flag = 2;
                        break;
                    }
                    set_bitmap(data_bitmap, new_data_block_idx); // 추가
                    new_inode->pointer[pointer_cnt] = new_data_block_idx;
                    pointer_cnt++;
                    new_inode->blocks = pointer_cnt;
                    remain_byte = (remain_byte <= BLOCK_SIZE)? 0 : (remain_byte - BLOCK_SIZE);

                }

                if (error_flag) {
                    break;
                }

                remain_byte = byte;

                // 파일 내용 쓰기
                while (remain_byte) {
                    for (int j = 0; j < new_inode->blocks; j++) {
                        if (remain_byte == 0) {
                            break;
                        }
                        int data_block_no = new_inode->pointer[j];
                        int data_block_offset = data_block_no * BLOCK_SIZE + 8*BLOCK_SIZE;
                        char* data_block = (partition + data_block_offset);
                        int smaller_byte = (remain_byte <= BLOCK_SIZE)? remain_byte : BLOCK_SIZE;

                        for (int ptr = 0; ptr < smaller_byte; ptr++) {
                            *(data_block + ptr) = file_name[0];
                        }
                        remain_byte -= smaller_byte;
                    }
                }

                set_bitmap(inode_bitmap, new_inode_num);
                *(root_data_block+i) = new_inode_num;
                strcpy((root_data_block+i+1), file_name);
                break;
            }
        }
    }
    
    // 에러 발생
    if (error_flag) {
        if (error_flag == 1) {
            printf("Already exists\n");
        }
        else if (error_flag == 2) {
            // 앞서 임시로 할당했던 데이터 초기화
            Inode* del_inode = (partition + new_inode_offset);
            for (int i = 0; i < del_inode->blocks; i++) {
                clear_bitmap(data_bitmap, del_inode->pointer[i]);
                del_inode->pointer[i] = 0;
            }
            clear_bitmap(inode_bitmap, new_inode_offset);

            char* del_block = (char*)(del_inode);
            for (int i = 0; i < 256; i++) {
                *(del_block + i) = 0;
            }
                
            printf("No space\n");
        }
        else if (error_flag == 3) {
            printf("No spaace\n");
        }

        return -1;
    }
    
    return 0;
}

int read_file(char* file_name, unsigned int byte) {
    // 동일한 이름 파일 여부
    for (int i = 0; i < BLOCK_SIZE; i+=4) {
        if (*(root_data_block+i) != 0) {
            if (strcmp((root_data_block+i + 1), file_name) == 0) {
                int target_file_inode_num = *(root_data_block+i);
                int target_file_inode_offset = target_file_inode_num * sizeof(Inode) + 3*BLOCK_SIZE;
                Inode* target_inode = (partition + target_file_inode_offset);

                if (target_inode->fsize >= byte && byte > BLOCK_SIZE || 
                        target_inode->fsize < byte && target_inode->fsize > BLOCK_SIZE) {
                    
                    unsigned int remain_byte = (target_inode->fsize > byte)? byte : target_inode->fsize;
                    while (remain_byte) {
                        for (int j = 0; j < target_inode->blocks; j++) {
                            if (remain_byte == 0) {
                                break;
                            }
                            int data_block_num = target_inode->pointer[j];
                            int data_block_offset = data_block_num * BLOCK_SIZE + 8*BLOCK_SIZE;
                            char* data_block = partition + data_block_offset;
                            int size = (remain_byte > BLOCK_SIZE)? BLOCK_SIZE : remain_byte;

                            for (int p = 0; p < size; p++) {
                                printf("%c", *(data_block + p));
                            }

                            int smaller_byte = (remain_byte <= BLOCK_SIZE)? remain_byte : BLOCK_SIZE;
                            remain_byte -= smaller_byte;
                        }
                           
                    }
                }
                else {
                    unsigned int remain_byte = (target_inode->fsize > byte)? byte : target_inode->fsize;
                    int data_block_num = target_inode->pointer[0];
                    int data_block_offset = data_block_num * BLOCK_SIZE + 8*BLOCK_SIZE;
                    char* data_block = partition + data_block_offset;
                    for (int j = 0; j < remain_byte; j++) {
                        printf("%c", *(data_block + j));
                    }
                }

                printf("\n");
                return 0;
            }
        }
    }

    printf("No such file\n");
    return -1; 
}

int delete_file(char* file_name) {
    // 파일 순회
    for (int i = 0; i < BLOCK_SIZE; i+=4) {
        if (*(root_data_block+i) != 0) {
            if (strcmp((root_data_block+i + 1), file_name) == 0) {
                int target_file_inode_num = *(root_data_block+i); //삭제해야 하는 inode 번호
                int target_file_inode_offset = target_file_inode_num * sizeof(Inode) + 3*BLOCK_SIZE;
                Inode* target_inode = (partition + target_file_inode_offset);

                for (int inum = 0; inum < target_inode->blocks; inum++) {
                    int del_data_inode_num = target_inode->pointer[inum];
                    clear_bitmap(data_bitmap, del_data_inode_num);
                }
                clear_bitmap(inode_bitmap, target_file_inode_num);
                *(root_data_block+i) = 0;

                return 0;
            }
        }
    }

    printf("No such file\n");
    return -1;
}
