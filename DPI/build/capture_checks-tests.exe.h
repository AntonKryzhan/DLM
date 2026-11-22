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
typedef struct dn_r_Host dn_r_Host;
typedef struct dn_r_Gene dn_r_Gene;
typedef struct dn_r_Individual dn_r_Individual;
typedef struct dn_r_Options dn_r_Options;
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
size_t dlm_native_sizeof_Host(void);
size_t dlm_native_alignof_Host(void);
uint32_t dlm_native_version_Host(void);
size_t dlm_native_offset_Host_bytes(void);
size_t dlm_native_offset_Host_length(void);
size_t dlm_native_sizeof_Gene(void);
size_t dlm_native_alignof_Gene(void);
uint32_t dlm_native_version_Gene(void);
size_t dlm_native_offset_Gene_reverse(void);
size_t dlm_native_offset_Gene_cut1(void);
size_t dlm_native_offset_Gene_cut2(void);
size_t dlm_native_offset_Gene_overlap(void);
size_t dlm_native_offset_Gene_guard(void);
size_t dlm_native_offset_Gene_repeats(void);
size_t dlm_native_offset_Gene_gap(void);
size_t dlm_native_offset_Gene_template_(void);
size_t dlm_native_sizeof_Individual(void);
size_t dlm_native_alignof_Individual(void);
uint32_t dlm_native_version_Individual(void);
size_t dlm_native_offset_Individual_gene(void);
size_t dlm_native_offset_Individual_fitness(void);
size_t dlm_native_offset_Individual_trials(void);
size_t dlm_native_sizeof_Options(void);
size_t dlm_native_alignof_Options(void);
uint32_t dlm_native_version_Options(void);
size_t dlm_native_offset_Options_aggressive(void);
size_t dlm_native_offset_Options_fake(void);
size_t dlm_native_offset_Options_http(void);
size_t dlm_native_offset_Options_sni(void);
size_t dlm_native_offset_Options_reverse(void);
size_t dlm_native_offset_Options_ech(void);
size_t dlm_native_offset_Options_adaptive(void);
size_t dlm_native_offset_Options_learn(void);
size_t dlm_native_offset_Options_all_ports(void);
size_t dlm_native_offset_Options_game(void);
size_t dlm_native_offset_Options_whitelist(void);
size_t dlm_native_offset_Options_quic(void);
size_t dlm_native_offset_Options_probe_ms(void);
size_t dlm_native_offset_Options_fake_mode(void);
size_t dlm_native_offset_Options_split_mode(void);
size_t dlm_native_offset_Options_strategy(void);
size_t dlm_native_offset_Options_first_packets(void);
size_t dlm_native_offset_Options_jitter_ms(void);
size_t dlm_native_offset_Options_ttl_margin(void);
size_t dlm_native_offset_Options_ttl_spread(void);
size_t dlm_native_offset_Options_ttl_max(void);
size_t dlm_native_offset_Options_min_hops(void);
size_t dlm_native_offset_Options_debug(void);
size_t dlm_native_offset_Options_help(void);
size_t dlm_native_offset_Options_error(void);
size_t dlm_native_offset_Options_auto_strategy(void);
size_t dlm_native_offset_Options_forced_arm(void);
size_t dlm_native_offset_Options_server_window(void);
size_t dlm_native_offset_Options_udp_obfuscate(void);
size_t dlm_native_offset_Options_diagnostic(void);
size_t dlm_native_offset_Options_handshake_timeout(void);
size_t dlm_native_offset_Options_safe_automatic(void);
size_t dlm_native_offset_Options_telegram(void);
size_t dlm_native_offset_Options_cloud_cdn(void);
size_t dlm_native_offset_Options_driver_check(void);
size_t dlm_native_offset_Options_driver_report(void);
size_t dlm_native_offset_Options_parent_pid(void);
size_t dlm_native_offset_Options_gene_mode(void);
size_t dlm_native_offset_Options_gene(void);
size_t dlm_native_offset_Options_gene_target(void);
size_t dlm_native_offset_Options_trial_tag(void);
size_t dlm_native_offset_Options_paths(void);
#ifdef __cplusplus
}
#endif
#undef DN_HEADER_ATOMIC
#undef DN_HEADER_ASSERT
#endif
