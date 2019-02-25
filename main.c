#define	FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>


#define	BLOCK_SIZE 512
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))
#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(long))


char disk_path[100] = "/home/tance/CLionProjects/filesystem/disk";
struct u_fs_directory_entry
{
    int nFiles;
    struct u_fs_file_dir
    {
        char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
        char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
        size_t fsize;					//file size
        long nStartBlock;				//where the first block is on disk
    } __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

    //This is some space to get this to be exactly the size of the disk block.
    //Don't use it for anything.
    char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct u_fs_file_dir) - sizeof(int)];
};

typedef struct u_fs_directory_entry u_fs_directory_entry;


struct u_fs_root_directory
{
    int nDirectories;
    struct u_fs_dir
    {
        char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
        long nStartBlock;				//where the directory block is on disk
    } __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

    //This is some space to get this to be exactly the size of the disk block.
    //Don't use it for anything.
    char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct u_fs_dir) - sizeof(int)];
};

typedef struct u_fs_root_directory u_fs_root_directory;


struct fs_disk_block
{
    //The next disk block, if needed. This is the next pointer in the linked
    //allocation list
    long nNextBlock;

    //And all the rest of the space in the block can be used for actual data
    //storage.
    char data[MAX_DATA_IN_BLOCK];
};

typedef struct fs_disk_block u_fs_disk_block;


char bitmap_block1[BLOCK_SIZE];
char bitmap_block2[BLOCK_SIZE];
u_fs_root_directory* root;

static int u_fs_read_disk(int block, void *buf);
static int u_fs_write_disk(int block, void *buf);
static void u_fs_deletedata(int block);
static int u_fs_getblock(char* bitmap_block);
static int u_fs_getnullBlock();


static int u_fs_read_disk(int block, void *buf){
    int result;
    FILE* disk = fopen(disk_path, "rb+");
    fseek(disk, (BLOCK_SIZE*block), SEEK_SET);
    result = fread(buf, BLOCK_SIZE, 1, disk);
    fclose(disk);
    return result;

}

static int u_fs_write_disk(int block, void *buf){
    int result;
    FILE* disk = fopen(disk_path, "rb+");
    fseek(disk, BLOCK_SIZE*block, SEEK_SET);
    result = fwrite(buf, BLOCK_SIZE, 1, disk);
    fclose(disk);
    return result;
}

static void u_fs_deletedata(int block){

    char Bytes[BLOCK_SIZE];
    int x_pos,y_pos;
    char target,ele;
    int layer = 0;

    for (int i = 0; i < BLOCK_SIZE; i++){
        Bytes[i] = 0;
    }
    u_fs_write_disk(block, Bytes);

    if (block >= (BLOCK_SIZE * 8)){
        layer = 1;
        block = block - (BLOCK_SIZE * 8);
    }

    x_pos = block / 8;
    y_pos = block - (8 * x_pos);

    if(y_pos == 0) target = 254;
        else if(y_pos == 1) target = 253;
            else if(y_pos == 2) target = 251;
                else if(y_pos == 3) target = 247;
                    else if(y_pos == 4) target = 239;
                        else if(y_pos == 5) target = 223;
                            else if(y_pos == 6) target = 191;
                                else target = 127;

    if (!layer){
        ele = bitmap_block1[x_pos];
        bitmap_block1[x_pos] = ele & target;
        u_fs_write_disk(1, bitmap_block1);
    }

    else{
        ele = bitmap_block2[x_pos];
        bitmap_block2[x_pos] = ele & target;
        u_fs_write_disk(2, bitmap_block2);
    }

}

static int u_fs_getblock(char* bitmap_block){

    int i,j,freeBlock,layer = 1;
    char element;
    char ONE = 1;
    char target;
    for (i = 0; i < BLOCK_SIZE; i++){
        element = bitmap_block[i];
        target = 1;
        for (j = 0; j < 8; j++){
            layer = (ONE & element);
            if (!layer){
                break;
            }
            element = element >> 1;
            target = target << 1;
        }

        if (!layer){
            element = bitmap_block[i];
            bitmap_block1[i] = element | target;
            freeBlock = (i * 8 + j);
            u_fs_write_disk(1, bitmap_block);
            return freeBlock;
        }
    }
    return -1;

}

