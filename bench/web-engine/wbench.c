/*
 * wbench: engine-level cost probe for the xwallpaper web backend.
 *
 * Loads one synthetic page into a WebKitWebView, then reports load time,
 * time-to-first-animated-frame, sustained fps, CPU seconds and memory
 * (PSS/RSS, summed over the whole process subtree) as a single JSON line.
 *
 * Built twice from the same source:
 *   GTK3 + webkit2gtk-4.1  (what xwallpaperd uses today)
 *   GTK4 + webkitgtk-6.0   (candidate)
 *
 * Not a substitute for measuring xwallpaperd itself (see daemon-cost.sh);
 * this isolates the engine: no per-wallpaper context, no X11 reparenting.
 */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(WBENCH_GTK4)
#include <webkit/webkit.h>
#else
#include <webkit2/webkit2.h>
#endif

enum { MAX_PIDS = 256 };

static GMainLoop *loop;
static GtkWidget *window;
static GtkWidget *view;
static const char *opt_label = "unknown";
static const char *opt_mode = "anim";
static int opt_w, opt_h;
static int opt_warm_ms = 1000;
static int opt_sample_ms = 5000;

static gint64 t_load_start;
static gint64 t_first_frame = -1;
static gint64 t_load_finish = -1;
static gint64 t_sample_start;
static guint64 frames_now;
static guint64 frames_at_start;
static long long cpu_at_start;
static char *wasm_state;
static char *webgl_state;
static double js_ms = -1;

static void finish(const char *err);

static void collect_tree(pid_t root, pid_t *pids, int *n, int max)
{
  if (*n >= max)
    return;
  pids[(*n)++] = root;
  char path[64];
  snprintf(path, sizeof path, "/proc/%d/task/%d/children", (int)root, (int)root);
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  pid_t kids[64];
  int nk = 0;
  while (nk < 64 && fscanf(f, "%d", &kids[nk]) == 1)
    nk++;
  fclose(f);
  for (int i = 0; i < nk; i++)
    collect_tree(kids[i], pids, n, max);
}

static long long read_cpu_ticks(pid_t pid)
{
  char path[64], buf[1024];
  snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
  FILE *f = fopen(path, "r");
  if (!f)
    return 0;
  size_t n = fread(buf, 1, sizeof buf - 1, f);
  buf[n] = 0;
  fclose(f);
  char *p = strrchr(buf, ')');
  if (!p)
    return 0;
  p += 2;
  long long ut = 0, st = 0;
  int field = 3;
  for (char *tok = strtok(p, " "); tok; tok = strtok(NULL, " "), field++) {
    if (field == 14)
      ut = strtoll(tok, NULL, 10);
    else if (field == 15) {
      st = strtoll(tok, NULL, 10);
      break;
    }
  }
  return ut + st;
}

static long long read_pss_kb(pid_t pid)
{
  char path[64], line[256];
  snprintf(path, sizeof path, "/proc/%d/smaps_rollup", (int)pid);
  FILE *f = fopen(path, "r");
  if (!f)
    return 0;
  long long kb = 0;
  while (fgets(line, sizeof line, f)) {
    if (strncmp(line, "Pss:", 4) == 0) {
      kb = strtoll(line + 4, NULL, 10);
      break;
    }
  }
  fclose(f);
  return kb;
}

static long long read_rss_kb(pid_t pid)
{
  char path[64];
  long long pages = 0, resident = 0;
  snprintf(path, sizeof path, "/proc/%d/statm", (int)pid);
  FILE *f = fopen(path, "r");
  if (!f)
    return 0;
  if (fscanf(f, "%lld %lld", &pages, &resident) != 2)
    resident = 0;
  fclose(f);
  return resident * (sysconf(_SC_PAGESIZE) / 1024);
}

