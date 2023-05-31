/*
 * Copyright (c) 2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef MJS_OBJECT_H_
#define MJS_OBJECT_H_

#include "mjs_object_public.h"
#include "mjs_internal.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

struct mjs;

struct mjs_position {
    uint32_t mask : 8;
    uint32_t byte : 24;
};

#define POSITION_LESS(a, b) \
  ((a).byte < (b).byte || ((a).byte == (b).byte && (a).mask > (b).mask))

struct mjs_node {
  struct mjs_node *parent;
  union {
    struct {
      uintptr_t child[2];
      struct mjs_position pos;
    };
    struct {
      mjs_val_t name;   /* Property name (a string) */
      mjs_val_t value;  /* Property value */
    };
  };
};

#define IS_INNER_NODE(child) (((child) & 1) != 0)

#define IS_LEAF_NODE(child) (((child) & 1) == 0)

#define DECODE_NODE(child) ((struct mjs_node*)((child) & (uintptr_t)~1))

#define ENCODE_INNER_NODE(node) ((uintptr_t)(node) | 1)

#define ENCODE_LEAF_NODE(node) ((uintptr_t)(node))

struct mjs_object {
  struct mjs_node *tree;
  size_t prop_count;
};

MJS_PRIVATE struct mjs_object *get_object_struct(mjs_val_t v);

MJS_PRIVATE struct mjs_node *mjs_get_own_node(struct mjs *mjs,
                                              mjs_val_t obj,
                                              const char *name,
                                              size_t name_len);

MJS_PRIVATE struct mjs_node *mjs_get_own_node_v(struct mjs *mjs,
                                                mjs_val_t obj,
                                                mjs_val_t key);

mjs_val_t mjs_next_node(struct mjs *mjs, mjs_val_t obj, mjs_val_t *iterator);

/*
 * A worker function for `mjs_set()` and `mjs_set_v()`: it takes name as both
 * ptr+len and mjs_val_t. If `name` pointer is not NULL, it takes precedence
 * over `name_v`.
 */
MJS_PRIVATE mjs_err_t mjs_set_internal(struct mjs *mjs, mjs_val_t obj,
                                       mjs_val_t name_v, char *name,
                                       size_t name_len, mjs_val_t val);

/*
 * Implementation of `Object.create(proto)`
 */
MJS_PRIVATE void mjs_op_create_object(struct mjs *mjs);

#define MJS_PROTO_PROP_NAME "__p" /* Make it < 5 chars */

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* MJS_OBJECT_H_ */
