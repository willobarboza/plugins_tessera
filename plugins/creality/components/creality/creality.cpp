#include "creality.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::creality {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

// Icons of Tessera's set (screen_manager/app/tile_icons.py).
static constexpr uint32_t ICON_CAMERA = 0xF07AE;    // cctv
static constexpr uint32_t ICON_PLAY = 0xF040A;      // play
static constexpr uint32_t ICON_PAUSE = 0xF03E4;     // pause
static constexpr uint32_t ICON_STOP = 0xF04DB;      // stop
static constexpr uint32_t ICON_LIGHT = 0xF0335;     // lightbulb
static constexpr uint32_t ICON_LIGHT_OFF = 0xF0E4F; // lightbulb-off

static void visible(lv_obj_t *part, bool on) {
  if (!part) return;
  if (on) lv_obj_remove_flag(part, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
}

// A text into a buffer of its own, cut on a whole character: a cut inside a UTF-8 letter would draw a box.
static void copy_to(char *to, size_t size, const char *from) {
  if (!size) return;
  if (!from) from = "";
  size_t n = strlen(from);
  if (n >= size) {
    n = size - 1;
    while (n && (static_cast<unsigned char>(from[n]) & 0xC0) == 0x80) n--;
  }
  memmove(to, from, n);
  to[n] = 0;
}

// A corner only when it changes: LVGL draws a part again for every style that is set, also to the same value.
static void round_to(lv_obj_t *part, int radius) {
  if (lv_obj_get_style_radius(part, LV_PART_MAIN) != radius) lv_obj_set_style_radius(part, radius, 0);
}

static void put(lv_obj_t *part, int x, int y, int w, int h) {
  lv_obj_set_size(part, w, h);
  lv_obj_set_pos(part, x, y);
}

// A number as Home Assistant has it: a number, or a text ("210.5"); NAN for none, "unavailable" or "unknown".
static float number_of(JsonVariantConst v) {
  if (v.is<int>() || v.is<long>() || v.is<float>() || v.is<double>()) return v.as<float>();
  const char *s = v | "";
  if (!*s) return NAN;
  char *end = nullptr;
  const float f = strtof(s, &end);
  return end != s ? f : NAN;
}

// ---- What the printer says: the state of the integration's Print Status sensor and its attributes ----

// TOO_LARGE: the app had no room for this entity in the tile's message.
enum class St : uint8_t { NONE, TOO_LARGE, OFFLINE, OFF, IDLE, PROCESSING, PRINTING, PAUSED, STOPPED, COMPLETED,
                          ERROR, TESTING };

static St state_of(const char *s) {
  if (strcmp(s, "printing") == 0) return St::PRINTING;
  if (strcmp(s, "paused") == 0) return St::PAUSED;
  if (strcmp(s, "processing") == 0) return St::PROCESSING;
  if (strcmp(s, "self-testing") == 0) return St::TESTING;
  if (strcmp(s, "completed") == 0) return St::COMPLETED;
  if (strcmp(s, "stopped") == 0) return St::STOPPED;
  if (strcmp(s, "error") == 0) return St::ERROR;
  if (strcmp(s, "idle") == 0) return St::IDLE;
  if (strcmp(s, "off") == 0) return St::OFF;
  return St::OFFLINE;  // unavailable, unknown: the integration does not reach the printer
}

static const char *word_of(St s) {
  switch (s) {
    case St::NONE: return "loading";
    case St::TOO_LARGE: return "too_large";
    case St::OFF: return "off";
    case St::IDLE: return "idle";
    case St::PROCESSING: return "processing";
    case St::PRINTING: return "printing";
    case St::PAUSED: return "paused";
    case St::STOPPED: return "stopped";
    case St::COMPLETED: return "completed";
    case St::ERROR: return "error";
    case St::TESTING: return "testing";
    default: return "offline";
  }
}

static Tone tone_of(St s) {
  switch (s) {
    case St::PRINTING: return Tone::ACCENT;
    case St::PROCESSING:
    case St::TESTING:
    case St::PAUSED: return Tone::BUSY;
    case St::COMPLETED: return Tone::GOOD;
    case St::ERROR: return Tone::ALERT;
    default: return Tone::NORMAL;
  }
}

struct Printer {
  St st = St::NONE;
  char name[52] = "";
  char file[64] = "";
  float progress = NAN;  // 0 to 100
  long left = -1;        // seconds still to go, as the printer said them
  uint32_t left_at = 0;  // when the screen heard them
  long err = 0;
  float nozzle = NAN, bed = NAN, chamber = NAN;
  long layer = -1, total = -1;
  int8_t light = -1;  // -1 not chosen or unknown, 0 off, 1 on

  // A job on the printer: what the progress, the keys and the time left are for.
  bool busy() const { return st == St::PRINTING || st == St::PAUSED || st == St::PROCESSING || st == St::TESTING; }
  bool here() const { return st != St::NONE && st != St::TOO_LARGE && st != St::OFFLINE && st != St::OFF; }
  bool has_progress() const { return busy() && !std::isnan(progress); }
  bool has_temps() const { return !std::isnan(nozzle) || !std::isnan(bed) || !std::isnan(chamber); }
  // The time left now: it counts down from what the printer said while it prints.
  long left_now(uint32_t epoch) const {
    if (left < 0 || !busy()) return -1;
    long l = left;
    if (st == St::PRINTING && left_at && epoch > left_at) l -= static_cast<long>(epoch - left_at);
    return l < 0 ? 0 : l;
  }
};

// The file without its folder and without ".gcode": "/usr/data/printer_data/gcodes/Benchy.gcode" is "Benchy".
static void file_of(const char *path, char *out, size_t size) {
  const char *slash = strrchr(path, '/');
  const char *name = slash ? slash + 1 : path;
  copy_to(out, size, name);
  const size_t n = strlen(out);
  if (n > 6 && strcasecmp(out + n - 6, ".gcode") == 0) out[n - 6] = 0;
}

// The status sensor's data: {"state", "name", "attributes"}. Nothing there (no entity chosen, or the app waits) leaves
// NONE; an entity the message had no room for, TOO_LARGE.
static void parse_status(JsonObjectConst e, Printer &p) {
  const int8_t light = p.light;
  const float nozzle = p.nozzle, bed = p.bed, chamber = p.chamber;
  const long layer = p.layer, total = p.total;
  p = Printer();
  p.light = light;
  p.nozzle = nozzle;
  p.bed = bed;
  p.chamber = chamber;
  p.layer = layer;
  p.total = total;
  if (e.isNull()) return;
  if (strcmp(e["wait"] | "", "too_large") == 0) {
    p.st = St::TOO_LARGE;
    return;
  }
  if (!e["state"].is<const char *>()) return;
  p.st = state_of(e["state"] | "");
  copy_to(p.name, sizeof(p.name), e["name"] | "");
  JsonObjectConst a = e["attributes"];
  file_of(a["file"] | "", p.file, sizeof(p.file));
  p.progress = number_of(a["progress"]);
  if (!std::isnan(p.progress)) p.progress = p.progress < 0 ? 0 : p.progress > 100 ? 100 : p.progress;
  const float left = number_of(a["left_time_s"]);
  if (!std::isnan(left) && left >= 0) {
    p.left = static_cast<long>(left);
    p.left_at = tessera::epoch();
  }
  const float err = number_of(a["error_code"]);
  p.err = std::isnan(err) ? 0 : static_cast<long>(err);
}

// The entities chosen in the inspector, under "entities" and the option's id.
static void parse_extras(JsonObjectConst entities, Printer &p) {
  auto value = [&](const char *id) -> float {
    JsonObjectConst e = entities[id];
    return e.isNull() ? NAN : number_of(e["state"]);
  };
  p.nozzle = value("nozzle");
  p.bed = value("bed");
  p.chamber = value("chamber");
  const float layer = value("layer"), total = value("total_layers");
  p.layer = std::isnan(layer) ? -1 : static_cast<long>(layer);
  p.total = std::isnan(total) || total <= 0 ? -1 : static_cast<long>(total);
  JsonObjectConst light = entities["light"];
  const char *ls = light["state"] | "";
  p.light = strcmp(ls, "on") == 0 ? 1 : strcmp(ls, "off") == 0 ? 0 : -1;
}

// ---- The words: a time, the temperatures, the layer ----

// 3840 seconds as "1h 04m", 720 as "12 min".
static std::string duration_text(const tessera::Plugin *plugin, long s) {
  const long m = (s + 59) / 60;
  char n[8];
  if (m >= 60) {
    snprintf(n, sizeof(n), "%ld", m / 60);
    std::string t = tessera::fill(plugin->text("duration_hm"), "h", n);
    snprintf(n, sizeof(n), "%02ld", m % 60);
    return tessera::fill(t.c_str(), "m", n);
  }
  snprintf(n, sizeof(n), "%ld", m);
  return tessera::fill(plugin->text("duration_m"), "m", n);
}

static std::string degrees(float v) {
  char t[12];
  snprintf(t, sizeof(t), "%.0f°", v);
  return t;
}

// The temperatures in the words that fit `w`: "Nozzle 210° · Bed 60° · Chamber 35°", then without the chamber, then
// "210° / 60°". "" when none fits, or none is chosen.
static std::string temps_text(const tessera::Plugin *plugin, const Printer &p, int w, Font font) {
  if (!p.has_temps()) return "";
  for (int form = 0; form < 3; form++) {
    std::string t;
    auto add = [&](const char *key, float v, bool word) {
      if (std::isnan(v)) return;
      if (!t.empty()) t += form == 2 ? " / " : " · ";
      t += word ? tessera::fill(plugin->text(key), "t", degrees(v)) : degrees(v);
    };
    add("nozzle_t", p.nozzle, form < 2);
    add("bed_t", p.bed, form < 2);
    if (form == 0) add("chamber_t", p.chamber, true);
    if (t.empty()) continue;
    if (ui::text_width(t, font) <= w) return t;
  }
  return "";
}

// "Layer 126 of 300", or "126 / 300" where that does not fit.
static std::string layer_text(const tessera::Plugin *plugin, const Printer &p, int w, Font font) {
  if (p.layer < 0) return "";
  char n[12], total[12];
  snprintf(n, sizeof(n), "%ld", p.layer);
  snprintf(total, sizeof(total), "%ld", p.total);
  std::string t = tessera::fill(plugin->text(p.total > 0 ? "layer_of" : "layer_n"), "n", n);
  t = tessera::fill(t.c_str(), "total", total);
  if (ui::text_width(t, font) <= w) return t;
  t = p.total > 0 ? std::string(n) + " / " + total : std::string(n);
  return ui::text_width(t, font) <= w ? t : "";
}

static std::string percent_text(float progress) {
  char t[8];
  snprintf(t, sizeof(t), "%.0f%%", progress);
  return t;
}

// The line under the bar: the time left, or the error's number.
static std::string left_text(const tessera::Plugin *plugin, const Printer &p, uint32_t epoch) {
  if (p.st == St::ERROR && p.err) return tessera::format(plugin->text("error_n"), p.err);
  const long l = p.left_now(epoch);
  if (l < 0) return "";
  return tessera::fill(plugin->text("left"), "t", duration_text(plugin, l));
}

// "Done at 14:32", while it prints and the screen's clock is set.
static std::string eta_text(const tessera::Plugin *plugin, const Printer &p, uint32_t epoch) {
  const long l = p.left_now(epoch);
  if (l <= 0 || p.st != St::PRINTING || epoch == 0) return "";
  return tessera::fill(plugin->text("done_at"), "t", tessera::clock_text(epoch + static_cast<uint32_t>(l)));
}

// ---- A bar: how far along ----

struct Bar {
  lv_obj_t *track{}, *fill{};
  void make(lv_obj_t *parent) {
    track = ui::block(parent, theme::TRACK);
    fill = ui::block(track, theme::ACCENT);
  }
  void hide() { visible(track, false); }
  void place(const Printer &p, int x, int y, int w, int h) {
    visible(track, true);
    put(track, x, y, w, h);
    round_to(track, h / 2);
    ui::set_fill(track, Tone::NORMAL, theme::TRACK);
    const int filled = static_cast<int>(w * p.progress / 100.0f);
    visible(fill, filled > 0);
    put(fill, 0, 0, filled < h ? h : filled, h);
    round_to(fill, h / 2);
    ui::set_fill(fill, p.st == St::PRINTING ? Tone::ACCENT : Tone::BUSY, theme::ACCENT);
  }
};

// ---- The face of the printer: everything a tile shows of it, laid out in the room it gets ----
// The rows, the most important first: the state (with the percent at the right), the file, the bar, the time left and
// when it is done, the temperatures, the layer. Each is laid out only where it fits whole; a tile too low for the rows
// shows the percent large, or the state.

struct Face {
  lv_obj_t *dot{}, *state{}, *pct{}, *title{}, *big{}, *left{}, *eta{}, *temps{}, *layer{}, *note{};
  Bar bar;

  void make(lv_obj_t *parent) {
    dot = ui::block(parent, theme::MUTED);
    state = ui::label(parent, Font::BODY, theme::INK_SOFT);
    pct = ui::label(parent, Font::BODY, theme::INK);
    lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);
    title = ui::label(parent, Font::TITLE, theme::INK);
    big = ui::label(parent, Font::VALUE, theme::INK);
    bar.make(parent);
    left = ui::label(parent, Font::BODY, theme::MUTED);
    eta = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(eta, LV_TEXT_ALIGN_RIGHT, 0);
    temps = ui::label(parent, Font::BODY, theme::INK_SOFT);
    layer = ui::label(parent, Font::BODY, theme::MUTED);
    note = ui::text(parent, Font::BODY, theme::MUTED);
  }

  void hide() {
    for (lv_obj_t *p : {dot, state, pct, title, big, left, eta, temps, layer, note}) visible(p, false);
    bar.hide();
  }

  void message(const char *text, int x, int y, int w, int h) {
    hide();
    visible(note, true);
    ui::set_text(note, text);
    ui::set_color(note, theme::MUTED);
    put(note, x, y, w, h);
  }

  // The state as a dot in its colour and its word; with `with_pct`, the percent at the right of the same line.
  int head(const tessera::Plugin *plugin, const Printer &p, int x, int y, int w, Font font, bool with_pct) {
    const int lh = ui::line_height(font), d = ui::px(8), dx = d + ui::px(6);
    visible(dot, true);
    visible(state, true);
    put(dot, x, y + (lh - d) / 2, d, d);
    round_to(dot, d / 2);
    ui::set_fill(dot, tone_of(p.st), theme::MUTED);
    ui::set_font(state, font);
    ui::set_text(state, plugin->text(word_of(p.st)));
    ui::set_color(state, theme::INK_SOFT);
    int sw = w - dx;
    const bool show_pct = with_pct && p.has_progress();
    visible(pct, show_pct);
    if (show_pct) {
      const std::string t = percent_text(p.progress);
      const int pw = ui::text_width(t, font) + ui::px(4);
      ui::set_font(pct, font);
      ui::set_text(pct, t);
      ui::set_color(pct, theme::INK);
      put(pct, x + w - pw, y, pw, lh);
      sw -= pw + ui::px(4);
    }
    put(state, x + dx, y, sw > 0 ? sw : 1, lh);
    return lh;
  }

  void draw(const tessera::Plugin *plugin, const Printer &p, const char *name, uint32_t epoch, int x, int y, int w,
            int h) {
    if (p.st == St::NONE || p.st == St::TOO_LARGE) {
      message(plugin->text(word_of(p.st)), x, y, w, h);
      return;
    }
    visible(note, false);
    const int gap = ui::px(6), bar_h = ui::px(6);
    const int bh = ui::line_height(Font::BODY), th = ui::line_height(Font::TITLE), vh = ui::line_height(Font::VALUE);
    const bool progress = p.has_progress();
    // The full face needs the state, the file and (while it prints) the bar and the line under it.
    const int full = bh + th + (progress ? gap + bar_h + ui::px(4) + bh : 0);
    if (h < full) {
      compact(plugin, p, name, x, y, w, h);
      return;
    }
    visible(big, false);
    int at = y + head(plugin, p, x, y, w, Font::BODY, true);
    // The file while there is a job (or one just ended), else the printer's name.
    const char *what = p.file[0] && (p.busy() || p.st == St::COMPLETED || p.st == St::STOPPED || p.st == St::ERROR)
                           ? p.file
                           : name;
    visible(title, true);
    ui::set_text(title, what);
    ui::set_color(title, theme::INK);
    put(title, x, at, w, th);
    at += th;
    const std::string lt = left_text(plugin, p, epoch);
    if (progress) {
      bar.place(p, x, at + gap, w, bar_h);
      at += gap + bar_h + ui::px(4);
    } else {
      bar.hide();
    }
    // The time left at the left, when it is done at the right where both fit.
    const bool show_left = !lt.empty() && at + bh <= y + h;
    visible(left, show_left);
    visible(eta, false);
    if (show_left) {
      const int lw = ui::text_width(lt, Font::BODY) + ui::px(4);
      ui::set_text(left, lt);
      ui::set_tone(left, p.st == St::ERROR ? Tone::ALERT : Tone::NORMAL, theme::MUTED);
      put(left, x, at, lw < w ? lw : w, bh);
      const std::string et = eta_text(plugin, p, epoch);
      const int ew = et.empty() ? 0 : ui::text_width(et, Font::BODY) + ui::px(4);
      if (ew && lw + ew + gap <= w) {
        visible(eta, true);
        ui::set_text(eta, et);
        ui::set_color(eta, theme::MUTED);
        put(eta, x + w - ew, at, ew, bh);
      }
      at += bh;
    }
    const std::string tt = temps_text(plugin, p, w, Font::BODY);
    const bool show_temps = !tt.empty() && p.here() && at + gap + bh <= y + h;
    visible(temps, show_temps);
    if (show_temps) {
      ui::set_text(temps, tt);
      ui::set_color(temps, theme::INK_SOFT);
      put(temps, x, at + gap, w, bh);
      at += gap + bh;
    }
    const std::string ly = p.busy() ? layer_text(plugin, p, w, Font::BODY) : std::string();
    const bool show_layer = !ly.empty() && at + bh <= y + h;
    visible(layer, show_layer);
    if (show_layer) {
      ui::set_text(layer, ly);
      ui::set_color(layer, theme::MUTED);
      put(layer, x, at, w, bh);
    }
  }

 private:
  // A small tile: the percent large with the state under it while it prints, else the state with the name under it.
  void compact(const tessera::Plugin *plugin, const Printer &p, const char *name, int x, int y, int w, int h) {
    for (lv_obj_t *o : {pct, left, eta, temps, layer, note}) visible(o, false);
    bar.hide();
    const int bh = ui::line_height(Font::BODY), th = ui::line_height(Font::TITLE);
    if (p.has_progress()) {
      const std::string t = percent_text(p.progress);
      Font f = Font::VALUE;
      if (ui::line_height(f) > h || ui::text_width(t, f) > w) f = Font::TITLE;
      const int fh = ui::line_height(f);
      visible(title, false);
      visible(big, true);
      ui::set_font(big, f);
      ui::set_text(big, t);
      ui::set_tone(big, p.st == St::PAUSED ? Tone::BUSY : Tone::NORMAL, theme::INK);
      const bool under = fh + bh <= h;
      const int top = y + (h - fh - (under ? bh : 0)) / 2;
      put(big, x, top, w, fh);
      if (under) head(plugin, p, x, top + fh, w, Font::BODY, false);
      else {
        visible(dot, false);
        visible(state, false);
      }
      return;
    }
    visible(big, false);
    // The state in the title's font, the name under it where there is room.
    const bool under = th + bh <= h;
    const int top = y + (h - th - (under ? bh : 0)) / 2;
    head(plugin, p, x, top, w, th <= h ? Font::TITLE : Font::BODY, false);
    visible(title, under);
    if (under) {
      ui::set_font(title, Font::BODY);
      ui::set_text(title, name);
      ui::set_color(title, theme::MUTED);
      put(title, x, top + th, w, bh);
    }
  }
};

