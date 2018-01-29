#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ext2fs/ext2_fs.h>


#define EXT_FILEPATH "../../ext2_img"

#define SUPERBLOCK_OFFSET   1024
#define FIRST_BYTE_MASK     0xFF
#define EXT2_S_IFDIR        0x4000
#define BPB                 8               //  Bits per byte


#define err_exit(msg)    do {                    \
                             perror(msg);        \
                             exit(EXIT_FAILURE); \
                         } while (0)


struct fs_info {
    int fd;

    // It is superblock number
    // Really first data block = first_data_block + 1
    unsigned first_data_block;

    unsigned inodes_per_group;
    unsigned inodes_count;
    unsigned inode_size;
    unsigned block_size;
    unsigned group_size;
};


struct block_iter {
    struct fs_info *info;
    struct ext2_inode *inode;
    void *curr_block_data;
    unsigned next_block_idx;
};


struct dentry_iter {
    struct block_iter *biter;
    struct ext2_dir_entry_2 *curr_dentry;   //  Current dir entry (dentry)
    unsigned dir_size;                      //  Dir size in bytes (for stopping)
    unsigned curr_offset;                   //  Current offset in dir (in bytes)
};


struct block_iter *block_iter_init(struct ext2_inode *inode, struct fs_info *info);
void block_iter_fini(struct block_iter *biter);
void *get_next_block(struct block_iter *biter);
unsigned read_ptr_from_block(unsigned block_number, unsigned ptr_idx, struct fs_info *info);
struct dentry_iter *dentry_iter_init(struct ext2_inode *inode, struct fs_info *info);
void dentry_iter_fini(struct dentry_iter *diter);
struct ext2_dir_entry_2 *get_next_dentry(struct dentry_iter *diter);

struct fs_info *get_fs_info(int fd);
void free_fs_info(struct fs_info *info);
void print_directory_by_inode_number(unsigned inode_number, struct fs_info *info);
struct ext2_inode *get_inode_by_number(unsigned inode_number, struct fs_info *info);

void print_directory_by_path(char *path, struct fs_info *info);
unsigned get_inode_number_by_name(unsigned base_inode_number, char *name, struct fs_info *info);
unsigned get_inode_number_by_path(char *path, struct fs_info *info);

char *read_path();
char *get_curr_dir(char *path);
char *cut_path(char *path);


int main(void)
{
    int ext_fd = open(EXT_FILEPATH, O_RDONLY);
    if(ext_fd == -1)
        err_exit("Can't open ext2 image file");

    struct fs_info *info = get_fs_info(ext_fd);

    char *path = read_path();

    //print_directory_by_inode_number(14, info);
    print_directory_by_path(path, info);
    close(ext_fd);

    return 0;
}


void print_directory_by_path(char *path, struct fs_info *info) {
    unsigned inode_number = get_inode_number_by_path(path, info);
    print_directory_by_inode_number(inode_number, info);
}


unsigned get_inode_number_by_path(char *path, struct fs_info *info) {
    unsigned curr_inode_number = get_inode_number_by_name(EXT2_ROOT_INO,
                                                          get_curr_dir(path),
                                                          info);
    path = cut_path(path);
    while(path && curr_inode_number != 0) {
        curr_inode_number = get_inode_number_by_name(curr_inode_number,
                                                     get_curr_dir(path),
                                                     info);
        path = cut_path(path);
    }

    return curr_inode_number;
}


unsigned get_inode_number_by_name(unsigned base_inode_number, char *name, struct fs_info *info) {
    struct ext2_inode *base_inode = get_inode_by_number(base_inode_number, info);
    struct dentry_iter *diter = dentry_iter_init(base_inode, info);

    struct ext2_dir_entry_2 *curr_dentry = get_next_dentry(diter);
    while(curr_dentry && curr_dentry->inode != 0) {
        if(strcmp(curr_dentry->name, name) == 0) {
            unsigned inode = curr_dentry->inode;
            dentry_iter_fini(diter);
            return inode;
        }

        curr_dentry = get_next_dentry(diter);
    }

    dentry_iter_fini(diter);
    return 0;
}


void print_directory_by_inode_number(unsigned inode_number, struct fs_info *info) {
    struct ext2_inode *dir_inode = get_inode_by_number(inode_number, info);
    if(!dir_inode)
        err_exit("Can't get inode");

    if(!(dir_inode->i_mode & EXT2_S_IFDIR))
        err_exit("It is not a directory");

    struct dentry_iter *diter = dentry_iter_init(dir_inode, info);
    struct ext2_dir_entry_2 *curr_dentry = get_next_dentry(diter);
    printf("(inode #%d)\n", inode_number);
    while(curr_dentry && curr_dentry->inode != 0) {
        printf("%s\n", curr_dentry->name);
        curr_dentry = get_next_dentry(diter);
    }
}


