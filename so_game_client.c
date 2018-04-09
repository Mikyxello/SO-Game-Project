
#include <GL/glut.h>
#include <math.h>
#include <string.h>
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

#define TIME_TO_SLEEP = 20000;

int my_id;
int window;
WorldViewer viewer;
World world;
Vehicle* vehicle; // The vehicle
int socket_desc; //socket tcp
struct timeval last_update_time;
int server_connected;
playerWorld player_world;
sem_t* world_update_sem;

typedef struct playerWorld{
    int id_list[WORLDSIZE];
    int players_online;
    Vehicle** vehicles;
}localWorld;

typedef struct udpArgs{
    localWorld* lw;
    struct sockaddr_in server_addr;
    int socket_udp;
    int socket_tcp;
}udpArgs;

void* UDP_Sender(void* args){
    udpArgs udp_args =(udpArgs*)args;
    struct sockaddr_in server_addr=udp_args.server_addr;
    int socket_udp =udp_args.socket_udp;
    int serverlen=sizeof(server_addr);   
    while (server_connected){
		ret = send_updates(socket_udp,server_addr,serverlen);
		PTHREAD_ERROR_HELPER(ret,"error during send_updates");
		usleep(TIME_TO_SLEEP);
	}
    close(s);
    return 0;
}

void* UDP_Receiver(void* args){
	udpArgs udp_args =(udpArgs*)args;
	struct sockaddr_in server_addr=udp_args.server_addr;
    int socket_udp =udp_args.socket_udp;
    int serverlen=sizeof(server_addr);
    int bytes_read;
    char buf_rcv[1000000];
        while (server_connected){
		bytes_read=recvfrom(socket_udp, buf_rcv, 1000000, 0, (struct sockaddr*) &server_addr, &addrlen);
		PTHREAD_ERROR_HELPER(bytes_read,"error during recvfrom");
		ret = packet_analisys(buf_rcv,bytes_read);
		PTHREAD_ERROR_HELPER(bytes_read,"error during packet_analisys");
		usleep(TIME_TO_SLEEP);
	}
}

int packet_analisys(char* buffer, int len){
	
	World world_aux = world;
	ret=sem_wait(world_update_sem);
	PTHREAD_ERROR_HELPER(ret,"errore sulla sem_wait in world_updater");
	int i,new_players=0;
    WorldUpdatePacket* deserialized_wu_packet = (WorldUpdatePacket*)Packet_deserialize(world_buffer, world_buffer_size);
	if(deserialized_wu_packet->header->type != WorldUpdate) return 0;
	// serve?? if(deserialized_wu_packet->header->size != len) return 0; perchè l'udp non ha trasportato tutti i dati?
	ClientUpdate* aux = deserialized_wu_packet->updates;
	while(aux!=NULL){
		for(int i=0;i<WORLDSIZE;i++){
			if (player_world->id_list[i] == aux->id){
				Vehicle_setXYTheta(player_world->vehicles[i],deserialized_wu_packet->updates->x,deserialized_wu_packet->updates->y,deserialized_wu_packet->updates->theta);
				break;
			}
		new_players=1;
		}
	}
	
	if (deserialized_wu_packet->num_vehicles != player_world->players_online || new_players) check_newplayers(deserialized_wu_packet); //aggiunge eventuali nuovi giocatori
	
	
	
	
	
	
}
int sendUpdates(int socket_udp,struct sockaddr_in server_addr,int serverlen){
		//aggiorno la macchinetta e poi invio
		Vehicle_update(vehicle,1); //controllare VARIABILE float dt a che serve
		VehicleUpdatePacket* vehicle_packet = (VehicleUpdatePacket*)malloc(sizeof(VehicleUpdatePacket));
		PacketHeader v_head;
		v_head.type = VehicleUpdate;
		vehicle_packet->header = v_head;
		vehicle_packet->id = my_id;
		vehicle_packet->rotational_force = vehicle->rotational_force;
		vehicle_packet->translational_force = vehicle->translational_force;
		char vehicle_buffer[1000000];
		int vehicle_buffer_size = Packet_serialize(vehicle_buffer, &vehicle_packet->header);
		ret = sendto(s, vehicle_buffer, vehicle_buffer_size , 0 , (struct sockaddr *) &si_other, slen);
		PTHREAD_ERROR_HELPER(ret,"UDP sendto failed"); 
		//----- paccehtto update vehicle udp------
}


