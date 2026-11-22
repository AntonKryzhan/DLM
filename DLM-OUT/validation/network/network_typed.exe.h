#ifndef DLM_NATIVE_APPLICATION_H
#define DLM_NATIVE_APPLICATION_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
#include <atomic>
#define DN_HEADER_ATOMIC(T) std::atomic<T>
#define DN_HEADER_ASSERT static_assert
#else
#include <stdatomic.h>
#define DN_HEADER_ATOMIC(T) _Atomic(T)
#define DN_HEADER_ASSERT _Static_assert
#endif
typedef struct dn_utf8 {const uint8_t *data;size_t len;} dn_utf8;
typedef struct dn_window dn_window;
typedef struct dn_r_NetText dn_r_NetText;
typedef struct dn_r_NetIo dn_r_NetIo;
typedef struct dn_r_NetOpened dn_r_NetOpened;
typedef struct dn_r_NetAddress dn_r_NetAddress;
typedef struct dn_r_NetResolved dn_r_NetResolved;
typedef struct dn_r_NetDatagram dn_r_NetDatagram;
typedef struct dn_r_NetWebSocketIo dn_r_NetWebSocketIo;
typedef struct dn_r_NetPoll dn_r_NetPoll;
typedef struct dn_r_NetworkError dn_r_NetworkError;
typedef struct dn_r_NetworkTransfer dn_r_NetworkTransfer;
typedef struct dn_r_TcpSocket dn_r_TcpSocket;
typedef struct dn_r_TcpListener dn_r_TcpListener;
typedef struct dn_r_UdpSocket dn_r_UdpSocket;
typedef struct dn_r_TlsSocket dn_r_TlsSocket;
typedef struct dn_r_SocketAddress dn_r_SocketAddress;
typedef struct dn_r_SocketAddressResult dn_r_SocketAddressResult;
typedef struct dn_r_SocketDatagram dn_r_SocketDatagram;
typedef struct dn_r_SocketOptions dn_r_SocketOptions;
typedef struct dn_r_NativeSocketOptions dn_r_NativeSocketOptions;
typedef struct dn_r_SocketOptionsResult dn_r_SocketOptionsResult;
typedef struct dn_r_NetworkReady dn_r_NetworkReady;
typedef struct dn_r_ProtocolValue dn_r_ProtocolValue;
typedef struct dn_r_ProtocolFrame dn_r_ProtocolFrame;
typedef struct dn_r_Frame32Decoder dn_r_Frame32Decoder;
typedef struct dn_r_Frame32Step dn_r_Frame32Step;
#ifdef __cplusplus
extern "C" {
#endif
const char *dlm_native_last_error(void);
void dlm_native_clear_error(void);
uint32_t dlm_native_abi_version(void);
uint64_t dlm_native_thread_init(void);
uint64_t dlm_native_allocations(void);
uint64_t dlm_native_frees(void);
int dlm_native_cpu_profile(void);
uint64_t dlm_native_live_allocations(void);
bool dlm_native_runtime_init(void);
bool dlm_native_runtime_shutdown(void);
size_t dlm_native_sizeof_NetText(void);
size_t dlm_native_alignof_NetText(void);
uint32_t dlm_native_version_NetText(void);
size_t dlm_native_offset_NetText_data(void);
size_t dlm_native_offset_NetText_count(void);
size_t dlm_native_sizeof_NetIo(void);
size_t dlm_native_alignof_NetIo(void);
uint32_t dlm_native_version_NetIo(void);
size_t dlm_native_offset_NetIo_count(void);
size_t dlm_native_offset_NetIo_status(void);
size_t dlm_native_offset_NetIo_system(void);
size_t dlm_native_sizeof_NetOpened(void);
size_t dlm_native_alignof_NetOpened(void);
uint32_t dlm_native_version_NetOpened(void);
size_t dlm_native_offset_NetOpened_handle(void);
size_t dlm_native_offset_NetOpened_status(void);
size_t dlm_native_offset_NetOpened_system(void);
size_t dlm_native_sizeof_NetAddress(void);
size_t dlm_native_alignof_NetAddress(void);
uint32_t dlm_native_version_NetAddress(void);
size_t dlm_native_offset_NetAddress_family(void);
size_t dlm_native_offset_NetAddress_port(void);
size_t dlm_native_offset_NetAddress_scope(void);
size_t dlm_native_offset_NetAddress_bytes(void);
size_t dlm_native_sizeof_NetResolved(void);
size_t dlm_native_alignof_NetResolved(void);
uint32_t dlm_native_version_NetResolved(void);
size_t dlm_native_offset_NetResolved_address(void);
size_t dlm_native_offset_NetResolved_status(void);
size_t dlm_native_offset_NetResolved_system(void);
size_t dlm_native_sizeof_NetDatagram(void);
size_t dlm_native_alignof_NetDatagram(void);
uint32_t dlm_native_version_NetDatagram(void);
size_t dlm_native_offset_NetDatagram_io(void);
size_t dlm_native_offset_NetDatagram_peer(void);
size_t dlm_native_sizeof_NetWebSocketIo(void);
size_t dlm_native_alignof_NetWebSocketIo(void);
uint32_t dlm_native_version_NetWebSocketIo(void);
size_t dlm_native_offset_NetWebSocketIo_io(void);
size_t dlm_native_offset_NetWebSocketIo_offset(void);
size_t dlm_native_offset_NetWebSocketIo_remaining(void);
size_t dlm_native_offset_NetWebSocketIo_flags(void);
size_t dlm_native_sizeof_NetPoll(void);
size_t dlm_native_alignof_NetPoll(void);
uint32_t dlm_native_version_NetPoll(void);
size_t dlm_native_offset_NetPoll_socket(void);
size_t dlm_native_offset_NetPoll_events(void);
size_t dlm_native_offset_NetPoll_ready(void);
size_t dlm_native_offset_NetPoll_status(void);
size_t dlm_native_offset_NetPoll_system(void);
size_t dlm_native_sizeof_NetworkError(void);
size_t dlm_native_alignof_NetworkError(void);
uint32_t dlm_native_version_NetworkError(void);
size_t dlm_native_offset_NetworkError_kind(void);
size_t dlm_native_offset_NetworkError_system(void);
size_t dlm_native_offset_NetworkError_operation(void);
size_t dlm_native_sizeof_NetworkTransfer(void);
size_t dlm_native_alignof_NetworkTransfer(void);
uint32_t dlm_native_version_NetworkTransfer(void);
size_t dlm_native_offset_NetworkTransfer_count(void);
size_t dlm_native_offset_NetworkTransfer_error(void);
size_t dlm_native_sizeof_TcpSocket(void);
size_t dlm_native_alignof_TcpSocket(void);
uint32_t dlm_native_version_TcpSocket(void);
size_t dlm_native_offset_TcpSocket_inner(void);
size_t dlm_native_offset_TcpSocket_opened_by(void);
size_t dlm_native_sizeof_TcpListener(void);
size_t dlm_native_alignof_TcpListener(void);
uint32_t dlm_native_version_TcpListener(void);
size_t dlm_native_offset_TcpListener_inner(void);
size_t dlm_native_sizeof_UdpSocket(void);
size_t dlm_native_alignof_UdpSocket(void);
uint32_t dlm_native_version_UdpSocket(void);
size_t dlm_native_offset_UdpSocket_inner(void);
size_t dlm_native_sizeof_TlsSocket(void);
size_t dlm_native_alignof_TlsSocket(void);
uint32_t dlm_native_version_TlsSocket(void);
size_t dlm_native_offset_TlsSocket_inner(void);
size_t dlm_native_sizeof_SocketAddress(void);
size_t dlm_native_alignof_SocketAddress(void);
uint32_t dlm_native_version_SocketAddress(void);
size_t dlm_native_offset_SocketAddress_value(void);
size_t dlm_native_sizeof_SocketAddressResult(void);
size_t dlm_native_alignof_SocketAddressResult(void);
uint32_t dlm_native_version_SocketAddressResult(void);
size_t dlm_native_offset_SocketAddressResult_address(void);
size_t dlm_native_offset_SocketAddressResult_error(void);
size_t dlm_native_sizeof_SocketDatagram(void);
size_t dlm_native_alignof_SocketDatagram(void);
uint32_t dlm_native_version_SocketDatagram(void);
size_t dlm_native_offset_SocketDatagram_transfer(void);
size_t dlm_native_offset_SocketDatagram_peer(void);
size_t dlm_native_sizeof_SocketOptions(void);
size_t dlm_native_alignof_SocketOptions(void);
uint32_t dlm_native_version_SocketOptions(void);
size_t dlm_native_offset_SocketOptions_no_delay(void);
size_t dlm_native_offset_SocketOptions_keep_alive(void);
size_t dlm_native_offset_SocketOptions_send_buffer(void);
size_t dlm_native_offset_SocketOptions_receive_buffer(void);
size_t dlm_native_sizeof_NativeSocketOptions(void);
size_t dlm_native_alignof_NativeSocketOptions(void);
uint32_t dlm_native_version_NativeSocketOptions(void);
size_t dlm_native_offset_NativeSocketOptions_options(void);
size_t dlm_native_offset_NativeSocketOptions_status(void);
size_t dlm_native_offset_NativeSocketOptions_system(void);
size_t dlm_native_sizeof_SocketOptionsResult(void);
size_t dlm_native_alignof_SocketOptionsResult(void);
uint32_t dlm_native_version_SocketOptionsResult(void);
size_t dlm_native_offset_SocketOptionsResult_options(void);
size_t dlm_native_offset_SocketOptionsResult_error(void);
size_t dlm_native_sizeof_NetworkReady(void);
size_t dlm_native_alignof_NetworkReady(void);
uint32_t dlm_native_version_NetworkReady(void);
size_t dlm_native_offset_NetworkReady_events(void);
size_t dlm_native_offset_NetworkReady_error(void);
size_t dlm_native_sizeof_ProtocolValue(void);
size_t dlm_native_alignof_ProtocolValue(void);
uint32_t dlm_native_version_ProtocolValue(void);
size_t dlm_native_offset_ProtocolValue_value(void);
size_t dlm_native_offset_ProtocolValue_next(void);
size_t dlm_native_offset_ProtocolValue_status(void);
size_t dlm_native_sizeof_ProtocolFrame(void);
size_t dlm_native_alignof_ProtocolFrame(void);
uint32_t dlm_native_version_ProtocolFrame(void);
size_t dlm_native_offset_ProtocolFrame_payload(void);
size_t dlm_native_offset_ProtocolFrame_count(void);
size_t dlm_native_offset_ProtocolFrame_next(void);
size_t dlm_native_offset_ProtocolFrame_status(void);
size_t dlm_native_sizeof_Frame32Decoder(void);
size_t dlm_native_alignof_Frame32Decoder(void);
uint32_t dlm_native_version_Frame32Decoder(void);
size_t dlm_native_offset_Frame32Decoder_maximum(void);
size_t dlm_native_offset_Frame32Decoder_header(void);
size_t dlm_native_offset_Frame32Decoder_prefix_count(void);
size_t dlm_native_offset_Frame32Decoder_remaining(void);
size_t dlm_native_offset_Frame32Decoder_fault(void);
size_t dlm_native_sizeof_Frame32Step(void);
size_t dlm_native_alignof_Frame32Step(void);
uint32_t dlm_native_version_Frame32Step(void);
size_t dlm_native_offset_Frame32Step_consumed(void);
size_t dlm_native_offset_Frame32Step_payload_offset(void);
size_t dlm_native_offset_Frame32Step_payload_count(void);
size_t dlm_native_offset_Frame32Step_remaining(void);
size_t dlm_native_offset_Frame32Step_complete(void);
size_t dlm_native_offset_Frame32Step_status(void);
#ifdef __cplusplus
}
#endif
#undef DN_HEADER_ATOMIC
#undef DN_HEADER_ASSERT
#endif
