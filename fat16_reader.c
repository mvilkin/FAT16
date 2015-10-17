#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#define bool int
#define true 1
#define false 0
#define min(x,y) (((x)<(y))?(x):(y))
#define MAX_PATH_LENGTH 256
#define FILE_IMG_PATH "timg1.IMA"

typedef enum {
    NOTHING,
    DELETED,
    DIRECTORY,
    REGFILE
} record_type_t;

typedef enum {
    NOPE,
    EXIT,
    HELP,
    LS,
    CAT
} instruction_t;

typedef enum {
    WRONG_PATH,
    WRONG_INSTRUCTION,
    WRONG_COMMAND
} error_t;

typedef struct {
    instruction_t type;
    char path[MAX_PATH_LENGTH];
} command_t;

typedef struct {
    size_t bytes_per_sector;
    size_t sectors_per_cluster;
    size_t reserved_sectors;
    size_t number_of_fats;
    size_t root_entries;
    size_t sectors_per_fat;
} boot_sector_t;

typedef struct {
    size_t offset_to_fat;
    size_t size_of_fat;
    size_t offset_to_root_dir;
    size_t size_of_root_dir;
    size_t offset_to_clusters;
    size_t size_of_cluster;
} fs_info_t;

typedef struct {
    unsigned char name[9];
    unsigned char ext[4];
    unsigned char full_name[14];
    unsigned char attr;
    size_t begin_cluster;
    size_t size;
    record_type_t type;
} dir_record_t;


void init_reader(int fd);
void deinit_reader();
void read_boot_sector(int fd, boot_sector_t * bsector);
void count_fs_info(boot_sector_t * bsector);
void read_dir(int fd, size_t offset, char * path, instruction_t instr);
void read_dir_record(int fd, size_t offset, dir_record_t * record);
void print_name(dir_record_t * record);
void print_dir(int fd, size_t num_cluster);
void print_file(int fd, size_t num_cluster, size_t file_size);
void print_file_cluster(int fd, size_t offset, size_t size);
void print_file_cluster(int fd, size_t offset, size_t file_size);
void get_name_from_path(char * name, char * path);
void remove_name_from_path(char * path);
size_t offset_of_cluster(size_t cluster_num);
size_t cluster_of_offset(size_t offset);
size_t number_of_dir_records(size_t num_cluster);
void get_command(command_t * command);
instruction_t get_instruction_from_string(char * string);
void print_help();
void print_error(error_t type);
size_t find_num_cluster_by_path(int fd, size_t num_entry_cluster, char * path, record_type_t * type);
bool find_name_in_cluster(int fd, size_t offset, char * name, dir_record_t * record);

fs_info_t * fs_info;


int main(int argc, char * argv[]) {
     if (strcmp(FILE_IMG_PATH, "NAME.IMG") == 0) {
        printf("Need to define path to image file.\n");
        return 0;
    }
    char image_name[] = FILE_IMG_PATH;
    int image_fd = open(image_name, O_RDONLY);
    init_reader(image_fd);
    command_t * command = (command_t*)calloc(1, sizeof(command_t));
    record_type_t current_type;
    size_t current_cluster;

    while (1) {
        printf("> ");
        get_command(command);
        switch (command->type) {
        case HELP:
            print_help();
            break;
        case LS:
            current_cluster = find_num_cluster_by_path(image_fd, 1, command->path, &current_type);
            if (current_cluster == 0) {
                print_error(WRONG_PATH);
                break;
            }
            if (current_type == DIRECTORY)
                print_dir(image_fd, current_cluster);
            break;
        case CAT:
            read_dir(image_fd, fs_info->offset_to_root_dir, command->path, command->type);
        case EXIT:
        default:
            break;
        }
        if (command->type == EXIT)
            break;
    }

    free(command);
    deinit_reader();
    close(image_fd);
    return 0;
}

void get_command(command_t * command) {
    char input_string[MAX_PATH_LENGTH];
    fgets(input_string, MAX_PATH_LENGTH, stdin);
    input_string[strlen(input_string) - 1] = '\0';
    int i;
    for (i = 0; i < strlen(input_string); i++)
        input_string[i] = toupper(input_string[i]);
    command->type = get_instruction_from_string(input_string);
    strcpy(command->path, input_string);
}

instruction_t get_instruction_from_string(char * string) {
    char instr[5];
    int offset = 0;
    while (string[offset] != ' ' && offset < 4) {
        instr[offset] = string[offset];
        offset++;
    }
    instr[offset] = '\0';
    offset++;
    if (offset == strlen(string))
        string[0] = '\0';
    else {
        int i = 0;
        while (i + offset < strlen(string)) {
            string[i] = string[i + offset];
            i++;
        }
        string[i] = '\0';
    }
    if (strcmp(instr, "CAT") == 0)
        return CAT;
    if (strcmp(instr, "LS") == 0)
        return LS;
    if (strcmp(instr, "HELP") == 0)
        return HELP;
    if (strcmp(instr, "EXIT") == 0)
        return EXIT;
    print_error(WRONG_INSTRUCTION);
    return NOPE;
}

