#include "proxmox.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::proxmox {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

// What a state of a status entity means. A binary sensor or a switch is up when on, unless its device class is
// "problem" (on means something is wrong); a sensor says it in words (Proxmox: running, stopped; Uptime Kuma: up,
// down; Proxmox Extended Sensors: a ZFS pool ONLINE or DEGRADED, a backup healthy or critical, a cluster quorate).
enum class Health : uint8_t { NONE, UP, DOWN, PENDING, UNKNOWN };

static bool is_one_of(const char *s, std::initializer_list<const char *> words) {
  for (const char *w : words)
    if (strcasecmp(s, w) == 0) return true;
  return false;
}

static Health health_of(JsonObjectConst entity) {
  if (entity.isNull() || entity["wait"].is<const char *>()) return Health::NONE;
  const char *state = entity["state"] | "";
  const char *kind = entity["attributes"]["device_class"] | "";
  const bool problem = strcmp(kind, "problem") == 0;
  if (is_one_of(state, {"unavailable", "unknown", ""})) return Health::UNKNOWN;
  if (is_one_of(state, {"on", "running", "online", "up", "true", "connected", "ok", "healthy", "quorate", "active", "verified ok"})) return problem ? Health::DOWN : Health::UP;
  if (is_one_of(state, {"off", "stopped", "offline", "down", "false", "disconnected", "error", "problem", "faulted", "critical", "no quorum", "verification failed"}))
    return problem ? Health::UP : Health::DOWN;
  if (is_one_of(state, {"pending", "paused", "maintenance", "starting", "stopping", "suspended", "degraded", "warning"})) return Health::PENDING;
  return Health::UNKNOWN;
}

static Tone tone_of(Health h) {
  switch (h) {
    case Health::UP: return Tone::GOOD;
    case Health::DOWN: return Tone::ALERT;
    case Health::PENDING: return Tone::BUSY;
    default: return Tone::NORMAL;
  }
}

static const char *word_of(Health h) {
  switch (h) {
    case Health::UP: return "online";
    case Health::DOWN: return "offline";
    case Health::PENDING: return "pending";
    default: return "unknown";
  }
}

