/* THIS CODE WAS MY OWN WORK, IT WAS WRITTEN WITHOUT CONSULTING ANY
SOURCES OUTSIDE OF THOSE APPROVED BY THE INSTRUCTOR. Zachariah Dawood */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inodemap.h"
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>

void handlecreate(char*, char*);
void recursecreate(char*, int);
void handleextract(char*);
void handletest(char*);
void rwerror(size_t, off_t, char*);
struct dirent* readdirerror(DIR*);

// tarfile magic number to validate an input file is actually compatible
const int magicno = 0x7261746D;
FILE* tarfile;

int main(int argc, char *argv[]){
    // iterate through all the options, assigning the mode and 
    // extracting the filename
    char arg, *filename = NULL, mode = -1;
    while((arg = getopt(argc, argv, "cxtf:")) != -1){
        switch(arg){
            case 'c': mode = 'c'; break;
            case 'x': mode = 'x'; break;
            case 't': mode = 't'; break;
            case 'f': filename = optarg; break;
            default: break;
        }
    }
    // if no filename was selected (either if f was not passed as an argument or if
    // -f was the last argument with no optarg), throw an error and exit the program.    
    if(filename == NULL) {
        fprintf(stderr, "Error: No tarfile specified.\n"); 
        exit(-1);
    // an appropriate mytar call will have 3 arguments that get parsed by getopt, which are
    // the mode, the filename indicator, and the filename itself, meaning that the 
    // optind should be at 4. If more arguments are passed, then it is not a valid call and 
    // the program throws and error and exits. 
    } else if(optind > 4) {
        fprintf(stderr, "Error: Multiple modes specified.\n");
        exit(-1);
    // if mode is unchanged because one was not specified, indicate and exit. 
    } else if(mode == -1) {
        fprintf(stderr, "Error: No mode specified\n");
        exit(-1);
    // if create mode is specified and no target directory is specified (that is, if 
    // the index of the next option is the same as the total number of args since 
    // if there was a directory, there'd be another argument parse), indicate and exit.
    } else if(mode == 'c' && optind == argc) {
        fprintf(stderr, "Error: No directory target specified.\n");
        exit(-1);
    }
    switch(mode){
        case 'c':
            // call handlecreate with the tar file name and the input directory as
            // the last argument.
            handlecreate(filename, argv[argc-1]);
            break;
        case 'x':
            handleextract(filename);
            break;
        case 't':
            handletest(filename);
            break;
    }
    exit(0);
}

void handlecreate(char* tarfilename, char* dir){
    // collect metadata about the directory. If stat fails, then it 
    // likely doesn't exist so throw and error and exit and if it is not a directory
    // as indicated in the st_mode property, then throw an error as well.
    struct stat* topdirstat = (struct stat*) malloc(sizeof(struct stat));
    if(stat(dir, topdirstat)){
        fprintf(stderr, "Error: Specified target(\"%s\") does not exist.\n", dir);
        exit(-1);
    } 
    if(!S_ISDIR(topdirstat -> st_mode)){
        fprintf(stderr, "Error: Specified target(\"%s\") is not a directory.\n", dir);
        exit(-1);
    }
    // open the tarfile for writing to, assiging it to the global variable
    // if fopen fails, it'll return null so in that case, throw an error and exit.
    tarfile = fopen(tarfilename, "w");
    if(tarfile == NULL){
        perror("fopen");
        exit(-1);
    }
    // the target directory can either be in the current directory or it can be 
    // specifed through an absolute path or it can be in a complex relative path chain 
    // from the current directory. The tarfile acts like the input directory is the top directory
    // (eg if we're reading from the ../../dir1/dir2 directory, the tarfile will have all
    // the file names relative to dir2 (dir2/whatever) but the actual data has to obtained relative 
    // to the current directory the program is running in). This while loop determines 
    // where the forward slash before the directory of interest's position in the pathname is 
    // so that any characters before and up to it can be ignored when writing to the tarfile 
    // by decrementing from the last character in the directory's name and checking if it's a slash.
    // The exception would be if the user specified a trailing slash when calling mytar (
    // ../../dir1/dir2/) in which case, that trailing slash does not count as finding the relative path
    // and is actually replaced with a string terminator since a lot of the following logic assumes
    // there is no trailing slash. If no slash is present, the offset decreases to 0.
    int offset = strlen(dir) - 1;
    while(offset > 0){
        if(dir[offset] == '/' && offset == strlen(dir) - 1){
            dir[offset] = '\0';
        } else if(dir[offset] == '/'){
            offset++;
            break;
        }
        offset--;
    }
    // write the magic number (from the global constant) and inode number from stat to the start of the tarfile. 
    // rwerror checks if fwrite and fread encounter any errors and handle them if so. 
    // fwrite wants a void pointer for its first entry so that it can write generic data so 
    // most calls use the address for the data that I want to write (with the exception of strings
    // since they are already pointers)
    rwerror(fwrite(&magicno, 4, 1, tarfile), 1, "fwrite");
    rwerror(fwrite(&(topdirstat -> st_ino), 8, 1, tarfile), 1, "fwrite");
    // find the length of the directory name from the offset position (that is, ingoring the path part)
    // and write it along with the mode and last modified time. 
    int dirnamelen = strlen(dir + offset);
    rwerror(fwrite(&dirnamelen, 4, 1, tarfile), 1, "fwrite");
    rwerror(fwrite(dir + offset, dirnamelen, 1, tarfile), 1, "fwrite");
    rwerror(fwrite(&(topdirstat -> st_mode), 4, 1, tarfile), 1, "fwrite");
    rwerror(fwrite(&(topdirstat -> st_mtime), 8, 1, tarfile), 1, "fwrite");
    free(topdirstat);
    // handles adding the rest of the entries 
    recursecreate(dir, offset);
    // throw an error if there's an issue with closing the tarfile since fclose returns nonzero on
    // error
    if(fclose(tarfile)){
        perror("fclose");
        exit(-1);
    }
}

