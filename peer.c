// Function strptime
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <sys/timerfd.h>
 
#define COMMAND_LENGTH 1024
#define BUFFER_SIZE 4096
#define BOOT_LENGTH 4
#define RES_BOOT_LENGTH 4  
#define TIMEOUT_INTERVAL 5
#define RES_FLOOD_TIMEOUT 3
#define MAX_NEIGHBORS 2
#define MAX_REQUESTERS 5

// General utility
int my_port;       
struct sockaddr_in my_addr;
int connected;      // 1 connesso al network, 0 non connesso
int register_closed; // 1 se il registro giornaliero è chiuso 0 se è aperto

// Prima data selezionabile
char *start_date = "1:12:2020";

// Utility comando get
char missing_regs[1024];    // stringa che contiene i registri mancanti per la richiesta get
int received_REPLY_DATA_NULL;   // memorizza il numero di reply data NULL ricevute dai neighbor
int waiting_for_RES_FLOOD = 0;  // evita che la tastiera accetti altri comandi durante l'attesa dei res_flood

char regs_to_send[MAX_REQUESTERS][4096]; // memorizza i regs da inviare ai requesters
int n_requesters;       // numero di requesters per le mie entry

// Memorizzo i parametri del comando get che sto gestendo
char last_get_aggr[11];
char last_get_type[2];
char last_get_period[25];

// Timer per ricevere i RES_FLOOD
int timerfd;

// Utility Neighbors
int n_neighbors = 0;

struct neighbors{
    int port;         // numero porta neighbor      
    int sd;         // descrittore socket TCP
}n_list[MAX_NEIGHBORS];

struct sockaddr_in neig_addr[MAX_NEIGHBORS];

// Socket che ascolta le richieste di connessione dai peer     
int listener; 

// Utility Discovery Server
char DS_addr[256];
int DS_port;
int sv_udp_socket;     // Socket invio boot message UDP
struct sockaddr_in sv_addr;

// File descriptor table utility
int fdmax;
fd_set master;
fd_set read_fds;
fd_set write_fds;

void prompt()
{
	printf("\n> ");
    fflush(stdout);
}    

void fdt_init() {       
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
	FD_SET(0, &master);
	
	fdmax = 0;
}

