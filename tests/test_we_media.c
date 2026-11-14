/* Unit tests: WE media payloads, MPRIS metadata parsing, artwork resolution.
 * No session bus, no X, no GTK. */

#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "harness.h"
#include "../src/we_media.h"

static GVariant *metadata_full(GVariant *artist, GVariant *length,
                               GVariant *album_artist) {
  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&b, "{sv}", "xesam:title",
                        g_variant_new_string("Song"));
  g_variant_builder_add(&b, "{sv}", "xesam:artist", artist);
  g_variant_builder_add(&b, "{sv}", "xesam:album",
                        g_variant_new_string("Album"));
  g_variant_builder_add(&b, "{sv}", "xesam:subtitle",
                        g_variant_new_string("Live at X"));
  g_variant_builder_add(&b, "{sv}", "xesam:genre",
                        g_variant_new_strv((const char *[]){"Rock", "Indie", "",
                                                           NULL},
                                           -1));
  g_variant_builder_add(&b, "{sv}", "xesam:albumArtist", album_artist);
  g_variant_builder_add(&b, "{sv}", "mpris:artUrl",
                        g_variant_new_string("file:///tmp/cover.png"));
  g_variant_builder_add(&b, "{sv}", "mpris:length", length);
  g_variant_builder_add(&b, "{sv}", "xesam:url",
                        g_variant_new_string("https://example.test/video.mp4"));
  return g_variant_builder_end(&b);
}

static void test_json_escape(void) {
  TEST_BEGIN("JSON escaping covers quotes, slashes and controls");
  char out[128];
  TEST_CHECK_EQ_INT(xw_we_media_json_escape("a\"b\\c\n\t中", out, sizeof(out)),
                    0);
  TEST_CHECK_EQ_STR(out, "a\\\"b\\\\c\\n\\t中");

  char tiny[4];
  TEST_CHECK_EQ_INT(xw_we_media_json_escape("abcdef", tiny, sizeof(tiny)), -1);
}