// The label a tile shows for the printer: the tile's own name, the entity's, or "3D printer".
static void name_of(const tessera::Plugin *plugin, const char *given, const char *entity_name, char *out, size_t size) {
  copy_to(out, size, given[0] ? given : entity_name[0] ? entity_name : plugin->text("name_fallback"));
}

// What the keys of the card act on, from a tile's options.
static void setup_from(CardSetup &s, JsonObjectConst options) {
  copy_to(s.light, sizeof(s.light), options["light"] | "");
  copy_to(s.pause, sizeof(s.pause), options["pause"] | "");
  copy_to(s.resume, sizeof(s.resume), options["resume"] | "");
  copy_to(s.stop, sizeof(s.stop), options["stop"] | "");
}

// ---- The printer tile: its entity is the integration's Print Status sensor ----

class PrinterTile : public tessera::Tile {
 public:
  explicit PrinterTile(Creality *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    copy_to(entity_, sizeof(entity_), c.entity);
    copy_to(given_, sizeof(given_), c.name);
    setup_from(setup_, c.options);
    face_.make(c.parent);
  }

  void on_state(JsonObjectConst data) override {
    parse_extras(data["entities"], printer_);
    parse_status(data, printer_);
    name_of(plugin_, given_, printer_.name, name_, sizeof(name_));
  }