void* world_updater(){
	while (server_connected){
		ret=sem_wait(world_update_sem);
		PTHREAD_ERROR_HELPER(ret,"errore sulla sem_wait in world_updater");
		World_update(&world);
		ret=sem_post(world_update_sem);
		PTHREAD_ERROR_HELPER(ret,"errore sulla sem_post in world_updater");
		usleep(100000);
	}
	return;
}


void* serverHandshake (int* my_id, Image** mytexture,Image** map_elevation,Image** map_texture,Image** my_texture_from_server){
	int ret, bytes_sent, bytes_recv;
	char image_packet_buffer[1000000];
	char id_packet_buffer[1000000];

	PacketHeader packet_recv;

    // variables for handling a socket
    struct sockaddr_in server_addr = {0}; // some fields are required to be filled with 0

    // create a socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    ERROR_HELPER(socket_desc, "Could not create socket");

    // set up parameters for the connection
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); // don't forget about network byte order!

    // initiate a connection on the socket
    ret = connect(socket_desc, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
    ERROR_HELPER(ret, "Could not create connection");

    if (DEBUG) fprintf(stderr, "Connection established!\n");
    
    IdPacket* idpack = (IdPacket*)malloc(sizeof(IdPacket));
    
    PacketHeader id_head;
    id_head.type = GetId;
    
    idpack->header = id_head;
    idpack->id = -1;    
    
    bytes_sent = Packet_serialize(id_packet_buffer, &idpack->header);
    
	while ( (ret = send(socket_desc, id_packet_buffer, bytes_sent, 0)) < 0) {
		if (errno == EINTR) continue;
		ERROR_HELPER(-1, "Cannot write to socket");
	}
	
	//azzeramento buffer id_packet_buffer
	
	while ( (bytes_recv = recv(socket_desc, id_packet_buffer, 1000000, 0)) < 0 ) { //ricevo ID dal server
        if (errno == EINTR) continue;
        ERROR_HELPER(-1, "Cannot read from socket");
    }
    
    IdPacket* deserialized_packet = (IdPacket*)Packet_deserialize(id_packet_buffer,bytes_recv);
    
    *my_id = deserialized_packet->id; //scrivo l'id dentro la variabile
    
    Packet_free(&deserialized_packet->header);
	Packet_free(&idpack->header);
	
    ImagePacket* image_packet = (ImagePacket*)malloc(sizeof(ImagePacket));
    PacketHeader im_head;
    im_head.type = PostTexture;
    image_packet->image = *mytexture;
    image_packet->header = im_head;

    int image_packet_buffer_size = Packet_serialize(image_packet_buffer, &image_packet->header);
	while ( (ret = send(socket_desc, image_packet_buffer, image_packet_buffer_size, 0)) < 0) { //invio mytexture al server
		if (errno == EINTR) continue;
		ERROR_HELPER(-1, "Cannot write to socket");
	}
	
	Packet_free(&image_packet->header);
	
	ImagePacket* deserialized_image_packet = (ImagePacket*)malloc(sizeof(ImagePacket));
	
	for (int k=0;k<3,k++){ //ciclo 3 volte per prendere my_texture / map_elevation / map_texture

		
		while ( (bytes_recv = recv(socket_desc, image_packet_buffer, 1000000, 0)) < 0 ) { //ricevo pacchetto dal server
			if (errno == EINTR) continue;
			ERROR_HELPER(-1, "Cannot read from socket");
		}
		
		deserialized_image_packet = (ImagePacket*)Packet_deserialize(image_packet_buffer, bytes_recv);
		
		if (deserialized_image_packet->header->type == PostElevation){
			*map_elevation = deserialized_image_packet->image;
		}
		else if (deserialized_image_packet->header->type == PostTexture){
			if (deserialized_image_packet->id == 0){
				*map_texture = deserialized_image_packet->image;  //scrivo la  map_texture che ho ricevuto dal server nella variabile
			}
			else {
				*my_texture_from_server = deserialized_image_packet->image;  //scrivo la texture che ho ricevuto dal server nella variabile
			}
		}
	}
	
	Packet_free(&deserialized_image_packet->header);
	return;
}

void keyPressed(unsigned char key, int x, int y)
{
  switch(key){
  case 27:
    glutDestroyWindow(window);
    exit(0);
  case ' ':
    vehicle->translational_force_update = 0;
    vehicle->rotational_force_update = 0;
    break;
  case '+':
    viewer.zoom *= 1.1f;
    break;
  case '-':
    viewer.zoom /= 1.1f;
    break;
  case '1':
    viewer.view_type = Inside;
    break;
  case '2':
    viewer.view_type = Outside;
    break;
  case '3':
    viewer.view_type = Global;
    break;
  }
}


