/*
 *  Author : WangBoJing , email : 1989wangbojing@gmail.com
 * 
 *  Copyright Statement:
 *  --------------------
 *  This software is protected by Copyright and the information contained
 *  herein is confidential. The software may not be copied and the information
 *  contained herein may not be used or disclosed except with the written
 *  permission of Author. (C) 2016
 * 
 *
 
****       *****
  ***        *
  ***        *                         *               *
  * **       *                         *               *
  * **       *                         *               *
  *  **      *                        **              **
  *  **      *                       ***             ***
  *   **     *       ******       ***********     ***********    *****    *****
  *   **     *     **     **          **              **           **      **
  *    **    *    **       **         **              **           **      *
  *    **    *    **       **         **              **            *      *
  *     **   *    **       **         **              **            **     *
  *     **   *            ***         **              **             *    *
  *      **  *       ***** **         **              **             **   *
  *      **  *     ***     **         **              **             **   *
  *       ** *    **       **         **              **              *  *
  *       ** *   **        **         **              **              ** *
  *        ***   **        **         **              **               * *
  *        ***   **        **         **     *        **     *         **
  *         **   **        **  *      **     *        **     *         **
  *         **    **     ****  *       **   *          **   *          *
*****        *     ******   ***         ****            ****           *
                                                                       *
                                                                      *
                                                                  *****
                                                                  ****


 *
 */

#include <pthread.h>
#include <poll.h>

#include "NattyUtils.h"
#include "NattyResult.h"
#include "NattyPush.h"
#include "NattyAbstractClass.h"



static int ntyPushTcpConnect(void *self, U8 mode);
static SSL* ntyPushSSLConnect(void *self, int sockfd, U8 mode);

void *ntyPushHandleInstance(void);
void *ntyPushHandleRelease(void);
void *ntyPushHandleGetInstance(void);



int ntyDeviceToken2Binary(const char *sz, const int len, unsigned char *const binary, const int size) {
	int i, val;
	const char *pin;
	char buf[3] = {0};
	
	if (size < TOKEN_SIZE) return NTY_RESULT_FAILED;

	for (i = 0;i < len;i ++) {
		pin = sz + i * 2;
		buf[0] = pin[0];
		buf[1] = pin[1];

		val = 0;
		sscanf(buf, "%X", &val);
		binary[i] = val;
	}

	return NTY_RESULT_SUCCESS;
}


int ntyDeviceBinary2Token(const unsigned char *data, const int len, char *const token, const int size) {
	int i;
	if (size <= TOKEN_SIZE*2) return NTY_RESULT_FAILED;

	for (i = 0;i < len;i ++) {
		sprintf(token+i*2, "%02x", data[i]);
	}
	return NTY_RESULT_SUCCESS;
}


void ntyCloseSocket(int sockfd) {
#ifdef _WIN32 
	closesocket(sockfd);
#else
	close(sockfd);
#endif
}


void ntyOpensslInitialize(void) {
#ifdef _WIN32 
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

	SSL_library_init();
	ERR_load_BIO_strings();

	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	
}

SSL_CTX *ntyContextInitialize(const char *crt, const char *key, const char *pwd, const char *cacert) {
	SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
	if (!ctx) {
		return NULL;
	}

	if (SSL_CTX_use_certificate_file(ctx, crt, SSL_FILETYPE_PEM) <= 0) {
		return NULL;
	}

	if (SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <= 0) {
		return NULL;
	}

	if (SSL_CTX_check_private_key(ctx) == 0) {
		return NULL;
	}

	if (cacert) {
		if (!SSL_CTX_load_verify_locations(ctx, cacert, NULL)) {
			return NULL;
		}
	}

	return ctx;
}


int tcp_connect(const char *host, int port) {
	struct hostent *hp;
	struct sockaddr_in addr;
	int sock = -1;

	if (!(hp = gethostbyname(host))) {
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_addr = *(struct in_addr*)hp->h_addr_list[0];
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		return NTY_RESULT_FAILED;
	}

	int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
	if (ret != 0) {
		return NTY_RESULT_FAILED;
	}
	return sock;
}

SSL *ssl_connect(SSL_CTX *ctx, int sockfd) {
	SSL *ssl = SSL_new(ctx);
	BIO *bio = BIO_new_socket(sockfd, BIO_NOCLOSE);

	SSL_set_bio(ssl, bio, bio);
	if (SSL_connect(ssl) <= 0) {
		return NULL;
	}
	return ssl;
}

int verify_connection(SSL *ssl, const char *peername) {
	int result = SSL_get_verify_result(ssl);
	if (result != X509_V_OK) {
		fprintf(stderr, "WARNING ! ssl verify failed:%d\n", result);
		return NTY_RESULT_FAILED;
	}

	X509 *peer;
	char peer_CN[256] = {0};

	peer = SSL_get_peer_certificate(ssl);
	X509_NAME_get_text_by_NID(X509_get_subject_name(peer), NID_commonName, peer_CN, 255);
	if (strcmp(peer_CN, peername) != 0) {
		fprintf(stderr, "WARNING ! Server Name Doesn't match, got: %s, required: %s", peer_CN, peername);
	}
	return NTY_RESULT_SUCCESS;
}

