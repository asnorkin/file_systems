#include <linux/msdos_fs.h>
#include <linux/kernel.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <time.h>
#include <errno.h>
#include <wchar.h>

#define MIN(x,y) (x<y ? x : y)

struct fat_info{
	unsigned short *fat_table;
	int fd;
	unsigned short cluster_size;
	unsigned short data_offset;
};

struct fat_dirent{
	struct fat_info fat_info;
	void *buffer;
	long current_offset;
	unsigned short cluster;
	unsigned int cluster_count;
};

char *get_name(struct msdos_dir_entry dir_entry){
	char *filename = (char *) calloc(13, 1);
	int char_index = 0;
	if (dir_entry.name[0] == 0x05){
		memcpy(filename, dir_entry.name + 1, 7);
		char_index = 6;
	}else{
		memcpy(filename, dir_entry.name, 8);
		char_index = 7;
	}
	while(filename[char_index] == ' '){
		filename[char_index] = '\0';
		char_index--;
	}
	if ((dir_entry.name[8] | dir_entry.name[9] | dir_entry.name[10]) != ' '){
		filename[++char_index] = '.';
		char_index++;
		memcpy(filename + char_index, dir_entry.name + 8, 3);
		char_index += 2;
		while(filename[char_index] == ' ') {
			filename[char_index] = '\0';
			char_index--;
		}
	}
	return filename;
}

struct tm get_creation_time(struct msdos_dir_entry dir_entry){
	struct tm time;
	//hhhhhmmm mmmxxxxx
	time.tm_hour = (dir_entry.ctime & 0xF800) >> 11;
	time.tm_min = (dir_entry.ctime & 0x07E0) >> 5;
	time.tm_sec = (dir_entry.ctime & 0x001F) * 2; //In FAT16 seconds are divided by 2

	//yyyyyyym mmmddddd
	time.tm_year = ((dir_entry.cdate & 0xFE00) >> 9) + 80; //Years in FAT16 are counted from 1980, in tm from 1900
	time.tm_mon = ((dir_entry.cdate & 0x01E0) >> 5) - 1; //Month 8 in FAT16 is 9th month in tm
	time.tm_mday = (dir_entry.cdate & 0x001F);
	return time;
}

struct tm get_access_time(struct msdos_dir_entry dir_entry){
	struct tm time;
	//hhhhhmmm mmmxxxxx
	time.tm_hour = (dir_entry.time & 0xF800) >> 11;
	time.tm_min = (dir_entry.time & 0x07E0) >> 5;
	time.tm_sec = (dir_entry.time & 0x001F) * 2; //In FAT16 seconds are divided by 2

	//yyyyyyym mmmddddd
	time.tm_year = ((dir_entry.adate & 0xFE00) >> 9) + 80; //Years in FAT16 are counted from 1980, in tm from 1900
	time.tm_mon = ((dir_entry.adate & 0x01E0) >> 5) - 1; //Month 8 in FAT16 is 9th month in tm
	time.tm_mday = (dir_entry.adate & 0x001F);
	return time;
}

int print_file(struct msdos_dir_entry dir_entry, struct fat_info fat_info){
	unsigned short *fat_table = fat_info.fat_table;
	int fd = fat_info.fd;
	unsigned short cluster_size = fat_info.cluster_size;
	unsigned short data_offset = fat_info.data_offset;

	unsigned short cluster = 0xFFFF;

	void *buffer = malloc(cluster_size);
	ssize_t ret = -1;

	ssize_t file_size = __le32_to_cpu(dir_entry.size);
	for (cluster = __le16_to_cpu(dir_entry.start); cluster != 0xFFFF && file_size != 0; cluster = fat_table[cluster]){
		unsigned cluster_offset = data_offset + (cluster - 2) * cluster_size;
		lseek(fd, cluster_offset, SEEK_SET);
		ret = read(fd, buffer, cluster_size);
		if (ret != cluster_size) return ret;

		ssize_t write_size = MIN(cluster_size, file_size);
		ret = write(STDOUT_FILENO, buffer, write_size);
		file_size -= write_size;
		if (ret != write_size) return ret;
	}
	free(buffer);
	if (cluster != 0xFFFF) return -1;
	if (file_size != 0) return -2;
	return 0;
}

int fat_open_dirent(struct msdos_dir_entry base_dir_entry, struct fat_dirent *new_dir, struct fat_info fat_info){
	unsigned short cluster_size = fat_info.cluster_size;

	new_dir -> fat_info = fat_info;
	new_dir -> buffer = malloc(cluster_size);
	if (!new_dir -> buffer) return -1;
	new_dir -> current_offset = -1;
	new_dir -> cluster = base_dir_entry.start;
	new_dir -> cluster_count = cluster_size / sizeof(struct msdos_dir_entry);

	return 0;
}

int fat_close_dirent(struct fat_dirent dirent){
	free(dirent.buffer);
	return 0;
}

//Returns next dir_entry or NULL if failed
//If called after NULL returned, behaviour is unexpected
struct msdos_dir_entry *fat_next_dir_entry(struct fat_dirent *dirent){
	//Check if offset is available

	if (dirent -> current_offset != -1) {
		//Offset is available, but is there any more stuff in offset?
		dirent -> current_offset++;
		if (dirent -> current_offset < dirent -> cluster_count) {
			//There more records in current buffer -- returning it
			return (struct msdos_dir_entry *) (dirent -> buffer) + dirent -> current_offset;
		}
		//Setting up new cluster
		dirent -> cluster = dirent -> fat_info.fat_table[dirent -> cluster];
		if (dirent -> cluster == 0xFFFF) return NULL;
	}

