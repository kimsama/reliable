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

typedef struct 
{
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
  struct sockaddr_in client_addr;
  int have_client;
} ServerContext;

StoredPacket stored_packets[MAX_STORED_PACKETS];
int next_stored_packet = 0;
int server_magazine_count = 30;  // Initial magazine count

static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  *buf = uv_buf_init((char*)malloc(suggested_size), suggested_size);
}

void store_packet(uint16_t sequence, const struct packet* event, double current_time) {
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
  ServerContext* ctx = (ServerContext*)context;
  if (!ctx->have_client) return;

  uv_udp_send_t* req = (uv_udp_send_t*)malloc(sizeof(uv_udp_send_t));
  uv_buf_t buf = uv_buf_init((char*)packet_data, packet_bytes);
  uv_udp_send(req, &ctx->udp_handle, &buf, 1, (const struct sockaddr*)&ctx->client_addr, on_send);
}

void send_packet_event(ServerContext* ctx, const struct packet* event) 
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

void handle_received_event(ServerContext* ctx, const struct packet* event) 
{
  //static uint16_t last_processed_sequence = 0;

  printf("Received event: name %s, id %d, state_01 %d, magazine count %d\n",
    event->name, event->id, event->state_01, event->magazine_count);

  // Check if this is a new event
  //if (event->id > last_processed_sequence) 
  {
    //last_processed_sequence = event->id;
    // Fire event
    if (event->state_01 == 1)
    {
      server_magazine_count--;
      printf("Server magazine count updated: %d\n", server_magazine_count);

      // Echo the event back to the client with the updated magazine count
      struct packet response_event = *event;
      response_event.magazine_count = server_magazine_count;
      send_packet_event(ctx, &response_event);
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

  ServerContext* ctx = (ServerContext*)context;
  struct packet* received_event = (struct packet*)packet_data;
  handle_received_event(ctx, received_event);
  return 1;
}

void check_and_retransmit_packets(ServerContext* ctx, double current_time) 
{
  int num_acks;
  uint16_t* acks = reliable_endpoint_get_acks(ctx->endpoint, &num_acks);

  for (int i = 0; i < num_acks; i++) 
  {
    for (int j = 0; j < MAX_STORED_PACKETS; j++) 
    {
      if (stored_packets[j].sequence == acks[i]) 
      {
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

  for (int i = 0; i < MAX_STORED_PACKETS; i++) 
  {
    if (stored_packets[i].sequence != 0 &&
      current_time - stored_packets[i].send_time > rto) 
    {

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

void on_read(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) 
{
  ServerContext* ctx = (ServerContext*)handle->data;

  if (nread < 0) 
  {
    fprintf(stderr, "Read error %s\n", uv_err_name(nread));
    free(buf->base);
    return;
  }

  if (nread > 0) 
  {
    if (!ctx->have_client) 
    {
      ctx->have_client = 1;
      ctx->client_addr = *(struct sockaddr_in*)addr;
      char ip[17] = { '\0' };
      uv_ip4_name(&ctx->client_addr, ip, sizeof(ip));
      printf("New client connected from %s:%d\n", ip, ntohs(ctx->client_addr.sin_port));
    }
    reliable_endpoint_receive_packet(ctx->endpoint, (uint8_t*)buf->base, nread);
  }

  free(buf->base);
}

int main() 
{
  ServerContext ctx = { 0 };
  ctx.loop = uv_default_loop();
  ctx.running = 1;
  ctx.have_client = 0;

  reliable_init();

  struct reliable_config_t config;
  reliable_default_config(&config);
  config.context = &ctx;
  config.transmit_packet_function = transmit_packet;
  config.process_packet_function = process_packet;

  ctx.endpoint = reliable_endpoint_create(&config, uv_now(ctx.loop) / 1000.0);

  uv_udp_init(ctx.loop, &ctx.udp_handle);
  ctx.udp_handle.data = &ctx;

  uv_ip4_addr("0.0.0.0", SERVER_PORT, &ctx.server_addr);
  int bind_result = uv_udp_bind(&ctx.udp_handle, (const struct sockaddr*)&ctx.server_addr, UV_UDP_REUSEADDR);
  if (bind_result != 0) {
    fprintf(stderr, "Bind error: %s\n", uv_strerror(bind_result));
    return 1;
  }

  uv_udp_recv_start(&ctx.udp_handle, alloc_buffer, on_read);

  printf("Server listening on port %d\n", SERVER_PORT);

  while (ctx.running) 
  {
    uv_run(ctx.loop, UV_RUN_NOWAIT);

    double current_time = uv_now(ctx.loop) / 1000.0;
    reliable_endpoint_update(ctx.endpoint, current_time);
    check_and_retransmit_packets(&ctx, current_time);
    reliable_endpoint_clear_acks(ctx.endpoint);

    // Add a small delay to avoid busy-waiting
    uv_sleep(10);
  }

  // Cleanup code
  reliable_endpoint_destroy(ctx.endpoint);
  uv_close((uv_handle_t*)&ctx.udp_handle, NULL);
  reliable_term();

  return 0;
}