int json_escape(char *str) {
	int n;
	char buf[1024];

	n = strlen(str) * sizeof(char) + 100;

	if (n >= sizeof(buf)) return NTY_RESULT_FAILED;

	strncpy(buf, str, n);
	buf[n] = '\0';
	char *found = buf;

	while (*found != '\0') {
		if ('\\' == *found || '"' == *found || '\n' == *found || '/' == *found) {
			*str++ = '\\';
		}

		if ('\n' == *found) {
			*found = 'n';
		}

		*str++ = *found++;
	}

	*str = '\0';

	return NTY_RESULT_SUCCESS;
}

int build_payload(char *buffer, int *plen, char *msg, int badage, const char *sound) {
	int n;
	char buf[2048];
	char str[2048] = "{\"aps\":{\"alert\":\"";

	n = strlen(str);

	if (msg) {
		strcpy(buf, msg);
		json_escape(buf);

		n = n + sprintf(str+n, "%s", buf);
	}
	n = n + sprintf(str+n, "%s%d", "\",\"badge\":", badage);

	if (sound) {
		n = n + sprintf(str+n, "%s", ",\"sound\":\"");
		strcpy(buf, sound);

		json_escape(buf);
		n = n + sprintf(str+n, "%s%s", buf, "\"");
	}

	strcat(str, "}}");

	n = strlen(str);

	if (n > *plen) {
		*plen = n;
		return NTY_RESULT_FAILED;
	}

	if (n < *plen) {
		strcpy(buffer, str);
	} else {
		strncpy(buffer, str, *plen);
	}
	*plen = n;

	return *plen;
}

int build_output_packet(char *buf, int buflen, const char *tokenbinary, char *msg, int badage, const char *sound) {

	if (buflen < 1+2+TOKEN_SIZE+2+MAX_PAYLOAD_SIZE) return NTY_RESULT_FAILED;

	char *pdata = buf;
	*pdata = 0;
	pdata ++;

    *(uint16_t*)pdata = htons(TOKEN_SIZE);
	pdata += 2;
	memcpy(pdata, tokenbinary, TOKEN_SIZE);

	pdata += TOKEN_SIZE;
	int payloadlen = MAX_PAYLOAD_SIZE;

	if (build_payload(pdata + 2, &payloadlen, msg, badage, sound) < 0) {
		msg[strlen(msg) - (payloadlen - MAX_PAYLOAD_SIZE)] = '\0';
		payloadlen = MAX_PAYLOAD_SIZE;

		if (build_payload(pdata+2, &payloadlen, msg, badage, sound) <= 0) {
			return NTY_RESULT_FAILED;
		}
	}

    *(uint16_t*)pdata = htons(payloadlen);
	return 1 + 2 + TOKEN_SIZE + 2 + payloadlen;
}


int send_message(SSL *ssl, const char *token, char *msg, int badage, const char *sound) {
	int n;
	char buf[1+2+TOKEN_SIZE + 2 + MAX_PAYLOAD_SIZE];
	unsigned char binary[TOKEN_SIZE];
	int buflen = sizeof(buf);
	if ( token != NULL ){
		n = strlen(token);
	}
	ntyDeviceToken2Binary(token, n, binary, TOKEN_SIZE);

	buflen = build_output_packet(buf, buflen, (const char *)binary, msg, badage, sound);
	if (buflen <= 0) {
		return NTY_RESULT_FAILED;
	}

	return SSL_write(ssl, buf, buflen);
	
}


int build_output_packet_2(char* buf, int buflen, /* ?????? */
                          uint32_t messageid, /* ???? */
                          uint32_t expiry, /* ???? */
                          const char* tokenbinary, /* ???Token */
                          char* msg, /* message */
                          int badage, /* badage */
                          const char * sound) /* sound */
{
	if (buflen < 1 + 4 + 4 + 2 + TOKEN_SIZE + 2 + MAX_PAYLOAD_SIZE) return NTY_RESULT_FAILED;

	char *pdata = buf;
	*pdata = 1;

	pdata ++;
    *(uint32_t*)pdata = messageid;

	pdata += 4;
    *(uint32_t*)pdata = htonl(expiry);

	pdata += 4;
    *(uint16_t*)pdata = htons(TOKEN_SIZE);

	pdata += 2;
	memcpy(pdata, tokenbinary, TOKEN_SIZE);

	pdata += TOKEN_SIZE;
	int payloadlen = MAX_PAYLOAD_SIZE;

	if (build_payload(pdata+2, &payloadlen, msg, badage, sound) < 0) {
		msg[strlen(msg)-(payloadlen-MAX_PAYLOAD_SIZE)] = '\0';
		payloadlen = MAX_PAYLOAD_SIZE;

		if (build_payload(pdata + 2, &payloadlen, msg, badage, sound) <= 0) {
			return NTY_RESULT_FAILED;
		}
	}

    *(uint16_t*)pdata = htons(payloadlen);

    return 1 + 4 + 4 + 2 + TOKEN_SIZE + 2 + payloadlen;
}

