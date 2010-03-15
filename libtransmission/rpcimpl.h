/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_RPC_H
#define TR_RPC_H

#ifdef __cplusplus
extern "C" {
#endif

/***
****  RPC processing
***/

typedef enum
{
    TR_QUEUE_UP         = 0,
    TR_QUEUE_DOWN       = 1,
    TR_QUEUE_TOP        = 2,
    TR_QUEUE_BOTTOM     = 3,
    TR_QUEUE_IGNORE     = 4,
    TR_QUEUE_UNIGNORE   = 5
}
tr_queue_direction;

struct tr_benc;

/* FIXME(libevent2): make "response" an evbuffer and remove response_len */
typedef void( *tr_rpc_response_func )( tr_session      * session,
                                       const char      * response,
                                       size_t            response_len,
                                       void            * user_data );
/* http://www.json.org/ */
void tr_rpc_request_exec_json( tr_session            * session,
                               const void            * request_json,
                               int                     request_len,
                               tr_rpc_response_func    callback,
                               void                  * callback_user_data );

/* see the RPC spec's "Request URI Notation" section */
void tr_rpc_request_exec_uri( tr_session           * session,
                              const void           * request_uri,
                              int                    request_len,
                              tr_rpc_response_func   callback,
                              void                 * callback_user_data );

void tr_rpc_parse_list_str( struct tr_benc * setme,
                            const char     * list_str,
                            int              list_str_len );

#ifdef __cplusplus
}
#endif

#endif /* TR_RPC_H */
