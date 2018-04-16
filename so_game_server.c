#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>  // htons() and inet_addr()
#include <netinet/in.h> // struct sockaddr_in
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include "common.h"
#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "so_game_protocol.h"

#define BUFFER_SIZE     1000000       // Dimensione massima dei buffer utilizzati
#define TCP_PORT        25252         // Porta per la connessione TCP
#define UDP_PORT        8888          // Porta per la connessione UDP
#define SERVER_ADDRESS  "127.0.0.1"   // Indirizzo del server (localhost)

/* Struttura per args dei threads in TCP */
typedef struct {
	int client_desc;
	Image* elevation_texture;
	Image* surface_elevation;
} tcp_args_t;

/* Definizione Socket e variabili 'globali' */
int tcp_socket, udp_socket, World world;

//UserList* users;  // Lista degli utenti connessi


/* Gestione pacchetti TCP ricevuti */
int TCP_packet (int tcp_socket, int id, char* buffer, Image* surface_elevation, Image* elevation_texture) {
  PacketHeader* header = (PacketHeader*) buffer;  // Pacchetto per controllo del tipo di richiesta

  /* Se la richiesta dal client a questo server è per l'ID (invia l'id assegnato al client che lo richiede) */
  if (header->type == GetId) {
    // Crea un IdPacket utilizzato per mandare l'id assegnato dal server al client (specifica struct per ID)
    IdPacket* id_to_send = (IdPacket*) malloc(sizeof(IdPacket));

    PacketHeader header_send;
    header_send.type = GetId;
    
    id_to_send->header = header_send;
    id_to_send->id = id;  // Gli assegno l'id passato da funzione TCPHandler

    char buffer_send[BUFFER_SIZE];
    int pckt_length = Packet_serialize(buffer_send, &(id_to_send->header)); // Ritorna il numero di bytes scritti

    // Invio del messaggio tramite socket
    int bytes_sent = 0;
    int ret;
    while(bytes_sent < pckt_length){
      ret = send(tcp_socket, buffer_send + bytes_sent, pckt_length - bytes_sent,0);
      if (ret==-1 && errno==EINTR) continue;
      ERROR_HELPER(ret, "Errore nell'assegnazione dell'ID!");
      if (ret==0) break;
      bytes_sent += ret;
    }

    Packet_free(&(id_to_send->header));   // Libera la memoria del pacchetto non più utilizzato
    free(id_to_send);

    fprintf(stdout, "[ID Sent] %d!\n", id);  // DEBUG OUTPUT

    return 1;
  }

  /* Se la richiesta dal client a questo server è per la texture della mappa */
  else if (header->type == GetTexture) {
    // Converto il pacchetto ricevuto in un ImagePacket per estrarne la texture richiesta
    ImagePacket* texture_request = (ImagePacket*) buffer;
    int id_request = texture_request->id;
    
    // Preparo header per la risposta
    PacketHeader header_send;
    header_send.type = PostTexture;
    
    // Preparo il pacchetto per inviare la texture al client
    ImagePacket* texture_to_send = (ImagePacket*) malloc(sizeof(ImagePacket));
    texture_to_send->header = header_send;
    texture_to_send->id = id_request;
    texture_to_send->image = elevation_texture;

    char buffer_send[BUFFER_SIZE];
    int pckt_length = Packet_serialize(buffer_send, &(texture_to_send->header)); // Ritorna il numero di bytes scritti

    // Invio del messaggio tramite socket
    int bytes_sent = 0;
    int ret;
    while(bytes_sent < pckt_length){
      ret = send(tcp_socket, buffer_send + bytes_sent, pckt_length - bytes_sent,0);
      if (ret==-1 && errno==EINTR) continue;
      ERROR_HELPER(ret, "Errore nella richiesta della Texture!");
      if (ret==0) break;
      bytes_sent += ret;
    }

    Packet_free(&(texture_to_send->header));   // Libera la memoria del pacchetto non più utilizzato
    free(texture_to_send);

    fprintf(stdout, "[Texture Sent] %d!\n", id);   // DEBUG OUTPUT

    return 1;
  }

  /* Se la richiesta dal client a questo server è per la elevation surface */
  else if (header->type == GetElevation) {
    // Converto il pacchetto ricevuto in un ImagePacket per estrarne la elevation richiesta
    ImagePacket* elevation_request = (ImagePacket*) buffer;
    int id_request = elevation_request->id;
    
    // Preparo header per la risposta
    PacketHeader header_send;
    header_send.type = PostElevation;
    
    // Preparo il pacchetto per inviare la elevation al client
    ImagePacket* elevation_to_send = (ImagePacket*) malloc(sizeof(ImagePacket));
    elevation_to_send->header = header_send;
    elevation_to_send->id = id_request;
    elevation_to_send->image = surface_elevation;

    char buffer_send[BUFFER_SIZE];
    int pckt_length = Packet_serialize(buffer_send, &(elevation_to_send->header)); // Ritorna il numero di bytes scritti

    // Invio del messaggio tramite socket
    int bytes_sent = 0;
    int ret;
    while(bytes_sent < pckt_length){
      ret = send(tcp_socket, buffer_send + bytes_sent, pckt_length - bytes_sent,0);
      if (ret==-1 && errno==EINTR) continue;
      ERROR_HELPER(ret, "Errore nella richiesta della Elevation!");
      if (ret==0) break;
      bytes_sent += ret;
    }

    Packet_free(&(elevation_to_send->header));   // Libera la memoria del pacchetto non più utilizzato
    free(elevation_to_send);

    fprintf(stdout, "[Elevation Sent] %d!\n", id);   // DEBUG OUTPUT

    return 1;
  }

  /* Se il server riceve una texture dal client */
  else if (header->type == PostTexture) {
    PacketHeader* received_header = Packet_deserialize(buffer, header->size);
    ImagePacket* received_texture = (ImagePacket*) received_header;
    
    Vehicle* new_vehicle = malloc(sizeof(Vehicle));
    Vehicle_init(new_vehicle, &world, id, received_texture->image);
    World_addVehicle(&world, new_vehicle);

   	Packet_free(received_header);	// Libera la memoria del pacchetto non più utilizzato
    free(received_texture);

    fprintf(stdout, "[Texture Received] %d!\n", id);   // DEBUG OUTPUT

    return 1;
  }

  /* Nel caso si verificasse un errore */
  else fprintf(stdout, "[Error] Unknown packet received %d!\n", id);   // DEBUG OUTPUT

  return -1;  // Return in caso di errore
}