  void on_tick(uint32_t epoch) override { layout(epoch); }
  void on_theme() override { layout(tessera::epoch()); }

  void on_tap() override {
    if (printer_.st == St::NONE) return;
    plugin_->card_setup = setup_;
    tessera::open_card(plugin_->plugin_id(), "control", entity_, tile_, given_[0] ? given_ : plugin_->text("name_fallback"));
  }

 private:
  void layout(uint32_t epoch) {
    if (!entity_[0]) {
      face_.message(plugin_->text("choose_status"), 0, 0, width_, height_);
      return;
    }
    face_.draw(plugin_, printer_, name_, epoch, 0, 0, width_, height_);
  }

  Creality *plugin_;
  CardSetup setup_;
  int width_ = 0, height_ = 0, tile_ = -1;
  char entity_[96] = "";
  char given_[84] = "";
  char name_[84] = "";
  Printer printer_;
  Face face_;
};

// ---- The camera tile: its entity is the printer's camera; the status sensor is an option ----
// The picture on top, a line under it of how the print goes. Nothing lies over the picture, so it may stream.

class CameraTile : public tessera::Tile {
 public:
  explicit CameraTile(Creality *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    copy_to(entity_, sizeof(entity_), c.entity);
    copy_to(given_, sizeof(given_), c.name);
    copy_to(status_, sizeof(status_), c.options["status"] | "");
    setup_from(setup_, c.options);
    setup_.camera = true;
    if (status_[0]) copy_to(setup_.status, sizeof(setup_.status), "status");
    // The picture's backdrop, under it: an icon, and a word when the camera has no picture.
    back_ = ui::block(c.parent, theme::TRACK);
    back_icon_ = ui::label(back_, Font::ICON, theme::MUTED);
    lv_obj_set_style_text_align(back_icon_, LV_TEXT_ALIGN_CENTER, 0);
    ui::set_text(back_icon_, ui::icon(ICON_CAMERA));
    back_word_ = ui::label(back_, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(back_word_, LV_TEXT_ALIGN_CENTER, 0);
    if (ui::pictures() && entity_[0]) {
      place_ = ui::picture(c.parent, tessera::Placeholder::NONE);
      if (c.options["live"] | false) ui::live(place_, true);
    }
    bar_.make(c.parent);
    dot_ = ui::block(c.parent, theme::MUTED);
    line_ = ui::label(c.parent, Font::BODY, theme::INK_SOFT);
    right_ = ui::label(c.parent, Font::BODY, theme::INK);
    lv_obj_set_style_text_align(right_, LV_TEXT_ALIGN_RIGHT, 0);
    // A screen without pictures shows the printer's face instead.
    face_.make(c.parent);
  }

  void on_state(JsonObjectConst data) override {
    JsonObjectConst entities = data["entities"];
    parse_extras(entities, printer_);
    parse_status(entities["status"], printer_);
    copy_to(camera_name_, sizeof(camera_name_), data["name"] | "");
    name_of(plugin_, given_, camera_name_, name_, sizeof(name_));
    if (place_) ui::set_picture(place_, std::string("camera:") + entity_, tessera::Fit::COVER);
  }

  void on_picture(lv_obj_t *place, tessera::PictureState state) override {
    if (place != place_) return;
    no_picture_ = state != tessera::PictureState::SHOWN;
    layout(tessera::epoch());
  }

  void on_tick(uint32_t epoch) override { layout(epoch); }
  void on_theme() override { layout(tessera::epoch()); }

  void on_tap() override {
    if (!entity_[0]) return;
    plugin_->card_setup = setup_;
    tessera::open_card(plugin_->plugin_id(), "control", entity_, tile_, given_[0] ? given_ : plugin_->text("name_fallback"));
  }

 private:
  void hide_strip() {
    for (lv_obj_t *p : {dot_, line_, right_}) visible(p, false);
    bar_.hide();
  }

  void layout(uint32_t epoch) {
    if (!entity_[0]) {
      visible(back_, false);
      hide_strip();
      face_.message(plugin_->text("choose_camera"), 0, 0, width_, height_);
      return;
    }
    if (!place_) {
      // No pictures on this screen (the CYD): the printer's face, or what to choose for it.
      visible(back_, false);
      hide_strip();
      if (!status_[0]) face_.message(plugin_->text("no_pictures"), 0, 0, width_, height_);
      else face_.draw(plugin_, printer_, name_, epoch, 0, 0, width_, height_);
      return;
    }
    face_.hide();
    const int gap = ui::px(6), bh = ui::line_height(Font::BODY), bar_h = ui::px(4);
    const bool progress = status_[0] && printer_.has_progress();
    // The line under the picture, where it leaves the picture at least half the tile.
    const int strip = gap + bh + (progress ? bar_h + ui::px(4) : 0);
    const bool with_strip = height_ - strip >= height_ / 2 && height_ - strip >= ui::mm(10);
    // A wide, low tile: the picture at the left, 4:3, and the printer's face beside it.
    const int side_w = height_ * 4 / 3, side_gap = ui::px(10);
    const bool beside = !with_strip && status_[0] && width_ - side_w - side_gap >= ui::mm(22);
    const int pw = beside ? side_w : width_;
    const int ph = with_strip ? height_ - strip : height_;
    visible(back_, true);
    put(back_, 0, 0, pw, ph);
    ui::set_fill(back_, Tone::NORMAL, theme::TRACK);
    const int ih = ui::line_height(Font::ICON);
    const bool word = no_picture_ && ph >= ih + bh + gap;
    const int iy = (ph - ih - (word ? bh : 0)) / 2;
    put(back_icon_, 0, iy, pw, ih);
    ui::set_color(back_icon_, theme::MUTED);
    visible(back_word_, word);
    if (word) {
      ui::set_text(back_word_, plugin_->text("no_picture"));
      ui::set_color(back_word_, theme::MUTED);
      put(back_word_, 0, iy + ih, pw, bh);
    }
    put(place_, 0, 0, pw, ph);
    if (beside) {
      hide_strip();
      face_.draw(plugin_, printer_, name_, epoch, pw + side_gap, 0, width_ - pw - side_gap, height_);
      return;
    }
    if (!with_strip) {
      hide_strip();
      return;
    }
    int y = ph + gap;
    if (progress) {
      bar_.place(printer_, 0, y, width_, bar_h);
      y += bar_h + ui::px(4);
    } else {
      bar_.hide();
    }
    // Without a status sensor, the camera's name; with one, the state and at the right the percent or the time left.
    const int d = ui::px(8), dx = status_[0] ? d + ui::px(6) : 0;
    visible(dot_, status_[0] != 0);
    if (status_[0]) {
      put(dot_, 0, y + (bh - d) / 2, d, d);
      round_to(dot_, d / 2);
      ui::set_fill(dot_, tone_of(printer_.st), theme::MUTED);
    }
    std::string right;
    if (progress) {
      right = percent_text(printer_.progress);
      const long l = printer_.left_now(epoch);
      if (l >= 0) {
        const std::string both = right + " · " + duration_text(plugin_, l);
        if (ui::text_width(both, Font::BODY) <= width_ / 2) right = both;
      }
    }
    const int rw = right.empty() ? 0 : ui::text_width(right, Font::BODY) + ui::px(4);
    visible(right_, rw > 0);
    if (rw) {
      ui::set_text(right_, right);
      ui::set_color(right_, theme::INK);
      put(right_, width_ - rw, y, rw, bh);
    }
    visible(line_, true);
    ui::set_text(line_, status_[0] ? plugin_->text(word_of(printer_.st)) : name_);
    ui::set_color(line_, theme::INK_SOFT);
    const int lw = width_ - dx - (rw ? rw + ui::px(4) : 0);
    put(line_, dx, y, lw > 0 ? lw : 1, bh);
  }

  Creality *plugin_;
  CardSetup setup_;
  int width_ = 0, height_ = 0, tile_ = -1;
  char entity_[96] = "";
  char status_[96] = "";
  char given_[84] = "";
  char camera_name_[84] = "";
  char name_[84] = "";
  bool no_picture_ = false;
  Printer printer_;
  lv_obj_t *back_{}, *back_icon_{}, *back_word_{}, *place_{}, *dot_{}, *line_{}, *right_{};
  Bar bar_;
  Face face_;
};

// ---- A key of the card: a rounded block with an icon in the middle ----

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
  void place(int kx, int ky, int kw, int kh, uint32_t icon, Tone tone) {
    shown = true;
    x = kx;
    y = ky;
    w = kw;
    h = kh;
    visible(block, true);
    put(block, kx, ky, kw, kh);
    ui::set_fill(block, tone, theme::TRACK);
    ui::set_text(label, ui::icon(icon));
    if (tone == Tone::NORMAL) ui::set_color(label, theme::INK);
    else ui::set_color(label, theme::ON_ACCENT);
    const int lh = ui::line_height(Font::ICON);
    put(label, 0, (kh - lh) / 2, kw, lh);
  }
  bool hit(const tessera::Point &p) const { return shown && p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
};

// ---- The control card: the printer large, and keys ----
// Pause or resume, stop (a second tap within five seconds confirms it), and the light. Each key shows only when its
// entity was chosen in the tile's inspector and the printer is in a state where it does something.

class ControlCard : public tessera::Card {
 public:
  enum KeyId { PLAY, STOP, LIGHT, KEYS };
  explicit ControlCard(Creality *plugin) : plugin_(plugin), setup_(plugin->card_setup) {}

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    height_ = c.height;
    copy_to(entity_, sizeof(entity_), c.entity);
    if (setup_.camera && ui::pictures() && entity_[0]) {
      back_ = ui::block(c.parent, theme::TRACK);
      back_icon_ = ui::label(back_, Font::ICON, theme::MUTED);
      lv_obj_set_style_text_align(back_icon_, LV_TEXT_ALIGN_CENTER, 0);
      ui::set_text(back_icon_, ui::icon(ICON_CAMERA));
      place_ = ui::picture(c.parent, tessera::Placeholder::NONE);
    }
    dot_ = ui::block(c.parent, theme::MUTED);
    state_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK_SOFT);
    pct_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
    lv_obj_set_style_text_align(pct_, LV_TEXT_ALIGN_RIGHT, 0);
    title_ = ui::label(c.parent, Font::HEADLINE, theme::INK);
    bar_.make(c.parent);
    left_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    eta_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(eta_, LV_TEXT_ALIGN_RIGHT, 0);
    temps_ = ui::label(c.parent, Font::BODY, theme::INK_SOFT);
    layer_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    for (Key &k : keys_) k.make(c.parent);
    note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
  }

  void on_state(JsonObjectConst data) override {
    JsonObjectConst entities = data["entities"];
    parse_extras(entities, printer_);
    parse_status(setup_.status[0] ? entities[setup_.status].as<JsonObjectConst>() : data, printer_);
    if (place_) ui::set_picture(place_, std::string("camera:") + entity_, tessera::Fit::COVER);
    layout(tessera::epoch());
  }

  void on_tick(uint32_t epoch) override {
    if (note_left_ && --note_left_ == 0) note_text_[0] = 0;
    if (armed_ && --armed_ == 0) note_text_[0] = 0;
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
  void say(const char *key, uint8_t seconds) {
    copy_to(note_text_, sizeof(note_text_), plugin_->text(key));
    note_left_ = seconds;
  }

  void press(KeyId key) {
    bool sent = true;
    switch (key) {
      case PLAY:
        armed_ = 0;
        if (printer_.st == St::PAUSED) sent = tessera::action("button.press", setup_.resume);
        else sent = tessera::action("button.press", setup_.pause);
        if (sent) say("sent", 3);
        break;
      case STOP:
        if (!armed_) {
          armed_ = 5;
          copy_to(note_text_, sizeof(note_text_), plugin_->text("stop_confirm"));
          note_left_ = 0;
          break;
        }
        armed_ = 0;
        sent = tessera::action("button.press", setup_.stop);
        if (sent) say("stop_sent", 4);
        break;
      case LIGHT:
        sent = tessera::action(strncmp(setup_.light, "switch.", 7) == 0 ? "switch.toggle" : "light.toggle",
                               setup_.light);
        break;
      default:
        return;
    }
    if (!sent) say("not_sent", 4);
    layout(tessera::epoch());
  }

  void layout(uint32_t epoch) {
    const int gap = ui::px(10), small_gap = ui::px(4);
    const int bh = ui::line_height(Font::BODY), lh = ui::line_height(Font::BODY_LARGE);
    const Printer &p = printer_;
    // The keys in one row at the bottom, above the note.
    bool want[KEYS] = {};
    want[PLAY] = p.st == St::PAUSED ? setup_.resume[0] != 0
                                    : (p.st == St::PRINTING || p.st == St::PROCESSING || p.st == St::TESTING) &&
                                          setup_.pause[0] != 0;
    want[STOP] = p.busy() && setup_.stop[0] != 0;
    want[LIGHT] = setup_.light[0] != 0 && p.st != St::NONE && p.st != St::OFF;
    if (!want[STOP]) armed_ = 0;
    int count = 0;
    for (bool k : want) count += k;
    const int kh = ui::large() ? ui::mm(12) : ui::mm(9);
    const int keys_top = height_ - bh - small_gap - (count ? kh : 0);
    const int room = keys_top - (count ? gap : 0);
    // The camera, 4:3, as large as it can be: at the left with the words beside it (a wide card), or on top with the
    // words under it (a square or tall one), whichever gives the larger picture.
    int pw = 0, ph = 0, px = 0;
    bool on_top = false;
    if (place_) {
      const int words = lh + ui::line_height(Font::TITLE) + gap + ui::px(8) + small_gap + bh;
      int sw = room * 4 / 3;
      if (sw > width_ * 11 / 20) sw = width_ * 11 / 20;
      if (width_ - sw - gap * 2 < ui::mm(30)) sw = 0;
      int th = room - words - gap;
      int tw = th > 0 ? th * 4 / 3 : 0;
      if (tw > width_) tw = width_;
      th = tw * 3 / 4;
      if (th < ui::mm(12)) tw = th = 0;
      // A landscape card keeps the words beside the camera; a square or tall one puts them under it.
      const bool landscape = sw > 0 && width_ * 3 > room * 4;
      if (!landscape && tw * th > sw * (sw * 3 / 4)) {
        on_top = true;
        pw = tw;
        ph = th;
        px = (width_ - tw) / 2;
      } else if (sw > 0) {
        pw = sw;
        ph = sw * 3 / 4;
      }
    }
    visible(back_, pw > 0);
    visible(place_, pw > 0);
    if (pw > 0) {
      put(back_, px, 0, pw, ph);
      ui::set_fill(back_, Tone::NORMAL, theme::TRACK);
      const int ih = ui::line_height(Font::ICON);
      put(back_icon_, 0, (ph - ih) / 2, pw, ih);
      ui::set_color(back_icon_, theme::MUTED);
      put(place_, px, 0, pw, ph);
    }
    const int x = pw && !on_top ? pw + gap * 2 : 0;
    const int top = pw && on_top ? ph + gap : 0;
    const int w = width_ - x;
    // The words above the keys, each only where it ends above them, the most important first.
    const bool progress = p.has_progress();
    const std::string lt = p.here() || p.st == St::ERROR ? left_text(plugin_, p, epoch) : std::string();
    const std::string tt = p.here() ? temps_text(plugin_, p, w, Font::BODY) : std::string();
    const std::string ly = p.busy() ? layer_text(plugin_, p, w, Font::BODY) : std::string();
    const int bar_h = ui::px(8);
    const int rest = (progress ? gap + bar_h : 0) + (!lt.empty() ? small_gap + bh : 0) + (!tt.empty() ? gap + bh : 0) +
                     (!ly.empty() ? small_gap + bh : 0);
    const char *what = p.file[0] && (p.busy() || p.st == St::COMPLETED || p.st == St::STOPPED || p.st == St::ERROR)
                           ? p.file
                           : p.st == St::IDLE ? plugin_->text("ready") : "";
    // The title large where it fits in height and in width, else a step smaller.
    Font title_font = Font::HEADLINE;
    if (top + lh + ui::line_height(Font::HEADLINE) + rest > room || ui::text_width(what, Font::HEADLINE) > w)
      title_font = ui::text_width(what, Font::TITLE) <= w ? Font::TITLE : Font::BODY_LARGE;
    const int th = ui::line_height(title_font);
    int y = top;
    // The state, with the percent at its right.
    const bool show_head = y + lh <= room;
    visible(dot_, show_head);
    visible(state_, show_head);
    visible(pct_, show_head && progress);
    if (show_head) {
      const int d = ui::px(10), dx = d + ui::px(8);
      put(dot_, x, y + (lh - d) / 2, d, d);
      round_to(dot_, d / 2);
      ui::set_fill(dot_, tone_of(p.st), theme::MUTED);
      ui::set_text(state_, plugin_->text(word_of(p.st)));
      ui::set_color(state_, theme::INK_SOFT);
      int sw = w - dx;
      if (progress) {
        const std::string t = percent_text(p.progress);
        const int tw = ui::text_width(t, Font::BODY_LARGE) + ui::px(4);
        ui::set_text(pct_, t);
        ui::set_color(pct_, theme::INK);
        put(pct_, x + w - tw, y, tw, lh);
        sw -= tw + ui::px(4);
      }
      put(state_, x + dx, y, sw > 0 ? sw : 1, lh);
      y += lh;
    }
    const bool show_title = what[0] && y + th <= room;
    visible(title_, show_title);
    if (show_title) {
      ui::set_font(title_, title_font);
      ui::set_text(title_, what);
      ui::set_color(title_, theme::INK);
      put(title_, x, y, w, th);
      y += th;
    }
    if (progress && y + gap + bar_h <= room) {
      bar_.place(p, x, y + gap, w, bar_h);
      y += gap + bar_h;
    } else {
      bar_.hide();
    }
    const bool show_left = !lt.empty() && y + small_gap + bh <= room;
    visible(left_, show_left);
    visible(eta_, false);
    if (show_left) {
      const int lw = ui::text_width(lt, Font::BODY) + ui::px(4);
      ui::set_text(left_, lt);
      ui::set_tone(left_, p.st == St::ERROR ? Tone::ALERT : Tone::NORMAL, theme::MUTED);
      put(left_, x, y + small_gap, lw < w ? lw : w, bh);
      const std::string et = eta_text(plugin_, p, epoch);
      const int ew = et.empty() ? 0 : ui::text_width(et, Font::BODY) + ui::px(4);
      if (ew && lw + ew + gap <= w) {
        visible(eta_, true);
        ui::set_text(eta_, et);
        ui::set_color(eta_, theme::MUTED);
        put(eta_, x + w - ew, y + small_gap, ew, bh);
      }
      y += small_gap + bh;
    }
    const bool show_temps = !tt.empty() && y + gap + bh <= room;
    visible(temps_, show_temps);
    if (show_temps) {
      ui::set_text(temps_, tt);
      ui::set_color(temps_, theme::INK_SOFT);
      put(temps_, x, y + gap, w, bh);
      y += gap + bh;
    }
    const bool show_layer = !ly.empty() && y + small_gap + bh <= room;
    visible(layer_, show_layer);
    if (show_layer) {
      ui::set_text(layer_, ly);
      ui::set_color(layer_, theme::MUTED);
      put(layer_, x, y + small_gap, w, bh);
    }
    // The keys.
    for (Key &k : keys_) k.hide();
    if (count) {
      const int kw = (width_ - gap * (count - 1)) / count;
      int kx = 0;
      for (int i = 0; i < KEYS; i++) {
        if (!want[i]) continue;
        Key &k = keys_[i];
        switch (i) {
          case PLAY: k.place(kx, keys_top, kw, kh, p.st == St::PAUSED ? ICON_PLAY : ICON_PAUSE, Tone::ACCENT); break;
          case STOP: k.place(kx, keys_top, kw, kh, ICON_STOP, armed_ ? Tone::ALERT : Tone::NORMAL); break;
          case LIGHT:
            k.place(kx, keys_top, kw, kh, p.light == 0 ? ICON_LIGHT_OFF : ICON_LIGHT,
                    p.light == 1 ? Tone::ACCENT : Tone::NORMAL);
            break;
          default: break;
        }
        kx += kw + gap;
      }
    }
    // Under the keys: what a tap did, or what to choose when the card has nothing to show.
    const char *note = note_text_[0]                  ? note_text_
                       : p.st == St::NONE && setup_.camera && !setup_.status[0] ? plugin_->text("choose_status")
                       : !count && !setup_.pause[0] && !setup_.stop[0] && !setup_.light[0] ? plugin_->text("choose_keys")
                                                                                           : "";
    ui::set_text(note_, note);
    ui::set_tone(note_, armed_ ? Tone::ALERT : Tone::NORMAL, theme::MUTED);
    put(note_, 0, height_ - bh, width_, bh);
  }

  Creality *plugin_;
  CardSetup setup_;
  int width_ = 0, height_ = 0;
  char entity_[96] = "";
  Printer printer_;
  lv_obj_t *back_{}, *back_icon_{}, *place_{}, *dot_{}, *state_{}, *pct_{}, *title_{}, *left_{}, *eta_{}, *temps_{},
      *layer_{}, *note_{};
  Bar bar_;
  Key keys_[KEYS];
  char note_text_[72] = "";
  uint8_t note_left_ = 0, armed_ = 0;
};

void Creality::setup() {
  add_tile("printer", [this]() { return new PrinterTile(this); });
  add_tile("camera", [this]() { return new CameraTile(this); });
  add_card("control", [this]() { return new ControlCard(this); }, true);
}

}  // namespace esphome::creality
