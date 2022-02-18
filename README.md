# P2P Covid Tracker

P2P command line application for the distribution of info about Covid19 cases using C socket
realized for the course in Networking @ University of Pisa.

Refer to the [project assignment](docs/requirements.pdf) to check all the requirements and to the 
[report](docs/report.pdf) to read the design choices that I made.

## Project structure
 - `data`: contains the peer's daily registries
 - `docs`: assignment and the report
 - `test`: registries to test the functionalities of the application
 - `ds.c`: discovery server
 - `peer.c`: peer
 - `exec.sh`: bash script that launches the server and 5 peers

## Usage

The discovery server can be started with the following syntax:
```
$ ./ds <server_port>
```

The discovery server supports the following operations:
 - `help`: prints an help message.
 - `status`: shows a list of connected peers.
 - `showneighbor <peer>`: shows the neighbours of the specified peer.
 - `esc`: close the discovery server.

Example of server execution:
```
**********************DS COVID STARTED**********************
Digita un comando:

1) help --> mostra i dettagli dei comandi
2) status -->  mostra un elenco di peer connessi
3) showneighbor <peer>  --> mostra i neighbor di un peer
4) esc  --> chiude il DSDettaglio comandi

> status
Peer 1: IP 127.0.0.1 - Porta 4241
Peer 2: IP 127.0.0.1 - Porta 4243
Peer 3: IP 127.0.0.1 - Porta 4248
Peer 4: IP 127.0.0.1 - Porta 4249
Peer 5: IP 127.0.0.1 - Porta 4240

> showneighbor 4240
Peer 4240 - Neighbors: 4241 4249
```


The peer client can be started with the following syntax:
```
$ ./peer <server_IP_address> <server_port>
```

The discovery server supports the following operations:
 - `start <DS_addr> <DS_port>`: connects the peer to the specified discovery server.
 - `add <type> <quantity>`: updates the daily register.
 - `get <aggr> <type> <period>`: requires aggregated data (see assignment for more details).
 - `stop`: disconnects the peer from the network.

Example of peer execution:
```
**********************PEER 4240**********************
Digita un comando:

1) start <DS_addr> <DS_port>  -->  connette il client al DS specificato
2) add <type> <quantity>  -->  aggiorna il registro della data corrente
3) get <aggr> <type> <period>  -->  richiede dato aggregato 
4) stop  -->  disconnette il peer dal network

> start 127.0.0.1 4242
Invio richiesta di connessione al network 127.0.0.1:4242
Connessione riuscita

> add tampone 100
Entry aggiunta

> add nuovo caso 20
Entry aggiunta

> get variazione N 10:12:2020-15:12:2020
Variazione 11:12:2020: -3000
Variazione 12:12:2020: 0
Variazione 13:12:2020: 0
Variazione 14:12:2020: 8000
Variazione 15:12:2020: -8000

> stop
Invio richiesta di uscita dal network 127.0.0.1:4242
Disconnessione avvenuta

```
