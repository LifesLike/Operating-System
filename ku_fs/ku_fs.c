#include <stdio.h>
#include <stdlib.h>


#define PARTITION_SIZE 256 * 1024
#define BLOCK_SIZE 4 * 1024

typedef struct __inode {
    unsigned int fsize;
    unsigned int blocks;
    unsigned int pointer[12];
    char __dummy__[200];
} inode;

void* partition;
char* ibmap;
char* dbmap;

int ku_fs_init();
void set_bmap(char* bmap, int inode_num);
int is_mapped(char* bmap, int inode_num);
int find_free_bmap_idx(char* bmap);



int main(int argc, char** argv) {

    ku_fs_init();
    unsigned int offset = 2 * sizeof(inode) + 3 * BLOCK_SIZE;
    inode* root = (partition + offset);
    printf("%d\n", root->blocks);



    printf("start\n");
    // inode test;
    // printf("size: %d\n", sizeof(test));
    for (int i = 0; i < 4096*64; i++) {
        printf("%.2x ", *((unsigned char*)partition + i));        
        // printf("%.2x ", *(unsigned char*)(partition + i));
        if (*((unsigned char*)partition + i) != 0) {
            printf("here in i: %d\n", i);
        }
        fflush(stdout);
    }
    printf("fin\n");

    return 0;
}

int ku_fs_init() {
    // 256KB 파티션 생성
    partition = calloc(1, PARTITION_SIZE);
    ibmap = &partition[BLOCK_SIZE];
    dbmap = &partition[2*BLOCK_SIZE];

    set_bmap(ibmap, 0);
    set_bmap(ibmap, 1); // not used

    //루트 디렉토리 초기화
    int inode_idx = find_free_bmap_idx(ibmap);
    if (inode_idx == -1) {
        return -1;
    }
    set_bmap(ibmap, inode_idx);
    // inode offset
    unsigned int offset = inode_idx * sizeof(inode) + 3 * BLOCK_SIZE;
    inode root_inode;
    root_inode.fsize = 4 * 80;
    root_inode.blocks = 1;

    int data_block_idx = find_free_bmap_idx(dbmap);
    if (data_block_idx == -1) {
        return -1;
    }
    set_bmap(dbmap, data_block_idx);

    unsigned int data_block_offset = data_block_idx * BLOCK_SIZE + 8*BLOCK_SIZE;

    *(inode*)(partition+data_block_offset) = root_inode;

    printf("hello\n");

    return 0;
}

void set_bmap(char* bmap, int inode_num) {
    int order = inode_num/8;
    int offset = 8 - inode_num%8 - 1;
    unsigned char flag = 1;
    flag = flag << offset;
    bmap[order] = bmap[order] | flag;
    printf("%d\n", bmap[order]);

}

int is_mapped(char* bmap, int inode_num) {
    int order = inode_num/8;
    int offset = 8 - inode_num%8 - 1;
    unsigned char flag =1;
    flag = flag << offset;
    if (bmap[order] & flag) {
        return 1;
    }
    else {
        return 0;
    }
}

int find_free_bmap_idx(char* bmap) {
    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (is_mapped(bmap, i) == 0) {
            return i;
        }
    }
    return -1;
}
