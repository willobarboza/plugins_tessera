#include "jellyfin.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::jellyfin {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

// Icons of Tessera's set (screen_manager/app/tile_icons.py).
static constexpr uint32_t ICON_PLAY = 0xF040A;      // play
static constexpr uint32_t ICON_PAUSE = 0xF03E4;     // pause
static constexpr uint32_t ICON_STOP = 0xF04DB;      // stop
static constexpr uint32_t ICON_VOL_DOWN = 0xF075E;  // volume-minus
static constexpr uint32_t ICON_VOL_UP = 0xF075D;    // volume-plus
static constexpr uint32_t ICON_MUTED = 0xF0581;     // volume-off
static constexpr uint32_t ICON_VOLUME = 0xF057E;    // volume-high
static constexpr uint32_t ICON_FOLDER = 0xF024B;    // folder
static constexpr uint32_t ICON_TV = 0xF0ECF;        // television-play
static constexpr uint32_t ICON_MOVIE = 0xF0FCE;     // movie-open
static constexpr uint32_t ICON_MUSIC = 0xF075A;     // music
static constexpr uint32_t ICON_LEFT = 0xF0141;      // chevron-left
static constexpr uint32_t ICON_RIGHT = 0xF0142;     // chevron-right

// Home Assistant's supported_features of a media player. The Jellyfin integration sets them from what the client says
// it can do: a client without remote control has none, one without a volume of its own has no VOLUME_SET.
static constexpr uint32_t F_PAUSE = 1, F_SEEK = 2, F_VOLUME_SET = 4, F_VOLUME_MUTE = 8, F_STOP = 4096, F_PLAY = 16384,
                          F_BROWSE = 131072;