void recursecreate(char* dirname, int offset){
    // allocates enough room for any filename (max of 256 bytes for a filename plus one byte for
    // the forward slash separating the path and the file plus the pathname itself)
    char* currentname = (char*) malloc((strlen(dirname) + 257) * sizeof(char));
    // if there's an issue with opening the dir, throw an error 
    DIR* dir = opendir(dirname);
    if(dir == NULL){
        perror("opendir");
        exit(-1);
    }
    // space to hold dirents returned by readdir and stats from lstat 
    // readdirerror is a wrapper function for readdir that handles errors.
    struct dirent* entry = readdirerror(dir);
    struct stat* statholder = (struct stat*) malloc(sizeof(struct stat));
    // when readdir is exhausted, it'll return a null pointer, which indicates when 
    // the loop is able to terminate. 
    while(entry != NULL){
        // currentname holdes the entry's name (including the path), partially obtained from
        // the entry's d_name field
        strcpy(currentname, dirname);
        strcat(currentname, "/");
        strcat(currentname, entry -> d_name);
        // if the current entry is . or .., iterate the entry and skip to the next
        // iteration
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name, "..")){
            entry = readdirerror(dir);
            continue;
        }
        // take lstat of the current entry (this requires the entire (possibly-relative) pathname) and 
        // stores it in statholder. Since it returns nonzero on error, if one happens, it'll throw an 
        // error and exit. lstat is necessary instead of just stat because stat will make sym links go to
        // the file they're linked to and therefore they won't be skipped and instead will be treated like 
        // hard links. 
        if(lstat(currentname, statholder)){
            perror("lstat");
            exit(-1);
        }
        // skip symbolic links using the macro that tests if it's a link. Iterates to the next
        // entry from readdir and continue the loop
        if(S_ISLNK(statholder -> st_mode)){
            entry = readdirerror(dir);
            continue;
        }
        // if the entry is not being skipped, then actual writing to the tarfile happens
        // the tarfile writes content relevant to hard links, directories, and regular files first
        // and gradually gets more exclusive (writing to the stuff that all three need to know,
        // then only the stuff that dirs and files need and then only the stuff for dirs, continuing the
        // loop when appropriate). Here, the inode number, name length (not including the full path)
        // and name offset just to the relevant parts arewritten. If get_inode does not return null, 
        // then the entry is a hard link to something already written to the tarfile and the loop
        // continues
        rwerror(fwrite(&(statholder -> st_ino), 8, 1, tarfile), 1, "fwrite");
        int currentnamelen = strlen(currentname + offset);
        rwerror(fwrite(&currentnamelen, 4, 1, tarfile), 1, "fwrite");
        rwerror(fwrite(currentname + offset, currentnamelen, 1, tarfile), 1, "fwrite");
        if(get_inode(statholder -> st_ino) != NULL){
            entry = readdirerror(dir);
            continue; 
        }
        // if the current file is not a hard link, then add a new entry to the map from inodemap.c 
        // with the current file name so if another has the same inode number, it will be recognized 
        // as a hard link
        set_inode(statholder -> st_ino, currentname);
        // write the mode and modified time both obtained from statholder
        rwerror(fwrite(&(statholder -> st_mode), 4, 1, tarfile), 1, "fwrite");
        rwerror(fwrite(&(statholder -> st_mtime), 8, 1, tarfile), 1, "fwrite");
        // use the S_ISDIR macro to tell if the current file is a directory. If so,
        // the program calls recursecreate on that directory since tarfiles are built up 
        // recursively. The recursize call functions very similarly to the base call except that
        // there is now another folder in the path. All the data needed for a directory has already been
        // written to the tarfile so there is no issue calling recursecreate and its entries will go before
        // the rest of the current directory's entries. After the recursive call is finished, 
        // get the next entry from its parent forlder's readdir and continue.
        if(S_ISDIR(statholder->st_mode)){
            recursecreate(currentname, offset);
            entry = readdirerror(dir);
            continue;
        }
        // if the program makes it here, it's dealing with a regular file and so
        // it gets its size from the stat call, writing it and then opening the 
        // file for reading (throwing an error if there is an issue with fopen).
        rwerror(fwrite(&(statholder -> st_size), 8, 1, tarfile), 1, "fwrite");
        FILE* currentfile = fopen(currentname, "r");
        if(currentfile == NULL){
            perror("fopen");
            exit(-1);
        }
        // allocate enough space to store the entire contents of the file in the heap
        // and then read the entire file to that allocated space and then write it to
        // the tarfile. Then close the file, free the space that held the contents, 
        // increment the entry and continue the loop.
        char* filecontents = (char*) malloc(statholder -> st_size);
        rwerror(fread(filecontents, sizeof(char), statholder->st_size, currentfile), statholder->st_size, "fread");
        rwerror(fwrite(filecontents, sizeof(char), statholder->st_size, tarfile), statholder->st_size, "fwrite");
        if(fclose(currentfile)){
            perror("fclose");
            exit(-1);
        }
        free(filecontents);
        entry = readdirerror(dir);
    }
    // cleanup allocated memory. The tarfile will get closed after the first recursecreate 
    // returns to handlecreate by recursively called directories are closed when they are 
    // done.
    free(currentname);
    free(entry);
    free(statholder);
    if(closedir(dir)){
        perror("closedir");
        exit(-1);
    }
}

