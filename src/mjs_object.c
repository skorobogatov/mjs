/*
 * Copyright (c) 2016 Cesanta Software Limited
 * All rights reserved
 */

#include "mjs_object.h"
#include "mjs_conversion.h"
#include "mjs_core.h"
#include "mjs_internal.h"
#include "mjs_primitive.h"
#include "mjs_string.h"
#include "mjs_util.h"

#include "common/mg_str.h"

MJS_PRIVATE mjs_val_t mjs_object_to_value(struct mjs_object *o) {
  if (o == NULL) {
    return MJS_NULL;
  } else {
    return mjs_legit_pointer_to_value(o) | MJS_TAG_OBJECT;
  }
}

MJS_PRIVATE struct mjs_object *get_object_struct(mjs_val_t v) {
  struct mjs_object *ret = NULL;
  if (mjs_is_null(v)) {
    ret = NULL;
  } else {
    assert(mjs_is_object(v));
    ret = (struct mjs_object *) get_ptr(v);
  }
  return ret;
}

mjs_val_t mjs_mk_object(struct mjs *mjs) {
  struct mjs_object *o = new_object(mjs);
  if (o == NULL) {
    return MJS_NULL;
  }
  (void) mjs;
  o->tree = NULL;
  o->prop_count = 0;
  return mjs_object_to_value(o);
}

int mjs_is_object(mjs_val_t v) {
  return (v & MJS_TAG_MASK) == MJS_TAG_OBJECT ||
         (v & MJS_TAG_MASK) == MJS_TAG_ARRAY;
}

MJS_PRIVATE struct mjs_node *mjs_descend(struct mjs_node *x,
                                         const char *name,
                                         size_t name_len) {
  uintptr_t child;
  do {
    struct mjs_position pos = x->pos;
    uint8_t c = pos.byte < name_len ? (uint8_t)(name[pos.byte]) : 0;
    int dir = (1 + (int)(pos.mask | c)) >> 8;
    child = x->child[dir];
    x = DECODE_NODE(child);
  } while (IS_INNER_NODE(child));
  return x;
}

MJS_PRIVATE struct mjs_node *mjs_get_own_node(struct mjs *mjs,
                                              mjs_val_t obj,
                                              const char *name,
                                              size_t name_len) {
  if (!mjs_is_object(obj)) {
    return NULL;
  }

  struct mjs_object *o = get_object_struct(obj);
  struct mjs_node *leaf;

  switch (o->prop_count) {
  case 0:
    return 0;
  case 1:
    leaf = o->tree;
    break;
  default:
    leaf = mjs_descend(o->tree, name, name_len);
  }

  if (name_len <= 5) {
    mjs_val_t ss = mjs_mk_string(mjs, name, name_len, 1);
    if (leaf->name != ss) {
      return NULL;
    }
  } else {
    if (mjs_strcmp(mjs, &leaf->name, name, name_len) != 0) {
      return NULL;
    }
  }

  return leaf;
}

MJS_PRIVATE struct mjs_node *mjs_get_own_node_v(struct mjs *mjs,
                                                mjs_val_t obj,
                                                mjs_val_t key) {
  size_t n;
  char *s = NULL;
  int need_free = 0;
  struct mjs_node *leaf = NULL;
  mjs_err_t err = mjs_to_string(mjs, &key, &s, &n, &need_free);
  if (err == MJS_OK) {
    leaf = mjs_get_own_node(mjs, obj, s, n);
  }
  if (need_free) free(s);
  return leaf;
}

mjs_val_t mjs_get(struct mjs *mjs, mjs_val_t obj, const char *name,
                  size_t name_len) {
  if (name_len == (size_t) ~0) {
    name_len = strlen(name);
  }

  struct mjs_node *leaf = mjs_get_own_node(mjs, obj, name, name_len);
  return leaf == NULL ? MJS_UNDEFINED : leaf->value;
}

