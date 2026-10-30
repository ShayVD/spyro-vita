/* Interactive play for the headless linux32 host (decisions D22): with
 * --serve PORT, vblanks run at real time (60 Hz) and a small HTTP server
 * shows the picture in any browser and takes its keys or gamepad:
 *   GET /                  the player page (below)
 *   GET /frame?after=N     waits for a picture newer than N; body = RGB
 *                          bytes, headers X-Frame, X-Width, X-Height
 *   GET /pad?b=HEX         buttons held, active-high (pad.c's bits)
 *   GET /audio?after=N     PCM from the SPU after sample N (audio.c)
 * No dependencies: plain sockets and pthreads. Bound to 0.0.0.0 inside the
 * container; tools/tc publishes it on the Mac's localhost only. */
#include "host.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_new_frame = PTHREAD_COND_INITIALIZER;
static uint8_t *s_rgb;
static int s_w, s_h;
static unsigned s_frame;
static int s_serving;
static struct timespec s_t0;

static const char k_page[] =
    "<!doctype html><html><head><meta charset=utf-8><title>Spyro host</title>"
    "<style>body{background:#111;color:#ccc;font:14px sans-serif;margin:16px}"
    "canvas{width:1024px;height:480px;image-rendering:pixelated;"
    "background:#000;max-width:100%}</style></head><body>"
    "<canvas id=c width=512 height=240></canvas>"
    "<p>Arrows: D-pad &middot; X: cross &middot; Z: square &middot; C: circle "
    "&middot; S: triangle &middot; Enter: start &middot; Backspace: select "
    "&middot; Q/E: L1/R1 &middot; 1/3: L2/R2 &middot; a gamepad works too. "
    "<button id=snd>Sound on</button> <span id=st></span></p><script>\n"
    "const cv=document.getElementById('c'),cx=cv.getContext('2d');\n"
    "let frame=0,keys=0,pad=0,last=-1;\n"
    "const K={ArrowUp:0x10,ArrowRight:0x20,ArrowDown:0x40,ArrowLeft:0x80,"
    "KeyX:0x4000,KeyZ:0x8000,KeyC:0x2000,KeyS:0x1000,Enter:8,Backspace:1,"
    "KeyQ:0x400,KeyE:0x800,Digit1:0x100,Digit3:0x200};\n"
    "const G=[0x4000,0x2000,0x8000,0x1000,0x400,0x800,0x100,0x200,1,8,2,4,"
    "0x10,0x40,0x80,0x20];\n"
    "function send(){const b=keys|pad;if(b!==last){last=b;"
    "fetch('/pad?b='+b.toString(16));}}\n"
    "onkeydown=e=>{if(K[e.code]){keys|=K[e.code];send();e.preventDefault();}};\n"
    "onkeyup=e=>{if(K[e.code]){keys&=~K[e.code];send();e.preventDefault();}};\n"
    "onblur=()=>{keys=0;send();};\n"
    "function poll(){pad=0;for(const g of navigator.getGamepads?"
    "navigator.getGamepads():[]){if(!g)continue;g.buttons.forEach((x,i)=>{"
    "if(x.pressed&&G[i])pad|=G[i];});const ax=g.axes;if(ax.length>1){"
    "if(ax[0]<-.5)pad|=0x80;if(ax[0]>.5)pad|=0x20;if(ax[1]<-.5)pad|=0x10;"
    "if(ax[1]>.5)pad|=0x40;}}send();requestAnimationFrame(poll);}poll();\n"
    "async function frames(){for(;;){try{const r=await fetch('/frame?after='"
    "+frame);if(r.status!==200)continue;frame=+r.headers.get('X-Frame');"
    "const w=+r.headers.get('X-Width'),h=+r.headers.get('X-Height');"
    "const d=new Uint8Array(await r.arrayBuffer());if(cv.width!==w||"
    "cv.height!==h){cv.width=w;cv.height=h;}const im=cx.createImageData(w,h);"
    "for(let i=0,j=0;i<w*h*3;i+=3,j+=4){im.data[j]=d[i];im.data[j+1]=d[i+1];"
    "im.data[j+2]=d[i+2];im.data[j+3]=255;}cx.putImageData(im,0,0);}catch(e)"
    "{document.getElementById('st').textContent='disconnected';await new "
    "Promise(r=>setTimeout(r,1000));}}}frames();\n"
    /* Audio: 44.1 kHz s16 stereo chunks queued on a WebAudio clock. */
    "let ac=null,anext=0,apos=-1;\n"
    "async function audio(){for(;;){const r=await fetch('/audio?after='+apos);"
    "if(r.status!==200){await new Promise(r=>setTimeout(r,50));continue;}"
    "apos=+r.headers.get('X-Pos');const s=new Int16Array(await "
    "r.arrayBuffer());const n=s.length/2;if(!n)continue;const b=ac."
    "createBuffer(2,n,44100),L=b.getChannelData(0),R=b.getChannelData(1);"
    "for(let i=0;i<n;i++){L[i]=s[2*i]/32768;R[i]=s[2*i+1]/32768;}const src="
    "ac.createBufferSource();src.buffer=b;src.connect(ac.destination);"
    "if(anext<ac.currentTime+.03||anext>ac.currentTime+.3)anext=ac."
    "currentTime+.06;src.start(anext);anext+=n/44100;}}\n"
    "document.getElementById('snd').onclick=e=>{if(!ac){ac=new AudioContext();"
    "audio();}e.target.disabled=true;};\n"
    "</script></body></html>\n";