static int u_fs_getnullBlock(){

    if(u_fs_getblock(bitmap_block1) != -1)
        return u_fs_getblock(bitmap_block1);

    if(u_fs_getblock(bitmap_block2) != -1)
        return u_fs_getblock(bitmap_block2);

    return -1;

}

static void *fs_init(struct fuse_conn_info *conn,
                     struct fuse_config *cfg)
{
    (void)conn;
    cfg->kernel_cache = 1;
    return NULL;
}


static int u_fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void)fi;
    printf("get attr:%s\n\n", path);
    char* directory = malloc(MAX_FILENAME + 1);
    char* filename = malloc(MAX_FILENAME + 1);
    char* extension = malloc(MAX_EXTENSION + 1);
    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    int i, found=0, nFile;

    u_fs_directory_entry* subDir = malloc(sizeof(u_fs_directory_entry));

    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }
    else if (strcmp(directory,"")!=0){

        for (i = 0; i < MAX_DIRS_IN_ROOT; i++){
            if (strcmp(directory, (root->directories[i]).dname) == 0){
                found = 1;
                u_fs_read_disk((root->directories[i]).nStartBlock, subDir);
                break;
            }
        }
        if (found){
            if (strcmp(filename, "") == 0 && strcmp(extension, "") == 0){
                //Might want to return a structure with these fields
                stbuf->st_mode = S_IFDIR | 0755;
                stbuf->st_nlink = 2;
                return  0; //no error
            }
            else{
                found = 0;
                for (i = 0; i < MAX_FILES_IN_DIR; i++){
                    if (strcmp(filename, (subDir->files[i]).fname) == 0 && strcmp(extension,(subDir->files[i]).fext) == 0){
                        found = 1;
                        nFile = i;
                        stbuf->st_mode = S_IFREG | 0666;
                        stbuf->st_nlink = 1; //file links
                        stbuf->st_size = (subDir->files[nFile]).fsize; //file size - make sure you replace with real size!
                        return  0; // no error
                    }
                }

                return -ENOENT;
            }
        }
        else
            return -ENOENT;
    }
        //Else return that path doesn't exist
    else
        return -ENOENT;

}


static int u_fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{

    (void)offset;
    (void)fi;
    (void)flags;

    char* directory = malloc(MAX_FILENAME + 1);
    char* filename = malloc(MAX_FILENAME + 1);
    char* extension = malloc(MAX_EXTENSION + 1);
    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    int i, found = 0;

    u_fs_directory_entry* subDirectory = malloc(sizeof(u_fs_directory_entry));



    if (strcmp(path, "/") == 0){
        filler(buf, ".", NULL, 0, 0);
        filler(buf, "..", NULL, 0, 0);
        for (i = 0; i < root->nDirectories; i++){
            directory = (root->directories[i]).dname;
            filler(buf, directory, NULL, 0, 0);
        }
        return 0;
    }

    else {

        filler(buf, ".", NULL, 0, 0);
        filler(buf, "..", NULL, 0, 0);

        for (i = 0; i < root->nDirectories; i++){
            if (strcmp(directory, ((root->directories[i]).dname)) == 0){
                u_fs_read_disk((root->directories[i]).nStartBlock, subDirectory);
                found = 1;
                break;
            }
        }

        if (found){
            for (i = 0; i < subDirectory->nFiles; i++){
                filename = (subDirectory->files[i]).fname;
                char all_file_name[13];
                strcpy(all_file_name, filename);
                strcat(all_file_name, ".");
                strcat(all_file_name, (subDirectory->files[i]).fext);
                filler(buf, all_file_name, NULL, 0, 0);
            }
        }
        return 0;

    }

        return -ENOENT;

}


static int u_fs_mkdir(const char *path, mode_t mode)
{
    (void)mode;

    char* directory = malloc(MAX_FILENAME + 1);
    char* filename = malloc(MAX_FILENAME + 1);
    char* extension = malloc(MAX_EXTENSION + 1);
    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    int i, dir_Len, freeBlock, num_Dir;

    dir_Len = strlen(directory);

    struct stat *stbuf = malloc(sizeof(struct stat));

    if (dir_Len > MAX_FILENAME){

        return -ENAMETOOLONG;
    }

//    else if (u_fs_getattr(path, stbuf, NULL) == 0){
//        return -EEXIST;
//    }

    else{
        freeBlock = u_fs_getnullBlock();
        num_Dir = root->nDirectories;
        for (i = 0; i < dir_Len; i++){
            root->directories[num_Dir].dname[i] = directory[i];
        }
        //root->directories[num_Dir].dname[dir_Len] = '\0';

        root->directories[num_Dir].nStartBlock = freeBlock;
        root->nDirectories = num_Dir + 1;

        u_fs_write_disk(0, root);
        return 0;
    }

}


