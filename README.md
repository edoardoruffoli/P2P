# P2P Covid Tracker

P2P command line application for the distribution of info about Covid19 cases using C socket
realized for the course in Networking @ University of Pisa.

The [project assignment](docs/requirements.pdf) requires to:

The discovery server can be started with the following syntax:
```
$ ./ds <server_port>
```

The discovery server supports the following operations:
 - `help`: prints an help message.
 - `status`: shows a list of connected peers.
 - `showneighbor <peer>`: shows the neighbours of the specified peer.
 - `esc`: close the discovery server.


The peer client can be started with the following syntax:
```
$ ./peer <server_IP_address> <server_port>
```

The discovery server supports the following operations:
 - `start <DS_addr> <DS_port>`: connects the peer to the specified discovery server.
 - `add <type> <quantity>`: updates the daily register.
 - `get <aggr> <type> <period>`: requires aggregated data (see assignment for more details).
 - `stop`: disconnects the peer from the network.
