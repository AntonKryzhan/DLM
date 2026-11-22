/* Compile with installed libcurl development headers to independently check
 * the public ABI used by the dynamically loaded provider. No network access.
 */
#include <curl/curl.h>
#include <curl/websockets.h>
#include "../../src/native/network.c"
#define ABI_VALUE(name,value) _Static_assert((name)==(value),#name " ABI changed")
ABI_VALUE(CURLOPT_URL,10002);
ABI_VALUE(CURLOPT_PROXY,10004);
ABI_VALUE(CURLOPT_WRITEFUNCTION,20011);
ABI_VALUE(CURLOPT_WRITEDATA,10001);
ABI_VALUE(CURLOPT_HTTPHEADER,10023);
ABI_VALUE(CURLOPT_HEADERDATA,10029);
ABI_VALUE(CURLOPT_SSLVERSION,32);
ABI_VALUE(CURLOPT_CUSTOMREQUEST,10036);
ABI_VALUE(CURLOPT_NOPROGRESS,43);
ABI_VALUE(CURLOPT_NOBODY,44);
ABI_VALUE(CURLOPT_FOLLOWLOCATION,52);
ABI_VALUE(CURLOPT_SSL_VERIFYPEER,64);
ABI_VALUE(CURLOPT_CAINFO,10065);
ABI_VALUE(CURLOPT_HEADERFUNCTION,20079);
ABI_VALUE(CURLOPT_SSL_VERIFYHOST,81);
ABI_VALUE(CURLOPT_HTTP_VERSION,84);
ABI_VALUE(CURLOPT_NOSIGNAL,99);
ABI_VALUE(CURLOPT_POSTFIELDSIZE_LARGE,30120);
ABI_VALUE(CURLOPT_CONNECT_ONLY,141);
ABI_VALUE(CURLOPT_TIMEOUT_MS,155);
ABI_VALUE(CURLOPT_CONNECTTIMEOUT_MS,156);
ABI_VALUE(CURLOPT_COPYPOSTFIELDS,10165);
ABI_VALUE(CURLOPT_XFERINFOFUNCTION,20219);
ABI_VALUE(CURLOPT_XFERINFODATA,10057);
ABI_VALUE(CURLOPT_PINNEDPUBLICKEY,10230);
ABI_VALUE(CURLOPT_PROTOCOLS_STR,10318);
ABI_VALUE(CURLOPT_ECH,10325);
ABI_VALUE(CURLINFO_RESPONSE_CODE,0x200000+2);
ABI_VALUE(CURLINFO_HTTP_VERSION,0x200000+46);
ABI_VALUE(CURLINFO_ACTIVESOCKET,0x500000+44);
ABI_VALUE(CURL_SSLVERSION_TLSv1_3,7);
ABI_VALUE(CURL_HTTP_VERSION_1_1,2);
ABI_VALUE(CURL_HTTP_VERSION_2_0,3);
ABI_VALUE(CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE,5);
ABI_VALUE(CURL_HTTP_VERSION_3,30);
ABI_VALUE(CURL_HTTP_VERSION_3ONLY,31);
ABI_VALUE(CURLWS_TEXT,1);
ABI_VALUE(CURLWS_BINARY,2);
ABI_VALUE(CURLWS_CONT,4);
ABI_VALUE(CURLWS_CLOSE,8);
ABI_VALUE(CURLWS_PING,16);
ABI_VALUE(CURLWS_PONG,64);
ABI_VALUE(CURLE_AGAIN,81);
ABI_VALUE(CURLE_OPERATION_TIMEDOUT,28);
ABI_VALUE(CURLE_ABORTED_BY_CALLBACK,42);
ABI_VALUE(CURLE_UNKNOWN_OPTION,48);
ABI_VALUE(CURLE_SSL_PINNEDPUBKEYNOTMATCH,90);
ABI_VALUE(CURL_VERSION_SSL,1<<2);
ABI_VALUE(CURL_VERSION_HTTP2,1<<16);
ABI_VALUE(CURL_VERSION_HTTP3,1<<25);
_Static_assert(sizeof(curl_off_t)==sizeof(dnn_curl_off),"curl_off_t");
_Static_assert(sizeof(struct curl_ws_frame)==sizeof(dnn_curl_frame),"WebSocket metadata");
_Static_assert(offsetof(struct curl_ws_frame,offset)==offsetof(dnn_curl_frame,offset),"WebSocket offset");
_Static_assert(offsetof(struct curl_ws_frame,bytesleft)==offsetof(dnn_curl_frame,bytesleft),"WebSocket remaining");
_Static_assert(offsetof(struct curl_ws_frame,len)==offsetof(dnn_curl_frame,len),"WebSocket length");
_Static_assert(offsetof(curl_version_info_data,protocols)==offsetof(dnn_curl_version,protocols),"version prefix");
_Static_assert(offsetof(curl_version_info_data,features)==offsetof(dnn_curl_version,features),"feature prefix");
_Static_assert(sizeof(dnn_curl_version)==offsetof(curl_version_info_data,ares),"version prefix length");
int main(void) { return 0; }