static int write_all(int fd, const void *buf, size_t n) {
  const char *p = buf;
  while (n > 0) {
    ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
    if (k < 0 && errno == EINTR)
      continue;
    if (k <= 0)
      return -1;
    p += k;
    n -= (size_t)k;
  }
  return 0;
}

static int reply(int fd, const char *status, const char *type,
                 const char *extra, const void *body, size_t n) {
  char hdr[512];
  int k = snprintf(hdr, sizeof hdr,
                   "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                   "Cache-Control: no-store\r\n%s\r\n",
                   status, type, n, extra ? extra : "");
  if (write_all(fd, hdr, (size_t)k) != 0)
    return -1;
  return n ? write_all(fd, body, n) : 0;
}

static unsigned query_num(const char *path, const char *key, int base) {
  const char *q = strstr(path, key);
  return q ? (unsigned)strtoul(q + strlen(key), NULL, base) : 0;
}

static int serve_frame(int fd, const char *path) {
  const char *after = strstr(path, "after=");
  unsigned want = after ? (unsigned)strtoul(after + 6, NULL, 10) : 0;
  struct timespec until;
  uint8_t *copy = NULL;
  int w = 0, h = 0, r;
  unsigned frame;
  char extra[128];
  clock_gettime(CLOCK_REALTIME, &until);
  until.tv_nsec += 250 * 1000000;
  if (until.tv_nsec >= 1000000000)
    until.tv_sec++, until.tv_nsec -= 1000000000;
  pthread_mutex_lock(&s_lock);
  while (s_frame <= want && after &&
         pthread_cond_timedwait(&s_new_frame, &s_lock, &until) == 0)
    ;
  frame = s_frame;
  if (s_rgb && frame > want) {
    w = s_w, h = s_h;
    copy = malloc((size_t)w * h * 3);
    memcpy(copy, s_rgb, (size_t)w * h * 3);
  }
  pthread_mutex_unlock(&s_lock);
  if (!copy)
    return reply(fd, "204 No Content", "text/plain", NULL, NULL, 0);
  snprintf(extra, sizeof extra, "X-Frame: %u\r\nX-Width: %d\r\nX-Height: %d\r\n",
           frame, w, h);
  r = reply(fd, "200 OK", "application/octet-stream", extra, copy,
            (size_t)w * h * 3);
  free(copy);
  return r;
}

static int serve_audio(int fd, const char *path) {
  const char *after = strstr(path, "after=");
  long long pos = after ? strtoll(after + 6, NULL, 10) : -1;
  int16_t *pcm = NULL;
  size_t frames = 0;
  long long end = host_audio_take(pos, &pcm, &frames);
  char extra[64];
  int r;
  if (end < 0)
    return reply(fd, "204 No Content", "text/plain", NULL, NULL, 0);
  snprintf(extra, sizeof extra, "X-Pos: %lld\r\n", end);
  r = reply(fd, "200 OK", "application/octet-stream", extra, pcm,
            frames * 4);
  free(pcm);
  return r;
}

