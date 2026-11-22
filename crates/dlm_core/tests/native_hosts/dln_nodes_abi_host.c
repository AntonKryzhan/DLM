#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "dln_nodes.a.h"
typedef struct { uint32_t family,port,scope;uint8_t bytes[16]; } address;
typedef struct { address address;uint32_t profiles,priority; } locator;
typedef struct { uint8_t identity[32],root[32],key[32];uint64_t epoch,sequence,not_before,key_expires,issued,expires;uint32_t locator_count;locator locators[8];uint8_t delegation[64],signature[64]; } record;
typedef struct { uint8_t id[32];uint64_t size;uint32_t leaves,reserved; } child;
typedef struct { uint8_t root[32],leaf[32];uint64_t total_size,leaf_size;uint32_t index,leaf_count,depth,reserved;child siblings[32];uint32_t sides[32]; } proof;
typedef struct { size_t count;int32_t status,system; } host_io;
extern host_io dlm_net_crypto_shutdown(void);
extern host_io dlm_net_provider_shutdown(void);
#define FIELD(type,field) assert(dlm_native_offset_##type##_##field()==offsetof(dn_r_##type,f_##field))
int main(void) {
    assert(sizeof(record)==sizeof(dn_r_SignedLocatorRecord));assert(sizeof(proof)==sizeof(dn_r_MerkleProof));assert(sizeof(locator)==sizeof(dn_r_NetworkLocator));assert(sizeof(child)==sizeof(dn_r_ContentChild));
    assert(dlm_native_sizeof_SignedLocatorRecord()==sizeof(record) && dlm_native_alignof_SignedLocatorRecord()==_Alignof(record));assert(dlm_native_sizeof_MerkleProof()==sizeof(proof));FIELD(SignedLocatorRecord,identity);FIELD(SignedLocatorRecord,locators);FIELD(SignedLocatorRecord,signature);FIELD(MerkleProof,siblings);FIELD(MerkleProof,sides);
    dn_r_SignedLocatorRecord a={0};a.f_sequence=UINT64_MAX;a.f_locators.data[7].f_address.f_bytes.data[15]=255;a.f_signature.data[63]=128;dn_r_SignedLocatorRecord b=nodes_record_abi(a);assert(!memcmp(&a,&b,sizeof a));dn_r_MerkleProof p={0};p.f_leaf_size=UINT64_MAX;p.f_siblings.data[31].f_leaves=4096;p.f_sides.data[31]=1;dn_r_MerkleProof q=nodes_proof_abi(p);assert(!memcmp(&p,&q,sizeof p));
    assert(nodes_identity()==1);assert(nodes_content()==20);assert(nodes_discovery()==1);assert(!dlm_native_last_error());nodes_abort_cleanup();assert(dlm_native_last_error());assert(!dlm_native_live_allocations());dlm_native_clear_error();assert(!dlm_net_crypto_shutdown().status);assert(!dlm_net_provider_shutdown().status);assert(dlm_native_runtime_shutdown());puts("DLM identity discovery content ownership and C ABI OK");return 0;
}