static int u_fs_rmdir(const char *path)
{
    char* directory = malloc(MAX_FILENAME + 1);
    char* filename = malloc(MAX_FILENAME + 1);
    char* extension = malloc(MAX_EXTENSION + 1);
    sscanf(path, "/%[^/]%[^.].%s", directory, filename, extension);

    struct stat *stbuf = malloc(sizeof(struct stat));
    u_fs_directory_entry* subDir = malloc(sizeof(u_fs_directory_entry));

    int found = 0, i , sub_dir_Block;


//    if(filename !=NULL || filename[0] != '\0') {
//        return -ENOTDIR;
//    }

    for (i = 0; i < root->nDirectories; i++) {
        if (strcmp(directory, root->directories[i].dname) == 0) {
            u_fs_read_disk(root->directories[i].nStartBlock, subDir);
            sub_dir_Block = root->directories[i].nStartBlock;
            found = 1;
            break;
        }
    }

    if (!found) {
        return -ENOENT;
    }
    else {
        if (subDir->nFiles != 0) {
            return -ENOTEMPTY;
        }
        else {
            u_fs_deletedata(sub_dir_Block);

            root->nDirectories = root->nDirectories - 1;
            u_fs_write_disk(0, root);
            return 0;
        }
    }
}


static int u_fs_mknod(const char *path, mode_t mode, dev_t dev)
{
    (void)mode;
    (void)dev;

    char* directory = malloc(MAX_FILENAME + 1);
    char* filename = malloc(MAX_FILENAME + 1);
    char* extension = malloc(MAX_EXTENSION + 1);
    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);


    int i, filenameLength, extensionLength, freeBlock, found = 0, subDirBlock, nFile;

    struct stat *stbuf = malloc(sizeof(struct stat));
    u_fs_directory_entry* subDirectory = malloc(sizeof(u_fs_directory_entry));

    if (directory == NULL || directory[0] == '\0'){
        return -EPERM;
    }
    filenameLength = strlen(filename);
    extensionLength = strlen(extension);

    if (filenameLength > MAX_FILENAME || extensionLength > MAX_EXTENSION){
        return -ENAMETOOLONG;
    }

    for (i = 0; i < root->nDirectories; i++){
        if (strcmp(directory, (root->directories[i]).dname) == 0){
            u_fs_read_disk((root->directories[i]).nStartBlock, subDirectory);
            subDirBlock = (root->directories[i]).nStartBlock;
            found = 1;
            break;
        }
    }
    if (found){

        for (i = 0; i < subDirectory->nFiles; i++){
            if (strcmp(filename, (subDirectory->files[i]).fname) == 0){
                return -EEXIST;
            }
        }

        freeBlock = u_fs_getnullBlock();

        nFile = subDirectory->nFiles;
        subDirectory->nFiles = nFile + 1;

        subDirectory->files[nFile].fsize = 0;
        subDirectory->files[nFile].nStartBlock = freeBlock;

        for (i = 0; i < filenameLength; i++){
            subDirectory->files[nFile].fname[i] = filename[i];
        }
        subDirectory->files[nFile].fname[filenameLength] = '\0';

        for (i = 0; i < extensionLength; i++){
            subDirectory->files[nFile].fext[i] = extension[i];
        }
        subDirectory->files[nFile].fext[extensionLength] = '\0';

        u_fs_write_disk(subDirBlock, subDirectory);
        u_fs_write_disk(0, root);

        return 0;
    }

    else {
        return -ENOENT;
    }
}