static void visible(lv_obj_t *part, bool on) {
  if (!part) return;
  if (on) lv_obj_remove_flag(part, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
}

static void copy_to(char *to, size_t size, const char *from) { snprintf(to, size, "%s", from ? from : ""); }

// ---- What a Jellyfin player says: its state, what it plays, how far along, and what it can do ----

enum class Play : uint8_t { NONE, GONE, OFF, IDLE, PAUSED, PLAYING };

static Play play_of(const char *state) {
  if (strcmp(state, "playing") == 0 || strcmp(state, "buffering") == 0) return Play::PLAYING;
  if (strcmp(state, "paused") == 0) return Play::PAUSED;
  if (strcmp(state, "idle") == 0 || strcmp(state, "on") == 0 || strcmp(state, "standby") == 0) return Play::IDLE;
  if (strcmp(state, "off") == 0) return Play::OFF;
  return Play::GONE;  // unavailable, unknown: the client left the server
}

static const char *word_of(Play p) {
  switch (p) {
    case Play::PLAYING: return "playing";
    case Play::PAUSED: return "paused";
    case Play::IDLE: return "idle";
    case Play::OFF: return "off";
    default: return "offline";
  }
}

static Tone tone_of(Play p) {
  switch (p) {
    case Play::PLAYING: return Tone::GOOD;
    case Play::PAUSED: return Tone::BUSY;
    default: return Tone::NORMAL;
  }
}

// A season's or an episode's number as Home Assistant has it: a number, or a text.
static void number_text(JsonVariantConst v, char *out, size_t size) {
  if (v.is<int>()) snprintf(out, size, "%d", v.as<int>());
  else if (v.is<float>()) snprintf(out, size, "%.0f", v.as<float>());
  else copy_to(out, size, v | "");
}

struct Player {
  Play play = Play::NONE;
  char name[48] = "";
  char title[52] = "";
  char sub[96] = "";  // "Series · S1 E2", or "Artist · Album"
  float duration = 0, position = 0;
  uint32_t position_at = 0;
  uint32_t features = 0;
  bool muted = false;

  bool has_media() const { return title[0] && (play == Play::PLAYING || play == Play::PAUSED); }
  bool can(uint32_t feature) const { return features == 0 || (features & feature) != 0; }
  // Where it is now: Home Assistant says where it was at a moment, and a player that plays moves on from there.
  float position_now(uint32_t epoch) const {
    float p = position;
    if (play == Play::PLAYING && position_at && epoch > position_at) p += static_cast<float>(epoch - position_at);
    if (duration > 0 && p > duration) p = duration;
    return p < 0 ? 0 : p;
  }
};

// A player's data: {"state", "name", "attributes"}. Nothing there (no entity chosen, or the app waits) leaves NONE.
static void parse_player(JsonObjectConst e, const tessera::Plugin *plugin, Player &p) {
  p = Player();
  if (e.isNull() || e["wait"].is<const char *>() || !e["state"].is<const char *>()) return;
  p.play = play_of(e["state"] | "");
  copy_to(p.name, sizeof(p.name), e["name"] | "");
  JsonObjectConst a = e["attributes"];
  copy_to(p.title, sizeof(p.title), a["media_title"] | "");
  const char *series = a["media_series_title"] | "";
  if (series[0]) {
    char season[8], episode[8];
    number_text(a["media_season"], season, sizeof(season));
    number_text(a["media_episode"], episode, sizeof(episode));
    if (season[0] && episode[0]) {
      std::string ep = tessera::fill(plugin->text("episode"), "season", season);
      ep = tessera::fill(ep.c_str(), "episode", episode);
      snprintf(p.sub, sizeof(p.sub), "%s · %s", series, ep.c_str());
    } else {
      copy_to(p.sub, sizeof(p.sub), series);
    }
  } else {
    const char *artist = a["media_artist"] | "";
    const char *album = a["media_album_name"] | "";
    if (artist[0] && album[0]) snprintf(p.sub, sizeof(p.sub), "%s · %s", artist, album);
    else copy_to(p.sub, sizeof(p.sub), artist[0] ? artist : album);
  }
  p.duration = a["media_duration"] | 0.0f;
  p.position = a["media_position"] | 0.0f;
  // A moment: the app sends it as seconds since 1970 (an attribute whose name ends in _at).
  p.position_at = static_cast<uint32_t>(a["media_position_updated_at"].as<double>());
  p.features = a["supported_features"] | 0u;
  p.muted = a["is_volume_muted"] | false;
}

// 754 seconds as "12:34", 3723 as "1:02:03".
static void time_text(float seconds, char *out, size_t size) {
  const int s = seconds > 0 ? static_cast<int>(seconds) : 0;
  if (s >= 3600) snprintf(out, size, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
  else snprintf(out, size, "%d:%02d", s / 60, s % 60);
}

// ---- A progress line: a bar, and under it how far along at the left and what is left at the right ----

struct Progress {
  lv_obj_t *track{}, *fill{}, *left{}, *right{};

  void make(lv_obj_t *parent, Font font) {
    track = ui::block(parent, theme::TRACK);
    fill = ui::block(track, theme::ACCENT);
    left = ui::label(parent, font, theme::MUTED);
    right = ui::label(parent, font, theme::MUTED);
    lv_obj_set_style_text_align(right, LV_TEXT_ALIGN_RIGHT, 0);
  }
  void hide() {
    visible(track, false);
    visible(left, false);
    visible(right, false);
  }
  static int height(Font font, int bar, bool times) { return bar + (times ? ui::px(4) + ui::line_height(font) : 0); }
  void place(const Player &p, uint32_t epoch, int x, int y, int w, int bar, bool times, Font font) {
    const float now = p.position_now(epoch);
    visible(track, true);
    lv_obj_set_size(track, w, bar);
    lv_obj_set_pos(track, x, y);
    lv_obj_set_style_radius(track, bar / 2, 0);
    lv_obj_set_style_bg_color(track, ui::color(theme::TRACK), 0);
    const int filled = p.duration > 0 ? static_cast<int>(w * now / p.duration) : 0;
    visible(fill, filled > 0);
    lv_obj_set_size(fill, filled < bar ? bar : filled, bar);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_style_radius(fill, bar / 2, 0);
    ui::set_fill(fill, p.play == Play::PLAYING ? Tone::ACCENT : Tone::BUSY, theme::ACCENT);
    visible(left, times);
    visible(right, times);
    if (!times) return;
    const int lh = ui::line_height(font), ty = y + bar + ui::px(4);
    char a[16], b[16], rest[20];
    time_text(now, a, sizeof(a));
    time_text(p.duration - now, b, sizeof(b));
    snprintf(rest, sizeof(rest), "-%s", b);
    ui::set_text(left, a);
    ui::set_text(right, rest);
    ui::set_color(left, theme::MUTED);
    ui::set_color(right, theme::MUTED);
    lv_obj_set_size(left, w / 2, lh);
    lv_obj_set_pos(left, x, ty);
    lv_obj_set_size(right, w / 2, lh);
    lv_obj_set_pos(right, x + w - w / 2, ty);
  }
};

// ---- A key of a card: a rounded block with an icon or a word in the middle ----

struct Key {
  lv_obj_t *block{}, *label{};
  int x = 0, y = 0, w = 0, h = 0;
  bool shown = false;

  void make(lv_obj_t *parent) {
    block = ui::block(parent, theme::TRACK);
    label = ui::label(block, Font::ICON, theme::INK);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  }
  void hide() {
    shown = false;
    visible(block, false);
  }
  void place(int kx, int ky, int kw, int kh, const std::string &text, Font font, bool lit) {
    shown = true;
    x = kx;
    y = ky;
    w = kw;
    h = kh;
    visible(block, true);
    lv_obj_set_size(block, kw, kh);
    lv_obj_set_pos(block, kx, ky);
    ui::set_fill(block, lit ? Tone::ACCENT : Tone::NORMAL, theme::TRACK);
    ui::set_font(label, font);
    ui::set_text(label, text);
    ui::set_color(label, lit ? theme::ON_ACCENT : theme::INK);
    const int lh = ui::line_height(font);
    lv_obj_set_size(label, kw, lh);
    lv_obj_set_pos(label, 0, (kh - lh) / 2);
  }
  bool hit(const tessera::Point &p) const { return shown && p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
};

// ---- The now playing tile: one client, its cover, what plays and how far along. A tap opens its control card ----

class NowTile : public tessera::Tile {
 public:
  explicit NowTile(Jellyfin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    copy_to(entity_, sizeof(entity_), c.entity);
    copy_to(title_text_, sizeof(title_text_), c.name);
    jump_ = c.options["jump"] | 30;
    // The cover's backdrop: an icon on a soft block, until (or while there is no) cover. The cover lies over it.
    back_ = ui::block(c.parent, theme::TRACK);
    back_icon_ = ui::label(back_, Font::ICON, theme::MUTED);
    lv_obj_set_style_text_align(back_icon_, LV_TEXT_ALIGN_CENTER, 0);
    ui::set_text(back_icon_, ui::icon(ICON_TV));
    if (ui::pictures()) cover_ = ui::picture(c.parent, tessera::Placeholder::NONE);
    state_dot_ = ui::block(c.parent, theme::MUTED);
    name_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    title_ = ui::label(c.parent, Font::TITLE, theme::INK);
    sub_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    progress_.make(c.parent, Font::BODY);
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
  }

  void on_state(JsonObjectConst data) override {
    parse_player(data, plugin_, player_);
    if (cover_)
      ui::set_picture(cover_, player_.has_media() && entity_[0] ? std::string("cover:") + entity_ : std::string(),
                      tessera::Fit::COVER);
  }

  void on_tick(uint32_t epoch) override { layout(epoch); }
  void on_theme() override { layout(tessera::epoch()); }

  void on_tap() override {
    if (player_.play == Play::NONE) return;
    plugin_->card_setup = CardSetup();
    plugin_->card_setup.jump = jump_;
    tessera::open_card(plugin_->plugin_id(), "control", entity_, tile_, display_name());
  }

 private:
  const char *display_name() const {
    if (title_text_[0]) return title_text_;
    if (player_.name[0]) return player_.name;
    return plugin_->text("name_fallback");
  }

  void hide_all() {
    for (lv_obj_t *p : {back_, cover_, state_dot_, name_, title_, sub_}) visible(p, false);
    progress_.hide();
  }

  void layout(uint32_t epoch) {
    if (player_.play == Play::NONE) {
      hide_all();
      visible(note_, true);
      ui::set_text(note_, plugin_->text("choose_player"));
      ui::set_color(note_, theme::MUTED);
      lv_obj_set_size(note_, width_, height_);
      lv_obj_set_pos(note_, 0, 0);
      return;
    }
    visible(note_, false);
    const bool media = player_.has_media();
    const int gap = ui::px(8);
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY);
    const int bar = ui::px(6);
    const bool timed = media && player_.duration > 0;
    // What the words need at the least: the title and the bar.
    const int least = th + (timed ? gap + bar : 0);
    // The cover: at the left as high as the tile when there is room for the words beside it, else on top of a tall
    // tile, else none. Without media the same place shows the icon, so a small tile still says what it is.
    int side = 0;
    bool on_top = false;
    if (height_ >= ui::mm(12) && width_ - height_ >= ui::mm(22)) {
      side = height_;
    } else if (width_ >= ui::mm(12) && height_ - width_ >= least + gap) {
      side = width_;
      on_top = true;
    }
    visible(back_, side > 0);
    visible(cover_, side > 0 && media);
    if (side > 0) {
      lv_obj_set_size(back_, side, side);
      lv_obj_set_pos(back_, 0, 0);
      lv_obj_set_style_bg_color(back_, ui::color(theme::TRACK), 0);
      const int ih = ui::line_height(Font::ICON);
      lv_obj_set_size(back_icon_, side, ih);
      lv_obj_set_pos(back_icon_, 0, (side - ih) / 2);
      ui::set_color(back_icon_, theme::MUTED);
      if (cover_) {
        lv_obj_set_size(cover_, side, side);
        lv_obj_set_pos(cover_, 0, 0);
      }
    }
    const int x = side > 0 && !on_top ? side + gap : 0;
    const int top = on_top ? side + gap : 0;
    const int w = width_ - x, room = height_ - top;
    // The rows, the most important first; the roomiest set that fits is taken.
    const bool has_sub = media ? player_.sub[0] != 0 : true;
    struct Fit { bool head, sub, times; };
    const Fit fits[] = {{true, true, true}, {true, true, false}, {false, true, false}, {false, false, false}};
    Fit fit = fits[3];
    for (const Fit &f : fits) {
      int need = th;
      if (f.head && media) need += bh;
      if (f.sub && has_sub) need += bh;
      if (timed) need += gap + Progress::height(Font::BODY, bar, f.times);
      if (need <= room) {
        fit = f;
        break;
      }
    }
    int need = th + (fit.head && media ? bh : 0) + (fit.sub && has_sub ? bh : 0) +
               (timed ? gap + Progress::height(Font::BODY, bar, fit.times) : 0);
    // Beside the cover the words stand in the middle; on their own, at the top.
    int y = top + (side > 0 && !on_top && need < room ? (room - need) / 2 : 0);
    const bool head = fit.head && media;
    visible(state_dot_, head);
    visible(name_, head);
    if (head) {
      // A dot in the colour of the state (green playing, amber paused) before the player's name.
      const int d = ui::px(8), dx = d + ui::px(6);
      lv_obj_set_size(state_dot_, d, d);
      lv_obj_set_style_radius(state_dot_, d / 2, 0);
      lv_obj_set_pos(state_dot_, x, y + (bh - d) / 2);
      ui::set_fill(state_dot_, tone_of(player_.play), theme::MUTED);
      ui::set_text(name_, display_name());
      ui::set_color(name_, theme::MUTED);
      lv_obj_set_size(name_, w - dx, bh);
      lv_obj_set_pos(name_, x + dx, y);
      y += bh;
    }
    visible(title_, true);
    ui::set_text(title_, media ? player_.title : display_name());
    ui::set_color(title_, theme::INK);
    lv_obj_set_size(title_, w, th);
    lv_obj_set_pos(title_, x, y);
    y += th;
    const bool sub = fit.sub && has_sub && y + bh <= height_;
    visible(sub_, sub);
    if (sub) {
      ui::set_text(sub_, media ? player_.sub : plugin_->text(player_.play == Play::IDLE ? "nothing_playing" : word_of(player_.play)));
      ui::set_color(sub_, theme::MUTED);
      lv_obj_set_size(sub_, w, bh);
      lv_obj_set_pos(sub_, x, y);
      y += bh;
    }
    if (timed && y + gap + bar <= height_) progress_.place(player_, epoch, x, y + gap, w, bar, fit.times, Font::BODY);
    else progress_.hide();
  }

  Jellyfin *plugin_;
  int width_ = 0, height_ = 0, tile_ = -1, jump_ = 30;
  char entity_[96] = "";
  char title_text_[48] = "";
  Player player_;
  lv_obj_t *back_{}, *back_icon_{}, *cover_{}, *state_dot_{}, *name_{}, *title_{}, *sub_{}, *note_{};
  Progress progress_;
};

// ---- The sessions tile: up to eight clients, who plays what, the ones that play first ----

class SessionsTile : public tessera::Tile {
 public:
  static constexpr int ITEMS = 8;
  explicit SessionsTile(Jellyfin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    jump_ = c.options["jump"] | 30;
    active_only_ = c.options["active_only"] | false;
    for (int i = 0; i < ITEMS; i++) {
      char id[12];
      snprintf(id, sizeof(id), "session_%d", i + 1);
      copy_to(entities_[i], sizeof(entities_[i]), c.options[id] | "");
      if (entities_[i][0]) configured_++;
    }
    // Names of the person's own, in the order of the sessions: "Sala, Quarto". An empty place keeps the client's.
    const char *names = c.options["names"] | "";
    int slot = 0;
    for (const char *p = names; slot < ITEMS;) {
      const char *end = strchr(p, ',');
      const size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
      char one[40];
      snprintf(one, sizeof(one), "%.*s", static_cast<int>(len < sizeof(one) ? len : sizeof(one) - 1), p);
      char *g = one;
      while (*g == ' ') g++;
      size_t n = strlen(g);
      while (n && g[n - 1] == ' ') g[--n] = 0;
      copy_to(given_[slot++], sizeof(given_[0]), g);
      if (!end) break;
      p = end + 1;
    }
    head_ = ui::label(c.parent, Font::TITLE, theme::INK);
    for (Row &r : rows_) {
      r.dot = ui::block(c.parent, theme::MUTED);
      r.name = ui::label(c.parent, Font::BODY, theme::INK);
      r.what = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(r.what, LV_TEXT_ALIGN_RIGHT, 0);
    }
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
  }

  void on_state(JsonObjectConst data) override {
    JsonObjectConst entities = data["entities"];
    count_ = 0;
    active_ = 0;
    for (int i = 0; i < ITEMS; i++) {
      char id[12];
      snprintf(id, sizeof(id), "session_%d", i + 1);
      Entry &e = entries_[count_];
      parse_player(entities[id], plugin_, e.player);
      if (e.player.play == Play::NONE) continue;
      const bool active = e.player.play == Play::PLAYING || e.player.play == Play::PAUSED;
      if (active_only_ && !active) continue;
      if (active) active_++;
      e.slot = i;
      copy_to(e.label, sizeof(e.label), given_[i][0] ? given_[i] : e.player.name);
      count_++;
    }
    // The ones that play first, then paused, idle and gone; the order of the inspector within each.
    for (int i = 1; i < count_; i++)
      for (int j = i; j > 0 && rank(entries_[j]) < rank(entries_[j - 1]); j--) {
        Entry t = entries_[j];
        entries_[j] = entries_[j - 1];
        entries_[j - 1] = t;
      }
  }

  void on_tick(uint32_t) override { layout(); }
  void on_theme() override { layout(); }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    for (int i = 0; i < shown_; i++) {
      const Row &r = rows_[i];
      if (p.y < r.y || p.y >= r.y + r.h) continue;
      const Entry &e = entries_[i];
      if (!entities_[e.slot][0]) return;
      plugin_->card_setup = CardSetup();
      snprintf(plugin_->card_setup.slot, sizeof(plugin_->card_setup.slot), "session_%d", e.slot + 1);
      plugin_->card_setup.jump = jump_;
      tessera::open_card(plugin_->plugin_id(), "control", entities_[e.slot], tile_, e.label);
      return;
    }
  }

 private:
  struct Entry {
    Player player;
    int slot = 0;
    char label[48] = "";
  };
  struct Row {
    lv_obj_t *dot{}, *name{}, *what{};
    int y = 0, h = 0;
  };

  static int rank(const Entry &e) {
    switch (e.player.play) {
      case Play::PLAYING: return 0;
      case Play::PAUSED: return 1;
      case Play::IDLE: return 2;
      case Play::OFF: return 3;
      default: return 4;
    }
  }

  void layout() {
    if (configured_ == 0) {
      visible(head_, false);
      for (Row &r : rows_) hide(r);
      shown_ = 0;
      visible(note_, true);
      ui::set_text(note_, plugin_->text("choose_sessions"));
      ui::set_color(note_, theme::MUTED);
      lv_obj_set_size(note_, width_, height_);
      lv_obj_set_pos(note_, 0, 0);
      return;
    }
    visible(note_, false);
    visible(head_, true);
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY);
    ui::set_text(head_, active_ ? tessera::format(plugin_->text("watching"), active_) : std::string(plugin_->text("nobody")));
    ui::set_tone(head_, active_ ? Tone::GOOD : Tone::NORMAL, theme::INK);
    lv_obj_set_size(head_, width_, th);
    lv_obj_set_pos(head_, 0, 0);
    // A row is as high as a finger needs when there is room, else as high as its words.
    const int gap = ui::px(4), d = ui::px(10);
    const int row_h = (height_ - th - gap) >= count_ * ui::mm(8) ? ui::mm(8) : bh + gap;
    int y = th + gap;
    shown_ = 0;
    for (int i = 0; i < ITEMS; i++) {
      Row &r = rows_[i];
      if (i >= count_ || y + bh > height_) {
        hide(r);
        continue;
      }
      const Entry &e = entries_[i];
      const bool media = e.player.has_media();
      visible(r.dot, true);
      visible(r.name, true);
      visible(r.what, true);
      const int ty = y + (row_h - bh) / 2;
      lv_obj_set_size(r.dot, d, d);
      lv_obj_set_style_radius(r.dot, d / 2, 0);
      lv_obj_set_pos(r.dot, 0, ty + (bh - d) / 2);
      ui::set_fill(r.dot, tone_of(e.player.play), e.player.play == Play::GONE ? theme::TRACK : theme::MUTED);
      const int x = d + gap * 2;
      // The name takes what it needs up to half the row; what plays takes the rest.
      int nw = ui::text_width(e.label, Font::BODY) + gap * 2;
      if (nw > (width_ - x) / 2) nw = (width_ - x) / 2;
      ui::set_text(r.name, e.label);
      ui::set_color(r.name, e.player.play == Play::GONE ? theme::MUTED : theme::INK);
      lv_obj_set_size(r.name, nw, bh);
      lv_obj_set_pos(r.name, x, ty);
      ui::set_text(r.what, media ? e.player.title : plugin_->text(word_of(e.player.play)));
      if (media) ui::set_color(r.what, theme::INK_SOFT);
      else ui::set_color(r.what, theme::MUTED);
      lv_obj_set_size(r.what, width_ - x - nw, bh);
      lv_obj_set_pos(r.what, x + nw, ty);
      r.y = y;
      r.h = row_h;
      shown_++;
      y += row_h;
    }
  }

  static void hide(Row &r) {
    visible(r.dot, false);
    visible(r.name, false);
    visible(r.what, false);
  }

  Jellyfin *plugin_;
  int width_ = 0, height_ = 0, tile_ = -1, jump_ = 30;
  bool active_only_ = false;
  int configured_ = 0, count_ = 0, active_ = 0, shown_ = 0;
  char entities_[ITEMS][96] = {};
  char given_[ITEMS][40] = {};
  Entry entries_[ITEMS];
  lv_obj_t *head_{}, *note_{};
  Row rows_[ITEMS];
};