static void visible(lv_obj_t *part, bool on) {
  if (on) lv_obj_clear_flag(part, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
}

static void copy_to(char *to, size_t size, const char *from) {
  snprintf(to, size, "%s", from ? from : "");
}

static constexpr int NAMES = 6;

// "Docker, NAS" into up to six names; returns how many names there were in all. A list as ESPHome hands an attribute
// over, "['Docker', 'NAS']", reads the same.
static bool is_edge(char c) { return c == ' ' || c == '[' || c == ']' || c == '\'' || c == '"'; }
static int split_names(const char *text, char out[][40], int room) {
  int count = 0;
  const char *p = text;
  while (p && *p) {
    while (*p && (is_edge(*p) || *p == ',' || *p == ';')) p++;
    if (!*p) break;
    const char *end = p;
    while (*end && *end != ',' && *end != ';') end++;
    const char *last = end;
    while (last > p && is_edge(last[-1])) last--;
    if (last > p) {
      if (count < room) snprintf(out[count], 40, "%.*s", static_cast<int>(last - p), p);
      count++;
    }
    p = end;
  }
  return count;
}


// "arco-(124) Status" becomes "Arco": the words an integration adds to an entity's name say nothing on a list of
// statuses. A trailing "Status", a VM's number in brackets and loose dashes go; the first letter is made capital.
static void clean_name(char *name) {
  char out[40];
  size_t n = 0;
  for (const char *p = name; *p && n + 1 < sizeof(out); p++) {
    if (*p == '(') {  // "(124)"
      const char *q = p + 1;
      while (*q >= '0' && *q <= '9') q++;
      if (*q == ')' && q > p + 1) {
        p = q;
        continue;
      }
    }
    out[n++] = *p;
  }
  out[n] = 0;
  // Words at the end that only say what kind of entity it is.
  for (const char *word : {" Status", " status", " Estado", " estado", " State", " state"}) {
    const size_t len = strlen(word), have = strlen(out);
    if (have > len && strcmp(out + have - len, word) == 0) out[have - len] = 0;
  }
  // Dashes, colons and spaces left at either end.
  size_t start = 0, end = strlen(out);
  while (start < end && (out[start] == ' ' || out[start] == '-' || out[start] == '_' || out[start] == ':')) start++;
  while (end > start && (out[end - 1] == ' ' || out[end - 1] == '-' || out[end - 1] == '_' || out[end - 1] == ':')) end--;
  if (end == start) return;  // nothing would be left: keep the name as it was
  out[end] = 0;
  if (out[start] >= 'a' && out[start] <= 'z') out[start] = static_cast<char>(out[start] - 'a' + 'A');
  snprintf(name, 40, "%s", out + start);
}

// ---- What a node or guest is: its status and three measures, read from the entities chosen for the tile ----

struct Measure {
  bool there = false, number = false, percent = false;
  float level = 0;
  char text[24] = "";
};

struct NodeData {
  Health health = Health::NONE;
  char name[48] = "";
  Measure measures[3];  // cpu, memory, disk
  bool any = false;
};

static const char *const MEASURE_IDS[3] = {"cpu", "memory", "disk"};

static void parse_node(JsonObjectConst data, NodeData &n) {
  JsonObjectConst entities = data["entities"];
  JsonObjectConst status = entities["status"];
  n.health = health_of(status);
  copy_to(n.name, sizeof(n.name), n.health != Health::NONE ? (status["name"] | "") : "");
  n.any = n.health != Health::NONE;
  for (int i = 0; i < 3; i++) {
    Measure &m = n.measures[i];
    JsonObjectConst e = entities[MEASURE_IDS[i]];
    m.there = !e.isNull() && !e["wait"].is<const char *>();
    if (!m.there) continue;
    n.any = true;
    const char *state = e["state"] | "";
    const char *unit = e["attributes"]["unit_of_measurement"] | "";
    char *end = nullptr;
    const float v = strtof(state, &end);
    m.number = end && end != state;
    m.percent = m.number && strcmp(unit, "%") == 0;
    m.level = m.number ? (v < 0 ? 0 : v > 100 && m.percent ? 100 : v) : 0;
    if (!m.number) copy_to(m.text, sizeof(m.text), "-");
    else if (m.percent) snprintf(m.text, sizeof(m.text), v < 10 ? "%.1f%%" : "%.0f%%", v);
    else snprintf(m.text, sizeof(m.text), v < 10 ? "%.1f %s" : "%.0f %s", v, unit);
  }
}

static Tone level_tone(float level) { return level >= 90 ? Tone::ALERT : level >= 75 ? Tone::BUSY : Tone::GOOD; }

// A row of a measure: its name and value on a line, and a bar under it for a percentage. Made once, laid out on every
// tick, so the tile and the card draw the same rows at their own sizes.
struct MeasureRow {
  lv_obj_t *name{}, *value{}, *track{}, *fill{};

  void make(lv_obj_t *parent, Font font) {
    name = ui::label(parent, font, theme::MUTED);
    value = ui::label(parent, font, theme::INK);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    track = ui::block(parent, theme::TRACK);
    fill = ui::block(track, theme::ACCENT);
  }
  void hide() {
    visible(name, false);
    visible(value, false);
    visible(track, false);
  }
  int height(const Measure &m, Font font, int bar) const {
    return ui::line_height(font) + (m.percent ? bar + ui::px(2) : 0);
  }
  // Lays the row out at y and returns its height.
  int place(const Measure &m, const char *label, Font font, int width, int y, int bar) {
    const int lh = ui::line_height(font);
    visible(name, true);
    visible(value, true);
    ui::set_text(name, label);
    ui::set_text(value, m.text);
    const int vw = width / 2;
    lv_obj_set_size(name, width - vw, lh);
    lv_obj_set_pos(name, 0, y);
    lv_obj_set_size(value, vw, lh);
    lv_obj_set_pos(value, width - vw, y);
    visible(track, m.percent);
    if (!m.percent) return lh;
    const int ty = y + lh + ui::px(2);
    lv_obj_set_size(track, width, bar);
    lv_obj_set_pos(track, 0, ty);
    const int filled = static_cast<int>(width * m.level / 100.0f);
    visible(fill, filled > 0);
    lv_obj_set_size(fill, filled < bar ? bar : filled, bar);
    lv_obj_set_pos(fill, 0, 0);
    ui::set_fill(fill, level_tone(m.level), theme::ACCENT);
    return lh + ui::px(2) + bar;
  }
  void paint(const Measure &m) {
    ui::set_color(name, theme::MUTED);
    ui::set_color(value, theme::INK);
    lv_obj_set_style_bg_color(track, ui::color(theme::TRACK), 0);
    ui::set_fill(fill, level_tone(m.level), theme::ACCENT);
  }
};

// ---- The node tile: a title with a dot, a status line, and rows of CPU, memory and disk. A tap opens its card ----

class NodeTile : public tessera::Tile {
 public:
  explicit NodeTile(Proxmox *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    copy_to(title_text_, sizeof(title_text_), c.name);
    // The keys of the card: the button entities chosen for the tile, kept to hand to the card on a tap.
    static const char *const KEYS[3] = {"start", "shutdown", "reboot"};
    for (int i = 0; i < 3; i++) copy_to(setup_.keys[i], sizeof(setup_.keys[i]), c.options[KEYS[i]] | "");
    copy_to(setup_.cpu, sizeof(setup_.cpu), c.options["cpu"] | "");
    copy_to(setup_.memory, sizeof(setup_.memory), c.options["memory"] | "");
    dot_ = ui::block(c.parent, theme::MUTED);
    title_ = ui::label(c.parent, Font::TITLE, theme::INK);
    status_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    for (MeasureRow &r : rows_) r.make(c.parent, Font::BODY);
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
  }

  void on_state(JsonObjectConst data) override { parse_node(data, node_); }

  void on_tick(uint32_t) override {
    if (!node_.any) {
      for (lv_obj_t *p : {dot_, title_, status_}) visible(p, false);
      for (MeasureRow &r : rows_) r.hide();
      visible(note_, true);
      ui::set_text(note_, plugin_->text("choose"));
      lv_obj_set_size(note_, width_, height_);
      lv_obj_set_pos(note_, 0, 0);
      return;
    }
    visible(note_, false);
    const int gap = ui::px(6);
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY);
    const int d = ui::px(10);
    // The title, with the dot before it when there is a status.
    const bool has_status = node_.health != Health::NONE;
    visible(dot_, has_status);
    int x = 0;
    if (has_status) {
      lv_obj_set_size(dot_, d, d);
      lv_obj_set_style_radius(dot_, d / 2, 0);
      lv_obj_set_pos(dot_, 0, (th - d) / 2);
      ui::set_fill(dot_, tone_of(node_.health), theme::MUTED);
      x = d + gap;
    }
    visible(title_, true);
    ui::set_text(title_, title());
    lv_obj_set_size(title_, width_ - x, th);
    lv_obj_set_pos(title_, x, 0);
    // Every chosen row should fit: the roomiest of these layouts that does is taken. First with the status line, a
    // gap and a bar under each value; then tighter; then without bars (the value alone); then without the status line
    // (the dot beside the title still says it).
    struct Fit { int gap, bar; bool status; };
    const Fit fits[] = {{ui::px(6), ui::px(6), true}, {ui::px(3), ui::px(4), true}, {ui::px(2), 0, true},
                        {ui::px(2), 0, false}};
    Fit fit = fits[3];
    for (const Fit &f : fits) {
      int need = th + (has_status && f.status ? bh : 0);
      for (int i = 0; i < 3; i++)
        if (node_.measures[i].there) need += f.gap + bh + (node_.measures[i].percent && f.bar ? f.bar + ui::px(2) : 0);
      if (need <= height_) {
        fit = f;
        break;
      }
    }
    int y = th;
    const bool status_line = has_status && fit.status && y + bh <= height_;
    visible(status_, status_line);
    if (status_line) {
      ui::set_text(status_, plugin_->text(word_of(node_.health)));
      ui::set_tone(status_, tone_of(node_.health), theme::MUTED);
      lv_obj_set_size(status_, width_, bh);
      lv_obj_set_pos(status_, 0, y);
      y += bh;
    }
    for (int i = 0; i < 3; i++) {
      Measure m = node_.measures[i];
      if (!fit.bar) m.percent = false;  // no room for bars: the value alone
      if (!m.there || y + fit.gap + rows_[i].height(m, Font::BODY, fit.bar) > height_) {
        rows_[i].hide();
        continue;
      }
      y += fit.gap;
      y += rows_[i].place(m, plugin_->text(MEASURE_IDS[i]), Font::BODY, width_, y, fit.bar);
    }
  }

  void on_theme() override {
    ui::set_color(title_, theme::INK);
    ui::set_tone(status_, tone_of(node_.health), theme::MUTED);
    ui::set_fill(dot_, tone_of(node_.health), theme::MUTED);
    ui::set_color(note_, theme::MUTED);
    for (int i = 0; i < 3; i++) rows_[i].paint(node_.measures[i]);
  }

  void on_tap() override {
    if (!node_.any) return;
    plugin_->card_setup = setup_;
    tessera::open_card(plugin_->plugin_id(), "control", "", tile_, title());
  }

 private:
  const char *title() const {
    if (title_text_[0]) return title_text_;
    if (node_.name[0]) return node_.name;
    return plugin_->text("name_fallback");
  }

  Proxmox *plugin_;
  int width_ = 0, height_ = 0, tile_ = -1;
  char title_text_[48] = "";
  CardSetup setup_;
  NodeData node_;
  lv_obj_t *dot_{}, *title_{}, *status_{}, *note_{};
  MeasureRow rows_[3];
};