void create_tcp_notifying_socket(int i)        // Creo un socket TCP con il neighbor i
{
    int yes = 1;
    printf("Invio richiesta di connessione al neighbor %d\n", n_list[i].port);

    // Creazione del socket TCP
    if((n_list[i].sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket() error");
        printf("Chiusura del programma...\n"); 
        exit(-1);
    }
    //printf("TCP socket: %d.\n", tcp_socket);

    // Creazione indirizzo del neighbor
    memset(&neig_addr[i], 0, sizeof(neig_addr[i])); // Pulizia
    neig_addr[i].sin_family = AF_INET;						
    neig_addr[i].sin_port = htons(n_list[i].port);		
    inet_pton(AF_INET, "127.0.0.1", &neig_addr[i].sin_addr);

    // Creazione indirizzo di bind
    memset(&my_addr, 0, sizeof(my_addr)); // Pulizia
    my_addr.sin_family = AF_INET;						
    my_addr.sin_port = htons(my_port);								
    my_addr.sin_addr.s_addr = INADDR_ANY;

    // Voglio usare la stessa porta sia per il listener delle richieste sia per le connessioni private con i neighbor
    if(setsockopt(n_list[i].sd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) == -1) {
		perror("setsockopt() error");
		exit(-1);
	}

    if(bind(n_list[i].sd, (struct sockaddr*)&my_addr, sizeof(my_addr)) == -1) {
        perror("bind() error");
        exit(-1);
    }

    // Connessione al neighbor
    if(connect(n_list[i].sd, (struct sockaddr*)&neig_addr[i], sizeof(neig_addr[i])) == -1) {
        perror("connect() error");
        exit(-1);
    }

    printf("I miei neighbor ora sono %d %d\n", n_list[0].port, n_list[1].port);
}

void create_tcp_listening_socket(int *sd)        
{
    printf("Creo socket di ascolto richieste dai peer\n");

    int yes = 1;

    // Creazione del socket TCP
    if((*sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket() error");
        printf("Chiusura del programma...\n"); 
        exit(-1);
    }
    //printf("TCP socket: %d.\n", tcp_socket);

    // Creazione indirizzo di bind
    memset(&my_addr, 0, sizeof(my_addr)); // Pulizia
    my_addr.sin_family = AF_INET;						
    my_addr.sin_port = htons(my_port);								
    my_addr.sin_addr.s_addr = INADDR_ANY;
    
    
    if(setsockopt(*sd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) == -1) {
		perror("setsockopt() error");
		exit(-1);
	}

    // Connessione al server
    if(bind(*sd, (struct sockaddr*)&my_addr, sizeof(my_addr)) == -1) {
        perror("bind() error");
        exit(-1);
    }

    // Mi metto in ascolto
    if(listen(*sd,10) == -1){
        perror("Errore listen()");
        exit(-1);
    }
}

void create_sv_udp_socket() {
	
	// Creazione del socket UDP
	if((sv_udp_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket() error");
		printf("Chiusura del programma...\n");
		exit(-1);
	}

	//printf("UDP socket: %d.\n", udp_socket); 

    // Creazione indirizzo di bind
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(my_port);
	my_addr.sin_addr.s_addr = INADDR_ANY;

	if(bind(sv_udp_socket, (struct sockaddr*)&my_addr, sizeof(my_addr)) == -1) {
		perror("bind() error");
		exit(1);
	}

	if(sv_udp_socket > fdmax)
		fdmax = sv_udp_socket;
	FD_SET(sv_udp_socket, &master);

	// Creazione indirizzo server
	memset(&sv_addr, 0, sizeof(sv_addr));
	sv_addr.sin_family = AF_INET;
	sv_addr.sin_port = htons(DS_port);
	if(inet_pton(AF_INET, DS_addr, &sv_addr.sin_addr) == 0) {
		perror("inet_pton() error");
		exit(1);
	}
}

int find_old_neighbor(int victim_port)    // ritorna la posizione dell'array di neigh a cui associare il nuovo neigh
{
    if(n_list[0].port == 0 || (n_list[0].port == victim_port && n_neighbors == 2))
        return 0;
    else 
        return 1;
}

void next_day_date(char* date)
{
    int year, month, day;
    char next_date[11];

    // Recupero il giorno il mese e l'anno
    sscanf(date, "%d:%d:%d", &day, &month, &year);

    day++;  // tomorrow

    int days_per_month = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        days_per_month = 30;
    } else if (month == 2) {
        days_per_month = 28;
        if (year % 4 == 0) {
            days_per_month = 29;
            if (year % 100 == 0) {
                days_per_month = 28;
                if (year % 400 == 0) {
                days_per_month = 29;
                }
            }
        }
    }
    if (day > days_per_month) {
        day = 1;
        month++;
        if (month > 12) {
            month = 1;
            year++;
        }
    }

    sprintf(next_date, "%d:%d:%d", day, month, year);
    strcpy(date, next_date);
}

int search_aggr_data(char *dato_richiesto, char* dato_aggr) // Cerca nell'archivio il dato richiesto e lo scrive in dato_aggr
{
    FILE* ptr;
    char dato[1024];
    char work_string[256];
    char file_dati_aggr_path[256];
    char *temp;

    sprintf(file_dati_aggr_path, "./data/dati_aggregati_%d.txt", my_port);
    if((ptr = fopen(file_dati_aggr_path, "r")) == NULL){
        perror("Errore fopen()");
        exit(-1);
    }
    
    while (fscanf(ptr,"%s", work_string) == 1){

        if(strstr(work_string, dato_richiesto) != NULL) {
            
            // Recupero dato
            strcpy(dato, work_string);
            strtok(dato, "=");
            temp = strtok(NULL, "=");
            strcpy(dato_aggr, temp);
            fclose(ptr);
            return 1; 
        }
    }
    fclose(ptr);
    return 0;
}

void print_aggr(char *aggr, char *period, char *dato_trovato)   // Stampa dato aggregato
{
    if(!strcmp(aggr, "variazione")){
        char from[11], to[11], *entry_val, temp[64], dato_trovato_copy[1024];
        strcpy(temp, period);
        strcpy(dato_trovato_copy, dato_trovato);
        strcpy(from, strtok(temp, "-"));
        strcpy(to, strtok(NULL, "-"));
        entry_val = strtok(dato_trovato_copy, ",");

        while(strcmp(from, to)){
            next_day_date(from);
            printf("Variazione %s: %s\n", from, entry_val); 
            entry_val = strtok(NULL, ","); 
        }
    }
    else
        printf("Totale: %s\n", dato_trovato);
}

void calcola_dato_aggregato(char *aggr, char *type, char *period)
{
    FILE *ptr;
    char from[11], to[11], *entry_type;
    char reg_path[64], archivio_path[64], temp[64], s_entry_value[64], period_copy[64];
    int totale = 0, totale_oggi, totale_ieri, first_it = 1, val;

    // Contiene il dato aggregato da aggiungere all'archivio
    char s_data[1024]="";

    // Recupero le date limite del periodo
    strcpy(period_copy, period);
    strcpy(from, strtok(period_copy, "-"));
    strcpy(to, strtok(NULL, "-"));

    // Includo "to" nell'intervallo temporale considerato
    next_day_date(to);

    while(strcmp(from, to)){
        totale_oggi = 0;

        // Apro il register
        sprintf(reg_path, "./data/registers_%d/%s$.txt", my_port, from);

        if((ptr = fopen(reg_path, "r")) == NULL){
            perror("Errore fopen()");
            exit(-1);
        }
            
        // Scorro le entry
        while(fscanf(ptr, "%s", temp) == 1){

            // Recupero il tipo dell'entry
            strtok(temp, ",");
            entry_type = strtok(NULL, ",");

            // L'entry si riferisce all'evento richiesto, aggiorno totale
            if(!strcmp(entry_type, type)){
                val = atoi(strtok(NULL, ","));
                totale_oggi += val;
                totale += val;
            }
        }

        // La prima iterazione non stampo 
        if(first_it && !strcmp(aggr, "variazione")){
            first_it = 0;
        }
        else if(!strcmp(aggr, "variazione")){
            printf("Variazione %s: %d\n", from, totale_oggi - totale_ieri);
            sprintf(s_entry_value, "%d", totale_oggi - totale_ieri);
            strcat(s_data, s_entry_value);
            strcat(s_data, ",");
        }

        totale_ieri = totale_oggi;

        // Scorro al register successivo
        next_day_date(from);

        fclose(ptr);
    }

    if(!strcmp(aggr, "totale")){
        printf("Totale: %d\n", totale);
        sprintf(s_data, "%d", totale);
    }
    // Tolgo l'ultima virgola
    else{
        s_data[strlen(s_data)-1] = '\0';
    }
   
    // Aggiungo dato aggregato all'archivio
    sprintf(archivio_path, "./data/dati_aggregati_%d.txt", my_port);

    if((ptr = fopen(archivio_path, "a")) == NULL){
        perror("Errore fopen()");
        exit(-1);
    }

    printf("Aggiungo il dato aggregato all'archivio\n");
    if(fprintf(ptr, "%s%s%s=%s\n", aggr, type, period, s_data) == -1){
        perror("Errore fprintf()");
        exit(-1);
    }
 
    fclose(ptr);
}

void send_REPLY_DATA(int ret, char *dato_aggr, int sd)
{
    char buffer[BUFFER_SIZE];

    if(ret == 0)
        sprintf(buffer, "REPLY_DATA-NULL");
    else
        sprintf(buffer, "REPLY_DATA-%s", dato_aggr);

    if(send(sd, buffer, strlen(buffer), 0) == -1){
        perror("Errore send()");
        exit(-1);
    }
}

void send_FLOOD_FOR_ENTRIES(int sd, int requester_port, char *regs)
{
    char buffer[BUFFER_SIZE];

    // Preparo richiesta FLOOD con dimensione registri
    sprintf(buffer, "FLOOD_FOR_ENTRIES-%d-%s", requester_port, regs);

    // printf("Invio %s\n", buffer);
    if(send(sd, buffer, strlen(buffer), 0) == -1){
        perror("Errore send()");
        exit(-1);
    }
}

int already_to_serve(char* request) // Controlla se il FLOOD_FOR_ENTRIES è già stato ricevuto
{
    int i;

    for(i=0; i<n_requesters-1; i++){    //regs_to_send[n_requesters-1] è la richiesta corrente

        // Confronto la porta del registro da servire
        if(!strncmp(regs_to_send[i], request, 5)){
            return 1;
        }
    }
    return 0;
}

char *strremove(char *str, const char *sub) // Rimuove dalla stringa str la sottostringa sub
 {
    size_t len = strlen(sub);
    if (len > 0) {
        char *p = str;
        while ((p = strstr(p, sub)) != NULL) {
            memmove(p, p + len, strlen(p + len) + 1);
        }
    }
    return str;
}

void add_entries(char *entries) 
{
    char *token, *save_ptr, *save_ptr1;
    char data_reg[12], last_data_reg[12];
    char reg_path[64], old_reg_path[64];
    char entry[64];
    FILE *ptr = NULL;

    // strtok_r() serve per eseguire più strtok in simultanea
    token = strtok_r(entries, "-", &save_ptr);

    while(token != NULL){
        
        // Recupero la data dell'entry
        strcpy(data_reg, token);
        strtok_r(data_reg, ",", &save_ptr1);

        sprintf(reg_path, "./data/registers_%d/%s$.txt", my_port, data_reg);
        sprintf(old_reg_path, "./data/registers_%d/%s.txt", my_port, data_reg); 

        // Se l'entry è marchiata
        if(token[strlen(token)-1] == '$'){
            
            // Se è la prima del registro
            if(strcmp(data_reg, last_data_reg)){

                // Il registro marchiato è già presente, se l'ha mandato un peer in precedenza
                if((ptr = fopen(reg_path, "r")) != NULL){

                    // Scorro alla prossima entry, non aggiorno old_data_reg, così ri eseguo questo controllo anche sull'entry successiva
                    fclose(ptr);
                    token = strtok_r(NULL, "-", &save_ptr);
                    continue;
                }

                printf("Elimino vecchio reg\n");

                // Creo/sovrascrivo il vecchio registro non marchiato
                if((ptr = fopen(old_reg_path, "w")) == NULL){
                        perror("Errore fopen()");
                        exit(-1);
                } 

                // Aggiungo il marchio al registro
                rename(old_reg_path, reg_path);
            }  
            else {
                // Apro/creo il register corrispondente
                if((ptr = fopen(reg_path, "a")) == NULL){
                        perror("Errore fopen()");
                        exit(-1);
                }
            }
        }

        // L'entry non è marchiata
        else{
            // Se è già presente il registro marchiato non inserisco la entry
            if((ptr = fopen(reg_path, "r")) != NULL){

                // Scorro alla prossima entry
                fclose(ptr);
                token = strtok_r(NULL, "-", &save_ptr);
                strcpy(last_data_reg, data_reg);
                continue;
            }

            // Apro/creo il register corrispondente
            if((ptr = fopen(old_reg_path, "a")) == NULL){
                    perror("Errore fopen()");
                    exit(-1);
            }
        }

        // Preparo la entry
        strcpy(entry, token);

        // Rimuovo il marchio dalla entry
        if(entry[strlen(entry)-1] == '$')
            entry[strlen(entry)-1] = '\0';

        // Aggiungo la entry al register
        printf("Aggiungo entry %s \n", entry);
        if(fprintf(ptr, "%s\n", entry)  < 0){
            perror("Errore fprintf()");
            exit(-1);
        }

        fclose(ptr);

        // Scorro alla prossima entry
        token = strtok_r(NULL, "-", &save_ptr);

        // Aggiorno last_data_reg
        strcpy(last_data_reg, data_reg);
    }
}

void send_READY_TO_RECEIVE(int donor_port)  
{
    int donor_sd;
    char *buffer = NULL;
    char temp[64];
    char size[16];
    unsigned long entries_size = 0;
    struct sockaddr_in dest_addr;

    int yes = 1;
    printf("Invio richiesta di connessione al donor %d\n", donor_port);

    // Creazione del socket TCP
    if((donor_sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket() error");
        printf("Chiusura del programma...\n"); 
        exit(-1);
    }

    // Creazione indirizzo del neighbor
    memset(&dest_addr, 0, sizeof(dest_addr)); // Pulizia
    dest_addr.sin_family = AF_INET;						
    dest_addr.sin_port = htons(donor_port);		
    inet_pton(AF_INET, "127.0.0.1", &dest_addr.sin_addr);  

    // Voglio usare la stessa porta sia per il listener delle richieste sia per le connessioni private con i neighbor
    if(setsockopt(donor_sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt() error");
        exit(-1);
    }

    // Connessione al peer
    if(connect(donor_sd, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) == -1) {
        perror("connect() error");
        exit(-1);
    }

    printf("Connesso al peer donor %d\n", donor_port);

    // Invio avviso che sono pronto a ricevere, specificando la mia porta
    sprintf(temp, "READY_TO_RECEIVE-%d", my_port);
    if(send(donor_sd, temp, strlen(temp), 0) == -1){
        perror("Errore send()");
        exit(-1);
    } 
    // printf("Inviato %s \n", temp);

    // Ricevo la dimensione del buffer
    if(recv(donor_sd, size, sizeof(size), 0) == -1){
        perror("Errore recv()");
        exit(-1);
    }

    // printf("Ricevuto %s \n", size);
    entries_size = atoi(size) + 1;

    // Alloco un buffer che possa contenere tutte le entry
    buffer = malloc(entries_size);
    memset(buffer, 0, entries_size);

    // Avviso il donor di inviare le entry
    if(send(donor_sd, "ACK", strlen("ACK"), 0) == -1){
        perror("Errore send()");
        exit(-1);
    }

    // Attendo le entry
    if(recv(donor_sd, buffer, entries_size - 1, 0) == -1){
        perror("Errore recv()");
        exit(-1);
    }

    // Aggiungo le entry ai miei registri
    add_entries(buffer);

    printf("Entry aggiunte\n");

    // Chiudo la connessione
    close(donor_sd);
}

void add_mark_to_regs(char *period)
{
    char from[11], to[11];
    char temp[64], old_reg_path[64], new_reg_path[64];
    FILE *ptr;

    strcpy(temp, period);
    strcpy(from, strtok(temp, "-"));
    strcpy(to, strtok(NULL, "-"));

    // Devo aggiungere il marchio anche al registro di data "to"
    next_day_date(to);

    while(strcmp(from, to)){
        sprintf(old_reg_path, "./data/registers_%d/%s.txt", my_port, from);
        sprintf(new_reg_path, "./data/registers_%d/%s$.txt", my_port, from);

        // Aggiungo il marchio al registro, se non c'era lo creo
        if(rename(old_reg_path, new_reg_path) == -1){
            if((ptr = fopen(new_reg_path, "a")) == NULL){
                perror("Errore fopen()");
                exit(-1);
            }
            else
                fclose(ptr);
        }
        
        // Scorro al registro successivo
        next_day_date(from);   
    }
}

long get_dir_size(char *dirname)
{
    DIR *dir = opendir(dirname);
    if (dir == 0)
        return 0;

    struct dirent *dit;
    struct stat st;
    long size = 0;
    long total_size = 0;
    char filePath[256];

    while ((dit = readdir(dir)) != NULL)
    {
        if ( (strcmp(dit->d_name, ".") == 0) || (strcmp(dit->d_name, "..") == 0) )
            continue;

        sprintf(filePath, "%s/%s", dirname, dit->d_name);
        if (lstat(filePath, &st) != 0)
            continue;
        size = st.st_size;

        // File trovato
        if (!S_ISDIR(st.st_mode)){
            total_size += size;

            // Aggiungo qualche byte in più perchè per ogni entry potrei aggiungere i $ 
            total_size += (size/10);
        }
    }
    return total_size;
}

void send_requested_regs(int requester_port, int sd)
{
    // Recupero i registri richiesti dal requester
    int i;
    char r_port[6];
    char *token;
    FILE *ptr;
    char reg_path[64];
    char dir_path[64];
    char *entries;
    int dir_size;
    char size[16];
    char temp[64];
    pid_t pid;

    sprintf(r_port, "%d", requester_port);

    // Scorro l'array dei registri da spedire ai requesters fino a che non trovo quello della porta in questione
    for(i=0; i<MAX_REQUESTERS; i++){
        if(!strncmp(r_port, regs_to_send[i], strlen(r_port))){
            printf("Registri da spedire: %s\n", regs_to_send[i]);
            break;
        }
    }

    // Ho servito un requester
    n_requesters--;

    sprintf(dir_path, "./data/registers_%d", my_port);
    dir_size = get_dir_size(dir_path);

    pid = fork();
    
    if(pid == -1){
        perror("Errore fork()");
        exit(-1);
    }

    // Faccio gestire la richiesta al figlio
    if(pid == 0){

        // Alloco un buffer che contenga sicuramente tutte le entry
        entries = malloc(dir_size);
        memset(entries, 0, dir_size);

        // Scorro i registri da inviare
        token = strtok(regs_to_send[i], "-");
        token = strtok(NULL, ",");
        
        while(token != NULL){
            sprintf(reg_path, "./data/registers_%d/%s.txt", my_port, token);

            if((ptr = fopen(reg_path, "r")) == NULL){
                perror("Errore: fopen()");
                exit(-1);
            }

            // Scorro tutte le entry del registro e le aggiungo al buffer da inviare
            while(EOF != fscanf(ptr, "%s", temp)){           
            //    printf("Entry: %s\n", temp);
                strcat(entries, temp);

                // Se l'entry appartiene a un registro marchiato, aggiungo il marchio alla entry
                if(token[strlen(token)-1] == '$')
                    strcat(entries, "$");
                strcat(entries, "-");
            }

            // Passo al registro successivo
            token = strtok(NULL, ",");
        }  

        // Invio la dimensione delle entry
        sprintf(size, "%d", (int)strlen(entries));
        if(send(sd, size, strlen(size),0) == -1){
            perror("Errore send()");
            exit(-1);
        }

        // Attendo ACK
        if(recv(sd, size, 4, 0) == -1){
            perror("Errore recv()");
            exit(-1);
        }

        // Invio entries 
        if(send(sd, entries, strlen(entries), 0) == -1){
            perror("Errore send()");
            exit(-1);
        }

        // Pulisco regs_to_send[i]
        memset(regs_to_send[i], 0, sizeof(regs_to_send[i]));
 
        free(entries);

        exit(0);
    }
}

void remove_spaces(char* s) 
{
    const char* d = s;
    do {
        while (*d == ' ') {
            ++d;
        }
    } 
    while ((*s++ = *d++));
}

int remove_directory(const char *path) // Rimuove i register del peer
{
   DIR *d = opendir(path);
   size_t path_len = strlen(path);
   int r = -1;

   if (d) {
      struct dirent *p;

      r = 0;
      while (!r && (p=readdir(d))) {
          int r2 = -1;
          char *buf;
          size_t len;

          // Salta i file "." e ".." 
          if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
             continue;

          len = path_len + strlen(p->d_name) + 2; 
          buf = malloc(len);

          if (buf) {
             struct stat statbuf;

             snprintf(buf, len, "%s/%s", path, p->d_name);
             if (!stat(buf, &statbuf)) {
                if (S_ISDIR(statbuf.st_mode))
                   r2 = remove_directory(buf);
                else
                   r2 = unlink(buf);
             }
             free(buf);
          }
          r = r2;
      }
      closedir(d);
   }

   if (!r)
      r = rmdir(path);

   return r;
}

void help_command()
{
	printf( "Digita un comando:\n\n"
			"1) start <DS_addr> <DS_port>  -->  connette il client al DS specificato\n"
			"2) add <type> <quantity>  -->  aggiorna il registro della data corrente\n"
			"3) get <aggr> <type> <period>  -->  richiede dato aggregato \n"
			"4) stop  -->  disconnette il peer dal network\n");
}

void start_command()    // Comando di boot
{
    int ret, i;
    char folder_path[50];
    char file_dati_aggr_path[50];
    FILE *ptr;
    socklen_t len;
    
    // Per creazione directory
    struct stat st = {0};       

    // Buffer per il recupero dei parametri
    char arg1[256];         
    char arg2[256];

    // Transfer buffer
    char buffer[BUFFER_SIZE]; 

    // Opcode messaggio start (BOOT = 1)
	uint16_t opcode = htons(1);

    // Recupero indirizzo del DS 
    scanf("%s", arg1);

    // Recupero porta del DS
    scanf("%s", arg2);
    
    // check argomenti mancanti

    // Inizializzo variabili globali
    memcpy(DS_addr, arg1, sizeof(arg1));   
    DS_port = atoi(arg2);

    // Creo cartella registri
    sprintf(folder_path, "./data/registers_%d", my_port);
    if (stat(folder_path, &st) == -1) {
        printf("Creo nuova cartella %s\n", folder_path);
        mkdir(folder_path, 0700);
    }

    // Creo archivio dati aggregati calcolati
    sprintf(file_dati_aggr_path, "./data/dati_aggregati_%d.txt", my_port);
    if((ptr = fopen(file_dati_aggr_path, "a")) == NULL){
        perror("Errore fopen()");
        exit(-1);
    }

	// Creo socket UDP per comunicare con il server
	create_sv_udp_socket();

    // Setto il timeout, SO_RCVTIMEO specifica il receiving timeout prima di segnalare un errore
    struct timeval tv;
    tv.tv_sec = TIMEOUT_INTERVAL;
    tv.tv_usec = 0;
    setsockopt(sv_udp_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

	// Preparo il buffer
	memcpy(buffer, &opcode, 2);

	// Invio boot message finchè non ricevo risposta
    do{
        // Stampa info
	    printf("Invio richiesta di connessione al network %s:%d\n", DS_addr, DS_port);

        ret = sendto(sv_udp_socket, buffer, sizeof(u_int16_t), 0,
              (struct sockaddr*)&sv_addr, sizeof(sv_addr));
        if(ret < 0)
            continue;

        // Ricevo i numeri di porta dei due neighbor
        ret = recvfrom(sv_udp_socket, buffer, BUFFER_SIZE, 0, 
                (struct sockaddr*)&sv_addr, &len);          
    }
    while(ret < 0);
    
    printf("Connessione riuscita\n");
    connected = 1;

    // Inizializzazione lista dei neighbor 
    sscanf(buffer, "%d %d", &n_list[0].port, &n_list[1].port);

   // Conto il numero di neighbors
    if(n_list[0].port != 0)
        n_neighbors++;
    if(n_list[1].port != 0)
        n_neighbors++;
    
    // Mostro a video i neighbors
    printf("Lista neighbors ricevuta:\n");
    for(i=0; i<MAX_NEIGHBORS; i++){
        if(!n_list[0].port){
            printf("Nessun neighbor disponibile\n");
            break;
        }
        if(n_list[i].port != 0)
            printf("%d) Porta: %d\n", i+1, n_list[i].port);
    }

    // Creo il socket TCP per ricevere richieste dai peer
    create_tcp_listening_socket(&listener);

    // Aggiungo il nuovo socket al set
    FD_SET(listener, &master);
    if(listener > fdmax){ fdmax = listener;}

    // Creo i socket TCP per comunicare con i neighbor
    // Avviso i neighbors, se presenti  
    for(i=0; i<n_neighbors; i++){
        create_tcp_notifying_socket(i); // socket in scrittura

        // Invio altro neighbor
        sprintf(buffer, "NEIGHBOR_REQUEST-%d", n_list[!i].port);
        if(send(n_list[i].sd, buffer, strlen(buffer), 0) == -1){
            perror("send() error");
            exit(-1);
        }
        // Aggiungo il nuovo socket al set
        FD_SET(n_list[i].sd, &master);
        if(n_list[i].sd > fdmax){ fdmax = n_list[i].sd;}
    }
}

void add_command()  // Aggiunge una entry al registro odierno
{
    int ret;
    char arg1[256], arg2[256], entry[256];
    char quantity[32];
    char event[2];
    char reg_path[50];
    char current_date[256];
    
    FILE* ptr;
    time_t rawtime;
    struct tm* timeinfo;

    scanf("%s", arg1);
    scanf("%s", arg2);    

    // Controlli sul tipo dell'evento
    if(strcmp(arg1, "tampone") && 
        (strcmp(arg1, "nuovo") || strcmp(arg2, "caso"))){        
        fprintf(stderr, "I tipi di evento sono \"tampone\" o \"nuovo caso\"\n");
        return;
    }

    // Recupero la quantità
    if(!strcmp(arg1, "tampone")){
        strcpy(quantity, arg2);    // arg2 contiene la quantità se arg1 == tampone 
        strcpy(event, "T");
    }
    else{
        scanf("%s", quantity);
        strcpy(event, "N");
    }

    // Recupero la data
    time(&rawtime);

    // Converto la data (FORSE INUTILE)
    timeinfo = localtime(&rawtime);

    // Controllo sull'ora
    if(timeinfo->tm_hour > 17 || register_closed)      // dalle 18 -> giorno dopo oppure su richiesta
        timeinfo->tm_mday++;
    
    // Creo una stringa con la data corrente
    sprintf(current_date, "%d:%d:%d", timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);

    // Preparo l'entry
    sprintf(entry, "%s,%s,%s\n", current_date, event, quantity);

    // Creo/apro il registro
    sprintf(reg_path, "./data/registers_%d/%s.txt", my_port, current_date);
    ptr = fopen(reg_path, "a");
    if(ptr == NULL){
        fprintf(stderr, "Errore apertura registro\n");
        return;
    }

    // Aggiungo l'entry
    ret = fprintf(ptr, "%s", entry);
    if(ret < 0){      
        fprintf(stderr, "Errore scrittura entry\n");
        return;
    }

    // Chiudo il file
    fclose(ptr);

    printf("Entry aggiunta\n");
}

void get_command()
{
    int found_all_registers;
    char aggr[256], type[256], period[256]="";
    char *from, *to;
    char today[11];
    char dato_richiesto[256], dato_trovato[256];
    char period_copy[50];
    char reg_path[64];
    char buffer[BUFFER_SIZE];
    char temp[64];
    long days;
    double seconds;
    time_t rawtime;
    struct tm* timeinfo;

    // Controllo su date
    struct tm dt = {0};
    time_t dt1 = 0, dt2 = 0, dt3 = 0, dt4 = 0;

    // Recupero il dato aggregato
    scanf("%s", aggr);
    if(strcmp(aggr, "totale") && strcmp(aggr, "variazione")){
        printf("I tipi di dato aggregato sono \"totale\" o \"variazione\"\n");
        return;
    }

    // Recupero il tipo dell'evento
    scanf("%s", type);
    if(strcmp(type, "T") && strcmp(type, "N")){
        printf("I tipi di evento sono \"T\" (tampone) o \"N\" (nuovo caso)\n");
        return;
    }

    // Recupero il periodo, che può mancare  
    fgets(period, sizeof(period), stdin);
    remove_spaces(period);

    // Period mancante
    if(!strcmp(period, "\n"))
        strcpy(period, "*-*");
    else 
        period[strlen(period)-1] = '\0'; // Rimuovo \n

    // strtok() modifica il parametro stringa quindi faccio una copia
    strcpy(period_copy, period);
    from = strtok(period_copy, "-");
    to = strtok(NULL, " ");

    // Parametri mancanti 
    if(!strcmp(from, "*"))
        from = start_date;      // Lower bound

    if(!strcmp(to, "*")){
    
        // Recupero la data corrente
        time(&rawtime);

        // Converto la data 
        timeinfo = localtime(&rawtime);

        // Controllo sull'ora
        if(timeinfo->tm_hour < 18)      
            timeinfo->tm_mday--;
        
        // Upper Bound
        sprintf(to, "%d:%d:%d", timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);
    }     

    // Aggiorno il periodo
    sprintf(period, "%s-%s", from , to);

    // Controlli sulle date

    // Recupero la data corrente
    time(&rawtime);

    // Converto la data 
    timeinfo = localtime(&rawtime);

    // Upper Bound
    sprintf(today, "%d:%d:%d", timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);

    // Converto le stringhe con le datae in struct tm 
    strptime(to, "%d:%m:%Y", &dt);    
    dt1 = mktime(&dt);   
    
    strptime(from, "%d:%m:%Y", &dt);    
    dt2 = mktime(&dt);

    strptime(start_date, "%d:%m:%Y", &dt);    
    dt3 = mktime(&dt);

    strptime(today, "%d:%m:%Y", &dt);    
    dt4 = mktime(&dt);

    // Differenza date
    seconds = difftime (dt2, dt1);
    days = (seconds/(60*60*24));

    // from > to
    if(days >= 0){
        printf("Data non valida, from > to\n");
        return;
    }

    // start_date > from
    seconds = difftime (dt3, dt2);
    days = (seconds/(60*60*24));

    if(days > 0){
        printf("Data non valida, start_date > from\n");
        return;
    }

    // to > oggi || to == oggi (ora oggi < 18)
    seconds = difftime (dt4, dt1);
    days = (seconds/(60*60*24));

    if(days < 0 || (days == 0 && timeinfo->tm_hour < 18)){
        printf("Data non valida, to non valido\n");
        return;
    }

    printf("Periodo: %s\n", period);

    // Salvo le info sulla richiesta get nelle variabili globali
    sprintf(last_get_aggr, "%s", aggr);
    sprintf(last_get_type, "%s", type);
    sprintf(last_get_period, "%s", period);

    // Creo una stringa contenente le info sul dato richiesto
    sprintf(dato_richiesto, "%s%s%s", aggr, type, period);

    // Controllo se il dato era già stato calcolato, e lo stampo
    printf("Cerco dato aggregato nell'archivio \n");

    if(search_aggr_data(dato_richiesto, dato_trovato)){
        print_aggr(aggr, period, dato_trovato);
        return;
    }

    printf("Non ho trovato il dato. \n"
        "Provo a calcolarlo. Contollo se ho tutte le entry\n");

    // Controllo se tutti i registri del periodo hanno il marchio
    // Uso di nuovo le variabili work-string e found

    // Pulisco la var globale missing_regs
    memset(missing_regs, 0, sizeof(missing_regs));

    sprintf(temp, "%s$", from);
    found_all_registers = 1;

    // Sposto "to" di un giorno per includerlo nella ricerca
    next_day_date(to);  

    // Aggiungo il marchio a to per fare la cmp
    strcat(to, "$");

    while(strcmp(temp, to)){  
        sprintf(reg_path, "./data/registers_%d/%s.txt", my_port, temp);
        if(fopen(reg_path, "r") == NULL){
            found_all_registers = 0;

            // Mi salvo i registers incompleti
            strncat(missing_regs, temp, strlen(temp)-1);
            strcat(missing_regs, ",");
        }         

        // Rimuovo il marchio
        temp[strlen(temp)] = '\0';

        // Data successiva
        next_day_date(temp);

        // Aggiungo il marchio
        strcat(temp, "$");  
    }  

    // Se found è rimasto a 1 allora ho tutti i registri
    if(found_all_registers){
        printf("Ho tutti i registri necessari, calcolo il dato\n");

        calcola_dato_aggregato(aggr, type, period);
        return;
    }

    printf("Non ho le entry, richiedo il dato ai neighbor\n");
    if(!n_neighbors){
        printf("Impossibile calcolare il dato richiesto: entry mancanti e neighbor assenti\n");
        return;
    }    

    // Preparo la richiesta "REQ_DATA-4240-"
    sprintf(buffer, "REQ_DATA-%d-%s", my_port, dato_richiesto);

    // Invio a(i) neighbor(s)
    if(n_neighbors > 0){
        printf("Invio REQ_DATA al neighbor 0\n");
        if(send(n_list[0].sd, buffer, strlen(buffer), 0) == -1){
            perror("Errore send()");
            exit(-1);
        }
    }

    if(n_neighbors > 1){ 
        printf("Invio REQ_DATA al neighbor 1\n");
        if(send(n_list[1].sd, buffer, strlen(buffer), 0) == -1){
            perror("Errore send()");
            exit(-1);
        }
    }

    // Inizializzo
    received_REPLY_DATA_NULL = 0;
}

void stop_command()
{
    int i, ret;
    char size[16];
    char dir_path[64], reg_path[64], temp[64];
    char buffer[BUFFER_SIZE];
    char *entries;
    int dir_size;
    FILE* ptr;
    DIR* dir;
    struct dirent *in_file;
    socklen_t len;

    // Opcode messaggio stop (STOP = 2)
	uint16_t opcode = htons(2);

    // Notifico il server della disconnessione 
    // Copio l'opcode del comando STOP nel buffer
	memcpy(buffer, &opcode, 2);

    do{
        // Stampa info
	    printf("Invio richiesta di uscita dal network %s:%d\n", DS_addr, DS_port);

        if(sendto(sv_udp_socket, buffer, sizeof(u_int16_t), 0,
              (struct sockaddr*)&sv_addr, sizeof(sv_addr)) < 0)
        if(ret < 0)
            continue;

        // Ricevo ACK dal server
        ret = recvfrom(sv_udp_socket, buffer, BUFFER_SIZE, 0, 
                (struct sockaddr*)&sv_addr, &len);          
    }
    while(ret < 0);

    // Preparo una stringa contenente tutte le mie entries
    // Apro la dir dei registri
    sprintf(dir_path, "./data/registers_%d", my_port);
    dir = opendir(dir_path);

    if(dir==NULL){
        perror("Errore apertura directory");
        exit(-1);
    }

    dir_size = get_dir_size(dir_path);

    // Alloco un buffer che contenga sicuramente tutte le entry
    entries = malloc(dir_size);
    memset(entries, 0, dir_size);

    // Scorro tutti i registri
    while( (in_file = readdir(dir)) != NULL) {
        if (!strcmp (in_file->d_name, "."))
            continue;
        if (!strcmp (in_file->d_name, ".."))
            continue;
        sprintf(reg_path, "%s/%s", dir_path, in_file->d_name);

       // printf("Apro %s\n", reg_path);

        if((ptr = fopen(reg_path, "r")) == NULL){
            perror("Errore fopen()");
            exit(-1);
        }

        // Alloco memoria al buffer dinamico

        // Scorro tutte le entry del registro e le aggiungo al buffer da inviare
        while(EOF != fscanf(ptr, "%s", temp)){           
           // printf("Entry: %s\n", temp);
            strcat(entries, temp);

            // Se l'entry appartiene a un registro marchiato, aggiungo il marchio alla entry
            if(in_file->d_name[strlen(in_file->d_name)-5] == '$')
                strcat(entries, "$");
            strcat(entries, "-");
        }
        fclose(ptr);
    }
    closedir(dir);

    // Contatto i neighbors
    for(i=0; i<n_neighbors; i++){
        
        if(i == 0 || n_neighbors == 1)
            sprintf(buffer, "STOP-0");
        else
            sprintf(buffer, "STOP-%d", n_list[0].port);
        
        // Invio messaggio per avvisare i neighbor
        printf("Invio %s\n", buffer);
        if(send(n_list[i].sd, buffer, strlen(buffer), 0) == -1){
            perror("Errore send()");
            exit(-1);
        }

        // Invio le entry solo a un neighbor, per evitare di avere entry duplicate
        if(i == 0 && n_list[1].port != 0)
            continue;

        // Attendo ACK
        if(recv(n_list[i].sd, buffer, BUFFER_SIZE, 0) == -1){
            perror("Errore recv()");
            exit(-1);
        }

        // Invio la dimensione delle entry
        sprintf(size, "%d", (int)strlen(entries));        
        if(send(n_list[i].sd, size, strlen(size),0) == -1){
            perror("Errore send()");
            exit(-1);
        }

        // Attendo ACK
        if(recv(n_list[i].sd, size, 4, 0) == -1){
            perror("Errore recv()");
            exit(-1);
        }

        // Invio entries 
        printf("Invio le mie entry a %d\n", n_list[i].port);
        if(send(n_list[i].sd, entries, strlen(entries), 0) == -1){
            perror("Errore send()");
            exit(-1);
        }
    }

    // Elimino la mia cartella
//    remove_directory(dir_path);

    printf("Disconnessione avvenuta\n");
    exit(0);
}

void receive_entries(int sd)
{
    char *buffer = NULL;
    char temp[64];
    unsigned long entries_size = 0;

    // Avviso il neighbor di inviare la size
    if(send(sd, "ACK", strlen("ACK"), 0) == -1){
        perror("Errore send()");
        exit(-1);
    }

    // Ricevo la dimensione del buffer
    if(recv(sd, temp, sizeof(temp), 0) == -1){
        perror("Errore send()");
        exit(-1);
    }

 //   printf("Size ricevuta %d\n", atoi(temp));
    entries_size = atoi(temp) + 1;

    // Alloco un buffer che possa contenere tutte le entry
    buffer = malloc(entries_size);
    memset(buffer, 0, entries_size);

    // Avviso il neighbor di inviare le entry
    if(send(sd, "ACK", strlen("ACK"), 0) == -1){
        perror("Errore send()");
        exit(-1);
    }

    if(recv(sd, buffer, entries_size-1, 0) == -1){
        perror("Errore recv()");
        exit(-1);
    }

    printf("Entry ricevute\n");
    // Aggiungo le entries ricevute alle mie
    add_entries(buffer);

    free(buffer);
}

void read_command() 
{
    char command[COMMAND_LENGTH]; 

    // Leggo il nuovo comando da stdin, max 1024 caratteri
	scanf("%1024s", command);

	if (!strncmp(command, "help", 4))		// HELP
		help_command();
	else if (!strncmp(command, "start", 5) && !connected)	// START
		start_command();
	else if (!strncmp(command, "add", 3) && connected)	// ADD
		add_command();
    else if (!strncmp(command, "get", 3) && connected)	// GET
		get_command();
	else if (!strncmp(command, "stop", 4) && connected)	// STOP
        stop_command();
    else if (!strncmp(command, "start", 5) && connected){
        while(getchar() != '\n');   //?
        fprintf(stderr, "Peer già connesso\n");     
    }
    else if (!connected){
        while(getchar() != '\n');   //?
        fprintf(stderr, "Peer non connesso\n"); 
    }
	else						
		fprintf(stderr, "Comando non valido\n"); 
}

/**
 * Entry point.
 */

int main(int argc, char** argv)
{
    int i, newfd, ret;
    socklen_t addrlen;
    char buffer[BUFFER_SIZE];

    if(argc != 2){
		fprintf(stderr, "Uso: ./peer <porta>\n"); 
		exit(-1);
    }

    my_port = atoi(argv[1]);

	// Controllo della tastiera
	fdt_init();

    // Messaggio iniziale
    printf("**********************PEER %d**********************\n", my_port);
	help_command();
    prompt();

    while(1){           
		read_fds = master;

        if(select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {  // lettura
			perror("select() error");
			exit(-1);
		}

        for(i=0; i<=fdmax; i++){
    
            if(FD_ISSET(i, &read_fds)){
               
                // Tastiera
                if(i == 0 && !waiting_for_RES_FLOOD){          
                    read_command();
                }

                // Richiesta di connessione
                else if(i == listener && !waiting_for_RES_FLOOD){         
                    struct sockaddr_in work_addr;      // struct di lavoro per ricevere il nuovo peer
                    int new_peer;
   
                    printf("Richiesta di connessione da un peer\n");
                    addrlen = sizeof(work_addr);

                    if((newfd = accept(listener, 
                        (struct sockaddr*)&work_addr, &addrlen)) == -1){
                        perror("Errore: accept()");
                        exit(-1);
                    } 
                    
                    if(recv(newfd, buffer, BUFFER_SIZE , 0) == -1){
                        perror("Errore: recv()");
                        exit(-1);
                    }

                    // Richiesta di connessione da un nuovo neighbor
                    if(!strncmp(buffer, "NEIGHBOR_REQUEST", strlen("NEIGHBOR_REQUEST"))){
                        int neighbor_to_replace;

                        sscanf(buffer, "NEIGHBOR_REQUEST-%d", &neighbor_to_replace);

                        // Trovo la posizione nell'array dei neighbor da associare al nuovo neighbor
                        new_peer = find_old_neighbor(neighbor_to_replace);   
                            
                        // Salvo la struct
                        neig_addr[new_peer] = work_addr;

                        // Chiudo il socket del vecchio neighbor socket
                        // La sostituzione avviene solo se i neighbors sono 2
                        if(n_neighbors == 2){
                            printf("Elimino il socket del vecchio neighbor %d\n", neighbor_to_replace);
                            if(close(n_list[new_peer].sd) == -1)
                                exit(-1);
                            FD_CLR(n_list[new_peer].sd, &master);
                        }

                        if(n_neighbors < 2)
                            n_neighbors++;

                        // Salvo indirizzo nuovo socket
                        n_list[new_peer].sd = newfd;

                        // Salvo porta
                        n_list[new_peer].port = ntohs(work_addr.sin_port);

                        // Aggiungo il nuovo socket
                        FD_SET(newfd, &master);
                        if(newfd > fdmax){ fdmax = newfd; }

                        printf("Nuovo neighbor connesso: %d\n", n_list[new_peer].port);

                        printf("I miei neighbor ora sono %d %d\n", n_list[0].port, n_list[1].port);
                    }
                    // Un requester è pronto a ricevere le entry
                    else if(!strncmp(buffer, "READY_TO_RECEIVE", strlen("READY_TO_RECEIVE"))){
                        int requester_port;
                        sscanf(buffer, "READY_TO_RECEIVE-%d", &requester_port);

                        printf("Richiesta di invio entry da parte di %d\n", requester_port);

                        send_requested_regs(requester_port, newfd);

                        printf("Entry inviate\n");

                        close(newfd);
                    }
                }   
                
                // mi contatta il server
                else if(i == sv_udp_socket){    
                    char reg_path[256]; 
                    addrlen = sizeof(sv_addr);

                    do{
                        ret = recvfrom(i, buffer, sizeof(buffer), 0,
                                    (struct sockaddr*)&sv_addr, &addrlen);
                        if(ret < 0)
                            sleep(1);
                    }
                    while(ret < 0);

                    if(!strcmp(buffer, "ESC")){
                        printf("Richiesta di chiusura da parte del server\n");
                        sprintf(buffer, "ACK");

                        // Invio ACK
                        do{
                            ret = sendto(i, buffer, strlen(buffer), 0, 
                                    (struct sockaddr*)&sv_addr, sizeof(sv_addr));
                            if(ret < 0)
                                sleep(1);
                        }
                        while(ret < 0);
                    
                        printf("Elimino registri\n");

                        // Elimino i registri ma mantengo i dati aggregati
                        sprintf(reg_path, "./data/registers_%d", my_port);
                    //    remove_directory(reg_path);

                        printf("Disconnessione avvenuta\n");
                        sleep(3);
                        exit(0);
                    }
                }
                
                // Mi contatta un neighbor
                else if((i == n_list[0].sd || i == n_list[1].sd)) {                  
                    if(recv(i, buffer, sizeof(buffer), 0) == -1){
                        perror("errore recv()");
                        exit(-1);
                    }

                    // Ricevuta richiesta di dato aggregato
                    if(!strncmp(buffer, "REQ_DATA", 8)){ 
                        int source_port;
                        char data_requested[64], dato_aggr[1024];

                        // Recupero informazioni
                        sscanf(buffer, "REQ_DATA-%d-%s", &source_port, data_requested);
                        printf("REQ_DATA ricevuta da %d richiesta: %s\n", source_port, data_requested);

                        // Cerco dato nell'archivio
                        ret = search_aggr_data(data_requested, dato_aggr);

                        printf("Invio REPLY_DATA\n");
                        send_REPLY_DATA(ret, dato_aggr, i);
                    }

                    // risposta alla richiesta di dato aggregato
                    else if(!strncmp(buffer, "REPLY_DATA", 10)){

                        if(!strncmp(buffer, "REPLY_DATA-NULL", strlen(buffer))){

                            // Aspetto di ricevere la REPLY_DATA da entrambi i neighbor prima di fare il flood
                            received_REPLY_DATA_NULL++;
                            
                            if(n_neighbors == 1 && received_REPLY_DATA_NULL == 1){

                                printf("Invio FLOOD_FOR_ENTRIES a %d\n", n_list[0].port);
                                send_FLOOD_FOR_ENTRIES(n_list[0].sd, my_port, missing_regs);
                            }
                            else if(n_neighbors == 2 && received_REPLY_DATA_NULL == 2){

                                printf("Invio FLOOD_FOR_ENTRIES a %d\n", n_list[0].port);
                                send_FLOOD_FOR_ENTRIES(n_list[0].sd, my_port, missing_regs);

                                printf("Invio FLOOD_FOR_ENTRIES a %d\n", n_list[1].port);
                                send_FLOOD_FOR_ENTRIES(n_list[1].sd, my_port, missing_regs);
                            }
                            else
                                continue;

                            // Chiedo le entry mancanti ai peers
                            printf("Dato non disponibile\n");

                            // Disattivo la tastiera e le richieste
                            waiting_for_RES_FLOOD = 1;

                            // Creo un timer entro il quale posso ricevere le entry
                            if((timerfd = timerfd_create(CLOCK_REALTIME,0)) == -1){
                                perror("Errore: timerfd_create()");
                                exit(-1);
                            }
                            
                            struct itimerspec timspec;
                            memset(&timspec, 0, sizeof(timspec)); //check
                            timspec.it_value.tv_sec = RES_FLOOD_TIMEOUT;
                            timspec.it_value.tv_nsec = 0;
                            timspec.it_interval.tv_sec = 0;
                            timspec.it_interval.tv_nsec = 0;

                            if(timerfd_settime(timerfd, 0, &timspec, 0) == -1){
                                perror("Errore: timerfd_settime()");
                                exit(-1);
                            }

                            // Aggiungo il descrittore del timer alla select()
                            FD_SET(timerfd, &master);
                            if(timerfd > fdmax){ fdmax = timerfd;}
                        }
                        else{
                            // Ho ricevuto il dato aggregato 
                            char dato_ricevuto[1024];
                            char archivio_path[64];
                            FILE *ptr;

                            sscanf(buffer, "REPLY_DATA-%s", dato_ricevuto);
                            
                            // Stampo il dato aggregato
                            print_aggr(last_get_aggr, last_get_period, dato_ricevuto);

                            printf("Aggiungo il dato aggregato all'archivio\n");

                            sprintf(archivio_path, "./data/dati_aggregati_%d.txt", my_port);

                            if((ptr = fopen(archivio_path, "a")) == NULL){
                                perror("Errore fopen()");
                                exit(-1);
                            }

                            if(fprintf(ptr, "%s%s%s=%s\n", last_get_aggr, last_get_type, last_get_period, dato_ricevuto) == -1){
                                perror("Errore fprintf()");
                                exit(-1);
                            }

                            fclose(ptr);
                        } 
                    }

                    // risposta al FLOOD_FOR_ENTRIES
                    else if(!strncmp(buffer, "FLOOD_FOR_ENTRIES", 17)){
                        int source_port;
                        char regs_requested[BUFFER_SIZE];
                        char res_flood[BUFFER_SIZE];

                        // Recupero info
                        sscanf(buffer, "FLOOD_FOR_ENTRIES-%d-%s", &source_port, regs_requested);

                        if(my_port == source_port){
                            // Se il FLOOD_FOR_ENTRIES era mio, non invio entry
                            printf("Ho ricevuto il mio FLOOD_FOR_ENTRIES\n");
                        }
                        else{
                            int dest_sd;
                            char reg_path[64];
                            char *token;
                            char reg_removed[20];
                            char temp[1024];

                            // Variabile per inizializzare la stringa regs_to_send
                            int i_have_regs = 0;    

                            printf("Ricevuto FLOOD_FOR_ENTRIES di %d da ", source_port);

                            if(i == n_list[0].sd)
                                printf("%d\n", n_list[0].port);

                            if(i == n_list[1].sd)
                                printf("%d\n", n_list[1].port);

                            // Rimuovo dai regs requested quelli che ho già marchiati
                            strcpy(temp, regs_requested);
                            token = strtok(temp, ",");
                    
                            while(token != NULL){  

                                // Controllo se ho il registro marchiato
                                sprintf(reg_path, "./data/registers_%d/%s$.txt", my_port, token);
                                    
                                if(fopen(reg_path, "r") != NULL){
                                    if(i_have_regs == 0){
                                        // Aggiungo la porta del richiedente come identificatore
                                        sprintf(regs_to_send[n_requesters], "%d-", source_port);    
                                        i_have_regs++;
                                        n_requesters++;
                                    }
                                       
                                    // Aggiungo il registro alla lista di quelli da spedire
                                    strcat(regs_to_send[n_requesters-1], token);
                                    strcat(regs_to_send[n_requesters-1], "$");
                                    strcat(regs_to_send[n_requesters-1], ",");

                                    // Rimuovo il registro marchiato da quelli richiesti dal requester
                                    strcpy(reg_removed, token);
                                    strcat(reg_removed, ",");
                                    strremove(regs_requested, reg_removed);
                                    token = strtok(NULL, ",");
                                    continue;
                                }       

                                // Controllo se ho il registro normale
                                sprintf(reg_path, "./data/registers_%d/%s.txt", my_port, token);
    
                                if(fopen(reg_path, "r") != NULL){
                                    if(i_have_regs == 0){
                                        // Aggiungo la porta del richiedente come identificatore
                                        sprintf(regs_to_send[n_requesters], "%d-", source_port);    
                                        i_have_regs++;
                                        n_requesters++;
                                    }
                                    strcat(regs_to_send[n_requesters-1], token);
                                    strcat(regs_to_send[n_requesters-1], ",");
                                }
                                token = strtok(NULL, ",");
                            }  

                            // Se avevo almeno un registro richiesto invio il RES_FLOOD
                            if(i_have_regs){
                                printf("Lista di regs da spedire %s\n", regs_to_send[n_requesters-1]);

                                // Controllo se avevo già salvato le entry da mandare al peer 
                                // i Flood che arrivano a un peer sono doppi
                                if(already_to_serve(regs_to_send[n_requesters-1])){
                                    printf("Richiesta multipla: non inoltro il FLOOD_FOR_ENTRIES\n");
                                    n_requesters--;
                                    memset(buffer, 0, sizeof(buffer));
                                    prompt();
                                    continue;
                                }

                                // Aggiungo ! per delimitare i messaggi consecutivi
                                sprintf(res_flood, "RES_FLOOD-%d-%d!", source_port, my_port);

                                if(i == n_list[0].sd){
                                    printf("Invio RES_FLOOD a %d\n", n_list[0].port);
                                }
                                else{
                                    printf("Invio RES_FLOOD a %d\n", n_list[1].port);
                                }                                
                                if(send(i, res_flood, strlen(res_flood), 0) == -1){
                                    perror("Errore send()");
                                    exit(-1);
                                }

                                // Evito di inoltrare i FLOOD vuoti
                                if(!strcmp(regs_requested, "")){
                                    printf("FLOOD_FOR_ENTRIES vuoto, non inoltro\n");
                                    memset(buffer, 0, BUFFER_SIZE);
                                    prompt();
                                    continue;
                                }
                            }

                            // Inoltro i FLOOD_FOR_ENTRIES 
                            if(i == n_list[0].sd && n_neighbors > 1){
                                dest_sd = n_list[1].sd;
                                printf("Inoltro FLOOD_FOR_ENTRIES a %d \n", n_list[1].port);
                            }
                            else{
                                dest_sd = n_list[0].sd;
                                printf("Inoltro FLOOD_FOR_ENTRIES a %d \n", n_list[0].port);
                            }

                            send_FLOOD_FOR_ENTRIES(dest_sd, source_port, regs_requested);
                        }                     
                    }
                    else if(!strncmp(buffer, "RES_FLOOD", 9)){
                        int source_port, donor_port;
                        char buffer_copy[BUFFER_SIZE];                        
                        char *pch;
                        char entry[100];

                        // TCP Unisce in un unico messaggio più send() provenienti dallo stesso socket
                        // uso una copia perchè strtok modifica il buffer
                        // ! è il separatore dei RES_FLOOD
                        strcpy(buffer_copy, buffer);
                        pch = strtok (buffer_copy,"!");

                        while (pch != NULL){
                            strcpy(entry, pch);
                            sscanf(pch, "RES_FLOOD-%d-%d", &source_port, &donor_port);

                            // Se il RES_FLOOD è relativo a una richiesta partita da me, invio richiesta per entry                                                                     
                            if(my_port == source_port){ 
                                printf("RES_FLOOD ricevuto da %d\n", donor_port);
                                
                                // Valore corrente del timer
                                struct itimerspec curr_value;   

                                // Blocco il timer
                                if((timerfd_gettime(timerfd, &curr_value)) == -1){
                                    perror("Errore: timerfd_gettime()");
                                    exit(-1);
                                }

                                // Contatto per ricevere le entry 
                                send_READY_TO_RECEIVE(donor_port);

                                // Faccio riprendere il timer da dove era stato interrotto
                                if((timerfd_settime(timerfd, 0, &curr_value, 0)) == -1){
                                    perror("Errore timerfd_settime()");
                                    exit(-1);
                                }
                            }
                            
                            // Inoltro il RES_FLOOD
                            else{
                                int dest_sd;
                                
                                if(i == n_list[0].sd){
                                    dest_sd = n_list[1].sd;
                                    printf("Inoltro RES_FLOOD di %d da %d a %d\n",donor_port, n_list[0].port, n_list[1].port);
                                }
                                else{
                                    dest_sd = n_list[0].sd;
                                    printf("Inoltro RES_FLOOD di %d da %d a %d\n", donor_port, n_list[1].port, n_list[0].port);
                                }

                                // RI-aggiungo il divisore alla entry
                                strcat(entry, "!");
                                if((send(dest_sd, entry, strlen(entry), 0)) == -1){
                                    perror("Errore send()");
                                    exit(-1);
                                }
                            }

                        // Scorro alla RES_FLOOD successiva
                        pch = strtok (NULL, "!");
                        }
                    }

                    //Un neighbor lascia il network
                    if(!strncmp(buffer, "STOP", 4)){                        
                        int leaving_neighbor, replace_this_port, new_neighbor = 0;

                        // Identifico il neighbor che sta abbandonando
                        if(i == n_list[1].sd)
                            leaving_neighbor = 1;
                        else 
                            leaving_neighbor = 0;

                        // Porta da comunicare al nuovo neighbor affinchè la sostituisca
                        replace_this_port = n_list[leaving_neighbor].port;

                        sscanf(buffer, "STOP-%d", &new_neighbor);
                        printf("Il neighbor %d abbandona il network\n", n_list[leaving_neighbor].port);

                        // Aggiungo le entry del neighbor che si sta disconnetendo
                        if(new_neighbor != 0)
                            receive_entries(i);

                        printf("Elimino il socket del vecchio neighbor %d\n", n_list[leaving_neighbor].port);
 
                        if(close(n_list[leaving_neighbor].sd) == -1)
                            exit(-1);
                        FD_CLR(n_list[leaving_neighbor].sd, &master);  
                            
                        n_list[leaving_neighbor].port = new_neighbor;

                        n_neighbors--;

                        // Devo contattare l'altro neighbor
                        if(new_neighbor != 0 && (n_list[!leaving_neighbor].port != new_neighbor)){
                            n_neighbors++;
                            // Avviso il mio nuovo neighbor dello scambio
                            create_tcp_notifying_socket(leaving_neighbor);

                            // Invio il neighbor da rimpiazzare al mio nuovo neighbor
                            sprintf(buffer, "NEIGHBOR_REQUEST-%d", replace_this_port);
                            if(send(n_list[leaving_neighbor].sd, buffer, strlen(buffer), 0) == -1){
                                perror("send() error");
                                exit(-1);
                            }

                            // Aggiungo il nuovo socket al set
                            FD_SET(n_list[leaving_neighbor].sd, &master);
                            if(n_list[leaving_neighbor].sd > fdmax){ fdmax = n_list[leaving_neighbor].sd;}
                        }
                    }
                } 
               
                // Timer per la ricezione dei RES_FLOOD
                else if(i == timerfd){
                    FD_CLR(timerfd, &master);
                    printf("Finito timer per ricevere i RES_FLOOD\n");

                    // Marchio i regs che non erano marchiati
                    add_mark_to_regs(last_get_period);

                    // Calcolo dato e restituisco
                    calcola_dato_aggregato(last_get_aggr, last_get_type, last_get_period);

                    waiting_for_RES_FLOOD = 0;
                }
                memset(buffer, 0, BUFFER_SIZE);
                prompt();
            }	        
        }
    }
}