	//No records available, generate new ones
	unsigned short cluster_size = dirent -> fat_info.cluster_size;
	int fd = dirent -> fat_info.fd;
	unsigned short data_offset = dirent -> fat_info.data_offset;

	unsigned cluster_offset = data_offset + (dirent -> cluster - 2) * cluster_size;
	lseek(fd, cluster_offset, SEEK_SET);
	int ret = read(fd, dirent -> buffer, cluster_size);
	if (ret != cluster_size) return NULL;

	dirent -> current_offset = 0;
	return (struct msdos_dir_entry *) dirent -> buffer;
}

void traverse_dirent(struct fat_dirent* dirent, char *needle, char *prefix, struct fat_info fat_info){
	struct msdos_dir_entry *dir_entry_ptr;
	for (dir_entry_ptr = fat_next_dir_entry(dirent); dir_entry_ptr != NULL && dir_entry_ptr -> name[0] != 0x00; dir_entry_ptr = fat_next_dir_entry(dirent)){
		//printf("---%ld---", dirent -> current_offset);
		struct msdos_dir_entry dir_entry = *dir_entry_ptr;
		//struct msdos_dir_entry dir_entry = dir_entries[dir_num];
		if (dir_entry.name[0] == 0x2e) continue;
		if (dir_entry.name[0] == 0xe5) continue; //File was deleted
		if (dir_entry.attr & 0x40) continue; //Wrong file
		if (dir_entry.attr & 0x80) continue; //Wrong file
		if (dir_entry.attr == 0xF) continue; //TODO: Process files with strange attributes

		char *filename = get_name(dir_entry);

		if (needle == NULL)
			printf("%s%-12s | ", prefix, filename);
		if (dir_entry.attr & 0x10){
			putchar('\n');
			struct fat_dirent *dir = (struct fat_dirent*) malloc(sizeof(struct fat_dirent));
			fat_open_dirent(dir_entry, dir, fat_info);

			int prefix_len = strlen(prefix);
			char *new_prefix = (char *) calloc(prefix_len + 2, 1);
			strcpy(new_prefix, prefix);
			new_prefix[prefix_len] = '\t';
			traverse_dirent(dir, needle, new_prefix, fat_info);
			fat_close_dirent(*dir);
			free(new_prefix);
			free(dir);
			free(filename);
			continue;
		}
		if (needle == NULL){
			if (dir_entry.attr & 0x01) putchar('R'); else putchar(' ');
			if (dir_entry.attr & 0x02) putchar('H'); else putchar(' ');
			if (dir_entry.attr & 0x04) putchar('S'); else putchar(' ');
			if (dir_entry.attr & 0x08) putchar('Y'); else putchar(' ');
			if (dir_entry.attr & 0x20) putchar('M'); else putchar(' ');
			printf(" | ");

			struct tm creation_time = get_creation_time(dir_entry);
			printf("%.24s | ", asctime(&creation_time));
			struct tm access_time = get_access_time(dir_entry);
			printf("%.24s\n", asctime(&access_time));
			free(filename);
		}else{
			if (!strcmp(filename, needle)) {
				int ret = print_file(dir_entry, fat_info);
				if (ret) fprintf(stderr, "Error while reading file: %s (%d)\n", strerror(errno), ret);
				free(filename);
				return;
			}
		}
	}
}

int main(int argc, char *argv[]){
	if (argc < 2){
		printf("Usage: %s IMAGE [FILE]\n", argv[0]);
		puts("    IMAGE - FAT16 image\n");
		puts("    Not providing argument FILE, program will print out all files\n");
		puts("    File attributes are printed after file\n");
		puts("    R - Read Only, H - hidden file, S - system file\n");
		puts("    Y - Special Entry, D - Directory, M - Modified Flag\n");
		printf("Usage: %s IMAGE [FILE]", argv[0]);
		printf("Usage: %s IMAGE [FILE]", argv[0]);
		exit(1);
	}
	int fd = open(argv[1], O_RDONLY);

	struct fat_boot_sector boot_sector;
	read(fd, &boot_sector, sizeof(boot_sector));

	unsigned short sector_size = __le16_to_cpu(*(__le16 *)boot_sector.sector_size);
	unsigned short dir_entries_count = __le16_to_cpu(*(__le16 *)boot_sector.dir_entries);
	unsigned short cluster_size = boot_sector.sec_per_clus * sector_size;
	unsigned short reserved_size = boot_sector.reserved * sector_size;

	unsigned short FAT_table_size = boot_sector.fat_length * boot_sector.fats * sector_size;
	unsigned short root_directory_size = dir_entries_count * sizeof(struct msdos_dir_entry);

	unsigned short data_offset = reserved_size + FAT_table_size + root_directory_size;

	lseek(fd, reserved_size, SEEK_SET);
	unsigned short *fat_table = (unsigned short *) malloc(FAT_table_size);
	read(fd, fat_table, FAT_table_size);

	struct msdos_dir_entry *dir_entries = (struct msdos_dir_entry *) malloc(root_directory_size);
	read(fd, dir_entries, root_directory_size);

	struct fat_info fat_info = {
		.fat_table = fat_table,
		.fd = fd,
		.cluster_size = cluster_size,
		.data_offset = data_offset
	};

	//Setup root directory by hands, it is not required for FAT32, only FAT16
	struct fat_dirent root_dirent = {
		.fat_info = fat_info,
		.buffer = dir_entries,
		.current_offset = 0,
		.cluster = 0,
		.cluster_count = dir_entries_count
	};

	if (argc < 3)
		traverse_dirent(&root_dirent, NULL, (char *)"", fat_info);
	else
		traverse_dirent(&root_dirent, argv[2], (char *)"", fat_info);

	free(fat_table);
	free(dir_entries);

	if (argc < 2)
	return 0;
}