/* Gestione del thread del client per aggiunta del client alla lista e controllo pacchetti tramite TCP_packet (DA COMPLETARE) */
void* TCP_client_handler (void* args){
  /* TODO: creare lista utenti alla quale aggiungere l'utente appena connesso */
  tcp_args_t* tcp_args = (tcp_args_t*) args;

  int tcp_client_desc = tcp_args->client_desc;

  int msg_length = 0;
  int ret;
  char buffer_recv[BUFFER_SIZE];	// Conterrà il PacketHeader

  int packet_length = sizeof(PacketHeader);

  /* Ricezione del pacchetto */
  while(msg_length < packet_length){
  	ret = recv(tcp_client_desc, buffer_recv + msg_length, packet_length - msg_length, 0);
  	if (ret==-1 && errno == EINTR) continue;
  	ERROR_HELPER(ret, "[TCP Client Thread] Failed to receive packet");
  	msg_length += ret;
  }

  /* Ricezione pacchetto per intero (tramite l'utilizzo di 'size' del PacketHeader ricevuto (in caso sia più grande di un PacketHeader) */
  PacketHeader* header = (PacketHeader*) buffer_recv;
  int size = header->size - packet_length;
  msg_length=0;

  while(msg_length < size){
    ret = recv(tcp_client_desc, buffer_recv + msg_length + packet_length, size - msg_length, 0);
    if (ret==-1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "[TCP Client Thread] Failed to receive packet");
    msg_length += ret;
  }

  /* Gestione del pacchetto ricevuto tramite l'handler dei pacchetti */
  ret = TCP_packet(tcp_client_desc, tcp_args->client_desc, buffer_recv, tcp_args->surface_elevation, tcp_args->elevation_texture);

  if (ret == 1) fprintf(stdout, "[TCP Client Thread] Success");
  else fprintf(stdout, "[TCP Client Thread] Failed");

  /* Chiusura thread */
  pthread_exit(0);
}



