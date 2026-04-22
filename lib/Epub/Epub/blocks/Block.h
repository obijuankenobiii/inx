#pragma once

/**
 * @file Block.h
 * @brief Public interface and types for Block.
 */

class GfxRenderer;

typedef enum { TEXT_BLOCK, IMAGE_BLOCK } BlockType;


class Block {
 public:
  virtual ~Block() = default;
  virtual void layout(GfxRenderer& renderer) = 0;
  virtual BlockType getType() = 0;
  virtual bool isEmpty() = 0;
  virtual void finish() {}
};