// ---- The control card: a client large, its cover, what plays, and keys ----
// Play and pause and mute are wishes (they show at once and Home Assistant squares them); seek, stop and the volume
// are actions. Each key shows only when the client says it can do it (supported_features).

class ControlCard : public tessera::Card {
 public:
  enum KeyId { BACK, PLAY, FORWARD, STOP, VOL_DOWN, MUTE, VOL_UP, LIBRARY, KEYS };
  explicit ControlCard(Jellyfin *plugin) : plugin_(plugin), setup_(plugin->card_setup) {}

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    copy_to(entity_, sizeof(entity_), c.entity);
    back_ = ui::block(c.parent, theme::TRACK);
    back_icon_ = ui::label(back_, Font::ICON, theme::MUTED);
    lv_obj_set_style_text_align(back_icon_, LV_TEXT_ALIGN_CENTER, 0);
    ui::set_text(back_icon_, ui::icon(ICON_TV));
    if (ui::pictures()) cover_ = ui::picture(c.parent, tessera::Placeholder::NONE);
    state_dot_ = ui::block(c.parent, theme::MUTED);
    name_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK_SOFT);
    title_ = ui::label(c.parent, Font::HEADLINE, theme::INK);
    sub_ = ui::label(c.parent, Font::BODY_LARGE, theme::MUTED);
    progress_.make(c.parent, Font::BODY);
    for (Key &k : keys_) k.make(c.parent);
    note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
  }

  void on_state(JsonObjectConst data) override {
    // A now playing tile's data is its player; a sessions tile's holds every player under its option's id.
    JsonObjectConst e = setup_.slot[0] ? data["entities"][setup_.slot].as<JsonObjectConst>() : data;
    parse_player(e, plugin_, player_);
    if (cover_)
      ui::set_picture(cover_, player_.has_media() && entity_[0] ? std::string("cover:") + entity_ : std::string(),
                      tessera::Fit::COVER);
    layout(tessera::epoch());
  }

  void on_tick(uint32_t epoch) override {
    if (note_left_ && --note_left_ == 0) note_text_[0] = 0;
    layout(epoch);
  }
  void on_theme() override { layout(tessera::epoch()); }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    for (int i = 0; i < KEYS; i++) {
      if (!keys_[i].hit(p)) continue;
      press(static_cast<KeyId>(i));
      return;
    }
  }

 private:
  void press(KeyId key) {
    bool sent = true;
    switch (key) {
      case BACK:
      case FORWARD: {
        float to = player_.position_now(tessera::epoch()) + (key == BACK ? -setup_.jump : setup_.jump);
        if (to < 0) to = 0;
        if (player_.duration > 0 && to > player_.duration - 1) to = player_.duration - 1;
        JsonDocument d;
        d["seek_position"] = static_cast<int>(to);
        sent = tessera::action("media_player.media_seek", entity_, d.as<JsonVariantConst>());
        break;
      }
      case PLAY:
        sent = tessera::wish(entity_, tessera::Wish::PLAYING, player_.play == Play::PLAYING ? "0" : "1");
        break;
      case STOP:
        sent = tessera::action("media_player.media_stop", entity_);
        break;
      case VOL_DOWN:
      case VOL_UP:
        sent = tessera::action(key == VOL_UP ? "media_player.volume_up" : "media_player.volume_down", entity_);
        break;
      case MUTE:
        sent = tessera::wish(entity_, tessera::Wish::MUTED, player_.muted ? "0" : "1");
        break;
      case LIBRARY:
        tessera::open_card(plugin_->plugin_id(), "library", entity_, tile_, plugin_->text("library"));
        return;
      default:
        return;
    }
    if (!sent) {
      copy_to(note_text_, sizeof(note_text_), plugin_->text("not_sent"));
      note_left_ = 4;
    }
    layout(tessera::epoch());
  }

  void layout(uint32_t epoch) {
    const int gap = ui::px(10);
    const int bh = ui::line_height(Font::BODY), lh = ui::line_height(Font::BODY_LARGE);
    const bool media = player_.has_media();
    const bool here = player_.play != Play::NONE && player_.play != Play::GONE;
    // The keys: the player's keys in a row, the volume and the library in a second.
    bool want[KEYS] = {};
    if (here && media) {
      want[BACK] = want[FORWARD] = player_.can(F_SEEK) && player_.duration > 0;
      want[PLAY] = player_.can(F_PAUSE) || player_.can(F_PLAY);
      want[STOP] = player_.can(F_STOP);
    }
    if (here) {
      want[VOL_DOWN] = want[VOL_UP] = player_.can(F_VOLUME_SET);
      want[MUTE] = player_.can(F_VOLUME_MUTE);
      want[LIBRARY] = player_.can(F_BROWSE);
    }
    int first = 0, second = 0;
    for (int i = BACK; i <= STOP; i++) first += want[i];
    for (int i = VOL_DOWN; i <= LIBRARY; i++) second += want[i];
    // Keys as high as a finger needs; a little lower on the small glass of the CYD, so the words keep some room.
    const int kh = ui::large() ? ui::mm(12) : ui::mm(9);
    const int rows = (first ? 1 : 0) + (second ? 1 : 0);
    const int keys_h = rows ? rows * kh + (rows - 1) * gap : 0;
    const int keys_top = height_ - bh - ui::px(4) - keys_h;
    // Above the keys: the words, and the cover at their left when it plays and there is room for both. Every part is
    // laid out only where it ends above the keys, the most important first, so nothing lies over another.
    const int room = keys_top - gap;
    const int bar = ui::px(8);
    const int prog_h = media && player_.duration > 0 ? gap + Progress::height(Font::BODY, bar, true) : 0;
    const bool has_sub = media && player_.sub[0];
    const Font title_font =
        lh + ui::line_height(Font::HEADLINE) + (has_sub ? lh : 0) + prog_h <= room ? Font::HEADLINE : Font::TITLE;
    const int th = ui::line_height(title_font);
    const bool show_title = th <= room;
    const bool show_head = show_title && lh + th <= room;
    int need = (show_head ? lh : 0) + (show_title ? th : 0);
    const bool show_sub = has_sub && need + lh <= room;
    need += show_sub ? lh : 0;
    const bool show_progress = prog_h && need + prog_h <= room;
    need += show_progress ? prog_h : 0;
    int side = 0;
    if (media && room >= ui::mm(14)) {
      side = room < width_ * 2 / 5 ? room : width_ * 2 / 5;
      if (width_ - side - gap * 2 < ui::mm(30)) side = 0;  // the words would have no room beside it
    }
    visible(back_, side > 0);
    visible(cover_, side > 0);
    if (side > 0) {
      lv_obj_set_size(back_, side, side);
      lv_obj_set_pos(back_, 0, 0);
      lv_obj_set_style_bg_color(back_, ui::color(theme::TRACK), 0);
      const int ih = ui::line_height(Font::ICON);
      lv_obj_set_size(back_icon_, side, ih);
      lv_obj_set_pos(back_icon_, 0, (side - ih) / 2);
      ui::set_color(back_icon_, theme::MUTED);
      if (cover_) {
        lv_obj_set_size(cover_, side, side);
        lv_obj_set_pos(cover_, 0, 0);
      }
    }
    const int x = side ? side + gap * 2 : 0;
    const int w = width_ - x;
    // Beside the cover the words stand in the middle of it; without one, at the top.
    int y = side > need ? (side - need) / 2 : 0;
    // The state: a dot in its colour and its word, in the colour of words.
    visible(state_dot_, show_head && here);
    visible(name_, show_head);
    if (show_head) {
      int nx = x;
      if (here) {
        const int d = ui::px(10);
        lv_obj_set_size(state_dot_, d, d);
        lv_obj_set_style_radius(state_dot_, d / 2, 0);
        lv_obj_set_pos(state_dot_, x, y + (lh - d) / 2);
        ui::set_fill(state_dot_, tone_of(player_.play), theme::MUTED);
        nx += d + ui::px(8);
      }
      ui::set_text(name_, plugin_->text(player_.play == Play::NONE ? "nothing_playing" : word_of(player_.play)));
      ui::set_color(name_, theme::INK_SOFT);
      lv_obj_set_size(name_, x + w - nx, lh);
      lv_obj_set_pos(name_, nx, y);
      y += lh;
    }
    visible(title_, show_title);
    if (show_title) {
      ui::set_font(title_, title_font);
      ui::set_text(title_, media ? player_.title : plugin_->text(here ? "nothing_playing" : "offline"));
      ui::set_color(title_, theme::INK);
      lv_obj_set_size(title_, w, th);
      lv_obj_set_pos(title_, x, y);
      y += th;
    }
    visible(sub_, show_sub);
    if (show_sub) {
      ui::set_text(sub_, player_.sub);
      ui::set_color(sub_, theme::MUTED);
      lv_obj_set_size(sub_, w, lh);
      lv_obj_set_pos(sub_, x, y);
      y += lh;
    }
    if (show_progress) progress_.place(player_, epoch, x, y + gap, w, bar, true, Font::BODY);
    else progress_.hide();
    // The keys.
    int ky = keys_top;
    for (int row = 0; row < 2; row++) {
      const int from = row == 0 ? BACK : VOL_DOWN, to = row == 0 ? STOP : LIBRARY;
      const int count = row == 0 ? first : second;
      for (int i = from; i <= to; i++) keys_[i].hide();
      if (!count) continue;
      const int kw = (width_ - gap * (count - 1)) / count;
      int kx = 0;
      for (int i = from; i <= to; i++) {
        if (!want[i]) continue;
        place_key(static_cast<KeyId>(i), kx, ky, kw, kh);
        kx += kw + gap;
      }
      ky += kh + gap;
    }
    ui::set_text(note_, note_text_);
    ui::set_color(note_, theme::MUTED);
    lv_obj_set_size(note_, width_, bh);
    lv_obj_set_pos(note_, 0, height_ - bh);
  }

  void place_key(KeyId id, int x, int y, int w, int h) {
    Key &k = keys_[id];
    switch (id) {
      case BACK: k.place(x, y, w, h, tessera::format(plugin_->text("back_n"), setup_.jump), Font::BODY_LARGE, false); break;
      case FORWARD: k.place(x, y, w, h, tessera::format(plugin_->text("forward_n"), setup_.jump), Font::BODY_LARGE, false); break;
      case PLAY:
        k.place(x, y, w, h, ui::icon(player_.play == Play::PLAYING ? ICON_PAUSE : ICON_PLAY), Font::ICON, true);
        break;
      case STOP: k.place(x, y, w, h, ui::icon(ICON_STOP), Font::ICON, false); break;
      case VOL_DOWN: k.place(x, y, w, h, ui::icon(ICON_VOL_DOWN), Font::ICON, false); break;
      case VOL_UP: k.place(x, y, w, h, ui::icon(ICON_VOL_UP), Font::ICON, false); break;
      case MUTE: k.place(x, y, w, h, ui::icon(player_.muted ? ICON_MUTED : ICON_VOLUME), Font::ICON, player_.muted); break;
      case LIBRARY: k.place(x, y, w, h, plugin_->text("library"), Font::BODY_LARGE, false); break;
      default: break;
    }
  }

  Jellyfin *plugin_;
  CardSetup setup_;
  int width_ = 0, height_ = 0, tile_ = -1;
  char entity_[96] = "";
  Player player_;
  lv_obj_t *back_{}, *back_icon_{}, *cover_{}, *state_dot_{}, *name_{}, *title_{}, *sub_{}, *note_{};
  Progress progress_;
  Key keys_[KEYS];
  char note_text_[64] = "";
  uint8_t note_left_ = 0;
};

