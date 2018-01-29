#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <linux/msdos_fs.h>
#include <string.h>
#include <unistd.h>


#define FAT_FILEPATH "../../fat16_img"

#define SEC_MASK        0b0000000000011111
#define MIN_MASK        0b0000011111100000
#define HOUR_MASK       0b1111100000000000
#define DAY_MASK        0b0000000000011111
#define MONTH_MASK      0b0000000111100000
#define YEAR_MASK       0b1111111000000000

#define FILENAME_LENGTH     8
#define EXTENSION_LENGTH    3
#define END_OF_CAT          0x00
#define DENTRY_IS_DIR       0x2E
#define INIT_OFFSET        -1

#define INDENT_1    16
#define INDENT_2    9
#define INDENT_3    27

#define err_exit(msg)    do {                    \
                             perror(msg);        \
                             exit(EXIT_FAILURE); \
                         } while (0)


struct fs_info {
    int fat_fd;

    unsigned short cluster_size;
    unsigned short data_offset;

    unsigned short *FAT_table;

    struct msdos_dir_entry *root_dir;
    unsigned short dir_entries;
};


struct dir_iter {
    struct fs_info *info;

    void *data;
    long offset;

    unsigned short cluster;
    unsigned int dentry_in_cluster;
};


struct file_iter {
    struct fs_info *info;
    void *data;
    unsigned short next_cluster;
};


struct fs_info *get_fs_info(int fd);
struct msdos_dir_entry *get_next_dentry(struct dir_iter *dir);
struct dir_iter *open_dir(struct msdos_dir_entry *dentry, struct fs_info *info);
struct msdos_dir_entry *find_dir_entry(struct dir_iter *dir_iter, char *dentry_name);
struct msdos_dir_entry *find_file(char *filepath, struct fs_info *info);
void *get_next_cluster(struct file_iter *fiter);
struct file_iter *open_file(struct msdos_dir_entry *dentry, struct fs_info *info);

char *get_filename(char *name);
char *read_filepath();
char *cut_filepath(char *filepath);
char *get_curr_dir_name(char *filepath);
int names_cmp(char *str1, char *str2);
void print_file(char *filepath, struct fs_info *info);


int main() {
    int fat_fd = open(FAT_FILEPATH, O_RDONLY);
    if(fat_fd == -1)
        err_exit("Can't open fat 16 file");

    struct fs_info *info = get_fs_info(fat_fd);

    char *filepath = read_filepath();

    print_file(filepath, info);

    return 0;
}


void print_file(char *filepath, struct fs_info *info) {
    struct msdos_dir_entry *fdentry = find_file(filepath, info);
    if(!fdentry)
        err_exit("Can't find file");

    struct file_iter *file_iter = open_file(fdentry, info);

    printf("%s:\n", filepath);
    char *curr_data = (char *)(get_next_cluster(file_iter));

    while(curr_data) {
        int i = 0;
        for(i = 0; i < info->cluster_size; ++i) {
            putchar(curr_data[i]);
        }

        curr_data = (char *)get_next_cluster(file_iter);
    }
    printf("\n");
}


struct msdos_dir_entry *find_file(char *filepath, struct fs_info *info) {
    char *curr_dir_name = get_curr_dir_name(filepath);
    struct dir_iter root_iter = { info, info->root_dir, 0, 0, info->dir_entries };
    struct dir_iter *curr_dir_iter = &root_iter;
    struct msdos_dir_entry *curr_dentry;

    while(curr_dir_name) {
        if(filepath[0] != '/')
            err_exit("Wrong filepath format");

        curr_dentry = find_dir_entry(curr_dir_iter, curr_dir_name);
        curr_dir_iter = open_dir(curr_dentry, info);

        filepath = cut_filepath(filepath);
        curr_dir_name = get_curr_dir_name(filepath);
    }

    char *filename = get_filename(filepath);
    curr_dentry = find_dir_entry(curr_dir_iter, filename);
    return curr_dentry;
}


struct msdos_dir_entry *find_dir_entry(struct dir_iter *dir_iter, char *dentry_name) {
    struct msdos_dir_entry *curr_dentry = get_next_dentry(dir_iter);

    while(curr_dentry && curr_dentry->name[0] != END_OF_CAT) {
        if(curr_dentry->name[0] == DELETED_FLAG || curr_dentry->name[0] == DENTRY_IS_DIR) {
            curr_dentry = get_next_dentry(dir_iter);
            continue;
        }

        if(!names_cmp(curr_dentry->name, dentry_name))
            return curr_dentry;

        curr_dentry = get_next_dentry(dir_iter);
    }

    return NULL;
}


void *get_next_cluster(struct file_iter *fiter) {
    if(fiter->next_cluster == EOF_FAT16 || fiter->next_cluster == FAT_ENT_FREE)
        return NULL;

    long offset = fiter->info->data_offset + (fiter->next_cluster - FAT_START_ENT) * fiter->info->cluster_size;
    if(pread(fiter->info->fat_fd, fiter->data, fiter->info->cluster_size, offset) == -1)
            err_exit("Can't read cluster");    

    fiter->next_cluster = fiter->info->FAT_table[fiter->next_cluster];

    return (void *)fiter->data;
}


struct file_iter *open_file(struct msdos_dir_entry *dentry, struct fs_info *info) {
    struct file_iter *new_fiter = (struct file_iter *)calloc(1, sizeof(struct file_iter));

    new_fiter->info = info;
    new_fiter->next_cluster = dentry->start;
    new_fiter->data = (void *)calloc(1, info->cluster_size);

    return new_fiter;
}


