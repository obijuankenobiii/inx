#pragma once

#include "state/RecentBooks.h"

class RecentActivity;

namespace recent {

class Grid {
 public:
  static void render(RecentActivity& self, int startY);
};

class Grid3x3 {
 public:
  static void render(RecentActivity& self, int startY);
};

class List {
 public:
  static void render(RecentActivity& self, int startY);
};

class Cover {
 public:
  static void render(RecentActivity& self);
};

class Flow {
 public:
  static void render(RecentActivity& self);
};

class SimpleUi {
 public:
  static void render(RecentActivity& self);
};

}  // namespace recent
