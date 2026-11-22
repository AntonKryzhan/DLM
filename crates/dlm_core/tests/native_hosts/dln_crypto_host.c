#include <assert.h>
#include "../../src/native/network.c"
#if defined(DLM_CRYPTO_CHECK_HEADERS)
#include <gnutls/crypto.h>
#include <nettle/curve25519.h>
_Static_assert(sizeof(dnc_datum)==sizeof(gnutls_datum_t),"GnuTLS datum size");
_Static_assert(_Alignof(dnc_datum)==_Alignof(gnutls_datum_t),"GnuTLS datum alignment");
_Static_assert(offsetof(dnc_datum,size)==offsetof(gnutls_datum_t,size),"GnuTLS datum fields");
_Static_assert(GNUTLS_CIPHER_AES_256_GCM==11 && GNUTLS_MAC_SHA256==6 && GNUTLS_DIG_SHA256==6,"GnuTLS identifiers");
_Static_assert(GNUTLS_E_DECRYPTION_FAILED==-24 && CURVE25519_SIZE==32 && NETTLE_CURVE25519_RFC7748==1,"crypto provider contract");
#endif
static void watch_wipe(void *memory,size_t count);
#if defined(DLM_CRYPTO_WIPE_AUDIT)
static void *watched;static size_t watched_count;
void __real_free(void *memory);
void __wrap_free(void *memory) {
    if(memory && memory==watched) {
        const volatile uint8_t *bytes=(const volatile uint8_t*)memory;
        for(size_t i=0;i<watched_count;i++) assert(bytes[i]==0);
        watched=NULL;watched_count=0;
    }
    __real_free(memory);
}
static void watch_wipe(void *memory,size_t count) { assert(!watched);watched=memory;watched_count=count; }
#else
static void watch_wipe(void *memory,size_t count) { (void)memory;(void)count; }
#endif
static unsigned int hex_digit(char c) { return c>='0' && c<='9'?(unsigned)(c-'0'):(unsigned)(c-'a'+10); }
static void unhex(const char *source,uint8_t *target,size_t count) {
    assert(strlen(source)==count*2);for(size_t i=0;i<count;i++) target[i]=(uint8_t)((hex_digit(source[i*2])<<4)|hex_digit(source[i*2+1]));
}
static void test_hash_hmac_hkdf(void) {
    uint8_t output[64],expected[64],key[22];memset(output,0xa5,sizeof output);
    unhex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",expected,32);
    assert(dlm_net_crypto_hash(NULL,(const uint8_t*)"abc",3,output,64).status==0);assert(!memcmp(output,expected,32) && output[32]==0xa5);
    memset(key,0x0b,20);dnn_open secret=dlm_net_crypto_secret(key,20,1,false);assert(secret.status==0);
    unhex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",expected,32);
    assert(dlm_net_crypto_hmac(secret.handle,(const uint8_t*)"Hi There",8,output,64).status==0);assert(!memcmp(output,expected,32));dlm_net_crypto_close(secret.handle);
    memset(output,0xa5,sizeof output);assert(dlm_net_crypto_hmac(NULL,(const uint8_t*)"Hi There",8,output,64).status==DNN_INVALID);
    for(size_t i=0;i<sizeof output;i++) assert(output[i]==0xa5);
    memset(key,0x0b,22);uint8_t salt[13],info[10];for(size_t i=0;i<13;i++) salt[i]=(uint8_t)i;for(size_t i=0;i<10;i++) info[i]=(uint8_t)(0xf0+i);
    secret=dlm_net_crypto_secret(key,22,1,false);assert(secret.status==0);
    dnn_open derived=dlm_net_crypto_hkdf(secret,salt,13,info,10,42);assert(derived.status==0);
    unhex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865",expected,42);
    dnc_secret *material=(dnc_secret*)derived.handle;assert(material->length==42 && !memcmp(material->bytes,expected,42));
    watch_wipe(derived.handle,sizeof(dnc_secret)+42);dlm_net_crypto_close(derived.handle);
}
static void test_aes_gcm_known_answer(void) {
    uint8_t key[32]={0},iv[12]={0},plain[16]={0},record[32],expected[32];dnc_datum value={key,32};void *cipher=NULL;
    assert(dnc.aead_new(&cipher,11,&value)==0);size_t length=sizeof record;
    assert(dnc.encrypt(cipher,iv,12,NULL,0,16,plain,16,record,&length)==0 && length==32);
    unhex("cea7403d4d606b6e074ec5d3baf39d18d0d1c8a799996bf0265b98b5d48ab919",expected,32);
    assert(!memcmp(record,expected,32));dnc.aead_close(cipher);
}
static void test_x25519_and_record_lifecycle(void) {
    uint8_t a[32],b[32],ap[32],bp[32],expected[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",a,32);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",b,32);
    dnn_open alice=dlm_net_crypto_secret(a,32,2,false),bob=dlm_net_crypto_secret(b,32,2,false);assert(alice.status==0 && bob.status==0);
    assert(dlm_net_crypto_public(alice.handle,ap,32).status==0 && dlm_net_crypto_public(bob.handle,bp,32).status==0);
    unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a",expected,32);assert(!memcmp(ap,expected,32));
    unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f",expected,32);assert(!memcmp(bp,expected,32));
    dnn_open left=dlm_net_crypto_agree(alice,bp,32),right=dlm_net_crypto_agree(bob,ap,32);assert(left.status==0 && right.status==0);
    unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742",expected,32);
    assert(!memcmp(((dnc_secret*)left.handle)->bytes,expected,32) && !memcmp(((dnc_secret*)right.handle)->bytes,expected,32));
    const uint8_t context[]="KAT authenticated transcript";
    dnn_open client=dlm_net_crypto_channel(left,ap,32,context,sizeof context-1,1,8,64,10000);
    dnn_open server=dlm_net_crypto_channel(right,ap,32,context,sizeof context-1,2,8,64,10000);assert(client.status==0 && server.status==0);
    assert(dlm_net_crypto_shutdown().status==DNN_INVALID);
    uint8_t record[64],plain[8],message[3]={0,255,128};memset(plain,0xa5,8);
    dnn_io sealed=dlm_net_crypto_seal(client.handle,message,3,context,sizeof context-1,record,64);assert(sealed.status==0 && sealed.count==35);
    record[34]^=1;dnn_io opened=dlm_net_crypto_open(server.handle,record,35,context,sizeof context-1,plain,8);
    assert(opened.status==DNC_AUTH && !opened.count);for(size_t i=0;i<8;i++) assert(plain[i]==0xa5);
    record[34]^=1;opened=dlm_net_crypto_open(server.handle,record,35,context,sizeof context-1,plain,8);
    assert(opened.status==0 && opened.count==3 && !memcmp(plain,message,3) && plain[3]==0xa5);
    assert(dlm_net_crypto_open(server.handle,record,35,context,sizeof context-1,plain,8).status==DNC_REPLAY);
    sealed=dlm_net_crypto_seal(server.handle,NULL,0,NULL,0,record,64);assert(sealed.status==0 && sealed.count==32);
    opened=dlm_net_crypto_open(client.handle,record,32,NULL,0,NULL,0);assert(opened.status==0 && !opened.count);
    watch_wipe(client.handle,sizeof(dnc_channel));dlm_net_crypto_close(client.handle);dlm_net_crypto_close(server.handle);
    memset(bp,0,32);alice=dlm_net_crypto_secret(a,32,2,false);left=dlm_net_crypto_agree(alice,bp,32);assert(left.status==DNC_AUTH && !left.handle);
}
int main(void) {
    if(dlm_net_crypto_capabilities()!=15) { fprintf(stderr,"Crypto backend unavailable: GnuTLS >=3.6.13 and Nettle/Hogweed are required. Check DLM_LIBGNUTLS and DLM_LIBHOGWEED.\n");return 1; }
    test_hash_hmac_hkdf();test_aes_gcm_known_answer();test_x25519_and_record_lifecycle();
    assert(dlm_net_crypto_shutdown().status==0);assert(dlm_net_provider_shutdown().status==0);
    puts("crypto RFC HKDF X25519 AES-GCM hash and lifetime vectors OK");return 0;
}
