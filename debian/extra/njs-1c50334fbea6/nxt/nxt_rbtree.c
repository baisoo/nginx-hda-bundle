
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_rbtree.h>


/*
 * The red-black tree code is based on the algorithm described in
 * the "Introduction to Algorithms" by Cormen, Leiserson and Rivest.
 */


static void nxt_rbtree_insert_fixup(nxt_rbtree_node_t *node);
static void nxt_rbtree_delete_fixup(nxt_rbtree_t *tree, nxt_rbtree_node_t *node);
nxt_inline void nxt_rbtree_left_rotate(nxt_rbtree_node_t *node);
nxt_inline void nxt_rbtree_right_rotate(nxt_rbtree_node_t *node);
nxt_inline void nxt_rbtree_parent_relink(nxt_rbtree_node_t *subst,
    nxt_rbtree_node_t *node);


#define NXT_RBTREE_BLACK  0
#define NXT_RBTREE_RED    1


#define nxt_rbtree_set_callback_type(tree, type)                               \
    (tree)->sentinel.spare = type

#define nxt_rbtree_has_insertion_callback(tree)                                \
    ((tree)->sentinel.spare != 0)

#define nxt_rbtree_comparison_callback(tree)                                   \
    ((nxt_rbtree_compare_t) (tree)->sentinel.right)


void
nxt_rbtree_init(nxt_rbtree_t *tree, nxt_rbtree_compare_t compare)
{
    /*
     * The sentinel is used as a leaf node sentinel and as a tree root
     * sentinel: it is a parent of a root node and the root node is
     * the left child of the sentinel.  Combining two sentinels in one
     * entry and the fact that the sentinel's left child is a root node
     * simplifies nxt_rbtree_node_successor() and eliminates explicit
     * root node test before or inside nxt_rbtree_min().
     */

    /* The root is empty. */
    tree->sentinel.left = &tree->sentinel;

    /*
     * The sentinel's right child is never used so
     * or comparison callback can be safely stored here.
     */
    tree->sentinel.right = (void *) compare;

    /* The root and leaf sentinel must be black. */
    tree->sentinel.color = NXT_RBTREE_BLACK;
}


void
nxt_rbtree_insert(nxt_rbtree_t *tree, nxt_rbtree_part_t *part)
{
    nxt_rbtree_node_t     *node, *new_node, *sentinel, **child;
    nxt_rbtree_compare_t  compare;

    new_node = (nxt_rbtree_node_t *) part;

    node = nxt_rbtree_root(tree);
    sentinel = nxt_rbtree_sentinel(tree);

    new_node->left = sentinel;
    new_node->right = sentinel;
    new_node->color = NXT_RBTREE_RED;

    compare = (nxt_rbtree_compare_t) tree->sentinel.right;
    child = &nxt_rbtree_root(tree);

    while (*child != sentinel) {
        node = *child;

        nxt_prefetch(node->left);
        nxt_prefetch(node->right);

        child = (compare(new_node, node) < 0) ? &node->left : &node->right;
    }

    *child = new_node;
    new_node->parent = node;

    nxt_rbtree_insert_fixup(new_node);

    node = nxt_rbtree_root(tree);
    node->color = NXT_RBTREE_BLACK;
}


static void
nxt_rbtree_insert_fixup(nxt_rbtree_node_t *node)
{
    nxt_rbtree_node_t  *parent, *grandparent, *uncle;

    /*
     * Prefetching parent nodes does not help here because they are
     * already traversed during insertion.
     */

    for ( ;; ) {
        parent = node->parent;

        /*
         * Testing whether a node is a tree root is not required here since
         * a root node's parent is the sentinel and it is always black.
         */
        if (parent->color == NXT_RBTREE_BLACK) {
            return;
        }

        grandparent = parent->parent;

        if (parent == grandparent->left) {
            uncle = grandparent->right;

            if (uncle->color == NXT_RBTREE_BLACK) {

                if (node == parent->right) {
                    node = parent;
                    nxt_rbtree_left_rotate(node);
                }

                parent = node->parent;
                parent->color = NXT_RBTREE_BLACK;

                grandparent = parent->parent;
                grandparent->color = NXT_RBTREE_RED;
                nxt_rbtree_right_rotate(grandparent);

                continue;
            }

        } else {
            uncle = grandparent->left;

            if (uncle->color == NXT_RBTREE_BLACK) {

                if (node == parent->left) {
                    node = parent;
                    nxt_rbtree_right_rotate(node);
                }

                parent = node->parent;
                parent->color = NXT_RBTREE_BLACK;

                grandparent = parent->parent;
                grandparent->color = NXT_RBTREE_RED;
                nxt_rbtree_left_rotate(grandparent);

                continue;
            }
        }

        uncle->color = NXT_RBTREE_BLACK;
        parent->color = NXT_RBTREE_BLACK;
        grandparent->color = NXT_RBTREE_RED;

        node = grandparent;
    }
}