/* Handler della connessione TCP con il client (nel thread) */
void* TCP_handler(void* args){
	int ret;
  tcp_args_t* tcp_args = (tcp_args_t*) args;	// Cast degli args da void a tcp_args_t

  /* Creazione nuovo utente e inserimento in lista */
  /*
  User user = malloc(sizeof(User));
  user->id = tcp_args->client_desc;
  user->x     = 0;
  user->y     = 0;
  user->theta = 0;

  UserList_insert(users, user);
  */

  int sockaddr_len = sizeof(struct sockaddr_in);
  struct sockaddr_in client_addr = {0};
  int tcp_client_desc = accept(tcp_socket, (struct sockaddr*)&client_addr, (socklen_t*) &sockaddr_len);   // Accetta nuova connessione dal client
	ERROR_HELPER(tcp_client_desc, "[Error] Failed to accept client TCP connection");

	pthread_t client_thread;

	/* args del thread client */
	tcp_args_t tcp_args_aux;
	tcp_args_aux.client_desc = tcp_client_desc;
	tcp_args_aux.elevation_texture = tcp_args->elevation_texture;
	tcp_args_aux.surface_elevation = tcp_args->surface_elevation;

	/* Thread create - TODO: TCP_client_handler da implementare */
	ret = pthread_create(&client_thread, NULL, TCP_client_handler, &tcp_args_aux);
	PTHREAD_ERROR_HELPER(ret, "[Client] Failed to create TCP client thread");

	/* Thread detach (NON JOIN) */
	ret = pthread_detach(client_thread);

	/* Chiusura thread */
  pthread_exit(0);
}



/* Handler della connessione UDP con il client in modalità 'receiver' (riceve pacchetti) */
void* UDP_receiver_handler(void* args) {

}


/* Handler della connessione UDP con il client in modalità 'sender' (invia pacchetti) */
void* UDP_sender_handler(void* args) {
  int ret;
  char buffer_send[BUFFER_SIZE];

  /* Creazione del pacchetto da inviare */
  PacketHeader header;
  header.type = WorldUpdate;

  WorldUpdatePacket* world_update = (WorldUpdatePacket*) malloc(sizeof(WorldUpdatePacket));
  world_update->header = header;

  int num_vehicles_update = 0;  /* TODO: Implementare lista client da contare */
 
  /*
  User user = users->first;
  while(user != NULL) {
    num_vehicles_update++;
    user = user->next;
  }

  user = users->first;
  */

  world_update->updates = (ClientUpdate*)malloc(sizeof(ClientUpdate) * n);

  for (int i=0; i<num_vehicles_update; i++) {
    ClientUpdate i_client = &(world_update->updates[i]);

    /*
    client_update->id = user->id;
    client_update->x = Vehicle_getX(user[i]);
    client_update->y = Vehicle_getY(user[i]);
    client_update->theta = Vehicle_getTheta(user[i]);
    client_update->translational_force = Vehicle_getTranslationalForce(user[i]);
    client_update->rotational_force = Vehicle_getRotationalForce(user[i]);

    user = user->next;
    */
  }
  int size = Packet_serialize(buffer_send, &world_update->header)

  //user = users->first;

  /* Invia i pacchetti a tutti gli utenti connessi */
  /*
  while (user != NULL) {
    ret = sendto(udp_socket, buffer_send, size, 0, (struct sockaddr*)&user->user_addr, (socklen_t)sizeof(user->user_addr));
    user = user->nex;
  }
  */

  Packet_free(world_update->header);
  Packet_free(world_update);
  pthread_exit(0);
}


