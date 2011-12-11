/*
	virgil - scruffy AI chatbot
	Copyright (C) 2009 Edward Cree

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <SDL/SDL.h>

#define max(a,b)	((a)>(b)?(a):(b))
#define min(a,b)	((a)<(b)?(a):(b))

typedef struct
{
	int x;
	int y;
} pos;

SDL_Surface * gf_init(int x, int y);
int pset(SDL_Surface * screen, int x, int y, char r, char g, char b);
int line(SDL_Surface * screen, int x1, int y1, int x2, int y2, char r, char g, char b);

typedef enum
{
	SWITCH,
	GLOBAL,
	SESSION,
}
nodetype; // types of node in neural net

typedef struct
{
	long ptr; // position in table of nodes
	signed char bias;
	signed char val;
}
conn;

typedef struct
{
	nodetype type;
	signed char charge; // used by global nodes
	conn conns[16]; // table of connections, indexed by direction and distance
	char trig; // which node triggered this node?  Used for feedback tracking
}
node; // neural net node

typedef struct
{
	char * word;
	long ptr; // position (in table of nodes) of connected node
}
dictent;

// function protos
char * getl(char *, FILE *[2]);
void n_iter(node *, signed char *, long);
void massage(char **input, char punct, bool dside);
void replace(char *input, char old, char new);

unsigned seed; // for shared rand_r() stream

int main(int argc, char *argv[])
{
	bool pipe=false;
	char *fn="net";
	int arg;
	for(arg=1;arg<argc;arg++)
	{
		if(strcmp(argv[arg], "-p")==0)
		{
			pipe=true;
		}
		else // assume it's a filename
		{
			fn=argv[arg];
		}
	}
	// SDL stuff
	SDL_Surface * screen;SDL_Rect cls;
	if(!pipe)
	{
		screen = gf_init(OSIZ_X, OSIZ_Y);
		SDL_WM_SetCaption("Virgil - Neural Net visualiser", "Virgil - NN");
		SDL_EnableUNICODE(1);
		SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
		cls.x=0;
		cls.y=0;
		cls.w=OSIZ_X;
		cls.h=OSIZ_Y;
	}
	
	seed=1000; // seed the PRNG stream
	long nnodes=65536; // 64kn net for now (~9MB)
	printf("Alloc mem for net...\n");
	printf("\tper node: %zuB\n", sizeof(node));
	printf("\ttotal: %luB\n", nnodes*sizeof(node));
	node * net = (node *)malloc(nnodes*sizeof(node));
	if(net)
	{
		printf("Allocated OK\n");
	}
	else
	{
		printf("Failed!\n");
		perror("malloc");
		return(1);
	}
	memset(net, 0, nnodes*sizeof(node));
	printf("Alloc mem for ratings net...\n");
	printf("\tper node: %zuB\n", sizeof(node));
	printf("\ttotal: %luB\n", nnodes*sizeof(node));
	node * ranet = (node *)malloc(nnodes*sizeof(node));
	if(ranet)
	{
		printf("Allocated OK\n");
	}
	else
	{
		printf("Failed!\n");
		perror("malloc");
		return(1);
	}
	memset(ranet, 0, nnodes*sizeof(node));
	// initialise nodes
	long i;
	long dirs[16];
	for(i=0;i<16;i+=2)
	{
		dirs[i]=1<<i;
		dirs[i+1]=-i;
	}
	for(i=0;i<nnodes;i++)
	{
		// set nodetype
		switch(i%5)
		{
			case 0:
			case 1:
			case 2:
				net[i].type=ranet[i].type=SWITCH;
			break;
			case 3:
				net[i].type=ranet[i].type=GLOBAL;
			break;
			case 4:
				net[i].type=ranet[i].type=SESSION;
			break;
		}
		// fill in conn.ptrs
		int t;
		for(t=0;t<16;t++)
		{
			net[i].conns[t].ptr=ranet[i].conns[t].ptr=(i+dirs[t]+nnodes)%nnodes;
		}
	}
	long dictbuf=1024;
	printf("Alloc mem for (provisional) dict...\n");
	printf("\tper dictent: %zuB\n", sizeof(dictent));
	printf("\ttotal: %luB\n", dictbuf*sizeof(dictent));
	dictent * dict=(dictent *)malloc(dictbuf*sizeof(dictent));
	if(dict)
	{
		printf("Allocated OK\n");
	}
	else
	{
		printf("Failed!\n");
		perror("malloc");
		free(net);
		return(1);
	}
	memset(dict, 0, dictbuf*sizeof(dictent));
	long dictoff=0; // offset of next new dict entry; should always be <= dictbuf
	
	signed char charge[nnodes], racharge[nnodes]; // for switch- and session-nodes
	memset(charge, 0, sizeof(charge));
	memset(racharge, 0, sizeof(racharge));
	while(true)
	{
		char * input=strdup("");
		while(input[0]==0)
		{
			free(input);
			FILE *streams[2] = {stdout, stdin};
			input=getl(pipe?NULL:">", streams);
		}
		if(strcmp(input, "X")==0)
			break;
		if(strcmp(input, "S")==0)
		{
			// Save state (net, dict)
			char *tilde=(char *)malloc(strlen(fn)+3);
			sprintf(tilde, "%s~", fn);
			rename(fn, tilde);
			FILE *fp = fopen(fn, "w");
			fprintf(fp, "%lu,%lu\n", nnodes,dictoff);
			long n;
			for(n=0;n<nnodes;n++)
			{
				fprintf(fp, "%c%c%c\n", net[n].type, net[n].charge, net[n].trig);
				int t;
				for(t=0;t<16;t++)
				{
					fprintf(fp, "%lu,%c%c\n", net[n].conns[t].ptr, net[n].conns[t].bias, net[n].conns[t].val);
				}
			}
			for(n=0;n<nnodes;n++)
			{
				fprintf(fp, "%c%c%c\n", ranet[n].type, ranet[n].charge, ranet[n].trig);
				int t;
				for(t=0;t<16;t++)
				{
					fprintf(fp, "%lu,%c%c\n", ranet[n].conns[t].ptr, ranet[n].conns[t].bias, ranet[n].conns[t].val);
				}
			}
			long d;
			for(d=0;d<dictoff;d++)
			{
				fprintf(fp, "%lu,%s\n", dict[d].ptr, dict[d].word);
			}
			fclose(fp);
		}
		else if(strcmp(input, "L")==0)
		{
			// Load state (net, dict)
			FILE *fp = fopen(fn, "r");
			fscanf(fp, "%lu,%lu\n", &nnodes,&dictoff);
			printf("Realloc mem for net...\n");
			printf("\tper node: %zuB\n", sizeof(node));
			printf("\ttotal: %luB\n", nnodes*sizeof(node));
			net = (node *)realloc(net, nnodes*sizeof(node));
			ranet = (node *)realloc(ranet, nnodes*sizeof(node));
			dictbuf=1024+dictoff-(dictoff%1024);
			printf("Realloc mem for dict...\n");
			printf("\tper node: %zuB\n", sizeof(dictent));
			printf("\ttotal: %luB\n", dictbuf*sizeof(dictent));
			dict=(dictent *)realloc(dict, dictbuf*sizeof(dictent));
			if(!(dict && net && ranet))
			{
				printf("Allocation failure; probably out of memory\n");
				return(1);
			}
			long n;
			for(n=0;n<nnodes;n++)
			{
				fscanf(fp, "%c%c%c\n", (char *)&net[n].type, &net[n].charge, &net[n].trig);
				if(net[n].type!=GLOBAL)
					net[n].charge=-32;
				int t;
				for(t=0;t<16;t++)
				{
					fscanf(fp, "%lu,%c%c\n", &net[n].conns[t].ptr, &net[n].conns[t].bias, &net[n].conns[t].val);
				}
			}
			for(n=0;n<nnodes;n++)
			{
				fscanf(fp, "%c%c%c\n", (char *)&ranet[n].type, &ranet[n].charge, &ranet[n].trig);
				if(ranet[n].type!=GLOBAL)
					ranet[n].charge=-32;
				int t;
				for(t=0;t<16;t++)
				{
					fscanf(fp, "%lu,%c%c\n", &ranet[n].conns[t].ptr, &ranet[n].conns[t].bias, &ranet[n].conns[t].val);
				}
			}
			long d;
			for(d=0;d<dictoff;d++)
			{
				dict[d].word=(char *)malloc(320);
				fscanf(fp, "%lu,%s\n", &dict[d].ptr, dict[d].word);
				dict[d].word=(char *)realloc(dict[d].word, 1+strlen(dict[d].word));
			}
			fclose(fp);
		}
		else
		{
			bool noresp=false;
			{
				long n;
				for(n=0;n<nnodes;n++)
				{
					if(net[n].type==SWITCH)
					{
						charge[n]=-32;
					}
					if(ranet[n].type==SWITCH)
					{
						racharge[n]=-32;
					}
				}
			}
			// massage input
			if(input[0]=='\t') noresp=true;
			replace(input, '`', '\'');
			replace(input, '\t', ' ');
			replace(input, '\"', ' ');
			replace(input, '(', ' ');
			replace(input, ')', ' ');
			massage(&input, ',', false);
			massage(&input, '.', false);
			massage(&input, ';', false);
			massage(&input, ':', false);
			massage(&input, '?', false);
			massage(&input, '!', false);
			massage(&input, '-', true);
			long ops[160], rops[160];
			int nops=0, nrops=0;
			char *output=strdup("");
			char *lasts;
			char *word=strtok_r(input, " \t\n", &lasts);
			int q=0;
			signed int nrate=0;
			while(word)
			{
				long d;
				bool dfound=false;
				for(d=0;d<dictoff;d++)
				{
					if(strcasecmp(word, dict[d].word)==0)
					{
						dfound=true;
						break;
					}
				}
				if(!dfound)
				{
					charge[0]=127;
					net[0].trig=-1;
					racharge[0]=127;
					ranet[0].trig=-1;
					d=dictoff;
					dict[dictoff].word=strdup(word);
					off_t o;
					for(o=0;o<strlen(dict[dictoff].word);o++)
					{
						dict[dictoff].word[o]=toupper(dict[dictoff].word[o]);
					}
					printf("Dict-add[%lu]: %s\n", dictoff, dict[dictoff].word);
					dict[dictoff].ptr=(dictoff*5)+5;
					dictoff++;
					if(dictoff>=dictbuf)
					{
						dictbuf+=1024;
						printf("Realloc mem for larger dict...\n");
						printf("\tper dictent: %zuB\n", sizeof(dictent));
						printf("\ttotal: %luB\n", dictbuf*sizeof(dictent));
						dictent * newdict=(dictent *)realloc(dict, dictbuf*sizeof(dictent));
						if(newdict)
						{
							printf("Allocated OK\n");
							dict=newdict;
						}
						else
						{
							printf("Failed!\n");
							perror("realloc");
							return(1); // later, call into some state-saving code
						}
					}
				}
				charge[d]=127;
				net[d].trig=-1;
				racharge[d]=127;
				ranet[d].trig=-1;
				q++;
				if(racharge[1]>47)
				{
					nrate++;
					racharge[1]=0;
					rops[nrops]=(1+dirs[(ranet[1].trig+8)%16]+nnodes)%nnodes;
					nrops++;
				}
				if(racharge[2]>47)
				{
					nrate--;
					racharge[2]=0;
					rops[nrops]=(2+dirs[(ranet[2].trig+8)%16]+nnodes)%nnodes;
					nrops++;
				}
				n_iter(net, charge, nnodes);
				n_iter(net, racharge, nnodes);
				if(!pipe)
				{
					long n;
					for(n=0;n<nnodes;n++)
					{
						signed int ch=(net[n].type==GLOBAL)?net[n].charge:charge[n];
						pset(screen, n%256, (n/256), ch+128, ch+128, ch+128);
					}
					for(n=0;n<nnodes;n++)
					{
						signed int ch=(ranet[n].type==GLOBAL)?ranet[n].charge:racharge[n];
						pset(screen, 320+(n%256), (n/256), ch+128, ch+128, ch+128);
					}
					SDL_Flip(screen);
				}
				word=strtok_r(NULL, " \t\n", &lasts);
			}
			if(q>2)
			{
				for(;q<16;q++)
				{
					if(racharge[1]>47)
					{
						nrate++;
						racharge[1]=0;
						rops[nrops]=(1+dirs[(ranet[1].trig+8)%16]+nnodes)%nnodes;
						nrops++;
					}
					if(racharge[2]>47)
					{
						nrate--;
						racharge[2]=0;
						rops[nrops]=(2+dirs[(ranet[2].trig+8)%16]+nnodes)%nnodes;
						nrops++;
					}
					n_iter(net, racharge, nnodes);
					if(!pipe)
					{
						long n;
						for(n=0;n<nnodes;n++)
						{
							signed int ch=(ranet[n].type==GLOBAL)?ranet[n].charge:racharge[n];
							pset(screen, 320+(n%256), (n/256), ch+128, ch+128, ch+128);
						}
						SDL_Flip(screen);
					}
				}
				bool bt[nnodes];
				memset(bt, 0, sizeof(bt));
				signed int fq, fr;
				fq = nrate>0?(nrate*2)-1:nrate-3;
				fr = (fq>0)?abs(6-nrate):-2;
				printf("(ranet (%d) feedback %d, %d)\n", nrate, fq, fr);
				fflush(stdout);
				int hops;
				for(i=0;i<nrops;i++)
				{
					long n=rops[i];
					long o=n;
					hops=0;
					while(!(bt[n] || hops>31))
					{
						hops++;
						ranet[n].conns[o].bias=max(min((signed int)(ranet[n].conns[o].bias) + fq, 127), -128);
						if(ranet[n].conns[o].val<0)
							ranet[n].conns[o].val=max(min((signed int)(ranet[n].conns[o].val) - (fq-fr), 127), -128);
						else
							ranet[n].conns[o].val=max(min((signed int)(ranet[n].conns[o].val) + (fq+fr), 127), -128);
						o=n;
						bt[n]=true;
						n=(n+dirs[(unsigned)ranet[n].trig]+nnodes)%nnodes;
						if(ranet[n].trig==-1)
							break;
					}
				}
			}
			q=0;
			nrate=0;
			if(!noresp)
			{
				while(((charge[1]<15) || !strlen(output)) && (strlen(output)<80) && (q<128))
				{
					q++;
					long df;
					for(df=5;(df<(dictoff+1)*5) && (strlen(output)<80);df+=5)
					{
						if(charge[df]>125)
						{
							charge[df]=0;
							racharge[df]=95;
							ranet[df].trig=-1;
							long fd=(df/5)-1;
							output = (char *)realloc(output, strlen(output)+strlen(dict[fd].word)+2);
							strcat(output, " ");
							strcat(output, dict[fd].word);
							ops[nops]=df;
							nops++;
						}
					}
					if(racharge[1]>47)
					{
						nrate++;
						racharge[1]=0;
						rops[nrops]=(1+dirs[(ranet[1].trig+8)%16]+nnodes)%nnodes;
						nrops++;
					}
					if(racharge[2]>47)
					{
						nrate--;
						racharge[2]=0;
						rops[nrops]=(2+dirs[(ranet[2].trig+8)%16]+nnodes)%nnodes;
						nrops++;
					}
					n_iter(net, charge, nnodes);
					n_iter(ranet, racharge, nnodes);
					/*long rn=rand_r(&seed)*(double)nnodes/RAND_MAX; // random integer in [0, nnodes)
					net[rn].charge+=64;
					charge[rn]+=64;*/
					if(!pipe)
					{
						long n;
						for(n=0;n<nnodes;n++)
						{
							signed int ch=(net[n].type==GLOBAL)?net[n].charge:charge[n];
							pset(screen, n%256, (n/256), ch+128, ch+128, ch+128);
						}
						for(n=0;n<nnodes;n++)
						{
							signed int ch=(ranet[n].type==GLOBAL)?ranet[n].charge:racharge[n];
							pset(screen, 320+(n%256), (n/256), ch+128, ch+128, ch+128);
						}
						SDL_Flip(screen);
					}
				}
				for(;q<32;q++)
				{
					if(racharge[5]>47)
					{
						nrate++;
						racharge[5]=0;
						rops[nrops]=(5+dirs[(ranet[5].trig+8)%16]+nnodes)%nnodes;
						nrops++;
					}
					if(racharge[10]>47)
					{
						nrate--;
						racharge[10]=0;
						rops[nrops]=(10+dirs[(ranet[10].trig+8)%16]+nnodes)%nnodes;
						nrops++;
					}
					n_iter(net, racharge, nnodes);
					if(!pipe)
					{
						long n;
						for(n=0;n<nnodes;n++)
						{
							signed int ch=(ranet[n].type==GLOBAL)?ranet[n].charge:racharge[n];
							pset(screen, 320+(n%256), (n/256), ch+128, ch+128, ch+128);
						}
						SDL_Flip(screen);
					}
				}
			
				if(charge[1]>=15)
				{
					ops[nops]=1; // the stop-node needs feedback too!
					nops++;
				}
	
				printf("Virgil: %s\n", output);
				fflush(stdout);
				fprintf(stderr, "%s\n", output);
				fflush(stderr);
				if(nops)
				{
					signed int feed;
					if(pipe)
					{
						printf("Rated %d\n", nrate);
						feed=nrate; // rate yourself
					}
					else
					{
						printf("Rate? (%d)\n", nrate);
						fflush(stdout);
						scanf("%d", &feed);
					}
					bool bt[nnodes];
					memset(bt, 0, sizeof(bt));
					int i;
					int hops;
					for(i=0;i<nops;i++)
					{
						long n=ops[i];
						long o=net[n].trig;
						if(o==-1)
							continue;
						n=(n+dirs[(o+8)%16]+nnodes)%nnodes;
						hops=0;
						while(!(bt[n] || hops>255))
						{
							hops++;
							net[n].conns[o].bias=max(min((signed int)(net[n].conns[o].bias) + feed, 127), -128);
							if(net[n].conns[o].val<0)
								net[n].conns[o].val=max(min((signed int)(net[n].conns[o].val) - feed, 127), -128);
							else
								net[n].conns[o].val=max(min((signed int)(net[n].conns[o].val) + feed, 127), -128);
							o=n;
							bt[n]=true;
							if((n<dictoff) && (n%5 == 0))
								break;
							if(net[n].trig==-1)
								break;
							n=(n+dirs[(unsigned)net[n].trig]+nnodes)%nnodes;
						}
					}
					if(!pipe)
					{
						memset(bt, 0, sizeof(bt));
						signed int fq, fr;
						if(nrate==0)
							fq=3-abs(feed);
						else if(feed+nrate==0)
							fq = -5;
						else
							fq = 5-abs(((double)feed - (double)nrate) * 5.0 / ((double)feed + (double)nrate));
						if(feed*nrate<0)
							fq -= 2; // knock off 2 more points for getting the direction wrong
						fr = abs(feed)-abs(nrate);
						printf("(ranet feedback %d, %d)\n", fq, fr);
						fflush(stdout);
						for(i=0;i<nrops;i++)
						{
							long n=rops[i];
							long o=n;
							hops=0;
							while(!(bt[n] || hops>31))
							{
								hops++;
								ranet[n].conns[o].bias=max(min((signed int)(ranet[n].conns[o].bias) + fq, 127), -128);
								if(ranet[n].conns[o].val<0)
									ranet[n].conns[o].val=max(min((signed int)(ranet[n].conns[o].val) - (fq-fr), 127), -128);
								else
									ranet[n].conns[o].val=max(min((signed int)(ranet[n].conns[o].val) + (fq+fr), 127), -128);
								o=n;
								bt[n]=true;
								n=(n+dirs[(unsigned)ranet[n].trig]+nnodes)%nnodes;
								if(ranet[n].trig==-1)
									break;
							}
						}
					}
					if(!pipe) getchar();
				}
			}
			free(output);
			free(input);
		}
	}
	return(0);
}

