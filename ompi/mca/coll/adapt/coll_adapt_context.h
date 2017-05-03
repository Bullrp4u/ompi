#include "ompi/mca/coll/coll.h"
#include "opal/class/opal_free_list.h"      //free list
#include "opal/class/opal_list.h"       //list
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/communicator/communicator.h"
#include "ompi/op/op.h"
#include "ompi/mca/coll/base/coll_base_topo.h"  //ompi_coll_tree_t
#include "coll_adapt_inbuf.h"

/* bcast constant context in bcast context */
struct mca_coll_adapt_constant_bcast_context_s {
    opal_object_t  super;
    int root;
    size_t count;
    size_t seg_count;
    ompi_datatype_t * datatype;
    ompi_communicator_t * comm;
    int real_seg_size;
    int num_segs;
    ompi_request_t * request;
    opal_mutex_t * mutex;
    int* recv_array;
    int* send_array;
    int num_recv_segs; //store the length of the fragment array, how many fragments are recevied
    int num_recv_fini;  //store how many segs is finish recving
    int num_sent_segs;  //number of sent segments
    ompi_coll_tree_t * tree;
    int ibcast_tag;
};

typedef struct mca_coll_adapt_constant_bcast_context_s mca_coll_adapt_constant_bcast_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_constant_bcast_context_t);


//bcast context
struct mca_coll_adapt_bcast_context_s {
    opal_free_list_item_t super;
    char *buff;
    int frag_id;
    int child_id;
    int peer;
    mca_coll_adapt_constant_bcast_context_t * con;
};

typedef struct mca_coll_adapt_bcast_context_s mca_coll_adapt_bcast_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_bcast_context_t);

/* reduce constant context in reduce context */
struct mca_coll_adapt_constant_reduce_context_s {
    opal_object_t  super;
    size_t count;
    size_t seg_count;
    ompi_datatype_t * datatype;
    ompi_communicator_t * comm;
    int segment_increment;      //increment of each segment
    int num_segs;
    ompi_request_t * request;
    int rank;      //change, unused
    int32_t num_recv_segs; //store the length of the fragment array, how many fragments are recevied
    int32_t num_sent_segs;  //number of sent segments
    int32_t* next_recv_segs;  //next seg need to be received for every children
    opal_mutex_t * mutex;     //old, only for test
    opal_mutex_t * mutex_recv_list;     //use to lock recv list
    opal_mutex_t * mutex_num_recv_segs;     //use to lock num_recv_segs
    opal_mutex_t * mutex_num_sent;     //use to lock num_sent
    opal_mutex_t ** mutex_op_list;   //use to lock each segment when do the reduce op
    ompi_op_t * op;  //reduce operation
    ompi_coll_tree_t * tree;
    char ** accumbuf;   //accumulate buff, used in reduce
    opal_free_list_t *inbuf_list;
    opal_list_t *recv_list;    //a list to store the segments which are received and not yet be sent
    ptrdiff_t lower_bound;
    int32_t ongoing_send;   //how many send is posted but not finished
    char * sbuf;    //inputed sbuf
    char * rbuf;    //inputed rbuf
    int root;
    int distance;   //address of inbuf->buff to address of inbuf
    int ireduce_tag;
};

typedef struct mca_coll_adapt_constant_reduce_context_s mca_coll_adapt_constant_reduce_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_constant_reduce_context_t);


//reduce context
struct mca_coll_adapt_reduce_context_s {
    opal_free_list_item_t super;
    char *buff;
    int frag_id;
    int child_id;
    int peer;
    mca_coll_adapt_constant_reduce_context_t * con;
    mca_coll_adapt_inbuf_t *inbuf;  //only used in reduce, store the incoming segment
};

typedef struct mca_coll_adapt_reduce_context_s mca_coll_adapt_reduce_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_reduce_context_t);

/* bcast constant context in bcast context for two trees */
struct mca_coll_adapt_constant_bcast_two_trees_context_s {
    opal_object_t  super;
    size_t count;
    size_t seg_count;
    ompi_datatype_t * datatype;
    ompi_communicator_t * comm;
    int real_seg_size;
    int* num_segs;
    ompi_request_t * request;
    int** recv_arrays;
    int** send_arrays;
    int *num_recv_segs; //store the length of the fragment array, how many fragments are recevied
    int *num_sent_segs;  //number of sent segments
    opal_mutex_t * mutex;
    ompi_coll_tree_t ** trees;
    int complete;
    int ibcast_tag;
};

typedef struct mca_coll_adapt_constant_bcast_two_trees_context_s mca_coll_adapt_constant_bcast_two_trees_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_constant_bcast_two_trees_context_t);


//bcast context for two trees
struct mca_coll_adapt_bcast_two_trees_context_s {
    opal_free_list_item_t super;
    char *buff;
    int frag_id;
    int child_id;
    int peer;
    int tree; //which tree are using
    mca_coll_adapt_constant_bcast_two_trees_context_t * con;
};

typedef struct mca_coll_adapt_bcast_two_trees_context_s mca_coll_adapt_bcast_two_trees_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_bcast_two_trees_context_t);