void print_error(error_t type) {
    switch(type) {
    case WRONG_PATH:
        printf("ERROR: Wrong path.\n");
        break;
    case WRONG_INSTRUCTION:
        printf("ERROR: Wrong instruction.\n");
        break;
    case WRONG_COMMAND:
        printf("ERROR: Wrong command.\n");
        break;
    default:
        printf("ERROR: Unknown error.\n");
        break;
    }
}

void print_help() {
    printf("HELP:\n");
    printf("command 'ls <path>' to show directory (if path is absent, show root directory).\n");
    printf("command 'cat <path>' to show file.\n");
    printf("command 'help' to show manual.\n");
    printf("command 'exit' to finish usage.\n");
}

bool find_name_in_cluster(int fd, size_t offset, char * name, dir_record_t * record) {
    size_t num_records = number_of_dir_records(cluster_of_offset(offset));
    while (num_records-- > 0) {
        read_dir_record(fd, offset, record);
        if (strcmp(name, record->full_name) == 0 && (record->type == DIRECTORY || record->type == REGFILE)) {
            return true;
        }
        offset += 32;
    }
    return false;
}

size_t find_num_cluster_by_path(int fd, size_t num_entry_cluster, char * path, record_type_t * type) {
    if (path[0] == 0 && num_entry_cluster == 1) {
        *type = DIRECTORY;
        return 1;
    }
    dir_record_t * record = (dir_record_t *)calloc(1, sizeof(dir_record_t));
    char * name = (char *)calloc(14, sizeof(char));
    get_name_from_path(name, path);
    int num = num_entry_cluster;
    bool path_is_right = false;
    do {
        if (find_name_in_cluster(fd, offset_of_cluster(num), name, record)) {
            path_is_right = true;
            break;
        }
        lseek(fd, fs_info->offset_to_fat + num * 2, SEEK_SET);
        read(fd, &num, 2);
    } while (0x0002 <= num && num <= 0xFFEF);
    if (!path_is_right)
        return 0;
    remove_name_from_path(path);
    int next_entry_cluster = record->begin_cluster;
    record_type_t current_type = record->type;
    free(name);
    free(record);
    if (strlen(path) == 0) {
        *type = current_type;
        return next_entry_cluster;
    } else
        return find_num_cluster_by_path(fd, record->begin_cluster, path, type);
}

void read_dir(int fd, size_t offset, char * path, instruction_t instr) {
    if (path[0] == 0 && offset == fs_info->offset_to_root_dir && instr == LS) {
        print_dir(fd, 1);
        return;
    }
    dir_record_t * record = (dir_record_t *)calloc(1, sizeof(dir_record_t));
    char * name = (char *)calloc(14, sizeof(char));
    bool path_is_right = false;
    get_name_from_path(name, path);
    size_t num_records = number_of_dir_records(cluster_of_offset(offset));
    while (num_records-- > 0) {
        read_dir_record(fd, offset, record);
        if (strcmp(name, record->full_name) == 0 && (record->type == DIRECTORY || record->type == REGFILE)) {
            remove_name_from_path(path);
            path_is_right = true;
            if (strlen(path) == 0) {
                // TODO: save data in buffers and print after
                switch(instr) {
                case LS:
                    if (record->type == DIRECTORY)
                        // TODO: directory for many clusters
                        // this code is only for 1 dir
                        print_dir(fd, record->begin_cluster);
                    else
                        print_error(WRONG_COMMAND);
                    break;
                case CAT:
                    if (record->type == REGFILE)
                        print_file(fd, record->begin_cluster, record->size);
                    else
                        print_error(WRONG_COMMAND);
                    break;
                default:
                    break;
                }
            } else {
                read_dir(fd, offset_of_cluster(record->begin_cluster), path, instr);
            }
        }
        offset += 32;
    }
    if (!path_is_right)
        print_error(WRONG_PATH);
    free(name);
    free(record);
}


void print_dir(int fd, size_t num_cluster) {
    size_t num_records = number_of_dir_records(num_cluster);
    dir_record_t * record = (dir_record_t *)calloc(1, sizeof(dir_record_t));
    size_t offset = offset_of_cluster(num_cluster);
    while (num_records-- > 0) {
        read_dir_record(fd, offset, record);
        print_name(record);
        offset += 32;
    }
    free(record);
}

void print_file(int fd, size_t num_cluster, size_t file_size) {
    int num = num_cluster;
    do {
        print_file_cluster(fd, offset_of_cluster(num), min(file_size, fs_info->size_of_cluster));
        file_size -= fs_info->size_of_cluster;
        lseek(fd, fs_info->offset_to_fat + num * 2, SEEK_SET);
        read(fd, &num, 2);
    } while (0x0002 <= num && num <= 0xFFEF);
    printf("\n");
}

void print_file_cluster(int fd, size_t offset, size_t size) {
    lseek(fd, offset, SEEK_SET);
    unsigned char symbol;
    while (size-- > 0) {
        read(fd, &symbol, 1);
        printf("%c", symbol);
    }
}