int send_message_2(SSL *ssl, const char* token, uint32_t id, uint32_t expire, char* msg, int badage, const char* sound)
{
	int i, n;
	char buf[1+4+4+2+TOKEN_SIZE+2+MAX_PAYLOAD_SIZE];
	unsigned char binary[TOKEN_SIZE];
	int buflen = sizeof(buf);
	if ( token != NULL ){
		n = strlen(token);
	}
	printf("token length : %d, TOKEN_SIZE = %d\n, token = %s\n", n, TOKEN_SIZE, token);
	ntyDeviceToken2Binary(token, n, binary, TOKEN_SIZE);
	

	for (i = 0;i < TOKEN_SIZE;i ++) {
		printf("%d ", binary[i]);
	}
	printf("\n");

	buflen = build_output_packet_2(buf, buflen, id, expire, (const char *)binary, msg, badage, sound);
	if (buflen <= 0) {
		return NTY_RESULT_FAILED;
	}
	n = SSL_write(ssl, buf, buflen);

	return n;
	
}


static int ntyJsonEscape(char *str) {
	int n;
	char buf[MAX_MESSAGE_PACKET];

	n = strlen(str) * sizeof(char) + 100;

	if (n >= sizeof(buf)) return NTY_RESULT_FAILED;

	strncpy(buf, str, n);
	buf[n] = '\0';
	char *found = buf;

	while (*found != '\0') {
		if ('\\' == *found || '"' == *found || '\n' == *found || '/' == *found) {
			*str++ = '\\';
		}

		if ('\n' == *found) {
			*found = 'n';
		}

		*str++ = *found++;
	}

	*str = '\0';

	return NTY_RESULT_SUCCESS;
}



// {"aps":{"alert" : "You got your emails.","badge" : 9,"sound" : "default"}}
int ntyBuildPayload(C_DEVID gId, U32 type, U32 counter, char *buffer, int *plen, char *msg, int badage, const char *sound) {
	int n;
	
	char buf[2048];
	char str[2048] = "{\"aps\":{\"alert\":\"";

	n = strlen(str);

	if (msg) {
		strcpy(buf, msg);
		ntyJsonEscape(buf);

		n = n + sprintf(str+n, "%s", buf);
	}
	n = n + sprintf(str+n, "%s%d", "\",\"badge\":", counter);
	n = n + sprintf(str+n, ",\"deviceId\":\"%llx\"", gId);
	n = n + sprintf(str+n, ",\"type\":\"%d\"", type);
#if 0
	n = n + sprintf(str+n, ",\"count\":\"%d\"", counter);
#endif
	if (sound) {
		n = n + sprintf(str+n, "%s", ",\"sound\":\"");
		strcpy(buf, sound);

		json_escape(buf);
		n = n + sprintf(str+n, "%s%s", buf, "\"");
	}

	strcat(str, "}}");

	n = strlen(str);

	if (n > *plen) {
		*plen = n;
		return NTY_RESULT_FAILED;
	}

	if (n < *plen) {
		strcpy(buffer, str);
	} else {
		strncpy(buffer, str, *plen);
	}
	*plen = n;

	ntylog("ntyBuildPayload -> %s\n", buffer);

	return *plen;
}


int ntyBuildPacket(C_DEVID gId, U32 type, U32 counter, char* buf, int buflen, unsigned int messageid, unsigned int expiry, 
					const char* tokenbinary, char* msg, int badage, const char * sound) {
	
	if (buflen < 1 + 4 + 4 + 2 + TOKEN_SIZE + 2 + MAX_PAYLOAD_SIZE) return NTY_RESULT_FAILED;

	char *pdata = buf;
	*pdata = 1;

	pdata ++;
    *(uint32_t*)pdata = messageid;

	pdata += 4;
    *(uint32_t*)pdata = htonl(expiry);

	pdata += 4;
    *(uint16_t*)pdata = htons(TOKEN_SIZE);

	pdata += 2;
	memcpy(pdata, tokenbinary, TOKEN_SIZE);

	pdata += TOKEN_SIZE;
	int payloadlen = MAX_PAYLOAD_SIZE;

	if (ntyBuildPayload(gId, type, counter, pdata+2, &payloadlen, msg, badage, sound) < 0) {
		msg[strlen(msg)-(payloadlen-MAX_PAYLOAD_SIZE)] = '\0';
		payloadlen = MAX_PAYLOAD_SIZE;

		if (ntyBuildPayload(gId, type, counter, pdata + 2, &payloadlen, msg, badage, sound) <= 0) {
			return NTY_RESULT_FAILED;
		}
	}

    *(uint16_t*)pdata = htons(payloadlen);

    return 1 + 4 + 4 + 2 + TOKEN_SIZE + 2 + payloadlen;
}