struct ext2_inode *get_inode_by_number(unsigned inode_number, struct fs_info *info) {
    if(inode_number >= info->inodes_count)  //  Inode by this number can't exist
        return NULL;

    unsigned inumb_base_0 = inode_number - 1;
    unsigned zero_group_offset = (info->first_data_block + 1) * info->block_size;
    unsigned group_offset = (inumb_base_0 / info->inodes_per_group) * info->group_size;
    struct ext2_group_desc *gdesc = (struct ext2_group_desc *)calloc(1, sizeof(struct ext2_group_desc));
    if(!gdesc)
        err_exit("Can't allocate memory for group description");

    if(pread(info->fd, gdesc, sizeof(struct ext2_group_desc), group_offset + zero_group_offset) == -1)
        err_exit("Can't read group description");

    unsigned inode_bitmap_offset = group_offset + gdesc->bg_inode_bitmap * info->block_size;
    char *inode_bitmap = (char *)calloc(info->inodes_count / sizeof(char), sizeof(char));
    if(pread(info->fd, inode_bitmap, info->inodes_count / BPB, inode_bitmap_offset) == -1)
        err_exit("Can't read inode bitmap");

    if(!(inode_bitmap[inumb_base_0 / BPB] & (1 << (inumb_base_0 % BPB))))
        return NULL;

    unsigned inode_table_offset = group_offset + gdesc->bg_inode_table * info->block_size;
    unsigned inode_offset = inode_table_offset + (inumb_base_0 % info->inodes_per_group) * info->inode_size;

    struct ext2_inode *inode = (struct ext2_inode *)calloc(1, sizeof(struct ext2_inode));
    if(!inode)
        err_exit("Can't allocate memory for inode");

    if(pread(info->fd, inode, sizeof(struct ext2_inode), inode_offset) == -1)
        err_exit("Can't read inode");

    return inode;
}


struct fs_info *get_fs_info(int fd) {
    struct ext2_super_block SB;
    if(pread(fd, &SB, sizeof(struct ext2_super_block), SUPERBLOCK_OFFSET) == -1)
        err_exit("Can't read super block");

    struct fs_info *info = (struct fs_info *)calloc(1, sizeof(struct fs_info));
    if(!info)
        err_exit("Can't allocate memory for filesystem info");

    info->fd                = fd;
    info->first_data_block  = SB.s_first_data_block;
    info->inodes_per_group  = SB.s_inodes_per_group;
    info->inodes_count      = SB.s_inodes_count;
    info->inode_size        = SB.s_inode_size;
    info->block_size        = 1 << (SB.s_log_block_size + 10);
    info->group_size        = info->block_size * SB.s_blocks_per_group;

    return info;
}


void free_fs_info(struct fs_info *info) {
    
}


struct block_iter *block_iter_init(struct ext2_inode *inode, struct fs_info *info) {
    struct block_iter *biter = (struct block_iter *)calloc(1, sizeof(struct block_iter));
    if(!biter)
        err_exit("Can't allocate memory for block iterator");

    biter->info = info;
    biter->inode = inode;
    biter->curr_block_data = calloc(1, info->block_size);
    biter->next_block_idx = 0;

    return biter;
}


void block_iter_fini(struct block_iter *biter) {    
    biter->next_block_idx = 0;
    free(biter->curr_block_data);    
    free(biter);    
}


