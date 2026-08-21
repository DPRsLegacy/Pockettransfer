#ifndef POCKETTRANSFER_BOXES_H
#define POCKETTRANSFER_BOXES_H

/* Interactive save/bank PC. Returns 1 if the session save should be written back. */
int boxes_run(const char *host, const char *session_id);

/* View bank boxes with no game save loaded. */
int boxes_browse(const char *host);

#endif