int ntySendMessage(C_DEVID gId, U32 type, U32 counter, SSL *ssl, const char* token, uint32_t id, uint32_t expire, 
	char* msg, int badage, const char* sound) {
	
	int i, n;
	char buf[1+4+4+2+TOKEN_SIZE+2+MAX_PAYLOAD_SIZE];
	unsigned char binary[TOKEN_SIZE];
	int buflen = sizeof(buf);

	if (token == NULL) return NTY_RESULT_ERROR;

	n = strlen(token);
	ntylog("ntySendMessage token length : %d, TOKEN_SIZE = %d\n, token = %s\n", n, TOKEN_SIZE, token);
	ntyDeviceToken2Binary(token, n, binary, TOKEN_SIZE);
	
#if 0
	for (i = 0;i < TOKEN_SIZE;i ++) {
		ntylog("%d ", binary[i]);
	}
	ntylog("\n");
#endif

	buflen = ntyBuildPacket(gId, type, counter, buf, buflen, id, expire, (const char *)binary, msg, badage, sound);
	if (buflen <= 0) {
		return NTY_RESULT_FAILED;
	}

	return SSL_write(ssl, buf, buflen);
	
}

int exiting = 0;
struct sockaddr_in addr[NTY_PUSH_CLIENT_COUNT] = {0};



static void *ntyPushRecvCallback(void *arg) {
	nPushContext *pCtx = (nPushContext*)arg;
	if (pCtx == NULL) return NULL;
	int ret = 0;
	struct pollfd fds[NTY_PUSH_CLIENT_COUNT] = {0};
	
	fds[NTY_PUSH_CLIENT_DEVELOPMENT].fd = pCtx->d_sockfd;
	fds[NTY_PUSH_CLIENT_DEVELOPMENT].events = POLLIN;

	fds[NTY_PUSH_CLIENT_PRODUCTION].fd = pCtx->p_sockfd;
	fds[NTY_PUSH_CLIENT_PRODUCTION].events = POLLIN;

	fds[NTY_PUSH_CLIENT_DEVELOPMENT_B].fd = pCtx->d_sockfd;
	fds[NTY_PUSH_CLIENT_DEVELOPMENT_B].events = POLLIN;

	fds[NTY_PUSH_CLIENT_PRODUCTION_B].fd = pCtx->p_sockfd;
	fds[NTY_PUSH_CLIENT_PRODUCTION_B].events = POLLIN;

	while (1) {
		if ( exiting ) { 		
			break;
		}
		ret = poll(fds, 1, 5);
		if ( ret ) {
			if ( fds[NTY_PUSH_CLIENT_DEVELOPMENT].events & POLLIN ) {
				U8 buffer[1024] = {0};
				int rLen = read(fds[NTY_PUSH_CLIENT_DEVELOPMENT].fd, buffer, 1024);
				if ( rLen <= 0 ) {
					ntylog("ntyPushRecvCallback --> development disconnect\n");				
					ntyPushHandleRelease();
				} else {
					ntylog("ntyPushRecvCallback --> development: %s\n", buffer);
				}
			}
			if ( fds[NTY_PUSH_CLIENT_PRODUCTION].events & POLLIN ) {
				U8 buffer[1024] = {0};
				int rLen = read(fds[NTY_PUSH_CLIENT_PRODUCTION].fd, buffer, 1024);
				if ( rLen <= 0 ) {
					ntylog("ntyPushRecvCallback --> production disconnect\n");					
					ntyPushHandleRelease();
				} else {
					ntylog("ntyPushRecvCallback --> production: %s\n", buffer);
				}
			}
			if ( fds[NTY_PUSH_CLIENT_DEVELOPMENT_B].events & POLLIN ) {
				U8 buffer[1024] = {0};
				int rLen = read(fds[NTY_PUSH_CLIENT_DEVELOPMENT_B].fd, buffer, 1024);
				if ( rLen <= 0 ) {
					ntylog("ntyPushRecvCallback --> development B disconnect\n");				
					ntyPushHandleRelease();
				} else {
					ntylog("ntyPushRecvCallback --> development B: %s\n", buffer);
				}
			}
			if ( fds[NTY_PUSH_CLIENT_PRODUCTION_B].events & POLLIN ) {
				U8 buffer[1024] = {0};
				int rLen = read(fds[NTY_PUSH_CLIENT_PRODUCTION_B].fd, buffer, 1024);
				if (rLen <= 0) {
					ntylog("ntyPushRecvCallback --> production B disconnect\n");			
					ntyPushHandleRelease();
				} else {
					ntylog("ntyPushRecvCallback --> production B: %s\n", buffer);
				}
			}
		}
	}
} 


