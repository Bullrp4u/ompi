/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2015 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2015      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"

#include "mpi.h"
#include "opal/util/bit_ops.h"
#include "ompi/constants.h"
#include "ompi/communicator/communicator.h"
#include "ompi/mca/coll/base/coll_tags.h"
#include "ompi/mca/coll/base/coll_base_functions.h"
#include "coll_base_topo.h"
#include <math.h>

/*
 * Some static helpers.
 */
static int pown( int fanout, int num )
{
    int j, p = 1;
    if( num < 0 ) return 0;
    if (1==num) return fanout;
    if (2==fanout) {
        return p<<num;
    }
    else {
        for( j = 0; j < num; j++ ) { p*= fanout; }
    }
    return p;
}

static int calculate_level( int fanout, int rank )
{
    int level, num;
    if( rank < 0 ) return -1;
    for( level = 0, num = 0; num <= rank; level++ ) {
        num += pown(fanout, level);
    }
    return level-1;
}

static int calculate_num_nodes_up_to_level( int fanout, int level )
{
    /* just use geometric progression formula for sum:
       a^0+a^1+...a^(n-1) = (a^n-1)/(a-1) */
    return ((pown(fanout,level) - 1)/(fanout - 1));
}

/*
 * And now the building functions.
 *
 * An example for fanout = 2, comm_size = 7
 *
 *              0           <-- delta = 1 (fanout^0)
 *            /   \
 *           1     2        <-- delta = 2 (fanout^1)
 *          / \   / \
 *         3   5 4   6      <-- delta = 4 (fanout^2)
 */

ompi_coll_tree_t*
ompi_coll_base_topo_build_tree( int fanout,
                                 struct ompi_communicator_t* comm,
                                 int root )
{
    int rank, size, schild, sparent, shiftedrank, i;
    int level; /* location of my rank in the tree structure of size */
    int delta; /* number of nodes on my level */
    int slimit; /* total number of nodes on levels above me */
    ompi_coll_tree_t* tree;

    OPAL_OUTPUT((ompi_coll_base_framework.framework_output, "coll:base:topo_build_tree Building fo %d rt %d", fanout, root));

    if (fanout<1) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output, "coll:base:topo_build_tree invalid fanout %d", fanout));
        return NULL;
    }
    if (fanout>MAXTREEFANOUT) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo_build_tree invalid fanout %d bigger than max %d", fanout, MAXTREEFANOUT));
        return NULL;
    }

    /*
     * Get size and rank of the process in this communicator
     */
    size = ompi_comm_size(comm);
    rank = ompi_comm_rank(comm);

    tree = (ompi_coll_tree_t*)malloc(sizeof(ompi_coll_tree_t));
    if (!tree) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo_build_tree PANIC::out of memory"));
        return NULL;
    }

    tree->tree_root     = MPI_UNDEFINED;
    tree->tree_nextsize = MPI_UNDEFINED;

    /*
     * Set root
     */
    tree->tree_root = root;

    /*
     * Initialize tree
     */
    tree->tree_fanout   = fanout;
    tree->tree_bmtree   = 0;
    tree->tree_root     = root;
    tree->tree_prev     = -1;
    tree->tree_nextsize = 0;
    for( i = 0; i < fanout; i++ ) {
        tree->tree_next[i] = -1;
    }

    /* return if we have less than 2 processes */
    if( size < 2 ) {
        return tree;
    }

    /*
     * Shift all ranks by root, so that the algorithm can be
     * designed as if root would be always 0
     * shiftedrank should be used in calculating distances
     * and position in tree
     */
    shiftedrank = rank - root;
    if( shiftedrank < 0 ) {
        shiftedrank += size;
    }

    /* calculate my level */
    level = calculate_level( fanout, shiftedrank );
    delta = pown( fanout, level );

    /* find my children */
    for( i = 0; i < fanout; i++ ) {
        schild = shiftedrank + delta * (i+1);
        if( schild < size ) {
            tree->tree_next[i] = (schild+root)%size;
            tree->tree_nextsize = tree->tree_nextsize + 1;
        } else {
            break;
        }
    }

    /* find my parent */
    slimit = calculate_num_nodes_up_to_level( fanout, level );
    sparent = shiftedrank;
    if( sparent < fanout ) {
        sparent = 0;
    } else {
        while( sparent >= slimit ) {
            sparent -= delta/fanout;
        }
    }
    tree->tree_prev = (sparent+root)%size;

    return tree;
}