void print_name(dir_record_t * record) {
    switch(record->type) {
    case DIRECTORY:
        printf("%s/\n", record->full_name);
        break;
    case REGFILE:
        printf("%s\n", record->full_name);
        break;
    default:
        break;
    }
}

void read_dir_record(int fd, size_t offset, dir_record_t * record) {
    lseek(fd, offset, SEEK_SET);
    read(fd, &record->name, 8);
    read(fd, &record->ext, 3);
    read(fd, &record->attr, 1);
    lseek(fd, offset + 0x1A, SEEK_SET);
    read(fd, &record->begin_cluster, 2);
    read(fd, &record->size, 4);

    record->name[8] = '\0';
    while (strlen(record->name) && record->name[strlen(record->name) - 1] == ' ')
        record->name[strlen(record->name) - 1] = '\0';
    record->ext[3] = '\0';
    while (strlen(record->ext) && record->ext[strlen(record->ext) - 1] == ' ')
        record->ext[strlen(record->ext) - 1] = '\0';

    if (record->name[0] == 0x0) {
        record->type = NOTHING;
    } else {
        if (record->name[0] == 0xE5) {
            record->type = DELETED;
        } else {
            if (record->name[0] == 0x05)
                record->name[0] = 0xE5;
            if (record->attr == 0x10) {
                record->type = DIRECTORY;
            } else {
                record->type = REGFILE;
            }
        }
    }

    int i = 0;
    while (i < strlen(record->name)) {
        record->full_name[i] = record->name[i];
        i++;
    }
    int j = 0;
    if (record->type == REGFILE && strlen(record->ext) > 0) {
        record->full_name[i++] = '.';
        while (j < strlen(record->ext)) {
            record->full_name[i+j] = record->ext[j];
            j++;
        }
    }
    record->full_name[i+j] = '\0';
}

void remove_name_from_path(char * path) {
    int offset = 0;
    while (offset < strlen(path) && path[offset] != '/') {
        offset++;
    }
    offset++;
    if (offset == strlen(path))
        path[0] = '\0';
    else {
        int i = 0;
        while (i + offset < strlen(path)) {
            path[i] = path[i + offset];
            i++;
        }
        path[i] = '\0';
    }
}

void get_name_from_path(char * name, char * path) {
    int i = 0;
    while (path[i] != '/' && i < strlen(path)) {
        name[i] = path[i];
        i++;
    }
    name[i] = 0;
}

size_t cluster_of_offset(size_t offset) {
    return (offset == fs_info->offset_to_root_dir) ? 1 : ((offset - fs_info->offset_to_clusters) / fs_info->size_of_cluster + 2);
}

size_t offset_of_cluster(size_t cluster_num) {
    return (cluster_num == 1) ? (fs_info->offset_to_root_dir)
                              : (fs_info->offset_to_clusters + (cluster_num - 2) * fs_info->size_of_cluster);
}

size_t number_of_dir_records(size_t num_cluster) {
    return (num_cluster == 1) ? (fs_info->size_of_root_dir / 32) : (fs_info->size_of_cluster / 32);
}

void count_fs_info(boot_sector_t * bsector) {
    fs_info->offset_to_fat = bsector->reserved_sectors * bsector->bytes_per_sector;
    fs_info->size_of_fat = bsector->sectors_per_fat * bsector->bytes_per_sector;
    fs_info->offset_to_root_dir = fs_info->offset_to_fat + bsector->number_of_fats * fs_info->size_of_fat;
    fs_info->size_of_root_dir = bsector->root_entries * 32;
    fs_info->offset_to_clusters = fs_info->offset_to_root_dir + fs_info->size_of_root_dir;
    fs_info->size_of_cluster = bsector->sectors_per_cluster * bsector->bytes_per_sector;
}

void read_boot_sector(int fd, boot_sector_t * bsector) {
    lseek(fd, 0xB, SEEK_SET);
    read(fd, &bsector->bytes_per_sector, 2);
    lseek(fd, 0xD, SEEK_SET);
    read(fd, &bsector->sectors_per_cluster, 1);
    lseek(fd, 0xE, SEEK_SET);
    read(fd, &bsector->reserved_sectors, 2);
    lseek(fd, 0x10, SEEK_SET);
    read(fd, &bsector->number_of_fats, 1);
    lseek(fd, 0x11, SEEK_SET);
    read(fd, &bsector->root_entries, 2);
    lseek(fd, 0x16, SEEK_SET);
    read(fd, &bsector->sectors_per_fat, 2);
}

void deinit_reader() {
    free(fs_info);
}

void init_reader(int fd) {
    boot_sector_t * boot_sector = (boot_sector_t *)calloc(1, sizeof(boot_sector_t));
    read_boot_sector(fd, boot_sector);
    fs_info = (fs_info_t *)calloc(1, sizeof(fs_info_t));
    count_fs_info(boot_sector);
    free(boot_sector);
}