// fread and fwrite errors happen when the number of items you tell it to
// read is not equal to how many actually get read. If that happens, then
// throw an error and exit. The number of items is normally 1 with either 4 or 8 bytes
// but for larger reads, 1 byte at a time is read with size items retrieved.
// fncall indicates whether the call was done by fread or fwrite.
void rwerror(size_t itemsread, off_t expecteditems, char* fncall){
    if(itemsread != expecteditems){
        perror(fncall);
        exit(-1);
    }
}

// set the errno to zero in case it wasn't already and then call readdir.
// if the output of readdir is NULL and the error number is 0 (the and is 
// needed because readdir can output null when it is out of entries) 
// then throw and error and exit. otherwise, just pass through the output. 
struct dirent* readdirerror(DIR* dir){
    errno = 0;
    struct dirent* readdiroutput = readdir(dir);
    if(readdiroutput == NULL && errno != 0){
        perror("readdir");
        exit(-1);
    }
    return readdiroutput;
}

void handleextract(char* tarfilename){
    // open the tarfile and make sure that it actually exists by checking if the call returns null
    // if it does, exit.
    tarfile = fopen(tarfilename, "r");
    if(tarfile == NULL){
        fprintf(stderr, "Error: Specified target(\"%s\") does not exist.\n", tarfilename);
        exit(-1);
    }
    // allocate some space that'll be useful during the loop body and below 
    void* fourbyteholder = malloc(4);
    void* eightbyteholder = malloc(8);
    // read the first 4 bytes of the tarfile to fourbyte holder and then compare fourbyteholder's
    // contents as in int with the magicno at the top of the file (also as an int). If they do not
    // match, throw the error.
    rwerror(fread(fourbyteholder, 4, 1, tarfile), 1, "fread");
    if(*((int*) fourbyteholder) != magicno){
        fprintf(stderr, "Bad magic number (%d), should be: %d.\n", *((int*) fourbyteholder), magicno);
        exit(-1);
    }
    long long unsigned int inodenum, modtime, size;
    unsigned int namelen, mode; 
    char* name;
    // create a list to hold the names of the files. This is necessary for the names to be freed.
    char** namelist = (char**) malloc(sizeof(char*) * MAPSIZE);
    unsigned int namelistindex = 0;
    struct timeval times[2];
    while(1){
        // get a character from the tarfile and then run feof to check if EOF 
        // has been reached. If so, break the loop and if not, move the cursor back one place
        // to make up for the fgetc, throwing an error if fseek causes an issue.
        fgetc(tarfile);
        if(feof(tarfile)) break; else {
            if(fseek(tarfile, -1, SEEK_CUR)){
                perror("fseek");
                exit(-1);
            }
        } 
        // the pattern of reading an appropriate number of bytes 
        // (either 8 for the long long unsigned int entries or 4 for the unsigned int data)
        // into four or eightbyteholder and then casting and storing the data is how most of the
        // numerical data for extract and test are obtained, including the inode number and
        // length of the name in this case.
        rwerror(fread(eightbyteholder, 8, 1, tarfile), 1, "fread");
        inodenum = *((long long unsigned int*) eightbyteholder);
        rwerror(fread(fourbyteholder, 4, 1, tarfile), 1, "fread");
        namelen = *((unsigned int*) fourbyteholder);
        // allocate exactly enough space for the name (the namelen obtained from the tarfile plus
        // one for the terminating character and then read the next bit of data from the tarfile)
        // into name and append the null character. Append the pointer to the name to namelist
        name = (char*) malloc(namelen + 1);
        rwerror(fread(name, 1, namelen, tarfile), namelen, "fread");
        name[namelen] = '\0';
        namelist[namelistindex++] = name;
        // if get_inode returns not null, then this entry is a hard link and so
        // link is called to create the hard link between the original file (from get_inode) and
        // the name of the hard link. If it fails, throw an error. The loop continues to the 
        // next entry in the tarfile.
        if(get_inode(inodenum) != NULL){
            if(link(get_inode(inodenum), name)){
                perror("link");
                exit(-1);
            }
            continue;
        }
        // if the entry is not a hard link, add the inode to the map so that it can be compared 
        // going forward. It's important to note that this is *not* the inode number that the 
        // file is getting stored at after extraction from tar but instead the inode number it was 
        // at when it was archived. However, any hard links will be compared to the old inode number
        // since they'll have the old inode number as well 
        set_inode(inodenum, name);
        // the mode and modtime are read with the same pattern as described before.
        rwerror(fread(fourbyteholder, 4, 1, tarfile), 1, "fread");
        mode = *((unsigned int*) fourbyteholder);
        rwerror(fread(eightbyteholder, 8, 1, tarfile), 1, "fread");
        modtime = *((long long unsigned int*) eightbyteholder);
        // now that the mode is obtained, S_ISDIR can be run as if the mode was obtained from 
        // lstat and if the current entry in the tarfile is a directory, then make the directory 
        // with the mode from mode, throwing an error if mkdir encounters an issue as indicated by a 
        // nonzero return. The loop continues if it's a directory.
        if(S_ISDIR(mode)){
            if(mkdir(name, mode)){
                perror("mkdir");
                exit(-1);
            }
            continue;
        }
        // if the entry is not a hard link and not a directory, then it is a regular file.
        // A new file iscreated with fopen (throwing an error if there's an issue) and size is read
        // using fread. The file has the name given after the offset so it does not get created 
        // in some far-off directory.
        rwerror(fread(eightbyteholder, 8, 1, tarfile), 1, "fread");
        size = *((long long unsigned int*) eightbyteholder);
        FILE* currentfile = fopen(name, "w");
        if(currentfile == NULL){
            perror("fopen");
            exit(-1);
        }
        // allocate enough space for its contents using size and then 
        // fread the next size bytes from the tarfile into filecontents and 
        // fwrite those bytes into the newly-created file before freeing the filecontents.
        char* filecontents = malloc(size);
        rwerror(fread(filecontents, sizeof(char), size, tarfile), size, "fread");
        rwerror(fwrite(filecontents, sizeof(char), size, currentfile), size, "fwrite");
        free(filecontents);
        // close the current file. This has to be done before the modification time is 
        // assigned otherwise the modification file will update to the current time.
        if(fclose(currentfile)){
            perror("fclose");
            exit(-1);
        }
        // chmod the file, throwing an error if appropriate. 
        if(chmod(name, mode)){
            perror("chmod");
            exit(-1);
        } 
        // call gettimeofday on the first struct timeval to get 
        // the current time for access, throwing an error if appropriate.
        if(gettimeofday(times, NULL)){
            perror("gettimeofday");
            exit(-1);
        }
        // for the second struct timeval, set its tv_sec to the modified time from the tarfile 
        // and then use utimes to update the file's access and modifacion time, possibly throwing an
        // error 
        times[1].tv_sec = modtime;
        times[1].tv_usec = 0;
        if(utimes(name, times)){
            perror("utimes");
            exit(-1);
        }
    }
    // cleanup
    free(fourbyteholder);
    free(eightbyteholder);
    if(fclose(tarfile)){
        perror("fclose");
        exit(-1);
    }
    // because the inode map does not provide any functionality to free its entries (even
    // doing something like &(map = get_inode(0)) and then freeing entries from there does not work
    // becasue the map is const) and because names can not be freed as soon as the iteration with their
    // associated file is finished since future entries might need the name to link, it was 
    // necessary to create another list of the pointers to the names that are freed here.
    for(unsigned int i = 0; i < namelistindex; i++){
        free(namelist[i]);
    }
    free(namelist);
}