mjs_val_t mjs_get_v(struct mjs *mjs, mjs_val_t obj, mjs_val_t name) {
  size_t n;
  char *s = NULL;
  int need_free = 0;
  mjs_val_t ret = MJS_UNDEFINED;

  mjs_err_t err = mjs_to_string(mjs, &name, &s, &n, &need_free);

  if (err == MJS_OK) {
    /* Successfully converted name value to string: get the property */
    ret = mjs_get(mjs, obj, s, n);
  }

  if (need_free) {
    free(s);
    s = NULL;
  }
  return ret;
}

mjs_val_t mjs_get_v_proto(struct mjs *mjs, mjs_val_t obj, mjs_val_t key) {
  mjs_val_t res;

  struct mjs_node *leaf = mjs_get_own_node_v(mjs, obj, key);
  if (leaf != NULL) {
    res = leaf->value;
  } else {
    mjs_val_t pn = mjs_mk_string(mjs, MJS_PROTO_PROP_NAME, ~0, 1);
    leaf = mjs_get_own_node_v(mjs, obj, pn);
    res = leaf ? mjs_get_v_proto(mjs, leaf->value, key) : MJS_UNDEFINED;
  }

  return res;
}

mjs_err_t mjs_set(struct mjs *mjs, mjs_val_t obj, const char *name,
                  size_t name_len, mjs_val_t val) {
  return mjs_set_internal(mjs, obj, MJS_UNDEFINED, (char *) name, name_len,
                          val);
}

mjs_err_t mjs_set_v(struct mjs *mjs, mjs_val_t obj, mjs_val_t name,
                    mjs_val_t val) {
  return mjs_set_internal(mjs, obj, name, NULL, 0, val);
}

MJS_PRIVATE mjs_err_t mjs_set_internal(struct mjs *mjs, mjs_val_t obj,
                                       mjs_val_t name_v, char *name,
                                       size_t name_len, mjs_val_t val) {
  if (!mjs_is_object(obj)) {
    return MJS_REFERENCE_ERROR;
  }

  int need_free = 0;

  if (name == NULL) {
    /* Pointer was not provided, so obtain one from the name_v. */
    mjs_err_t rcode = mjs_to_string(mjs, &name_v, &name, &name_len, &need_free);
    if (rcode != MJS_OK) {
      return rcode;
    }
  } else {
    if (name_len == ~((size_t)0)) {
      name_len = strlen(name);
    }
    name_v = MJS_UNDEFINED;
  }

  struct mjs_node *leaf, *new_leaf;
  uintptr_t root;
  struct mjs_object *o = get_object_struct(obj);

  switch (o->prop_count) {
  case 0:
    new_leaf = new_node(mjs);
    new_leaf->parent = NULL;
    new_leaf->value = val;
    o->tree = new_leaf;
    goto save_name_v;
  case 1:
    leaf = o->tree;
    root = ENCODE_LEAF_NODE(leaf);
    break;
  default:
    leaf = mjs_descend(o->tree, name, name_len);
    root = ENCODE_INNER_NODE(o->tree);
  }

  size_t leaf_name_len;
  const char *leaf_name = mjs_get_string(mjs, &leaf->name, &leaf_name_len);

  size_t min_len = name_len < leaf_name_len ? name_len : leaf_name_len;
  size_t byte = 0;
  while (byte < min_len && name[byte] == leaf_name[byte]) {
    byte++;
  }

  if (byte == min_len && name_len == leaf_name_len) {
    leaf->value = val;
    goto clean;
  }

  uint8_t c = byte < name_len ? name[byte] : 0;
  uint8_t leaf_c = byte < leaf_name_len ? leaf_name[byte] : 0;

  int n = __builtin_ctz(c ^ leaf_c);
  struct mjs_position new_pos = { ~(1 << n), byte };
  int new_dir = (leaf_c >> n) & 1;

  struct mjs_node *new_inner_node = new_node(mjs);
  new_inner_node->pos = new_pos;

  new_leaf = new_node(mjs);
  new_leaf->parent = new_inner_node;
  new_leaf->value = val;

  new_inner_node->child[1 - new_dir] = ENCODE_LEAF_NODE(new_leaf);

  struct mjs_node *x;
  uintptr_t *where = &root;
  while (IS_INNER_NODE(*where)) {
    struct mjs_node *x = DECODE_NODE(*where);
    struct mjs_position pos = x->pos;
    if (POSITION_LESS(new_pos, pos)) {
      break;
    }

    c = pos.byte < name_len ? (uint8_t)(name[pos.byte]) : 0;
    int dir = (1 + (int)(pos.mask | c)) >> 8;
    where = &(x->child[dir]);
  }

  new_inner_node->child[new_dir] = *where;
  x = DECODE_NODE(*where);
  new_inner_node->parent = x->parent;
  x->parent = new_inner_node;
  *where = ENCODE_INNER_NODE(new_inner_node);
  o->tree = DECODE_NODE(root);

save_name_v:
  if (!mjs_is_string(name_v)) {
    /* We intentially convert 'name' into value here, because 'mjs_mk_string'
       function can reallocate string buffer, thus invalidating the 'name'
       pointer! */
    new_leaf->name = mjs_mk_string(mjs, name, name_len, 1);
  } else {
    new_leaf->name = name_v;
  }

  o->prop_count++;

clean:
  if (need_free) {
    free(name);
  }
  return MJS_OK;
}