/*
 * Constructs in-order binary tree which can be used for non-commutative reduce
 * operations.
 * Root of this tree is always rank (size-1) and fanout is 2.
 * Here are some of the examples of this tree:
 * size == 2     size == 3     size == 4                size == 9
 *      1             2             3                        8
 *     /             / \          /   \                    /   \
 *    0             1  0         2     1                  7     3
 *                                    /                 /  \   / \
 *                                   0                 6    5 2   1
 *                                                         /     /
 *                                                        4     0
 */
ompi_coll_tree_t*
ompi_coll_base_topo_build_in_order_bintree( struct ompi_communicator_t* comm )
{
    int rank, size, myrank, rightsize, delta, parent, lchild, rchild;
    ompi_coll_tree_t* tree;

    /*
     * Get size and rank of the process in this communicator
     */
    size = ompi_comm_size(comm);
    rank = ompi_comm_rank(comm);

    tree = (ompi_coll_tree_t*)malloc(sizeof(ompi_coll_tree_t));
    if (!tree) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output,
                     "coll:base:topo_build_tree PANIC::out of memory"));
        return NULL;
    }

    tree->tree_root     = MPI_UNDEFINED;
    tree->tree_nextsize = MPI_UNDEFINED;

    /*
     * Initialize tree
     */
    tree->tree_fanout   = 2;
    tree->tree_bmtree   = 0;
    tree->tree_root     = size - 1;
    tree->tree_prev     = -1;
    tree->tree_nextsize = 0;
    tree->tree_next[0]  = -1;
    tree->tree_next[1]  = -1;
    OPAL_OUTPUT((ompi_coll_base_framework.framework_output,
                 "coll:base:topo_build_in_order_tree Building fo %d rt %d",
                 tree->tree_fanout, tree->tree_root));

    /*
     * Build the tree
     */
    myrank = rank;
    parent = size - 1;
    delta = 0;

    while ( 1 ) {
        /* Compute the size of the right subtree */
        rightsize = size >> 1;

        /* Determine the left and right child of this parent  */
        lchild = -1;
        rchild = -1;
        if (size - 1 > 0) {
            lchild = parent - 1;
            if (lchild > 0) {
                rchild = rightsize - 1;
            }
        }

        /* The following cases are possible: myrank can be
           - a parent,
           - belong to the left subtree, or
           - belong to the right subtee
           Each of the cases need to be handled differently.
        */

        if (myrank == parent) {
            /* I am the parent:
               - compute real ranks of my children, and exit the loop. */
            if (lchild >= 0) tree->tree_next[0] = lchild + delta;
            if (rchild >= 0) tree->tree_next[1] = rchild + delta;
            break;
        }
        if (myrank > rchild) {
            /* I belong to the left subtree:
               - If I am the left child, compute real rank of my parent
               - Iterate down through tree:
               compute new size, shift ranks down, and update delta.
            */
            if (myrank == lchild) {
                tree->tree_prev = parent + delta;
            }
            size = size - rightsize - 1;
            delta = delta + rightsize;
            myrank = myrank - rightsize;
            parent = size - 1;

        } else {
            /* I belong to the right subtree:
               - If I am the right child, compute real rank of my parent
               - Iterate down through tree:
               compute new size and parent,
               but the delta and rank do not need to change.
            */
            if (myrank == rchild) {
                tree->tree_prev = parent + delta;
            }
            size = rightsize;
            parent = rchild;
        }
    }

    if (tree->tree_next[0] >= 0) { tree->tree_nextsize = 1; }
    if (tree->tree_next[1] >= 0) { tree->tree_nextsize += 1; }

    return tree;
}

