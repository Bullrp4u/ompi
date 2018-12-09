#include "coll_future.h"
#include "ompi/mca/coll/base/coll_base_functions.h"
#include "ompi/mca/coll/base/coll_tags.h"
#include "ompi/mca/pml/pml.h"
#include "coll_future_trigger.h"

void mac_coll_future_set_allgather_argu(mca_allgather_argu_t *argu,
                                        mca_coll_task_t *cur_task,
                                        void *sbuf,
                                        void *sbuf_inter_free,
                                        int scount,
                                        struct ompi_datatype_t *sdtype,
                                        void *rbuf,
                                        int rcount,
                                        struct ompi_datatype_t *rdtype,
                                        int root_low_rank,
                                        struct ompi_communicator_t *up_comm,
                                        struct ompi_communicator_t *low_comm,
                                        int w_rank,
                                        bool noop,
                                        bool is_mapbycore,
                                        int *topo,
                                        ompi_request_t *req){
    argu->cur_task = cur_task;
    argu->sbuf = sbuf;
    argu->sbuf_inter_free = sbuf_inter_free;
    argu->scount = scount;
    argu->sdtype = sdtype;
    argu->rbuf = rbuf;
    argu->rcount = rcount;
    argu->rdtype = rdtype;
    argu->root_low_rank = root_low_rank;
    argu->up_comm = up_comm;
    argu->low_comm = low_comm;
    argu->w_rank = w_rank;
    argu->noop = noop;
    argu->is_mapbycore = is_mapbycore;
    argu->topo = topo;
    argu->req = req;
}

int
mca_coll_future_allgather_intra(const void *sbuf, int scount,
                                struct ompi_datatype_t *sdtype,
                                void* rbuf, int rcount,
                                struct ompi_datatype_t *rdtype,
                                struct ompi_communicator_t *comm,
                                mca_coll_base_module_t *module){
    int w_rank;
    w_rank = ompi_comm_rank(comm);
    
    /* create the subcommunicators */
    mca_coll_future_module_t *future_module = (mca_coll_future_module_t *)module;
    mca_coll_future_comm_create(comm, future_module);
    ompi_communicator_t *low_comm = future_module->cached_low_comm;
    ompi_communicator_t *up_comm = future_module->cached_up_comm;
    int low_rank = ompi_comm_rank(low_comm);

    ompi_request_t *temp_request = NULL;
    //set up request
    temp_request = OBJ_NEW(ompi_request_t);
    OMPI_REQUEST_INIT(temp_request, false);
    temp_request->req_state = OMPI_REQUEST_ACTIVE;
    temp_request->req_type = 0;
    temp_request->req_free = future_request_free;
    temp_request->req_status.MPI_SOURCE = 0;
    temp_request->req_status.MPI_TAG = 0;
    temp_request->req_status.MPI_ERROR = 0;
    temp_request->req_status._cancelled = 0;
    temp_request->req_status._ucount = 0;

    /* init topo */
    int *topo = mca_coll_future_topo_init(comm, future_module, 2);

    int root_low_rank = 0;
    /* create lg (low-level gather) task for the first union segment */
    mca_coll_task_t *lg = OBJ_NEW(mca_coll_task_t);
    /* setup lg task arguments */
    mca_allgather_argu_t *lg_argu = malloc(sizeof(mca_allgather_argu_t));
    mac_coll_future_set_allgather_argu(lg_argu, lg, (char *)sbuf, NULL, scount, sdtype, rbuf, rcount, rdtype, root_low_rank, up_comm, low_comm, w_rank, low_rank!=root_low_rank, future_module->is_mapbycore, topo, temp_request);
    /* init lg task */
    init_task(lg, mca_coll_future_allgather_lg_task, (void *)(lg_argu));
    /* issure lg task */
    issue_task(lg);
    
    ompi_request_wait(&temp_request, MPI_STATUS_IGNORE);

    return OMPI_SUCCESS;
}

int mca_coll_future_allgather_lg_task(void *task_argu)
{
    mca_allgather_argu_t *t = (mca_allgather_argu_t *)task_argu;
    OPAL_OUTPUT_VERBOSE((30, mca_coll_future_component.future_output, "[%d] Future Allgather:  lg\n", t->w_rank));
    OBJ_RELEASE(t->cur_task);
    
    /* if the process is one of the node leader */
    char *tmp_buf = NULL;
    char *tmp_rbuf = NULL;
    if (!t->noop) {
        int low_size = ompi_comm_size(t->low_comm);
        ptrdiff_t rsize, rgap = 0;
        rsize = opal_datatype_span(&t->rdtype->super, (int64_t)t->rcount * low_size, &rgap);
        tmp_buf = (char *) malloc(rsize);
        tmp_rbuf = tmp_buf - rgap;
    }
    /* shared memory node gather */
    t->low_comm->c_coll->coll_gather((char *)t->sbuf, t->scount, t->sdtype, tmp_rbuf, t->rcount, t->rdtype, t->root_low_rank, t->low_comm, t->low_comm->c_coll->coll_gather_module);
    t->sbuf = tmp_rbuf;
    t->sbuf_inter_free = tmp_buf; //need to be free
    
    /* create uag tasks for the current union segment */
    mca_coll_task_t *uag = OBJ_NEW(mca_coll_task_t);
    /* setup uag task arguments */
    t->cur_task = uag;
    /* init uag task */
    init_task(uag, mca_coll_future_allgather_uag_task, (void *)t);
    /* issure uag task */
    issue_task(uag);
    
    return OMPI_SUCCESS;
}

