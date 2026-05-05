#include "simu_ui_automation.h"

#if defined(COLORLCD)
#include "gui/colorlcd/libui/window.h"
#include "lvgl/lvgl.h"
#endif

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace SimuUiAutomation
{
namespace
{

std::mutex snapshotMutex;
std::condition_variable snapshotCv;
uint64_t requestedRevision = 0;
uint64_t completedRevision = 0;
bool snapshotRequested = false;
std::string latestTreeJson = "\"ui\":{\"nodes\":[]}";

uint64_t requestedActionRevision = 0;
uint64_t completedActionRevision = 0;
bool actionRequested = false;
bool latestActionOk = false;
std::string requestedActionId;
std::string requestedAction;
std::string latestActionExtra;
std::string latestActionError;

std::string jsonEscape(const std::string& value)
{
  std::string escaped;
  escaped.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

#if defined(COLORLCD)
std::string nodeId(const lv_obj_t* obj)
{
  std::ostringstream out;
  out << "lv:" << reinterpret_cast<std::uintptr_t>(obj);
  return out.str();
}

Window* automationWindow(lv_obj_t* obj)
{
  return static_cast<Window*>(lv_obj_get_user_data(obj));
}

std::string lvglRole(const lv_obj_t* obj)
{
  if (lv_obj_check_type(obj, &lv_label_class)) return "text";
  if (lv_obj_check_type(obj, &lv_btn_class)) return "button";
  if (lv_obj_check_type(obj, &lv_canvas_class)) return "image";
  if (lv_obj_check_type(obj, &lv_img_class)) return "image";
  return "object";
}

std::string nodeRole(lv_obj_t* obj)
{
  auto* w = automationWindow(obj);
  if (w) return w->automationRole();
  return lvglRole(obj);
}

std::string nodeText(lv_obj_t* obj)
{
  auto* w = automationWindow(obj);
  if (w) {
    auto text = w->automationText();
    if (!text.empty()) return text;
  }

  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* text = lv_label_get_text(obj);
    if (text) return text;
  }

  return "";
}

bool nodeClickable(lv_obj_t* obj)
{
  auto* w = automationWindow(obj);
  if (w) return w->automationClickable();
  return lv_obj_check_type(obj, &lv_btn_class) &&
         lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

bool nodeLongClickable(lv_obj_t* obj)
{
  auto* w = automationWindow(obj);
  return w && w->automationLongClickable();
}

bool containsPoint(lv_obj_t* obj, int x, int y)
{
  if (!obj || !lv_obj_is_visible(obj)) return false;

  lv_area_t bounds;
  lv_obj_get_coords(obj, &bounds);
  return x >= bounds.x1 && x <= bounds.x2 &&
         y >= bounds.y1 && y <= bounds.y2;
}

lv_obj_t* topObjectAtPoint(lv_obj_t* obj, int x, int y)
{
  if (!containsPoint(obj, x, y)) return nullptr;

  const auto childCount = lv_obj_get_child_cnt(obj);
  for (uint32_t i = childCount; i > 0; i -= 1) {
    if (auto* found = topObjectAtPoint(lv_obj_get_child(obj, i - 1), x, y))
      return found;
  }

  return obj;
}

bool isAncestorOf(lv_obj_t* ancestor, lv_obj_t* obj)
{
  while (obj) {
    if (obj == ancestor) return true;
    obj = lv_obj_get_parent(obj);
  }
  return false;
}

bool centerPointIsReachable(lv_obj_t* obj, const lv_area_t& bounds)
{
  const int x = (bounds.x1 + bounds.x2) / 2;
  const int y = (bounds.y1 + bounds.y2) / 2;
  return isAncestorOf(obj, topObjectAtPoint(lv_scr_act(), x, y));
}

void appendNode(std::ostringstream& out, lv_obj_t* obj, lv_obj_t* parent,
                bool& first)
{
  if (!obj || !lv_obj_is_visible(obj)) return;

  lv_area_t bounds;
  lv_obj_get_coords(obj, &bounds);
  const auto id = nodeId(obj);
  const auto role = nodeRole(obj);
  const auto text = nodeText(obj);
  const auto* w = automationWindow(obj);
  const bool reachable = centerPointIsReachable(obj, bounds);
  const bool clickable = reachable && nodeClickable(obj);
  const bool longClickable = reachable && nodeLongClickable(obj);

  if (!first) out << ",";
  first = false;

  out << "{"
      << "\"id\":\"" << jsonEscape(id) << "\""
      << ",\"parent\":\""
      << (parent ? jsonEscape(nodeId(parent)) : std::string()) << "\""
      << ",\"role\":\"" << jsonEscape(role) << "\""
      << ",\"text\":\"" << jsonEscape(text) << "\"";
  if (w && !w->automationId().empty())
    out << ",\"automation_id\":\"" << jsonEscape(w->automationId()) << "\"";
  out << ",\"bounds\":[" << bounds.x1 << "," << bounds.y1 << ","
      << (bounds.x2 - bounds.x1 + 1) << ","
      << (bounds.y2 - bounds.y1 + 1) << "]"
      << ",\"visible\":true"
      << ",\"enabled\":"
      << (lv_obj_has_state(obj, LV_STATE_DISABLED) ? "false" : "true")
      << ",\"checked\":"
      << (lv_obj_has_state(obj, LV_STATE_CHECKED) ? "true" : "false")
      << ",\"focused\":"
      << (lv_obj_has_state(obj, LV_STATE_FOCUSED) ? "true" : "false")
      << ",\"actions\":[";
  bool firstAction = true;
  if (clickable) {
    out << "\"click\"";
    firstAction = false;
  }
  if (longClickable) {
    if (!firstAction) out << ",";
    out << "\"long_click\"";
  }
  out << "]}";

  const auto childCount = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < childCount; i += 1) {
    appendNode(out, lv_obj_get_child(obj, i), obj, first);
  }
}

std::string buildSnapshot()
{
  lv_obj_t* screen = lv_scr_act();
  if (!screen) return "\"ui\":{\"nodes\":[]}";

  lv_obj_update_layout(screen);

  lv_obj_t* focused = nullptr;
  if (auto* group = lv_group_get_default()) focused = lv_group_get_focused(group);

  auto* top = Window::topWindow();
  std::ostringstream out;
  out << "\"ui\":{"
      << "\"screen\":\"" << jsonEscape(nodeId(screen)) << "\""
      << ",\"focused\":\""
      << (focused ? jsonEscape(nodeId(focused)) : std::string()) << "\""
      << ",\"top_window\":\""
      << (top && top->getLvObj() ? jsonEscape(nodeId(top->getLvObj()))
                                  : std::string())
      << "\""
      << ",\"nodes\":[";
  bool first = true;
  appendNode(out, screen, nullptr, first);
  out << "]}";
  return out.str();
}

lv_obj_t* findNode(lv_obj_t* obj, const std::string& id)
{
  if (!obj) return nullptr;
  if (nodeId(obj) == id) return obj;
  const auto childCount = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < childCount; i += 1) {
    if (auto* found = findNode(lv_obj_get_child(obj, i), id))
      return found;
  }
  return nullptr;
}

bool invokeAction(const std::string& id, const std::string& action,
                  std::string& extra, std::string& error)
{
  auto* node = findNode(lv_scr_act(), id);
  if (!node) {
    error = "unknown UI node: " + id;
    return false;
  }
  if (!lv_obj_is_visible(node)) {
    error = "UI node is not visible: " + id;
    return false;
  }

  lv_area_t bounds;
  lv_obj_get_coords(node, &bounds);
  if (!centerPointIsReachable(node, bounds)) {
    error = "UI node is covered by another window: " + id;
    return false;
  }

  const bool click = action == "click";
  const bool longClick = action == "long_click";
  if (!click && !longClick) {
    error = "unknown UI action: " + action;
    return false;
  }
  if ((click && !nodeClickable(node)) ||
      (longClick && !nodeLongClickable(node))) {
    error = "UI node does not support action `" + action + "`: " + id;
    return false;
  }

  const int x = (bounds.x1 + bounds.x2) / 2;
  const int y = (bounds.y1 + bounds.y2) / 2;
  std::ostringstream out;
  out << "\"node\":\"" << jsonEscape(id) << "\""
      << ",\"x\":" << x
      << ",\"y\":" << y;
  extra = out.str();

  if (auto* window = automationWindow(node)) {
    if (click)
      window->onClicked();
    else
      window->onLongPress();
  } else {
    lv_event_send(node, click ? LV_EVENT_CLICKED : LV_EVENT_LONG_PRESSED,
                  nullptr);
  }

  if (auto* screen = lv_scr_act()) {
    lv_obj_invalidate(screen);
    lv_obj_update_layout(screen);
  }
  lv_refr_now(nullptr);
  return true;
}
#endif

}  // namespace

