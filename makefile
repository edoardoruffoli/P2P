all: peer ds clean

peer: peer.o
	gcc -Wall peer.o -o peer

server: ds.o
	gcc -Wall ds.o -o ds

clean:
	rm peer.o ds.o