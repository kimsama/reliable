#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "reliable.h"

#define MAX_PACKET_SIZE 1024
#define MAX_STORED_PACKETS 256
#define SERVER_PORT 8000

struct packet
{
  char name[16];
  unsigned short id;
  char state_01;              // 1: fire 
  char state_02;              
  unsigned short buttonState; 
  float thumb_x;              
  float thumb_y;              
  int magazine_count;         // Added to synchronize magazine count
};

typedef struct {
  uint16_t sequence;
  struct packet event;
  double send_time;
  int retransmit_count;
} StoredPacket;

typedef struct
{
  uv_udp_t udp_handle;
  struct sockaddr_in server_addr;
  reliable_endpoint_t* endpoint;
  uv_loop_t* loop;
  int running;
} ClientContext;

StoredPacket stored_packets[MAX_STORED_PACKETS];
int next_stored_packet = 0;
int local_magazine_count = 30;  // Initial magazine count

void store_packet(uint16_t sequence, const struct packet* event, double current_time) 
{
  int index = next_stored_packet % MAX_STORED_PACKETS;
  stored_packets[index].sequence = sequence;
  stored_packets[index].event = *event;
  stored_packets[index].send_time = current_time;
  stored_packets[index].retransmit_count = 0;
  next_stored_packet++;
}

void on_send(uv_udp_send_t* req, int status) 
{
  if (status) 
  {
    fprintf(stderr, "Send error %s\n", uv_strerror(status));
  }
  free(req);
}

void transmit_packet(void* context, uint64_t id, uint16_t sequence, uint8_t* packet_data, int packet_bytes) 
{
  ClientContext* ctx = (ClientContext*)context;
  uv_udp_send_t* req = (uv_udp_send_t*)malloc(sizeof(uv_udp_send_t));
  uv_buf_t buf = uv_buf_init((char*)packet_data, packet_bytes);
  uv_udp_send(req, &ctx->udp_handle, &buf, 1, (const struct sockaddr*)&ctx->server_addr, on_send);
}

void send_packet_event(ClientContext* ctx, const struct packet* event) 
{
  uint16_t sequence = reliable_endpoint_next_packet_sequence(ctx->endpoint);
  uint8_t packet_data[sizeof(struct packet)];
  memcpy(packet_data, event, sizeof(struct packet));

  double current_time = uv_now(ctx->loop) / 1000.0;
  store_packet(sequence, event, current_time);

  reliable_endpoint_send_packet(ctx->endpoint, packet_data, sizeof(struct packet));
  printf("Sent event: sequence %d, name %s, id %d, state_01 %d, magazine count %d\n",
    sequence, event->name, event->id, event->state_01, event->magazine_count);
}

void handle_received_event(const struct packet* event) 
{
  static uint16_t last_processed_sequence = 0;

  printf("Received event: name %s, id %d, state_01 %d, magazine count %d\n",
    event->name, event->id, event->state_01, event->magazine_count);

  // Check if this is a new event
  //if (event->id > last_processed_sequence)
  {
    //last_processed_sequence = event->id;

    // Fire event
    if (event->state_01 == 1)
    {
      if (event->magazine_count != local_magazine_count)
      {
        printf("Synchronizing local magazine count: %d -> %d\n",
          local_magazine_count, event->magazine_count);
        local_magazine_count = event->magazine_count;
      }
    }
  }
  //else
  //{
  //  printf("Ignoring out-of-order or duplicate event\n");
  //}
}

int process_packet(void* context, uint64_t id, uint16_t sequence, uint8_t* packet_data, int packet_bytes) 
{
  if (packet_bytes != sizeof(struct packet)) 
  {
    printf("Received invalid packet size\n");
    return 0;
  }

  struct packet* received_event = (struct packet*)packet_data;
  handle_received_event(received_event);
  return 1;
}