static gboolean on_sample_end(gpointer u)
{
  (void)u;
  pid_t pids[MAX_PIDS];
  int n = 0;
  collect_tree(getpid(), pids, &n, MAX_PIDS);
  long long cpu1 = 0, pss = 0, rss = 0;
  for (int i = 0; i < n; i++) {
    cpu1 += read_cpu_ticks(pids[i]);
    pss += read_pss_kb(pids[i]);
    rss += read_rss_kb(pids[i]);
  }
  gint64 t1 = g_get_monotonic_time();
  double span = (t1 - t_sample_start) / 1e6;
  double fps = span > 0 ? (double)(frames_now - frames_at_start) / span : 0;
  double cpu_pct = 100.0 * (double)(cpu1 - cpu_at_start) /
                   (double)sysconf(_SC_CLK_TCK) / span;

  g_print("WENCH {\"label\":\"%s\",\"mode\":\"%s\",\"size\":\"%dx%d\","
          "\"load_ms\":%.1f,\"first_frame_ms\":%.1f,\"fps\":%.1f,"
          "\"cpu_pct\":%.1f,\"procs\":%d,\"pss_mb\":%.1f,\"rss_mb\":%.1f,"
          "\"js_ms\":%.1f,\"wasm\":\"%s\",\"webgl\":\"%s\"}\n",
          opt_label, opt_mode, opt_w, opt_h,
          t_load_finish < 0 ? -1.0 : (double)(t_load_finish - t_load_start) / 1000.0,
          t_first_frame < 0 ? -1.0 : (double)(t_first_frame - t_load_start) / 1000.0,
          fps, cpu_pct, n, (double)pss / 1024.0, (double)rss / 1024.0,
          js_ms, wasm_state ? wasm_state : "n/a", webgl_state ? webgl_state : "n/a");
  fflush(stdout);
  finish(NULL);
  return G_SOURCE_REMOVE;
}

static gboolean on_sample_start(gpointer u)
{
  (void)u;
  pid_t pids[MAX_PIDS];
  int n = 0;
  collect_tree(getpid(), pids, &n, MAX_PIDS);
  cpu_at_start = 0;
  for (int i = 0; i < n; i++)
    cpu_at_start += read_cpu_ticks(pids[i]);
  frames_at_start = frames_now;
  t_sample_start = g_get_monotonic_time();
  g_timeout_add((guint)opt_sample_ms, on_sample_end, NULL);
  return G_SOURCE_REMOVE;
}

/* WebKitJavascriptResult was removed in the 6.0 ABI: the signal hands over a
 * JSCValue directly. Keep one callback shape for both builds. */
#if defined(WBENCH_GTK4)
typedef JSCValue BenchMsg;
#define bench_msg_value(r) (r)
#else
typedef WebKitJavascriptResult BenchMsg;
static JSCValue *bench_msg_value(WebKitJavascriptResult *r)
{
  return webkit_javascript_result_get_js_value(r);
}
#endif

static void on_msg(WebKitUserContentManager *ucm, BenchMsg *r, gpointer u)
{
  (void)ucm;
  (void)u;
  char *s = jsc_value_to_string(bench_msg_value(r));
  if (!s)
    return;
  if (strncmp(s, "f|", 2) == 0) {
    frames_now = strtoull(s + 2, NULL, 10);
    if (t_first_frame < 0)
      t_first_frame = g_get_monotonic_time();
  } else if (strcmp(s, "webgl|ok") == 0) {
    g_free(webgl_state);
    webgl_state = g_strdup("ok");
  } else if (strcmp(s, "webgl|no") == 0) {
    g_free(webgl_state);
    webgl_state = g_strdup("no");
  } else if (strcmp(s, "wasm|ok") == 0) {
    g_free(wasm_state);
    wasm_state = g_strdup("ok");
  } else if (strcmp(s, "wasm|err") == 0) {
    g_free(wasm_state);
    wasm_state = g_strdup("err");
  } else if (strncmp(s, "js|", 3) == 0) {
    js_ms = g_ascii_strtod(s + 3, NULL);
  }
  g_free(s);
}

static void on_load_changed(WebKitWebView *wv, WebKitLoadEvent ev, gpointer u)
{
  (void)wv;
  (void)u;
  if (ev == WEBKIT_LOAD_FINISHED) {
    t_load_finish = g_get_monotonic_time();
    g_timeout_add((guint)opt_warm_ms, on_sample_start, NULL);
  }
}

static gboolean on_watchdog(gpointer u)
{
  (void)u;
  g_printerr("wbench: watchdog fired (page never finished loading?)\n");
  g_print("WENCH {\"label\":\"%s\",\"mode\":\"%s\",\"size\":\"%dx%d\","
          "\"load_ms\":-1,\"first_frame_ms\":-1,\"fps\":-1,\"cpu_pct\":-1,"
          "\"procs\":0,\"pss_mb\":-1,\"rss_mb\":-1,\"js_ms\":-1,"
          "\"wasm\":\"%s\",\"webgl\":\"%s\"}\n",
          opt_label, opt_mode, opt_w, opt_h, wasm_state ? wasm_state : "n/a",
          webgl_state ? webgl_state : "n/a");
  fflush(stdout);
  finish(NULL);
  return G_SOURCE_REMOVE;
}