// ---- The control card: the node large, its last 24 hours, and keys that start, shut down or restart it ----
// The history is Home Assistant's long-term statistics: one mean per hour (recorder/statistics_during_period), so a
// day is 24 numbers whatever the sensor's rate. A tap on the chart switches between CPU and memory.
// A key asks once more before it acts: the first tap turns it red and says "Tap again"; a second tap within five
// seconds presses the button entity in Home Assistant. Five seconds without a tap and it goes back.

// A moment as Home Assistant reads it: "2026-11-06T13:00:00Z".
static void iso_utc(uint32_t epoch, char *out, size_t size) {
  long days = static_cast<long>(epoch / 86400);
  const uint32_t rest = epoch % 86400;
  // civil_from_days (Howard Hinnant)
  days += 719468;
  const long era = days / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  const long y = static_cast<long>(yoe) + era * 400 + (m <= 2);
  snprintf(out, size, "%04ld-%02u-%02uT%02u:%02u:%02uZ", y, m, d, static_cast<unsigned>(rest / 3600),
           static_cast<unsigned>(rest / 60 % 60), static_cast<unsigned>(rest % 60));
}

class ControlCard : public tessera::Card, public Listener {
 public:
  static constexpr int KEYS = 3;
  static constexpr int HOURS = 24;
  explicit ControlCard(Proxmox *plugin) : plugin_(plugin), setup_(plugin->card_setup) {
    for (int i = 0; i < KEYS; i++) copy_to(keys_[i].entity, sizeof(keys_[i].entity), setup_.keys[i]);
    series_ = setup_.cpu[0] ? 0 : 1;
  }
  ~ControlCard() override { plugin_->forget(this); }

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    height_ = c.height;
    dot_ = ui::block(c.parent, theme::MUTED);
    status_ = ui::label(c.parent, Font::HEADLINE, theme::INK);
    chart_name_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    chart_peak_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(chart_peak_, LV_TEXT_ALIGN_RIGHT, 0);
    chart_ground_ = ui::block(c.parent, theme::TRACK);
    for (lv_obj_t *&col : columns_) col = ui::block(chart_ground_, theme::ACCENT);
    for (MeasureRow &r : rows_) r.make(c.parent, Font::BODY_LARGE);
    for (Key &k : keys_) {
      k.block = ui::block(c.parent, theme::TRACK);
      k.label = ui::label(k.block, Font::BODY_LARGE, theme::INK);
      lv_obj_set_style_text_align(k.label, LV_TEXT_ALIGN_CENTER, 0);
    }
    note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    plugin_->listen(this);
    ask(0);
    ask(1);
  }

  void on_state(JsonObjectConst data) override { parse_node(data, node_); }

  void on_tick(uint32_t epoch) override {
    for (Key &k : keys_)
      if (k.armed) k.armed--;
    if (note_left_ && --note_left_ == 0) note_text_[0] = 0;
    // The clock may not have been set when the card opened; ask again once it is, and every ten minutes.
    if (epoch && (!asked_at_ || epoch - asked_at_ > 600)) {
      ask(0);
      ask(1);
    }
    layout();
  }

  void on_theme() override { layout(); }

  void on_answer(JsonObjectConst m) override {
    const uint32_t re = m["re"] | 0u;
    for (int s = 0; s < 2; s++) {
      if (!re || re != asked_[s]) continue;
      Series &series = history_[s];
      series.count = 0;
      if (m["ok"] | false) {
        for (JsonVariantConst v : m["result"]["means"].as<JsonArrayConst>()) {
          if (series.count >= HOURS) break;
          series.values[series.count++] = v.isNull() ? -1.0f : v.as<float>();
        }
      }
      series.known = true;
      layout();
    }
  }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (chart_shown_ && p.y >= chart_top_ && p.y < chart_bottom_ && setup_.cpu[0] && setup_.memory[0]) {
      series_ = 1 - series_;
      layout();
      return;
    }
    for (int i = 0; i < KEYS; i++) {
      Key &k = keys_[i];
      if (!k.shown || p.x < k.x || p.x >= k.x + k.w || p.y < k.y || p.y >= k.y + k.h) continue;
      if (!k.armed) {
        for (Key &other : keys_) other.armed = 0;
        k.armed = 5;
      } else {
        k.armed = 0;
        const bool sent = tessera::action("button.press", k.entity);
        copy_to(note_text_, sizeof(note_text_), plugin_->text(sent ? "sent" : "not_sent"));
        note_left_ = 3;
      }
      layout();
      return;
    }
  }

 private:
  struct Key {
    char entity[64] = "";
    lv_obj_t *block{}, *label{};
    int x = 0, y = 0, w = 0, h = 0;
    uint8_t armed = 0;  // seconds left to tap again
    bool shown = false;
  };
  struct Series {
    float values[HOURS] = {};
    int count = 0;
    bool known = false;
  };

  // Ask for a sensor's hourly means of the last day.
  void ask(int s) {
    const char *id = s == 0 ? setup_.cpu : setup_.memory;
    const uint32_t now = tessera::epoch();
    if (!id[0] || !now) return;
    char start[24];
    iso_utc(now - HOURS * 3600, start, sizeof(start));
    JsonDocument request;
    request["ask"] = "recorder/statistics_during_period";
    request["data"]["start_time"] = start;
    request["data"]["statistic_ids"].add(id);
    request["data"]["period"] = "hour";
    request["data"]["types"].add("mean");
    asked_[s] = tessera::send(plugin_, request.as<JsonObjectConst>());
    asked_at_ = now;
  }

  void layout() {
    const int gap = ui::px(10);
    const int hh = ui::line_height(Font::HEADLINE), bh = ui::line_height(Font::BODY);
    const int d = ui::px(16);
    const bool has_status = node_.health != Health::NONE;
    visible(dot_, has_status);
    visible(status_, has_status);
    int y = 0;
    if (has_status) {
      lv_obj_set_size(dot_, d, d);
      lv_obj_set_style_radius(dot_, d / 2, 0);
      lv_obj_set_pos(dot_, 0, (hh - d) / 2);
      ui::set_fill(dot_, tone_of(node_.health), theme::MUTED);
      ui::set_text(status_, plugin_->text(word_of(node_.health)));
      ui::set_tone(status_, tone_of(node_.health), theme::INK);
      lv_obj_set_size(status_, width_ - d - gap, hh);
      lv_obj_set_pos(status_, d + gap, 0);
      y = hh + gap;
    }
    // The keys stand at the bottom, as high as a finger needs.
    int count = 0;
    for (Key &k : keys_) count += k.entity[0] ? 1 : 0;
    const int kh = ui::mm(12);
    const int keys_top = count ? height_ - kh - bh - ui::px(4) : height_;
    // The chart: a quarter of the room, when there is a sensor to draw.
    const bool want_chart = setup_.cpu[0] || setup_.memory[0];
    const int chart_h = height_ / 4;
    chart_shown_ = want_chart && y + bh + chart_h + gap < keys_top;
    visible(chart_name_, chart_shown_);
    visible(chart_peak_, chart_shown_);
    visible(chart_ground_, chart_shown_);
    if (chart_shown_) {
      chart_top_ = y;
      draw_chart(y, chart_h);
      y += bh + ui::px(4) + chart_h + gap;
      chart_bottom_ = y;
    }
    const int bar = ui::px(10);
    for (int i = 0; i < 3; i++) {
      const Measure &m = node_.measures[i];
      if (!m.there || y + rows_[i].height(m, Font::BODY_LARGE, bar) > keys_top - gap) {
        rows_[i].hide();
        continue;
      }
      y += rows_[i].place(m, plugin_->text(MEASURE_IDS[i]), Font::BODY_LARGE, width_, y, bar) + gap;
      rows_[i].paint(m);
    }
    static const char *const WORDS[KEYS] = {"start", "shutdown", "reboot"};
    const int kw = count ? (width_ - gap * (count - 1)) / count : 0;
    int x = 0;
    for (int i = 0; i < KEYS; i++) {
      Key &k = keys_[i];
      k.shown = k.entity[0] != 0;
      visible(k.block, k.shown);
      if (!k.shown) continue;
      k.x = x;
      k.y = keys_top;
      k.w = kw;
      k.h = kh;
      lv_obj_set_size(k.block, kw, kh);
      lv_obj_set_pos(k.block, x, keys_top);
      ui::set_fill(k.block, k.armed ? Tone::ALERT : Tone::NORMAL, theme::TRACK);
      ui::set_text(k.label, plugin_->text(k.armed ? "again" : WORDS[i]));
      ui::set_color(k.label, k.armed ? theme::ON_ACCENT : theme::INK);
      const int lh = ui::line_height(Font::BODY_LARGE);
      lv_obj_set_size(k.label, kw, lh);
      lv_obj_set_pos(k.label, 0, (kh - lh) / 2);
      x += kw + gap;
    }
    visible(note_, count > 0);
    if (count) {
      ui::set_text(note_, note_text_);
      lv_obj_set_size(note_, width_, bh);
      lv_obj_set_pos(note_, 0, height_ - bh);
      ui::set_color(note_, theme::MUTED);
    }
  }

  // "CPU, 24 h" over 24 columns of the hourly means, scaled to 100 for a percentage, else to the day's highest.
  void draw_chart(int y, int h) {
    const int bh = ui::line_height(Font::BODY);
    const Series &s = history_[series_];
    const bool cpu = series_ == 0;
    ui::set_text(chart_name_, tessera::fill(plugin_->text("last_day"), "name", plugin_->text(cpu ? "cpu" : "memory")));
    lv_obj_set_size(chart_name_, width_ * 2 / 3, bh);
    lv_obj_set_pos(chart_name_, 0, y);
    float peak = 0;
    for (int i = 0; i < s.count; i++) peak = s.values[i] > peak ? s.values[i] : peak;
    const Measure &now = node_.measures[cpu ? 0 : 1];
    const float scale = now.percent || peak <= 0 ? 100.0f : peak;
    char words[32] = "";
    if (s.count) {
      if (now.percent || peak <= 100) snprintf(words, sizeof(words), "%s %.0f%%", plugin_->text("peak"), peak);
      else snprintf(words, sizeof(words), "%s %.0f", plugin_->text("peak"), peak);
    } else {
      copy_to(words, sizeof(words), plugin_->text(s.known ? "no_history" : "loading"));
    }
    ui::set_text(chart_peak_, words);
    lv_obj_set_size(chart_peak_, width_ / 3 + ui::px(40), bh);
    lv_obj_set_pos(chart_peak_, width_ - width_ / 3 - ui::px(40), y);
    ui::set_color(chart_name_, theme::MUTED);
    ui::set_color(chart_peak_, theme::MUTED);
    const int top = y + bh + ui::px(4);
    lv_obj_set_size(chart_ground_, width_, h);
    lv_obj_set_pos(chart_ground_, 0, top);
    lv_obj_set_style_radius(chart_ground_, ui::px(6), 0);
    lv_obj_set_style_bg_color(chart_ground_, ui::color(theme::TRACK), 0);
    const int pad = ui::px(4);
    const int cw = (width_ - pad * 2) / HOURS;
    for (int i = 0; i < HOURS; i++) {
      lv_obj_t *col = columns_[i];
      // The newest hour at the right: a day with fewer hours fills from the right.
      const int idx = s.count - HOURS + i;
      const float v = idx >= 0 ? s.values[idx] : -1;
      if (v < 0) {
        visible(col, false);
        continue;
      }
      int ch = static_cast<int>((h - pad * 2) * (v > scale ? 1.0f : v / scale));
      if (ch < ui::px(2)) ch = ui::px(2);
      visible(col, true);
      lv_obj_set_size(col, cw > 3 ? cw - 2 : cw, ch);
      lv_obj_set_pos(col, pad + i * cw, h - pad - ch);
      lv_obj_set_style_radius(col, ui::px(2), 0);
      ui::set_fill(col, now.percent ? level_tone(v) : Tone::ACCENT, theme::ACCENT);
    }
  }

  Proxmox *plugin_;
  CardSetup setup_;
  int width_ = 0, height_ = 0;
  NodeData node_;
  lv_obj_t *dot_{}, *status_{}, *note_{};
  lv_obj_t *chart_name_{}, *chart_peak_{}, *chart_ground_{};
  lv_obj_t *columns_[HOURS] = {};
  MeasureRow rows_[3];
  Key keys_[KEYS];
  Series history_[2];
  uint32_t asked_[2] = {0, 0};
  uint32_t asked_at_ = 0;
  int series_ = 0;
  bool chart_shown_ = false;
  int chart_top_ = 0, chart_bottom_ = 0;
  char note_text_[48] = "";
  uint8_t note_left_ = 0;
};