void* ntyPushContextCtor(void *self, va_list *params) {
	nPushContext *pCtx = self;

	ntyOpensslInitialize();

	pCtx->ctx = ntyContextInitialize(APPLE_CLIENT_PEM_NAME, APPLE_CLIENT_PEM_NAME, APPLE_CLIENT_PEM_PWD, APPLE_SERVER_PEM_NAME);
	pCtx->pctx = ntyContextInitialize(APPLE_CLIENT_PEM_NAME_PUBLISH, APPLE_CLIENT_PEM_NAME_PUBLISH, APPLE_CLIENT_PEM_PWD, APPLE_SERVER_PEM_NAME);
	pCtx->ctx_b = ntyContextInitialize(APPLE_CLIENT_PEM_NAME_JINGWU, APPLE_CLIENT_PEM_NAME_JINGWU, APPLE_CLIENT_PEM_PWD, APPLE_SERVER_PEM_NAME);
	pCtx->pctx_b = ntyContextInitialize(APPLE_CLIENT_PEM_NAME_PUBLISH_JINGWU, APPLE_CLIENT_PEM_NAME_PUBLISH_JINGWU, APPLE_CLIENT_PEM_PWD, APPLE_SERVER_PEM_NAME);
	
	
	pCtx->d_sockfd = ntyPushTcpConnect(pCtx, NTY_PUSH_CLIENT_DEVELOPMENT);
	if (pCtx->d_sockfd < 0) {
		ntylog("failed to connect to host %s\n", strerror(errno));
		return pCtx;
	}

	pCtx->d_ssl = ntyPushSSLConnect(pCtx, pCtx->d_sockfd, NTY_PUSH_CLIENT_DEVELOPMENT);
	if (pCtx->d_ssl == NULL) {
		ntylog("ssl connect failed: %s\n", ERR_reason_error_string(ERR_get_error())); 
		ntyCloseSocket(pCtx->d_sockfd);

		return pCtx;
	}

	ntylog(" development is success\n");


	pCtx->p_sockfd = ntyPushTcpConnect(pCtx, NTY_PUSH_CLIENT_PRODUCTION);
	if (pCtx->p_sockfd < 0) {
		ntylog("failed to connect to host %s\n", strerror(errno));
		return pCtx;
	}
	
	pCtx->p_ssl = ntyPushSSLConnect(pCtx, pCtx->p_sockfd, NTY_PUSH_CLIENT_PRODUCTION);
	if (pCtx->p_ssl == NULL) {
		ntylog("ssl connect failed: %s\n", ERR_reason_error_string(ERR_get_error())); 
		ntyCloseSocket(pCtx->p_sockfd);

		return pCtx;
	}

	ntylog(" production is success\n");

	//B stand for jingwu
	ntylog("ntyPushContextCtor\n");
	
	pCtx->d_sockfd_b = ntyPushTcpConnect(pCtx, NTY_PUSH_CLIENT_DEVELOPMENT_B);
	if (pCtx->d_sockfd_b < 0) {
		ntylog("failed to connect to host %s\n", strerror(errno));
		return pCtx;
	}

	pCtx->d_ssl_b = ntyPushSSLConnect(pCtx, pCtx->d_sockfd_b, NTY_PUSH_CLIENT_DEVELOPMENT_B);
	if (pCtx->d_ssl_b == NULL) {
		ntylog("ssl connect failed: %s\n", ERR_reason_error_string(ERR_get_error())); 
		ntyCloseSocket(pCtx->d_sockfd_b);

		return pCtx;
	}

	ntylog(" development version b is success\n");


	pCtx->p_sockfd_b = ntyPushTcpConnect(pCtx, NTY_PUSH_CLIENT_PRODUCTION_B);
	if (pCtx->p_sockfd_b < 0) {
		ntylog("failed to connect to host %s\n", strerror(errno));
		return pCtx;
	}
	
	pCtx->p_ssl_b = ntyPushSSLConnect(pCtx, pCtx->p_sockfd_b, NTY_PUSH_CLIENT_PRODUCTION_B);
	if (pCtx->p_ssl_b == NULL) {
		ntylog("ssl connect failed: %s\n", ERR_reason_error_string(ERR_get_error())); 
		ntyCloseSocket(pCtx->p_sockfd_b);

		return pCtx;
	}

	ntylog(" production version b is success\n");


#if 1 //setup recv thread
	pthread_t recvThreadId;
	int err = pthread_create(&recvThreadId, NULL, ntyPushRecvCallback, pCtx);				
	if (err != 0) { 				
		ntylog(" can't create thread:%s\n", strerror(err)); 
	}
#endif

	return pCtx;
}