int names_cmp(char *str1, char *str2) {
    int len2 = strlen(str2);
    int miss_count = 0;

    for(int i = 0; i < len2; ++i)
        if(str1[i] != str2[i])
            miss_count++;

    if(miss_count)
        return 1;

    for(int i = len2; i < 11; ++i)
        if(str1[i] != ' ')
            miss_count++;

    return miss_count;
}


char *cut_filepath(char *filepath) {
    int i = 1;
    while(filepath[i] != '/' && filepath[i] != '\0')
        i++;

    strcpy(filepath, &filepath[i]);
    return filepath;
}


char *get_curr_dir_name(char *filepath) {
    int i = 1;
    while(filepath[i] != '/' && filepath[i] != '\0')
        i++;

    if(filepath[i] == '\0')
        return NULL;

    char *curr_dir_name = (char *)calloc(i + 1, sizeof(char));
    strncpy(curr_dir_name, &filepath[1], i - 1);
    curr_dir_name[i] = '\0';

    return curr_dir_name;
}

struct msdos_dir_entry *get_next_dentry(struct dir_iter *dir) {
    if(dir->offset == INIT_OFFSET) {
        long offset = dir->info->data_offset + (dir->cluster - FAT_START_ENT) * dir->info->cluster_size;
        if(pread(dir->info->fat_fd, dir->data, sizeof(struct msdos_dir_entry) * dir->dentry_in_cluster, offset) == -1)
                err_exit("Can't read cluster");        

        dir->offset = 0;
        return (struct msdos_dir_entry *)dir->data;
    }

    dir->offset++;
    if(dir->offset < dir->dentry_in_cluster)
        return ((struct msdos_dir_entry *)dir->data) + dir->offset;
    else {
        dir->cluster = dir->info->FAT_table[dir->cluster];
        if(dir->cluster == dir->info->FAT_table[1])
            return NULL;

        dir->offset = INIT_OFFSET;
        return get_next_dentry(dir);
    }
}


struct dir_iter *open_dir(struct msdos_dir_entry *dentry, struct fs_info *info) {
    struct dir_iter *new_diter = (struct dir_iter *)calloc(1, sizeof(struct dir_iter));

    new_diter->info = info;
    new_diter->dentry_in_cluster = info->cluster_size / sizeof(struct msdos_dir_entry);
    new_diter->cluster = dentry->start;
    new_diter->offset = INIT_OFFSET;
    new_diter->data = (void *)calloc(new_diter->dentry_in_cluster, sizeof(struct msdos_dir_entry));

    return new_diter;
}


struct fs_info *get_fs_info(int fd) {
    struct fat_boot_sector BS;
    if(pread(fd, &BS, sizeof(struct fat_boot_sector), 0) == -1)
        err_exit("Can't read the boot sector");

    unsigned short sector_size = __le16_to_cpu(*(__le16 *)BS.sector_size);
    unsigned short dir_entries = __le16_to_cpu(*(__le16 *)BS.dir_entries);

    unsigned short reserved_size = BS.reserved * sector_size;
    unsigned short FAT_table_size = BS.fat_length * sector_size;
    unsigned short *FAT_table = (unsigned short *)calloc(1, FAT_table_size);
    if(!FAT_table)
        err_exit("Can't allocate memory for FAT table");

    if(pread(fd, FAT_table, FAT_table_size, reserved_size) == -1)
        err_exit("Can't read FAT table");

    struct msdos_dir_entry *root_dir_entries = (struct msdos_dir_entry *)calloc(dir_entries, sizeof(struct msdos_dir_entry));
    if(!root_dir_entries)
        err_exit("Can't allocate memory for root dir reading");

    if(pread(fd, root_dir_entries, sizeof(struct msdos_dir_entry) * dir_entries, reserved_size + BS.fats * FAT_table_size) == -1)
        err_exit("Can't read the root directory");

    struct fs_info *info = (struct fs_info *)calloc(1, sizeof(struct fs_info));
    if(!info)
        err_exit("Can't allocate memory for fs info");

    info->fat_fd = fd;
    info->cluster_size = BS.sec_per_clus * sector_size;
    info->data_offset = reserved_size + FAT_table_size * BS.fats + dir_entries * sizeof(struct msdos_dir_entry);
    info->FAT_table = FAT_table;
    info->root_dir = root_dir_entries;
    info->dir_entries = dir_entries;

    return info;
}


char *get_filename(char *name) {
    char *filename = (char *)calloc(11, sizeof(char));
    if(!filename)
        err_exit("Can't allocate memory for filename");

    int len = 0;
    for(len = 0; len < 8; ++len)
        if(name[len + 1] == '.' || name[len + 1] == '\0')
            break;

    strncpy(filename, &name[1], len);

    for(int i = len; i < 8; ++i)
        strncat(filename, " ", 1);

    int ex_len = 0;
    if(name[len] == '.') {
        for(ex_len = 0; ex_len < 3; ++ex_len)
            if(name[len + 1 + ex_len] == '\0')
                break;

        strncat(filename, &name[len + 1], ex_len);
    }

    for(int i = ex_len; i < 3; ++i)
        strncat(filename, " ", 1);

    return filename;
}


char *read_filepath() {
    printf("Enter path of file to print in format:\n/dir_1/dir_2/file.txt\n");

    int path_size = 32;
    char *filepath = (char *)calloc(path_size, sizeof(char));

    char c;
    int i = 0;
    while((c = getchar()) != '\n' && c != EOF) {
        if(i >= path_size) {
            path_size *= 2;
            char *new_filepath = (char *)calloc(path_size, sizeof(char));
            memcpy(new_filepath, filepath, path_size / 2);
            free(filepath);
            filepath = new_filepath;
        }
        filepath[i++] = c;
    }

    filepath[i] = '\0';
    return filepath;
}