nxt_rbtree_node_t *
nxt_rbtree_find(nxt_rbtree_t *tree, nxt_rbtree_part_t *part)
{
    nxt_int_t             n;
    nxt_rbtree_node_t     *node, *next, *sentinel;
    nxt_rbtree_compare_t  compare;

    node = (nxt_rbtree_node_t *) part;

    next = nxt_rbtree_root(tree);
    sentinel = nxt_rbtree_sentinel(tree);
    compare = nxt_rbtree_comparison_callback(tree);

    while (next != sentinel) {
        nxt_prefetch(next->left);
        nxt_prefetch(next->right);

        n = compare(node, next);

        if (n < 0) {
            next = next->left;

        } else if (n > 0) {
            next = next->right;

        } else {
            return next;
        }
    }

    return NULL;
}


nxt_rbtree_node_t *
nxt_rbtree_find_less_or_equal(nxt_rbtree_t *tree, nxt_rbtree_part_t *part)
{
    nxt_int_t             n;
    nxt_rbtree_node_t     *node, *retval, *next, *sentinel;
    nxt_rbtree_compare_t  compare;

    node = (nxt_rbtree_node_t *) part;

    retval = NULL;
    next = nxt_rbtree_root(tree);
    sentinel = nxt_rbtree_sentinel(tree);
    compare = nxt_rbtree_comparison_callback(tree);

    while (next != sentinel) {
        nxt_prefetch(next->left);
        nxt_prefetch(next->right);

        n = compare(node, next);

        if (n < 0) {
            next = next->left;

        } else if (n > 0) {
            retval = next;
            next = next->right;

        } else {
            /* Exact match. */
            return next;
        }
    }

    return retval;
}


nxt_rbtree_node_t *
nxt_rbtree_find_greater_or_equal(nxt_rbtree_t *tree, nxt_rbtree_part_t *part)
{
    nxt_int_t             n;
    nxt_rbtree_node_t     *node, *retval, *next, *sentinel;
    nxt_rbtree_compare_t  compare;

    node = (nxt_rbtree_node_t *) part;

    retval = NULL;
    next = nxt_rbtree_root(tree);
    sentinel = nxt_rbtree_sentinel(tree);
    compare = nxt_rbtree_comparison_callback(tree);

    while (next != sentinel) {
        nxt_prefetch(next->left);
        nxt_prefetch(next->right);

        n = compare(node, next);

        if (n < 0) {
            retval = next;
            next = next->left;

        } else if (n > 0) {
            next = next->right;

        } else {
            /* Exact match. */
            return next;
        }
    }

    return retval;
}


void
nxt_rbtree_delete(nxt_rbtree_t *tree, nxt_rbtree_part_t *part)
{
    nxt_uint_t         color;
    nxt_rbtree_node_t  *node, *sentinel, *subst, *child;

    node = (nxt_rbtree_node_t *) part;

    subst = node;
    sentinel = nxt_rbtree_sentinel(tree);

    if (node->left == sentinel) {
        child = node->right;

    } else if (node->right == sentinel) {
        child = node->left;

    } else {
        subst = nxt_rbtree_branch_min(tree, node->right);
        child = subst->right;
    }

    nxt_rbtree_parent_relink(child, subst);

    color = subst->color;

    if (subst != node) {
        /* Move the subst node to the deleted node position in the tree. */

        subst->color = node->color;

        subst->left = node->left;
        subst->left->parent = subst;

        subst->right = node->right;
        subst->right->parent = subst;

        nxt_rbtree_parent_relink(subst, node);
    }

#if (NXT_DEBUG)
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
#endif

    if (color == NXT_RBTREE_BLACK) {
        nxt_rbtree_delete_fixup(tree, child);
    }
}