void check_and_retransmit_packets(ClientContext* ctx, double current_time) 
{
  int num_acks;
  uint16_t* acks = reliable_endpoint_get_acks(ctx->endpoint, &num_acks);

  //printf("Received %d acks\n", num_acks);
  for (int i = 0; i < num_acks; i++) 
  {
    printf("Ack received for sequence: %d\n", acks[i]);
    for (int j = 0; j < MAX_STORED_PACKETS; j++) 
    {
      // See whether this packet has been successfully received by the server.
      if (stored_packets[j].sequence == acks[i]) 
      {
        printf("Marking packet %d as acknowledged\n", acks[i]);
        // Mark as acknowledged
        stored_packets[j].sequence = 0;  
        break;
      }
    }
  }

  float rtt = reliable_endpoint_rtt(ctx->endpoint);
  float packet_loss = reliable_endpoint_packet_loss(ctx->endpoint);
  //double rto = fmax(rtt * 2.0 * (1.0 + packet_loss), 0.003);
  double rto = rtt * 2.0 * (1.0 + packet_loss);

  //printf("Current RTT: %f, Packet Loss: %f, RTO: %f\n", rtt, packet_loss, rto);

  for (int i = 0; i < MAX_STORED_PACKETS; i++) 
  {
    if (stored_packets[i].sequence != 0 &&
      current_time - stored_packets[i].send_time > rto) 
    {

      printf("Packet %d has been waiting for %f seconds\n",
        stored_packets[i].sequence,
        current_time - stored_packets[i].send_time);

      uint8_t packet_data[sizeof(struct packet)];
      memcpy(packet_data, &stored_packets[i].event, sizeof(struct packet));
      reliable_endpoint_send_packet(ctx->endpoint, packet_data, sizeof(struct packet));

      stored_packets[i].send_time = current_time;
      stored_packets[i].retransmit_count++;

      printf("Retransmitting packet: sequence %d, name %s, id %d, state_01 %d, magazine count %d (attempt %d)\n",
        stored_packets[i].sequence,
        stored_packets[i].event.name,
        stored_packets[i].event.id,
        stored_packets[i].event.state_01,
        stored_packets[i].event.magazine_count,
        stored_packets[i].retransmit_count);
    }
  }
}

void fire_weapon(ClientContext* ctx) 
{
  if (local_magazine_count > 0) 
  {
    local_magazine_count--;  // Decrement local magazine count

    struct packet event;
    strncpy_s(event.name, sizeof(event.name), "weapon_type", _TRUNCATE);
    event.id = 1;  
    event.state_01 = 1;     // Fire state
    event.state_02 = 0;     
    event.buttonState = 0;  
    event.thumb_x = 0.0f;
    event.thumb_y = 0.0f;

    event.magazine_count = local_magazine_count;  // Send current magazine count

    send_packet_event(ctx, &event);

    printf("Local magazine count after firing: %d\n", local_magazine_count);
  }
  else 
  {
    printf("Cannot fire, magazine is empty\n");
    // Stop sending when out of ammo
    ctx->running = 0;  
  }
}

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) 
{
  *buf = uv_buf_init((char*)malloc(suggested_size), suggested_size);
}

void on_read(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) 
{
  if (nread < 0) 
  {
    fprintf(stderr, "Read error %s\n", uv_err_name(nread));
    free(buf->base);
    return;
  }

  if (nread > 0) 
  {
    ClientContext* ctx = (ClientContext*)handle->data;
    reliable_endpoint_receive_packet(ctx->endpoint, (uint8_t*)buf->base, nread);
  }

  free(buf->base);
}

int main() 
{
  ClientContext ctx;
  ctx.loop = uv_default_loop();
  ctx.running = 1;

  reliable_init();

  struct reliable_config_t config;
  reliable_default_config(&config);
  config.context = &ctx;
  config.transmit_packet_function = transmit_packet;
  config.process_packet_function = process_packet;

  ctx.endpoint = reliable_endpoint_create(&config, uv_now(ctx.loop) / 1000.0);

  uv_udp_init(ctx.loop, &ctx.udp_handle);
  ctx.udp_handle.data = &ctx;

  uv_ip4_addr("127.0.0.1", SERVER_PORT, &ctx.server_addr);
  uv_udp_recv_start(&ctx.udp_handle, alloc_buffer, on_read);

  //int counter = 0;
  while (ctx.running) 
  {
    uv_run(ctx.loop, UV_RUN_NOWAIT);

    double current_time = uv_now(ctx.loop) / 1000.0;
    reliable_endpoint_update(ctx.endpoint, current_time);
    check_and_retransmit_packets(&ctx, current_time);
    reliable_endpoint_clear_acks(ctx.endpoint);

    // Simulate firing every few updates
    //if (++counter % 10 == 0) 
    //{
      fire_weapon(&ctx);
    //}

    // Add a small delay to avoid busy-waiting
    uv_sleep(50);
  }

  printf("Out of ammo. Press Enter to exit...\n");
  getchar();

  // Cleanup code
  reliable_endpoint_destroy(ctx.endpoint);
  uv_close((uv_handle_t*)&ctx.udp_handle, NULL);
  reliable_term();

  return 0;
}