static void finish(const char *err)
{
  (void)err;
  if (loop)
    g_main_loop_quit(loop);
}

/* --- page bodies ---------------------------------------------------------- */

static const char *kCommonJS =
    "function post(m){try{window.webkit.messageHandlers.bench.postMessage(m);"
    "}catch(e){}}"
    "var N=0;function tick(){N++;post('f|'+N);requestAnimationFrame(tick);}"
    "requestAnimationFrame(tick);"
    "try{new WebAssembly.Module(new Uint8Array([0,97,115,109,1,0,0,0]));"
    "post('wasm|ok');}catch(e){post('wasm|err');}"
    "setTimeout(function(){"
    "var t0=performance.now(),a=new Float64Array(1024),s=0;"
    "for(var i=0;i<3000000;i++){a[i&1023]=i*0.5;s+=a[(i*7)&1023];}"
    "post('js|'+(performance.now()-t0));},800);";

static const char *kCSS =
    "html,body{margin:0;padding:0;width:100%;height:100%;overflow:hidden;"
    "background:#101820}";

static const char *kAnimBody =
    "<style>@keyframes k{from{transform:rotate(0) scale(0.6)}"
    "to{transform:rotate(360deg) scale(1.4)}}"
    ".s{position:absolute;left:40%;top:40%;width:20%;height:20%;"
    "background:linear-gradient(#457b9d,#e63946);animation:k 3s linear infinite}"
    ".t{position:absolute;left:0;top:0;color:#eee;font:28px sans-serif;"
    "animation:k 6s linear infinite}</style>"
    "<div class=s></div><div class=t>Wallpaper Engine style animation panel "
    "with a long line of text to exercise layout and text shaping each frame"
    "</div>";

static const char *kCanvasBody =
    "<canvas id=c></canvas><script>"
    "var c=document.getElementById('c');c.width=1920;c.height=1080;"
    "var x=c.getContext('2d');"
    "function d(){x.fillStyle='#0b1a2a';x.fillRect(0,0,1920,1080);"
    "for(var i=0;i<200;i++){x.fillStyle='hsl('+((i*7)%360)+',70%,55%)';"
    "x.fillRect((i*37+Date.now()/20)%1920,(i*53)%1080,60,60);}"
    "requestAnimationFrame(d);}d();</script>";

static const char *kWebglBody =
    "<canvas id=c width=1920 height=1080></canvas><script>"
    "var c=document.getElementById('c');"
    "var gl=c.getContext('webgl2')||c.getContext('webgl');"
    "if(!gl){post('webgl|no');}else{post('webgl|ok');"
    "var vs=gl.createShader(gl.VERTEX_SHADER);"
    "gl.shaderSource(vs,'attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}');"
    "gl.compileShader(vs);"
    "var fs=gl.createShader(gl.FRAGMENT_SHADER);"
    "gl.shaderSource(fs,'precision highp float;uniform float t;"
    "void main(){vec2 u=gl_FragCoord.xy/1080.;"
    "gl_FragColor=vec4(sin(u.x*20.+t),cos(u.y*20.+t),sin(t),1.);}');"
    "gl.compileShader(fs);"
    "var pr=gl.createProgram();gl.attachShader(pr,vs);gl.attachShader(pr,fs);"
    "gl.linkProgram(pr);gl.useProgram(pr);"
    "var b=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,b);"
    "gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,1,1]),"
    "gl.STATIC_DRAW);"
    "var loc=gl.getAttribLocation(pr,'p');gl.enableVertexAttribArray(loc);"
    "gl.vertexAttribPointer(loc,2,gl.FLOAT,false,0,0);"
    "var tl=gl.getUniformLocation(pr,'t');"
    "function f(){gl.uniform1f(tl,Date.now()/1000);"
    "gl.drawArrays(gl.TRIANGLE_STRIP,0,4);requestAnimationFrame(f);}f();}"
    "</script>";

static const char *kJsBody = "<div>js only</div>";