static void
nxt_rbtree_delete_fixup(nxt_rbtree_t *tree, nxt_rbtree_node_t *node)
{
    nxt_rbtree_node_t  *parent, *sibling;

    while (node != nxt_rbtree_root(tree) && node->color == NXT_RBTREE_BLACK) {
        /*
         * Prefetching parent nodes does not help here according
         * to microbenchmarks.
         */

        parent = node->parent;

        if (node == parent->left) {
            sibling = parent->right;

            if (sibling->color != NXT_RBTREE_BLACK) {

                sibling->color = NXT_RBTREE_BLACK;
                parent->color = NXT_RBTREE_RED;

                nxt_rbtree_left_rotate(parent);

                sibling = parent->right;
            }

            if (sibling->right->color == NXT_RBTREE_BLACK) {

                sibling->color = NXT_RBTREE_RED;

                if (sibling->left->color == NXT_RBTREE_BLACK) {
                    node = parent;
                    continue;
                }

                sibling->left->color = NXT_RBTREE_BLACK;

                nxt_rbtree_right_rotate(sibling);
                /*
                 * If the node is the leaf sentinel then the right
                 * rotate above changes its parent so a sibling below
                 * becames the leaf sentinel as well and this causes
                 * segmentation fault.  This is the reason why usual
                 * red-black tree implementations with a leaf sentinel
                 * which does not require to test leaf nodes at all
                 * nevertheless test the leaf sentinel in the left and
                 * right rotate procedures.  Since according to the
                 * algorithm node->parent must not be changed by both
                 * the left and right rotates above, it can be cached
                 * in a local variable.  This not only eliminates the
                 * sentinel test in nxt_rbtree_parent_relink() but also
                 * decreases the code size because C forces to reload
                 * non-restrict pointers.
                 */
                sibling = parent->right;
            }

            sibling->color = parent->color;
            parent->color = NXT_RBTREE_BLACK;
            sibling->right->color = NXT_RBTREE_BLACK;

            nxt_rbtree_left_rotate(parent);

            break;

        } else {
            sibling = parent->left;

            if (sibling->color != NXT_RBTREE_BLACK) {

                sibling->color = NXT_RBTREE_BLACK;
                parent->color = NXT_RBTREE_RED;

                nxt_rbtree_right_rotate(parent);

                sibling = parent->left;
            }

            if (sibling->left->color == NXT_RBTREE_BLACK) {

                sibling->color = NXT_RBTREE_RED;

                if (sibling->right->color == NXT_RBTREE_BLACK) {
                    node = parent;
                    continue;
                }

                sibling->right->color = NXT_RBTREE_BLACK;

                nxt_rbtree_left_rotate(sibling);

                /* See the comment in the symmetric branch above. */
                sibling = parent->left;
            }

            sibling->color = parent->color;
            parent->color = NXT_RBTREE_BLACK;
            sibling->left->color = NXT_RBTREE_BLACK;

            nxt_rbtree_right_rotate(parent);

            break;
        }
    }

    node->color = NXT_RBTREE_BLACK;
}


nxt_inline void
nxt_rbtree_left_rotate(nxt_rbtree_node_t *node)
{
    nxt_rbtree_node_t  *child;

    child = node->right;
    node->right = child->left;
    child->left->parent = node;
    child->left = node;

    nxt_rbtree_parent_relink(child, node);

    node->parent = child;
}


nxt_inline void
nxt_rbtree_right_rotate(nxt_rbtree_node_t *node)
{
    nxt_rbtree_node_t  *child;

    child = node->left;
    node->left = child->right;
    child->right->parent = node;
    child->right = node;

    nxt_rbtree_parent_relink(child, node);

    node->parent = child;
}


/* Relink a parent from the node to the subst node. */

nxt_inline void
nxt_rbtree_parent_relink(nxt_rbtree_node_t *subst, nxt_rbtree_node_t *node)
{
    nxt_rbtree_node_t  *parent, **link;

    parent = node->parent;
    /*
     * The leaf sentinel's parent can be safely changed here.
     * See the comment in nxt_rbtree_delete_fixup() for details.
     */
    subst->parent = parent;
    /*
     * If the node's parent is the root sentinel it is safely changed
     * because the root sentinel's left child is the tree root.
     */
    link = (node == parent->left) ? &parent->left : &parent->right;
    *link = subst;
}