static int u_fs_unlink(const char *path)
{

    char* directory = malloc(MAX_FILENAME + 1);
    char* filename = malloc(MAX_FILENAME + 1);
    char* extension = malloc(MAX_EXTENSION + 1);
    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    int found = 0, sub_dir_block, fileBlock, nFile, i;

    struct stat *stbuf = malloc(sizeof(struct stat));
    u_fs_directory_entry* subDir = malloc(sizeof(u_fs_directory_entry));
    u_fs_disk_block* t_File = malloc(sizeof(u_fs_disk_block));



    if(filename == NULL || filename[0] == '\0') {
        return -EISDIR;
    }

    for (i = 0; i < root->nDirectories; i++){
        if (strcmp(directory, (root->directories[i]).dname) == 0){
            u_fs_read_disk((root->directories[i]).nStartBlock, subDir);
            sub_dir_block = (root->directories[i]).nStartBlock;
            found = 1;
            break;
        }
    }

    if (!found){
        return -ENOENT;
    }

    found = 0;
    for (i = 0; i < subDir->nFiles; i++){
        if (strcmp(filename, (subDir->files[i]).fname) == 0){
            u_fs_read_disk((subDir->files[i]).nStartBlock, t_File);
            fileBlock = (subDir->files[i]).nStartBlock;
            u_fs_deletedata(fileBlock);
            nFile = i;
            found = 1;
            break;
        }
    }

    if (!found){
        return -ENOENT;
    }

    while (t_File->nNextBlock != 0){
        fileBlock = t_File->nNextBlock;
        u_fs_deletedata(fileBlock);
        u_fs_read_disk(fileBlock, t_File);
    }

    for (i = nFile; i < subDir->nFiles; i++){
        subDir->files[i] = subDir->files[i + 1];
    }

    subDir->nFiles = subDir->nFiles - 1;

    u_fs_write_disk(sub_dir_block, subDir);
    u_fs_write_disk(0, root);

    return 0;
}


static int u_fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    (void)fi;

    char* directory = malloc(MAX_FILENAME + 1);
    char* filename = malloc(MAX_FILENAME + 1);
    char* extension = malloc(MAX_EXTENSION + 1);
    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    int i, j, found = 0, sub_dir_block, fileBlock, fileSize, loop = 0, newOffset, nFile;

    struct stat *stbuf = malloc(sizeof(struct stat));
    u_fs_directory_entry* subDir = malloc(sizeof(u_fs_directory_entry));
    u_fs_disk_block* t_file = malloc(sizeof(u_fs_disk_block));



    //check that size is > 0
    if (size <= 0){
        return -EPERM;
    }
    for (i = 0; i < root->nDirectories; i++){
        if (strcmp(directory, (root->directories[i]).dname) == 0){
            u_fs_read_disk((root->directories[i]).nStartBlock, subDir);
            sub_dir_block = (root->directories[i]).nStartBlock;
            found = 1;
            break;
        }
    }

    if (!found){
        return -ENOENT;
    }
    found = 0;
    for (i = 0; i < subDir->nFiles; i++){
        if (strcmp(filename, (subDir->files[i]).fname) == 0){
            u_fs_read_disk((subDir->files[i]).nStartBlock, t_file);
            fileBlock = (subDir->files[i]).nStartBlock;
            fileSize = (subDir->files[i]).fsize;
            nFile = i;
            found = 1;
            break;
        }
    }

    if (!found){
        return -EISDIR;
    }

    //check that offset is <= to the file size
    if (offset > fileSize){
        return -EFBIG;
    }

    if(fileSize-offset < size) size = fileSize - offset;

    //check if offset is in another file block
    if (offset >= MAX_DATA_IN_BLOCK){
        loop = offset / MAX_DATA_IN_BLOCK;
        newOffset = offset;

        for (i = 0; i < loop; i++){
            fileBlock = t_file->nNextBlock;
            u_fs_read_disk(t_file->nNextBlock, t_file);
            newOffset = newOffset - MAX_DATA_IN_BLOCK;
        }

        offset = newOffset;
    }
    //read in data


    for (i = offset, j = 0; j < size; i++, j++){
        buf[j] = t_file->data[i];

        if (i >= MAX_DATA_IN_BLOCK && t_file->nNextBlock != 0){
            fileBlock = t_file->nNextBlock;
            u_fs_read_disk(fileBlock, t_file);
            i = 0;
        }
    }
    return size;
}


