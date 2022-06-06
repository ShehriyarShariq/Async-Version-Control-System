#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<pthread.h>
#include <dirent.h>
#include <errno.h>

//the thread function
void *connection_handler(void *);
void write_file(int, char*, int);

int main(int argc , char *argv[]){
	int socket_desc , client_sock , c , *new_sock;
	struct sockaddr_in server , client;
	
	//Create socket
	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1)
	{
		printf("Could not create socket");
	}
	puts("Socket created");
	
	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_port = 8080;
	
	//Bind
	if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
	{
		//print the error message
		perror("bind failed. Error");
		return 1;
	}
	puts("bind done");
	
	//Listen
	listen(socket_desc , 3);
	
	//Accept and incoming connection
	puts("Waiting for incoming connections...");
	c = sizeof(struct sockaddr_in);
	while( (client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c)) )
	{
		puts("Connection accepted");
		
		pthread_t sniffer_thread;
		new_sock = malloc(1);
		*new_sock = client_sock;
		
		if( pthread_create( &sniffer_thread , NULL ,  connection_handler , (void*) new_sock) < 0)
		{
			perror("could not create thread");
			return 1;
		}
		
		//Now join the thread , so that we dont terminate before the thread
		//pthread_join( sniffer_thread , NULL);
		puts("Handler assigned");
	}
	
	if (client_sock < 0)
	{
		perror("accept failed");
		return 1;
	}
	
	return 0;
}

void write_file(int sockfd, char *fileName, int fileSize){
	int n;
	FILE *fp;
	char buffer[1024];

	char hashFileDirectoryName[3];
	strncpy(hashFileDirectoryName, fileName + 0, 2);
	hashFileDirectoryName[2] = '\0';
	
	char hashFileName[39];
	strncpy(hashFileName, fileName + 2, 38);
	hashFileName[38] = '\0';
	
	char filePath[1024];
	strcpy(filePath, "./");
	strcat(filePath, hashFileDirectoryName);
	
	mkdir(filePath, 0777);
	
	strcat(filePath, "/");
	strcat(filePath, hashFileName);

	fp = fopen(filePath, "w+");
	
	ssize_t len;
	int remain_data = fileSize;
	
	while ((remain_data > 0) && ((len = recv(sockfd, buffer, 1024, 0)) > 0)){
		printf("%s\n", buffer);
		/*if(strstr(buffer, "sendfile") != NULL) {
			char *fileCommand = strtok(buffer, " ");
			fileCommand = strtok(NULL, " ");
			char fileName[41];
			strcpy(fileName, fileCommand);
			
			fileCommand = strtok(NULL, " ");
			
			char fileSizeStr[20];
			strcpy(fileSizeStr, fileCommand);
			int fileSize = atoi(fileSizeStr);
			
			printf("FILE NAME: %s\n", fileName);
			write_file(sockfd, fileName, fileSize);
		} else {*/
			fputs(buffer, fp);
			remain_data -= len;
		//}
        }
	
	fclose(fp);
	return;
}

/*
 * This will handle connection for each client
 * */
void *connection_handler(void *socket_desc) {
	//Get the socket descriptor
	int sock = *(int*)socket_desc;
	int read_size;
	char message[2048] , client_message[2048];
	
	//Receive a message from client
	read_size = recv(sock , client_message , 2048 , 0);	
	
	char *token = strtok(client_message, " ");
	char command[20];
	strcpy(command, token);
	token = strtok(NULL, " ");
	
	if (strcmp(command, "createuser") == 0) {
		char email[100];
		strcpy(email, token);
		
		token = strtok(NULL, " ");
		char password[41];
		strcpy(password, token);
		
		char command[1024];	
		strcpy(command, "sudo $HOME/bin/async_server/scripts/create_user.sh ");
		strcat(command, email);
		strcat(command, " ");
		strcat(command, password);
		
		FILE *fd = popen(command, "r");
											
		char output[3];
		fgets(output, 3, fd);				
		strtok(output, "\n");										
		
		pclose(fd);
		
		write(sock , output , strlen(output));
	} else if (strcmp(command, "login") == 0) {
		char email[100];
		strcpy(email, token);
		
		token = strtok(NULL, " ");
		char password[41];
		strcpy(password, token);
		
		char command[1024];	
		strcpy(command, "sudo $HOME/bin/async_server/scripts/check_user.sh ");
		strcat(command, email);
		strcat(command, " ");
		strcat(command, password);
		
		FILE *fd = popen(command, "r");
											
		char output[41];
		fgets(output, 41, fd);				
		strtok(output, "\n");										
		
		pclose(fd);
		
		write(sock , output , strlen(output));
	} else if (strcmp(command, "init") == 0) {
		char authToken[100];
		strcpy(authToken, token);
		
		token = strtok(NULL, " ");
		char repoName[100];
		strcpy(repoName, token);
		
		char currentPath[1024];
		getcwd(currentPath, 1024);
		strcat(currentPath, "/");
		strcat(currentPath, authToken);
		strcat(currentPath, "/");
		strcat(currentPath, repoName);
		
		char command[4096];	
		strcpy(command, "sudo $HOME/bin/async_server/scripts/init_repo.sh ");
		strcat(command, authToken);
		strcat(command, " ");
		strcat(command, repoName);
		strcat(command, " \"");
		strcat(command, currentPath);
		strcat(command, "\"");
		
		FILE *fd = popen(command, "r");
											
		char output[3];
		fgets(output, 3, fd);				
		strtok(output, "\n");							
		
		pclose(fd);	
		
		if (strcmp(output, "0") == 0) {
			char userDirPath[43];
			strcpy(userDirPath, "./");
			strcat(userDirPath, authToken);
			
			DIR *dir = opendir(userDirPath);
			if (ENOENT == errno) {
				mkdir(userDirPath, 0777);
			} else if (dir) {
				closedir(dir);
			}
			
			mkdir(currentPath, 0777);
			
			strcat(currentPath, "/.async");
			mkdir(currentPath, 0777);
			
			char logFilePath[1024];
			strcpy(logFilePath, currentPath);
			strcat(logFilePath, "/log");
			FILE *logFile = fopen(logFilePath, "w+");
			fclose(logFile);
						
			strcat(currentPath, "/tree");
			mkdir(currentPath, 0777);
			
			strcat(currentPath, "/tree/temp");
			mkdir(currentPath, 0777);			
		}
		
		write(sock , output , strlen(output));
	} else if (strcmp(command, "commit") == 0) {
		char authToken[100];
		strcpy(authToken, token);
		
		token = strtok(NULL, " ");
		char repoName[100];
		strcpy(repoName, token);
		
		printf("SERVER: %s    |    %s\n", authToken, repoName);
		
		int read_size;
		char buffer[1024];
		while((read_size = recv(sock , buffer , 4096 , 0)) > 0 ) {
			char *fileCommand = strtok(buffer, " ");
			if(strcmp(fileCommand, "sendfile") == 0) {
				fileCommand = strtok(NULL, " ");
				char fileName[41];
				strcpy(fileName, fileCommand);
				
				fileCommand = strtok(NULL, " ");
				
				char fileSizeStr[20];
				strcpy(fileSizeStr, fileCommand);
				int fileSize = atoi(fileSizeStr);
				
				printf("FILE NAME: %s\n", fileName);
				write_file(sock, fileName, fileSize);
			}
		}
	}
		
	fflush(stdout);
		
	//Free the socket pointer
	free(socket_desc);
	
	return 0;
}