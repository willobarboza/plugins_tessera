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

static void show(lv_obj_t *part, bool on) {
  if (on) lv_obj_clear_flag(part, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
}

static void copy_to(char *to, size_t size, const char *from) {
  snprintf(to, size, "%s", from ? from : "");
}

// ---- The node tile: a title with a dot, a status line, and rows of CPU, memory and disk ----

class NodeTile : public tessera::Tile {
 public:
  explicit NodeTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    copy_to(title_text_, sizeof(title_text_), c.name);
    dot_ = ui::block(c.parent, theme::MUTED);
    title_ = ui::label(c.parent, Font::TITLE, theme::INK);
    status_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    for (Row &r : rows_) {
      r.name = ui::label(c.parent, Font::BODY, theme::MUTED);
      r.value = ui::label(c.parent, Font::BODY, theme::INK);
      lv_obj_set_style_text_align(r.value, LV_TEXT_ALIGN_RIGHT, 0);
      r.track = ui::block(c.parent, theme::TRACK);
      r.fill = ui::block(r.track, theme::ACCENT);
    }
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
  }

  void on_state(JsonObjectConst data) override {
    JsonObjectConst entities = data["entities"];
    JsonObjectConst status = entities["status"];
    health_ = health_of(status);
    if (!title_text_[0] && health_ != Health::NONE) copy_to(title_text_, sizeof(title_text_), status["name"] | "");
    static const char *const IDS[3] = {"cpu", "memory", "disk"};
    any_ = health_ != Health::NONE;
    for (int i = 0; i < 3; i++) {
      Row &r = rows_[i];
      JsonObjectConst e = entities[IDS[i]];
      r.there = !e.isNull() && !e["wait"].is<const char *>();
      if (!r.there) continue;
      any_ = true;
      const char *state = e["state"] | "";
      const char *unit = e["attributes"]["unit_of_measurement"] | "";
      char *end = nullptr;
      const float v = strtof(state, &end);
      r.number = end && end != state;
      r.percent = r.number && strcmp(unit, "%") == 0;
      r.level = r.number ? v : 0;
      if (!r.number) copy_to(r.text, sizeof(r.text), "-");
      else if (r.percent) snprintf(r.text, sizeof(r.text), "%.0f%%", v);
      else snprintf(r.text, sizeof(r.text), v < 10 ? "%.1f %s" : "%.0f %s", v, unit);
    }
  }

  void on_tick(uint32_t) override {
    if (!any_) {
      for (lv_obj_t *p : {dot_, title_, status_}) show(p, false);
      for (Row &r : rows_) hide_row(r);
      show(note_, true);
      ui::set_text(note_, plugin_->text("choose"));
      lv_obj_set_size(note_, width_, height_);
      lv_obj_set_pos(note_, 0, 0);
      return;
    }
    show(note_, false);
    const int gap = ui::px(6);
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY);
    const int d = ui::px(10);
    int y = 0;
    // The title, with the dot before it when there is a status.
    const bool has_status = health_ != Health::NONE;
    show(dot_, has_status);
    int x = 0;
    if (has_status) {
      lv_obj_set_size(dot_, d, d);
      lv_obj_set_style_radius(dot_, d / 2, 0);
      lv_obj_set_pos(dot_, 0, (th - d) / 2);
      ui::set_fill(dot_, tone_of(health_), theme::MUTED);
      x = d + gap;
    }
    show(title_, true);
    ui::set_text(title_, title_text_[0] ? title_text_ : plugin_->text("name_fallback"));
    lv_obj_set_size(title_, width_ - x, th);
    lv_obj_set_pos(title_, x, 0);
    y = th;
    show(status_, has_status && y + bh <= height_);
    if (has_status && y + bh <= height_) {
      ui::set_text(status_, plugin_->text(word_of(health_)));
      ui::set_tone(status_, tone_of(health_), theme::MUTED);
      lv_obj_set_size(status_, width_, bh);
      lv_obj_set_pos(status_, 0, y);
      y += bh;
    }
    // The rows that are chosen and fit: a name and a value on a line, a bar under it for a percentage.
    static const char *const NAMES[3] = {"cpu", "memory", "disk"};
    const int bar = ui::px(6);
    for (int i = 0; i < 3; i++) {
      Row &r = rows_[i];
      const int need = gap + bh + (r.percent ? bar + ui::px(2) : 0);
      if (!r.there || y + need > height_) {
        hide_row(r);
        continue;
      }
      y += gap;
      show(r.name, true);
      show(r.value, true);
      ui::set_text(r.name, plugin_->text(NAMES[i]));
      ui::set_text(r.value, r.text);
      const int vw = width_ / 2;
      lv_obj_set_size(r.name, width_ - vw, bh);
      lv_obj_set_pos(r.name, 0, y);
      lv_obj_set_size(r.value, vw, bh);
      lv_obj_set_pos(r.value, width_ - vw, y);
      y += bh;
      show(r.track, r.percent);
      if (r.percent) {
        y += ui::px(2);
        lv_obj_set_size(r.track, width_, bar);
        lv_obj_set_pos(r.track, 0, y);
        float level = r.level < 0 ? 0 : r.level > 100 ? 100 : r.level;
        const int filled = static_cast<int>(width_ * level / 100.0f);
        show(r.fill, filled > 0);
        lv_obj_set_size(r.fill, filled < bar ? bar : filled, bar);
        lv_obj_set_pos(r.fill, 0, 0);
        const Tone t = level >= 90 ? Tone::ALERT : level >= 75 ? Tone::BUSY : Tone::GOOD;
        ui::set_fill(r.fill, t, theme::ACCENT);
        y += bar;
      }
    }
  }

  void on_theme() override {
    ui::set_color(title_, theme::INK);
    ui::set_tone(status_, tone_of(health_), theme::MUTED);
    ui::set_fill(dot_, tone_of(health_), theme::MUTED);
    ui::set_color(note_, theme::MUTED);
    for (Row &r : rows_) {
      ui::set_color(r.name, theme::MUTED);
      ui::set_color(r.value, theme::INK);
      lv_obj_set_style_bg_color(r.track, ui::color(theme::TRACK), 0);
      const float level = r.level < 0 ? 0 : r.level > 100 ? 100 : r.level;
      ui::set_fill(r.fill, level >= 90 ? Tone::ALERT : level >= 75 ? Tone::BUSY : Tone::GOOD, theme::ACCENT);
    }
  }

 private:
  struct Row {
    lv_obj_t *name{}, *value{}, *track{}, *fill{};
    bool there = false, number = false, percent = false;
    float level = 0;
    char text[24] = "";
  };
  void hide_row(Row &r) {
    show(r.name, false);
    show(r.value, false);
    show(r.track, false);
  }

  const tessera::Plugin *plugin_;
  int width_ = 0, height_ = 0;
  char title_text_[48] = "";
  Health health_ = Health::NONE;
  bool any_ = false;
  lv_obj_t *dot_{}, *title_{}, *status_{}, *note_{};
  Row rows_[3];
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
    // The ones that are down first, so a small tile shows what matters; values keep their order after the statuses.
    for (int i = 1; i < count_; i++)
      for (int j = i; j > 0 && rank(items_[j]) < rank(items_[j - 1]); j--) swap_data(items_[j], items_[j - 1]);
  }

  void on_tick(uint32_t) override {
    if (count_ == 0) {
      show(head_, false);
      for (Item &it : items_) hide(it);
      show(note_, true);
      ui::set_text(note_, plugin_->text("choose"));
      lv_obj_set_size(note_, width_, height_);
      lv_obj_set_pos(note_, 0, 0);
      return;
    }
    show(note_, false);
    show(head_, true);
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
      show(it.dot, !it.is_value);
      show(it.name, true);
      show(it.value, true);
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
    copy_to(a.text, sizeof(a.text), b.text);
    copy_to(a.shown, sizeof(a.shown), b.shown);
    b.health = t.health;
    b.is_value = t.is_value;
    copy_to(b.text, sizeof(b.text), t.text);
    copy_to(b.shown, sizeof(b.shown), t.shown);
  }
  static void hide(Item &it) {
    show(it.dot, false);
    show(it.name, false);
    show(it.value, false);
  }

  const tessera::Plugin *plugin_;
  int width_ = 0, height_ = 0;
  int count_ = 0, down_ = 0, statuses_ = 0;
  char title_text_[48] = "";
  lv_obj_t *head_{}, *note_{};
  Item items_[ITEMS];
};

void Proxmox::setup() {
  add_tile("node", [this]() { return new NodeTile(this); });
  add_tile("status_list", [this]() { return new ListTile(this); });
}

}  // namespace esphome::proxmox