static void *conn_thread(void *arg) {
  int fd = (int)(intptr_t)arg, one = 1;
  char req[2048];
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  for (;;) { /* keep-alive: one request at a time */
    size_t n = 0;
    char method[8], path[512];
    while (n < sizeof req - 1) {
      ssize_t k = recv(fd, req + n, sizeof req - 1 - n, 0);
      if (k <= 0)
        goto done;
      n += (size_t)k;
      req[n] = 0;
      if (strstr(req, "\r\n\r\n"))
        break;
    }
    if (sscanf(req, "%7s %511s", method, path) != 2)
      break;
    if (strcmp(path, "/") == 0) {
      if (reply(fd, "200 OK", "text/html", NULL, k_page, sizeof k_page - 1))
        break;
    } else if (strncmp(path, "/frame", 6) == 0) {
      if (serve_frame(fd, path))
        break;
    } else if (strncmp(path, "/pad", 4) == 0) {
      host_pad_set_live((uint16_t)query_num(path, "b=", 16));
      if (reply(fd, "204 No Content", "text/plain", NULL, NULL, 0))
        break;
    } else if (strncmp(path, "/audio", 6) == 0) {
      if (serve_audio(fd, path))
        break;
    } else if (reply(fd, "404 Not Found", "text/plain", NULL, "no\n", 3)) {
      break;
    }
  }
done:
  close(fd);
  return NULL;
}

static void *accept_thread(void *arg) {
  int ls = (int)(intptr_t)arg;
  for (;;) {
    pthread_t t;
    int fd = accept(ls, NULL, NULL);
    if (fd < 0) {
      if (errno == EINTR)
        continue;
      host_log("serve: accept failed (%d)", errno);
      return NULL;
    }
    if (pthread_create(&t, NULL, conn_thread, (void *)(intptr_t)fd) == 0)
      pthread_detach(t);
    else
      close(fd);
  }
}

int host_serve_start(int port) {
  struct sockaddr_in a;
  pthread_t t;
  int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
  if (ls < 0)
    return -1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(ls, (struct sockaddr *)&a, sizeof a) != 0 || listen(ls, 8) != 0) {
    host_log("serve: cannot listen on port %d (%d)", port, errno);
    close(ls);
    return -1;
  }
  if (pthread_create(&t, NULL, accept_thread, (void *)(intptr_t)ls) != 0)
    return -1;
  pthread_detach(t);
  clock_gettime(CLOCK_MONOTONIC, &s_t0);
  s_serving = 1;
  host_log("serve: http://localhost:%d/", port);
  return 0;
}

/* Once per vblank: publish the picture, then sleep until this vblank's
 * real-time deadline (vblank v ends at t0 + v/60 s). */
void host_serve_vblank(int vblank) {
  struct timespec now, due;
  long long ns;
  int w, h;
  uint8_t *rgb;
  if (!s_serving)
    return;
  rgb = host_display_rgb(&w, &h);
  pthread_mutex_lock(&s_lock);
  free(s_rgb);
  s_rgb = rgb, s_w = w, s_h = h;
  s_frame++;
  pthread_cond_broadcast(&s_new_frame);
  pthread_mutex_unlock(&s_lock);

  ns = (long long)vblank * 1000000000LL / 60;
  due.tv_sec = s_t0.tv_sec + (time_t)(ns / 1000000000LL);
  due.tv_nsec = s_t0.tv_nsec + (long)(ns % 1000000000LL);
  if (due.tv_nsec >= 1000000000)
    due.tv_sec++, due.tv_nsec -= 1000000000;
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (now.tv_sec > due.tv_sec + 1) { /* fell behind: don't try to catch up */
    s_t0 = now;
    s_t0.tv_sec -= (time_t)(ns / 1000000000LL);
    return;
  }
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &due, NULL) == EINTR)
    ;
}