void menuTick()
{
#if defined(COLORLCD)
  uint64_t snapshotRevision = 0;
  uint64_t actionRevision = 0;
  bool buildTree = false;
  bool runAction = false;
  std::string actionId;
  std::string action;
  {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    buildTree = snapshotRequested;
    runAction = actionRequested;
    if (!buildTree && !runAction) return;

    if (buildTree) {
      snapshotRequested = false;
      snapshotRevision = requestedRevision;
    }
    if (runAction) {
      actionRequested = false;
      actionRevision = requestedActionRevision;
      actionId = requestedActionId;
      action = requestedAction;
    }
  }

  bool actionOk = false;
  std::string actionExtra;
  std::string actionError;
  if (runAction) {
    actionOk = invokeAction(actionId, action, actionExtra, actionError);
  }

  std::string snapshot;
  if (buildTree) {
    snapshot = buildSnapshot();
  }

  {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    if (runAction) {
      latestActionOk = actionOk;
      latestActionExtra = std::move(actionExtra);
      latestActionError = std::move(actionError);
      completedActionRevision = actionRevision;
    }
    if (buildTree) {
      latestTreeJson = std::move(snapshot);
      completedRevision = snapshotRevision;
    }
  }
  snapshotCv.notify_all();
#endif
}

bool requestSnapshot(std::string& json, std::string& error, uint32_t timeoutMs)
{
#if defined(COLORLCD)
  std::unique_lock<std::mutex> lock(snapshotMutex);
  const uint64_t revision = ++requestedRevision;
  snapshotRequested = true;
  const auto timeout = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(timeoutMs);

  if (!snapshotCv.wait_until(lock, timeout, [revision] {
        return completedRevision >= revision;
      })) {
    error = "timed out waiting for UI snapshot";
    return false;
  }

  json = latestTreeJson;
  return true;
#else
  (void)timeoutMs;
  error = "ui_tree is only available for color LCD simulator builds";
  return false;
#endif
}

bool requestAction(const std::string& id, const std::string& action,
                   std::string& extra, std::string& error, uint32_t timeoutMs)
{
#if defined(COLORLCD)
  std::unique_lock<std::mutex> lock(snapshotMutex);
  const uint64_t revision = ++requestedActionRevision;
  requestedActionId = id;
  requestedAction = action;
  actionRequested = true;
  const auto timeout = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(timeoutMs);

  if (!snapshotCv.wait_until(lock, timeout, [revision] {
        return completedActionRevision >= revision;
      })) {
    error = "timed out waiting for UI action";
    return false;
  }

  if (!latestActionOk) {
    error = latestActionError;
    return false;
  }

  extra = latestActionExtra;
  return true;
#else
  (void)id;
  (void)action;
  (void)timeoutMs;
  error = "UI actions are only available for color LCD simulator builds";
  return false;
#endif
}

}
