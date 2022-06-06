#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librsync.h>
#include <errno.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <dirent.h>
#include<sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <regex.h>
#include <arpa/inet.h>

#define COMMAND_OUTPUT_SIZE 3

typedef struct LogEntry {
	char prev_commit[41];
	char this_commit[41];	
} LogEntry;

struct lastSavedBlob {
	char type[5];
	char signature[41];
	char delta[41];
	char name[100];
};

struct nodeLink {
	char type[5];
	char id[41];
	char name[100];	
};

////////////////
// Tree Start //
////////////////

typedef struct TreeNode {
    char name[1024];
    struct TreeNode *child;
    struct TreeNode *sibling;
    int level;
    int siblingIndex;
    
} TreeNode;

TreeNode *fileRoot = NULL;

/* Know the location of the first child, put the node to be added to the end of the chain */
int addTreeNode(TreeNode **first, TreeNode *tree_ptr) {
    int index = 0;
    TreeNode *ptr = *first;
    while (ptr->sibling) {
        ptr = ptr->sibling;
	index++;
    }

    //printf("Adding sibling to: %s\n", ptr->name);
    ptr->sibling = tree_ptr;
    return index + 1;
}

/*  3-layer tree
 * level: the list to be added is on the first level
 * parentIndexes: is an array containing all the previous parent indexes starting from the root
 * name: the value of the node
*/
int addTree(int level, int *parentIndexes, char name[1024]) {
    TreeNode *tree_ptr = (TreeNode *)calloc(1, sizeof(TreeNode));
    if (!tree_ptr) {
        printf("calloc error\n");
        return -1;
    }
    int siblingIndex = 0;
    if (!fileRoot) {
	fileRoot = tree_ptr;
    } else {
	if(!(fileRoot->child)) {
	    fileRoot->child = tree_ptr;
	} else {
	    TreeNode *last_fa = fileRoot->child;
	    for(int i = 1; i < level; i++) {
	        for(int j = 0; j < parentIndexes[i - 1]; j++) {		    
		    last_fa = last_fa->sibling;
	        }
		
		if(last_fa->child != NULL)
		    last_fa = last_fa->child;
	    }
	    if (!(last_fa->child)){
		last_fa->child = tree_ptr;
	    } else {
		siblingIndex = addTreeNode(&(last_fa), tree_ptr);
	    }
	}
    }
    strcpy(tree_ptr->name, name);
    tree_ptr->child = NULL;
    tree_ptr->sibling = NULL;
    tree_ptr->level = level;
    tree_ptr->siblingIndex = siblingIndex;
    return 0;
error:
    free(tree_ptr);
    return -1;
}

/*  Output the data under the node and node */
int output_fa_and_child(TreeNode *fa, int level) {
    printf("Level: %d\n", fa->level);
    static int cnt = 0;
    printf("data %d : %s\n", cnt++, fa->name);
    TreeNode *vy = fa->child;   
    while (vy) {
        output_fa_and_child(vy, level + 1);   //Recursive call	        
	vy = vy->sibling;
    }
    
    return 0;
}

/*  Output all data in the tree */
int output_tree_data(TreeNode *fileRoot) {
    if (!fileRoot) {
        printf("no data\n");
        return -1;
    }
    output_fa_and_child(fileRoot, 0);
    return 0;
}

void DFS(TreeNode *fileRoot, char *prevPath) {
	if (fileRoot->child){
		DFS(fileRoot->child, "");
	}
	
	if(fileRoot->sibling) {
		TreeNode *siblingPtr = fileRoot->sibling;
		printf("Siblings: ");
		while(siblingPtr) {
			printf("%s ", siblingPtr->name);
			siblingPtr = siblingPtr->sibling;
		}
		printf("\n\n");
		
		DFS(fileRoot->sibling, "");
	} else {
		printf("\n");
	}
}


///////////////
// Tree End ///
///////////////

void listdir(char *d_name, char ***fileNames, int **slashesCount, int *filesCount){
	DIR *dir;
	struct dirent *entry;

	if (!(dir = opendir(strcmp(d_name, "") == 0 ? "." : d_name)))
		return;
	
	int thisDirIndex = 0;
	
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		
		if (entry->d_type == DT_DIR) {			
			char path[1024];
			strcpy(path, d_name);			
			strcat(path, entry->d_name);
			strcat(path, "/");
				
			listdir(path, fileNames, slashesCount, filesCount);			
		} else {		
			char path[1024];			
			strcpy(path, d_name);
			strcat(path, entry->d_name);
			*fileNames = realloc(*fileNames, ((*filesCount) + 1) * sizeof(char*));
			(*fileNames)[(*filesCount)] = (char*) malloc(sizeof(char) * (strlen(path) + 1));
			strcpy((*fileNames)[(*filesCount)], path);
			
			int slashCount = 0;
			for (int i = 0; path[i] != '\0'; i++) {
				if ('/' == path[i])
					slashCount++;
			}
			*slashesCount = realloc(*slashesCount, ((*filesCount) + 1) * sizeof(int));
			(*slashesCount)[(*filesCount)] = slashCount;
			
			(*filesCount)++;			
		}
		thisDirIndex++;
	}
	closedir(dir);
}

