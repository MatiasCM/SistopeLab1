FLAGS = -Wall -lm

desafio1: desafio1.c
	gcc $(FLAGS) -o desafio1 desafio1.c

clear:
	rm -f desafio1 *.o