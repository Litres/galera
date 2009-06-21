/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
 
/*!
 * @file gcs.c Public GCS API
 */

#ifndef _gcs_h_
#define _gcs_h_

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/*! @typedef @brief Sequence number type. */
typedef int64_t gcs_seqno_t;

/*! @def @brief Illegal sequence number. Action not serialized. */
static const gcs_seqno_t GCS_SEQNO_ILL   = -1;
/*! @def @brief Empty state. No actions applied. */
static const gcs_seqno_t GCS_SEQNO_NIL   =  0;
/*! @def @brief Start of the sequence */
static const gcs_seqno_t GCS_SEQNO_FIRST =  1;
/*! @def @brief history UUID length */
#define GCS_UUID_LEN 16

/*! Connection handle type */
typedef struct gcs_conn gcs_conn_t;

/*! @brief Creates GCS connection handle. 
 * 
 * @param conn connection handle
 * @param backend an URL-like string that specifies backend communication
 *                driver in the form "TYPE://ADDRESS". For Spread backend
 *                it can be "spread://localhost:4803", for dummy backend
 *                ADDRESS field is ignored.
 *
 *                Currently supported backend types: "dummy", "spread", "gcomm"
 *
 * @return pointer to GCS connection handle, NULL in case of failure.
 */
extern gcs_conn_t*
gcs_create  (const char *backend);

/*! @brief Initialize group history values (optional). 
 * Serves to provide group history persistence after process restart (in case
 * these data were saved somewhere on persistent storage or the like). If these
 * values are provided, it is only a hint for the group, as they might be
 * outdated. Actual seqno and UUID are returned in GCS_ACT_CONF action (see
 * below) and are determined by quorum.
 *
 * This function must be called before gcs_open() or after gcs_close().
 *
 * @param seqno Sequence number of the application state (last action applied).
 * @param uuid  UUID of the sequence (group ID).
 *
 * @return 0 in case of success, -EBUSY if conneciton is already opened,
 *         -EBADFD if connection object is being destroyed.
 */
extern long
gcs_init (gcs_conn_t   *conn,
          gcs_seqno_t   seqno,
          const uint8_t uuid[GCS_UUID_LEN]);

/*! @brief Opens connection to group (joins channel). 
 * 
 * @param conn connection object
 * @param channel a name of the channel to join. It must uniquely identify
 *                the channel. If the channel with such name does not exist,
 *                it is created. Processes that joined the same channel
 *                receive the same actions.
 *
 * @return negative error code, 0 in case of success.
 */
extern long gcs_open  (gcs_conn_t *conn,
                       const char *channel);

/*! @brief Closes connection to group.
 *
 * @param  conn connection handle
 * @return negative error code or 0 in case of success.
 */
extern long gcs_close (gcs_conn_t *conn);

/*! @brief Frees resources associuated with connection handle.
 *
 * @param  conn connection handle
 * @return negative error code or 0 in case of success.
 */
extern long gcs_destroy (gcs_conn_t *conn);

/*! @brief Waits until the group catches up.
 * This call checks if any member of the group (including this one) has a
 * long slave queue. Should be called before gcs_repl(), gcs_send() or
 * gcs_join().
 *
 * @return negative error code, 1 if wait is required, 0 otherwise
 */
extern long gcs_wait (gcs_conn_t *conn);

/*! @typedef @brief Action types.
 * There is a conceptual difference between "messages"
 * and "actions". Messages are ELEMENTARY pieces of information
 * atomically delivered by group communication. They are typically
 * limited in size to a single IP packet and should not be normally
 * bigger than an ethernet frame. Events generated by group
 * communication layer must be delivered as a single message.
 *
 * For the purpose of this work "action" is a higher level concept
 * introduced to overcome the message size limitation. Application
 * replicates information in actions of ARBITRARY size that are
 * fragmented into as many messages as needed. As such actions
 * can be delivered only in primary configuration, when total order
 * of underlying messages is established.
 * The best analogy for action/message concept would be word/letter.
 *
 * The purpose of GCS library is to hide message handling from application.
 * Therefore application deals only with "actions".
 * Application can only send actions of types GCS_ACT_DATA and
 * GCS_ACT_SNAPSHOT. Actions of type GCS_ACT_SYNC, GCS_ACT_PRIMARY and
 * GCS_ACT_NON_PRIMARY are generated by the library.
 */