void* ntyPushContextDtor(void *self) {

	nPushContext *pCtx = self;

	if (pCtx != NULL) {
		if (pCtx->ctx != NULL) {
			SSL_CTX_free(pCtx->ctx);
		}

		if (pCtx->pctx != NULL) {
			SSL_CTX_free(pCtx->pctx);
		}

		if (pCtx->d_ssl != NULL) {
			SSL_shutdown(pCtx->d_ssl);
		}
		if (pCtx->d_sockfd >= 0) {
			ntyCloseSocket(pCtx->d_sockfd);
		}

		if (pCtx->p_ssl != NULL) {
			SSL_shutdown(pCtx->p_ssl);
		}
		if (pCtx->p_sockfd >= 0) {
			ntyCloseSocket(pCtx->p_sockfd);
		}
		//B stand for jingwu
		if (pCtx->ctx_b != NULL) {
			SSL_CTX_free(pCtx->ctx_b);
		}

		if (pCtx->pctx_b != NULL) {
			SSL_CTX_free(pCtx->pctx_b);
		}

		if (pCtx->d_ssl_b != NULL) {
			SSL_shutdown(pCtx->d_ssl_b);
		}
		if (pCtx->d_sockfd_b >= 0) {
			ntyCloseSocket(pCtx->d_sockfd_b);
		}

		if (pCtx->p_ssl_b != NULL) {
			SSL_shutdown(pCtx->p_ssl_b);
		}
		if (pCtx->p_sockfd_b >= 0) {
			ntyCloseSocket(pCtx->p_sockfd_b);
		}
	}
	
	return pCtx;
}

static SSL* ntyPushSSLConnect(void *self, int sockfd, U8 mode) {
	nPushContext *pCtx = self;
	if (pCtx == NULL) return NULL;
	if (pCtx->ctx == NULL) return NULL;
	if (pCtx->pctx == NULL) return NULL;
	if (pCtx->ctx_b == NULL) return NULL;
	if (pCtx->pctx_b == NULL) return NULL;

	SSL *ssl = NULL;
	if (mode == NTY_PUSH_CLIENT_DEVELOPMENT) { //development
		ssl = SSL_new(pCtx->ctx);
	} else if (mode == NTY_PUSH_CLIENT_PRODUCTION) { //production
		ssl = SSL_new(pCtx->pctx);
	} else if (mode == NTY_PUSH_CLIENT_DEVELOPMENT_B) { //production
		ssl = SSL_new(pCtx->ctx_b);
	} else if (mode == NTY_PUSH_CLIENT_PRODUCTION_B) { //production
		ssl = SSL_new(pCtx->pctx_b);
	} else {
		return ssl;
	}
#if 0
	BIO *bio = BIO_new_socket(sockfd, BIO_NOCLOSE);

	SSL_set_bio(ssl, bio, bio);
#else
	int ret = SSL_set_fd(ssl, sockfd);
#endif
	if (SSL_connect(ssl) <= 0) {
		return NULL;
	}
	return ssl;
}

static int ntyPushTcpConnect(void *self, U8 mode) {
	nPushContext *pCtx = self;
	
	struct hostent *hp;
#if 0
	if (*(unsigned char*)(&pCtx->addr.sin_addr.s_addr) == 0x0) {
		ntylog("gethostbyname : %s\n", APPLE_HOST_DEVELOPMENT_NAME);
		
		if (!(hp = gethostbyname(APPLE_HOST_DEVELOPMENT_NAME))) {
			return NTY_RESULT_FAILED;
		}

		memset(&pCtx->addr, 0, sizeof(pCtx->addr));
		pCtx->addr.sin_addr = *(struct in_addr*)hp->h_addr_list[0];
		pCtx->addr.sin_family = AF_INET;
		pCtx->addr.sin_port = htons(APPLE_HOST_PORT);

		char *p = inet_ntoa(pCtx->addr.sin_addr);
		ntylog("address : %s\n", p);
	}
	
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		return NTY_RESULT_FAILED;
	}
	
	int ret = connect(sock, (struct sockaddr*)&pCtx->addr, sizeof(pCtx->addr));
	if (ret != 0) {
		return NTY_RESULT_FAILED;
	}
