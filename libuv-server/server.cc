#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "reliable.h"
#include "uvcommon/serialize.h"

#define MAX_PACKET_SIZE 1024
#define SERVER_PORT 12345

typedef struct 
{
  uv_udp_t udp_handle;
  struct sockaddr_in client_addr;
  reliable_endpoint_t* endpoint;
  uv_loop_t* loop;
  int running;
} ServerContext;

static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) 
{
  *buf = uv_buf_init((char*)malloc(suggested_size), suggested_size);
}

static void server_transmit_packet(void* context, uint64_t id, uint16_t sequence, uint8_t* packet_data, int packet_bytes) 
{
  ServerContext* ctx = (ServerContext*)context;
  uv_buf_t buf = uv_buf_init((char*)packet_data, packet_bytes);
  uv_udp_try_send(&ctx->udp_handle, &buf, 1, (const struct sockaddr*)&ctx->client_addr);
  printf("Server sent packet: seq=%d, size=%d\n", sequence, packet_bytes);
}

//static int server_process_packet(void* context, uint64_t id, uint16_t sequence, uint8_t* packet_data, int packet_bytes) 
//{
//  printf("Server received: seq=%d, message=%s\n", sequence, packet_data);
//
//  return 1;
//}

static int server_process_packet(void* context, uint64_t id, uint16_t sequence, uint8_t* packet_data, int packet_bytes)
{
  if (packet_bytes == sizeof(struct packet::message)) 
  {
    struct packet::message pkt;
    packet::deserialize(packet_data, &pkt);
    printf("Server received: seq=%d, name=%s, id=%d, state_01=%d, buttonState=%d, thumb_x=%.2f, thumb_y=%.2f\n",
      sequence, pkt.name, pkt.id, pkt.state_01, pkt.buttonState, pkt.thumb_x, pkt.thumb_y);
  }
  else 
  {
    printf("Server received: seq=%d, size=%d (unknown format)\n", sequence, packet_bytes);
  }
  return 1;
}


static void on_read(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
  if (nread > 0) 
  {
    ServerContext* ctx = (ServerContext*)handle->data;
    ctx->client_addr = *(struct sockaddr_in*)addr;
    reliable_endpoint_receive_packet(ctx->endpoint, (uint8_t*)buf->base, nread);
  }

  if (buf->base)
    free(buf->base);
}

int main(int argc, char* argv[])
{
  ServerContext ctx;
  ctx.loop = uv_default_loop();
  ctx.running = 1;

  struct reliable_config_t config;
  reliable_default_config(&config);
  config.transmit_packet_function = server_transmit_packet;
  config.process_packet_function = server_process_packet;
  config.context = &ctx;

  ctx.endpoint = reliable_endpoint_create(&config, uv_now(ctx.loop) / 1000.0);

  uv_udp_init(ctx.loop, &ctx.udp_handle);
  ctx.udp_handle.data = &ctx;

  struct sockaddr_in recv_addr;
  uv_ip4_addr("0.0.0.0", SERVER_PORT, &recv_addr);
  uv_udp_bind(&ctx.udp_handle, (const struct sockaddr*)&recv_addr, UV_UDP_REUSEADDR);
  uv_udp_recv_start(&ctx.udp_handle, alloc_buffer, on_read);

  while (ctx.running) 
  {
    uv_run(ctx.loop, UV_RUN_NOWAIT);

    reliable_endpoint_update(ctx.endpoint, uv_now(ctx.loop) / 1000.0);
      
    int num_acks;
    uint16_t* acks = reliable_endpoint_get_acks(ctx.endpoint, &num_acks);
    for (int i = 0; i < num_acks; i++) {
      printf("Server: Acked packet %d\n", acks[i]);
    }
    reliable_endpoint_clear_acks(ctx.endpoint);

    // 약 60 FPS로 실행
    uv_sleep(16);
  }

  reliable_endpoint_destroy(ctx.endpoint);
  uv_close((uv_handle_t*)&ctx.udp_handle, NULL);

  while (uv_loop_alive(ctx.loop)) 
  {
    uv_run(ctx.loop, UV_RUN_NOWAIT);
  }

  uv_loop_close(ctx.loop);
  return 0;
}