typedef enum gcs_act_type
{
/* ordered actions */
    GCS_ACT_DATA,       //! application action, sent by application
    GCS_ACT_COMMIT_CUT, //! group-wide action commit cut
    GCS_ACT_STATE_REQ,  //! request for state transfer
    GCS_ACT_CONF,       //! new configuration
    GCS_ACT_JOIN,       //! state transfer status
    GCS_ACT_SYNC,       //! synchronized with group
    GCS_ACT_FLOW,       //! flow control
    GCS_ACT_SERVICE,    //! service action, sent by GCS
    GCS_ACT_ERROR,      //! error happened while receiving the action
    GCS_ACT_UNKNOWN     //! undefined/unknown action type
}
gcs_act_type_t;

/*! @brief Sends an action to group and returns.
 * Action is not duplicated, therefore action buffer
 * should not be accessed by application after the call returns.
 * Action will be either returned through gcs_recv() call, or discarded
 * (memory freed) in case it is not delivered by group. For a better
 * means to replicate an action see gcs_repl(). @see gcs_repl()
 *
 * @param conn opened connection
 * @param act_type action type
 * @param act_size action size
 * @param action action buffer
 * @return negative error code, action size in case of success
 */
extern long gcs_send (gcs_conn_t          *conn,
                      const void          *action,
                      const size_t         act_size,
                      const gcs_act_type_t act_type);

/*! @brief Receives an action from group.
 * Blocks if no actions are available. Action buffer is allocated by GCS
 * and must be freed by application when action is no longer needed.
 * Also sets global and local action IDs. Global action ID uniquely identifies
 * action in the history of the group and can be used to identify the state
 * of the application for the state snapshot purposes. Local action ID is a
 * monotonic gapless number sequence starting with 1 which can be used
 * to serialize access to critical sections.
 * 
 * @param conn opened connection to group
 * @param act_type action type
 * @param act_size action size
 * @param action action buffer
 * @param act_id global action ID (sequence number)
 * @param local_act_id local action ID (sequence number)
 * @return negative error code, action size in case of success
 */
extern long gcs_recv (gcs_conn_t      *conn,
                      void           **action,
                      size_t          *act_size,
                      gcs_act_type_t  *act_type,
                      gcs_seqno_t     *act_id,
                      gcs_seqno_t     *local_act_id);

/*! @brief Replicates an action.
 * Sends action to group and blocks until it is received. Upon return global
 * and local IDs are set. Arguments are the same as in gcs_recv().
 * @see gcs_recv()
 *
 * @param conn opened connection to group
 * @param act_type action type
 * @param act_size action size
 * @param action action buffer
 * @param act_id global action ID (sequence number)
 * @param local_act_id local action ID (sequence number)
 * @return negative error code, action size in case of success
 */
extern long gcs_repl (gcs_conn_t          *conn,
                      const void          *action,
                      const size_t         act_size,
                      const gcs_act_type_t act_type,
                      gcs_seqno_t         *act_id,
                      gcs_seqno_t         *local_act_id);

/*! @brief Sends state transfer request
 * Broadcasts state transfer request which will be passed to one of the
 * suitable group members.
 *
 * @param conn  connection to group
 * @param req   opaque byte array that contains data required for
 *              the state transfer (application dependent)
 * @param size  request size
 * @param seqno response to request was ordered with this seqno.
 *              Must be skipped in local queues.
 * @return negative error code, index of state transfer donor in case of success
 *         (notably, -EAGAIN means try later)
 */
extern long gcs_request_state_transfer (gcs_conn_t  *conn,
                                        const void  *req,
                                        size_t       size,
                                        gcs_seqno_t *local_act_id);

/*! @brief Informs group on behalf of donor that state stransfer is over.
 * If status is non-negative, joiner will be considered fully joined to group.
 *
 * @param conn opened connection to group
 * @param status negative error code in case of state transfer failure,
 *               0 or (optional) seqno corresponding to transferred state.
 * @return negative error code, 0 in case of success
 */
extern long gcs_join (gcs_conn_t *conn, gcs_seqno_t status);


///////////////////////////////////////////////////////////////////////////////

/*! Total Order object */
typedef struct gcs_to gcs_to_t;

/*! @brief Creates TO object.
 * TO object can be used to serialize access to application
 * critical section using sequence number.
 *
 * @param len   A length of the waiting queue. Should be no less than the
 *              possible maximum number of threads competing for the resource,
 *              but should not be too high either. Perhaps 1024 is good enough
 *              for most applications.
 * @param seqno A starting sequence number
 *              (the first to be used by gcs_to_grab()).
 * @return Pointer to TO object or NULL in case of error.
 */
extern gcs_to_t* gcs_to_create (int len, gcs_seqno_t seqno);

/*! @brief Destroys TO object.
 *
 * @param to A pointer to TO object to be destroyed
 * @return 0 in case of success, negative code in case of error.
 *         In particular -EBUSY means the object is used by other threads.
 */
extern long gcs_to_destroy (gcs_to_t** to);