/*
 * See comments in `object_public.h`
 */
int mjs_del(struct mjs *mjs, mjs_val_t obj, const char *name, size_t len) {
  if (!mjs_is_object(obj)) {
    return -1;
  }

  if (len == (size_t) ~0) {
    len = strlen(name);
  }

  struct mjs_node *x = mjs_get_own_node(mjs, obj, name, len);
  if (x == NULL) {
    return -1;
  }

  struct mjs_object *o = get_object_struct(obj);
  struct mjs_node *y = x->parent;
  if (y == NULL) {
    o->tree = NULL;
  } else {
    int dir = y->child[0] == ENCODE_LEAF_NODE(x) ? 1 : 0;
    uintptr_t encoded_z = y->child[dir];
    struct mjs_node *z = DECODE_NODE(encoded_z);
    struct mjs_node *parent = y->parent;
    if (parent == NULL) {
      o->tree = z;
    } else {
      dir = parent->child[0] == ENCODE_INNER_NODE(y) ? 0 : 1;
      parent->child[dir] = encoded_z;
    }
    z->parent = parent;
  }
  o->prop_count--;
  return 0;
}

mjs_val_t mjs_next_node(struct mjs *mjs, mjs_val_t obj, mjs_val_t *iterator) {
  struct mjs_node *x, *y;
  uintptr_t encoded_x;
  mjs_val_t key = MJS_UNDEFINED;

  if (*iterator == MJS_UNDEFINED) {
    struct mjs_object *o = get_object_struct(obj);
    switch (o->prop_count) {
    case 0:
      *iterator = MJS_UNDEFINED;
      break;
    case 1:
      key = o->tree->name;
      *iterator = mjs_mk_foreign(mjs, o->tree);
      break;
    default:
      x = o->tree;
      do {
        encoded_x = x->child[0];
        x = DECODE_NODE(encoded_x);
      } while (IS_INNER_NODE(encoded_x));

      key = x->name;
      *iterator = mjs_mk_foreign(mjs, x);
    }
  } else {
    x = (struct mjs_node*)get_ptr(*iterator);
    encoded_x = ENCODE_LEAF_NODE(x);

    for (;;) {
      y = x->parent;
      if (y == NULL) {
        *iterator = MJS_UNDEFINED;
        break;
      }

      if (encoded_x == y->child[0]) {
        encoded_x = y->child[1];
        x = DECODE_NODE(encoded_x);
        while (IS_INNER_NODE(encoded_x)) {
          encoded_x = x->child[0];
          x = DECODE_NODE(encoded_x);
        }

        key = x->name;
        *iterator = mjs_mk_foreign(mjs, x);
        break;
      }

      x = y;
      encoded_x = ENCODE_INNER_NODE(x);
    }
  }

  return key;
}

