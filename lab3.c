#define _GNU_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>


#define BUFFER_SIZE 4096
#define SUCCESS 0


typedef struct {
   char *src;
   char *dst;
} ThreadArgs;


void free_args_dirs_files(ThreadArgs* free_args, DIR* free_dir, int free_src_fd,
                                                               int free_dst_fd){
   free(free_args->src);
   if (free_args->dst != NULL)
       free(free_args->dst);
   free(free_args);
   if (free_dir != NULL)
       closedir(free_dir);
   if (free_src_fd != -1)
       close(free_src_fd);
   if (free_dst_fd != -1)
       close(free_dst_fd);
  
}


void *copy_file(void *arg) {
   ThreadArgs *args = (ThreadArgs *)arg;


   int err = pthread_detach(pthread_self());
   if (err != SUCCESS) {
       fprintf(stderr, "copy_file: pthread_detach() failed: %s\n", strerror(err));
       free_args_dirs_files(args, NULL, -1, -1);
       return NULL;
   }


   printf("copy_file start: tid:%d, src:%s\n", gettid(), args->src);


   int src_fd = open(args->src, O_RDONLY);


   int err_number = errno;
   while ((src_fd == -1) && (err_number == EMFILE)){
       sleep(1);
       src_fd = open(args->src, O_RDONLY);
       err_number = errno;
   }


   if (src_fd == -1) {
       fprintf(stderr, "copy_file: failed to open source file:%s\n", strerror(errno));
       free_args_dirs_files(args, NULL, -1, -1);
       return NULL;
   }


   int dst_fd = open(args->dst, O_WRONLY | O_CREAT | O_TRUNC, 0774);
  
   err_number = errno;
   while ((dst_fd == -1) && (err_number == EMFILE)){
       sleep(1);
       dst_fd = open(args->dst, O_WRONLY | O_CREAT | O_TRUNC, 0774);
       err_number = errno;
   }
 
   if (dst_fd == -1) {
       fprintf(stderr, "copy_file: failed to open destination file:%s\n", strerror(errno));
       free_args_dirs_files(args, NULL, src_fd, -1);
       return NULL;
   }


   char buffer[BUFFER_SIZE];
   ssize_t bytes_read, bytes_written;
   do {
       bytes_read = read(src_fd, buffer, sizeof(buffer));
       if (bytes_read == -1){
           fprintf(stderr, "copy_file: error with reading a file: %s\n", strerror(errno));
           free_args_dirs_files(args, NULL, src_fd, dst_fd);
           return NULL;
       }
      
       bytes_written = write(dst_fd, buffer, bytes_read);
       if (bytes_written == -1){
           fprintf(stderr, "copy_file: error with writing a file: %s\n", strerror(errno));
           free_args_dirs_files(args, NULL, src_fd, dst_fd);
           return NULL;
       }
       if (bytes_written != bytes_read) {
           fprintf(stderr, "copy_file: error with reading and writing a file\n");
           free_args_dirs_files(args, NULL, src_fd, dst_fd);
           return NULL;
       }
      
   } while (bytes_read > 0);


   printf("copy_file finish: tid:%d, src:%s\n", gettid(), args->src);


   free_args_dirs_files(args, NULL, src_fd, dst_fd);
   return NULL;
}