int ompi_coll_base_topo_destroy_tree( ompi_coll_tree_t** tree )
{
    ompi_coll_tree_t *ptr;

    if ((!tree)||(!*tree)) {
        return OMPI_SUCCESS;
    }

    ptr = *tree;

    free (ptr);
    *tree = NULL;   /* mark tree as gone */

    return OMPI_SUCCESS;
}

int ompi_coll_base_topo_destroy_two_trees( ompi_coll_tree_t** trees )
{
    free(trees[0]);
    free(trees[1]);
    free(trees);
    *trees = NULL;   /* mark tree as gone */
    
    return OMPI_SUCCESS;
}

/*
 *
 * Here are some of the examples of this tree:
 * size == 2                   size = 4                 size = 8
 *      0                           0                        0
 *     /                            | \                    / | \
 *    1                             2  1                  4  2  1
 *                                     |                     |  |\
 *                                     3                     6  5 3
 *                                                                |
 *                                                                7
 */
ompi_coll_tree_t*
ompi_coll_base_topo_build_bmtree( struct ompi_communicator_t* comm,
                                   int root )
{
    int childs = 0, rank, size, mask = 1, index, remote, i;
    ompi_coll_tree_t *bmtree;

    OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo:build_bmtree rt %d", root));

    /*
     * Get size and rank of the process in this communicator
     */
    size = ompi_comm_size(comm);
    rank = ompi_comm_rank(comm);

    index = rank -root;

    bmtree = (ompi_coll_tree_t*)malloc(sizeof(ompi_coll_tree_t));
    if (!bmtree) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo:build_bmtree PANIC out of memory"));
        return NULL;
    }

    bmtree->tree_bmtree   = 1;

    bmtree->tree_root     = MPI_UNDEFINED;
    bmtree->tree_nextsize = MPI_UNDEFINED;
    for( i = 0;i < MAXTREEFANOUT; i++ ) {
        bmtree->tree_next[i] = -1;
    }

    if( index < 0 ) index += size;

    mask = opal_next_poweroftwo(index);

    /* Now I can compute my father rank */
    if( root == rank ) {
        bmtree->tree_prev = root;
    } else {
        remote = (index ^ (mask >> 1)) + root;
        if( remote >= size ) remote -= size;
        bmtree->tree_prev = remote;
    }
    /* And now let's fill my childs */
    while( mask < size ) {
        remote = (index ^ mask);
        if( remote >= size ) break;
        remote += root;
        if( remote >= size ) remote -= size;
        if (childs==MAXTREEFANOUT) {
            OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo:build_bmtree max fanout incorrect %d needed %d", MAXTREEFANOUT, childs));
            free(bmtree);
            return NULL;
        }
        bmtree->tree_next[childs] = remote;
        mask <<= 1;
        childs++;
    }
    bmtree->tree_nextsize = childs;
    bmtree->tree_root     = root;
    return bmtree;
}

/*
 * Constructs in-order binomial tree which can be used for gather/scatter
 * operations.
 *
 * Here are some of the examples of this tree:
 * size == 2                   size = 4                 size = 8
 *      0                           0                        0
 *     /                          / |                      / | \
 *    1                          1  2                     1  2  4
 *                                  |                        |  | \
 *                                  3                        3  5  6
 *                                                                 |
 *                                                                 7
 */