char * getl(char * prompt, FILE *stream[2])
{
	if(prompt)
	{
		fprintf(stream[0],"%s", prompt);
		fflush(stream[0]);
	}
	// gets a line of string data, {re}alloc()ing as it goes, so you don't need to make a buffer for it, nor must thee fret thyself about overruns!
	char * lout = (char *)malloc(80);
	int i=0;
	char c;
	while(true)
	{
		c = fgetc(stream[1]);
		if((c == 10) || (c == 13))
			break;
		if(c != 0)
			lout[i++]=c;
		if((i%80) == 0)
		{
			if((lout = (char *)realloc(lout, i+80))==NULL)
			{
				free(lout);
				fprintf(stream[0], "\nNot enough memory to store input!\n");
				fflush(stream[0]);
				fprintf(stdout, "\nNot enough memory to store input!\n");
				return(NULL);
			}
		}
	}
	lout[i]=0;
	lout=(char *)realloc(lout, i+1);
	return(lout);
}

void n_iter(node * net, signed char * charge, long nnodes)
{
	long n;
	for(n=0;n<nnodes;n++)
	{
		// firing rules
		int t;
		for(t=0;t<16;t++)
		{
			long nbr=net[n].conns[t].ptr;
			signed int val=net[n].conns[t].val;
			if(val==0)
				val=rand_r(&seed)*256.0/RAND_MAX-128; // random integer in [-128, 127]
			int rn=rand_r(&seed)*256.0/RAND_MAX; // random integer in [0, 255]
			signed char chrg = (net[n].type==GLOBAL)?net[n].charge:charge[n];
			signed int bias = (signed int)net[n].conns[t].bias + (signed int)chrg;
			signed int u=rn*bias;
			signed int nbc = (net[nbr].type==GLOBAL)?net[nbr].charge:charge[nbr]; // neighbour charge
			if(u>4095)
			{
				nbc+=val;
				signed char nnc=min(max(nbc, -128), 127); // clamp to range for signed char
				if(net[nbr].type==GLOBAL)
				{
					net[nbr].charge=nnc;
				}
				else
				{
					charge[nbr]=nnc;
				}
				// (partially) discharge this node
				signed char nc=min(max(chrg-(val*0.7), -128), 127); // clamp to range for signed char
				if(net[n].type==GLOBAL)
				{
					net[n].charge=nc;
				}
				else
				{
					charge[n]=nc;
				}
				net[nbr].trig=t;
			}
		}
	}
}

