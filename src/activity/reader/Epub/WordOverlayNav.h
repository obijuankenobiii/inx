#pragma once

/**
 * Shared D-pad movement for highlight + dictionary word pickers.
 *
 * Up on the first line jumps to the last word on the page (so the bottom is one press away).
 * Down on the last line jumps to the first word. A second press of the same button within a short
 * window skips several words/lines. Hold-repeat does not wrap, so a held Down stops at the bottom.
 */

#include <Epub/PageWordIndex.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <vector>

#include "system/MappedInputManager.h"

namespace WordOverlayNav {

constexpr unsigned long kBounceMs = 50;
constexpr unsigned long kDoubleTapMs = 340;
constexpr int kDoubleTapStride = 3;
constexpr unsigned long kRepeatInitialMs = 700;
constexpr unsigned long kRepeatIntervalMs = 95;
constexpr int kRepeatLineStride = 3;
constexpr int kRepeatWordStride = 3;

struct EdgeState {
  unsigned long lastMs = 0;
  int lastDir = -1;
  EdgeState() = default;
  EdgeState(unsigned long ms, int dir) : lastMs(ms), lastDir(dir) {}
};

inline bool isBounce(EdgeState& state, const int dir, const unsigned long now) {
  if (state.lastDir == dir && (now - state.lastMs) < kBounceMs) {
    state.lastMs = now;
    return true;
  }
  return false;
}

inline int stepsForPress(EdgeState& state, const int dir, const unsigned long now) {
  const int steps =
      (state.lastDir == dir && (now - state.lastMs) < kDoubleTapMs) ? kDoubleTapStride : 1;
  state.lastMs = now;
  state.lastDir = dir;
  return steps;
}

inline void moveFocusWord(const std::vector<PageWordHit>& words, size_t& focus, const int delta) {
  if (words.empty() || delta == 0) {
    return;
  }
  if (delta > 0) {
    const size_t room = words.size() - 1 - focus;
    focus += std::min(static_cast<size_t>(delta), room);
    return;
  }
  const size_t back = static_cast<size_t>(-delta);
  focus = (back >= focus) ? 0 : focus - back;
}

inline size_t lineIndexForFocus(const std::vector<size_t>& lineFirst, const size_t wordCount, const size_t focus) {
  if (lineFirst.empty()) {
    return 0;
  }
  size_t lineIdx = 0;
  for (size_t i = 0; i < lineFirst.size(); ++i) {
    const size_t start = lineFirst[i];
    const size_t end = (i + 1 < lineFirst.size()) ? lineFirst[i + 1] : wordCount;
    if (focus >= start && focus < end) {
      lineIdx = i;
      break;
    }
  }
  return lineIdx;
}

inline size_t wordOnLineNearestX(const std::vector<PageWordHit>& words, const std::vector<size_t>& lineFirst,
                                 const size_t lineIdx, const int targetX) {
  const size_t start = lineFirst[lineIdx];
  const size_t end = (lineIdx + 1 < lineFirst.size()) ? lineFirst[lineIdx + 1] : words.size();
  size_t best = start;
  int bestDist = 0x7fffffff;
  for (size_t i = start; i < end; ++i) {
    const int cx = words[i].screenX + words[i].screenW / 2;
    const int dist = std::abs(cx - targetX);
    if (dist < bestDist) {
      bestDist = dist;
      best = i;
    }
  }
  return best;
}

/** @param wrap If true, Up on the first line goes to the last word and Down on the last line goes
 *  to the first word. Remaining steps after a wrap are ignored so that jump lands on the end. */
inline void moveFocusLine(const std::vector<PageWordHit>& words, const std::vector<size_t>& lineFirst, size_t& focus,
                          const int delta, const bool wrap) {
  if (words.empty() || lineFirst.empty() || delta == 0) {
    return;
  }
  size_t lineIdx = lineIndexForFocus(lineFirst, words.size(), focus);
  const int targetX = words[focus].screenX + words[focus].screenW / 2;
  const int sign = delta > 0 ? 1 : -1;
  int remaining = delta > 0 ? delta : -delta;
  while (remaining > 0) {
    if (sign < 0) {
      if (lineIdx == 0) {
        if (wrap) {
          focus = words.size() - 1;
        }
        return;
      }
      --lineIdx;
    } else {
      if (lineIdx + 1 >= lineFirst.size()) {
        if (wrap) {
          focus = 0;
        }
        return;
      }
      ++lineIdx;
    }
    --remaining;
  }
  focus = wordOnLineNearestX(words, lineFirst, lineIdx, targetX);
}

/** @return 0 none, 1 bounce consumed (no move), 2 moved */
template <typename MoveWord, typename MoveLine>
int handleDpad(const MappedInputManager& mapped, EdgeState& edge, int& repeatDir, unsigned long& repeatNextMs,
               const unsigned long now, MoveWord&& moveWord, MoveLine&& moveLine) {
  using Btn = MappedInputManager::Button;

  if (mapped.wasPressed(Btn::Left)) {
    if (isBounce(edge, 0, now)) {
      return 1;
    }
    moveWord(-stepsForPress(edge, 0, now));
    repeatDir = 0;
    repeatNextMs = now + kRepeatInitialMs;
    return 2;
  }
  if (mapped.wasPressed(Btn::Right)) {
    if (isBounce(edge, 1, now)) {
      return 1;
    }
    moveWord(stepsForPress(edge, 1, now));
    repeatDir = 1;
    repeatNextMs = now + kRepeatInitialMs;
    return 2;
  }
  if (mapped.wasPressed(Btn::Up)) {
    if (isBounce(edge, 2, now)) {
      return 1;
    }
    moveLine(-stepsForPress(edge, 2, now), true);
    repeatDir = 2;
    repeatNextMs = now + kRepeatInitialMs;
    return 2;
  }
  if (mapped.wasPressed(Btn::Down)) {
    if (isBounce(edge, 3, now)) {
      return 1;
    }
    moveLine(stepsForPress(edge, 3, now), true);
    repeatDir = 3;
    repeatNextMs = now + kRepeatInitialMs;
    return 2;
  }

  const bool leftHeld = mapped.isPressed(Btn::Left);
  const bool rightHeld = mapped.isPressed(Btn::Right);
  const bool upHeld = mapped.isPressed(Btn::Up);
  const bool downHeld = mapped.isPressed(Btn::Down);
  if (!leftHeld && !rightHeld && !upHeld && !downHeld) {
    repeatDir = -1;
    return 0;
  }
  if (repeatDir < 0 || now < repeatNextMs) {
    return 0;
  }
  if (repeatDir == 0 && leftHeld) {
    moveWord(-kRepeatWordStride);
  } else if (repeatDir == 1 && rightHeld) {
    moveWord(kRepeatWordStride);
  } else if (repeatDir == 2 && upHeld) {
    moveLine(-kRepeatLineStride, false);
  } else if (repeatDir == 3 && downHeld) {
    moveLine(kRepeatLineStride, false);
  } else {
    repeatDir = -1;
    return 0;
  }
  repeatNextMs = now + kRepeatIntervalMs;
  return 2;
}

}  // namespace WordOverlayNav
