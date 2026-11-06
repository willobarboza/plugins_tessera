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
// "problem" (on means something is wrong); a sensor says it in words (Proxmox: running, stopped; Uptime Kuma: up, down).
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
  if (is_one_of(state, {"on", "running", "online", "up", "true", "connected", "ok"})) return problem ? Health::DOWN : Health::UP;
  if (is_one_of(state, {"off", "stopped", "offline", "down", "false", "disconnected", "error", "problem"}))
    return problem ? Health::UP : Health::DOWN;
  if (is_one_of(state, {"pending", "paused", "maintenance", "starting", "stopping", "suspended"})) return Health::PENDING;
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

class ListTile : public tessera::Tile {
 public:
  static constexpr int ITEMS = 8;
  explicit ListTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    head_ = ui::label(c.parent, Font::TITLE, theme::INK);
    for (Item &it : items_) {
      it.dot = ui::block(c.parent, theme::MUTED);
      it.name = ui::label(c.parent, Font::BODY, theme::INK);
    }
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
  }

  void on_state(JsonObjectConst data) override {
    JsonObjectConst entities = data["entities"];
    count_ = 0;
    down_ = 0;
    for (int i = 0; i < ITEMS; i++) {
      char id[8];
      snprintf(id, sizeof(id), "item_%d", i + 1);
      JsonObjectConst e = entities[id];
      const Health h = health_of(e);
      if (h == Health::NONE) continue;
      Item &it = items_[count_++];
      it.health = h;
      copy_to(it.text, sizeof(it.text), e["name"] | id);
      if (h == Health::DOWN) down_++;
    }
    // The ones that are down first, so a small tile shows what matters.
    for (int i = 1; i < count_; i++)
      for (int j = i; j > 0 && rank(items_[j].health) < rank(items_[j - 1].health); j--) swap_data(items_[j], items_[j - 1]);
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
    ui::set_text(head_, down_ ? tessera::format(plugin_->text("down"), down_) : std::string(plugin_->text("all_up")));
    ui::set_tone(head_, down_ ? Tone::ALERT : Tone::GOOD, theme::INK);
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
      show(it.dot, true);
      show(it.name, true);
      lv_obj_set_size(it.dot, d, d);
      lv_obj_set_style_radius(it.dot, d / 2, 0);
      lv_obj_set_pos(it.dot, 0, y + (bh - d) / 2);
      ui::set_fill(it.dot, tone_of(it.health), theme::MUTED);
      ui::set_text(it.name, it.text);
      ui::set_color(it.name, it.health == Health::DOWN ? theme::INK : theme::INK_SOFT);
      lv_obj_set_size(it.name, width_ - d - gap * 2, bh);
      lv_obj_set_pos(it.name, d + gap * 2, y);
      y += bh + gap;
    }
  }

  void on_theme() override {
    ui::set_tone(head_, down_ ? Tone::ALERT : Tone::GOOD, theme::INK);
    ui::set_color(note_, theme::MUTED);
    for (int i = 0; i < count_; i++) {
      ui::set_fill(items_[i].dot, tone_of(items_[i].health), theme::MUTED);
      ui::set_color(items_[i].name, items_[i].health == Health::DOWN ? theme::INK : theme::INK_SOFT);
    }
  }

 private:
  struct Item {
    lv_obj_t *dot{}, *name{};
    Health health = Health::NONE;
    char text[40] = "";
  };
  static int rank(Health h) { return h == Health::DOWN ? 0 : h == Health::PENDING ? 1 : h == Health::UNKNOWN ? 2 : 3; }
  static void swap_data(Item &a, Item &b) {
    Item t = a;
    a.health = b.health;
    copy_to(a.text, sizeof(a.text), b.text);
    b.health = t.health;
    copy_to(b.text, sizeof(b.text), t.text);
  }
  static void hide(Item &it) {
    show(it.dot, false);
    show(it.name, false);
  }

  const tessera::Plugin *plugin_;
  int width_ = 0, height_ = 0;
  int count_ = 0, down_ = 0;
  lv_obj_t *head_{}, *note_{};
  Item items_[ITEMS];
};

void Proxmox::setup() {
  add_tile("node", [this]() { return new NodeTile(this); });
  add_tile("status_list", [this]() { return new ListTile(this); });
}

}  // namespace esphome::proxmox
