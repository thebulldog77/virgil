# Virgil makefile

CC ?= gcc
CFLAGS ?= -Wall

all: virgil bot

virgil: virgil.c
	$(CC) `sdl-config --cflags` $(CFLAGS) -o virgil virgil.c -DOSIZ_X=640 -DOSIZ_Y=256 -DOBPP=24 `sdl-config --libs`

bot: bot.c
	$(CC) $(CFLAGS) -o bot bot.c
