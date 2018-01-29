#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <linux/msdos_fs.h>
#include <string.h>


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
    FILE *file;

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


struct fs_info *get_fs_info(FILE *file);

char *get_attributes(unsigned char attr);
char *get_filename(char *name);
struct tm *get_ctime(struct msdos_dir_entry *dentry);
struct tm *get_atime(struct msdos_dir_entry *dentry);

struct msdos_dir_entry *get_next_dentry(struct dir_iter *dir);
struct dir_iter *open_dir(struct msdos_dir_entry *dentry, struct fs_info *info);

void print_dir_entry(struct msdos_dir_entry *dentry, struct fs_info *info, int dir_level);
void print_dir(struct dir_iter *dir, int dir_level);


int main() {
    FILE *file = fopen(FAT_FILEPATH, "r");
    if(!file)
        err_exit("Can't open fat 16 file");

    struct fs_info *info = get_fs_info(file);
    //printf("%d\n", info->FAT_table);

    printf("%-*s%-*s%-*s%-*s\n\n", INDENT_1, "FILE", INDENT_2, "ATTRIB", INDENT_3, "CREATION_TIME", INDENT_3, "ACCESS_TIME");
    struct dir_iter root_iter = { info, info->root_dir, 0, 0, info->dir_entries };
    print_dir(&root_iter, 0);

    return 0;
}


void print_dir(struct dir_iter *dir, int dir_level) {
    struct msdos_dir_entry *curr_dentry = get_next_dentry(dir);

    while(curr_dentry && curr_dentry->name[0] != END_OF_CAT) {
        if(curr_dentry->name[0] == DELETED_FLAG|| curr_dentry->name[0] == DENTRY_IS_DIR) {
            curr_dentry = get_next_dentry(dir);
            continue;
        }

        print_dir_entry(curr_dentry, dir->info, dir_level);

        curr_dentry = get_next_dentry(dir);
    }
}


struct msdos_dir_entry *get_next_dentry(struct dir_iter *dir) {
    if(dir->offset == INIT_OFFSET) {
        long offset = dir->info->data_offset + (dir->cluster - FAT_START_ENT) * dir->info->cluster_size;
        if(fseek(dir->info->file, offset, SEEK_SET) == -1)
            err_exit("Can't seek cluster offset");

        if(!fread(dir->data, sizeof(struct msdos_dir_entry), dir->dentry_in_cluster, dir->info->file))
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


struct fs_info *get_fs_info(FILE *file) {
    struct fat_boot_sector BS;
    if(!fread(&BS, sizeof(struct fat_boot_sector), 1, file))
        err_exit("Can't read the boot sector");

    unsigned short sector_size = __le16_to_cpu(*(__le16 *)BS.sector_size);
    unsigned short dir_entries = __le16_to_cpu(*(__le16 *)BS.dir_entries);

    unsigned short reserved_size = BS.reserved * sector_size;
    if(fseek(file, reserved_size, SEEK_SET) == -1)
        err_exit("Can't seek FAT table offset");

    unsigned short FAT_table_size = BS.fat_length * sector_size;
    unsigned short *FAT_table = (unsigned short *)calloc(1, sizeof(FAT_table_size));
    if(!FAT_table)
        err_exit("Can't allocate memory for FAT table");

    if(!fread(FAT_table, sizeof(FAT_table_size), 1, file))
        err_exit("Can't read FAT table");

    if(fseek(file, BS.fats * FAT_table_size + reserved_size, SEEK_SET) == -1)
        err_exit("Can't seek root offset");

    struct msdos_dir_entry *root_dir_entries = (struct msdos_dir_entry *)calloc(dir_entries, sizeof(struct msdos_dir_entry));
    if(!root_dir_entries)
        err_exit("Can't allocate memory for root dir reading");

    if(!fread(root_dir_entries, sizeof(struct msdos_dir_entry), dir_entries, file))
        err_exit("Can't read the root directory");

    struct fs_info *info = (struct fs_info *)calloc(1, sizeof(struct fs_info));
    if(!info)
        err_exit("Can't allocate memory for fs info");    

    info->file = file;
    info->cluster_size = BS.sec_per_clus * sector_size;
    info->data_offset = reserved_size + FAT_table_size * BS.fats + dir_entries * sizeof(struct msdos_dir_entry);
    info->FAT_table = FAT_table;
    info->root_dir = root_dir_entries;    
    info->dir_entries = dir_entries;

    return info;
}


void print_dir_entry(struct msdos_dir_entry *dentry, struct fs_info *info, int dir_level) {
    int lvl_indent = dir_level * 2;
    if(dentry->attr & ATTR_DIR)
        printf("%-*s%-*.8s", lvl_indent, "", INDENT_1 - lvl_indent, dentry->name);
    else
        printf("%-*s%-*s", lvl_indent, "", INDENT_1 - lvl_indent, get_filename((char *)dentry->name));

    printf("%-*s", INDENT_2, get_attributes(dentry->attr));
    printf("%-*.24s%-*.24s\n", INDENT_3, asctime(get_ctime(dentry)),
                               INDENT_3, asctime(get_ctime(dentry)));

    if(dentry->attr & ATTR_DIR)
        print_dir(open_dir(dentry, info), dir_level + 1);
}


char *get_filename(char *name) {
    char *filename = (char *)calloc(13, sizeof(char));
    if(!filename)
        err_exit("Can't allocate memory for filename");

    for(int i = 0; i < 8; ++i) {
        if(name[i] != ' ')
            strncpy(filename, &name[i], 1);
    }

    strncat(filename, ".", 1);
    strncat(filename, &name[8], 3);

    return filename;
}


char *get_attributes(unsigned char attr) {
    char *output = (char *)calloc(7, sizeof(char));

    strcat(output, attr & ATTR_RO ? "R" : "-");
    strcat(output, attr & ATTR_HIDDEN ? "H" : "-");
    strcat(output, attr & ATTR_SYS ? "S" : "-");
    strcat(output, attr & ATTR_VOLUME ? "V" : "-");
    strcat(output, attr & ATTR_DIR ? "D" : "-");
    strcat(output, attr & ATTR_ARCH ? "A" : "-");

    return output;
}


struct tm *get_atime(struct msdos_dir_entry *dentry) {
    struct tm *dt = (struct tm *)calloc(1, sizeof(struct tm));

    dt->tm_sec  = (dentry->time & SEC_MASK) * 2;
    dt->tm_min  = (dentry->time & MIN_MASK) >> 5;
    dt->tm_hour = (dentry->time & HOUR_MASK) >> 11;

    dt->tm_mday = (dentry->adate & DAY_MASK);
    dt->tm_mon  = ((dentry->adate & MONTH_MASK) >> 5) - 1;
    dt->tm_year = ((dentry->adate & YEAR_MASK) >> 9) + 80;

    return dt;
}


struct tm *get_ctime(struct msdos_dir_entry *dentry) {
    struct tm *dt = (struct tm *)calloc(1, sizeof(struct tm));

    dt->tm_sec  = (dentry->ctime & SEC_MASK) * 2;
    dt->tm_min  = (dentry->ctime & MIN_MASK) >> 5;
    dt->tm_hour = (dentry->ctime & HOUR_MASK) >> 11;

    dt->tm_mday = (dentry->cdate & DAY_MASK);
    dt->tm_mon  = ((dentry->cdate & MONTH_MASK) >> 5) - 1;
    dt->tm_year = ((dentry->cdate & YEAR_MASK) >> 9) + 80;

    return dt;
}