MJS_PRIVATE void mjs_op_create_object(struct mjs *mjs) {
  mjs_val_t ret = MJS_UNDEFINED;
  mjs_val_t proto_v = mjs_arg(mjs, 0);

  if (!mjs_check_arg(mjs, 0, "proto", MJS_TYPE_OBJECT_GENERIC, &proto_v)) {
    goto clean;
  }

  ret = mjs_mk_object(mjs);
  mjs_set(mjs, ret, MJS_PROTO_PROP_NAME, ~0, proto_v);

clean:
  mjs_return(mjs, ret);
}

mjs_val_t mjs_struct_to_obj(struct mjs *mjs, const void *base,
                            const struct mjs_c_struct_member *defs) {
  mjs_val_t obj;
  const struct mjs_c_struct_member *def = defs;
  if (base == NULL || def == NULL) return MJS_UNDEFINED;
  obj = mjs_mk_object(mjs);
  /* Pin the object while it is being built */
  mjs_own(mjs, &obj);
  /*
   * Because mjs inserts new properties at the head of the list,
   * start from the end so the constructed object more closely resembles
   * the definition.
   */
  while (def->name != NULL) def++;
  for (def--; def >= defs; def--) {
    mjs_val_t v = MJS_UNDEFINED;
    const char *ptr = (const char *) base + def->offset;
    switch (def->type) {
      case MJS_STRUCT_FIELD_TYPE_STRUCT: {
        const void *sub_base = (const void *) ptr;
        const struct mjs_c_struct_member *sub_def =
            (const struct mjs_c_struct_member *) def->arg;
        v = mjs_struct_to_obj(mjs, sub_base, sub_def);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_STRUCT_PTR: {
        const void **sub_base = (const void **) ptr;
        const struct mjs_c_struct_member *sub_def =
            (const struct mjs_c_struct_member *) def->arg;
        if (*sub_base != NULL) {
          v = mjs_struct_to_obj(mjs, *sub_base, sub_def);
        } else {
          v = MJS_NULL;
        }
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_INT: {
        double value = (double) (*(int *) ptr);
        v = mjs_mk_number(mjs, value);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_BOOL: {
        v = mjs_mk_boolean(mjs, *(bool *) ptr);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_DOUBLE: {
        v = mjs_mk_number(mjs, *(double *) ptr);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_FLOAT: {
        float value = *(float *) ptr;
        v = mjs_mk_number(mjs, value);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_CHAR_PTR: {
        const char *value = *(const char **) ptr;
        v = mjs_mk_string(mjs, value, ~0, 1);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_VOID_PTR: {
        v = mjs_mk_foreign(mjs, *(void **) ptr);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_MG_STR_PTR: {
        const struct mg_str *s = *(const struct mg_str **) ptr;
        if (s != NULL) {
          v = mjs_mk_string(mjs, s->p, s->len, 1);
        } else {
          v = MJS_NULL;
        }
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_MG_STR: {
        const struct mg_str *s = (const struct mg_str *) ptr;
        v = mjs_mk_string(mjs, s->p, s->len, 1);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_DATA: {
        const char *dptr = (const char *) ptr;
        const intptr_t dlen = (intptr_t) def->arg;
        v = mjs_mk_string(mjs, dptr, dlen, 1);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_INT8: {
        double value = (double) (*(int8_t *) ptr);
        v = mjs_mk_number(mjs, value);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_INT16: {
        double value = (double) (*(int16_t *) ptr);
        v = mjs_mk_number(mjs, value);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_UINT8: {
        double value = (double) (*(uint8_t *) ptr);
        v = mjs_mk_number(mjs, value);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_UINT16: {
        double value = (double) (*(uint16_t *) ptr);
        v = mjs_mk_number(mjs, value);
        break;
      }
      case MJS_STRUCT_FIELD_TYPE_CUSTOM: {
        mjs_val_t (*fptr)(struct mjs *, const void *) =
            (mjs_val_t (*) (struct mjs *, const void *)) def->arg;
        v = fptr(mjs, ptr);
      }
      default: { break; }
    }
    mjs_set(mjs, obj, def->name, ~0, v);
  }
  mjs_disown(mjs, &obj);
  return obj;
}
