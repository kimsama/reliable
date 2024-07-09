#include "reliable.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define MAX_BULLETS 30

struct connection_t 
{
  struct reliable_endpoint_t* endpoint;
  int bullets;
};

static struct connection_t client;
static struct connection_t server;

static void transmit_packet(void* context, uint64_t id, uint16_t sequence, uint8_t* packet_data, int packet_bytes) 
{
  (void)id;
  (void)sequence;

  // Simulate 10% packet loss
  if ((rand() % 10) != 0) 
  {
    // Send the packet directly to the other endpoint (normally this would be done via sockets...)
    if (context == &client) 
    {
      reliable_endpoint_receive_packet(server.endpoint, packet_data, packet_bytes);
    }
    else 
    {
      reliable_endpoint_receive_packet(client.endpoint, packet_data, packet_bytes);
    }
  }
}

static int process_packet(void* context, uint64_t id, uint16_t sequence, uint8_t* packet_data, int packet_bytes) 
{
  // Process bullet packet
  if (context == &server) 
  {
    server.bullets--;
    printf("Server received bullet packet. Bullets left: %d\n", server.bullets);

    // Send back ACK with remaining bullets count
    uint8_t ack_packet[8];
    memset(ack_packet, 0, sizeof(ack_packet));
    *(int*)ack_packet = server.bullets; // Include server's bullet count in ACK
    reliable_endpoint_send_packet(server.endpoint, ack_packet, sizeof(ack_packet));
  }
  else if (context == &client) 
  {
    // Process ACK packet
    int server_bullets = *(int*)packet_data;
    if (server_bullets != client.bullets) 
    {
      printf("Mismatch detected: client bullets = %d, server bullets = %d. Resending packet.\n", client.bullets, server_bullets);
      client.bullets = server_bullets;  // Synchronize with server
      // Resend last packet
      uint8_t packet[8];
      memset(packet, 0, sizeof(packet));
      reliable_endpoint_send_packet(client.endpoint, packet, sizeof(packet));
    }
  }

  return 1;
}

int main(int argc, char** argv) 
{
  (void)argc;
  (void)argv;

  printf("\nreliable example\n\n");

  double time = 0.0;

  // Initialize bullets
  client.bullets = MAX_BULLETS;
  server.bullets = MAX_BULLETS;

  // Configure the endpoints
  struct reliable_config_t config;

  reliable_default_config(&config);
  config.max_packet_size = 32 * 1024;
  config.fragment_above = 1200;
  config.max_fragments = 32;
  config.fragment_size = 1024;
  config.transmit_packet_function = transmit_packet;
  config.process_packet_function = process_packet;

  // Create client connection
  config.context = &client;
  reliable_copy_string(config.name, "client", sizeof(config.name));
  client.endpoint = reliable_endpoint_create(&config, time);
  if (client.endpoint == NULL) 
  {
    printf("error: could not create client endpoint\n");
    exit(1);
  }

  // Create server connection
  config.context = &server;
  reliable_copy_string(config.name, "server", sizeof(config.name));
  server.endpoint = reliable_endpoint_create(&config, time);
  if (server.endpoint == NULL) 
  {
    printf("error: could not create server endpoint\n");
    exit(1);
  }

  // Send packets and handle acks
  uint8_t packet[8];
  memset(packet, 0, sizeof(packet));

  for (int i = 0; i < 1000 && client.bullets > 0; i++) 
  {
    uint16_t client_packet_sequence = reliable_endpoint_next_packet_sequence(client.endpoint);

    // Client sends a bullet packet
    reliable_endpoint_send_packet(client.endpoint, packet, sizeof(packet));
    client.bullets--;
    printf("%d: client sent packet %d, bullets left: %d\n", i, client_packet_sequence, client.bullets);

    reliable_endpoint_update(client.endpoint, time);
    reliable_endpoint_update(server.endpoint, time);

    int num_acks;
    uint16_t* acks = reliable_endpoint_get_acks(client.endpoint, &num_acks);
    for (int j = 0; j < num_acks; j++) 
    {
      printf(" --> server acked packet %d\n", acks[j]);

      // Synchronize client bullets with server
      uint8_t ack_packet[8];
      memset(ack_packet, 0, sizeof(ack_packet));
      // Receive the packet
      reliable_endpoint_receive_packet(client.endpoint, ack_packet, sizeof(ack_packet));

      // Process the received packet
      int server_bullets = *(int*)ack_packet;
      if (server_bullets != 0 && server_bullets != client.bullets) // Check if the received data is valid
      {
        printf("Synchronizing bullets: client %d, server %d\n", client.bullets, server_bullets); 
        client.bullets = server_bullets;
      }
    }

    reliable_endpoint_clear_acks(client.endpoint);
    reliable_endpoint_clear_acks(server.endpoint);

    time += 0.01;
  }

  // Clean up
  reliable_endpoint_destroy(client.endpoint);
  reliable_endpoint_destroy(server.endpoint);

  printf("\nSuccess\n\n");

  return 0;
}
