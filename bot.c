/*
	virgil - scruffy AI chatbot
	Copyright (C) 2009 Edward Cree

	See virgil.c for license information
	
	bot.c: IRC bot stub
*/

#define _GNU_SOURCE // feature test macro

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

char * fgetl(FILE *);
int tx(int fd, char * packet); // tx/rx talk to IRC
int rx(int fd, char ** data);
int hear(int fd, char ** msg); // hear talks to VIRGIL

int main(int argc, char *argv[])
{
	char *conf;
	char *serverloc="irc.freenode.net", *portno="6667", *nick="VirgilBot", *chan="#sii", *netf="net"; // defaults
	int awkw=30;
	char pref='!';
	float awkp=0.9;
	bool slnt=false;
	int arg;
	for(arg=1;arg<argc;arg++)
	{
		if(strncmp(argv[arg], "--serv=", 7)==0)
		{
			serverloc=argv[arg]+7;
		}
		else if(strncmp(argv[arg], "--port=", 7)==0)
		{
			portno=argv[arg]+7;
		}
		else if(strncmp(argv[arg], "--nick=", 7)==0)
		{
			nick=argv[arg]+7;
		}
		else if(strncmp(argv[arg], "--chan=", 7)==0)
		{
			chan=argv[arg]+7;
		}
		else if(strncmp(argv[arg], "--netf=", 7)==0)
		{
			netf=argv[arg]+7;
		}
		else if(strncmp(argv[arg], "--awkw=", 7)==0)
		{
			sscanf(argv[arg]+7, "%d", &awkw);
		}
		else if(strncmp(argv[arg], "--awkp=", 7)==0)
		{
			sscanf(argv[arg]+7, "%g", &awkp);
		}
		else if(strncmp(argv[arg], "--pref=", 7)==0)
		{
			pref=argv[arg][7];
		}
		else if(strcmp(argv[arg], "--slnt")==0)
		{
			slnt=true;
		}
		else if(strcmp(argv[arg], "--no-slnt")==0)
		{
			slnt=false;
		}
		else if(strncmp(argv[arg], "--conf=", 7)==0)
		{
			conf=argv[arg]+7;
			FILE *fconf=fopen(conf, "r");
			if(!fconf)
			{
				printf("bot: failed to open file \"%s\"\n", conf);
				perror("bot: fopen");
			}
			else
			{
				char *line;
				while((line=fgetl(fconf)))
				{
					if(strncmp(line, "SERV ", 5)==0)
					{
						serverloc=strdup(line+5);
					}
					else if(strncmp(line, "PORT ", 5)==0)
					{
						portno=strdup(line+5);
					}
					else if(strncmp(line, "NICK ", 5)==0)
					{
						nick=strdup(line+5);
					}
					else if(strncmp(line, "CHAN ", 5)==0)
					{
						chan=strdup(line+5);
					}
					else if(strncmp(line, "NETF ", 5)==0)
					{
						netf=strdup(line+5);
					}
					else if(strncmp(line, "AWKW ", 5)==0)
					{
						sscanf(line+5, "%d", &awkw);
					}
					else if(strncmp(line, "AWKP ", 5)==0)
					{
						sscanf(line+5, "%g", &awkp);
					}
					else if(strncmp(line, "PREF ", 5)==0)
					{
						pref=line[5];
					}
					else if(strncmp(line, "SLNT ", 5)==0)
					{
						slnt=(line[5]=='+');
					}
					free(line);
				}
			}
		}
	}
	int wp[2],rp[2],e;
	int ww,rr;
	if((e=pipe(wp)))
	{
		fprintf(stderr, "bot: Error: Failed to create pipe: %s\n", strerror(errno));
		return(3);
	}
	if((e=pipe(rp)))
	{
		fprintf(stderr, "bot: Error: Failed to create pipe: %s\n", strerror(errno));
		return(3);
	}
	int pid=fork();
	switch(pid)
	{
		case -1: // failure
			fprintf(stderr, "bot: Error: failed to fork virgil: %s\n", strerror(errno));
		break;
		case 0: // child
			{
				ww=rp[1];close(rp[0]);
				rr=wp[0];close(wp[1]);
				if((e=dup2(ww, STDERR_FILENO))==-1)
				{
					fprintf(stderr, "bot.child: Error: Failed to redirect stderr with dup2: %s\n", strerror(errno));
					return(3);
				}
				if((e=dup2(rr, STDIN_FILENO))==-1)
				{
					fprintf(stderr, "bot.child: Error: Failed to redirect stdin with dup2: %s\n", strerror(errno));
					return(3);
				}
				execl("./virgil", "virgil", "-p", netf, NULL);
				fprintf(stderr, "bot.child: Error: Failed to execl virgil: %s\n", strerror(errno));
				return(255);
			}
		break;
		default: // parent
			ww=wp[1];close(wp[0]);
			rr=rp[0];close(rp[1]);
		break;
	}
	
	FILE *fchild = fdopen(ww, "w"); // get a stream to send stuff to the child
	if(!fchild)
	{
		fprintf(stderr, "bot: Error: Failed put stream on write-pipe: %s\n", strerror(errno));
		return(3);
	}
	
	fprintf(fchild, "L\n");
	fflush(fchild);
	
	int serverhandle;
	struct addrinfo hints, *servinfo;
	printf("bot: Looking up server: %s:%s\n", serverloc, portno);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family=AF_INET;
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	int rv;
	if((rv = getaddrinfo(serverloc, portno, &hints, &servinfo)) != 0)
	{
		fprintf(stderr, "bot: getaddrinfo: %s\n", gai_strerror(rv));
		return(1);
	}
	char sip[INET_ADDRSTRLEN];
	printf("bot: running, connecting to server...\n");
	struct addrinfo *p;
	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next)
	{
		inet_ntop(p->ai_family, &(((struct sockaddr_in*)p->ai_addr)->sin_addr), sip, sizeof(sip));
		printf("bot: connecting to %s\n", sip);
		if((serverhandle = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{
			perror("bot: socket");
			continue;
		}
		if(connect(serverhandle, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(serverhandle);
			perror("bot: connect");
			continue;
		}
		break;
	}
	if (p == NULL)
	{
		fprintf(stderr, "bot: failed to connect\n");
		return(1);
	}
	freeaddrinfo(servinfo); // all done with this structure
	
	char *nickmsg=(char *)malloc(6+strlen(nick));
	sprintf(nickmsg, "NICK %s", nick);
	tx(serverhandle, nickmsg);
	free(nickmsg);
	tx(serverhandle, "USER virgil dev-null newnet :Virgil AI Chatbot");
	
	fd_set master, readfds;
	FD_ZERO(&master);
	FD_SET(fileno(stdin), &master);
	FD_SET(serverhandle, &master);
	int errupt=0;
	char *packet=NULL;
	char *qmsg=NULL;
	int state=0;
	while(!errupt)
	{
		struct timeval timeout;
		timeout.tv_sec=awkw;
		timeout.tv_usec=0;
		readfds=master;
		if(select(serverhandle+1, &readfds, NULL, NULL, &timeout) == -1)
		{
			perror("bot: select");
			return(2);
		}
		if(FD_ISSET(fileno(stdin), &readfds))
		{
			getchar();
			errupt++; // for now, input just means die
			qmsg="virgil bot shut down";
		}
		if(FD_ISSET(serverhandle, &readfds))
		{
			int e;
			if((e=rx(serverhandle, &packet))!=0)
			{
				fprintf(stderr, "bot: error: rx(%d, &%p): %d\n", serverhandle, packet, e);
				errupt++;
				qmsg="error: virgil crashed";
			}
			else if(packet[0]!=0)
			{
				char *p=packet;
				printf("bot: rx: %s\n", p);
				if(p[0]==':')
				{
					p=strchr(p, ' ');
				}
				char *cmd=strtok(p, " ");
				if(strcmp(cmd, "PING")==0)
				{
					char *sender=strtok(NULL, " ");
					char *pong=(char *)malloc(15+strlen(sender));
					sprintf(pong, "PONG virgil %s", sender+1);
					tx(serverhandle, pong);
					free(pong);
				}
				else if(strcmp(cmd, "MODE")==0)
				{
					if(state==0)
					{
						state=1;
						char *joinmsg=(char *)malloc(6+strlen(chan));
						sprintf(joinmsg, "JOIN %s", chan);
						tx(serverhandle, joinmsg);
						free(joinmsg);
					}
				}
				else if(strcmp(cmd, "PRIVMSG")==0)
				{
					if(state==1) // lots of non-error-checked code here
					{
						char *dest=strtok(NULL, " ");
						char *msg=dest+strlen(dest)+1; // prefixed with :
						char *src=packet+1;
						char *bang=strchr(src, pref);
						if(bang)
							*bang=0;
						if(strcmp(dest, chan)==0) // to channel; check for !
						{
							bool skip=false;
							bool notme=(msg[1]!=pref) || slnt;
							bool noans=false;
							if(strchr("LSX", msg[2]) && !msg[3])
							{
								if(strcmp(src, "soundandfury")!=0) // checking on nick is a bad way to do it, though
								{
									skip=true;
								}
								noans=true;
							}
							if(strstr(msg, "://")) // block URLs, they confuse the poor thing
								skip=true;
							if(!skip && (strlen(msg)>2))
							{
								if(notme)
									fprintf(fchild, "L\n\t%s\nS\n", msg+1);
								else
									fprintf(fchild, "L\n%s\nS\n", msg+2);
								fflush(fchild);
								if(!(noans || notme))
								{
									char *vsay=NULL;
									hear(rr, &vsay);
									off_t o;
									for(o=2;o<strlen(vsay);o++)
									{
										vsay[o]=tolower(vsay[o]);
									}
									fprintf(stderr, "%s", vsay);
									char *resp=(char *)malloc(64+strlen(chan)+strlen(src)+(vsay==NULL?6:strlen(vsay)));
									sprintf(resp, "PRIVMSG %s :%s:%s", chan, src, vsay?vsay:"Error");
									tx(serverhandle, resp);
									free(resp);
									if(vsay) free(vsay);
								}
							}
						}
					}
				}
				free(packet);
			}
		}
		else if((rand()>(RAND_MAX*awkp)) && !slnt)
		{
			fprintf(fchild, "L\nWell?\nS\n");
			fflush(fchild);
			char *vsay=NULL;
			hear(rr, &vsay);
			off_t o;
			for(o=2;o<strlen(vsay);o++)
			{
				vsay[o]=tolower(vsay[o]);
			}
			fprintf(stderr, "%s", vsay);
			char *resp=(char *)malloc(64+strlen(chan)+(vsay==NULL?6:strlen(vsay)));
			sprintf(resp, "PRIVMSG %s :%s", chan, vsay?vsay:"Error");
			tx(serverhandle, resp);
			free(resp);
			if(vsay) free(vsay);
		}
	}
	printf("bot: closing\n");
	fprintf(fchild, "X\n");
	fflush(fchild);
	fclose(fchild);
	if(!qmsg) qmsg="Quit.";
	char *quit = (char *)malloc(7+strlen(qmsg));
	sprintf(quit, "QUIT %s", qmsg);
	tx(serverhandle, quit);
	free(quit);
	close(serverhandle);
	printf("bot: closed\n");
	return(0);
}

char * fgetl(FILE * stream)
{
	char * lout = (char *)malloc(80);
	int i=0;
	char c;
	while(!feof(stream))
	{
		c = fgetc(stream);
		if((c == '\n') || (c == '\r'))
			break;
		if(c != 0)
			lout[i++]=c;
		if((i%80) == 0)
		{
			if((lout = (char *)realloc(lout, i+80))==NULL)
			{
				free(lout);
				fprintf(stderr, "\nNot enough memory to store input!\n");
				return(NULL);
			}
		}
	}
	lout[i]=0;
	lout=(char *)realloc(lout, i+1);
	if(!i)
	{
		if(lout) free(lout);
		return(NULL);
	}
	return(lout);
}

int rx(int fd, char ** data)
{
	*data=(char *)malloc(512);
	if(!*data)
		return(1);
	int l=0;
	bool cr=false;
	while(!cr)
	{
		long bytes=recv(fd, (*data)+l, 1, MSG_WAITALL);
		if(bytes>0)
		{
			char c=(*data)[l];
			if((strchr("\n\r", c)!=NULL) || (l>510))
			{
				cr=true;
				(*data)[l]=0;
			}
			l++;
		}
	}
	return(0);
}

int tx(int fd, char * packet)
{
	printf("bot: tx: %s\n", packet);
	unsigned long l=strlen(packet)+1;
	unsigned long p=0;
	while(p<l)
	{
		signed long j=send(fd, packet+p, l-p, 0);
		if(j<1)
			return(p); // Something went wrong with send()!
		p+=j;
	}
	send(fd, "\n", 1, 0);
	return(l); // Return the number of bytes sent
}

int hear(int fd, char ** msg)
{
	char * lout = (char *)malloc(81);
	int i=0;
	char c;
	while(1)
	{
		int e=read(fd, &c, 1); // this is blocking, so if the child process didn't send its '\n' we will be blocked, which is bad
		if(e<1) // EOF without '\n' - we'd better put an '\n' in
			c='\n';
		if(c!=0)
		{
			lout[i++]=c;
			if((i%80)==0)
			{
				if((lout=(char *)realloc(lout, i+81))==NULL)
				{
					printf("\nNot enough memory to store input!\n");
					free(lout);
					*msg=NULL;
					return(-1);
				}
			}
		}
		if(c=='\n') // we do want to keep them this time
			break;
	}
	lout[i]=0;
	char *nlout=(char *)realloc(lout, i+1);
	if(nlout==NULL)
	{
		*msg=lout;
		return(1); // it doesn't really matter (assuming realloc is a decent implementation and hasn't nuked the original pointer), we'll just have to temporarily waste a bit of memory
	}
	*msg=nlout;
	return(0);
}