/* Main */
int main(int argc, char **argv) {
  int ret;	// Variabile utilizzata per i vari controlli sui return delle connessioni, ...

  if (argc<3) {
    printf("usage: %s <elevation_image> <texture_image>\n", argv[1]);
    exit(-1);
  }
  char* elevation_filename=argv[1];
  char* texture_filename=argv[2];
  char* vehicle_texture_filename="./images/arrow-right.ppm";
  printf("loading elevation image from %s ... ", elevation_filename);

  // load the images
  Image* surface_elevation = Image_load(elevation_filename);
  if (surface_elevation) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }


  printf("loading texture image from %s ... ", texture_filename);
  Image* surface_texture = Image_load(texture_filename);
  if (surface_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  printf("loading vehicle texture (default) from %s ... ", vehicle_texture_filename);
  Image* vehicle_texture = Image_load(vehicle_texture_filename);
  if (vehicle_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  /* Inizializza server TCP */
  tcp_socket = socket(AF_INET , SOCK_STREAM , 0);
  ERROR_HELPER(tcp_socket, "[TCP] Failed to create TCP socket");

  struct sockaddr_in tcp_server_addr = {0};
  int sockaddr_len = sizeof(struct sockaddr_in);
  tcp_server_addr.sin_addr.s_addr = INADDR_ANY;
  tcp_server_addr.sin_family      = AF_INET;
  tcp_server_addr.sin_port        = htons((uint16_t) TCP_PORT);

  int reuseaddr_opt_tcp = 1;
  ret = setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt_tcp, sizeof(reuseaddr_opt_tcp));
  ERROR_HELPER(ret, "[TCP] Failed setsockopt on TCP server socket");

  ret = bind(tcp_socket, (struct sockaddr*) &tcp_server_addr, sockaddr_len);
  ERROR_HELPER(ret, "[TCP] Failed bind address on TCP server socket");

  ret = listen(tcp_socket, 16);
  ERROR_HELPER(ret, "[TCP] Failed listen on TCP server socket");

  fprintf(stdout, "[TCP] Server Started!");  // DEBUG OUTPUT
  /* Server TCP inizializzato */

  /* Inizializza server UDP */
  udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(udp_socket, "[UDP] Failed to create UDP socket");

  struct sockaddr_in udp_server_addr = {0};
  udp_server_addr.sin_addr.s_addr = INADDR_ANY;
  udp_server_addr.sin_family      = AF_INET;
  udp_server_addr.sin_port        = htons((uint16_t) UDP_PORT);

  int reuseaddr_opt_udp = 1;
  ret = setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt_udp, sizeof(reuseaddr_opt_udp));
  ERROR_HELPER(ret, "[UDP] Failed setsockopt on UDP server socket");

  ret = bind(udp_socket, (struct sockaddr*) &udp_server_addr, sizeof(udp_server_addr));
  ERROR_HELPER(ret, "[UDP] Failed bind address on UDP server socket");

  fprintf(stdout, "[UDP] Server Started!");  // DEBUG OUTPUT
  /* Server UDP inizializzato */

  /* Inizializzazione utenti */
  /*
  users = (UserList*) malloc(sizeof(UserList));
  UserList_init(users);
  fprintf(stdout, "Lista degli utenti inizializzata");
  */

  /* Inizializzazione del mondo */
  World_init(&world, surface_elevation, surface_texture,  0.5, 0.5, 0.5);
  fprintf(stdout, "Mondo inizializzato");

  /* ------------------- */
  /* Gestione dei thread */
  /* ------------------- */
  pthread_t TCP_connection, UDP_sender_thread, UDP_receiver_thread, world_thread;

  /* Args per il thread TCP */
  tcp_args_t tcp_args;
  tcp_args.elevation_texture = surface_texture;
  tcp_args.surface_elevation = surface_elevation;

  /* Create dei thread */
  ret = pthread_create(&TCP_connection, NULL, TCP_handler, &tcp_args);
  PTHREAD_ERROR_HELPER(ret, "Failed to create TCP connection thread");

  ret = pthread_create(&UDP_sender_thread, NULL, UDP_sender_handler, NULL);
  PTHREAD_ERROR_HELPER(ret, "Failed to create UDP sender thread");

  ret = pthread_create(&UDP_receiver_thread, NULL, UDP_receiver_handler, NULL);  // TODO: Definire la funzione UDP_receiver_handler
  PTHREAD_ERROR_HELPER(ret, "Failed to create UDP receiver thread");

  ret = pthread_create(&world_thread, NULL, world_handler, NULL); // TODO: Definire la funzione world_handler
  PTHREAD_ERROR_HELPER(ret, "Failed to create World server thread");
  
  /* Join dei thread */
  ret=pthread_join(TCP_connection,NULL);
  ERROR_HELPER(ret,"Failed to join TCP server connection thread");

  ret=pthread_join(UDP_sender_thread,NULL);
  ERROR_HELPER(ret,"Failed to join UDP server sender thread");

  ret=pthread_join(UDP_receiver_thread,NULL);
  ERROR_HELPER(ret,"Failed to join UDP server receiver thread");

  ret=pthread_join(world_thread,NULL);
  ERROR_HELPER(ret,"Failed to join World server thread");

  /* Cleanup generale per liberare la memoria utilizzata */
  Image_free(surface_texture);
  Image_free(surface_elevation);
  World_destroy(&world);
  return 0;     
}