// ---- The library card: the server's libraries, then their folders and items, a page at a time ----
// It asks Home Assistant to browse through the client's media player (media_player/browse_media), the way Home
// Assistant's own media browser does. A tap on a folder, a series or an album opens it; a tap on a film, an episode or a
// song plays it on the client; "Play all" plays the folder that is open. Back goes up a level.
// An answer reaches the screen within about 3 KB, so a large library shows its first items only (sorted by name).

class LibraryCard : public tessera::Card, public Listener {
 public:
  static constexpr int ITEMS = 24;
  static constexpr int ROWS = 10;
  static constexpr int DEPTH = 8;
  explicit LibraryCard(Jellyfin *plugin) : plugin_(plugin) {}
  ~LibraryCard() override { plugin_->forget(this); }

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    height_ = c.height;
    copy_to(entity_, sizeof(entity_), c.entity);
    head_ = ui::label(c.parent, Font::TITLE, theme::INK);
    play_all_.make(c.parent);
    for (Row &r : rows_) {
      r.ground = ui::block(c.parent, theme::TRACK);
      r.icon = ui::label(r.ground, Font::ICON_SMALL, theme::MUTED);
      r.name = ui::label(r.ground, Font::BODY_LARGE, theme::INK);
    }
    prev_.make(c.parent);
    next_.make(c.parent);
    page_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(page_, LV_TEXT_ALIGN_CENTER, 0);
    message_ = ui::text(c.parent, Font::BODY_LARGE, theme::MUTED);
    lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, 0);
    note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    plugin_->listen(this);
    copy_to(levels_[0].title, sizeof(levels_[0].title), plugin_->text("library_root"));
    browse();
  }

  bool on_back() override {
    if (depth_ == 0) return false;
    depth_--;
    browse();
    return true;
  }

  void on_tick(uint32_t) override {
    if (note_left_ && --note_left_ == 0) note_text_[0] = 0;
    layout();
  }
  void on_theme() override { layout(); }

  void on_answer(JsonObjectConst m) override {
    const uint32_t re = m["re"] | 0u;
    if (!re || re != asked_) return;
    asked_ = 0;
    if (!(m["ok"] | false)) {
      state_ = State::FAILED;
      layout();
      return;
    }
    JsonObjectConst result = m["result"];
    Level &level = levels_[depth_];
    if (depth_ > 0) copy_to(level.title, sizeof(level.title), result["title"] | level.title);
    level.can_play = result["can_play"] | false;
    copy_to(level.kind, sizeof(level.kind), result["media_content_type"] | "");
    count_ = 0;
    for (JsonObjectConst child : result["children"].as<JsonArrayConst>()) {
      if (count_ >= ITEMS) break;
      Item &it = items_[count_];
      copy_to(it.id, sizeof(it.id), child["media_content_id"] | "");
      if (!it.id[0]) continue;
      copy_to(it.title, sizeof(it.title), child["title"] | "");
      copy_to(it.kind, sizeof(it.kind), child["media_content_type"] | "");
      copy_to(it.cls, sizeof(it.cls), child["media_class"] | "");
      it.expand = child["can_expand"] | false;
      it.play = child["can_play"] | false;
      count_++;
    }
    state_ = count_ ? State::READY : State::EMPTY;
    page_index_ = level.page;
    layout();
  }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (play_all_.hit(p)) {
      const Level &level = levels_[depth_];
      play(level.id, level.kind);
      return;
    }
    if (prev_.hit(p) && page_index_ > 0) {
      page_index_--;
      layout();
      return;
    }
    if (next_.hit(p) && (page_index_ + 1) * per_page_ < count_) {
      page_index_++;
      layout();
      return;
    }
    if (state_ != State::READY) return;
    for (int i = 0; i < ROWS; i++) {
      const Row &r = rows_[i];
      if (!r.shown || p.y < r.y || p.y >= r.y + r.h) continue;
      const int index = page_index_ * per_page_ + i;
      if (index >= count_) return;
      const Item &it = items_[index];
      if (it.expand && depth_ + 1 < DEPTH) {
        levels_[depth_].page = page_index_;
        depth_++;
        levels_[depth_] = Level();
        copy_to(levels_[depth_].id, sizeof(levels_[depth_].id), it.id);
        copy_to(levels_[depth_].title, sizeof(levels_[depth_].title), it.title);
        copy_to(levels_[depth_].kind, sizeof(levels_[depth_].kind), it.kind);
        browse();
      } else if (it.play) {
        play(it.id, it.kind);
      }
      return;
    }
  }

 private:
  enum class State : uint8_t { LOADING, READY, EMPTY, FAILED };
  struct Level {
    char id[48] = "";  // "" for the server's root: its libraries
    char title[52] = "";
    char kind[24] = "";
    bool can_play = false;
    int page = 0;
  };
  struct Item {
    char id[48] = "";
    char title[52] = "";
    char kind[24] = "";  // media_content_type, for play_media
    char cls[16] = "";   // media_class, for its icon
    bool expand = false, play = false;
  };
  struct Row {
    lv_obj_t *ground{}, *icon{}, *name{};
    int y = 0, h = 0;
    bool shown = false;
  };

  void browse() {
    state_ = State::LOADING;
    count_ = 0;
    page_index_ = 0;
    JsonDocument request;
    request["ask"] = "media_player/browse_media";
    request["data"]["entity_id"] = entity_;
    // Home Assistant takes a folder's id only together with its type, both or neither (the root).
    const Level &level = levels_[depth_];
    if (level.id[0]) {
      request["data"]["media_content_id"] = level.id;
      request["data"]["media_content_type"] = level.kind[0] ? level.kind : "collection";
    }
    asked_ = tessera::send(plugin_, request.as<JsonObjectConst>());
    if (!asked_) state_ = State::FAILED;
    layout();
  }

  void play(const char *id, const char *kind) {
    if (!id[0]) return;
    JsonDocument d;
    d["media_content_id"] = id;
    d["media_content_type"] = kind[0] ? kind : "video";
    const bool sent = tessera::action("media_player.play_media", entity_, d.as<JsonVariantConst>());
    copy_to(note_text_, sizeof(note_text_), plugin_->text(sent ? "play_sent" : "not_sent"));
    note_left_ = 4;
    layout();
  }

  static uint32_t icon_of(const Item &it) {
    if (it.expand) return ICON_FOLDER;
    if (strcmp(it.cls, "music") == 0 || strcmp(it.cls, "track") == 0) return ICON_MUSIC;
    return ICON_MOVIE;
  }

  void layout() {
    const int gap = ui::px(8);
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY);
    const int lh = ui::line_height(Font::BODY_LARGE), ih = ui::line_height(Font::ICON_SMALL);
    const int kh = ui::mm(10);
    // The head: where we are, and "Play all" at the right for a folder that plays.
    const Level &level = levels_[depth_];
    const bool all = state_ == State::READY && depth_ > 0 && level.can_play;
    int aw = 0;
    if (all) {
      aw = ui::text_width(plugin_->text("play_all"), Font::BODY_LARGE) + ui::px(32);
      play_all_.place(width_ - aw, 0, aw, kh, plugin_->text("play_all"), Font::BODY_LARGE, true);
    } else {
      play_all_.hide();
    }
    const int head_h = kh > th ? kh : th;
    ui::set_text(head_, level.title);
    ui::set_color(head_, theme::INK);
    lv_obj_set_size(head_, width_ - aw - (aw ? gap : 0), th);
    lv_obj_set_pos(head_, 0, (head_h - th) / 2);
    // The bottom: a note, and the page keys when there is more than one page.
    const int list_top = head_h + gap;
    const int note_top = height_ - bh;
    const int row_h = ui::mm(9) > lh + ui::px(8) ? ui::mm(9) : lh + ui::px(8);
    const int room = note_top - gap - list_top;
    per_page_ = (room + gap / 2) / (row_h + gap / 2);
    if (per_page_ > ROWS) per_page_ = ROWS;
    if (per_page_ < 1) per_page_ = 1;
    const bool paged = state_ == State::READY && count_ > per_page_;
    if (paged) {
      per_page_ = (room - kh - gap + gap / 2) / (row_h + gap / 2);
      if (per_page_ > ROWS) per_page_ = ROWS;
      if (per_page_ < 1) per_page_ = 1;
    }
    const int pages = (count_ + per_page_ - 1) / per_page_;
    if (page_index_ >= pages) page_index_ = pages ? pages - 1 : 0;
    // The rows.
    int y = list_top;
    for (int i = 0; i < ROWS; i++) {
      Row &r = rows_[i];
      const int index = page_index_ * per_page_ + i;
      r.shown = state_ == State::READY && i < per_page_ && index < count_;
      visible(r.ground, r.shown);
      if (!r.shown) continue;
      const Item &it = items_[index];
      r.y = y;
      r.h = row_h;
      lv_obj_set_size(r.ground, width_, row_h);
      lv_obj_set_pos(r.ground, 0, y);
      ui::set_fill(r.ground, Tone::NORMAL, theme::TRACK);
      const int pad = ui::px(12);
      ui::set_text(r.icon, ui::icon(icon_of(it)));
      ui::set_color(r.icon, it.expand ? theme::MUTED : theme::ACCENT);
      lv_obj_set_size(r.icon, ih, ih);
      lv_obj_set_pos(r.icon, pad, (row_h - ih) / 2);
      ui::set_text(r.name, it.title);
      ui::set_color(r.name, theme::INK);
      lv_obj_set_size(r.name, width_ - pad * 3 - ih, lh);
      lv_obj_set_pos(r.name, pad * 2 + ih, (row_h - lh) / 2);
      y += row_h + gap / 2;
    }
    // Page keys, under the rows.
    if (paged) {
      const int py = note_top - gap - kh;
      const int kw = ui::mm(16);
      prev_.place(0, py, kw, kh, ui::icon(ICON_LEFT), Font::ICON, false);
      next_.place(width_ - kw, py, kw, kh, ui::icon(ICON_RIGHT), Font::ICON, false);
      std::string words = tessera::fill(plugin_->text("page"), "n", std::to_string(page_index_ + 1));
      words = tessera::fill(words.c_str(), "total", std::to_string(pages));
      visible(page_, true);
      ui::set_text(page_, words);
      ui::set_color(page_, theme::MUTED);
      lv_obj_set_size(page_, width_ - kw * 2, bh);
      lv_obj_set_pos(page_, kw, py + (kh - bh) / 2);
    } else {
      prev_.hide();
      next_.hide();
      visible(page_, false);
    }
    // While it loads, when it failed, or for an empty folder: one line in the middle.
    const bool says = state_ != State::READY;
    visible(message_, says);
    if (says) {
      const char *key = state_ == State::LOADING ? "loading" : state_ == State::EMPTY ? "empty" : "browse_failed";
      ui::set_text(message_, plugin_->text(key));
      ui::set_color(message_, theme::MUTED);
      lv_obj_set_size(message_, width_, lh * 3);
      lv_obj_set_pos(message_, 0, list_top + (room - lh) / 2);
    }
    // The note: what a tap did, or that a long list was cut to its first items (the app shortens a long answer to 8,
    // 12, 16 or 24 items).
    std::string note = note_text_;
    if (note.empty() && state_ == State::READY && (count_ == 8 || count_ == 12 || count_ == 16 || count_ == 24))
      note = tessera::format(plugin_->text("first_n"), count_);
    ui::set_text(note_, note);
    ui::set_color(note_, theme::MUTED);
    lv_obj_set_size(note_, width_, bh);
    lv_obj_set_pos(note_, 0, note_top);
  }

  Jellyfin *plugin_;
  int width_ = 0, height_ = 0;
  char entity_[96] = "";
  Level levels_[DEPTH];
  int depth_ = 0;
  Item items_[ITEMS];
  int count_ = 0, per_page_ = 1, page_index_ = 0;
  State state_ = State::LOADING;
  uint32_t asked_ = 0;
  lv_obj_t *head_{}, *page_{}, *message_{}, *note_{};
  Key play_all_, prev_, next_;
  Row rows_[ROWS];
  char note_text_[64] = "";
  uint8_t note_left_ = 0;
};

void Jellyfin::setup() {
  add_tile("now_playing", [this]() { return new NowTile(this); });
  add_tile("sessions", [this]() { return new SessionsTile(this); });
  add_card("control", [this]() { return new ControlCard(this); }, true);
  add_card("library", [this]() { return new LibraryCard(this); }, true);
}

}  // namespace esphome::jellyfin