// ---- The status list: a head line that says how many are down, and a row per chosen entity ----
// A status entity (a VM, a service) shows a dot in its colour and a word; a sensor with a number (CPU, network) shows
// its value and unit, so one list can hold both.

// A sensor's state as a value: "12%", "3.4 MB/s"; false when the state is no number.
static bool value_text(const char *state, const char *unit, char *out, size_t size) {
  char *end = nullptr;
  const float v = strtof(state, &end);
  if (!end || end == state) return false;
  const bool whole = v >= 100 || v <= -100 || strcmp(unit, "%") == 0;
  if (!unit[0]) snprintf(out, size, whole ? "%.0f" : "%.1f", v);
  else if (strcmp(unit, "%") == 0) snprintf(out, size, "%.0f%%", v);
  else snprintf(out, size, whole ? "%.0f %s" : "%.1f %s", v, unit);
  return true;
}

class ListTile : public tessera::Tile {
 public:
  static constexpr int ITEMS = 8;
  explicit ListTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    copy_to(title_text_, sizeof(title_text_), c.name);
    // Names of the person's own, in the order of the chosen items (an item box left empty takes no name): "Arco,
    // Docker, NAS". An empty place keeps the entity's.
    char given[ITEMS][40] = {};
    const char *names = c.options["names"] | "";
    for (int i = 0, slot = 0; names[i] && slot < ITEMS;) {
      const char *start = names + i;
      const char *end = strchr(start, ',');
      const size_t len = end ? static_cast<size_t>(end - start) : strlen(start);
      snprintf(given[slot], sizeof(given[slot]), "%.*s", static_cast<int>(len), start);
      i += static_cast<int>(len) + (end ? 1 : 0);
      slot++;
    }
    for (int i = 0; i < ITEMS; i++) {
      char *g = given[i];
      while (*g == ' ') g++;
      size_t len = strlen(g);
      while (len && g[len - 1] == ' ') g[--len] = 0;
      copy_to(given_[i], sizeof(given_[i]), g);
    }
    head_ = ui::label(c.parent, Font::TITLE, theme::INK);
    for (Item &it : items_) {
      it.dot = ui::block(c.parent, theme::MUTED);
      it.name = ui::label(c.parent, Font::BODY, theme::INK);
      it.value = ui::label(c.parent, Font::BODY, theme::INK);
      lv_obj_set_style_text_align(it.value, LV_TEXT_ALIGN_RIGHT, 0);
    }
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
  }

  void on_state(JsonObjectConst data) override {
    JsonObjectConst entities = data["entities"];
    count_ = 0;
    down_ = 0;
    statuses_ = 0;
    for (int i = 0; i < ITEMS; i++) {
      char id[8];
      snprintf(id, sizeof(id), "item_%d", i + 1);
      JsonObjectConst e = entities[id];
      if (e.isNull() || e["wait"].is<const char *>()) continue;
      Item &it = items_[count_++];
      copy_to(it.text, sizeof(it.text), e["name"] | id);
      clean_name(it.text);
      it.slot = count_ - 1;  // the k-th chosen entity takes the k-th name, whichever item boxes hold them
      const char *state = e["state"] | "";
      const char *unit = e["attributes"]["unit_of_measurement"] | "";
      it.health = health_of(e);
      // A number is a value, never a status: a sensor of CPU or network traffic.
      it.is_value = value_text(state, unit, it.shown, sizeof(it.shown));
      if (it.is_value) {
        it.health = Health::NONE;
      } else {
        copy_to(it.shown, sizeof(it.shown), plugin_->text(word_of(it.health)));
        statuses_++;
        if (it.health == Health::DOWN) down_++;
      }
    }
    strip_common_start();
    // A name the person gave wins over the entity's.
    for (int i = 0; i < count_; i++)
      if (given_[items_[i].slot][0]) copy_to(items_[i].text, sizeof(items_[i].text), given_[items_[i].slot]);
    // The ones that are down first, so a small tile shows what matters; values keep their order after the statuses.
    for (int i = 1; i < count_; i++)
      for (int j = i; j > 0 && rank(items_[j]) < rank(items_[j - 1]); j--) swap_data(items_[j], items_[j - 1]);
  }

  void on_tick(uint32_t) override {
    if (count_ == 0) {
      visible(head_, false);
      for (Item &it : items_) hide(it);
      visible(note_, true);
      ui::set_text(note_, plugin_->text("choose"));
      lv_obj_set_size(note_, width_, height_);
      lv_obj_set_pos(note_, 0, 0);
      return;
    }
    visible(note_, false);
    visible(head_, true);
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY);
    paint_head();
    lv_obj_set_size(head_, width_, th);
    lv_obj_set_pos(head_, 0, 0);
    const int gap = ui::px(4), d = ui::px(10);
    int y = th + gap;
    for (int i = 0; i < ITEMS; i++) {
      Item &it = items_[i];
      if (i >= count_ || y + bh > height_) {
        hide(it);
        continue;
      }
      // A status: a dot, the name and a word. A value: the name and the value, under the dots' column.
      visible(it.dot, !it.is_value);
      visible(it.name, true);
      visible(it.value, true);
      int x = 0;
      if (statuses_ > 0) x = d + gap * 2;
      if (!it.is_value) {
        lv_obj_set_size(it.dot, d, d);
        lv_obj_set_style_radius(it.dot, d / 2, 0);
        lv_obj_set_pos(it.dot, 0, y + (bh - d) / 2);
      }
      ui::set_text(it.value, it.shown);
      int vw = ui::text_width(it.shown, Font::BODY) + gap;
      if (vw > (width_ - x) / 2) vw = (width_ - x) / 2;
      lv_obj_set_size(it.value, vw, bh);
      lv_obj_set_pos(it.value, width_ - vw, y);
      ui::set_text(it.name, it.text);
      lv_obj_set_size(it.name, width_ - x - vw - gap, bh);
      lv_obj_set_pos(it.name, x, y);
      paint(it);
      y += bh + gap;
    }
  }

  void on_theme() override {
    paint_head();
    ui::set_color(note_, theme::MUTED);
    for (int i = 0; i < count_; i++) paint(items_[i]);
  }

 private:
  struct Item {
    lv_obj_t *dot{}, *name{}, *value{};
    Health health = Health::NONE;
    bool is_value = false;
    int slot = 0;  // which of the chosen entities it is, for the name the person gave it
    char text[40] = "";
    char shown[24] = "";
  };

  void paint_head() {
    if (statuses_ == 0) {
      ui::set_text(head_, title_text_[0] ? title_text_ : plugin_->text("name_fallback"));
      ui::set_color(head_, theme::INK);
      return;
    }
    ui::set_text(head_, down_ ? tessera::format(plugin_->text("down"), down_) : std::string(plugin_->text("all_up")));
    ui::set_tone(head_, down_ ? Tone::ALERT : Tone::GOOD, theme::INK);
  }

  static void paint(Item &it) {
    if (it.is_value) {
      ui::set_color(it.name, theme::INK_SOFT);
      ui::set_color(it.value, theme::INK);
      return;
    }
    ui::set_fill(it.dot, tone_of(it.health), theme::MUTED);
    ui::set_color(it.name, it.health == Health::DOWN ? theme::INK : theme::INK_SOFT);
    ui::set_tone(it.value, tone_of(it.health), theme::MUTED);
  }

  // "1. Node: server CPU Usage", "1. Node: server Network RX" become "CPU Usage" and "Network RX": the words every name
  // starts with say nothing on a list of one device.
  void strip_common_start() {
    if (count_ < 2) return;
    size_t common = strlen(items_[0].text);
    for (int i = 1; i < count_; i++) {
      size_t k = 0;
      while (k < common && items_[i].text[k] && items_[i].text[k] == items_[0].text[k]) k++;
      common = k;
    }
    while (common > 0 && items_[0].text[common - 1] != ' ') common--;  // whole words only
    if (common == 0) return;
    for (int i = 0; i < count_; i++)
      if (strlen(items_[i].text) <= common) return;  // a name would be left empty
    for (int i = 0; i < count_; i++) memmove(items_[i].text, items_[i].text + common, strlen(items_[i].text) - common + 1);
  }

  static int rank(const Item &it) {
    if (it.is_value) return 4;
    return it.health == Health::DOWN ? 0 : it.health == Health::PENDING ? 1 : it.health == Health::UNKNOWN ? 2 : 3;
  }
  static void swap_data(Item &a, Item &b) {
    Item t = a;
    a.health = b.health;
    a.is_value = b.is_value;
    a.slot = b.slot;
    copy_to(a.text, sizeof(a.text), b.text);
    copy_to(a.shown, sizeof(a.shown), b.shown);
    b.health = t.health;
    b.is_value = t.is_value;
    b.slot = t.slot;
    copy_to(b.text, sizeof(b.text), t.text);
    copy_to(b.shown, sizeof(b.shown), t.shown);
  }
  static void hide(Item &it) {
    visible(it.dot, false);
    visible(it.name, false);
    visible(it.value, false);
  }

  const tessera::Plugin *plugin_;
  int width_ = 0, height_ = 0;
  int count_ = 0, down_ = 0, statuses_ = 0;
  char title_text_[48] = "";
  char given_[ITEMS][40] = {};
  lv_obj_t *head_{}, *note_{};
  Item items_[ITEMS];
};

