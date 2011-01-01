#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#define port "10086"
void error_handling(char * message);

int main(int argc, char * argv[])
{
	int serv_sock;
	int clnt_sock;

	struct sockaddr_in serv_addr;
	struct sockaddr_in clnt_addr;

	socklen_t clnt_addr_size;

	char message[] = "hello world!";
	char ip[20];

	serv_sock = socket(PF_INET, SOCK_STREAM, 0);
	if(serv_sock == -1)
		error_handling("socket() error");

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(port));

	if(bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
		error_handling("bind() error");

	if(listen(serv_sock, 5) == -1)
		error_handling("listen() error");

	while(1)
	{
		memset(&clnt_addr, 0, sizeof(clnt_addr));
		clnt_addr_size = sizeof(struct sockaddr_in);

		//�ȴ������µ����ӵ����������������ӵ��׽�����������acceptĬ��Ϊ��������
		clnt_sock = accept(serv_sock,(struct sockaddr *)&clnt_addr, &clnt_addr_size);
		if(clnt_sock < 0){
			perror("accept");
			continue;
		}
		
		//���µ����ӵĶ����Ƶ�ַת���ɵ��ʮ���ƣ��Ͷ˿ں�һ���ӡ
		inet_ntop(AF_INET, (void *)&clnt_addr.sin_addr, ip, clnt_addr_size);
		printf("Remote request the current time :%s(%d)\n",ip, ntohs(clnt_addr.sin_port));

		//�������ӷ���ʱ��t,send���ط��͵����ݴ�С��ʧ�ܷ���-1
		if(send(clnt_sock, message, sizeof(message), 0))
		{
			perror("send");
		}
		//�ر����������������ӵ�ͨ�Ž���
		close(clnt_sock);
	}


//	write(clnt_sock, message, sizeof(message));
	close(serv_sock);

	return 0;		
}

	
void error_handling(char * message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