static void test_format_payloads(void) {
  TEST_BEGIN("WE media payloads are valid JSON fragments");
  XWMediaInfo m;
  char out[1024];
  memset(&m, 0, sizeof(m));
  snprintf(m.title, sizeof(m.title), "Song \"One\"");
  snprintf(m.artist, sizeof(m.artist), "歌手");
  snprintf(m.album, sizeof(m.album), "Album");
  snprintf(m.subtitle, sizeof(m.subtitle), "Sub \"Live\"");
  snprintf(m.genres, sizeof(m.genres), "Rock, Pop");
  snprintf(m.album_artist, sizeof(m.album_artist), "AA");
  snprintf(m.content_type, sizeof(m.content_type), "music");
  m.state = XW_WE_MEDIA_PLAYING;
  m.position = 12.5;
  m.duration = 180.0;
  m.enabled = 1;
  m.thumbnail = "data:image/png;base64,AAAA";

  TEST_CHECK_EQ_INT(xw_we_media_format_props(&m, out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out,
                    "{\"title\":\"Song \\\"One\\\"\",\"artist\":\"歌手\","
                    "\"albumTitle\":\"Album\",\"contentType\":\"music\","
                    "\"subtitle\":\"Sub \\\"Live\\\"\",\"genres\":\"Rock, Pop\","
                    "\"albumArtist\":\"AA\"}");

  snprintf(m.color_primary, sizeof(m.color_primary), "#112233");
  snprintf(m.color_secondary, sizeof(m.color_secondary), "#445566");
  snprintf(m.color_tertiary, sizeof(m.color_tertiary), "#778899");
  snprintf(m.color_text, sizeof(m.color_text), "#ffffff");
  snprintf(m.color_high, sizeof(m.color_high), "#000000");
  TEST_CHECK_EQ_INT(xw_we_media_format_thumb(&m, out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "{\"thumbnail\":\"data:image/png;base64,AAAA\","
                         "\"primaryColor\":\"#112233\","
                         "\"secondaryColor\":\"#445566\","
                         "\"tertiaryColor\":\"#778899\","
                         "\"textColor\":\"#ffffff\","
                         "\"highContrastColor\":\"#000000\"}");
  m.color_primary[0] = '\0';
  m.color_secondary[0] = '\0';
  m.color_tertiary[0] = '\0';
  m.color_text[0] = '\0';
  m.color_high[0] = '\0';

  TEST_CHECK_EQ_INT(xw_we_media_format_playback(&m, out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "{\"state\":0}");

  TEST_CHECK_EQ_INT(xw_we_media_format_timeline(&m, out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "{\"position\":12.500,\"duration\":180.000}");

  TEST_CHECK_EQ_INT(xw_we_media_format_status(&m, out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "{\"enabled\":true}");

  m.enabled = 0;
  TEST_CHECK_EQ_INT(xw_we_media_format_status(&m, out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "{\"enabled\":false}");

  m.thumbnail = NULL;
  TEST_CHECK_EQ_INT(xw_we_media_format_thumb(&m, out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "{\"thumbnail\":\"\",\"primaryColor\":\"\","
                         "\"secondaryColor\":\"\",\"tertiaryColor\":\"\","
                         "\"textColor\":\"\",\"highContrastColor\":\"\"}");

  m.thumbnail = "data:image/png;base64,AA\"BB";
  TEST_CHECK_EQ_INT(xw_we_media_format_thumb(&m, out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "{\"thumbnail\":\"\",\"primaryColor\":\"\","
                         "\"secondaryColor\":\"\",\"tertiaryColor\":\"\","
                         "\"textColor\":\"\",\"highContrastColor\":\"\"}");

  TEST_BEGIN("props formatter handles worst-case escaped text");
  char big[6 * XW_WE_MEDIA_TEXT_MAX * 6 + 512];
  memset(&m, 0, sizeof(m));
  memset(m.title, 1, sizeof(m.title) - 1);
  memset(m.artist, 1, sizeof(m.artist) - 1);
  memset(m.album, 1, sizeof(m.album) - 1);
  memset(m.subtitle, 1, sizeof(m.subtitle) - 1);
  memset(m.genres, 1, sizeof(m.genres) - 1);
  memset(m.album_artist, 1, sizeof(m.album_artist) - 1);
  snprintf(m.content_type, sizeof(m.content_type), "music");
  TEST_CHECK_EQ_INT(xw_we_media_format_props(&m, big, sizeof(big)), 0);
  TEST_CHECK(strstr(big, "\\u0001") != NULL);
}

static void test_parse_metadata(void) {
  TEST_BEGIN("MPRIS metadata maps title/artist/album/art/length");
  const char *artists[] = {"A", "B", NULL};
  const char *album_artists[] = {"AA", "BB", NULL};
  GVariant *meta = metadata_full(
      g_variant_new_strv(artists, -1), g_variant_new_uint64(252186996),
      g_variant_new_strv(album_artists, -1));
  XWMediaInfo m;
  char out[4096];
  memset(&m, 0, sizeof(m));
  TEST_CHECK_EQ_INT(xw_we_media_parse_metadata(meta, &m), 0);
  TEST_CHECK_EQ_STR(m.title, "Song");
  TEST_CHECK_EQ_STR(m.artist, "A, B");
  TEST_CHECK_EQ_STR(m.album, "Album");
  TEST_CHECK_EQ_STR(m.subtitle, "Live at X");
  TEST_CHECK_EQ_STR(m.genres, "Rock, Indie");
  TEST_CHECK_EQ_STR(m.album_artist, "AA, BB");
  TEST_CHECK_EQ_STR(m.art_url, "file:///tmp/cover.png");
  TEST_CHECK_EQ_STR(m.content_type, "video");
  TEST_CHECK(m.duration > 252.18 && m.duration < 252.19);
  TEST_CHECK_EQ_INT(xw_we_media_format_props(&m, out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out,
                    "{\"title\":\"Song\",\"artist\":\"A, B\","
                    "\"albumTitle\":\"Album\",\"contentType\":\"video\","
                    "\"subtitle\":\"Live at X\",\"genres\":\"Rock, Indie\","
                    "\"albumArtist\":\"AA, BB\"}");
  g_variant_unref(meta);

  TEST_BEGIN("MPRIS int64 length and single-string artist also parse");
  meta = metadata_full(g_variant_new_string("Solo"),
                       g_variant_new_int64(438000000),
                       g_variant_new_string("Solo AA"));
  memset(&m, 0, sizeof(m));
  TEST_CHECK_EQ_INT(xw_we_media_parse_metadata(meta, &m), 0);
  TEST_CHECK_EQ_STR(m.artist, "Solo");
  TEST_CHECK_EQ_STR(m.album_artist, "Solo AA");
  TEST_CHECK(m.duration > 437.99 && m.duration < 438.01);
  g_variant_unref(meta);

  TEST_BEGIN("missing metadata clears fields and defaults to music");
  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
  meta = g_variant_builder_end(&b);
  memset(&m, 0xff, sizeof(m));
  /* Keep the stale 0xff bytes NUL-terminated so a parse that forgets to
   * clear cannot make strcmp read past the array. */
  m.subtitle[sizeof(m.subtitle) - 1] = '\0';
  m.genres[sizeof(m.genres) - 1] = '\0';
  m.album_artist[sizeof(m.album_artist) - 1] = '\0';
  m.duration = 99;
  TEST_CHECK_EQ_INT(xw_we_media_parse_metadata(meta, &m), 0);
  TEST_CHECK_EQ_STR(m.title, "");
  TEST_CHECK_EQ_STR(m.artist, "");
  TEST_CHECK_EQ_STR(m.album, "");
  TEST_CHECK_EQ_STR(m.subtitle, "");
  TEST_CHECK_EQ_STR(m.genres, "");
  TEST_CHECK_EQ_STR(m.album_artist, "");
  TEST_CHECK_EQ_STR(m.art_url, "");
  TEST_CHECK_EQ_STR(m.content_type, "music");
  TEST_CHECK(m.duration == 0.0);
  g_variant_unref(meta);
}

static void test_state_and_pick(void) {
  TEST_BEGIN("PlaybackStatus maps to WE constants");
  TEST_CHECK_EQ_INT(xw_we_media_state_from_status("Playing"),
                    XW_WE_MEDIA_PLAYING);
  TEST_CHECK_EQ_INT(xw_we_media_state_from_status("Paused"),
                    XW_WE_MEDIA_PAUSED);
  TEST_CHECK_EQ_INT(xw_we_media_state_from_status("Stopped"),
                    XW_WE_MEDIA_STOPPED);
  TEST_CHECK_EQ_INT(xw_we_media_state_from_status("bogus"),
                    XW_WE_MEDIA_STOPPED);

  TEST_BEGIN("active player: Playing beats newer Paused, playerctld skipped");
  XWMediaPick p[] = {
      {"org.mpris.MediaPlayer2.vlc", XW_WE_MEDIA_STOPPED, 5},
      {"org.mpris.MediaPlayer2.mpd", XW_WE_MEDIA_PAUSED, 99},
      {"org.mpris.MediaPlayer2.spotify", XW_WE_MEDIA_PLAYING, 1},
      {"org.mpris.MediaPlayer2.playerctld", XW_WE_MEDIA_PLAYING, 1000},
  };
  TEST_CHECK_EQ_INT(xw_we_media_pick(p, 4), 2);

  p[2].state = XW_WE_MEDIA_PAUSED;
  p[2].last_active = 1;
  TEST_CHECK_EQ_INT(xw_we_media_pick(p, 4), 1);

  TEST_BEGIN("no eligible player returns -1");
  XWMediaPick only_proxy[] = {
      {"org.mpris.MediaPlayer2.playerctld", XW_WE_MEDIA_PLAYING, 1000},
  };
  TEST_CHECK_EQ_INT(xw_we_media_pick(only_proxy, 1), -1);
}

static void test_art(void) {
  TEST_BEGIN("artwork URL classification");
  TEST_CHECK_EQ_INT(xw_we_media_art_kind(NULL), XW_WE_MEDIA_ART_NONE);
  TEST_CHECK_EQ_INT(xw_we_media_art_kind(""), XW_WE_MEDIA_ART_NONE);
  TEST_CHECK_EQ_INT(xw_we_media_art_kind("file:///tmp/a.jpg"),
                    XW_WE_MEDIA_ART_FILE);
  TEST_CHECK_EQ_INT(xw_we_media_art_kind("/tmp/a.jpg"), XW_WE_MEDIA_ART_FILE);
  TEST_CHECK_EQ_INT(xw_we_media_art_kind("https://x/a.jpg"),
                    XW_WE_MEDIA_ART_HTTP);
  TEST_CHECK_EQ_INT(xw_we_media_art_kind("data:image/png;base64,AA"),
                    XW_WE_MEDIA_ART_DATA);
  TEST_CHECK_EQ_INT(xw_we_media_art_kind("smb://x/a.jpg"),
                    XW_WE_MEDIA_ART_OTHER);

  TEST_BEGIN("local artwork becomes a data URL with palette colors");
  char *abs = g_canonicalize_filename("tests/data/red.png", NULL);
  char *uri = g_filename_to_uri(abs, NULL, NULL);
  char *out = NULL;
  XWMediaInfo m;
  memset(&m, 0, sizeof(m));
  TEST_CHECK(uri != NULL);
  if (uri) {
    TEST_CHECK_EQ_INT(xw_we_media_resolve_art(uri, &out, &m), 0);
    TEST_CHECK(out != NULL && strncmp(out, "data:image/png;base64,", 22) == 0);
    TEST_CHECK_EQ_STR(m.color_primary, "#fd0000");
    TEST_CHECK_EQ_STR(m.color_secondary, "");
    TEST_CHECK_EQ_STR(m.color_text, "#000000");
    g_free(out);
    g_free(uri);
  }
  g_free(abs);

  TEST_BEGIN("remote artwork passes through and keeps colors empty");
  memset(&m, 0xff, sizeof(m));
  out = NULL;
  TEST_CHECK_EQ_INT(xw_we_media_resolve_art("https://example.test/a.jpg", &out,
                                            &m),
                    0);
  TEST_CHECK_EQ_STR(out, "https://example.test/a.jpg");
  TEST_CHECK_EQ_STR(m.color_primary, "");
  TEST_CHECK_EQ_STR(m.color_tertiary, "");
  TEST_CHECK_EQ_STR(m.color_high, "");
  g_free(out);

  TEST_BEGIN("base64 data URL artwork is decoded for palette colors");
  gchar *raw = NULL;
  gsize raw_len = 0;
  char *b64 = NULL;
  char *data_url = NULL;
  char *png_path = g_canonicalize_filename("tests/data/red.png", NULL);
  if (g_file_get_contents(png_path, &raw, &raw_len, NULL)) {
    b64 = g_base64_encode((const guchar *)raw, raw_len);
    data_url = g_strdup_printf("data:image/png;base64,%s", b64);
  }
  g_free(png_path);
  g_free(raw);
  g_free(b64);
  TEST_CHECK(data_url != NULL);
  if (data_url) {
    memset(&m, 0, sizeof(m));
    out = NULL;
    TEST_CHECK_EQ_INT(xw_we_media_resolve_art(data_url, &out, &m), 0);
    TEST_CHECK_EQ_STR(out, data_url);
    TEST_CHECK_EQ_STR(m.color_primary, "#fd0000");
    TEST_CHECK_EQ_STR(m.color_text, "#000000");
    g_free(out);
    g_free(data_url);
  }

  TEST_BEGIN("unparseable data URL keeps artwork but leaves colors empty");
  memset(&m, 0xff, sizeof(m));
  out = NULL;
  TEST_CHECK_EQ_INT(xw_we_media_resolve_art("data:image/png;base64,AA", &out,
                                            &m),
                    0);
  TEST_CHECK_EQ_STR(out, "data:image/png;base64,AA");
  TEST_CHECK_EQ_STR(m.color_primary, "");
  TEST_CHECK_EQ_STR(m.color_text, "");
  g_free(out);

  TEST_BEGIN("empty artwork clears colors and returns no URL");
  memset(&m, 0xff, sizeof(m));
  out = NULL;
  TEST_CHECK_EQ_INT(xw_we_media_resolve_art("", &out, &m), 0);
  TEST_CHECK(out == NULL);
  TEST_CHECK_EQ_STR(m.color_primary, "");
  TEST_CHECK_EQ_STR(m.color_secondary, "");
  TEST_CHECK_EQ_STR(m.color_high, "");
}

int main(void) {
  TEST_SUITE("we_media");
  test_json_escape();
  test_format_payloads();
  test_parse_metadata();
  test_state_and_pick();
  test_art();
  TEST_SUMMARY();
}