void *copy_directory(void *arg) {
   ThreadArgs *args = (ThreadArgs *)arg;


   int err = pthread_detach(pthread_self());
   if (err != SUCCESS) {
       fprintf(stderr, "copy_directory: pthread_detach() failed: %s\n", strerror(err));
       free_args_dirs_files(args, NULL, -1, -1);
       return NULL;
   }
  
   if (mkdir(args->dst, 0775) != SUCCESS && errno != EEXIST) {
       fprintf(stderr, "copy_directory: failed to create directory:%s\n", strerror(errno));
      
       free_args_dirs_files(args, NULL, -1, -1);
       return NULL;
   }


   printf("copy_directory start: tid:%d, src:%s\n", gettid(), args->src);


   DIR *dir = opendir(args->src);


   int err_number = errno;
   while ((dir == NULL) && (err_number == EMFILE)){
       sleep(1);
       dir = opendir(args->src);
       err_number = errno;
   }


   if (dir == NULL) {
       fprintf(stderr, "copy_directory: failed to open directory:%s\n", strerror(errno));
       free_args_dirs_files(args, NULL, -1, -1);
       return NULL;
   }


   struct dirent *entry;
   struct stat statbuf;


   int name_max = pathconf(args->src, _PC_NAME_MAX);
   if (name_max == -1)        
       name_max = 255;        
   int len = sizeof(struct dirent) + name_max + 1;


   struct dirent *entry_buffer = malloc(len);
   if (entry_buffer == NULL) {
       fprintf(stderr, "copy_directory: cannot allocate memory for an entry buffer\n");
       free_args_dirs_files(args, dir, -1, -1);
       return NULL;
   }


   char src_path[PATH_MAX], dst_path[PATH_MAX];


   while (1) {
      
       int res = readdir_r(dir, entry_buffer, &entry);
       if (res != SUCCESS) {
           fprintf(stderr, "copy_directory: readdir_r failed:%s", strerror(res));
           break;
       }
       if (entry == NULL) {
           break;
       }


      
       if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
           continue;


       snprintf(src_path, sizeof(src_path), "%s/%s", args->src, entry->d_name);
       snprintf(dst_path, sizeof(dst_path), "%s/%s", args->dst, entry->d_name);


      
       if (stat(src_path, &statbuf) == -1) {
           fprintf(stderr, "copy_directory: failed to stat file: %s\n", strerror(errno));
           continue;
       }


       if (entry->d_type != DT_DIR && entry->d_type != DT_REG){
           printf("%s is not regfile or directory\n", src_path);
           continue;
       }
          


       if (S_ISDIR(statbuf.st_mode)) {
           ThreadArgs *new_args = malloc(sizeof(ThreadArgs));
           if (new_args == NULL) {
               fprintf(stderr, "copy_directory: cannot allocate memory for new args\n");
               continue;
           }


           new_args->src = strdup(src_path);
           if (new_args->src == NULL) {
               fprintf(stderr, "copy_directory: cannot allocate memory for new args->src\n");
               free(new_args);
               continue;
           }
           new_args->dst = strdup(dst_path);
           if (new_args->dst == NULL) {
               fprintf(stderr, "copy_directory: cannot allocate memory for new args->dst\n");
               free_args_dirs_files(new_args, NULL, -1, -1);
               continue;
           }


           pthread_t dir_thread;
           err = pthread_create(&dir_thread, NULL, copy_directory, new_args);
           if (err != SUCCESS) {
               fprintf(stderr, "copy_directory: pthread_create() failed: %s\n", strerror(err));
               free_args_dirs_files(new_args, NULL, -1, -1);
               continue;
           }


       } else if (S_ISREG(statbuf.st_mode)) {
          
           ThreadArgs *file_args = malloc(sizeof(ThreadArgs));
           if (entry_buffer == NULL) {
               fprintf(stderr, "copy_directory: cannot allocate memory for file args\n");
               continue;
           }


           file_args->src = strdup(src_path);
           if (file_args->src == NULL) {
               fprintf(stderr, "copy_directory: cannot allocate memory for file args->src\n");
               free(file_args);
               continue;
           }
           file_args->dst = strdup(dst_path);
           if (file_args->dst == NULL) {
               fprintf(stderr, "copy_directory: cannot allocate memory for file args->dst\n");
               free_args_dirs_files(file_args, NULL, -1, -1);
               continue;
           }


           pthread_t file_thread;
           err = pthread_create(&file_thread, NULL, copy_file, file_args);
           if (err != SUCCESS) {
               fprintf(stderr, "copy_directory: pthread_create() failed: %s\n", strerror(err));
               free_args_dirs_files(file_args, NULL, -1, -1);
               continue;
           }
          
       }
      
   }


   printf("copy_directory finish: tid:%d, src:%s\n", gettid(), args->src);


   free_args_dirs_files(args, dir, -1, -1);
   free(entry_buffer);
  
   return NULL;
}


int main(int argc, char *argv[]) {
   if (argc != 3) {
       fprintf(stderr, "Usage: %s <source_dir> <destination_dir>\n", argv[0]);
       return 1;
   }


   char *src_dir = argv[1];
   char *dst_dir = argv[2];


   if (mkdir(dst_dir, 0775) != 0 && errno != EEXIST) {
       fprintf(stderr, "main: failed to create destination directory:%s\n", strerror(errno));
       return 1;
   }


   ThreadArgs *args = malloc(sizeof(ThreadArgs));
   if (args == NULL) {
       fprintf(stderr, "main: cannot allocate memory for args\n");
       return 1;
   }


   struct stat statbuf;
   if (stat(src_dir, &statbuf) == -1) {
       fprintf(stderr, "copy_directory: failed to stat file: %s\n", strerror(errno));
       free(args);
       return 1;
   }
 
   if (S_ISDIR(statbuf.st_mode)){
       char fold_name[PATH_MAX];
       char tmp_dst_dir[PATH_MAX];
       int index;
       for (int i = strlen(src_dir)-1; i>=0; i--)
           if (src_dir[i] == '/'){
               index = i;
               break;
           }


       int k = 0;       
       for (int i = index; i < strlen(src_dir); i++){
           fold_name[k] = src_dir[i];
           k++;
       }
       snprintf(tmp_dst_dir, sizeof(tmp_dst_dir), "%s%s", dst_dir, fold_name);


       args->src = strdup(src_dir);
       if (args->src == NULL) {
           fprintf(stderr, "main: cannot allocate memory for args->src\n");
           free(args);
           return 1;
       }
       args->dst = strdup(tmp_dst_dir);
       if (args->dst == NULL) {
           fprintf(stderr, "main: cannot allocate memory for file args->dst\n");
           free_args_dirs_files(args, NULL, -1, -1);
           return 1;
       }


   } else {
       args->src = strdup(src_dir);
       if (args->src == NULL) {
           fprintf(stderr, "main: cannot allocate memory for args->src\n");
           free(args);
           return 1;
       }
       args->dst = strdup(dst_dir);
       if (args->dst == NULL) {
           fprintf(stderr, "main: cannot allocate memory for file args->dst\n");
           free_args_dirs_files(args, NULL, -1, -1);
           return 1;
       }
   }


  
   pthread_t root_thread;
   int err = pthread_create(&root_thread, NULL, copy_directory, args);
   if (err != SUCCESS) {
       fprintf(stderr, "main: pthread_create() failed: %s\n", strerror(err));
       free_args_dirs_files(args, NULL, -1, -1);
       return 1;
   }
 
   pthread_exit(0);
}