/* allreduce constant context in allreduce context */
struct mca_coll_adapt_constant_allreduce_context_s {
    opal_object_t  super;
    char *sendbuf;
    char *recvbuf;
    size_t count;
    ompi_datatype_t * datatype;
    ompi_communicator_t * comm;
    ompi_request_t * request;
    opal_free_list_t * context_list;
    ompi_op_t * op;  //reduce operation
    ptrdiff_t lower_bound;
    int extra_ranks;
    opal_free_list_t *inbuf_list;
    int complete;
    int adjsize;
    int sendbuf_ready;
    int inbuf_ready;
    int total_send;
    int total_recv;
    opal_mutex_t * mutex_buf;
    opal_mutex_t * mutex_total_send;
    opal_mutex_t * mutex_total_recv;
};

typedef struct mca_coll_adapt_constant_allreduce_context_s mca_coll_adapt_constant_allreduce_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_constant_allreduce_context_t);


//allreduce context
struct mca_coll_adapt_allreduce_context_s {
    opal_free_list_item_t super;
    mca_coll_adapt_inbuf_t *inbuf;  //store the incoming segment
    int newrank;
    int distance;      //distance for recursive doubleing
    int peer;
    mca_coll_adapt_constant_allreduce_context_t * con;
};

typedef struct mca_coll_adapt_allreduce_context_s mca_coll_adapt_allreduce_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_allreduce_context_t);

/* allreduce constant context in allreduce context */
struct mca_coll_adapt_constant_allreduce_ring_context_s {
    opal_object_t  super;
    char *rbuf;
    char *sbuf;
    ompi_datatype_t * dtype;
    ompi_communicator_t * comm;
    int count;
    opal_mutex_t * mutex_complete;
    int complete;
    int split_block;
    opal_free_list_t *inbuf_list;
    opal_free_list_t * context_list;
    int num_phases;
    int early_blockcount;
    int late_blockcount;
    ptrdiff_t lower_bound;
    ptrdiff_t extent;
    ompi_op_t * op;  //reduce operation
    ompi_request_t * request;
    ompi_coll_tree_t * tree;
};

typedef struct mca_coll_adapt_constant_allreduce_ring_context_s mca_coll_adapt_constant_allreduce_ring_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_constant_allreduce_ring_context_t);


//allreduce context
struct mca_coll_adapt_allreduce_ring_context_s {
    opal_free_list_item_t super;
    char *buff;
    int peer;
    int block;
    int phase;
    ptrdiff_t phase_offset;
    ptrdiff_t block_offset;
    int phase_count;
    mca_coll_adapt_inbuf_t *inbuf;  //store the incoming segment
    mca_coll_adapt_constant_allreduce_ring_context_t * con;
};

typedef struct mca_coll_adapt_allreduce_ring_context_s mca_coll_adapt_allreduce_ring_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_allreduce_ring_context_t);

/* allreduce constant context in allreduce generic context */
struct mca_coll_adapt_constant_allreduce_generic_context_s {
    opal_object_t  super;
    ompi_datatype_t * dtype;
    ompi_op_t * op;  //reduce operation
    ompi_communicator_t * comm;
    mca_coll_base_module_t *module;
    ompi_request_t * request;
    int rank;
    int num_blocks;
    opal_mutex_t * mutex_num_finished;
    int num_finished;
    opal_free_list_t * context_list;
};

typedef struct mca_coll_adapt_constant_allreduce_generic_context_s mca_coll_adapt_constant_allreduce_generic_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_constant_allreduce_generic_context_t);


//allreduce context
struct mca_coll_adapt_allreduce_generic_context_s {
    opal_free_list_item_t super;
    char *sbuf;
    char *rbuf;
    int count;
    int root;
    int tag;
    mca_coll_adapt_constant_allreduce_generic_context_t * con;
};

typedef struct mca_coll_adapt_allreduce_generic_context_s mca_coll_adapt_allreduce_generic_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_allreduce_generic_context_t);


/* alltoallv constant context in alltoallv context */
struct mca_coll_adapt_constant_alltoallv_context_s {
    opal_object_t  super;
    char *sbuf;
    const int* scounts;
    const int* sdisps;
    ompi_datatype_t * sdtype;
    char *rbuf;
    const int* rcounts;
    const int* rdisps;
    ompi_datatype_t * rdtype;
    ompi_communicator_t * comm;
    ompi_request_t * request;
    opal_free_list_t * context_list;
    ptrdiff_t sext;
    ptrdiff_t rext;
    const char *origin_sbuf;
    int finished_send;
    int finished_recv;
    int ongoing_send;
    int ongoing_recv;
    int complete;
    int next_send_distance;
    int next_recv_distance;
};

typedef struct mca_coll_adapt_constant_alltoallv_context_s mca_coll_adapt_constant_alltoallv_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_constant_alltoallv_context_t);


//alltoallv context
struct mca_coll_adapt_alltoallv_context_s {
    opal_free_list_item_t super;
    int distance;      //distance for recursive doubleing
    int peer;
    char *start;         //point to send or recv from which address
    mca_coll_adapt_constant_alltoallv_context_t * con;
};

typedef struct mca_coll_adapt_alltoallv_context_s mca_coll_adapt_alltoallv_context_t;

OBJ_CLASS_DECLARATION(mca_coll_adapt_alltoallv_context_t);

