#include "pane.h"
#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_view_id_seq = 1;
static uint64_t g_pane_id_seq = 1;
static uint64_t g_tab_id_seq  = 1;

view_t *view_new(buffer_t *buf)
{
    view_t *v = calloc(1, sizeof(*v));
    if (v == NULL) return NULL;

    v->id       = g_view_id_seq++;
    v->buf      = buf;
    v->softwrap = false;
    v->wordwrap = true;
    v->w        = 80;
    v->h        = 24;
    buffer_ref(buf);
    return v;
}

void view_free(view_t *v)
{
    if (v == NULL) return;
    buffer_unref(v->buf);
    free(v);
}

pane_node_t *pane_create_leaf(view_t *v)
{
    pane_node_t *n = calloc(1, sizeof(*n));
    if (n == NULL) return NULL;
    n->id    = g_pane_id_seq++;
    n->type  = PANE_LEAF;
    n->view  = v;
    n->ratio = 0.5f;
    return n;
}

void pane_free_tree(pane_node_t *node)
{
    if (node == NULL) return;
    if (node->type == PANE_LEAF) {
        view_free(node->view);
    } else {
        pane_free_tree(node->child1);
        pane_free_tree(node->child2);
    }
    free(node);
}

pane_node_t *pane_split(pane_node_t *target, pane_type_t type, view_t *new_view)
{
    if (target == NULL || target->type != PANE_LEAF || new_view == NULL) {
        return NULL;
    }

    pane_node_t *leaf1 = pane_create_leaf(target->view);
    pane_node_t *leaf2 = pane_create_leaf(new_view);
    if (leaf1 == NULL || leaf2 == NULL) {
        free(leaf1);
        free(leaf2);
        return NULL;
    }

    target->type   = type;
    target->view   = NULL;
    target->child1 = leaf1;
    target->child2 = leaf2;
    target->ratio  = 0.5f;

    leaf1->parent = target;
    leaf2->parent = target;

    pane_layout(target, target->x, target->y, target->w, target->h);
    return leaf2;
}

pane_node_t *pane_close(pane_node_t *root, pane_node_t *target, pane_node_t **new_root)
{
    if (target == NULL || target->type != PANE_LEAF) return NULL;

    if (target == root) {
        pane_free_tree(target);
        *new_root = NULL;
        return NULL;
    }

    pane_node_t *parent = target->parent;
    if (parent == NULL) return NULL;

    pane_node_t *sibling = (parent->child1 == target) ? parent->child2 : parent->child1;
    sibling->parent = parent->parent;

    if (parent->parent != NULL) {
        if (parent->parent->child1 == parent) {
            parent->parent->child1 = sibling;
        } else {
            parent->parent->child2 = sibling;
        }
        *new_root = root;
    } else {
        *new_root = sibling;
    }

    /* Free target and parent node shell */
    view_free(target->view);
    free(target);
    free(parent);

    if (*new_root != NULL) {
        pane_layout(*new_root, (*new_root)->x, (*new_root)->y,
                    (*new_root)->w, (*new_root)->h);
    }

    return *new_root;
}

void pane_layout(pane_node_t *node, int x, int y, int w, int h)
{
    if (node == NULL) return;
    node->x = x;
    node->y = y;
    node->w = w;
    node->h = h;

    if (node->type == PANE_LEAF) {
        if (node->view != NULL) {
            node->view->x = x;
            node->view->y = y;
            node->view->w = w;
            node->view->h = h;
        }
        return;
    }

    if (node->type == PANE_SPLIT_V) {
        /* Vertical split: child1 on left, child2 on right, separated by 1 col */
        int avail = w > 1 ? w - 1 : 1;
        int w1 = (int)(avail * node->ratio);
        if (w1 < 1) w1 = 1;
        if (w1 >= avail) w1 = avail - 1;
        int w2 = avail - w1;
        if (w2 < 1) w2 = 1;

        pane_layout(node->child1, x, y, w1, h);
        pane_layout(node->child2, x + w1 + 1, y, w2, h);
    } else if (node->type == PANE_SPLIT_H) {
        /* Horizontal split: child1 on top, child2 on bottom */
        int avail = h > 1 ? h : 1;
        int h1 = (int)(avail * node->ratio);
        if (h1 < 1) h1 = 1;
        if (h1 >= avail) h1 = avail - 1;
        int h2 = avail - h1;
        if (h2 < 1) h2 = 1;

        pane_layout(node->child1, x, y, w, h1);
        pane_layout(node->child2, x, y + h1, w, h2);
    }
}

static void collect_views(pane_node_t *node, view_t **list, size_t *count, size_t max_count)
{
    if (node == NULL || *count >= max_count) return;
    if (node->type == PANE_LEAF) {
        if (node->view != NULL) {
            list[(*count)++] = node->view;
        }
        return;
    }
    collect_views(node->child1, list, count, max_count);
    collect_views(node->child2, list, count, max_count);
}