void generateCommit(TreeNode *fileRoot, char* prefixPath, char levelFilePath[], char** childrenHash) {
	char path[1024];
	strcpy(path, prefixPath);
	if (strcmp(prefixPath, "") != 0) {
		strcat(path, "/");
	}
	strcat(path, fileRoot->name);
	char *hashFromChildren = (char*) malloc(sizeof(char) * 41);
	if (fileRoot->child) {
		generateCommit(fileRoot->child, path, "", &hashFromChildren);
	}
	
	char thisLevelFilePath[1024]; 
	if(fileRoot->siblingIndex == 0) {
		strcpy(thisLevelFilePath, "./.async/tree/temp/");
		strcat(thisLevelFilePath, "temp_level_");
		
		char levelStr[3];
		sprintf(levelStr, "%d", fileRoot->level);
		
		strcat(thisLevelFilePath, levelStr);	
	} else {
		strcpy(thisLevelFilePath, levelFilePath);
	}

	if(fileRoot->child == NULL) {
		FILE* levelFile_temp = fopen(thisLevelFilePath, "a+");

		struct timeval tv;

		gettimeofday(&tv,NULL);
		long long tempFileName = (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
		
		char originalFileName[30];
		snprintf(originalFileName, 20, "%lld", tempFileName);
		
		char originalFilePath[1024];
		strcpy(originalFilePath, "./.async/tree/temp/");
		strcat(originalFilePath, originalFileName);
		strcat(originalFilePath, "_o");
		
		char fileSigPath[1024];
		strcpy(fileSigPath, "./.async/tree/temp/");
		strcat(fileSigPath, originalFileName);
		strcat(fileSigPath, "_s");
		
		FILE *originalFile = fopen(originalFilePath, "w+");		
		FILE *fileSig = fopen(fileSigPath, "w+");	

		rs_sig_file(originalFile, fileSig, 1, 2, NULL);
		
		fclose(originalFile);
		fclose(fileSig);
		
		
		char sigAndDeltContainerFilePath[1024];
		strcpy(sigAndDeltContainerFilePath, "./.async/tree/temp/");
		strcat(sigAndDeltContainerFilePath, originalFileName);
		strcat(sigAndDeltContainerFilePath, "_sigAndDelt");
		
		char fileDeltaPath[1024];
		strcpy(fileDeltaPath, "./.async/tree/temp/");
		strcat(fileDeltaPath, originalFileName);
		strcat(fileDeltaPath, "_d");
		
		fileSig = fopen(fileSigPath, "r");
		FILE *currentFile = fopen(path, "r");
		FILE *currentFileDelta = fopen(fileDeltaPath, "w+");

		rs_signature_t *file1Signature;

		rs_loadsig_file(fileSig, &file1Signature, NULL);

		rs_build_hash_table(file1Signature);

		rs_delta_file(file1Signature, currentFile, currentFileDelta, NULL);
		
		fclose(fileSig);
		fclose(currentFile);
		fclose(currentFileDelta);
		
		char command[1024];	
		strcpy(command, "/usr/bin/sha1sum ");
		strcat(command, fileSigPath);
		strcat(command, " ");
		strcat(command, fileDeltaPath);
		
		FILE *fd = popen(command, "r");
		
		char buf[1024];
		int i = 0;
		char hashes[2][41];
		while (fgets(buf, 1024, fd) != NULL) {
			char hash[41];
			strncpy(hash, buf + 0, 40);
			hash[40] = '\0';
				
			strcpy(hashes[i], hash);		
			
			char directoryName[3];
			strncpy(directoryName, buf + 0, 2);
			directoryName[2] = '\0';
			
			char fileName[39];
			strncpy(fileName, buf + 2, 38);
			fileName[38] = '\0';
			
			FILE *sourceFile, *destFile;
			
			char destPath[1024];
			strcpy(destPath, "./.async/tree/");
			strcat(destPath, directoryName);
			
			mkdir(destPath, 0700);
			strcat(destPath, "/");
			strcat(destPath, fileName);
			
			sourceFile = fopen(i == 0 ? fileSigPath : fileDeltaPath, "r");			
			destFile = fopen(destPath, "w+");
			
			char buf[4096];
			while (fgets(buf, 4096, sourceFile) != NULL) {
				fputs(buf, destFile);
			}
			
			fclose(sourceFile);
			fclose(destFile);
			
			i++;
		}
		
		FILE* sigAndDeltContainerFile_temp = fopen(sigAndDeltContainerFilePath, "w+");
		for (i = 0; i < 2; i++) {
			char line[1024];
			strcpy(line, "blob ");
			strcat(line, i == 0 ? "sig " : "delt ");
			strcat(line, hashes[i]);
			strcat(line, " ");
			strcat(line, path);
			
			char *lineStr = (char*) malloc(sizeof(char) * (strlen(line) + 1));
			strcpy(lineStr, line);
			
			fwrite(line, 1, strlen(lineStr), sigAndDeltContainerFile_temp);
			fprintf(sigAndDeltContainerFile_temp, "\n");
			
			free(lineStr);
		}
		fclose(sigAndDeltContainerFile_temp);
		
		char sigAndDeltContainerFile_temp_filename_command[1024];	
		strcpy(sigAndDeltContainerFile_temp_filename_command, "/usr/bin/sha1sum ");
		strcat(sigAndDeltContainerFile_temp_filename_command, sigAndDeltContainerFilePath);
		
		FILE *sigAndDeltContainerFile_temp_filename_command_fd = popen(sigAndDeltContainerFile_temp_filename_command, "r");
		
		char sigAndDeltContainerFileHashBuffer[1024];
		fgets(sigAndDeltContainerFileHashBuffer, 1024, sigAndDeltContainerFile_temp_filename_command_fd);
		
		char sigAndDeltContainerFileHash[41];
		strncpy(sigAndDeltContainerFileHash, sigAndDeltContainerFileHashBuffer + 0, 40);
		sigAndDeltContainerFileHash[40] = '\0';
		
		pclose(sigAndDeltContainerFile_temp_filename_command_fd);
		
		char sigAndDeltContainerFileDirectoryName[3];
		strncpy(sigAndDeltContainerFileDirectoryName, sigAndDeltContainerFileHash + 0, 2);
		sigAndDeltContainerFileDirectoryName[2] = '\0';
		
		char sigAndDeltContainerFileName[39];
		strncpy(sigAndDeltContainerFileName, sigAndDeltContainerFileHash + 2, 38);
		sigAndDeltContainerFileName[38] = '\0';
		
		char sigAndDeltContainerActualFilePath[1024];
		strcpy(sigAndDeltContainerActualFilePath, "./.async/tree/");
		strcat(sigAndDeltContainerActualFilePath, sigAndDeltContainerFileDirectoryName);		
		
		mkdir(sigAndDeltContainerActualFilePath, 0700);
		strcat(sigAndDeltContainerActualFilePath, "/");
		strcat(sigAndDeltContainerActualFilePath, sigAndDeltContainerFileName);
		
		sigAndDeltContainerFile_temp = fopen(sigAndDeltContainerFilePath, "r");
		FILE *sigAndDeltContainerFD = fopen(sigAndDeltContainerActualFilePath, "w+");
		
		char sigAndDeltContainerFileBuffer[4096];
		while (fgets(sigAndDeltContainerFileBuffer, 4096, sigAndDeltContainerFile_temp) != NULL) {			
			fputs(sigAndDeltContainerFileBuffer, sigAndDeltContainerFD);
		}
		
		fclose(sigAndDeltContainerFile_temp);
		fclose(sigAndDeltContainerFD);
		
		
		char levelFileLine[1024];
		strcpy(levelFileLine, "tree ");
		strcat(levelFileLine, sigAndDeltContainerFileHash);
		strcat(levelFileLine, " ");
		strcat(levelFileLine, path);
		
		char *levelFileLineStr = (char*) malloc(sizeof(char) * (strlen(levelFileLine) + 1));
		strcpy(levelFileLineStr, levelFileLine);
		
		fwrite(levelFileLineStr, 1, strlen(levelFileLineStr), levelFile_temp);
		fprintf(levelFile_temp, "\n");
		
		free(levelFileLineStr);
		
		remove(sigAndDeltContainerFilePath);
		remove(originalFilePath);
		remove(fileSigPath);
		remove(fileDeltaPath);
		
		pclose(fd);
		
		fclose(levelFile_temp);
	} else if (fileRoot->level > 0) {
		FILE* levelFile_temp = fopen(thisLevelFilePath, "a+");
		
		char levelFileLine[1024];
		strcpy(levelFileLine, "tree ");
		strcat(levelFileLine, hashFromChildren);
		strcat(levelFileLine, " ");
		strcat(levelFileLine, path);
		
		char *levelFileLineStr = (char*) malloc(sizeof(char) * (strlen(levelFileLine) + 1));
		strcpy(levelFileLineStr, levelFileLine);
		
		fwrite(levelFileLineStr, 1, strlen(levelFileLineStr), levelFile_temp);
		fprintf(levelFile_temp, "\n");
		
		free(levelFileLineStr);
		
		fclose(levelFile_temp);
	}
	//////////////////////////////////////////////////////////////////////
	
	
	if (fileRoot->sibling) {
		generateCommit(fileRoot->sibling, prefixPath, thisLevelFilePath, NULL);
	}
	
	if (fileRoot->siblingIndex == 0 && fileRoot->level > 0) {
		char levelFileHash_command[1024];	
		strcpy(levelFileHash_command, "/usr/bin/sha1sum ");
		strcat(levelFileHash_command, thisLevelFilePath);
		
		FILE *levelFileHash_command_fd = popen(levelFileHash_command, "r");
		
		char levelFileHashBuffer[1024];
		fgets(levelFileHashBuffer, 1024, levelFileHash_command_fd);
		
		char levelFileHash[41];
		strncpy(levelFileHash, levelFileHashBuffer + 0, 40);
		levelFileHash[40] = '\0';

		char levelFileDirectoryName[3];
		strncpy(levelFileDirectoryName, levelFileHash + 0, 2);
		levelFileDirectoryName[2] = '\0';
		
		char levelFileFileName[39];
		strncpy(levelFileFileName, levelFileHash + 2, 38);
		levelFileFileName[38] = '\0';
		
		char levelFileActualPath[1024];
		strcpy(levelFileActualPath, "./.async/tree/");
		strcat(levelFileActualPath, levelFileDirectoryName);

		mkdir(levelFileActualPath, 0700);
		strcat(levelFileActualPath, "/");
		strcat(levelFileActualPath, levelFileFileName);
		
		FILE *leveFile_tempFD = fopen(thisLevelFilePath, "r");
		FILE *levelFileActual_FD = fopen(levelFileActualPath, "w+");
		
		char levelFileBuffer[4096];
		while (fgets(levelFileBuffer, 4096, leveFile_tempFD) != NULL) {
			fputs(levelFileBuffer, levelFileActual_FD);
		}
		
		fclose(leveFile_tempFD);
		fclose(levelFileActual_FD);
		
		pclose(levelFileHash_command_fd);	
		remove(thisLevelFilePath);
		
		strcpy(*childrenHash, levelFileHash);
	}
	
	if (fileRoot->level == 0) {
		FILE *logFile = fopen("./.async/log", "r+");

		LogEntry lastCommit;
		fseek(logFile, 0, SEEK_SET);
		while(fread(&lastCommit, sizeof(LogEntry), 1, logFile) == 1 );
		
		char prevCommit[41];
		strcpy(prevCommit, strcmp(lastCommit.this_commit, "") != 0 ? lastCommit.this_commit : "0000000000000000000000000000000000000000");
		
		LogEntry entry;
		strcpy(entry.prev_commit, prevCommit); 
		strcpy(entry.this_commit, hashFromChildren);
		
		fwrite(&entry, sizeof(LogEntry), 1, logFile);
		
		fclose(logFile);
	}
	
	free(hashFromChildren);
}

void pushToServer(int socket_desc, char *hash) {	
	char hashFileDirectoryName[3];
	strncpy(hashFileDirectoryName, hash + 0, 2);
	hashFileDirectoryName[2] = '\0';
	
	char hashFileName[39];
	strncpy(hashFileName, hash + 2, 38);
	hashFileName[38] = '\0';
	
	char hashFilePath[1024];
	strcpy(hashFilePath, "./.async/tree/");
	strcat(hashFilePath, hashFileDirectoryName);
	strcat(hashFilePath, "/");
	strcat(hashFilePath, hashFileName);
	
	//FILE *hashFile = fopen(hashFilePath, "r");
	
	int hashFile = open(hashFilePath, O_RDONLY);
	
	char levelFileBuffer[1024];
	while (read(hashFile, levelFileBuffer, 1024)) {
		char *fileData = strtok(levelFileBuffer, " ");
		
		char entryType[5];
		strcpy(entryType, fileData);
		fileData = strtok(NULL, " ");
		
		if(strcmp(entryType, "blob") == 0) {
			fileData = strtok(NULL, " ");
		}
		
		char entryHash[41];
		strcpy(entryHash, fileData);
		
		printf("%s\n", entryHash);
		
		if(strcmp(entryType, "tree") == 0) {
			pushToServer(socket_desc, entryHash);
		}
		
		char entryHashFileDirectoryName[3];
		strncpy(entryHashFileDirectoryName, entryHash + 0, 2);
		entryHashFileDirectoryName[2] = '\0';
		
		char entryHashFileName[39];
		strncpy(entryHashFileName, entryHash + 2, 38);
		entryHashFileName[38] = '\0';
		
		char entryHashFilePath[1024];
		strcpy(entryHashFilePath, "./.async/tree/");
		strcat(entryHashFilePath, entryHashFileDirectoryName);
		strcat(entryHashFilePath, "/");
		strcat(entryHashFilePath, entryHashFileName);
		
		printf("%s\n", entryHashFilePath);
		
		int entryFile = open(entryHashFilePath, O_RDONLY);
		int entryFileSize = lseek(entryFile, 0, SEEK_END);
		lseek(entryFile, 0, SEEK_SET);
		
		char entryFileSendHeaderCommand[1024];
		strcpy(entryFileSendHeaderCommand, "sendfile ");
		strcat(entryFileSendHeaderCommand, entryHash);
		strcat(entryFileSendHeaderCommand, " ");
		
		char entryFileSizeStr[20];
		sprintf(entryFileSizeStr, "%d", entryFileSize);
		
		strcat(entryFileSendHeaderCommand, entryFileSizeStr);

		send(socket_desc, entryFileSendHeaderCommand , strlen(entryFileSendHeaderCommand), 0);
						
		int offset = 0;
		int sent_bytes = 0;
		int remain_data = entryFileSize;
		
		printf("BHOSDIKE %d\n", remain_data);
		
		sent_bytes = sendfile(socket_desc, entryFile, &offset, 1024);
		
		printf("WTF %d\n", sent_bytes);
		
		while (((sent_bytes = sendfile(socket_desc, entryFile, &offset, 1024)) > 0) && (remain_data > 0)) {
			printf("%d\n", remain_data);
			remain_data -= sent_bytes;
			printf("%d\n", remain_data);
		}
		
		close(entryFile);
	}
	
	close(hashFile);
}

int validateEmail(char* email) {
	const char *email_regex = "^([a-z0-9])(([-a-z0-9._])*([a-z0-9]))*@([a-z0-9])"
				  "(([a-z0-9-])*([a-z0-9]))+(.([a-z0-9])([-a-z0-9_-])?"
				  "([a-z0-9])+)+$";				
	regex_t preg;
	int rc, status;
	
	rc = regcomp(&preg, email_regex, REG_EXTENDED|REG_NOSUB);
	rc = regexec(&preg, email, (size_t)0, NULL, 0);
	regfree(&preg);
	
	return rc == 0 ? 1 : 0;
}

int main(int argc, char *argv[]) {
	
	if (argc == 1) { // No arguments provided for async
		errno = EINVAL;
		perror("NoArgumentsProvidedException: ");
		return EXIT_FAILURE;
	} else {		
		if (strcmp(argv[1], "init") == 0) { // Initialize an async repository						
			char command[1024];	
			strcpy(command, "sudo $HOME/bin/async/scripts/get_user.sh");
			
			FILE *fd = popen(command, "r");
			
			char authToken[41];		
			fgets(authToken, 41, fd);
			strtok(authToken, "\n");
			pclose(fd);
			if (strcmp(authToken, "0") != 0) {
				DIR* asyncRepoCheck;
				if((asyncRepoCheck = opendir("./.async")) == NULL) {
					closedir(asyncRepoCheck);
					char repoName[100];
					int isRepoNameValid = 1;
					do {
						isRepoNameValid = 1;
						printf("Repository Name: ");
						fgets(repoName, 100, stdin);
						strtok(repoName, "\n");
											
						char *strPtr = repoName;
						while (*strPtr++ && isRepoNameValid)
							if (*strPtr == ' ' && *++strPtr) isRepoNameValid = 0;
									
						
						if(!isRepoNameValid) {
							printf("Invalid Repository Name. No spaces allowed in the name.\n\n");
						}
					} while (!isRepoNameValid);
					
					int socket_desc;
					struct sockaddr_in server_socket;
					char server_reply[41];
					
					// SERVER RESPONSES:
					//  0 - Success
					// -1 - Invalid User Token
					// -2 - Repo already exists
					
					socket_desc = socket(AF_INET , SOCK_STREAM , 0);
					if (socket_desc == -1){
						printf("Some error occurred! Please try again or check if the server is working.\n");
						return EXIT_FAILURE;
					}
					puts("Socket created");
					
					server_socket.sin_addr.s_addr = inet_addr("127.0.0.1");
					server_socket.sin_family = AF_INET;
					server_socket.sin_port = 8080;

					if (connect(socket_desc , (struct sockaddr *)&server_socket , sizeof(server_socket)) < 0){
						printf("Some error occurred! Please try again or check if the server is working.\n");
						return EXIT_FAILURE;
					}
					
					puts("Connected\n");
					
					char message[2048];
					strcpy(message, "init ");
					strcat(message, authToken);
					strcat(message, " ");
					strcat(message, repoName);
					
					if(send(socket_desc , message , strlen(message) , 0) < 0){
						printf("Some error occurred! Please try again or check if the server is working.\n");
						close(socket_desc);
						return EXIT_FAILURE;
					}
					
					if( recv(socket_desc , server_reply , 3 , 0) < 0) {
						printf("Some error occurred! Please try again or check if the server is working.\n");
						close(socket_desc);
						return EXIT_FAILURE;
					}
					
					if (strcmp(server_reply, "-1") == 0) {
						printf("Unauthorized Access! You are not a valid user.\n");
						return EXIT_FAILURE;
					} else if (strcmp(server_reply, "-2") == 0) {
						printf("Invalid repository name! A repository with this name already exists. Please try again with a different name.\n");
						return EXIT_FAILURE;
					} else {
						if ((mkdir("./.async", 0777)) == 0) {
							mkdir("./.async/tree", 0777);
							mkdir("./.async/tree/temp", 0777);
							
							FILE *logFile = fopen("./.async/log", "w+");
							fclose(logFile);
							
							FILE *infoFile = fopen("./.async/info", "w+");
							fwrite(&repoName, strlen(repoName), 1, infoFile);
							fclose(infoFile);
							
							printf("Initialized Empty Async Repository\n");
							return EXIT_SUCCESS;
						} else {
							errno = EACCES;
							perror("AsyncInitializationFailed: ");
							return EXIT_FAILURE;
						}
					}
					
					close(socket_desc);
				} else {
					printf("Invalid Command! This directory is already an async repository.\n");
					return EXIT_FAILURE;
				}
			} else {
				printf("You need to be logged-in to initialize an Async repository\n");
				return EXIT_FAILURE;
			}
		} else if (strcmp(argv[1], "config") == 0) { // Configure Async User					
			if(argc == 4) {	
				if (strcmp(argv[2], "--email") == 0 || strcmp(argv[2], "--password") == 0) {
					int shm_fd;
					if((shm_fd = shm_open("output_shm", O_CREAT | O_RDWR, 0600)) == -1) {
						printf("Error opening shared memory in Parent\n");
						return EXIT_FAILURE;
					} else {
						ftruncate(shm_fd, COMMAND_OUTPUT_SIZE);
						void* data;
						if((data = mmap(NULL, COMMAND_OUTPUT_SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
							printf("Error mapping to shared memory in child.\n");
							shm_unlink("output_shm");
							return EXIT_FAILURE;
						} else {
							if(fork() == 0) {
								char command[1024];	
								strcpy(command, "sudo $HOME/bin/async/scripts/set_user.sh ");
								
								if (strcmp(argv[2], "--email") == 0) {
									strcat(command, "--email ");
								}  else if (strcmp(argv[2], "--password") == 0) {
									strcat(command, "--password ");
								}									
								strcat(command, argv[3]);
								
								FILE *fd = popen(command, "r");
									
								char *output = (char *) data;
								fgets(output, COMMAND_OUTPUT_SIZE, fd);				
								strtok(output, "\n");										
								
								pclose(fd);
							} else {
								wait(NULL);
								char *output = (char *) data;											
								
								if (strcmp(output, "0") == 0) {
									printf("Successfully updated");
									if (strcmp(argv[2], "--email") == 0) {
										printf(" email!!\n");
									}  else if (strcmp(argv[2], "--password") == 0) {
										printf(" password!!\n");
									}
								} else if (strcmp(output, "-1") == 0) {
									printf("Can not perform action. Please run 'async_vc logout' before updating user\n");
								}
								shm_unlink("output_shm");
							}
						}
					}
				} else {
					printf("Invalid command! Use either 'async_vc config --email <your_email>' or 'async_vc config --password <your_password>'\n");
					return EXIT_FAILURE;
				}
			} else {
				printf("Invalid command! Use either 'async_vc config --email <your_email>' or 'async_vc config --password <your_password>'\n");
				return EXIT_FAILURE;
			}					
		} else if (strcmp(argv[1], "createuser") == 0) {
			printf("Create a new Async user:\n");
			
			char email[100];
			int isEmailValid = 1;
			do {
				printf("Enter email: ");
				fgets(email, 100, stdin);
				strtok(email, "\n");
				isEmailValid = validateEmail(email);					
				
				if(!isEmailValid) {
					printf("Invalid email.\n\n");
				}
			} while (!isEmailValid);
			
			char password[100];
			int isPassValid = 1;
			do {
				printf("Enter password: ");
				fgets(password, 100, stdin);
				strtok(password, "\n");
				isPassValid = strlen(password) >= 6 && strlen(password) <= 16;
				if (isPassValid) {
					char *strPtr = password;
					while (*strPtr++ && isPassValid)
						if (*strPtr == ' ' && *++strPtr) isPassValid = 0;
							
				}
				if(!isPassValid) {
					printf("Invalid password. Password must be atleast 6 to atmost 16 characters long and have no spaces.\n\n");
				}
			} while (!isPassValid);
			
			char command[1024];	
			strcpy(command, "echo -n \"");
			strcat(command, password);
			strcat(command, "\" | sha1sum | awk '{print $1}'");					
			
			FILE *fd = popen(command, "r");
												
			char hashedPassword[41];
			fgets(hashedPassword, 41, fd);				
			strtok(hashedPassword, "\n");										
			
			pclose(fd);
			
			int socket_desc;
			struct sockaddr_in server_socket;
			char server_reply[3];
			
			// SERVER RESPONSES:
			//  0 - Success
			// -1 - Process Error
			// -2 - User Exists
			
			socket_desc = socket(AF_INET , SOCK_STREAM , 0);
			if (socket_desc == -1){
				printf("Some error occurred! Please try again or check if the server is working.");
				return EXIT_FAILURE;
			}
			
			server_socket.sin_addr.s_addr = inet_addr("127.0.0.1");
			server_socket.sin_family = AF_INET;
			server_socket.sin_port = 8080;

			if (connect(socket_desc , (struct sockaddr *)&server_socket , sizeof(server_socket)) < 0){
				printf("Some error occurred! Please try again or check if the server is working.");
				return EXIT_FAILURE;
			}
			
			char message[2048];
			strcpy(message, "createuser ");
			strcat(message, email);
			strcat(message, " ");
			strcat(message, hashedPassword);
			
			if(send(socket_desc , message , strlen(message) , 0) < 0){
				printf("Some error occurred! Please try again or check if the server is working.");
				close(socket_desc);
				return EXIT_FAILURE;
			}
			
			if( recv(socket_desc , server_reply , 3 , 0) < 0) {
				printf("Some error occurred! Please try again or check if the server is working.");
				close(socket_desc);
				return EXIT_FAILURE;
			}
			
			if (strcmp(server_reply, "0") == 0) {
				printf("User created successfully!\n");
			} else if (strcmp(server_reply, "-2") == 0) {
				printf("User with this email already exists!\n");
			}
			
			close(socket_desc);
		} else if (strcmp(argv[1], "login") == 0) {
			char command[1024];	
			strcpy(command, "sudo $HOME/bin/async/scripts/get_user.sh");
			
			FILE *fd = popen(command, "r");
			
			int i = 0;
			int isLoggedIn = 0;
			char email[100], pass[100];
			char output[100];		
			while(fgets(output, 100, fd) > 0) {
				strtok(output, "\n");
				if (i == 0) {
					if (strcmp(output, "0") != 0) {
						isLoggedIn = 1;
						break;
					}
				} else if (i == 1) {
					strcpy(email, output);
					strtok(email, "\n");
				} else if (i == 2) {
					strcpy(pass, output);
					strtok(pass, "\n");	
				}
				
				i++;
			}
			
			pclose(fd);
			
			if (isLoggedIn == 1) {
				printf("Already logged in! Execute 'async_vc logout' to logout.\n");
				return EXIT_FAILURE;
			} else if (strlen(email) > 1 && strlen(pass) > 1) {
				char passHashCommand[1024];	
				strcpy(passHashCommand, "echo -n \"");
				strcat(passHashCommand, pass);
				strcat(passHashCommand, "\" | sha1sum | awk '{print $1}'");					
				
				fd = popen(passHashCommand, "r");
													
				char hashedPassword[41];
				fgets(hashedPassword, 41, fd);				
				strtok(hashedPassword, "\n");										
				
				pclose(fd);
				
				int socket_desc;
				struct sockaddr_in server_socket;
				char server_reply[41];
				
				// SERVER RESPONSES:
				//  0 - Success
				// -1 - Process Error
				// -2 - User Exists
				
				socket_desc = socket(AF_INET , SOCK_STREAM , 0);
				if (socket_desc == -1){
					printf("Some error occurred! Please try again or check if the server is working.\n");
					return EXIT_FAILURE;
				}
				puts("Socket created");
				
				server_socket.sin_addr.s_addr = inet_addr("127.0.0.1");
				server_socket.sin_family = AF_INET;
				server_socket.sin_port = 8080;

				if (connect(socket_desc , (struct sockaddr *)&server_socket , sizeof(server_socket)) < 0){
					printf("Some error occurred! Please try again or check if the server is working.\n");
					return EXIT_FAILURE;
				}
				
				puts("Connected\n");
				
				char message[2048];
				strcpy(message, "login ");
				strcat(message, email);
				strcat(message, " ");
				strcat(message, hashedPassword);
				
				if(send(socket_desc , message , strlen(message) , 0) < 0){
					printf("Some error occurred! Please try again or check if the server is working.\n");
					close(socket_desc);
					return EXIT_FAILURE;
				}
				
				if( recv(socket_desc , server_reply , 41 , 0) < 0) {
					printf("Some error occurred! Please try again or check if the server is working.\n");
					close(socket_desc);
					return EXIT_FAILURE;
				}
				
				if (strcmp(server_reply, "-1") == 0) {
					printf("Invalid credentials! User does not exist.\n");
				} else {
					char authTokenSaveCommand[1024];	
					strcpy(authTokenSaveCommand, "sudo $HOME/bin/async/scripts/set_user.sh --login ");
					strcat(authTokenSaveCommand, server_reply);			
					
					fd = popen(authTokenSaveCommand, "r");
					pclose(fd);
					
					printf("AUTH TOKEN: %s\n", server_reply);
				}
				
				close(socket_desc);
			} else {
				printf("Credentials not provided! Set them using 'async_vc config'\n");
				return EXIT_FAILURE;
			}			
			
			
		} else if (strcmp(argv[1], "logout") == 0) {						
			int shm_fd;
			if((shm_fd = shm_open("output_shm", O_CREAT | O_RDWR, 0600)) == -1) {
				printf("Error opening shared memory in Parent\n");
				return EXIT_FAILURE;
			} else {
				ftruncate(shm_fd, COMMAND_OUTPUT_SIZE);
				void* data;
				if((data = mmap(NULL, COMMAND_OUTPUT_SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
					printf("Error mapping to shared memory in child.\n");
					shm_unlink("output_shm");
					return EXIT_FAILURE;
				} else {
					if(fork() == 0) {
						FILE *fd = popen("sudo $HOME/bin/async/scripts/set_user.sh --logout", "r");
							
						char *output = (char *) data;
						fgets(output, COMMAND_OUTPUT_SIZE, fd);		
						strtok(output, "\n");
					
						pclose(fd);
					} else {
						wait(NULL);
						char *output = (char *) data;											
					
						if (strcmp(output, "0") == 0) {
							printf("Logged Out!!\n");									
						} else if (strcmp(output, "-1") == 0) {
							printf("Invalid command! No current user found.\n");
						}
						shm_unlink("output_shm");
					}
				}
			}
		} else {			
			DIR* asyncRepoCheck;
			if((asyncRepoCheck = opendir("./.async")) != NULL) {				
				closedir(asyncRepoCheck);							
				if (strcmp(argv[1], "sync") == 0) {
					system("async_watcher .");
				} else if (strcmp(argv[1], "commit") == 0) {
					// List of all files
					int indexes[1] = {0};
					addTree(0, indexes, ".");
					
					int *slashesCount = (int*) malloc(sizeof(int));
					char **files = (char**) malloc(sizeof(char*));
					int filesCount = 0;
					listdir("", &files, &slashesCount, &filesCount);												
					
					int j, k;
					char slash = '/';
					for (int i = 0; i < filesCount; i++) {
						for (j = i + 1; j < filesCount; j++) {
							if (slashesCount[i] < slashesCount[j]) {
								char *temp = files[i];
								files[i] = files[j];
								files[j] = temp;
								
								int tempSlashCount = slashesCount[i];
								slashesCount[i] = slashesCount[j];
								slashesCount[j] = tempSlashCount;
							}
						}
					}
										
					for (int i = 0; i < filesCount; i++) {
						TreeNode *treeNode = fileRoot;
						char *token = strtok(files[i], "/");
						
						int *parentIndexes = (int*) calloc(slashesCount[i], sizeof(int));
						int j = 0;
						
						int level = 0;
						
						while (token != NULL) {
							int index = 0;
							if (treeNode->child != NULL) {
								treeNode = treeNode->child;
								int hasSibling = 0;
								TreeNode *siblingsPtr = treeNode;
								while(siblingsPtr) {
									
									if(strcmp(siblingsPtr->name, token) == 0) {
										hasSibling = 1;
										parentIndexes[j++] = index;										
										break;
									}
									
									index++;
									
									siblingsPtr = siblingsPtr->sibling;									
								}
								
								if(!hasSibling) {
									addTree(level + 1, parentIndexes, token);
									parentIndexes[j++] = index;
									for (int k = 0; k < index; k++) {
										treeNode = treeNode->sibling;
									}
								}
							} else {
								addTree(level + 1, parentIndexes, token);
								parentIndexes[j++] = index;
								treeNode = treeNode->child;
							}					
							
							token = strtok(NULL, "/");	
							level++;
						}
						
						free(parentIndexes);
					}
					
					generateCommit(fileRoot, "", "", NULL);
					
					int socket_desc;
					struct sockaddr_in server_socket;
					char server_reply[41];
					
					socket_desc = socket(AF_INET , SOCK_STREAM , 0);
					if (socket_desc == -1){
						printf("Some error occurred! Please try again or check if the server is working.\n");
						return EXIT_FAILURE;
					}
					puts("Socket created");
					
					server_socket.sin_addr.s_addr = inet_addr("127.0.0.1");
					server_socket.sin_family = AF_INET;
					server_socket.sin_port = 8080;

					if (connect(socket_desc , (struct sockaddr *)&server_socket , sizeof(server_socket)) < 0){
						printf("Some error occurred! Please try again or check if the server is working.\n");
						return EXIT_FAILURE;
					}
					
					puts("Connected\n");
					
					char command[1024];	
					strcpy(command, "sudo $HOME/bin/async/scripts/get_user.sh");
					
					FILE *fd = popen(command, "r");
					
					char authToken[41];		
					fgets(authToken, 41, fd);
					strtok(authToken, "\n");
					pclose(fd);
					
					char repoName[100];
					FILE *infoFile = fopen("./.async/info", "r");
					fgets(repoName, 100, infoFile);
					strtok(repoName, "\n");
					fclose(infoFile);
					
					printf("REPO NAME: %s\n", repoName);
					
					char message[2048];
					strcpy(message, "commit ");
					strcat(message, authToken);
					strcat(message, " ");
					strcat(message, repoName);
					
					if(send(socket_desc , message , strlen(message) , 0) < 0){
						printf("Some error occurred! Please try again or check if the server is working.\n");
						close(socket_desc);
						return EXIT_FAILURE;
					}					
					
					FILE *logFile = fopen("./.async/log", "r");

					LogEntry lastCommit;
					while(fread(&lastCommit, sizeof(LogEntry), 1, logFile) == 1 );
					fclose(logFile);
					
					char commitDirectoryName[3];
					strncpy(commitDirectoryName, lastCommit.this_commit + 0, 2);
					commitDirectoryName[2] = '\0';
					
					char commitFileName[39];
					strncpy(commitFileName, lastCommit.this_commit + 2, 38);
					commitFileName[38] = '\0';
					
					
					char filePath[1024];
					strcpy(filePath, "./.async/tree/");
					strcat(filePath, commitDirectoryName);
					strcat(filePath, "/");
					strcat(filePath, commitFileName);
					
					int fp = open(filePath, O_RDONLY);

					int fileSize = lseek(fp, 0, SEEK_END);
					lseek(fp, 0, SEEK_SET);
					
					char fileSendHeaderCommand[1024];
					strcpy(fileSendHeaderCommand, "sendfile ");
					strcat(fileSendHeaderCommand, lastCommit.this_commit);
					strcat(fileSendHeaderCommand, " ");
					
					char fileSizeStr[20];
					sprintf(fileSizeStr, "%d", fileSize);
					
					strcat(fileSendHeaderCommand, fileSizeStr);

					send(socket_desc , fileSendHeaderCommand , strlen(fileSendHeaderCommand), 0);
					
					int offset = 0;
					int sent_bytes = 0;
					int remain_data = fileSize;
					/* Sending file data */
					while (((sent_bytes = sendfile(socket_desc, fp, &offset, 1024)) > 0) && (remain_data > 0)) {
						printf("WTFF");
						remain_data -= sent_bytes;
					}
					close(fp);
					
					//pushToServer(socket_desc, lastCommit.this_commit);

					close(socket_desc);
					
					/*FILE *logFile = fopen("./.async/log", "r+");
					
					/*struct logEntry input1 = {"0000000000000000000000000000000000000000", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}; 
					struct logEntry input2 = {"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"}; 
					
					fwrite(&input1, sizeof(struct logEntry), 1, logFile);					
					fwrite(&input2, sizeof(struct logEntry), 1, logFile);
					
					int logsCount = 0;
					fseek(logFile, 0, SEEK_SET);
					while(fread(&logEnt, sizeof(struct logEntry), 1, logFile) == 1 ) {
						printf("Previous Commit: %s\n", logEnt.prev_commit);
						printf("This Commit: %s\n", logEnt.this_commit);
						printf("\n");
						logsCount++;
					}
					
					if(logsCount == 0) {
						for(int i = 0; i < 1; i++) {//filesCount; i++) {
							printf("File: %s\n", files[i]);
							
							char *fileNameCopy = (char*) malloc(sizeof(char) * (strlen(files[i]) + 1));
							strcpy(fileNameCopy, files[i]);
							fileNameCopy[strlen(files[i])] = '\0';
							
							struct timeval tv;

							gettimeofday(&tv,NULL);
							long long tempFileName = (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
							
							char tempFileNameStr[30];
							snprintf(tempFileNameStr, 20, "%lld", tempFileName);										
							
							struct lastSavedBlob lastSaved;
							strcpy(lastSaved.type, "blob");							
							
							generateFileBlob("", fileNameCopy, 0, tempFileNameStr, lastSaved);
							printf("Committed: %s\n", files[i]);
							
							free(fileNameCopy);
						}
					}
					
					fclose(logFile);*/
					
					// Free malloc array
					free(files);		
					free(slashesCount);
				}
			} else {
				printf("Not a valid Async repository\n");
				return EXIT_FAILURE;
			}			
		}
	}
	
	
	/*char *file1_path = "files/file01_new.txt";
	char *file2_path = "files/file02.txt";
	
	FILE *file1, *file1Sig, *file2, *file2Delta, *file3;
	file1 = fopen(file1_path, "r");
	file1Sig = fopen("files/.gen/file01_new_signature.sig", "w+");	

	rs_result res1 = rs_sig_file(file1, file1Sig, 1, 2, NULL);

	printf("Result code1: %d\n", res1);	
	
	fclose(file1Sig);

	file1Sig = fopen("files/.gen/file01_signature.sig", "r");
	file2 = fopen(file2_path, "r");
	file2Delta = fopen("files/.gen/file02_delta.delt", "w+");

	rs_signature_t *file1Signature;

	rs_loadsig_file(file1Sig, &file1Signature, NULL);

	rs_build_hash_table(file1Signature);

	rs_result res2 = rs_delta_file(file1Signature, file2, file2Delta, NULL);

	printf("Result code2: %d\n", res2);
	
	fclose(file1Sig);
	fclose(file2);
	fclose(file2Delta);
	
	file2Delta = fopen("files/.gen/file02_delta.delt", "r");
	file3 = fopen("files/.gen/file03_file01_recreated.txt", "w+");

	rs_result res3 = rs_patch_file(file1, file2Delta, file3, NULL);
	
	printf("Result code3: %d\n", res3);
	
	fclose(file1);
	fclose(file2Delta);
	fclose(file3);*/

	return 0;
}