ompi_coll_tree_t*
ompi_coll_base_topo_build_in_order_bmtree( struct ompi_communicator_t* comm,
                                            int root )
{
    int childs = 0, rank, vrank, size, mask = 1, remote, i;
    ompi_coll_tree_t *bmtree;

    OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo:build_in_order_bmtree rt %d", root));

    /*
     * Get size and rank of the process in this communicator
     */
    size = ompi_comm_size(comm);
    rank = ompi_comm_rank(comm);

    vrank = (rank - root + size) % size;

    bmtree = (ompi_coll_tree_t*)malloc(sizeof(ompi_coll_tree_t));
    if (!bmtree) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo:build_bmtree PANIC out of memory"));
        return NULL;
    }

    bmtree->tree_bmtree   = 1;
    bmtree->tree_root     = MPI_UNDEFINED;
    bmtree->tree_nextsize = MPI_UNDEFINED;
    for(i=0;i<MAXTREEFANOUT;i++) {
        bmtree->tree_next[i] = -1;
    }

    if (root == rank) {
        bmtree->tree_prev = root;
    }

    while (mask < size) {
        remote = vrank ^ mask;
        if (remote < vrank) {
            bmtree->tree_prev = (remote + root) % size;
            break;
        } else if (remote < size) {
            bmtree->tree_next[childs] = (remote + root) % size;
            childs++;
            if (childs==MAXTREEFANOUT) {
                OPAL_OUTPUT((ompi_coll_base_framework.framework_output,
                             "coll:base:topo:build_bmtree max fanout incorrect %d needed %d",
                             MAXTREEFANOUT, childs));
                free(bmtree);
                return NULL;
            }
        }
        mask <<= 1;
    }
    bmtree->tree_nextsize = childs;
    bmtree->tree_root     = root;

    return bmtree;
}


ompi_coll_tree_t*
ompi_coll_base_topo_build_chain( int fanout,
                                  struct ompi_communicator_t* comm,
                                  int root )
{
    int i, maxchainlen, mark, head, len, rank, size, srank /* shifted rank */;
    ompi_coll_tree_t *chain;

    OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo:build_chain fo %d rt %d", fanout, root));

    /*
     * Get size and rank of the process in this communicator
     */
    size = ompi_comm_size(comm);
    rank = ompi_comm_rank(comm);

    if( fanout < 1 ) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo:build_chain WARNING invalid fanout of ZERO, forcing to 1 (pipeline)!"));
        fanout = 1;
    }
    if (fanout>MAXTREEFANOUT) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo:build_chain WARNING invalid fanout %d bigger than max %d, forcing to max!", fanout, MAXTREEFANOUT));
        fanout = MAXTREEFANOUT;
    }

    /*
     * Allocate space for topology arrays if needed
     */
    chain = (ompi_coll_tree_t*)malloc( sizeof(ompi_coll_tree_t) );
    if (!chain) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"coll:base:topo:build_chain PANIC out of memory"));
        fflush(stdout);
        return NULL;
    }
    chain->tree_root     = MPI_UNDEFINED;
    chain->tree_nextsize = -1;
    for(i=0;i<fanout;i++) chain->tree_next[i] = -1;

    /*
     * Set root & numchain
     */
    chain->tree_root = root;
    if( (size - 1) < fanout ) {
        chain->tree_nextsize = size-1;
        fanout = size-1;
    } else {
        chain->tree_nextsize = fanout;
    }

    /*
     * Shift ranks
     */
    srank = rank - root;
    if (srank < 0) srank += size;

    /*
     * Special case - fanout == 1
     */
    if( fanout == 1 ) {
        if( srank == 0 ) chain->tree_prev = -1;
        else chain->tree_prev = (srank-1+root)%size;

        if( (srank + 1) >= size) {
            chain->tree_next[0] = -1;
            chain->tree_nextsize = 0;
        } else {
            chain->tree_next[0] = (srank+1+root)%size;
            chain->tree_nextsize = 1;
        }
        return chain;
    }

    /* Let's handle the case where there is just one node in the communicator */
    if( size == 1 ) {
        chain->tree_next[0] = -1;
        chain->tree_nextsize = 0;
        chain->tree_prev = -1;
        return chain;
    }
    /*
     * Calculate maximum chain length
     */
    maxchainlen = (size-1) / fanout;
    if( (size-1) % fanout != 0 ) {
        maxchainlen++;
        mark = (size-1)%fanout;
    } else {
        mark = fanout+1;
    }

    /*
     * Find your own place in the list of shifted ranks
     */
    if( srank != 0 ) {
        int column;
        if( srank-1 < (mark * maxchainlen) ) {
            column = (srank-1)/maxchainlen;
            head = 1+column*maxchainlen;
            len = maxchainlen;
        } else {
            column = mark + (srank-1-mark*maxchainlen)/(maxchainlen-1);
            head = mark*maxchainlen+1+(column-mark)*(maxchainlen-1);
            len = maxchainlen-1;
        }

        if( srank == head ) {
            chain->tree_prev = 0; /*root*/
        } else {
            chain->tree_prev = srank-1; /* rank -1 */
        }
        if( srank == (head + len - 1) ) {
            chain->tree_next[0] = -1;
            chain->tree_nextsize = 0;
        } else {
            if( (srank + 1) < size ) {
                chain->tree_next[0] = srank+1;
                chain->tree_nextsize = 1;
            } else {
                chain->tree_next[0] = -1;
                chain->tree_nextsize = 0;
            }
        }
        chain->tree_prev = (chain->tree_prev+root)%size;
        if( chain->tree_next[0] != -1 ) {
            chain->tree_next[0] = (chain->tree_next[0]+root)%size;
        }
    } else {
        /*
         * Unshift values
         */
        chain->tree_prev = -1;
        chain->tree_next[0] = (root+1)%size;
        for( i = 1; i < fanout; i++ ) {
            chain->tree_next[i] = chain->tree_next[i-1] + maxchainlen;
            if( i > mark ) {
                chain->tree_next[i]--;
            }
            chain->tree_next[i] %= size;
        }
        chain->tree_nextsize = fanout;
    }

    return chain;
}