view_t *pane_next_view(pane_node_t *root, view_t *cur)
{
    view_t *views[64];
    size_t count = 0;
    collect_views(root, views, &count, 64);
    if (count == 0) return NULL;
    if (cur == NULL) return views[0];

    for (size_t i = 0; i < count; i++) {
        if (views[i] == cur) {
            return views[(i + 1) % count];
        }
    }
    return views[0];
}

view_t *pane_prev_view(pane_node_t *root, view_t *cur)
{
    view_t *views[64];
    size_t count = 0;
    collect_views(root, views, &count, 64);
    if (count == 0) return NULL;
    if (cur == NULL) return views[count - 1];

    for (size_t i = 0; i < count; i++) {
        if (views[i] == cur) {
            return views[(i + count - 1) % count];
        }
    }
    return views[count - 1];
}

view_t *pane_hit_test(pane_node_t *node, int screen_x, int screen_y)
{
    if (node == NULL) return NULL;
    if (screen_x < node->x || screen_x >= node->x + node->w ||
        screen_y < node->y || screen_y >= node->y + node->h) {
        return NULL;
    }
    if (node->type == PANE_LEAF) {
        return node->view;
    }
    view_t *hit = pane_hit_test(node->child1, screen_x, screen_y);
    if (hit != NULL) return hit;
    return pane_hit_test(node->child2, screen_x, screen_y);
}

pane_node_t *pane_find_view(pane_node_t *node, view_t *v)
{
    if (node == NULL || v == NULL) return NULL;
    if (node->type == PANE_LEAF) {
        return (node->view == v) ? node : NULL;
    }
    pane_node_t *found = pane_find_view(node->child1, v);
    if (found != NULL) return found;
    return pane_find_view(node->child2, v);
}

/* -------------------------------------------------------------------------- */
/* Tab Operations                                                             */
/* -------------------------------------------------------------------------- */

tab_t *tab_new(view_t *v)
{
    tab_t *t = calloc(1, sizeof(*t));
    if (t == NULL) return NULL;

    t->id          = g_tab_id_seq++;
    t->active_view = v;
    t->root        = pane_create_leaf(v);
    return t;
}

void tab_free(tab_t *tab)
{
    if (tab == NULL) return;
    pane_free_tree(tab->root);
    free(tab);
}

const char *tab_get_title(const tab_t *tab, char *buf, size_t buflen)
{
    if (tab == NULL || tab->active_view == NULL || tab->active_view->buf == NULL) {
        snprintf(buf, buflen, "[No Name]");
        return buf;
    }
    const char *p = tab->active_view->buf->path;
    if (p == NULL || *p == '\0') {
        snprintf(buf, buflen, "[No Name]");
    } else {
        const char *base = strrchr(p, '/');
        if (base != NULL) base++;
        else base = p;
        snprintf(buf, buflen, "%s", base);
    }
    return buf;
}

static bool node_is_dirty(pane_node_t *node)
{
    if (node == NULL) return false;
    if (node->type == PANE_LEAF) {
        return (node->view && node->view->buf && node->view->buf->dirty);
    }
    return node_is_dirty(node->child1) || node_is_dirty(node->child2);
}

bool tab_is_dirty(const tab_t *tab)
{
    if (tab == NULL) return false;
    return node_is_dirty(tab->root);
}

/* -------------------------------------------------------------------------- */
/* Tab List Operations                                                        */
/* -------------------------------------------------------------------------- */

void tab_list_init(tab_list_t *tl)
{
    memset(tl, 0, sizeof(*tl));
}

void tab_list_free(tab_list_t *tl)
{
    for (size_t i = 0; i < tl->count; i++) {
        tab_free(tl->tabs[i]);
    }
    free(tl->tabs);
    memset(tl, 0, sizeof(*tl));
}

tab_t *tab_list_add(tab_list_t *tl, view_t *v)
{
    tab_t *t = tab_new(v);
    if (t == NULL) return NULL;

    if (tl->count >= tl->cap) {
        size_t new_cap = tl->cap ? tl->cap * 2 : 4;
        tab_t **new_tabs = realloc(tl->tabs, new_cap * sizeof(tab_t *));
        if (new_tabs == NULL) {
            tab_free(t);
            return NULL;
        }
        tl->tabs = new_tabs;
        tl->cap  = new_cap;
    }

    tl->tabs[tl->count] = t;
    tl->active = tl->count;
    tl->count++;
    return t;
}

bool tab_list_close(tab_list_t *tl, size_t index)
{
    if (index >= tl->count) return false;
    tab_free(tl->tabs[index]);

    memmove(tl->tabs + index, tl->tabs + index + 1,
            (tl->count - index - 1) * sizeof(tab_t *));
    tl->count--;

    if (tl->active >= tl->count && tl->count > 0) {
        tl->active = tl->count - 1;
    }
    return true;
}

void tab_list_next(tab_list_t *tl)
{
    if (tl->count > 1) {
        tl->active = (tl->active + 1) % tl->count;
    }
}

void tab_list_prev(tab_list_t *tl)
{
    if (tl->count > 1) {
        tl->active = (tl->active + tl->count - 1) % tl->count;
    }
}