// ---- Alerts: what is down, in the top bar, in a popup and on the screensaver ----
// Home Assistant says it with one binary sensor (on while something is down) whose attribute "problems" names what is
// down, "Docker, NAS": a template binary sensor of the person's own (README). The plugin reads it straight from Home
// Assistant (plugin.yaml) for the top bar and the popup, and the screensaver gets it as its step's entity.

// The popup: a red disc and "2 offline" on the close key's line, the names under it. A tap closes it.
class ProblemPopup : public tessera::Popup {
 public:
  ProblemPopup(Proxmox *plugin, const char *names) : plugin_(plugin) { count_ = split_names(names, names_, NAMES); }

  void open(const tessera::PopupContext &c) override {
    const int gap = ui::px(10);
    disc_ = ui::block(c.parent, theme::MUTED);
    lv_obj_set_size(disc_, c.key, c.key);
    lv_obj_set_style_radius(disc_, c.key / 2, 0);
    lv_obj_set_pos(disc_, 0, 0);
    ui::set_fill(disc_, Tone::ALERT, theme::MUTED);
    const int th = ui::line_height(Font::TITLE);
    title_ = ui::label(c.parent, Font::TITLE, theme::INK);
    lv_obj_set_size(title_, c.width - c.key - gap - c.corner, th);
    lv_obj_set_pos(title_, c.key + gap, (c.key - th) / 2);
    const int bh = ui::line_height(Font::BODY);
    int y = c.key + gap;
    for (int i = 0; i < NAMES; i++) {
      lines_[i] = ui::label(c.parent, Font::BODY, theme::INK_SOFT);
      lv_obj_set_size(lines_[i], c.width, bh);
      lv_obj_set_pos(lines_[i], 0, y);
      const bool there = i < count_ && i < NAMES && y + bh <= c.height;
      visible(lines_[i], there);
      if (there) {
        ui::set_text(lines_[i], names_[i]);
        y += bh;
      }
    }
    paint();
  }
  void on_theme() override { paint(); }
  void on_tap() override { tessera::close_popup(plugin_); }