void specialInput(int key, int x, int y) {
  switch(key){
  case GLUT_KEY_UP:
    vehicle->translational_force_update += 0.1;
    break;
  case GLUT_KEY_DOWN:
    vehicle->translational_force_update -= 0.1;
    break;
  case GLUT_KEY_LEFT:
    vehicle->rotational_force_update += 0.1;
    break;
  case GLUT_KEY_RIGHT:
    vehicle->rotational_force_update -= 0.1;
    break;
  case GLUT_KEY_PAGE_UP:
    viewer.camera_z+=0.1;
    break;
  case GLUT_KEY_PAGE_DOWN:
    viewer.camera_z-=0.1;
    break;
  }
}


void display(void) {
  WorldViewer_draw(&viewer);
}


void reshape(int width, int height) {
  WorldViewer_reshapeViewport(&viewer, width, height);
}

void idle(void) {
  World_update(&world);
  usleep(30000);
  glutPostRedisplay();
  
  // decay the commands
  vehicle->translational_force_update *= 0.999;
  vehicle->rotational_force_update *= 0.7;
}

int main(int argc, char **argv) {
  if (argc<3) {
    printf("usage: %s <server_address> <player texture>\n", argv[1]);
    exit(-1);
  }

  printf("loading texture image from %s ... ", argv[2]);
  Image* my_texture = Image_load(argv[2]);
  if (my_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }
  
  Image* my_texture_for_server;
  // todo: connect to the server
  //   -get ad id
  //   -send your texture to the server (so that all can see you)
  //   -get an elevation map
  //   -get the texture of the surface

  // these come from the server
  // my_id dichiarata globale
  Image* map_elevation;
  Image* map_texture;
  Image* my_texture_from_server;
  
  //init semaforo world_update
   world_update_sem = malloc(sizeof(sem_t)); // we allocate a sem_t object on the heap

   ret = sem_init(world_update_sem, 0, 1);
   ERROR_HELPER(ret,"error creating semaphore");
  
  connectionHandler(&my_id,&mytexture,&map_elevation,&map_texture,&my_texture_from_server);

  // construct the world
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle=(Vehicle*) malloc(sizeof(Vehicle));
  Vehicle_init(&vehicle, &world, my_id, my_texture_from_server);
  World_addVehicle(&world, v);

  // spawn a thread that will listen the update messages from
  // the server, and sends back the controls
  // the update for yourself are written in the desired_*_force
  // fields of the vehicle variable
  // when the server notifies a new player has joined the game
  // request the texture and add the player to the pool
  /*FILLME*/
  
    //UDP Init
    uint16_t port_number_udp = htons((uint16_t)UDPPORT); // we use network byte order
	int socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
    ERROR_HELPER(socket_desc, "Can't create an UDP socket");
	struct sockaddr_in udp_server = {0}; // some fields are required to be filled with 0
    udp_server.sin_addr.s_addr = ip_addr;
    udp_server.sin_family      = AF_INET;
    udp_server.sin_port        = port_number_udp;
    debug_print("[Main] Socket UDP created and ready to work \n");

    //Create UDP Threads
    pthread_t UDP_sender,UDP_receiver,world_updater;
    udpArgs udp_args;
    udp_args.socket_tcp=socket_desc;
    udp_args.server_addr=udp_server;
    udp_args.socket_udp=socket_udp;
    
    ret = pthread_create(&UDP_sender, NULL, UDP_Sender, udp_args);
    ERROR_HELPER(ret,"can't create UDP_Sender thread");
    
    ret = pthread_create(&UDP_receiver, NULL, UDP_Receiver, udp_args);
    ERROR_HELPER(ret,"can't create UDP_Receiver thread");
    
	ret = pthread_create(&world_updater, NULL, world_updater, NULL);
    ERROR_HELPER(ret,"can't create UDP_Receiver thread");
  
  pthread_t update_thread;
  if (pthread_create(&update_thread, NULL, updating_thread, NULL) != 0) {
	fprintf(stderr, "Can't create a new thread, error %d\n", errno);
	exit(EXIT_FAILURE);
	}

  WorldViewer_runGlobal(&world, vehicle, &argc, argv);

  // cleanup
  World_destroy(&world);
  return 0;             
}