#else

	if (mode >= NTY_PUSH_CLIENT_COUNT) return NTY_RESULT_ERROR;

	//if (*(unsigned char*)(&addr[mode].sin_addr.s_addr) == 0x0) {
	
	if (mode == NTY_PUSH_CLIENT_DEVELOPMENT || mode == NTY_PUSH_CLIENT_DEVELOPMENT_B) {
		ntylog("gethostbyname : %s\n", APPLE_HOST_DEVELOPMENT_NAME);
		
		if (!(hp = gethostnameinfo(APPLE_HOST_DEVELOPMENT_NAME))) {
			return NTY_RESULT_FAILED;
		}
	} else {
		ntylog("gethostbyname : %s\n", APPLE_HOST_PRODUCTION_NAME);
		
		if (!(hp = gethostnameinfo(APPLE_HOST_PRODUCTION_NAME))) {
			return NTY_RESULT_FAILED;
		}
	}

	memset(&addr[mode], 0, sizeof(addr[mode]));
	addr[mode].sin_addr = *(struct in_addr*)hp->h_addr_list[0];
	addr[mode].sin_family = AF_INET;
	addr[mode].sin_port = htons(APPLE_HOST_PORT);

	char *p = inet_ntoa(addr[mode].sin_addr);
	ntylog("address : %s\n", p);
	//}
	
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		return NTY_RESULT_FAILED;
	}
	
	int ret = connect(sock, (struct sockaddr*)&addr[mode], sizeof(addr[mode]));
	if (ret != 0) {
		return NTY_RESULT_FAILED;
	}

#endif
	ntylog("Connect Success\n");
	return sock;
}

static int ntyVerifyConnection(SSL *ssl, const char *peername) {
	if (ssl == NULL || peername == NULL) return NTY_RESULT_ERROR;
	
	int result = SSL_get_verify_result(ssl);
	if (result != X509_V_OK) {
		fprintf(stderr, "WARNING ! ssl verify failed:%d\n", result);
		return NTY_RESULT_FAILED;
	}

	X509 *peer;
	char peer_CN[256] = {0};

	peer = SSL_get_peer_certificate(ssl);
	X509_NAME_get_text_by_NID(X509_get_subject_name(peer), NID_commonName, peer_CN, 255);
	if (strcmp(peer_CN, peername) != 0) {
		fprintf(stderr, "WARNING ! Server Name Doesn't match, got: %s, required: %s", peer_CN, peername);
	}
	
	return NTY_RESULT_SUCCESS;
}


int ntyPushNotify(void *self, C_DEVID gId, U32 type, U32 counter, U8 *msg, const U8 *token, U8 mode) {
	nPushContext *pCtx = self;
	if (pCtx == NULL) return NTY_RESULT_FAILED;

	ntylog("ntyPushNotify push begin..\n");
#if 1
	int ret = NTY_RESULT_FAILED;
	SSL *ssl = NULL;
	if ( mode == NTY_PUSH_CLIENT_DEVELOPMENT ) {
		ssl = pCtx->d_ssl;
		ntylog("ntyVerifyConnection --> APPLE_HOST_DEVELOPMENT_NAME\n");
		ret = ntyVerifyConnection(ssl, APPLE_HOST_DEVELOPMENT_NAME);
		if ( ret != NTY_RESULT_SUCCESS ) {
			ntylog("APPLE_HOST_DEVELOPMENT_NAME Verify failed\n");
	#if 1 
			ntyPushHandleRelease();
	#endif
			return NTY_RESULT_FAILED;
		}
	}  else if ( mode == NTY_PUSH_CLIENT_PRODUCTION ) {
		ssl = pCtx->p_ssl;
		ntylog("ntyVerifyConnection --> APPLE_HOST_PRODUCTION_NAME\n");
		ret = ntyVerifyConnection(ssl, APPLE_HOST_PRODUCTION_NAME);
		if (ret != NTY_RESULT_SUCCESS) {
			ntylog("APPLE_HOST_PRODUCTION_NAME Verify failed\n");	
	#if 1 
			ntyPushHandleRelease();
	#endif
			return NTY_RESULT_FAILED;
		}
		
	} else if (mode == NTY_PUSH_CLIENT_DEVELOPMENT_B) {
		ssl = pCtx->d_ssl_b;	
		ntylog("ntyVerifyConnection --> APPLE_HOST_DEVELOPMENT_NAME B\n");
		ret = ntyVerifyConnection(ssl, APPLE_HOST_DEVELOPMENT_NAME);
		if (ret != NTY_RESULT_SUCCESS) {
			ntylog("APPLE_HOST_DEVELOPMENT_NAME B Verify failed\n");
	#if 1 
			ntyPushHandleRelease();
	#endif
			return NTY_RESULT_FAILED;
		}

	} else if (mode == NTY_PUSH_CLIENT_PRODUCTION_B) {
		ssl = pCtx->p_ssl_b;
		ntylog("ntyVerifyConnection --> APPLE_HOST_PRODUCTION_NAME B\n");
		ret = ntyVerifyConnection(ssl, APPLE_HOST_PRODUCTION_NAME);
		if (ret != NTY_RESULT_SUCCESS) {
			ntylog("APPLE_HOST_PRODUCTION_NAME B Verify failed\n");	
	#if 1 
			ntyPushHandleRelease();
	#endif
			return NTY_RESULT_FAILED;
		}		
	}else{}
#else
	int sockfd = ntyPushTcpConnect(pCtx);
	if (sockfd < 0) {
		ntylog("failed to connect to host %s\n", strerror(errno));
		return NTY_RESULT_FAILED;
	}
	
	SSL *ssl = ntyPushSSLConnect(pCtx, sockfd, mode);
	if (ssl == NULL) {
		ntylog("ssl connect failed: %s\n", ERR_reason_error_string(ERR_get_error())); 
		ntyCloseSocket(sockfd);

		return NTY_RESULT_FAILED;
	}
#endif
	
	unsigned int msgid = 1;
	unsigned int expire = time(NULL) + 24 * 3600;
	ntylog("ntyPushNotify push msgid:%d,gId:%lld,type:%d,count:%d\n", msgid, gId, type, counter);
	if ( msg == NULL ) {
		msg = NTY_PUSH_MSG_CONTEXT;
	}
	ntylog("ntyPushNotify push msg:%s\n", msg);
	ret = ntySendMessage(gId, type, counter, ssl, token, msgid++, expire, msg, 1, "jg505.caf");
	if ( ret <= 0 ) {
		ntylog("send failed: %s\n", ERR_reason_error_string(ERR_get_error()));
	#if 1 
		ntyPushHandleRelease();
	#endif
		return NTY_RESULT_FAILED;

	} else {
		ntylog("send successfully\n");
	}
	ntylog("ntyPushNotify push end\n");

	return NTY_RESULT_SUCCESS;
}

