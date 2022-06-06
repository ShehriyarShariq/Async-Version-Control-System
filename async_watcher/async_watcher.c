#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include<unistd.h>
#include<signal.h>
#include<fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_EVENTS 1024  /* Maximum number of events to process*/
#define LEN_NAME 16  /* Assuming that the length of the filename
won't exceed 16 bytes*/
#define EVENT_SIZE  ( sizeof (struct inotify_event) ) /*size of one event*/
#define BUF_LEN     ( MAX_EVENTS * ( EVENT_SIZE + LEN_NAME ))
/*buffer to store the data of events*/

int fd;

typedef struct Watcher {
	int wd;
	char path[100];
} Watcher;

int currentWatchersCount = 0;
Watcher watchers[100];

int is_regular_file(const char *path){
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

void sig_handler(int sig){
 
       /* Step 5. Remove the watch descriptor and close the inotify instance*/
	for(int i = 0; i < currentWatchersCount; i++) {
		inotify_rm_watch(fd, watchers[i].wd);
	}       
	close( fd );
	exit( 0 );
 
}

void initDirWatchers(char *d_name){
	DIR *dir;
	struct dirent *entry;

	if (!(dir = opendir(strcmp(d_name, "") == 0 ? "." : d_name)))
		return;
	
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		
		if (entry->d_type == DT_DIR) {			
			char path[1024];
			strcpy(path, d_name);			
			strcat(path, entry->d_name);
			
			watchers[currentWatchersCount].wd = inotify_add_watch(fd, path,IN_MODIFY | IN_DELETE);
			strcpy(watchers[currentWatchersCount].path, path);
			currentWatchersCount++;
			
			strcat(path, "/");
			
			initDirWatchers(path);			
		}
	}
	closedir(dir);
}

int main(int argc, char *argv[]) {
	signal(SIGINT,sig_handler);
	
	fd = inotify_init();
	
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
		return 1;
	
	char startPath[4096];
	strcpy(startPath, argv[1]);
	
	watchers[currentWatchersCount].wd = inotify_add_watch(fd, startPath,IN_MODIFY | IN_DELETE);
	strcpy(watchers[currentWatchersCount].path, startPath);
	currentWatchersCount++;
	
	
	strcat(startPath, "/");
	initDirWatchers(startPath);
	
	for(int i = 0; i < currentWatchersCount; i++) {
		if(watchers[i].wd == -1){
			printf("Could not watch %d : %s\n", i, watchers[i].path);
		} else{
			printf("Watching %d : %s\n", watchers[i].wd, watchers[i].path);
		}
	}
	
	while(1){
		int i=0,length;
		char buffer[BUF_LEN];
 
		// Step 3. Read buffer
		length = read(fd,buffer,BUF_LEN);
 
		// Step 4. Process the events which has occurred
		struct inotify_event *event;// = (struct inotify_event *) &buffer[i];
		while(i<length){
			event = (struct inotify_event *) &buffer[i];
			i += EVENT_SIZE + event->len;
		}
		
		if(i > 0 && event->len){
			char fileName[1024];
			sprintf(fileName, "%s/%s", watchers[event->wd - 1].path, event->name);
			if (is_regular_file(fileName) || ((event->mask & IN_DELETE ) && (strrchr(event->name, '.') != NULL))) {
				printf("%s event triggered at %s for %s\n", (event->mask & IN_DELETE ) ? "Delete" : "Modify", watchers[event->wd - 1].path, fileName);
				
			} else if (strrchr(event->name, '.') == NULL){
				if (event->mask & IN_DELETE) {
					inotify_rm_watch(fd, watchers[event->wd - 1].wd);
					printf("%s directory deleted.\n", fileName);
				} else {
					watchers[currentWatchersCount].wd = inotify_add_watch(fd, fileName,IN_MODIFY | IN_DELETE);
					strcpy(watchers[currentWatchersCount].path, fileName);
					
					currentWatchersCount++;
					printf("%s directory created at %s\n", event->name, fileName);
				}
			}
		}
	}
	
	return 0;
}