SDL_Surface * gf_init(int x, int y)
{
	SDL_Surface * screen;
	if(SDL_Init(SDL_INIT_VIDEO)<0)
	{
		perror("SDL_Init");
		return(NULL);
	}
	atexit(SDL_Quit);
	if((screen = SDL_SetVideoMode(x, y, OBPP, SDL_HWSURFACE))==0)
	{
		perror("SDL_SetVideoMode");
		SDL_Quit();
		return(NULL);
	}
	if(SDL_MUSTLOCK(screen) && SDL_LockSurface(screen) < 0)
	{
		perror("SDL_LockSurface");
		return(NULL);
	}
	return(screen);
}

int pset(SDL_Surface * screen, int x, int y, char r, char g, char b)
{
	long int s_off = y*screen->pitch + x*screen->format->BytesPerPixel;
	unsigned long int pixval = SDL_MapRGB(screen->format, r, g, b),
		* pixloc = screen->pixels + s_off;
	*pixloc = pixval;
	return(0);
}

int line(SDL_Surface * screen, int x1, int y1, int x2, int y2, char r, char g, char b)
{
	if(x2<x1)
	{
		int _t=x1;
		x1=x2;
		x2=_t;
		_t=y1;
		y1=y2;
		y2=_t;
	}
	int dy=y2-y1,
		dx=x2-x1;
	if(dx==0)
	{
		int cy;
		for(cy=y1;(dy>0)?cy-y2:y2-cy<=0;cy+=(dy>0)?1:-1)
		{
			pset(screen, x1, cy, r, g, b);
		}
	}
	else if(dy==0)
	{
		int cx;
		for(cx=x1;cx<=x2;cx++)
		{
			pset(screen, cx, y1, r, g, b);
		}
	}
	else
	{
		double m = (double)dy/(double)dx;
		int cx=x1, cy=y1;
		if(m>0)
		{
			while(cx<x2)
			{
				do {
					pset(screen, cx, cy, r, g, b);
					cx++;
				} while((((cx-x1) * m)<(cy-y1)) && cx<x2);
				do {
					pset(screen, cx, cy, r, g, b);
					cy++;
				} while((((cx-x1) * m)>(cy-y1)) && cy<y2);
			}
		}
		else
		{
			while(cx<x2)
			{
				do {
					pset(screen, cx, cy, r, g, b);
					cx++;
				} while((((cx-x1) * m)>(cy-y1)) && cx<x2);
				do {
					pset(screen, cx, cy, r, g, b);
					cy--;
				} while((((cx-x1) * m)<(cy-y1)) && cy>y2);
			}
		}
	}
	return(0);
}

void massage(char **input, char punct, bool dside)
{
	//fprintf(stderr, "massage(&%s, %c)\n", *input, punct);
	char *ptr=*input;
	while((ptr=strchr(ptr+1, punct))!=NULL)
	{
		if(ptr[-1]!=' ')
		{
			ptrdiff_t p=ptr-*input;
			*input = (char *)realloc(*input, 3+strlen(*input));
			ptr=*input+p;
			char *tmp = strdup(ptr);
			ptr[0]=' ';
			if(dside)
			{
				ptr[1]=tmp[0];
				ptr[2]=' ';
				strcpy(ptr+3, tmp+1);
			}
			else
			{
				strcpy(ptr+1, tmp);
			}
			free(tmp);
		}
	}
	//fprintf(stderr, "returned %s\n", *input);
}

void replace(char *input, char old, char new)
{
	char *ptr=input;
	while((ptr=strchr(input, old))) *ptr=new;
}