//segment number must >=2 and the number of nodes must >=3
ompi_coll_tree_t**
ompi_coll_base_topo_build_two_trees_binary(struct ompi_communicator_t* comm,
                                           int root ){
    int i, j, rank, size, vrank;
    ompi_coll_tree_t** two_trees;
    //build the two tree array
    two_trees = (ompi_coll_tree_t **)malloc(2 * sizeof(ompi_coll_tree_t *));
    two_trees[0] = (ompi_coll_tree_t*)malloc(sizeof(ompi_coll_tree_t));
    two_trees[1] = (ompi_coll_tree_t*)malloc(sizeof(ompi_coll_tree_t));
    if (!two_trees || !two_trees[0] || !two_trees[1]) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output,
                     "coll:base:topo:build_two_tree PANIC out of memory"));
        return NULL;
    }
    
    size = ompi_comm_size(comm);
    rank = ompi_comm_rank(comm);
    vrank = (rank-root+size-1)%size;
    //printf("rank = %d, vrank = %d\n", rank, vrank);
    //int recovered_rank = (vrank+1+root)%size;      //only for test
    //printf("rank = %d, recovered_rank = %d\n", rank, recovered_rank);   //for test
    int p = size-1;       //number of node in the two tree, exclude the root
    int h = ceil(log(p+1)/log(2));
    //printf("p = %d, h = %d\n", p, h);
    
    //init the two trees
    two_trees[0]->tree_bmtree   = 0;
    two_trees[0]->tree_root     = root;
    two_trees[0]->tree_nextsize = 0;
    for( i = 0;i < MAXTREEFANOUT; i++ ) {
        two_trees[0]->tree_next[i] = -1;
    }
    
    two_trees[1]->tree_bmtree   = 0;
    two_trees[1]->tree_root     = root;
    two_trees[1]->tree_nextsize = 0;
    for( i = 0;i < MAXTREEFANOUT; i++ ) {
        two_trees[1]->tree_next[i] = -1;
    }
    if (p <= 1) {
        return two_trees;
    }
    
    //build tree 0
    if (vrank != p){
        for (j=0; j<h; j++) {
            //to decide which level the node will be in
            if (vrank % ((int)pow(2,j+1)) == (int)pow(2,j)-1){
                if (j != 0) {
                    //find children for all the non leaf nodes
                    //find left child, left child always in the boundary
                    int child_vrank = vrank-(int)pow(2,j-1);
                    two_trees[0]->tree_next[0] = (child_vrank+1+root)%size;
                    two_trees[0]->tree_nextsize += 1;
                    
                    //find right child, right child is not always in the boundary
                    child_vrank = vrank+(int)pow(2,j-1);
                    if (child_vrank < p && child_vrank >= 0) {
                        two_trees[0]->tree_next[1] = (child_vrank+1+root)%size;
                        two_trees[0]->tree_nextsize += 1;
                    }
                    //if the right children is not in the boundary
                    if (two_trees[0]->tree_nextsize < 2) {
                        //it is not the last node
                        if (vrank != p-1) {
                            int right_p = p-vrank-1;
                            int right_h = ceil(log(right_p+1)/log(2));
                            two_trees[0]->tree_next[1] = (vrank+(int)pow(2,right_h-1)+1+root)%size;
                            two_trees[0]->tree_nextsize += 1;
                        }
                    }
                }
                if (j != h-1) {
                    //find parent for all the non root nodes
                    int parent_vrank = (vrank-(int)pow(2,j)+size)%size;
                    if (parent_vrank % ((int)pow(2,j+2)) != (int)pow(2,j+1)-1) {
                        parent_vrank = (vrank+(int)pow(2,j)+size)%size;
                    }
                    two_trees[0]->tree_prev = (parent_vrank+1+root)%size;
                    //printf("rank = %d, parent = %d\n", rank, two_trees[0]->tree_prev);   //for test
                }
                else{
                    two_trees[0]->tree_prev = root;
                }
            }
        }
    }
    else{
        two_trees[0]->tree_next[0] = ((int)pow(2,h-1)+root)%size;
        two_trees[0]->tree_nextsize += 1;
        two_trees[0]->tree_prev = -1;
        
    }
    
    //build tree 1
    if (vrank != p){
        vrank = (vrank+1)%(size-1);
        //printf("rank = %d, vrank = %d\n", rank, vrank);
        //printf("rank = %d, recovered_rank = %d\n", rank, ((vrank+size-2)%(size-1)+1+root)%size);
        for (j=0; j<h; j++) {
            //to decide which level the node will be in
            if (vrank % ((int)pow(2,j+1)) == (int)pow(2,j)-1){
                if (j != 0) {
                    //find children for all the non leaf nodes
                    //find left child, left child always in the boundary
                    int child_vrank = vrank-(int)pow(2,j-1);
                    two_trees[1]->tree_next[0] = ((child_vrank+size-2)%(size-1)+1+root)%size;
                    two_trees[1]->tree_nextsize += 1;
                    
                    //find right child, right child is not always in the boundary
                    child_vrank = vrank+(int)pow(2,j-1);
                    if (child_vrank < p && child_vrank >= 0) {
                        two_trees[1]->tree_next[1] = ((child_vrank+size-2)%(size-1)+1+root)%size;
                        two_trees[1]->tree_nextsize += 1;
                    }
                    //if the right children is not in the boundary
                    if (two_trees[1]->tree_nextsize < 2) {
                        //it is not the last node
                        if (vrank != p-1) {
                            int right_p = p-vrank-1;
                            int right_h = ceil(log(right_p+1)/log(2));
                            two_trees[1]->tree_next[1] = ((vrank+(int)pow(2,right_h-1)+size-2)%(size-1)+1+root)%size;
                            two_trees[1]->tree_nextsize += 1;
                        }
                    }
                }
                if (j != h-1) {
                    //find parent for all the non root nodes
                    int parent_vrank = (vrank-(int)pow(2,j)+size)%size;
                    if (parent_vrank % ((int)pow(2,j+2)) != (int)pow(2,j+1)-1) {
                        parent_vrank = (vrank+(int)pow(2,j)+size)%size;
                    }
                    two_trees[1]->tree_prev = ((parent_vrank+size-2)%(size-1)+1+root)%size;
                    //printf("rank = %d, parent = %d\n", rank, two_trees[1]->tree_prev);   //for test
                }
                else{
                    two_trees[1]->tree_prev = root;
                }
            }
        }
    }
    else{
        two_trees[1]->tree_next[0] = (((int)pow(2,h-1)-1+size-2)%(size-1)+1+root)%size;
        two_trees[1]->tree_nextsize += 1;
        two_trees[1]->tree_prev = -1;
    }
    return two_trees;
}