int mca_coll_future_allgather_uag_task(void *task_argu){
    mca_allgather_argu_t *t = (mca_allgather_argu_t *)task_argu;
    OBJ_RELEASE(t->cur_task);
    
    if (t->noop) {
        OPAL_OUTPUT_VERBOSE((30, mca_coll_future_component.future_output, "[%d] Future Allgather:  uag noop\n", t->w_rank));
    }
    else {
        int low_size = ompi_comm_size(t->low_comm);
        int up_size = ompi_comm_size(t->up_comm);
        char *reorder_buf = NULL;
        char *reorder_rbuf = NULL;
        if (t->is_mapbycore) {
            OPAL_OUTPUT_VERBOSE((30, mca_coll_future_component.future_output, "[%d]: Future Allgather is bycore: ", t->w_rank));
            reorder_rbuf = (char *)t->rbuf;
        }
        else {
            ptrdiff_t rsize, rgap = 0;
            rsize = opal_datatype_span(&t->rdtype->super, (int64_t)t->rcount * low_size * up_size, &rgap);
            reorder_buf = (char *) malloc(rsize);
            reorder_rbuf = reorder_buf - rgap;
        }
        
        /* inter node allgather */
        t->up_comm->c_coll->coll_allgather((char *)t->sbuf, t->scount*low_size, t->sdtype, reorder_rbuf, t->rcount*low_size, t->rdtype, t->up_comm, t->up_comm->c_coll->coll_allgather_module);
        
        if (t->sbuf_inter_free != NULL) {
            free(t->sbuf_inter_free);
            t->sbuf_inter_free = NULL;
        }

        OPAL_OUTPUT_VERBOSE((30, mca_coll_future_component.future_output, "[%d] Future Allgather:  ug allgather finish\n", t->w_rank));
        
        /* reorder the node leader's rbuf */
        /* copy data from tmp_rbuf to rbuf */
        if (!t->is_mapbycore) {
            int i, j;
            ptrdiff_t rextent;
            ompi_datatype_type_extent(t->rdtype, &rextent);
            for (i=0; i<up_size; i++) {
                for (j=0; j<low_size; j++) {
                    OPAL_OUTPUT_VERBOSE((30, mca_coll_future_component.future_output, "[%d]: Future Allgather copy from %d %d\n", t->w_rank, (i*low_size+j)*2+1, t->topo[(i*low_size+j)*2+1]));
                    ompi_datatype_copy_content_same_ddt(t->rdtype,
                                                        (ptrdiff_t)t->rcount,
                                                        (char *)t->rbuf + rextent*(ptrdiff_t)t->topo[(i*low_size+j)*2+1]*(ptrdiff_t)t->rcount,
                                                        reorder_rbuf + rextent*(i*low_size + j)*(ptrdiff_t)t->rcount);
                }
            }
            free(reorder_buf);
            reorder_buf = NULL;
        }
    }
    

    /* create lb tasks for the current union segment */
    mca_coll_task_t *lb = OBJ_NEW(mca_coll_task_t);
    /* setup lb task arguments */
    t->cur_task = lb;
    /* init lb task */
    init_task(lb, mca_coll_future_allgather_lb_task, (void *)t);
    /* issure lb task */
    issue_task(lb);

    return OMPI_SUCCESS;
}

int mca_coll_future_allgather_lb_task(void *task_argu){
    mca_allgather_argu_t *t = (mca_allgather_argu_t *)task_argu;
    OPAL_OUTPUT_VERBOSE((30, mca_coll_future_component.future_output, "[%d] Future Allgather:  uag noop\n", t->w_rank));
    OBJ_RELEASE(t->cur_task);
    int low_size = ompi_comm_size(t->low_comm);
    int up_size = ompi_comm_size(t->up_comm);
    t->low_comm->c_coll->coll_bcast((char *)t->rbuf, t->rcount*low_size*up_size, t->rdtype, t->root_low_rank, t->low_comm, t->low_comm->c_coll->coll_bcast_module);

    ompi_request_t *temp_req = t->req;
    free(t);
    ompi_request_complete(temp_req, 1);
    return OMPI_SUCCESS;

}