static char *build_html(const char *video_b64)
{
  const char *body = kAnimBody;
  if (strcmp(opt_mode, "canvas2d") == 0)
    body = kCanvasBody;
  else if (strcmp(opt_mode, "webgl") == 0)
    body = kWebglBody;
  else if (strcmp(opt_mode, "js") == 0)
    body = kJsBody;
  else if (strcmp(opt_mode, "blank") == 0)
    body = "";

  GString *h = g_string_new("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>");
  g_string_append(h, kCSS);
  g_string_append(h, "</style><script>");
  g_string_append(h, kCommonJS);
  g_string_append(h, "</script></head><body>");
  if (strcmp(opt_mode, "video") == 0 && video_b64) {
    g_string_append(h,
        "<video autoplay muted loop playsinline style=\"width:100%;height:100%;"
        "object-fit:cover\" src=\"data:video/mp4;base64,");
    g_string_append(h, video_b64);
    g_string_append(h, "\"></video>");
  } else {
    g_string_append(h, body);
  }
  g_string_append(h, "</body></html>");
  return g_string_free(h, FALSE);
}

int main(int argc, char **argv)
{
  const char *video_path = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc)
      opt_mode = argv[++i];
    else if (strcmp(argv[i], "--label") == 0 && i + 1 < argc)
      opt_label = argv[++i];
    else if (strcmp(argv[i], "--video") == 0 && i + 1 < argc)
      video_path = argv[++i];
    else if (strcmp(argv[i], "--warm") == 0 && i + 1 < argc)
      opt_warm_ms = atoi(argv[++i]);
    else if (strcmp(argv[i], "--sample") == 0 && i + 1 < argc)
      opt_sample_ms = atoi(argv[++i]);
    else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc &&
             sscanf(argv[++i], "%dx%d", &opt_w, &opt_h) == 2) {
    } else {
      g_printerr("usage: %s --label L --mode blank|anim|canvas2d|webgl|js|video "
                 "[--video file.mp4] [--size WxH] [--warm ms] [--sample ms]\n",
                 argv[0]);
      return 2;
    }
  }
  if (opt_w <= 0 || opt_h <= 0) {
    opt_w = 1920;
    opt_h = 1080;
  }

  char *video_b64 = NULL;
  if (strcmp(opt_mode, "video") == 0) {
    if (!video_path) {
      g_printerr("wbench: --mode video needs --video\n");
      return 2;
    }
    gchar *raw = NULL;
    gsize len = 0;
    if (!g_file_get_contents(video_path, &raw, &len, NULL)) {
      g_printerr("wbench: cannot read %s\n", video_path);
      return 2;
    }
    video_b64 = g_base64_encode((const guchar *)raw, len);
    g_free(raw);
  }

#if defined(WBENCH_GTK4)
  gtk_init();
#else
  gtk_init(&argc, &argv);
#endif

#if defined(WBENCH_GTK4)
  window = gtk_window_new();
#else
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif
  gtk_window_set_default_size(GTK_WINDOW(window), opt_w, opt_h);
  gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

  view = webkit_web_view_new();
  WebKitUserContentManager *ucm = webkit_web_view_get_user_content_manager(
      WEBKIT_WEB_VIEW(view));
  g_signal_connect(ucm, "script-message-received::bench",
                   G_CALLBACK(on_msg), NULL);
  g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), NULL);
#if defined(WBENCH_GTK4)
  webkit_user_content_manager_register_script_message_handler(ucm, "bench",
                                                              NULL);
#else
  webkit_user_content_manager_register_script_message_handler(ucm, "bench");
#endif

#if defined(WBENCH_GTK4)
  gtk_window_set_child(GTK_WINDOW(window), view);
  gtk_window_present(GTK_WINDOW(window));
#else
  gtk_container_add(GTK_CONTAINER(window), view);
  gtk_widget_show_all(window);
#endif

  loop = g_main_loop_new(NULL, FALSE);
  g_timeout_add(45000, on_watchdog, NULL);

  char *html = build_html(video_b64);
  t_load_start = g_get_monotonic_time();
  webkit_web_view_load_html(WEBKIT_WEB_VIEW(view), html, "http://bench.invalid/");
  g_free(html);
  g_free(video_b64);

  g_main_loop_run(loop);
  g_main_loop_unref(loop);
  g_free(wasm_state);
  g_free(webgl_state);
  return 0;
}