static void add_edge(int** tree, int length, int parent, int child){
    int i;
    for (i=0; i<length; i++) {
        if (tree[parent][i] == -1) {
            break;
        }
    }
    tree[parent][i] = child;
}

static void divide_group(int** tree, int length, int start, int end, int tree_id){
    //2 elements in a group
    if (end - start == 1) {
        if (tree_id == 0){
            add_edge(tree, length, start, end);
        }
        else {
            add_edge(tree, length, end, start);
        }
    }
    //3 elements in a group
    else if(end - start == 2){
        if (tree_id == 0) {
            add_edge(tree, length, start, start+1);
            add_edge(tree, length, start+1, end);
        }
        else {
            add_edge(tree, length, end, start);
            add_edge(tree, length, start, start+1);
        }
    }
    else if(end - start > 2){
        int mid = (end-start)/2+start;
        if (tree_id == 0) {
            add_edge(tree, length, start, mid+1);
        }
        else {
            add_edge(tree, length, end, mid);
        }
        divide_group(tree, length, start, mid, tree_id);
        divide_group(tree, length, mid+1, end, tree_id);
    }
}

//segment number must >=2 and the number of nodes must >=3
ompi_coll_tree_t**
ompi_coll_base_topo_build_two_trees_binomial(struct ompi_communicator_t* comm,
                                             int root ){
    int i, j, rank, size, vrank;
    ompi_coll_tree_t** two_trees;
    //build the two tree array
    two_trees = (ompi_coll_tree_t **)malloc(2 * sizeof(ompi_coll_tree_t *));
    two_trees[0] = (ompi_coll_tree_t*)malloc(sizeof(ompi_coll_tree_t));
    two_trees[1] = (ompi_coll_tree_t*)malloc(sizeof(ompi_coll_tree_t));
    if (!two_trees || !two_trees[0] || !two_trees[1]) {
        OPAL_OUTPUT((ompi_coll_base_framework.framework_output,
                     "coll:base:topo:build_two_tree PANIC out of memory"));
        return NULL;
    }
    
    size = ompi_comm_size(comm);
    rank = ompi_comm_rank(comm);
    vrank = (rank-root+size-1)%size;    //root has the highest vrank
    //printf("rank = %d, vrank = %d\n", rank, vrank);
    //int recovered_rank = (vrank+1+root)%size;      //only for test
    //printf("rank = %d, recovered_rank = %d\n", rank, recovered_rank);   //for test
    int p = size-1;       //number of node in the two tree, exclude the root
    int max_num_child = ceil(log(p)/log(2));
    
    //calculate the topology
    //build a 2D array for tree0 and init with -1
    int** tree0 = (int **)malloc(p * sizeof(int *));
    for (i=0; i<p; i++) {
        tree0[i] = (int *)malloc(max_num_child * sizeof(int));
        for (j=0; j<max_num_child; j++) {
            tree0[i][j] = -1;
        }
    }
    divide_group(tree0, max_num_child, 0, p-1, 0);
    
    int** tree1 = (int **)malloc(p * sizeof(int *));
    for (i=0; i<p; i++) {
        tree1[i] = (int *)malloc(max_num_child * sizeof(int));
        for (j=0; j<max_num_child; j++) {
            tree1[i][j] = -1;
        }
    }
    divide_group(tree1, max_num_child, 0, p-1, 1);
    
    //init the two trees
    two_trees[0]->tree_bmtree   = 1;
    two_trees[0]->tree_root     = root;
    two_trees[0]->tree_nextsize = 0;
    for( i = 0;i < MAXTREEFANOUT; i++ ) {
        two_trees[0]->tree_next[i] = -1;
    }
    
    two_trees[1]->tree_bmtree   = 1;
    two_trees[1]->tree_root     = root;
    two_trees[1]->tree_nextsize = 0;
    for( i = 0;i < MAXTREEFANOUT; i++ ) {
        two_trees[1]->tree_next[i] = -1;
    }
    if (p <= 1) {
        return two_trees;
    }
    
    //build tree 0
    if (vrank != p) {
        //find children for all the nodes except root
        int num_child = 0;
        for (i=0; i<max_num_child; i++) {
            int child_vrank = tree0[vrank][i];
            if (child_vrank != -1) {
                two_trees[0]->tree_next[num_child] = (child_vrank+1+root)%size;
                two_trees[0]->tree_nextsize += 1;
                num_child++;
            }
        }
        //find parent for all the nodes
        if (vrank == 0) {
            two_trees[0]->tree_prev = root;
        }
        else{
            for (i=0; i<p; i++) {
                for (j=0; j<max_num_child; j++) {
                    if (vrank == tree0[i][j]) {
                        two_trees[0]->tree_prev = (i+1+root)%size;
                    }
                }
            }
        }
    }
    else{
        two_trees[0]->tree_next[0] = (0+1+root)%size;
        two_trees[0]->tree_nextsize += 1;
        two_trees[0]->tree_prev = -1;
    }
    
    //build tree 1
    if (vrank != p) {
        //find children for all the nodes except root
        int num_child = 0;
        for (i=0; i<max_num_child; i++) {
            int child_vrank = tree1[vrank][i];
            if (child_vrank != -1) {
                two_trees[1]->tree_next[num_child] = (child_vrank+1+root)%size;
                two_trees[1]->tree_nextsize += 1;
                num_child++;
            }
        }
        //find parent for all the nodes
        if (vrank == p-1) {
            two_trees[1]->tree_prev = root;
        }
        else{
            for (i=0; i<p; i++) {
                for (j=0; j<max_num_child; j++) {
                    if (vrank == tree1[i][j]) {
                        two_trees[1]->tree_prev = (i+1+root)%size;
                    }
                }
            }
        }
    }
    else{
        two_trees[1]->tree_next[0] = (p-1+1+root)%size;
        two_trees[1]->tree_nextsize += 1;
        two_trees[1]->tree_prev = -1;
    }
    
    return two_trees;
}

int ompi_coll_base_topo_dump_tree (ompi_coll_tree_t* tree, int rank)
{
    int i;

    OPAL_OUTPUT((ompi_coll_base_framework.framework_output, "coll:base:topo:topo_dump_tree %1d tree root %d"
                 " fanout %d BM %1d nextsize %d prev %d",
                 rank, tree->tree_root, tree->tree_bmtree, tree->tree_fanout,
                 tree->tree_nextsize, tree->tree_prev));
    if( tree->tree_nextsize ) {
        for( i = 0; i < tree->tree_nextsize; i++ )
            OPAL_OUTPUT((ompi_coll_base_framework.framework_output,"[%1d] %d", i, tree->tree_next[i]));
    }
    return (0);
}

