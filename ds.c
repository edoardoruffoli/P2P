#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#define COMMAND_LENGTH 1024
#define BUFFER_SIZE 1024
#define MAX_PEERS 5
#define MAX_NEIGHBORS 2
#define ACK_LEN 4

char command[COMMAND_LENGTH]; 

struct sockaddr_in my_addr;
int server_port;
pid_t pid;

// Ascolta le richieste di connessione di peer
int listening_socket;       
int fdmax;
fd_set master;
fd_set read_fds;

// Informazioni sui peer connessi
struct sockaddr_in cl_addr[MAX_PEERS];

struct peer{
    int port;
    // Coppia di neighbor connessi
    int neighbor[MAX_NEIGHBORS];
    int n_neighbors;
}peers[MAX_PEERS];

int n_peers;

void prompt()
{
	fprintf(stdout, "\n> ");
    fflush(stdout);
}

void fdt_init() 
{       
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_SET(0, &master);
	
	fdmax = 0;
}

void create_listening_socket(char* port)
{    
    // Creazione socket
    if((listening_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1){
        perror("socket() error");
        exit(-1);
    }

    // Creazione indirizzo di bind
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(atoi(port));

    // Aggancio del socket
    if(bind(listening_socket, (struct sockaddr*)&my_addr, sizeof(my_addr)) == -1){
        perror("Error: ");
        exit(-1);
    }
}

int get_peer_id(int port)
{
    int i;

    for(i=0; i<MAX_PEERS; i++){
        if(peers[i].port == port)
            return i;
    }
    return -1;
}

void replace_neighbor(int id, int new_peer_port, int common_port)   // common
{
    // Il peer non aveva neighbor
    if(peers[id].neighbor[0] == 0){      
        peers[id].neighbor[0] = new_peer_port;
        peers[id].n_neighbors++;
    }
    else if(peers[id].neighbor[1] == 0){ 
        peers[id].neighbor[1] = new_peer_port;
        peers[id].n_neighbors++;
    }
    // Elimino il neighbor in comune con il nuovo peer
    else if(peers[id].neighbor[0] == common_port){
        peers[id].neighbor[0] = new_peer_port;
    }
    else
        peers[id].neighbor[1] = new_peer_port;
    
   // printf("I nuovi neighbors di %d sono %d e %d\n", peers[id].port, peers[id].neighbor[0], peers[id].neighbor[1]);
}  

void find_neighbors(int id_peer)        // id_peer è la posizione nell'array di peer
{
    int new_peer_port = ntohs(cl_addr[id_peer].sin_port);
    int i, first_n, dist;      // lavoro

    // Non ci sono vicini
    if(n_peers < 2)
        return;

    // Il primo neighbor è il + vicino al nuovo peer
    for(i=0; i<n_peers; i++){

    // Salto il peer di cui stiamo cercando i neighbor
        if(i==id_peer)      
           continue;

    // Inizializzazione
        if((i==0 && id_peer != 0) || (i==1 && id_peer == 0)){ 
            peers[id_peer].neighbor[0] = ntohs(cl_addr[i].sin_port);
            dist = abs(new_peer_port - ntohs(cl_addr[i].sin_port));            
            first_n = i;
        }

        // Possibile neighbor trovato
        else if(dist > abs(new_peer_port - ntohs(cl_addr[i].sin_port))){

            // Aggiorno il primo neighbor
            peers[id_peer].neighbor[0] = ntohs(cl_addr[i].sin_port);

            // Aggiorno variabile di lavoro
            dist = abs(new_peer_port - ntohs(cl_addr[i].sin_port));
            first_n = i;
        }
    }   

    peers[id_peer].n_neighbors++;

    if(n_peers != 2){

        // Il secondo neighbor è il più lontano tra i neighbor del primo neighbor trovato
        if(peers[first_n].neighbor[1] == 0 || (abs(peers[first_n].port - peers[first_n].neighbor[0]) > 
            abs(peers[first_n].port - peers[first_n].neighbor[1]))){
            peers[id_peer].neighbor[1] = peers[first_n].neighbor[0];
        }
        else 
            peers[id_peer].neighbor[1] = peers[first_n].neighbor[1];

        peers[id_peer].n_neighbors++;
    }

    // Aggiorno i neighbor dei nuovi neighbor di id_peer
    // elimino il neighbor in comune (ALGORITMO)
    // per far si che i nodi formino un ciclo hamiltoniano
    replace_neighbor(first_n, new_peer_port, peers[id_peer].neighbor[1]); 
    if(n_peers != 2) 
        replace_neighbor(get_peer_id(peers[id_peer].neighbor[1]), new_peer_port, peers[id_peer].neighbor[0]);      
}

int check_peer(struct sockaddr_in peer_addr)    // ritorna 1 se il peer è presente
{
    int i, found = 0;

    // Scorro l'array dei peer connessi e controllo
    for(i=0; i<n_peers; i++){
        if(cl_addr[i].sin_port == peer_addr.sin_port && cl_addr[i].sin_addr.s_addr == peer_addr.sin_addr.s_addr){
            found = 1;
            break;
        }
    }
    return found;
}

void handle_request()       // Gestisce le richieste dei peer
{
    int ret;
    struct sockaddr_in connecting_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addrlen = sizeof(connecting_addr);
    int quitting_peer_id;
    int new_peer_id;

    // Codice messaggio ricevuto
    uint16_t opcode;
          
    // Attendo richieste
    ret = recvfrom(listening_socket, buffer, BUFFER_SIZE, 0, 
                (struct sockaddr*)&connecting_addr, &addrlen);
    if(ret < 0)
        return;

    // Recupero opcode
	memcpy(&opcode, (uint16_t *) & buffer, 2);
    opcode = ntohs(opcode);
        
    switch(opcode){

        // BOOT message
        case 1:         
            // Controllo se il peer era già presente (UDP messaggi ripetuti)
            if(check_peer(connecting_addr))
                break;

            // Aggiorno le info sui peers
            // Ritorna la prima posizione libera nell'array di peer
            if((new_peer_id = get_peer_id(0)) == -1){
                printf("Network FULL\n");
                return;
            }

            cl_addr[new_peer_id] = connecting_addr;
            peers[new_peer_id].port = ntohs(connecting_addr.sin_port);
            n_peers++;
            
            // Trovo i neighbors del nuovo peer e aggiorno le struct
            find_neighbors(new_peer_id);

            //printf("Neighbors trovati %d %d \n", peers[n_peers-1].neighbor[0], peers[n_peers-1].neighbor[1]);
            break;

        // STOP message
        case 2:
            quitting_peer_id = get_peer_id(ntohs(connecting_addr.sin_port));
            
            // Peer già disconesso (UDP messaggi ripetuti)
            if(quitting_peer_id == -1){
                printf("Peer già disconnesso\n");
                break;
            }
            printf("Richiesta di disconnessione dal peer %d\n", peers[quitting_peer_id].port);
            prompt();

            // Aggiorno le struct dei neighbor
            if(peers[quitting_peer_id].n_neighbors == 2){
                
                // Trovo i neighbor del peer che si sta disconnettendo
                int neigh1_id = get_peer_id(peers[quitting_peer_id].neighbor[0]);
                int neigh2_id = get_peer_id(peers[quitting_peer_id].neighbor[1]);

                // Li connetto tra di loro
                // Aggiorno neigh_1
                if(peers[neigh1_id].neighbor[0] == peers[quitting_peer_id].port){
  
                    if(peers[neigh1_id].neighbor[1] != peers[quitting_peer_id].neighbor[1])
                        peers[neigh1_id].neighbor[0] = peers[quitting_peer_id].neighbor[1];

                    // Caso limite: si disconnette un peer e ne rimangono 2
                    else{
                        peers[neigh1_id].neighbor[0] = 0;
                        peers[neigh1_id].n_neighbors--;
                    }
                }
                else{
                    if(peers[neigh1_id].neighbor[0] != peers[quitting_peer_id].neighbor[1])
                        peers[neigh1_id].neighbor[1] = peers[quitting_peer_id].neighbor[1];
                    
                    // Caso limite: si disconnette un peer e ne rimangono 2
                    else{ 
                        peers[neigh1_id].neighbor[1] = 0;
                        peers[neigh1_id].n_neighbors--;
                    }
                }
                // Aggiorno neigh2
                if(peers[neigh2_id].neighbor[0] == peers[quitting_peer_id].port){

                    if(peers[neigh2_id].neighbor[1] != peers[quitting_peer_id].neighbor[0])
                        peers[neigh2_id].neighbor[0] = peers[quitting_peer_id].neighbor[0];
                    
                    // Caso limite: si disconnette un peer e ne rimangono 2
                    else{
                        peers[neigh2_id].neighbor[0] = 0;
                        peers[neigh2_id].n_neighbors--;
                    }
                }
                else{
                    if(peers[neigh2_id].neighbor[0] != peers[quitting_peer_id].neighbor[0])
                        peers[neigh2_id].neighbor[1] = peers[quitting_peer_id].neighbor[0];
                    
                    // Caso limite: si disconnette un peer e ne rimangono 2
                    else{
                        peers[neigh2_id].neighbor[1] = 0;
                        peers[neigh2_id].n_neighbors--;
                    }
                }
            }
            if(peers[quitting_peer_id].n_neighbors == 1){
                // Trovo i neighbor del peer che si sta disconnettendo
                int neigh1_id = get_peer_id(peers[quitting_peer_id].neighbor[0]);

                // Aggiorno neigh_1
                if(peers[neigh1_id].neighbor[0] == peers[quitting_peer_id].port)
                    peers[neigh1_id].neighbor[0] = 0;
                else 
                    peers[neigh1_id].neighbor[1] = 0;

                peers[neigh1_id].n_neighbors--;
            }

            // Rimuovo il peer
            peers[quitting_peer_id].port = 0;
            n_peers--;
            break;
            
        default:
            printf("Opcode non riconosciuto\n");
            return;
    }

    // Faccio gestire le richieste a un processo figlio
    pid = fork();       

    if(pid == -1){         
        fprintf(stderr, "Errore fork()\n");
        exit(-1);
    }
    
    // Processo figlio:
    else if(pid == 0){  

        //Gestione richiesta
        switch(opcode){

            // BOOT message   
            case 1:                  
                sprintf(buffer,"%d %d", peers[new_peer_id].neighbor[0], peers[new_peer_id].neighbor[1]);

                ret = sendto(listening_socket, buffer, strlen(buffer), 0,
                        (struct sockaddr*)&cl_addr[new_peer_id], sizeof(cl_addr[new_peer_id]));
                break;

            // STOP message 
            case 2:         
                sprintf(buffer,"ACK");

                ret = sendto(listening_socket, buffer, strlen(buffer), 0,
                        (struct sockaddr*)&cl_addr[quitting_peer_id], sizeof(cl_addr[quitting_peer_id]));
                break;

            default:
                return;
        }
        exit(0);
    }   
}

void help_command()
{
	printf(	"Digita un comando:\n\n"
			"1) help --> mostra i dettagli dei comandi\n"
			"2) status -->  mostra un elenco di peer connessi\n"
			"3) showneighbor <peer>  --> mostra i neighbor di un peer\n"
			"4) esc  --> chiude il DSDettaglio comandi\n");
}

void status_command()
{
    int i;
    int peers_trovati = 0;
    char ip[17];

    if(n_peers == 0)
        printf("Nessun peer connesso\n");

    for(i=0; i<MAX_PEERS && peers_trovati < n_peers; i++){

        if(peers[i].port == 0)
            continue;

        // Peer trovato
        peers_trovati++;

        // Recupero e formato l'indirizzo del peer 
        inet_ntop(cl_addr[i].sin_family, (void*)&cl_addr[i].sin_addr, 
                    ip, sizeof(ip));

        // Stampo la riga relativa al peer
        printf("Peer %d: IP %s - Porta %d\n", peers_trovati, ip, ntohs(cl_addr[i].sin_port));
    }
}

void showneighbor_command()
{
    int i, port_requested = 0;  
    int peers_trovati = 0;   
    char arg1[256];   
    
    // Parametro opzionale     
    fgets(arg1, sizeof(arg1), stdin);
    sscanf(arg1, "%d", &port_requested);

    for(i=0; i<MAX_PEERS && peers_trovati < n_peers; i++){
        if(peers[i].port == 0)
            continue;

        // Ho trovato un peer 
        peers_trovati++;

        // Ho trovato il peer che stavo cercando
        if(peers[i].port == port_requested || port_requested == 0)
            printf("Peer %d - Neighbors: %d %d\n", peers[i].port, peers[i].neighbor[0], peers[i].neighbor[1]);
    }
}

void esc_command()
{
    int i, ret;
    int peers_trovati = 0;
    char buf[4];
    socklen_t addrlen;

    // Invio una messaggio di ESC a tutti i peer connessi
    for(i=0; i<MAX_PEERS && peers_trovati < n_peers; i++){
        if(peers[i].port == 0)
            continue;

        // Ho trovato un peer 
        peers_trovati++;

        sprintf(buf, "ESC");
        printf("Invio ESC a %d\n", peers[i].port);

        do{
           ret = sendto(listening_socket, buf, strlen(buf), 0,
                (struct sockaddr*)&cl_addr[i], sizeof(cl_addr[i]));
            if(ret < 0)
                continue;
            
            // ACK
            ret = recvfrom(listening_socket, buf, ACK_LEN, 0,
                (struct sockaddr*)&cl_addr[i], &addrlen);            
        }
        while(ret < 0);
    }

    printf("Tutti i peer sono stati disconnessi \n");

    exit(0);
}

void read_command() 
{
    // Legge il comando da stdin facendo lo scanning di 1024 caratteri
	scanf("%1024s", command);

	if (!strncmp(command, "help", 4))		// HELP
		help_command();
	else if (!strncmp(command, "status", 6))	// STATUS
		status_command();
	else if (!strncmp(command, "showneighbor", 12))	// SHOWNEIGHBOR
		showneighbor_command();
    else if (!strncmp(command, "esc", 3))	// ESC
		esc_command();
	else						
		fprintf(stderr, "Comando non valido"); 
    
    prompt();
}

/**
 * Entry point.
 */

int main(int argc, char** argv)
{
    if(argc < 2){
		fprintf(stderr, "Uso: ./peer <porta>\n"); 
		exit(1);
    }

    // Creo socket per ricevere messaggi UDP dai peer
	create_listening_socket(argv[1]);
	
    // Controllo della tastiera e del listening socket
	fdt_init();

	FD_SET(listening_socket, &master);
	fdmax = listening_socket;

    // Messaggio iniziale
    printf("**********************DS COVID STARTED**********************\n");
	help_command();
    prompt();

    while(1){
        int i;
        read_fds = master;

        if(select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select() error");
			exit(-1);
		}

        for(i=0; i<=fdmax; i++){
    
            if(FD_ISSET(i, &read_fds)){

                // Tastiera
                if(i == 0)                  
                    read_command();

                // Nuove richieste dai peer
                if(i == listening_socket)   
                    handle_request();
            }	
        }
    }
}