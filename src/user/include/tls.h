/*
 * A TLS 1.2 client (X25519, AES-128-GCM, SHA-256). The server's certificate
 * chain, host name and key-exchange signature are verified against the trusted
 * roots in /conf/roots.der; a failure aborts the connection.
 */
#ifndef TLS_H
#define TLS_H

/* All return a negative value on failure; tls_error() then says why. */
int tls_connect(int tcp_connection, const char *host);
int tls_send(const void *data, unsigned int length);
/* Returns the bytes read, or 0 when the server has closed the session. */
int tls_receive(void *buf, unsigned int size);
void tls_close(void);
const char *tls_error(void);

#endif
