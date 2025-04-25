all: cliente servidor

cliente: cliente.c powerudp.c
	gcc -o cliente cliente.c powerudp.c -Wall

servidor: servidor.c
	gcc -o servidor servidor.c -Wall

clean:
	rm -f cliente servidor