void *get_next_block(struct block_iter *biter) {
    struct fs_info *info = biter->info;
    struct ext2_inode *inode = biter->inode;
    unsigned next_block = biter->next_block_idx;

    if(next_block >= inode->i_blocks)
        return NULL;

    unsigned block_offset = 0;
    unsigned ppb = info->block_size / sizeof(unsigned); //  Pointers per block
    if(next_block < EXT2_NDIR_BLOCKS) {
        block_offset = inode->i_block[next_block] * info->block_size;

    //  It needs to be changed
    } else if(next_block < EXT2_NDIR_BLOCKS + ppb) {
        if(!inode->i_block[EXT2_IND_BLOCK])
            err_exit("Bad pointer to indirect blocks");

        unsigned ptr1 = read_ptr_from_block(inode->i_block[EXT2_IND_BLOCK],
                                            next_block - EXT2_IND_BLOCK,
                                            info);
        if(!ptr1)
            err_exit("Bad pointer in indirect pointers");

        block_offset = ptr1 * info->block_size;
    } else if(next_block < EXT2_NDIR_BLOCKS + ppb + ppb * ppb) {
        if(!inode->i_block[EXT2_DIND_BLOCK])
            err_exit("Bad pointer to double indirect blocks");

        unsigned ptr1 = read_ptr_from_block(inode->i_block[EXT2_DIND_BLOCK],
                                            (next_block - EXT2_IND_BLOCK - ppb) / ppb,
                                            info);
        if(!ptr1)
            err_exit("Bad pointer in the first level of double indirect pointers");

        unsigned ptr2 = read_ptr_from_block(ptr1,
                                            (next_block - EXT2_IND_BLOCK - ppb) % ppb,
                                            info);
        if(!ptr2)
            err_exit("Bad pointer in the second level of double indirect pointers");

        block_offset = ptr2 * info->block_size;
    } else if(next_block < EXT2_NDIR_BLOCKS + ppb + ppb * ppb + ppb * ppb * ppb) {
        if(!inode->i_block[EXT2_TIND_BLOCK])
            err_exit("Bad pointer to triple indirect blocks");

        unsigned ptr1 = read_ptr_from_block(inode->i_block[EXT2_TIND_BLOCK],
                                            (next_block - EXT2_IND_BLOCK - ppb - ppb * ppb) / (ppb * ppb),
                                            info);
        if(!ptr1)
            err_exit("Bad pointer in the first level of triple indirect pointers");

        unsigned ptr2 = read_ptr_from_block(ptr1,
                                            (next_block - EXT2_IND_BLOCK - ppb - ppb * ppb) % (ppb * ppb),
                                            info);
        if(!ptr2)
            err_exit("Bad pointer in the second level of triple indirect pointers");

        unsigned ptr3 = read_ptr_from_block(ptr2,
                                            (next_block - EXT2_IND_BLOCK - ppb - ppb * ppb) % ppb,
                                            info);
        if(!ptr3)
            err_exit("Bad pointer in the third level of triple indirect pointers");


        block_offset = ptr3 * info->block_size;
    } else {
        return NULL;
    }

    if(pread(info->fd, biter->curr_block_data, info->block_size, block_offset) == -1)
        err_exit("Can't read next block");

    biter->next_block_idx++;

    return biter->curr_block_data;
}


unsigned read_ptr_from_block(unsigned block_number,
                             unsigned ptr_idx, struct fs_info *info) {
    unsigned *ptrs = (unsigned *)calloc(info->block_size / sizeof(unsigned),
                                        sizeof(unsigned));

    if(pread(info->fd, ptrs, info->block_size, block_number * info->block_size) == -1)
        err_exit("Can't read ptr from block");

    unsigned ptr = ptrs[ptr_idx];
    free(ptrs);
    return ptr;
}


struct dentry_iter *dentry_iter_init(struct ext2_inode *inode, struct fs_info *info) {
    struct block_iter *biter = block_iter_init(inode, info);
    struct dentry_iter *diter = (struct dentry_iter *)calloc(1, sizeof(struct dentry_iter));
    if(!diter)
        err_exit("Can't allocate memory for dentry iterator");

    diter->biter = biter;
    diter->curr_dentry = (struct ext2_dir_entry_2 *)calloc(1, sizeof(struct ext2_dir_entry_2));
    diter->dir_size = biter->inode->i_size;
    diter->curr_offset = 0;

    return diter;
}


void dentry_iter_fini(struct dentry_iter *diter) {
    block_iter_fini(diter->biter);
    diter->curr_offset = 0;
    free(diter->curr_dentry);
    free(diter);
}


struct ext2_dir_entry_2 *get_next_dentry(struct dentry_iter *diter) {
    if(diter->curr_offset >= diter->dir_size)
        return NULL;

    if(diter->curr_offset >= diter->biter->info->block_size ||
       diter->curr_offset == 0) {
        if(!get_next_block(diter->biter))
            return NULL;
    }

    diter->curr_dentry = (struct ext2_dir_entry_2 *)(diter->biter->curr_block_data + diter->curr_offset);
    diter->curr_offset += diter->curr_dentry->rec_len;

    return diter->curr_dentry;
}


char *get_curr_dir(char *path) {
    int i = 1;
    while(path[i] != '/' && path[i] != '\0')
        i++;

    char *curr_dir_name = (char *)calloc(i + 1, sizeof(char));
    strncpy(curr_dir_name, &path[1], i - 1);
    curr_dir_name[i] = '\0';

    return curr_dir_name;
}

char *cut_path(char *path) {
    int i = 1;
    while(path[i] != '/' && path[i] != '\0')
        i++;

    if(path[i] == '\0')
        return NULL;

    strcpy(path, &path[i]);
    return path;
}


char *read_path() {
    printf("Enter path of directory to print in format:\n/dir_1/dir_2/dir_to_print/\n");

    int path_size = 32;
    char *path = (char *)calloc(path_size, sizeof(char));

    char c;
    int i = 0;
    while((c = getchar()) != '\n' && c != EOF) {
        if(i >= path_size) {
            path_size *= 2;
            char *new_path = (char *)calloc(path_size, sizeof(char));
            memcpy(new_path, path, path_size / 2);
            free(path);
            path = new_path;
        }
        path[i++] = c;
    }

    path[i] = '\0';
    return path;
}