 private:
  void paint() {
    ui::set_text(title_, count_ ? tessera::format(plugin_->text("down"), count_) : std::string(plugin_->text("trouble")));
    ui::set_tone(title_, Tone::ALERT, theme::INK);
    ui::set_fill(disc_, Tone::ALERT, theme::MUTED);
    for (lv_obj_t *l : lines_) ui::set_color(l, theme::INK_SOFT);
  }
  Proxmox *plugin_;
  char names_[NAMES][40] = {};
  int count_ = 0;
  lv_obj_t *disc_{}, *title_{};
  lv_obj_t *lines_[NAMES] = {};
};

// The screensaver: a step of its own that shows only while the problem sensor is on (the manifest's `when`). Large,
// in the dark look: a red dot and "2 offline", the names under it, in the middle of the glass.
class ProblemSaver : public tessera::Saver {
 public:
  explicit ProblemSaver(Proxmox *plugin) : plugin_(plugin) {}

  void show(const tessera::SaverContext &c) override {
    width_ = c.width;
    height_ = c.height;
    dot_ = ui::block(c.parent, theme::MUTED);
    head_ = ui::label(c.parent, Font::HEADLINE, theme::INK);
    lv_obj_set_style_text_align(head_, LV_TEXT_ALIGN_CENTER, 0);
    for (lv_obj_t *&l : lines_) {
      l = ui::label(c.parent, Font::BODY_LARGE, theme::INK_SOFT);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
  }

  void on_state(JsonObjectConst data) override {
    // A list (as a template gives it) comes whole; a text is cut at 48 bytes by the app.
    JsonVariantConst problems = data["attributes"]["problems"];
    if (problems.is<JsonArrayConst>()) {
      count_ = 0;
      for (JsonVariantConst v : problems.as<JsonArrayConst>()) {
        if (count_ < NAMES) copy_to(names_[count_], sizeof(names_[count_]), v | "");
        count_++;
      }
    } else {
      count_ = split_names(problems | "", names_, NAMES);
    }
    on_ = strcmp(data["state"] | "", "on") == 0;
    paint();
  }

 private:
  void paint() {
    const int d = ui::px(28);
    const int hh = ui::line_height(Font::HEADLINE), lh = ui::line_height(Font::BODY_LARGE);
    const int lines = count_ < NAMES ? count_ : NAMES;
    const int total = d + ui::px(16) + hh + ui::px(12) + lines * lh;
    int y = (height_ - total) / 2;
    lv_obj_set_size(dot_, d, d);
    lv_obj_set_style_radius(dot_, d / 2, 0);
    lv_obj_set_pos(dot_, (width_ - d) / 2, y);
    ui::set_fill(dot_, on_ ? Tone::ALERT : Tone::GOOD, theme::MUTED);
    y += d + ui::px(16);
    ui::set_text(head_, on_ ? (count_ ? tessera::format(plugin_->text("down"), count_) : std::string(plugin_->text("trouble")))
                            : std::string(plugin_->text("all_up")));
    ui::set_tone(head_, on_ ? Tone::ALERT : Tone::GOOD, theme::INK);
    lv_obj_set_size(head_, width_, hh);
    lv_obj_set_pos(head_, 0, y);
    y += hh + ui::px(12);
    for (int i = 0; i < NAMES; i++) {
      const bool there = on_ && i < lines;
      visible(lines_[i], there);
      if (!there) continue;
      ui::set_text(lines_[i], names_[i]);
      lv_obj_set_size(lines_[i], width_, lh);
      lv_obj_set_pos(lines_[i], 0, y);
      y += lh;
    }
  }

  Proxmox *plugin_;
  int width_ = 0, height_ = 0;
  bool on_ = false;
  char names_[NAMES][40] = {};
  int count_ = 0;
  lv_obj_t *dot_{}, *head_{};
  lv_obj_t *lines_[NAMES] = {};
};

tessera::BarItem Proxmox::bar() const {
  tessera::BarItem item;
  if (!in_trouble()) return item;
  char names[1][40];
  const int n = split_names(problem_names(), names, 1);
  item.shown = true;
  item.icon = 0xF0028;  // alert-circle
  item.tone = Tone::ALERT;
  item.text = n == 1 ? std::string(names[0]) : n ? tessera::format(text("down"), n) : std::string(text("trouble"));
  return item;
}

// Every 250 ms: a popup when something goes down, or when what is down changes, while the setting is on.
void Proxmox::on_interval(uint32_t) {
  const bool trouble = in_trouble();
  const char *names = problem_names();
  const bool changed = trouble && (!was_in_trouble_ || strcmp(names, last_names_) != 0);
  was_in_trouble_ = trouble;
  copy_to(last_names_, sizeof(last_names_), trouble ? names : "");
  if (!trouble) {
    if (tessera::popup_shown(this)) tessera::close_popup(this);
    return;
  }
  if (changed && popup_ && popup_->state) {
    char copy[160];
    copy_to(copy, sizeof(copy), names);
    std::string kept(copy);
    tessera::show_popup(this, [this, kept]() { return new ProblemPopup(this, kept.c_str()); });
  }
}

bool Proxmox::settings(tessera::SettingsPage &page) {
  page.icon = "\U000F0379";  // monitor
  page.toggle(text("setting_popup"), [this] { return popup_ && popup_->state; },
              [this](bool on) {
                if (!popup_) return;
                on ? popup_->turn_on() : popup_->turn_off();
              });
  return true;
}

void Proxmox::setup() {
  add_tile("node", [this]() { return new NodeTile(this); });
  add_card("control", [this]() { return new ControlCard(this); }, true);
  add_bar_item("problems", [this]() { return bar(); });
  add_saver("problems", tessera::SaverStep::OWN, [this]() { return new ProblemSaver(this); });
  add_tile("status_list", [this]() { return new ListTile(this); });
}

}  // namespace esphome::proxmox