static const nPushHandle ntyPushHandle = {
	sizeof(nPushContext),
	ntyPushContextCtor,
	ntyPushContextDtor,
	ntyPushNotify,
};


const void *pNtyPushHandle = &ntyPushHandle; 

static void *pPushHandle = NULL;

void *ntyPushHandleInstance(void) {
	if (pPushHandle == NULL) {
		void *pPush = New(pNtyPushHandle);
		if ((unsigned long)NULL != cmpxchg((void*)(&pPushHandle), (unsigned long)NULL, (unsigned long)pPush, WORD_WIDTH)) {
			Delete(pPush);
		}
		exiting = 0;
	}

	return pPushHandle;
}

void *ntyPushHandleGetInstance(void) {
	return pPushHandle;
}

void *ntyPushHandleRelease(void) {
	if (pPushHandle == NULL) return NULL;
	
	Delete(pPushHandle);
	pPushHandle = NULL;
	exiting = 1;
	
	return pPushHandle;
}

/*
 * mode : 	1 -> product
 * 			0 -> development
 */
int ntyPushNotifyHandle(void *self, C_DEVID gId, U32 type, U32 counter, U8 *msg, const U8 *token, U8 mode) {

	nPushHandle * const *pHandle = self;
	if (self && (*pHandle) && (*pHandle)->push) {
		return (*pHandle)->push(self, gId, type, counter, msg, token, mode);
	}
	
	return NTY_RESULT_ERROR;
}

#if 0
//²âÊÔÊ±ºòÐèÒªÐÞ¸Ä NattyAbstractClass.h #define NTY_DEBUG 	2
//gcc -o NattyPush NattyPush.c NattyAbstractClass.c NattyUtils.c NattyLetter.c -lssl -lcrypto -lpthread -I ../include/   
//gcc -o NattyPush NattyPush.c NattyAbstractClass.c -lssl -lcrypto -lpthread -I ../include/

int main(void) {
	void *pushHandle = ntyPushHandleInstance();

	char *msg = "abcd";
	const char *token = "4775d9d6f996f69a4d56bf054f8ca4ab56814498dbf7387abbc79bf4b374a020";
	//const char *token = "ee088b72e57c751b6c04fb40c371dee35a82a989fe1f9e358a7ee15a2084b5bd";
	//const char *token = "2f88b3df02e08a11e7777479943089aacffb2f8750aaa08a98a5f5978081d84a";
	C_DEVID devId = 0x355637053172771;
	U32 type = 1;

	//2f88b3df02e08a11e7777479943089aacffb2f8750aaa08a98a5f5978081d84a
	//ec49a6b5bcad57279a0fe08f8aeab941
	//015bafdb5a846a08cbae1d78959a461d 6b9c4e46f6a18dc36b4e9a191487bc0d
	//015bafdb5a846a08cbae1d78959a461d

	int ret = ntyPushNotifyHandle(pushHandle, devId, type, 5, NTY_PUSH_POLICE_MSG_CONTEXT, token, NTY_PUSH_CLIENT_DEVELOPMENT);

	getchar();

	int i = 0;

	for (i = 0;i < 10;i ++) {
		ntyPushNotifyHandle(pushHandle, devId, type, i+1, NTY_PUSH_POLICE_MSG_CONTEXT, token, NTY_PUSH_CLIENT_PRODUCTION);
	}

	getchar();
}
#endif