static int u_fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
    (void)fi;

    char* directory = malloc(MAX_FILENAME + 1);
    char* filename = malloc(MAX_FILENAME + 1);
    char* extension = malloc(MAX_EXTENSION + 1);
    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    int i, j, found = 0, sub_dir_block, fileBlock, freeBlock, fileSize, loop = 0, newOffset, nFile;

    struct stat *stbuf = malloc(sizeof(struct stat));
    u_fs_directory_entry* subDir = malloc(sizeof(u_fs_directory_entry));
    u_fs_disk_block* t_file = malloc(sizeof(u_fs_disk_block));


    //check that size is > 0
    if (size <= 0){
        return -EPERM;
    }

    for (i = 0; i < root->nDirectories; i++){
        if (strcmp(directory, (root->directories[i]).dname) == 0){
            u_fs_read_disk((root->directories[i]).nStartBlock, subDir);
            sub_dir_block = (root->directories[i]).nStartBlock;
            found = 1;
            break;
        }
    }

    if (!found){
        return -ENOENT;
    }

    found = 0;
    for (i = 0; i < subDir->nFiles; i++){
        if (strcmp(filename, (subDir->files[i]).fname) == 0){
            u_fs_read_disk((subDir->files[i]).nStartBlock, t_file);
            fileBlock = (subDir->files[i]).nStartBlock;
            fileSize = (subDir->files[i]).fsize;
            nFile = i;
            found = 1;
            break;
        }
    }

    if (!found){
        return -ENOENT;
    }

    //check that offset is <= to the file size
    if (offset > fileSize){
        return -EFBIG;
    }

    //check if offset is in another file block
    if (offset >= MAX_DATA_IN_BLOCK){
        loop = offset / MAX_DATA_IN_BLOCK;
        newOffset = offset;

        for (i = 0; i < loop; i++){
            fileBlock = t_file->nNextBlock;
            u_fs_read_disk(t_file->nNextBlock, t_file);
            newOffset = newOffset - MAX_DATA_IN_BLOCK;
        }

        offset = newOffset;
    }


    //write data
    for (i = offset, j = 0; j < size; i++, j++){
        t_file->data[i] = buf[j];

        if (i >= MAX_DATA_IN_BLOCK && t_file->nNextBlock == 0){
            freeBlock = u_fs_getnullBlock();
            t_file->nNextBlock = freeBlock;
            u_fs_write_disk(fileBlock, t_file);
            fileBlock = freeBlock;
            u_fs_read_disk(fileBlock, t_file);
            i = 0;
        }

        else if (i >= MAX_DATA_IN_BLOCK && t_file->nNextBlock != 0){
            u_fs_write_disk(fileBlock, t_file);
            fileBlock = t_file->nNextBlock;
            u_fs_read_disk(fileBlock, t_file);
            i = 0;
        }
    }

    u_fs_write_disk(fileBlock, t_file);

    subDir->files[nFile].fsize = (fileSize + size);

    u_fs_write_disk(sub_dir_block, subDir);
    u_fs_write_disk(0, root);

    //set size (should be same as input) and return, or error
    return size;

}


static int fs_open(const char *path, struct fuse_file_info *fi)
{
    (void)path;
    (void)fi;
    return 0; //success!
}

static int fs_flush(const char *path, struct fuse_file_info *fi)
{
    (void)path;
    (void)fi;
    return 0; //success!
}


static struct fuse_operations hello_oper = {
        .init = fs_init,
        .getattr = u_fs_getattr,
        .readdir = u_fs_readdir,
        .mkdir = u_fs_mkdir,
        .rmdir = u_fs_rmdir,
        .read = u_fs_read,
        .write = u_fs_write,
        .mknod = u_fs_mknod,
        .unlink = u_fs_unlink,
        .flush = fs_flush,
        .open = fs_open,
};

int main(int argc, char *argv[])
{
    int result;
    getcwd(disk_path, 100);
    strcat(disk_path, "/disk");

    FILE* disk = fopen(disk_path, "r+b");

    root = malloc(sizeof(u_fs_root_directory));
    result = fread(root, BLOCK_SIZE, 1, disk);

    fseek(disk, BLOCK_SIZE, SEEK_SET);
    result = fread(bitmap_block1, 1, BLOCK_SIZE, disk);

    fseek(disk, 2 * BLOCK_SIZE, SEEK_SET);
    result = fread(bitmap_block2, 1, BLOCK_SIZE, disk);

    fclose(disk);

    if (bitmap_block1[0] == 0){
        bitmap_block1[0] = 7;
    }
    return fuse_main(argc, argv, &hello_oper, NULL);
}