void handletest(char* tarfilename){
    // this function is similar to handlextract so anything that's essentially a repeat will not be
    // commented
    tarfile = fopen(tarfilename, "r");
    if(tarfile == NULL){
        fprintf(stderr, "Error: Specified target(\"%s\") does not exist.\n", tarfilename);
        exit(-1);
    }
    void* fourbyteholder = malloc(4);
    void* eightbyteholder = malloc(8);
    rwerror(fread(fourbyteholder, 4, 1, tarfile), 1, "fread");
    if(*((int*) fourbyteholder) != magicno){
        fprintf(stderr, "Bad magic number (%d), should be: %d.\n", *((int*) fourbyteholder), magicno);
        exit(-1);
    }
    long long unsigned int inodenum, modtime, size;
    unsigned int namelen, mode; 
    char* name;
    while(1){
        fgetc(tarfile);
        if(feof(tarfile)) break; else {
            if(fseek(tarfile, -1, SEEK_CUR)){
                perror("fseek");
                exit(-1);
            }
        } 
        rwerror(fread(eightbyteholder, 8, 1, tarfile), 1, "fread");
        inodenum = *((long long unsigned int*) eightbyteholder);
        rwerror(fread(fourbyteholder, 4, 1, tarfile), 1, "fread");
        namelen = *((unsigned int*) fourbyteholder);
        name = (char*) malloc(namelen + 1);
        rwerror(fread(name, 1, namelen, tarfile), namelen, "fread");
        name[namelen] = '\0';
        // if get_inode does not return null, then it is a hard link so
        // return the name and the inode number. then free the name since
        // the data the name pointer points to isn't important - only that
        // there is not a null and continue the loop to read the next data.
        if(get_inode(inodenum) != NULL){
            printf("%s/ -- inode: %llu\n", name, inodenum);
            free(name);
            continue;
        }
        set_inode(inodenum, name);
        rwerror(fread(fourbyteholder, 4, 1, tarfile), 1, "fread");
        mode = *((unsigned int*) fourbyteholder);
        rwerror(fread(eightbyteholder, 8, 1, tarfile), 1, "fread");
        modtime = *((long long unsigned int*) eightbyteholder);
        // if the current entry is a directory, then print, free, and continue, 
        // notably, the mode must be moduloed with 0o1000 so that only the 
        // last three digits which pertain to rwx for the user types are visible in the test 
        if(S_ISDIR(mode)){
            printf("%s/ -- inode: %llu, mode: %o, mtime: %llu\n", name, inodenum, mode % 01000, modtime);
            free(name);
            continue;
        }
        rwerror(fread(eightbyteholder, 8, 1, tarfile), 1, "fread");
        size = *((long long unsigned int*) eightbyteholder);
        // check if the file is executable by anding the last 9 bits of mode with 
        // 001001001, where the 1s represent the execute permissions for the user types.
        // If any of the execute bits in mode are set to 1, the if statement will execute 
        // and print the entry for a normal executable file. If it is not executable, 
        // a very similar entry will be printed but just one that's missing the * after the name.
        if((mode % 01000) & 0b001001001){
            printf("%s* -- inode: %llu, mode: %o, mtime: %llu, size: %llu\n", name, inodenum, mode % 01000, modtime, size);
        } else {
            printf("%s -- inode: %llu, mode: %o, mtime: %llu, size: %llu\n", name, inodenum, mode % 01000, modtime, size);
        }
        // fseek to skip to the contents of the regular or executable file by
        // moving the cursor from it current position to its current position plus the 
        // size of the file obtained from the tarfile. Exit and throw an error if appropriate (
        // when fseek returns something nonzero) and then free the name and continue to the next entry.
        if(fseek(tarfile, size, SEEK_CUR)){
            perror("fseek");
            exit(-1);
        }
        free(name);
    }
    free(fourbyteholder);
    free(eightbyteholder);
    if(fclose(tarfile)){
        perror("fclose");
        exit(-1);
    }
}