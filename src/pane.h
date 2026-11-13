#ifndef TAM_EDIT_PANE_H
#define TAM_EDIT_PANE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef TAM_EDIT_SELECTION_DEFINED
#define TAM_EDIT_SELECTION_DEFINED
typedef struct {
    size_t line;
    size_t col;
} loc_t;

typedef struct {
    loc_t anchor;
    loc_t cursor;
    bool  active;
} selection_t;
#endif

typedef struct buffer buffer_t;

typedef struct view {
    buffer_t   *buf;
    size_t      cx_line;
    size_t      cx_col;
    size_t      goal_col;
    size_t      top_line;
    size_t      left_col;
    int         top_subline; /* 0-based visual row offset for softwrap */
    selection_t sel;
    int         x;
    int         y;
    int         w;
    int         h;
    bool        softwrap;
    bool        wordwrap;
    uint64_t    id;
} view_t;

typedef enum {
    PANE_LEAF = 0,
    PANE_SPLIT_V, /* Side-by-side: child1 is left, child2 is right */
    PANE_SPLIT_H  /* Stacked: child1 is top, child2 is bottom */
} pane_type_t;

typedef struct pane_node {
    pane_type_t       type;
    view_t           *view;   /* Non-NULL for PANE_LEAF */
    struct pane_node *child1;
    struct pane_node *child2;
    struct pane_node *parent;
    float             ratio;  /* Portion given to child1 (e.g. 0.5f) */
    int               x;
    int               y;
    int               w;
    int               h;
    uint64_t          id;
} pane_node_t;

typedef struct tab {
    pane_node_t *root;
    view_t      *active_view;
    uint64_t     id;
} tab_t;

typedef struct {
    tab_t **tabs;
    size_t  count;
    size_t  cap;
    size_t  active;
} tab_list_t;

/* View operations */
view_t *view_new(buffer_t *buf);
void    view_free(view_t *v);

/* Split tree operations */
pane_node_t *pane_create_leaf(view_t *v);
pane_node_t *pane_split(pane_node_t *target, pane_type_t type, view_t *new_view);
pane_node_t *pane_close(pane_node_t *root, pane_node_t *target, pane_node_t **new_root);
void         pane_layout(pane_node_t *node, int x, int y, int w, int h);
view_t      *pane_next_view(pane_node_t *root, view_t *cur);
view_t      *pane_prev_view(pane_node_t *root, view_t *cur);
view_t      *pane_hit_test(pane_node_t *node, int screen_x, int screen_y);
pane_node_t *pane_find_view(pane_node_t *node, view_t *v);
void         pane_free_tree(pane_node_t *node);

/* Tab operations */
tab_t      *tab_new(view_t *v);
void        tab_free(tab_t *tab);
const char *tab_get_title(const tab_t *tab, char *buf, size_t buflen);
bool        tab_is_dirty(const tab_t *tab);

/* Tab list operations */
void    tab_list_init(tab_list_t *tl);
void    tab_list_free(tab_list_t *tl);
tab_t  *tab_list_add(tab_list_t *tl, view_t *v);
bool    tab_list_close(tab_list_t *tl, size_t index);
void    tab_list_next(tab_list_t *tl);
void    tab_list_prev(tab_list_t *tl);

#endif