/*! @brief Grabs TO resource in the specified order.
 * On successful return the mutex associated with specified TO is locked.
 * Must be released gcs_to_release(). @see gcs_to_release
 *
 * @param to    TO resource to be acquired.
 * @param seqno The order at which TO resouce should be aquired. For any N
 *              gcs_to_grab (to, N) will return exactly after
 *              gcs_to_release (to, N-1).
 * @return 0 in case of success, negative code in case of error.
 *         -EAGAIN means that there are too many threads waiting for TO
 *         already. It is safe to try again later.
 *         -ECANCEL if waiter was canceled, seqno is skipped in TO
 *         -EINTR if wait was interrupted, must retry grabbing later
 */
extern long gcs_to_grab (gcs_to_t* to, gcs_seqno_t seqno);

/*! @brief Releases TO specified resource.
 * On succesful return unlocks the mutex associated with TO.
 * TO must be previously acquired with gcs_to_grab(). @see gcs_to_grab
 *
 * @param to TO resource that was previously acquired with gcs_to_grab().
 * @param seqno The same number with which gcs_to_grab() was called.
 * @return 0 in case of success, negative code in case of error. Any error
 *         here is an application error - attempt to release TO resource
 *         out of order (not paired with gcs_to_grab()).
 */
extern long gcs_to_release (gcs_to_t* to, gcs_seqno_t seqno);

/*! @brief The last sequence number that had been used to access TO object.
 * Note that since no locks are held, it is a conservative estimation.
 * It is guaranteed however that returned seqno is no longer in use.
 *
 * @param to A pointer to TO object.
 * @return GCS sequence number. Since GCS TO sequence starts with 1, this
 *         sequence can start with 0.
 */
extern gcs_seqno_t gcs_to_seqno (gcs_to_t* to);

/*! @brief cancels a TO monitor waiter making it return immediately
 * It is assumed that the caller is currenly holding the TO.
 * The to-be-cancelled waiter can be some later transaction but also
 * some earlier transaction. Tests have shown that the latter case 
 * can also happen.
 *
 * @param to A pointer to TO object.
 * @param seqno Seqno of the waiter object to be cancelled
 * @return 0 for success and -ERANGE, if trying to cancel an earlier
 *         transaction
 */
extern long gcs_to_cancel (gcs_to_t *to, gcs_seqno_t seqno);


/*!
 * Self cancel to without attempting to enter critical secion
 */
extern long gcs_to_self_cancel(gcs_to_t *to, gcs_seqno_t seqno);

/*! @brief interrupts from TO monitor waiting state.
 *  Seqno remains valid in the queue and later seqnos still need to
 *  wait for this seqno to be released.
 * 
 *  The caller can (and must) later try gcs_to_grab() again or cancel
 *  the seqno with gcs_to_self_cancel().
 *
 * @param to A pointer to TO object.
 * @param seqno Seqno of the waiter object to be interrupted
 * @return 0 for success and -ERANGE, if trying to interrupt an already
 *         used transaction
 */
extern long gcs_to_interrupt (gcs_to_t *to, gcs_seqno_t seqno);
    
/* Service functions */

/*! Informs group about the last applied action on this node */
extern long gcs_set_last_applied (gcs_conn_t* conn, gcs_seqno_t seqno);


/* GCS Configuration */

/* Logging options */
extern long gcs_conf_set_log_file     (FILE *file);
extern long gcs_conf_set_log_callback (void (*logger) (int, const char*));
extern long gcs_conf_self_tstamp_on   ();
extern long gcs_conf_self_tstamp_off  ();
extern long gcs_conf_debug_on         ();
extern long gcs_conf_debug_off        ();

/* Sending options */
/* Sets maximum DESIRED network packet size.
 * For best results should be multiple of MTU */
extern long
gcs_conf_set_pkt_size (gcs_conn_t *conn, long pkt_size);
//#define GCS_DEFAULT_PKT_SIZE 1500 /* Standard Ethernet frame */
#define GCS_DEFAULT_PKT_SIZE 64500 /* 43 Eth. frames to carry max IP packet */

/* Configuration action */
/*! Member name max length (including terminating null) */
#define GCS_MEMBER_NAME_MAX 40

typedef struct {
    gcs_seqno_t  seqno;         /// last global seqno applied by this group
    gcs_seqno_t  conf_id;       /// configuration ID (-1 if non-primary)
    uint8_t      group_uuid[GCS_UUID_LEN];/// group UUID
    bool         st_required;   /// state transfer is required (gap in seqnos)
    long         memb_num;      /// number of members in configuration
    long         my_idx;        /// index of this node in the configuration
    char         data[0];       /// member array (null-terminated IDs)
} gcs_act_conf_t;

#ifdef	__cplusplus
}
#endif

#endif // _gcs_h_